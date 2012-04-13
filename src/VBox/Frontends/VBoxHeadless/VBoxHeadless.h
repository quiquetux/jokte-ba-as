/** @file
 *
 * VBox frontends: VRDE (headless Remote Desktop server):
 * Header file with registration call for ffmpeg framebuffer
 */

/*
 * Copyright (C) 2006-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */

#ifndef __H_VBOXHEADLESS
#define __H_VBOXHEADLESS

#include <VBox/com/VirtualBox.h>

#include <iprt/uuid.h>

#include <VBox/com/com.h>
#include <VBox/com/string.h>

#include <iprt/initterm.h>
#include <iprt/critsect.h>

/**
 * Callback function to register an ffmpeg framebuffer.
 *
 * @returns COM status code.
 * @param   width        Framebuffer width.
 * @param   height       Framebuffer height.
 * @param   bitrate      Bitrate of mpeg file to be created.
 * @param   pixelFormat  Framebuffer pixel format
 * @param   filename     Name of mpeg file to be created
 * @retval  retVal       The new framebuffer
 */
typedef DECLCALLBACK(HRESULT) FNREGISTERFFMPEGFB(ULONG width,
                                     ULONG height, ULONG bitrate,
                                     com::Bstr filename,
                                     IFramebuffer **retVal);
typedef FNREGISTERFFMPEGFB *PFNREGISTERFFMPEGFB;

#endif // __H_VBOXHEADLESS
