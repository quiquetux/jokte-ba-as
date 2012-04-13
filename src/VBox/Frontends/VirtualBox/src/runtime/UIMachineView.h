/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIMachineView class declaration
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

#ifndef ___UIMachineView_h___
#define ___UIMachineView_h___

/* Global includes */
#include <QAbstractScrollArea>
#include <QEventLoop>

/* Local includes */
#include "COMDefs.h"
#include "UIMachineDefs.h"

#ifdef Q_WS_MAC
# include <CoreFoundation/CFBase.h>
#endif /* Q_WS_MAC */

/* Local forwards */
class UISession;
class UIMachineLogic;
class UIMachineWindow;
class UIFrameBuffer;

class UIMachineView : public QAbstractScrollArea
{
    Q_OBJECT;

public:

    /* Desktop geometry types: */
    enum DesktopGeo { DesktopGeo_Invalid = 0, DesktopGeo_Fixed, DesktopGeo_Automatic, DesktopGeo_Any };

    /* Factory function to create machine-view: */
    static UIMachineView* create(  UIMachineWindow *pMachineWindow
                                 , ulong uScreenId
                                 , UIVisualStateType visualStateType
#ifdef VBOX_WITH_VIDEOHWACCEL
                                 , bool bAccelerate2DVideo
#endif /* VBOX_WITH_VIDEOHWACCEL */
    );
    /* Factory function to destroy required machine-view: */
    static void destroy(UIMachineView *pMachineView);

    /* Public setters: */
    virtual void setGuestAutoresizeEnabled(bool /* fEnabled */) {}

    /* Public members: */
    virtual void normalizeGeometry(bool /* bAdjustPosition = false */) = 0;

    /* Framebuffer aspect ratio: */
    double aspectRatio() const;

signals:

    /* Utility signals: */
    void resizeHintDone();

protected slots:

    /* Console callback handlers: */
    virtual void sltMachineStateChanged();

protected:

    /* Machine-view constructor: */
    UIMachineView(  UIMachineWindow *pMachineWindow
                  , ulong uScreenId
#ifdef VBOX_WITH_VIDEOHWACCEL
                  , bool bAccelerate2DVideo
#endif /* VBOX_WITH_VIDEOHWACCEL */
    );
    /* Machine-view destructor: */
    virtual ~UIMachineView();

    /* Prepare routines: */
    virtual void prepareViewport();
    virtual void prepareFrameBuffer();
    virtual void prepareCommon();
    virtual void prepareFilters();
    virtual void prepareConsoleConnections();
    virtual void loadMachineViewSettings();

    /* Cleanup routines: */
    //virtual void saveMachineViewSettings() {}
    //virtual void cleanupConsoleConnections() {}
    //virtual void cleanupFilters() {}
    //virtual void cleanupCommon() {}
    virtual void cleanupFrameBuffer();
    //virtual void cleanupViewport();

    /* Protected getters: */
    UIMachineWindow* machineWindowWrapper() const { return m_pMachineWindow; }
    UIMachineLogic* machineLogic() const;
    UISession* uisession() const;
    CSession& session();
    QSize sizeHint() const;
    int contentsX() const;
    int contentsY() const;
    int contentsWidth() const;
    int contentsHeight() const;
    int visibleWidth() const;
    int visibleHeight() const;
    ulong screenId() const { return m_uScreenId; }
    UIFrameBuffer* frameBuffer() const { return m_pFrameBuffer; }
    bool isMachineWindowResizeIgnored() const { return m_bIsMachineWindowResizeIgnored; }
    const QPixmap& pauseShot() const { return m_pauseShot; }
    QSize storedConsoleSize() const { return m_storedConsoleSize; }
    DesktopGeo desktopGeometryType() const { return m_desktopGeometryType; }
    QSize desktopGeometry() const;
    QSize guestSizeHint();

    /* Protected setters: */
    void setDesktopGeometry(DesktopGeo geometry, int iWidth, int iHeight);
    void storeConsoleSize(int iWidth, int iHeight);
    void setMachineWindowResizeIgnored(bool fIgnore = true) { m_bIsMachineWindowResizeIgnored = fIgnore; }
    void storeGuestSizeHint(const QSize &sizeHint);

    /* Protected helpers: */
    virtual void takePauseShotLive();
    virtual void takePauseShotSnapshot();
    virtual void resetPauseShot() { m_pauseShot = QPixmap(); }
    virtual QRect workingArea() = 0;
    virtual void calculateDesktopGeometry() = 0;
    virtual void maybeRestrictMinimumSize() = 0;
    virtual void updateSliders();
    QPoint viewportToContents(const QPoint &vp) const;
    void scrollBy(int dx, int dy);
    static void dimImage(QImage &img);
#ifdef VBOX_WITH_VIDEOHWACCEL
    void scrollContentsBy(int dx, int dy);
#endif /* VBOX_WITH_VIDEOHWACCEL */
#ifdef Q_WS_MAC
    void updateDockIcon();
    CGImageRef vmContentImage();
    CGImageRef frameBuffertoCGImageRef(UIFrameBuffer *pFrameBuffer);
#endif /* Q_WS_MAC */

    /* Cross-platforms event processors: */
    bool event(QEvent *pEvent);
    bool eventFilter(QObject *pWatched, QEvent *pEvent);
    void resizeEvent(QResizeEvent *pEvent);
    void moveEvent(QMoveEvent *pEvent);
    void paintEvent(QPaintEvent *pEvent);

    /* Platform specific event processors: */
#if defined(Q_WS_WIN)
    bool winEvent(MSG *pMsg, long *puResult);
#elif defined(Q_WS_X11)
    bool x11Event(XEvent *event);
#endif

    /* Private members: */
    UIMachineWindow *m_pMachineWindow;
    ulong m_uScreenId;
    UIFrameBuffer *m_pFrameBuffer;
    KMachineState m_previousState;

    DesktopGeo m_desktopGeometryType;
    QSize m_desktopGeometry;
    QSize m_storedConsoleSize;

    bool m_bIsMachineWindowResizeIgnored : 1;
#ifdef VBOX_WITH_VIDEOHWACCEL
    bool m_fAccelerate2DVideo : 1;
#endif /* VBOX_WITH_VIDEOHWACCEL */

    QPixmap m_pauseShot;

    /* Friend classes: */
    friend class UIKeyboardHandler;
    friend class UIMouseHandler;
    friend class UIMachineLogic;
    friend class UIFrameBuffer;
    friend class UIFrameBufferQImage;
    friend class UIFrameBufferQuartz2D;
    friend class UIFrameBufferQGL;
    template<class, class, class> friend class VBoxOverlayFrameBuffer;
};

/* This maintenance class is a part of future roll-back mechanism.
 * It allows to block main GUI thread until specific event received.
 * Later it will become more abstract but now its just used to help
 * fullscreen & seamless modes to restore normal guest size hint. */
class UIMachineViewBlocker : public QEventLoop
{
    Q_OBJECT;

public:

    UIMachineViewBlocker()
        : QEventLoop(0)
        , m_iTimerId(0)
    {
        /* Also start timer to unlock pool in case of
         * required condition doesn't happens by some reason: */
        m_iTimerId = startTimer(3000);
    }

    virtual ~UIMachineViewBlocker()
    {
        /* Kill the timer: */
        killTimer(m_iTimerId);
    }

protected:

    void timerEvent(QTimerEvent *pEvent)
    {
        /* If that timer event occurs => it seems
         * guest resize event doesn't comes in time,
         * shame on it, but we just unlocking 'this': */
        QEventLoop::timerEvent(pEvent);
        exit();
    }

    int m_iTimerId;
};

#endif // !___UIMachineView_h___
