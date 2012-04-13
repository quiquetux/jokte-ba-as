/* $Revision: 73443 $ */
/** @file
 * VBoxGuestLibR0 - Internal header.
 */

/*
 * Copyright (C) 2006-2007 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 *
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL) only, as it comes in the "COPYING.CDDL" file of the
 * VirtualBox OSE distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 */

#ifndef ___VBoxGuestLib_VBGLInternal_h
#define ___VBoxGuestLib_VBGLInternal_h

#include <VBox/VMMDev.h>
#include <VBox/VBoxGuest.h>
#include <VBox/VBoxGuestLib.h>

#include <VBox/log.h>


#ifdef RT_OS_WINDOWS /** @todo dprintf() -> Log() */
# if (defined(DEBUG) && !defined(NO_LOGGING)) || defined(LOG_ENABLED)
#  define dprintf(a) RTLogBackdoorPrintf a
# else
#  define dprintf(a) do {} while (0)
# endif
#else
# define dprintf(a) Log(a)
#endif

#include "SysHlp.h"

#pragma pack(4) /** @todo r=bird: What do we need packing for here? None of these structures are shared between drivers AFAIK. */

struct _VBGLPHYSHEAPBLOCK;
typedef struct _VBGLPHYSHEAPBLOCK VBGLPHYSHEAPBLOCK;
struct _VBGLPHYSHEAPCHUNK;
typedef struct _VBGLPHYSHEAPCHUNK VBGLPHYSHEAPCHUNK;

#ifndef VBGL_VBOXGUEST
struct VBGLHGCMHANDLEDATA
{
    uint32_t fAllocated;
    VBGLDRIVER driver;
};
#endif

enum VbglLibStatus
{
    VbglStatusNotInitialized = 0,
    VbglStatusInitializing,
    VbglStatusReady
};

/**
 * Global VBGL ring-0 data.
 * Lives in VbglR0Init.cpp.
 */
typedef struct _VBGLDATA
{
    enum VbglLibStatus status;

    VBGLIOPORT portVMMDev;

    VMMDevMemory *pVMMDevMemory;

    /**
     * Physical memory heap data.
     * @{
     */

    VBGLPHYSHEAPBLOCK *pFreeBlocksHead;
    VBGLPHYSHEAPBLOCK *pAllocBlocksHead;
    VBGLPHYSHEAPCHUNK *pChunkHead;

    RTSEMFASTMUTEX mutexHeap;
    /** @} */

    /**
     * The host version data.
     */
    VMMDevReqHostVersion hostVersion;


#ifndef VBGL_VBOXGUEST
    /**
     * Fast heap for HGCM handles data.
     * @{
     */

    RTSEMFASTMUTEX mutexHGCMHandle;

    struct VBGLHGCMHANDLEDATA aHGCMHandleData[64];

    /** @} */
#endif
} VBGLDATA;


#pragma pack()

#ifndef VBGL_DECL_DATA
extern VBGLDATA g_vbgldata;
#endif

/**
 * Internal macro for checking whether we can pass phyical page lists to the
 * host.
 *
 * ASSUMES that vbglR0Enter has been called already.
 */
#if defined(RT_OS_WINDOWS) && defined(RT_ARCH_AMD64)
/* Disable the PageList feature for 64 bit Windows, because shared folders do not work,
 * if this is enabled. This should be reenabled again when the problem is fixed.
 */
#define VBGLR0_CAN_USE_PHYS_PAGE_LIST() \
    ( 0 )
#else
#define VBGLR0_CAN_USE_PHYS_PAGE_LIST() \
    ( !!(g_vbgldata.hostVersion.features & VMMDEV_HVF_HGCM_PHYS_PAGE_LIST) )
#endif

int vbglR0Enter (void);

#ifdef VBOX_WITH_HGCM
# ifndef VBGL_VBOXGUEST
int vbglR0HGCMInit (void);
int vbglR0HGCMTerminate (void);
# endif
#endif /* VBOX_WITH_HGCM */

#endif /* !___VBoxGuestLib_VBGLInternal_h */

