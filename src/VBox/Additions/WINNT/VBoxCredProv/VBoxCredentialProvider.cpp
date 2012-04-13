/* $Id: VBoxCredentialProvider.cpp $ */
/** @file
 * VBoxCredentialProvider - Main file of the VirtualBox Credential Provider.
 */

/*
 * Copyright (C) 2012 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <windows.h>
#include <initguid.h>

#include <iprt/buildconfig.h>
#include <iprt/initterm.h>

#include <VBox/VBoxGuestLib.h>

#include "VBoxCredentialProvider.h"
#include "VBoxCredProvFactory.h"

/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
static LONG g_cDllRefs  = 0;            /**< Global DLL reference count. */
static HINSTANCE g_hDllInst = NULL;     /**< Global DLL hInstance. */



BOOL WINAPI DllMain(HINSTANCE hInst, DWORD dwReason, LPVOID pReserved)
{
    NOREF(pReserved);

    g_hDllInst = hInst;

    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            int rc = RTR3Init();
            if (RT_SUCCESS(rc))
                rc = VbglR3Init();

            if (RT_SUCCESS(rc))
            {
                VBoxCredProvVerbose(0, "VBoxCredProv: v%s r%s (%s %s) loaded (refs=%ld)\n",
                                    RTBldCfgVersion(), RTBldCfgRevisionStr(),
                                    __DATE__, __TIME__, g_cDllRefs);
            }

            DisableThreadLibraryCalls(hInst);
            break;
        }

        case DLL_PROCESS_DETACH:

            if (!g_cDllRefs)
                VbglR3Term();
            break;

        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            break;
    }

    return TRUE;
}


/**
 * Increments the reference count by one. Must be released
 * with VBoxCredentialProviderRelease() when finished.
 */
void VBoxCredentialProviderAcquire(void)
{
    LONG cRefCount = InterlockedIncrement(&g_cDllRefs);
    VBoxCredProvVerbose(0, "VBoxCredentialProviderAcquire: Increasing global refcount to %ld\n",
                        cRefCount);
}


/**
 * Decrements the reference count by one.
 */
void VBoxCredentialProviderRelease(void)
{
    LONG cRefCount = InterlockedDecrement(&g_cDllRefs);
    VBoxCredProvVerbose(0, "VBoxCredentialProviderRelease: Decreasing global refcount to %ld\n",
                        cRefCount);
}


/**
 * Returns the current DLL reference count.
 *
 * @return  LONG                The current reference count.
 */
LONG VBoxCredentialProviderRefCount(void)
{
    return g_cDllRefs;
}


/**
 * Entry point for determining whether the credential
 * provider DLL can be unloaded or not.
 *
 * @return  HRESULT
 */
HRESULT __stdcall DllCanUnloadNow(void)
{
    return (g_cDllRefs > 0) ? S_FALSE : S_OK;
}


/**
 * Create the VirtualBox credential provider by creating
 * its factory which then in turn can create instances of the
 * provider itself.
 *
 * @return  HRESULT
 * @param   classID             The class ID.
 * @param   interfaceID         The interface ID.
 * @param   ppvInterface        Receives the interface pointer on successful
 *                              object creation.
 */
HRESULT VBoxCredentialProviderCreate(REFCLSID classID, REFIID interfaceID,
                                     void **ppvInterface)
{
    HRESULT hr;
    if (classID == CLSID_VBoxCredProvider)
    {
        VBoxCredProvFactory* pFactory = new VBoxCredProvFactory();
        if (pFactory)
        {
            hr = pFactory->QueryInterface(interfaceID,
                                          ppvInterface);
            pFactory->Release();
        }
        else
            hr = E_OUTOFMEMORY;
    }
    else
        hr = CLASS_E_CLASSNOTAVAILABLE;

    return hr;
}


/**
 * Entry point for getting the actual credential provider
 * class object.
 *
 * @return  HRESULT
 * @param   classID             The class ID.
 * @param   interfaceID         The interface ID.
 * @param   ppvInterface        Receives the interface pointer on successful
 *                              object creation.
 */
HRESULT __stdcall DllGetClassObject(REFCLSID classID, REFIID interfaceID,
                                    void **ppvInterface)
{
    return VBoxCredentialProviderCreate(classID, interfaceID, ppvInterface);
}

