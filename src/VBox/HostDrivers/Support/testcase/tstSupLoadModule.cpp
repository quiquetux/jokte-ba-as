/* $Id: tstSupLoadModule.cpp $ */
/** @file
 * SUP Testcase - Test SUPR3LoadModule.
 */

/*
 * Copyright (C) 2006-2009 Oracle Corporation
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


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/sup.h>
#include <VBox/err.h>

#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>


int main(int argc, char **argv)
{
    /*
     * Init.
     */
    int rc = RTR3InitAndSUPLib();
    if (RT_FAILURE(rc))
    {
        RTMsgError("RTR3InitAndSUPLib failed with rc=%Rrc\n", rc);
        return 1;
    }

    /*
     * Process arguments.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--help",             'h', RTGETOPT_REQ_NOTHING } /* (dummy entry) */
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1, 0);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (ch)
        {
            case VINF_GETOPT_NOT_OPTION:
            {
                void           *pvImageBase;
                RTERRINFOSTATIC ErrInfo;
                RTErrInfoInitStatic(&ErrInfo);
                rc = SUPR3LoadModule(ValueUnion.psz, RTPathFilename(ValueUnion.psz), &pvImageBase, &ErrInfo.Core);
                if (RT_FAILURE(rc))
                {
                    RTMsgError("%Rrc when attempting to load '%s': %s\n", rc, ValueUnion.psz, ErrInfo.Core.pszMsg);
                    return 1;
                }
                RTPrintf("Loaded '%s' at %p\n", ValueUnion.psz, pvImageBase);

                rc = SUPR3FreeModule(pvImageBase);
                if (RT_FAILURE(rc))
                {
                    RTMsgError("%Rrc when attempting to load '%s'\n", rc, ValueUnion.psz);
                    return 1;
                }
                break;
            }

            case 'h':
                RTPrintf("%s [mod1 [mod2...]]\n");
                return 1;

            case 'V':
                RTPrintf("$Revision: 69027 $\n");
                return 0;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }

    return 0;
}

