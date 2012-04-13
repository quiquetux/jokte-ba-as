/* $Id: errorprint.cpp $ */

/** @file
 * MS COM / XPCOM Abstraction Layer:
 * Error info print helpers. This implements the shared code from the macros from errorprint.h.
 */

/*
 * Copyright (C) 2009-2010 Oracle Corporation
 *
 * This file is part of VirtualBox Open Source Edition (OSE), as
 * available from http://www.virtualbox.org. This file is free software;
 * you can redistribute it and/or modify it under the terms of the GNU
 * General Public License (GPL) as published by the Free Software
 * Foundation, in version 2 as it comes in the "COPYING" file of the
 * VirtualBox OSE distribution. VirtualBox OSE is distributed in the
 * hope that it will be useful, but WITHOUT ANY WARRANTY of any kind.
 */


#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/log.h>

#include <iprt/stream.h>
#include <iprt/message.h>
#include <iprt/path.h>

namespace com
{

void GluePrintErrorInfo(com::ErrorInfo &info)
{
    Utf8Str str = Utf8StrFmt("%ls\n"
                             "Details: code %Rhrc (0x%RX32), component %ls, interface %ls, callee %ls\n"
                             ,
                             info.getText().raw(),
                             info.getResultCode(),
                             info.getResultCode(),
                             info.getComponent().raw(),
                             info.getInterfaceName().raw(),
                             info.getCalleeName().raw());
    // print and log
    RTMsgError("%s", str.c_str());
    Log(("ERROR: %s", str.c_str()));
}

void GluePrintErrorContext(const char *pcszContext, const char *pcszSourceFile, uint32_t ulLine)
{
    // pcszSourceFile comes from __FILE__ macro, which always contains the full path,
    // which we don't want to see printed:
    Utf8Str strFilename(RTPathFilename(pcszSourceFile));
    Utf8Str str = Utf8StrFmt("Context: \"%s\" at line %d of file %s\n",
                                pcszContext,
                                ulLine,
                                strFilename.c_str());
    // print and log
    RTStrmPrintf(g_pStdErr, "%s", str.c_str());
    Log(("%s", str.c_str()));
}

void GluePrintRCMessage(HRESULT rc)
{
    Utf8Str str = Utf8StrFmt("Code %Rhra (extended info not available)\n", rc);
    // print and log
    RTMsgError("%s", str.c_str());
    Log(("ERROR: %s", str.c_str()));
}

void GlueHandleComError(ComPtr<IUnknown> iface,
                        const char *pcszContext,
                        HRESULT rc,
                        const char *pcszSourceFile,
                        uint32_t ulLine)
{
    // if we have full error info, print something nice, and start with the actual error message
    com::ErrorInfo info(iface, COM_IIDOF(IUnknown));
    if (info.isFullAvailable() || info.isBasicAvailable())
        GluePrintErrorInfo(info);
    else
        GluePrintRCMessage(rc);
    GluePrintErrorContext(pcszContext, pcszSourceFile, ulLine);
}


} /* namespace com */

