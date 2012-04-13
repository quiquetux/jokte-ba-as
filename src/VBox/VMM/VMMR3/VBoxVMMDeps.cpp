/* $Id: VBoxVMMDeps.cpp $ */
/** @file
 * VBoxVMM link dependencies - drag all we want into the link!
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
 */


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmqueue.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/dbg.h>
#include <VBox/vmm/ftm.h>

VMMR3DECL(int) VMMDoTest(PVM pVM);

/** Just a dummy global structure containing a bunch of
 * function pointers to code which is wanted in the link.
 */
PFNRT g_apfnDeps[] =
{
    (PFNRT)DBGFR3DisasInstrEx,
    (PFNRT)DBGFR3LogModifyFlags,
    (PFNRT)DBGFR3StackWalkEnd,
    (PFNRT)DBGFR3AsSymbolByAddr,
    (PFNRT)DBGFR3CpuGetMode,
    (PFNRT)DBGFR3CoreWrite,
    (PFNRT)DBGFR3MemScan,
    (PFNRT)DBGFR3RegCpuQueryU8,
    (PFNRT)EMInterpretInstruction,
    (PFNRT)IOMIOPortRead,
    (PFNRT)PDMQueueInsert,
    (PFNRT)PDMCritSectEnter,
    (PFNRT)PGMInvalidatePage,
    (PFNRT)PGMR3DbgR3Ptr2GCPhys,
    (PFNRT)VMR3Create,
    (PFNRT)VMMDoTest,
    (PFNRT)FTMR3PowerOn,
#ifdef VBOX_WITH_DEBUGGER
    (PFNRT)DBGCCreate,
#endif
#ifdef VBOX_WITH_PAGE_SHARING
    (PFNRT)PGMR3SharedModuleRegister,
#endif
    NULL
};
