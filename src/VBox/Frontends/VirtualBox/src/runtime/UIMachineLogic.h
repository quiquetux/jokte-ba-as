/** @file
 *
 * VBox frontends: Qt GUI ("VirtualBox"):
 * UIMachineLogic class declaration
 */

/*
 * Copyright (C) 2010-2011 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __UIMachineLogic_h__
#define __UIMachineLogic_h__

/* Local includes */
#include "UIMachineDefs.h"
#include <QIWithRetranslateUI.h>
#ifdef VBOX_WITH_DEBUGGER_GUI
# include <VBox/dbggui.h>
#endif

/* Global forwards */
class QAction;
class QActionGroup;

/* Local forwards */
class CSession;
class CMachine;
class CSnapshot;
class CUSBDevice;
class CVirtualBoxErrorInfo;
class UISession;
class UIKeyboardHandler;
class UIMouseHandler;
class UIMachineWindow;
class UIMachineView;
class UIDockIconPreview;

class UIMachineLogic : public QIWithRetranslateUI3<QObject>
{
    Q_OBJECT;

public:

    /* Factory function to create required logic sub-child: */
    static UIMachineLogic* create(QObject *pParent,
                                  UISession *pSession,
                                  UIVisualStateType visualStateType);

    /* Check if this mode is available: */
    virtual bool checkAvailability();

    /* Do the real initialization of the object: */
    virtual void initialize() = 0;

    /* Main getters/setters: */
    UISession* uisession() const { return m_pSession; }
    CSession& session();
    UIVisualStateType visualStateType() const { return m_visualStateType; }
    const QList<UIMachineWindow*>& machineWindows() const { return m_machineWindowsList; }
    UIKeyboardHandler* keyboardHandler() const { return m_pKeyboardHandler; }
    UIMouseHandler* mouseHandler() const { return m_pMouseHandler; }
    UIMachineWindow* mainMachineWindow() const;
    UIMachineWindow* defaultMachineWindow() const;

    /* Maintenance getters/setters: */
    bool isPreventAutoClose() const { return m_fIsPreventAutoClose; }
    void setPreventAutoClose(bool fIsPreventAutoClose) { m_fIsPreventAutoClose = fIsPreventAutoClose; }

#ifdef Q_WS_MAC
    void updateDockIcon();
    void updateDockIconSize(int screenId, int width, int height);
    UIMachineView* dockPreviewView() const;
#endif /* Q_WS_MAC */

signals:

    /* Signal to notify listeners about additions downloader created: */
    void sigDownloaderAdditionsCreated();

protected:

    /* Machine logic constructor/destructor: */
    UIMachineLogic(QObject *pParent,
                   UISession *pSession,
                   UIVisualStateType visualStateType);
    virtual ~UIMachineLogic();

    /* Protected getters/setters: */
    bool isMachineWindowsCreated() const { return m_fIsWindowsCreated; }
    void setMachineWindowsCreated(bool fIsWindowsCreated) { m_fIsWindowsCreated = fIsWindowsCreated; }

    /* Protected members: */
    void setKeyboardHandler(UIKeyboardHandler *pKeyboardHandler);
    void setMouseHandler(UIMouseHandler *pMouseHandler);
    void addMachineWindow(UIMachineWindow *pMachineWindow);
    void retranslateUi();
#ifdef Q_WS_MAC
    bool isDockIconPreviewEnabled() const { return m_fIsDockIconEnabled; }
    void setDockIconPreviewEnabled(bool fIsDockIconPreviewEnabled) { m_fIsDockIconEnabled = fIsDockIconPreviewEnabled; }
    void updateDockOverlay();
#endif /* Q_WS_MAC */

    /* Prepare helpers: */
    virtual void prepareSessionConnections();
    virtual void prepareActionConnections();
    virtual void prepareActionGroups();
    virtual void prepareHandlers();
#ifdef Q_WS_MAC
    virtual void prepareDock();
#endif /* Q_WS_MAC */
    virtual void prepareRequiredFeatures();
#ifdef VBOX_WITH_DEBUGGER_GUI
    virtual void prepareDebugger();
#endif /* VBOX_WITH_DEBUGGER_GUI */

    /* Cleanup helpers: */
    //virtual void cleanupRequiredFeatures() {}
#ifdef Q_WS_MAC
    virtual void cleanupDock();
#endif /* Q_WS_MAC */
    virtual void cleanupHandlers();
    //virtual void cleanupActionGroups() {}
    //virtual void cleanupActionConnections() {}
    //virtual void cleanupSessionConnections() {}

protected slots:

    /* Console callback handlers: */
    virtual void sltMachineStateChanged();
    virtual void sltAdditionsStateChanged();
    virtual void sltMouseCapabilityChanged();
    virtual void sltUSBDeviceStateChange(const CUSBDevice &device, bool fIsAttached, const CVirtualBoxErrorInfo &error);
    virtual void sltRuntimeError(bool fIsFatal, const QString &strErrorId, const QString &strMessage);
#ifdef RT_OS_DARWIN
    virtual void sltShowWindows();
#endif /* RT_OS_DARWIN */

    /* Mode request watch dog: */
    void sltCheckRequestedModes();

private slots:

    /* "Machine" menu functionality */
    void sltToggleGuestAutoresize(bool fEnabled);
    void sltAdjustWindow();
    void sltToggleMouseIntegration(bool fDisabled);
    void sltTypeCAD();
#ifdef Q_WS_X11
    void sltTypeCABS();
#endif
    void sltTakeSnapshot();
    void sltShowInformationDialog();
    void sltReset();
    void sltPause(bool fOn);
    void sltACPIShutdown();
    void sltClose();

    /* "Device" menu functionality */
    void sltOpenVMSettingsDialog(const QString &strCategory = QString());
    void sltOpenNetworkAdaptersDialog();
    void sltOpenSharedFoldersDialog();
    void sltPrepareStorageMenu();
    void sltMountStorageMedium();
    void sltMountRecentStorageMedium();
    void sltPrepareUSBMenu();
    void sltAttachUSBDevice();
    void sltSwitchVrde(bool fOn);
    void sltInstallGuestAdditions();

#ifdef VBOX_WITH_DEBUGGER_GUI
    void sltPrepareDebugMenu();
    void sltShowDebugStatistics();
    void sltShowDebugCommandLine();
    void sltLoggingToggled(bool);
#endif

#ifdef RT_OS_DARWIN /* Something is *really* broken in regards of the moc here */
    void sltDockPreviewModeChanged(QAction *pAction);
    void sltDockPreviewMonitorChanged(QAction *pAction);
    void sltChangeDockIconUpdate(bool fEnabled);
#endif /* RT_OS_DARWIN */

private:

    /* Utility functions: */
    static int searchMaxSnapshotIndex(const CMachine &machine,
                                      const CSnapshot &snapshot,
                                      const QString &strNameTemplate);

    /* Private variables: */
    UISession *m_pSession;
    UIVisualStateType m_visualStateType;
    UIKeyboardHandler *m_pKeyboardHandler;
    UIMouseHandler *m_pMouseHandler;
    QList<UIMachineWindow*> m_machineWindowsList;

    QActionGroup *m_pRunningActions;
    QActionGroup *m_pRunningOrPausedActions;

    bool m_fIsWindowsCreated : 1;
    bool m_fIsPreventAutoClose : 1;

#ifdef VBOX_WITH_DEBUGGER_GUI
    /* Debugger functionality: */
    bool dbgCreated();
    void dbgDestroy();
    void dbgAdjustRelativePos();
    /* The handle to the debugger GUI: */
    PDBGGUI m_pDbgGui;
    /* The virtual method table for the debugger GUI: */
    PCDBGGUIVT m_pDbgGuiVT;
#endif

#ifdef Q_WS_MAC
    bool m_fIsDockIconEnabled;
    UIDockIconPreview *m_pDockIconPreview;
    QActionGroup *m_pDockPreviewSelectMonitorGroup;
    int m_DockIconPreviewMonitor;
#endif /* Q_WS_MAC */

    /* Friend classes: */
    friend class UIMachineWindow;
};

#endif // __UIMachineLogic_h__

