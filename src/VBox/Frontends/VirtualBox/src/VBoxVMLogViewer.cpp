/* $Id: VBoxVMLogViewer.cpp $ */
/** @file
 *
 * VBox frontends: Qt4 GUI ("VirtualBox"):
 * VBoxVMLogViewer class implementation
 */

/*
 * Copyright (C) 2006-2008 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifdef VBOX_WITH_PRECOMPILED_HEADERS
# include "precomp.h"
#else  /* !VBOX_WITH_PRECOMPILED_HEADERS */
#include "QITabWidget.h"
#include "UIIconPool.h"
#include "UISpecialControls.h"
#include "VBoxGlobal.h"
#include "UIMessageCenter.h"
#include "VBoxUtils.h"
#include "VBoxVMLogViewer.h"

/* Qt includes */
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStyle>
#include <QTextEdit>
#endif /* !VBOX_WITH_PRECOMPILED_HEADERS */

VBoxVMLogViewer::LogViewersMap VBoxVMLogViewer::mSelfArray = LogViewersMap();

void VBoxVMLogViewer::createLogViewer (QWidget *aCenterWidget, CMachine &aMachine)
{
    if (!mSelfArray.contains (aMachine.GetName()))
    {
        /* Creating new log viewer if there is no one existing */
#ifdef Q_WS_MAC
        VBoxVMLogViewer *lv = new VBoxVMLogViewer (aCenterWidget, Qt::Window, aMachine);
#else /* Q_WS_MAC */
        VBoxVMLogViewer *lv = new VBoxVMLogViewer (NULL, Qt::Window, aMachine);
#endif /* Q_WS_MAC */

        lv->centerAccording (aCenterWidget);
        connect (vboxGlobal().mainWindow(), SIGNAL (closing()), lv, SLOT (close()));
        lv->setAttribute (Qt::WA_DeleteOnClose);
        mSelfArray [aMachine.GetName()] = lv;
    }

    VBoxVMLogViewer *viewer = mSelfArray [aMachine.GetName()];
    viewer->show();
    viewer->raise();
    viewer->setWindowState (viewer->windowState() & ~Qt::WindowMinimized);
    viewer->activateWindow();
}


VBoxVMLogViewer::VBoxVMLogViewer (QWidget *aParent,
                                  Qt::WindowFlags aFlags,
                                  const CMachine &aMachine)
    : QIWithRetranslateUI2<QIMainDialog> (aParent, aFlags)
    , mIsPolished (false)
    , mFirstRun (true)
    , mMachine (aMachine)
{
    /* Apply UI decorations */
    Ui::VBoxVMLogViewer::setupUi (this);

    /* Apply window icons */
    setWindowIcon(UIIconPool::iconSetFull(QSize (32, 32), QSize (16, 16),
                                          ":/vm_show_logs_32px.png", ":/show_logs_16px.png"));

    /* Enable size grip without using a status bar. */
    setSizeGripEnabled (true);

    /* Logs list creation */
    mLogList = new QITabWidget (mLogsFrame);
    QVBoxLayout *logsFrameLayout = new QVBoxLayout (mLogsFrame);
    logsFrameLayout->setContentsMargins (0, 0, 0, 0);
    logsFrameLayout->addWidget (mLogList);

    connect (mLogList, SIGNAL (currentChanged (int)),
             this, SLOT (currentLogPageChanged (int)));

    /* Search panel creation */
    mSearchPanel = new VBoxLogSearchPanel (mLogsFrame, this);
    logsFrameLayout->addWidget (mSearchPanel);
    mSearchPanel->hide();

    /* Add missing buttons & retrieve standard buttons */
    mBtnHelp = mButtonBox->button (QDialogButtonBox::Help);
    mBtnFind = mButtonBox->addButton (QString::null, QDialogButtonBox::ActionRole);
    mBtnSave = mButtonBox->button (QDialogButtonBox::Save);
    mBtnRefresh = mButtonBox->addButton (QString::null, QDialogButtonBox::ActionRole);
    mBtnClose = mButtonBox->button (QDialogButtonBox::Close);

    /* Setup connections */
    connect (mButtonBox, SIGNAL (helpRequested()),
             &msgCenter(), SLOT (sltShowHelpHelpDialog()));
    connect (mBtnFind, SIGNAL (clicked()), this, SLOT (search()));
    connect (mBtnSave, SIGNAL (clicked()), this, SLOT (save()));
    connect (mBtnRefresh, SIGNAL (clicked()), this, SLOT (refresh()));

    /* Reading log files */
    refresh();
    /* Set the focus to the initial default button */
    defaultButton()->setDefault (true);
    defaultButton()->setFocus();
#ifdef Q_WS_MAC
    /* We have to force this to get the default button L&F on the mac. */
    defaultButton()->setEnabled (true);
# ifdef VBOX_DARWIN_USE_NATIVE_CONTROLS
    logsFrameLayout->setSpacing (4);
# endif /* VBOX_DARWIN_USE_NATIVE_CONTROLS */
#endif /* Q_WS_MAC */
    /* Loading language constants */
    retranslateUi();
}

VBoxVMLogViewer::~VBoxVMLogViewer()
{
    if (!mMachine.isNull())
        mSelfArray.remove (mMachine.GetName());
}

QTextEdit* VBoxVMLogViewer::currentLogPage()
{
    if (mLogList->isEnabled())
    {
        QWidget *container = mLogList->currentWidget();
        QTextEdit *browser = container->findChild<QTextEdit*>();
        Assert (browser);
        return browser ? browser : 0;
    }
    else
        return 0;
}


bool VBoxVMLogViewer::close()
{
    mSearchPanel->hide();
    return QIMainDialog::close();
}

void VBoxVMLogViewer::refresh()
{
    /* Clearing old data if any */
    mLogFiles.clear();
    mLogList->setEnabled (true);
    while (mLogList->count())
    {
        QWidget *firstPage = mLogList->widget (0);
        mLogList->removeTab (0);
        delete firstPage;
    }

    bool isAnyLogPresent = false;

    const CSystemProperties &sys = vboxGlobal().virtualBox().GetSystemProperties();
    int cMaxLogs = sys.GetLogHistoryCount();
    for (int i=0; i <= cMaxLogs; ++i)
    {
        /* Query the log file name for index i */
        QString file = mMachine.QueryLogFilename(i);
        if (!file.isEmpty())
        {
            /* Try to read the log file with the index i */
            ULONG uOffset = 0;
            QString text;
            while (true)
            {
                QVector<BYTE> data = mMachine.ReadLog(i, uOffset, _1M);
                if (data.size() == 0)
                    break;
                text.append(QString::fromUtf8((char*)data.data(), data.size()));
                uOffset += data.size();
            }
            /* Anything read at all? */
            if (uOffset > 0)
            {
                /* Create a log viewer page and append the read text to it */
                QTextEdit *logViewer = createLogPage(QFileInfo(file).fileName());
                logViewer->setPlainText(text);
                /* Add the actual file name and the QTextEdit containing the
                   content to a list. */
                mLogFiles << qMakePair(file, logViewer);
                isAnyLogPresent = true;
            }
        }
    }

    /* Create an empty log page if there are no logs at all */
    if (!isAnyLogPresent)
    {
        QTextEdit *dummyLog = createLogPage ("VBox.log");
        dummyLog->setWordWrapMode (QTextOption::WordWrap);
        dummyLog->setHtml (tr ("<p>No log files found. Press the "
            "<b>Refresh</b> button to rescan the log folder "
            "<nobr><b>%1</b></nobr>.</p>")
            .arg (mMachine.GetLogFolder()));
        /* We don't want it to remain white */
        QPalette pal = dummyLog->palette();
        pal.setColor (QPalette::Base, pal.color (QPalette::Window));
        dummyLog->setPalette (pal);
    }

    /* Show the first tab widget's page after the refresh */
    mLogList->setCurrentIndex (0);
    currentLogPageChanged (0);

    /* Enable/Disable save button & tab widget according log presence */
    mBtnFind->setEnabled (isAnyLogPresent);
    mBtnSave->setEnabled (isAnyLogPresent);
    mLogList->setEnabled (isAnyLogPresent);
    /* Default to the save button if there are any log files otherwise to the
     * close button. The initial automatic of the main dialog has to be
     * overwritten */
    setDefaultButton (isAnyLogPresent ? mBtnSave:mBtnClose);
}

void VBoxVMLogViewer::save()
{
    /* Prepare "save as" dialog */
    QFileInfo fileInfo (mLogFiles.at(mLogList->currentIndex()).first);
    QDateTime dtInfo = fileInfo.lastModified();
    QString dtString = dtInfo.toString ("yyyy-MM-dd-hh-mm-ss");
    QString defaultFileName = QString ("%1-%2.log")
        .arg (mMachine.GetName()).arg (dtString);
    QString defaultFullName = QDir::toNativeSeparators (
        QDir::home().absolutePath() + "/" + defaultFileName);
    QString newFileName = QFileDialog::getSaveFileName (this,
        tr ("Save VirtualBox Log As"), defaultFullName);

    /* Copy log into the file */
    if (!newFileName.isEmpty())
        QFile::copy(mMachine.QueryLogFilename(mLogList->currentIndex()), newFileName);
}

void VBoxVMLogViewer::search()
{
    mSearchPanel->isHidden() ? mSearchPanel->show() : mSearchPanel->hide();
}

void VBoxVMLogViewer::currentLogPageChanged (int aIndex)
{
    if (aIndex >= 0 &&
        aIndex < mLogFiles.count())
        setFileForProxyIcon(mLogFiles.at(aIndex).first);
}

void VBoxVMLogViewer::retranslateUi()
{
    /* Translate uic generated strings */
    Ui::VBoxVMLogViewer::retranslateUi (this);

    /* Setup a dialog caption */
    if (!mMachine.isNull())
        setWindowTitle (tr ("%1 - VirtualBox Log Viewer").arg (mMachine.GetName()));

    mBtnFind->setText (tr ("&Find"));
    mBtnRefresh->setText (tr ("&Refresh"));
    mBtnSave->setText (tr ("&Save"));
    mBtnClose->setText (tr ("Close"));
}

void VBoxVMLogViewer::showEvent (QShowEvent *aEvent)
{
    QIMainDialog::showEvent (aEvent);

    /* One may think that QWidget::polish() is the right place to do things
     * below, but apparently, by the time when QWidget::polish() is called,
     * the widget style & layout are not fully done, at least the minimum
     * size hint is not properly calculated. Since this is sometimes necessary,
     * we provide our own "polish" implementation. */

    if (mIsPolished)
        return;

    mIsPolished = true;

    if (mFirstRun)
    {
        /* Resize the whole log-viewer to fit 80 symbols in
         * text-browser for the first time started */
        QTextEdit *firstPage = currentLogPage();
        if (firstPage)
        {
            int fullWidth =
                firstPage->fontMetrics().width (QChar ('x')) * 80 +
                firstPage->verticalScrollBar()->width() +
                firstPage->frameWidth() * 2 +
                /* mLogList margin */ 10 * 2 +
                /* CentralWidget margin */ 10 * 2;
            resize (fullWidth, height());
            mFirstRun = false;
        }
    }

    /* Make sure the log view widget has the focus */
    QWidget *w = currentLogPage();
    if (w)
        w->setFocus();
}

QTextEdit* VBoxVMLogViewer::createLogPage (const QString &aName)
{
    QWidget *pageContainer = new QWidget();
    QVBoxLayout *pageLayout = new QVBoxLayout (pageContainer);
    QTextEdit *logViewer = new QTextEdit (pageContainer);
    pageLayout->addWidget (logViewer);
    pageLayout->setContentsMargins (10, 10, 10, 10);

    QFont font = logViewer->currentFont();
    font.setFamily ("Courier New,courier");
    logViewer->setFont (font);
    logViewer->setWordWrapMode (QTextOption::NoWrap);
    logViewer->setVerticalScrollBarPolicy (Qt::ScrollBarAlwaysOn);
    logViewer->setReadOnly (true);

    mLogList->addTab (pageContainer, aName);
    return logViewer;
}


VBoxLogSearchPanel::VBoxLogSearchPanel (QWidget *aParent,
                                        VBoxVMLogViewer *aViewer)
    : QIWithRetranslateUI<QWidget> (aParent)
    , mViewer (aViewer)
    , mButtonClose (0)
    , mSearchName (0), mSearchString (0)
    , mButtonsNextPrev (0)
    , mCaseSensitive (0)
    , mWarningSpacer (0), mWarningIcon (0), mWarningString (0)
{
    mButtonClose = new UIMiniCancelButton (this);
    connect (mButtonClose, SIGNAL (clicked()), this, SLOT (hide()));

    mSearchName = new QLabel (this);
    mSearchString = new UISearchField (this);
    mSearchString->setSizePolicy (QSizePolicy::Preferred,
                                  QSizePolicy::Fixed);
    connect (mSearchString, SIGNAL (textChanged (const QString &)),
             this, SLOT (findCurrent (const QString &)));

    mButtonsNextPrev = new UIRoundRectSegmentedButton(2, this);
    mButtonsNextPrev->setEnabled (0, false);
    mButtonsNextPrev->setEnabled (1, false);
#ifndef Q_WS_MAC
    /* No icons on the Mac */
    mButtonsNextPrev->setIcon(0, UIIconPool::defaultIcon(UIIconPool::ArrowBackIcon, this));
    mButtonsNextPrev->setIcon(1, UIIconPool::defaultIcon(UIIconPool::ArrowForwardIcon, this));
#endif /* !Q_WS_MAC */
    connect (mButtonsNextPrev, SIGNAL (clicked (int)), this, SLOT (find (int)));

    mCaseSensitive = new QCheckBox (this);

    mWarningSpacer = new QSpacerItem (0, 0, QSizePolicy::Fixed,
                                            QSizePolicy::Minimum);
    mWarningIcon = new QLabel (this);
    mWarningIcon->hide();

    QIcon icon = UIIconPool::defaultIcon(UIIconPool::MessageBoxWarningIcon, this);
    if (!icon.isNull())
        mWarningIcon->setPixmap (icon.pixmap (16, 16));
    mWarningString = new QLabel (this);
    mWarningString->hide();

    QSpacerItem *spacer = new QSpacerItem (0, 0, QSizePolicy::Expanding,
                                                 QSizePolicy::Minimum);

#ifdef VBOX_DARWIN_USE_NATIVE_CONTROLS
    QFont font = mSearchName->font();
    font.setPointSize (::darwinSmallFontSize());
    mSearchName->setFont (font);
    mCaseSensitive->setFont (font);
    mWarningString->setFont (font);
#endif /* VBOX_DARWIN_USE_NATIVE_CONTROLS */

    QHBoxLayout *mainLayout = new QHBoxLayout (this);
    mainLayout->setSpacing (5);
    mainLayout->setContentsMargins (0, 0, 0, 0);
    mainLayout->addWidget (mButtonClose);
    mainLayout->addWidget (mSearchName);
    mainLayout->addWidget (mSearchString);
    mainLayout->addWidget (mButtonsNextPrev);
    mainLayout->addWidget (mCaseSensitive);
    mainLayout->addItem   (mWarningSpacer);
    mainLayout->addWidget (mWarningIcon);
    mainLayout->addWidget (mWarningString);
    mainLayout->addItem   (spacer);

    setFocusProxy (mCaseSensitive);
    qApp->installEventFilter (this);

    retranslateUi();
}

void VBoxLogSearchPanel::retranslateUi()
{
    mButtonClose->setToolTip (tr ("Close the search panel"));

    mSearchName->setText (tr ("Find "));
    mSearchString->setToolTip (tr ("Enter a search string here"));

    mButtonsNextPrev->setTitle (0, tr ("&Previous"));
    mButtonsNextPrev->setToolTip (0, tr ("Search for the previous occurrence "
                                         "of the string"));

    mButtonsNextPrev->setTitle (1, tr ("&Next"));
    mButtonsNextPrev->setToolTip (1, tr ("Search for the next occurrence of "
                                         "the string"));

    mCaseSensitive->setText (tr ("C&ase Sensitive"));
    mCaseSensitive->setToolTip (tr ("Perform case sensitive search "
                                    "(when checked)"));

    mWarningString->setText (tr ("String not found"));
}

void VBoxLogSearchPanel::findCurrent (const QString &aSearchString)
{
    mButtonsNextPrev->setEnabled (0, aSearchString.length());
    mButtonsNextPrev->setEnabled (1, aSearchString.length());
    toggleWarning (!aSearchString.length());
    if (aSearchString.length())
        search (true, true);
    else
    {
        QTextEdit *browser = mViewer->currentLogPage();
        if (browser && browser->textCursor().hasSelection())
        {
            QTextCursor cursor = browser->textCursor();
            cursor.setPosition (cursor.anchor());
            browser->setTextCursor (cursor);
        }
    }
}

void VBoxLogSearchPanel::search (bool aForward,
                                 bool aStartCurrent)
{
    QTextEdit *browser = mViewer->currentLogPage();
    if (!browser) return;

    QTextCursor cursor = browser->textCursor();
    int pos = cursor.position();
    int anc = cursor.anchor();

    QString text = browser->toPlainText();
    int diff = aStartCurrent ? 0 : 1;

    int res = -1;
    if (aForward && (aStartCurrent || pos < text.size() - 1))
        res = text.indexOf (mSearchString->text(),
                            anc + diff,
                            mCaseSensitive->isChecked() ?
                            Qt::CaseSensitive : Qt::CaseInsensitive);
    else if (!aForward && anc > 0)
        res = text.lastIndexOf (mSearchString->text(), anc - 1,
                                mCaseSensitive->isChecked() ?
                                Qt::CaseSensitive : Qt::CaseInsensitive);

    if (res != -1)
    {
        cursor.movePosition (QTextCursor::Start,
                             QTextCursor::MoveAnchor);
        cursor.movePosition (QTextCursor::NextCharacter,
                             QTextCursor::MoveAnchor, res);
        cursor.movePosition (QTextCursor::NextCharacter,
                             QTextCursor::KeepAnchor,
                             mSearchString->text().size());
        browser->setTextCursor (cursor);
    }

    toggleWarning (res != -1);
}

bool VBoxLogSearchPanel::eventFilter (QObject *aObject, QEvent *aEvent)
{
    /* Check that the object is a child of the parent of the search panel. If
     * not do not proceed, cause we get all key events from all windows here. */
    QObject *pp = aObject;
    while(pp && pp != parentWidget()) { pp = pp->parent(); };
    if (!pp)
        return false;
    switch (aEvent->type())
    {
        case QEvent::KeyPress:
        {
            QKeyEvent *e = static_cast<QKeyEvent*> (aEvent);

            /* handle the Enter keypress for mSearchString
             * widget as a search next string action */
            if (aObject == mSearchString &&
                (e->QInputEvent::modifiers() == 0 ||
                 e->QInputEvent::modifiers() & Qt::KeypadModifier) &&
                (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return))
            {
                mButtonsNextPrev->animateClick (1);
                return true;
            }
            /* handle other search next/previous shortcuts */
            else if (e->key() == Qt::Key_F3)
            {
                if (e->QInputEvent::modifiers() == 0)
                    mButtonsNextPrev->animateClick (1);
                else if (e->QInputEvent::modifiers() == Qt::ShiftModifier)
                    mButtonsNextPrev->animateClick (0);
                return true;
            }
            /* handle ctrl-f key combination as a shortcut to
             * move to the search field */
            else if (e->QInputEvent::modifiers() == Qt::ControlModifier &&
                     e->key() == Qt::Key_F)
            {
                if (mViewer->currentLogPage())
                {
                    if (isHidden()) show();
                    mSearchString->setFocus();
                    return true;
                }
            }
            /* handle alpha-numeric keys to implement the
             * "find as you type" feature */
            else if ((e->QInputEvent::modifiers() & ~Qt::ShiftModifier) == 0 &&
                     e->key() >= Qt::Key_Exclam &&
                     e->key() <= Qt::Key_AsciiTilde)
            {
                if (mViewer->currentLogPage())
                {
                    if (isHidden()) show();
                    mSearchString->setFocus();
                    mSearchString->insert (e->text());
                    return true;
                }
            }
            break;
        }
        default:
            break;
    }
    return false;
}

void VBoxLogSearchPanel::showEvent (QShowEvent *aEvent)
{
    QWidget::showEvent (aEvent);
    mSearchString->setFocus();
    mSearchString->selectAll();
}

void VBoxLogSearchPanel::hideEvent (QHideEvent *aEvent)
{
    QWidget *focus = QApplication::focusWidget();
    if (focus &&
        focus->parent() == this)
       focusNextPrevChild (true);
    QWidget::hideEvent (aEvent);
}

void VBoxLogSearchPanel::toggleWarning (bool aHide)
{
    mWarningSpacer->changeSize (aHide ? 0 : 16, 0, QSizePolicy::Fixed,
                                                   QSizePolicy::Minimum);
    if (!aHide)
        mSearchString->markError();
    else
        mSearchString->unmarkError();
    mWarningIcon->setHidden (aHide);
    mWarningString->setHidden (aHide);
}

void VBoxLogSearchPanel::find (int aButton)
{
    if (aButton == 0)
        findBack();
    else
        findNext();
}

