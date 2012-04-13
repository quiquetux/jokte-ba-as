/** @file
 *
 * VirtualBox X11 Additions mouse driver utility functions
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

#include <iprt/assert.h>
#include <iprt/err.h>
#include <VBox/VMMDev.h>
#include <VBox/VBoxGuestLib.h>
#include "VBoxUtils.h"

#include "xf86.h"
#define NEED_XF86_TYPES
#include <iprt/string.h>
#include "compiler.h"

/**
 * Have we ever failed to open the VBox device?  This is an ugly hack
 * to prevent the driver from being accessed when it is not open, as
 * I can't see anywhere good to store additional information in the driver
 * private data.
 */
static Bool gDeviceOpenFailed = FALSE;

int VBoxMouseInit(void)
{
    int rc;
    uint32_t fFeatures = 0;
    if (gDeviceOpenFailed)
        return 1;
    rc = VbglR3Init();
    if (RT_FAILURE(rc))
    {
        ErrorF("Failed to open the VirtualBox device, falling back to compatibility mouse mode.\n");
        gDeviceOpenFailed = TRUE;
        return 1;
    }

    rc = VbglR3SetMouseStatus(VMMDEV_MOUSE_GUEST_CAN_ABSOLUTE);
    if (RT_FAILURE(rc))
    {
        ErrorF("Error sending mouse pointer capabilities to VMM! rc = %d (%s)\n",
               errno, strerror(errno));
        gDeviceOpenFailed = TRUE;
        VbglR3Term();
        return 1;
    }
    xf86Msg(X_INFO, "VirtualBox mouse pointer integration available.\n");
    return 0;
}


/**
 * Query the absolute mouse position from the host
 * @returns VINF_SUCCESS or iprt error if the absolute values could not
 *          be queried, or the host wished to use relative coordinates
 * @param   pcx  where to return the pointer X coordinate
 * @param   pxy  where to return the pointer Y coordinate
 */
int VBoxMouseQueryPosition(unsigned int *pcx, unsigned int *pcy)
{
    int rc = VINF_SUCCESS;
    uint32_t cx, cy, fFeatures;

    AssertPtrReturn(pcx, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcy, VERR_INVALID_PARAMETER);
    if (gDeviceOpenFailed)
        rc = VERR_ACCESS_DENIED;
    if (RT_SUCCESS(rc))
        rc = VbglR3GetMouseStatus(&fFeatures, &cx, &cy);
    if (   RT_SUCCESS(rc)
        && !(fFeatures & VMMDEV_MOUSE_HOST_WANTS_ABSOLUTE))
        rc = VERR_NOT_SUPPORTED;
    if (RT_SUCCESS(rc))
    {
        *pcx = cx;
        *pcy = cy;
    }
    return rc;
}


int VBoxMouseFini(void)
{
    VbglR3Term();
    return VINF_SUCCESS;
}
