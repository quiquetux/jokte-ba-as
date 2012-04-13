/* $Id: UIMachineViewFullscreen.cpp $ */
/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIMachineViewFullscreen class implementation
 */

/*
 * Copyright (C) 2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/* Global includes */
#include <QApplication>
#include <QDesktopWidget>
#include <QMainWindow>
#include <QTimer>
#ifdef Q_WS_MAC
#include <QMenuBar>
#endif
#ifdef Q_WS_X11
#include <limits.h>
#endif

/* Local includes */
#include "VBoxGlobal.h"
#include "UISession.h"
#include "UIActionPoolRuntime.h"
#include "UIMachineLogicFullscreen.h"
#include "UIMachineWindow.h"
#include "UIMachineViewFullscreen.h"
#include "UIFrameBuffer.h"

UIMachineViewFullscreen::UIMachineViewFullscreen(  UIMachineWindow *pMachineWindow
                                                 , ulong uScreenId
#ifdef VBOX_WITH_VIDEOHWACCEL
                                                 , bool bAccelerate2DVideo
#endif
                                                 )
    : UIMachineView(  pMachineWindow
                    , uScreenId
#ifdef VBOX_WITH_VIDEOHWACCEL
                    , bAccelerate2DVideo
#endif
                    )
    , m_bIsGuestAutoresizeEnabled(gActionPool->action(UIActionIndexRuntime_Toggle_GuestAutoresize)->isChecked())
    , m_fShouldWeDoResize(false)
    , m_pSyncBlocker(0)
{
    /* Load machine view settings: */
    loadMachineViewSettings();

    /* Prepare viewport: */
    prepareViewport();

    /* Prepare frame buffer: */
    prepareFrameBuffer();

    /* Prepare common things: */
    prepareCommon();

    /* Prepare event-filters: */
    prepareFilters();

    /* Prepare console connections: */
    prepareConsoleConnections();

    /* Prepare fullscreen: */
    prepareFullscreen();

    /* Initialization: */
    sltMachineStateChanged();
}

UIMachineViewFullscreen::~UIMachineViewFullscreen()
{
    /* Cleanup fullscreen: */
    cleanupFullscreen();

    /* Cleanup frame buffer: */
    cleanupFrameBuffer();
}

void UIMachineViewFullscreen::sltPerformGuestResize(const QSize &toSize)
{
    if (m_bIsGuestAutoresizeEnabled && uisession()->isGuestSupportsGraphics())
    {
        /* Get machine window: */
        QMainWindow *pMachineWindow = machineWindowWrapper() && machineWindowWrapper()->machineWindow() ?
                                      qobject_cast<QMainWindow*>(machineWindowWrapper()->machineWindow()) : 0;

        /* If this slot is invoked directly then use the passed size otherwise get
         * the available size for the guest display. We assume here that centralWidget()
         * contains this view only and gives it all available space: */
        QSize newSize(toSize.isValid() ? toSize : pMachineWindow ? pMachineWindow->centralWidget()->size() : QSize());
        AssertMsg(newSize.isValid(), ("Size should be valid!\n"));

        /* Do not send the same hints as we already have: */
        if ((newSize.width() == storedConsoleSize().width()) && (newSize.height() == storedConsoleSize().height()))
            return;

        /* We only actually send the hint if either an explicit new size was given
         * (e.g. if the request was triggered directly by a console resize event) or
         * if no explicit size was specified but a resize is flagged as being needed
         * (e.g. the autoresize was just enabled and the console was resized while it was disabled). */
        if (toSize.isValid() || m_fShouldWeDoResize)
        {
            /* Remember the new size: */
            storeConsoleSize(newSize.width(), newSize.height());

            /* Send new size-hint to the guest: */
            session().GetConsole().GetDisplay().SetVideoModeHint(newSize.width(), newSize.height(), 0, screenId());
        }

        /* We had requested resize now, rejecting other accident requests: */
        m_fShouldWeDoResize = false;
    }
}

void UIMachineViewFullscreen::sltAdditionsStateChanged()
{
    /* Check if we should restrict minimum size: */
    maybeRestrictMinimumSize();

    /* Check if we should resize guest to fullscreen, all the
     * required features will be tested in sltPerformGuestResize(...): */
    if ((int)frameBuffer()->width() != workingArea().size().width() ||
        (int)frameBuffer()->height() != workingArea().size().height())
        sltPerformGuestResize(workingArea().size());
}

void UIMachineViewFullscreen::sltDesktopResized()
{
    /* If the desktop geometry is set automatically, this will update it: */
    calculateDesktopGeometry();
}

bool UIMachineViewFullscreen::event(QEvent *pEvent)
{
    switch (pEvent->type())
    {
        case VBoxDefs::ResizeEventType:
        {
            /* Some situations require framebuffer resize events to be ignored at all,
             * leaving machine-window, machine-view and framebuffer sizes preserved: */
            if (uisession()->isGuestResizeIgnored())
                return true;

            /* We are starting to perform machine-view resize,
             * we should temporary ignore other if they are trying to be: */
            bool fWasMachineWindowResizeIgnored = isMachineWindowResizeIgnored();
            setMachineWindowResizeIgnored(true);

            /* Get guest resize-event: */
            UIResizeEvent *pResizeEvent = static_cast<UIResizeEvent*>(pEvent);

            /* Perform framebuffer resize: */
            frameBuffer()->resizeEvent(pResizeEvent);

            /* Reapply maximum size restriction for machine-view: */
            setMaximumSize(sizeHint());

            /* Store the new size to prevent unwanted resize hints being sent back: */
            storeConsoleSize(pResizeEvent->width(), pResizeEvent->height());

            /* Perform machine-view resize: */
            resize(pResizeEvent->width(), pResizeEvent->height());

            /* May be we have to restrict minimum size? */
            maybeRestrictMinimumSize();

            /* Let our toplevel widget calculate its sizeHint properly: */
            QCoreApplication::sendPostedEvents(0, QEvent::LayoutRequest);

#ifdef Q_WS_MAC
            machineLogic()->updateDockIconSize(screenId(), pResizeEvent->width(), pResizeEvent->height());
#endif /* Q_WS_MAC */

            /* Update machine-view sliders: */
            updateSliders();

            /* Report to the VM thread that we finished resizing: */
            session().GetConsole().GetDisplay().ResizeCompleted(screenId());

            /* We are finishing to perform machine-view resize: */
            setMachineWindowResizeIgnored(fWasMachineWindowResizeIgnored);

            /* We also recalculate the desktop geometry if this is determined
             * automatically.  In fact, we only need this on the first resize,
             * but it is done every time to keep the code simpler. */
            calculateDesktopGeometry();

            /* Emit a signal about guest was resized: */
            emit resizeHintDone();

            /* Unlock after processing guest resize event: */
            if (m_pSyncBlocker && m_pSyncBlocker->isRunning())
                m_pSyncBlocker->quit();

            pEvent->accept();
            return true;
        }

        default:
            break;
    }
    return UIMachineView::event(pEvent);
}

bool UIMachineViewFullscreen::eventFilter(QObject *pWatched, QEvent *pEvent)
{
    /* Who are we watching? */
    QMainWindow *pMainDialog = machineWindowWrapper() && machineWindowWrapper()->machineWindow() ?
                               qobject_cast<QMainWindow*>(machineWindowWrapper()->machineWindow()) : 0;

    if (pWatched != 0 && pWatched == pMainDialog)
    {
        switch (pEvent->type())
        {
            case QEvent::Resize:
            {
                /* Send guest-resize hint only if top window resizing to required dimension: */
                QResizeEvent *pResizeEvent = static_cast<QResizeEvent*>(pEvent);
                if (pResizeEvent->size() != workingArea().size())
                    break;

                /* Set the "guest needs to resize" hint.
                 * This hint is acted upon when (and only when) the autoresize property is "true": */
                m_fShouldWeDoResize = uisession()->isGuestSupportsGraphics();
                if (m_bIsGuestAutoresizeEnabled && m_fShouldWeDoResize)
                    QTimer::singleShot(0, this, SLOT(sltPerformGuestResize()));
                break;
            }
            default:
                break;
        }
    }

    return UIMachineView::eventFilter(pWatched, pEvent);
}

void UIMachineViewFullscreen::prepareCommon()
{
    /* Base class common settings: */
    UIMachineView::prepareCommon();

    /* Setup size-policy: */
    setSizePolicy(QSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum));
    /* Maximum size to sizehint: */
    setMaximumSize(sizeHint());
    /* Minimum size is ignored: */
    setMinimumSize(0, 0);
    /* No scrollbars: */
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
}

void UIMachineViewFullscreen::prepareFilters()
{
    /* Base class filters: */
    UIMachineView::prepareFilters();

#ifdef Q_WS_MAC // TODO: Is it really needed? See UIMachineViewSeamless::eventFilter(...);
    /* Menu bar filter: */
    qobject_cast<QMainWindow*>(machineWindowWrapper()->machineWindow())->menuBar()->installEventFilter(this);
#endif
}

void UIMachineViewFullscreen::prepareConnections()
{
    connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(sltDesktopResized()));
}

void UIMachineViewFullscreen::prepareConsoleConnections()
{
    /* Base class connections: */
    UIMachineView::prepareConsoleConnections();

    /* Guest additions state-change updater: */
    connect(uisession(), SIGNAL(sigAdditionsStateChange()), this, SLOT(sltAdditionsStateChanged()));
}

void UIMachineViewFullscreen::prepareFullscreen()
{
    /* Create sync-blocker: */
    m_pSyncBlocker = new UIMachineViewBlocker;
}

void UIMachineViewFullscreen::cleanupFullscreen()
{
    /* If machine still running: */
    if (uisession()->isRunning())
    {
        /* And guest supports advanced graphics management which is enabled: */
        if (m_bIsGuestAutoresizeEnabled && uisession()->isGuestSupportsGraphics())
        {
            /* Rollback seamless frame-buffer size to normal: */
            machineWindowWrapper()->machineWindow()->hide();
            sltPerformGuestResize(guestSizeHint());
            m_pSyncBlocker->exec();

            /* Request to delete sync-blocker: */
            m_pSyncBlocker->deleteLater();
        }
    }
}

void UIMachineViewFullscreen::setGuestAutoresizeEnabled(bool fEnabled)
{
    if (m_bIsGuestAutoresizeEnabled != fEnabled)
    {
        m_bIsGuestAutoresizeEnabled = fEnabled;

        maybeRestrictMinimumSize();

        sltPerformGuestResize();
    }
}

QRect UIMachineViewFullscreen::workingArea()
{
    /* Get corresponding screen: */
    int iScreen = static_cast<UIMachineLogicFullscreen*>(machineLogic())->hostScreenForGuestScreen(screenId());
    /* Return available geometry for that screen: */
    return QApplication::desktop()->screenGeometry(iScreen);
}

void UIMachineViewFullscreen::calculateDesktopGeometry()
{
    /* This method should not get called until we have initially set up the desktop geometry type: */
    Assert((desktopGeometryType() != DesktopGeo_Invalid));
    /* If we are not doing automatic geometry calculation then there is nothing to do: */
    if (desktopGeometryType() == DesktopGeo_Automatic)
        m_desktopGeometry = workingArea().size();
}

void UIMachineViewFullscreen::maybeRestrictMinimumSize()
{
    /* Sets the minimum size restriction depending on the auto-resize feature state and the current rendering mode.
     * Currently, the restriction is set only in SDL mode and only when the auto-resize feature is inactive.
     * We need to do that because we cannot correctly draw in a scrolled window in SDL mode.
     * In all other modes, or when auto-resize is in force, this function does nothing. */
    if (vboxGlobal().vmRenderMode() == VBoxDefs::SDLMode)
    {
        if (!uisession()->isGuestSupportsGraphics() || !m_bIsGuestAutoresizeEnabled)
            setMinimumSize(sizeHint());
        else
            setMinimumSize(0, 0);
    }
}

