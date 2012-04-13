/* $Id: DBGCOps.cpp $ */
/** @file
 * DBGC - Debugger Console, Operators.
 */

/*
 * Copyright (C) 2006-2011 Oracle Corporation
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
#define LOG_GROUP LOG_GROUP_DBGC
#include <VBox/dbg.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "DBGCInternal.h"


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static DECLCALLBACK(int) dbgcOpMinus(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpPluss(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpBooleanNot(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpBitwiseNot(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpVar(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult);

static DECLCALLBACK(int) dbgcOpAddrFar(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpMult(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpDiv(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpMod(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpAdd(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpSub(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpBitwiseShiftLeft(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpBitwiseShiftRight(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpBitwiseAnd(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpBitwiseXor(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpBitwiseOr(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpBooleanAnd(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpBooleanOr(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpRangeLength(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpRangeLengthBytes(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);
static DECLCALLBACK(int) dbgcOpRangeTo(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult);


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/**
 * Generic implementation of a binary operator.
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc           Debugger console instance data.
 * @param   pArg1           The first argument.
 * @param   pArg2           The 2nd argument.
 * @param   pResult         Where to store the result.
 * @param   Operator        The C operator.
 * @param   fIsDiv          Set if it's division and we need to check for zero on the
 *                          right hand side.
 */
#define DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, Operator, fIsDiv) \
    do \
    { \
        /* Get the 64-bit right side value. */ \
        uint64_t u64Right; \
        int rc = dbgcOpHelperGetNumber((pDbgc), (pArg2), &u64Right); \
        if ((fIsDiv) && RT_SUCCESS(rc) && !u64Right) /* div/0 kludge */ \
            DBGCVAR_INIT_NUMBER((pResult), UINT64_MAX); \
        else if (RT_SUCCESS(rc)) \
        { \
            /* Apply it to the left hand side. */ \
            if (   (pArg1)->enmType == DBGCVAR_TYPE_SYMBOL \
                || (pArg1)->enmType == DBGCVAR_TYPE_STRING) \
            { \
                rc = dbgcSymbolGet((pDbgc), (pArg1)->u.pszString, DBGCVAR_TYPE_ANY, (pResult)); \
                if (RT_FAILURE(rc)) \
                    return rc; \
            } \
            else \
                *(pResult) = *(pArg1); \
            switch ((pResult)->enmType) \
            { \
                case DBGCVAR_TYPE_GC_FLAT: \
                    (pResult)->u.GCFlat     = (pResult)->u.GCFlat     Operator  u64Right; \
                    break; \
                case DBGCVAR_TYPE_GC_FAR: \
                    (pResult)->u.GCFar.off  = (pResult)->u.GCFar.off  Operator  u64Right; \
                    break; \
                case DBGCVAR_TYPE_GC_PHYS: \
                    (pResult)->u.GCPhys     = (pResult)->u.GCPhys     Operator  u64Right; \
                    break; \
                case DBGCVAR_TYPE_HC_FLAT: \
                    (pResult)->u.pvHCFlat   = (void *)((uintptr_t)(pResult)->u.pvHCFlat  Operator  u64Right); \
                    break; \
                case DBGCVAR_TYPE_HC_PHYS: \
                    (pResult)->u.HCPhys     = (pResult)->u.HCPhys     Operator  u64Right; \
                    break; \
                case DBGCVAR_TYPE_NUMBER: \
                    (pResult)->u.u64Number  = (pResult)->u.u64Number  Operator  u64Right; \
                    break; \
                default: \
                    return VERR_PARSE_INCORRECT_ARG_TYPE; \
            } \
        } \
        return rc; \
    } while (0)


/**
 * Switch the factors/whatever so we preserve pointers.
 * Far pointers are considered more  important that physical and flat pointers.
 *
 * @param   pArg1           The left side argument.  Input & output.
 * @param   pArg2           The right side argument.  Input & output.
 */
#define DBGC_GEN_ARIT_POINTER_TO_THE_LEFT(pArg1, pArg2) \
    do \
    { \
        if (    DBGCVAR_ISPOINTER((pArg2)->enmType) \
            &&  (   !DBGCVAR_ISPOINTER((pArg1)->enmType) \
                 || (   DBGCVAR_IS_FAR_PTR((pArg2)->enmType) \
                     && !DBGCVAR_IS_FAR_PTR((pArg1)->enmType)))) \
        { \
            PCDBGCVAR pTmp = (pArg1); \
            (pArg2) = (pArg1); \
            (pArg1) = pTmp; \
        } \
    } while (0)


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/** Operators. */
const DBGCOP g_aOps[] =
{
    /* szName is initialized as a 4 char array because of M$C elsewise optimizing it away in /Ox mode (the 'const char' vs 'char' problem). */
    /* szName,          cchName, fBinary,    iPrecedence,    pfnHandlerUnary,    pfnHandlerBitwise  */
    { {'-'},            1,       false,      1,              dbgcOpMinus,        NULL,                       DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Unary minus." },
    { {'+'},            1,       false,      1,              dbgcOpPluss,        NULL,                       DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Unary plus."  },
    { {'!'},            1,       false,      1,              dbgcOpBooleanNot,   NULL,                       DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Boolean not." },
    { {'~'},            1,       false,      1,              dbgcOpBitwiseNot,   NULL,                       DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Bitwise complement." },
    { {':'},            1,       true,       2,              NULL,               dbgcOpAddrFar,              DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Far pointer." },
    { {'%'},            1,       false,      3,              dbgcOpAddrFlat,     NULL,                       DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Flat address." },
    { {'%','%'},        2,       false,      3,              dbgcOpAddrPhys,     NULL,                       DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Physical address." },
    { {'#'},            1,       false,      3,              dbgcOpAddrHost,     NULL,                       DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Flat host address." },
    { {'#','%','%'},    3,       false,      3,              dbgcOpAddrHostPhys, NULL,                       DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Physical host address." },
    { {'$'},            1,       false,      3,              dbgcOpVar,          NULL,                       DBGCVAR_CAT_STRING, DBGCVAR_CAT_ANY, "Reference a variable." },
    { {'@'},            1,       false,      3,              dbgcOpRegister,     NULL,                       DBGCVAR_CAT_STRING, DBGCVAR_CAT_ANY, "Reference a register." },
    { {'*'},            1,       true,       10,             NULL,               dbgcOpMult,                 DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Multiplication." },
    { {'/'},            1,       true,       11,             NULL,               dbgcOpDiv,                  DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Division." },
    { {'%'},            1,       true,       12,             NULL,               dbgcOpMod,                  DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Modulus." },
    { {'+'},            1,       true,       13,             NULL,               dbgcOpAdd,                  DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Addition." },
    { {'-'},            1,       true,       14,             NULL,               dbgcOpSub,                  DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Subtraction." },
    { {'<','<'},        2,       true,       15,             NULL,               dbgcOpBitwiseShiftLeft,     DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Bitwise left shift." },
    { {'>','>'},        2,       true,       16,             NULL,               dbgcOpBitwiseShiftRight,    DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Bitwise right shift." },
    { {'&'},            1,       true,       17,             NULL,               dbgcOpBitwiseAnd,           DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Bitwise and." },
    { {'^'},            1,       true,       18,             NULL,               dbgcOpBitwiseXor,           DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Bitwise exclusiv or." },
    { {'|'},            1,       true,       19,             NULL,               dbgcOpBitwiseOr,            DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Bitwise inclusive or." },
    { {'&','&'},        2,       true,       20,             NULL,               dbgcOpBooleanAnd,           DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Boolean and." },
    { {'|','|'},        2,       true,       21,             NULL,               dbgcOpBooleanOr,            DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Boolean or." },
    { {'L'},            1,       true,       22,             NULL,               dbgcOpRangeLength,          DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Range elements." },
    { {'L','B'},        2,       true,       23,             NULL,               dbgcOpRangeLengthBytes,     DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Range bytes." },
    { {'T'},            1,       true,       24,             NULL,               dbgcOpRangeTo,              DBGCVAR_CAT_ANY,    DBGCVAR_CAT_ANY, "Range to." }
};

/** Number of operators in the operator array. */
const unsigned g_cOps = RT_ELEMENTS(g_aOps);


/**
 * Converts an argument to a number value.
 *
 * @returns VBox status code.
 * @param   pDbgc               The DBGC instance.
 * @param   pArg                The argument to convert.
 * @param   pu64Ret             Where to return the value.
 */
static int dbgcOpHelperGetNumber(PDBGC pDbgc, PCDBGCVAR pArg, uint64_t *pu64Ret)
{
    DBGCVAR Var = *pArg;
    switch (Var.enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
            *pu64Ret = Var.u.GCFlat;
            break;
        case DBGCVAR_TYPE_GC_FAR:
            *pu64Ret = Var.u.GCFar.off;
            break;
        case DBGCVAR_TYPE_GC_PHYS:
            *pu64Ret = Var.u.GCPhys;
            break;
        case DBGCVAR_TYPE_HC_FLAT:
            *pu64Ret = (uintptr_t)Var.u.pvHCFlat;
            break;
        case DBGCVAR_TYPE_HC_PHYS:
            *pu64Ret = Var.u.HCPhys;
            break;
        case DBGCVAR_TYPE_STRING:
        case DBGCVAR_TYPE_SYMBOL:
        {
            int rc = dbgcSymbolGet(pDbgc, Var.u.pszString, DBGCVAR_TYPE_NUMBER, &Var);
            if (RT_FAILURE(rc))
                return rc;
            /* fall thru */
        }
        case DBGCVAR_TYPE_NUMBER:
            *pu64Ret = Var.u.u64Number;
            break;
        default:
            return VERR_PARSE_INCORRECT_ARG_TYPE;
    }
    return VINF_SUCCESS;
}


/**
 * Minus (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpMinus(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpMinus\n"));
    *pResult = *pArg;
    switch (pArg->enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
            pResult->u.GCFlat       = -(RTGCINTPTR)pResult->u.GCFlat;
            break;
        case DBGCVAR_TYPE_GC_FAR:
            pResult->u.GCFar.off    = -(int32_t)pResult->u.GCFar.off;
            break;
        case DBGCVAR_TYPE_GC_PHYS:
            pResult->u.GCPhys       = (RTGCPHYS) -(int64_t)pResult->u.GCPhys;
            break;
        case DBGCVAR_TYPE_HC_FLAT:
            pResult->u.pvHCFlat     = (void *) -(intptr_t)pResult->u.pvHCFlat;
            break;
        case DBGCVAR_TYPE_HC_PHYS:
            pResult->u.HCPhys       = (RTHCPHYS) -(int64_t)pResult->u.HCPhys;
            break;
        case DBGCVAR_TYPE_NUMBER:
            pResult->u.u64Number    = -(int64_t)pResult->u.u64Number;
            break;

        case DBGCVAR_TYPE_UNKNOWN:
        case DBGCVAR_TYPE_STRING:
        default:
            return VERR_PARSE_INCORRECT_ARG_TYPE;
    }
    NOREF(pDbgc);
    return VINF_SUCCESS;
}


/**
 * Plus (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpPluss(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpPluss\n"));
    *pResult = *pArg;
    switch (pArg->enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
        case DBGCVAR_TYPE_GC_FAR:
        case DBGCVAR_TYPE_GC_PHYS:
        case DBGCVAR_TYPE_HC_FLAT:
        case DBGCVAR_TYPE_HC_PHYS:
        case DBGCVAR_TYPE_NUMBER:
            break;

        case DBGCVAR_TYPE_UNKNOWN:
        case DBGCVAR_TYPE_STRING:
        default:
            return VERR_PARSE_INCORRECT_ARG_TYPE;
    }
    NOREF(pDbgc);
    return VINF_SUCCESS;
}


/**
 * Boolean not (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpBooleanNot(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpBooleanNot\n"));
    *pResult = *pArg;
    switch (pArg->enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
            pResult->u.u64Number    = !pResult->u.GCFlat;
            break;
        case DBGCVAR_TYPE_GC_FAR:
            pResult->u.u64Number    = !pResult->u.GCFar.off && pResult->u.GCFar.sel <= 3;
            break;
        case DBGCVAR_TYPE_GC_PHYS:
            pResult->u.u64Number    = !pResult->u.GCPhys;
            break;
        case DBGCVAR_TYPE_HC_FLAT:
            pResult->u.u64Number    = !pResult->u.pvHCFlat;
            break;
        case DBGCVAR_TYPE_HC_PHYS:
            pResult->u.u64Number    = !pResult->u.HCPhys;
            break;
        case DBGCVAR_TYPE_NUMBER:
            pResult->u.u64Number    = !pResult->u.u64Number;
            break;
        case DBGCVAR_TYPE_STRING:
            pResult->u.u64Number    = !pResult->u64Range;
            break;

        case DBGCVAR_TYPE_UNKNOWN:
        default:
            return VERR_PARSE_INCORRECT_ARG_TYPE;
    }
    pResult->enmType = DBGCVAR_TYPE_NUMBER;
    NOREF(pDbgc);
    return VINF_SUCCESS;
}


/**
 * Bitwise not (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpBitwiseNot(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpBitwiseNot\n"));
    *pResult = *pArg;
    switch (pArg->enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
            pResult->u.GCFlat       = ~pResult->u.GCFlat;
            break;
        case DBGCVAR_TYPE_GC_FAR:
            pResult->u.GCFar.off    = ~pResult->u.GCFar.off;
            break;
        case DBGCVAR_TYPE_GC_PHYS:
            pResult->u.GCPhys       = ~pResult->u.GCPhys;
            break;
        case DBGCVAR_TYPE_HC_FLAT:
            pResult->u.pvHCFlat     = (void *)~(uintptr_t)pResult->u.pvHCFlat;
            break;
        case DBGCVAR_TYPE_HC_PHYS:
            pResult->u.HCPhys       = ~pResult->u.HCPhys;
            break;
        case DBGCVAR_TYPE_NUMBER:
            pResult->u.u64Number    = ~pResult->u.u64Number;
            break;

        case DBGCVAR_TYPE_UNKNOWN:
        case DBGCVAR_TYPE_STRING:
        default:
            return VERR_PARSE_INCORRECT_ARG_TYPE;
    }
    NOREF(pDbgc);
    return VINF_SUCCESS;
}


/**
 * Reference variable (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpVar(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpVar: %s\n", pArg->u.pszString));

    /*
     * Parse sanity.
     */
    if (pArg->enmType != DBGCVAR_TYPE_STRING)
        return VERR_PARSE_INCORRECT_ARG_TYPE;

    /*
     * Lookup the variable.
     */
    const char *pszVar = pArg->u.pszString;
    for (unsigned iVar = 0; iVar < pDbgc->cVars; iVar++)
    {
        if (!strcmp(pszVar, pDbgc->papVars[iVar]->szName))
        {
            *pResult = pDbgc->papVars[iVar]->Var;
            return VINF_SUCCESS;
        }
    }

    return VERR_PARSE_VARIABLE_NOT_FOUND;
}


/**
 * Reference register (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
DECLCALLBACK(int) dbgcOpRegister(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpRegister: %s\n", pArg->u.pszString));

    /*
     * Parse sanity.
     */
    if (   pArg->enmType != DBGCVAR_TYPE_STRING
        && pArg->enmType != DBGCVAR_TYPE_SYMBOL)
        return VERR_PARSE_INCORRECT_ARG_TYPE;

    /*
     * Get the register.
     */
    DBGFREGVALTYPE  enmType;
    DBGFREGVAL      Value;
    int rc = DBGFR3RegNmQuery(pDbgc->pVM, pDbgc->idCpu, pArg->u.pszString, &Value, &enmType);
    if (RT_SUCCESS(rc))
    {
        switch (enmType)
        {
            case DBGFREGVALTYPE_U8:
                DBGCVAR_INIT_NUMBER(pResult, Value.u8);
                return VINF_SUCCESS;

            case DBGFREGVALTYPE_U16:
                DBGCVAR_INIT_NUMBER(pResult, Value.u16);
                return VINF_SUCCESS;

            case DBGFREGVALTYPE_U32:
                DBGCVAR_INIT_NUMBER(pResult, Value.u32);
                return VINF_SUCCESS;

            case DBGFREGVALTYPE_U64:
                DBGCVAR_INIT_NUMBER(pResult, Value.u64);
                return VINF_SUCCESS;

            case DBGFREGVALTYPE_U128:
                DBGCVAR_INIT_NUMBER(pResult, Value.u128.s.Lo);
                return VINF_SUCCESS;

            case DBGFREGVALTYPE_R80:
#ifdef RT_COMPILER_WITH_80BIT_LONG_DOUBLE
                DBGCVAR_INIT_NUMBER(pResult, (uint64_t)Value.r80.lrd);
#else
                DBGCVAR_INIT_NUMBER(pResult, (uint64_t)Value.r80.sj64.u63Fraction);
#endif
                return VINF_SUCCESS;

            case DBGFREGVALTYPE_DTR:
                DBGCVAR_INIT_NUMBER(pResult, Value.dtr.u64Base);
                return VINF_SUCCESS;

            case DBGFREGVALTYPE_INVALID:
            case DBGFREGVALTYPE_END:
            case DBGFREGVALTYPE_32BIT_HACK:
                break;
        }
        rc = VERR_INTERNAL_ERROR_5;
    }
    return rc;
}


/**
 * Flat address (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
DECLCALLBACK(int) dbgcOpAddrFlat(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpAddrFlat\n"));
    DBGCVARTYPE enmType = DBGCVAR_ISHCPOINTER(pArg->enmType) ? DBGCVAR_TYPE_HC_FLAT : DBGCVAR_TYPE_GC_FLAT;
    return DBGCCmdHlpConvert(&pDbgc->CmdHlp, pArg, enmType, true /*fConvSyms*/, pResult);
}


/**
 * Physical address (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
DECLCALLBACK(int) dbgcOpAddrPhys(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpAddrPhys\n"));
    DBGCVARTYPE enmType = DBGCVAR_ISHCPOINTER(pArg->enmType) ? DBGCVAR_TYPE_HC_PHYS : DBGCVAR_TYPE_GC_PHYS;
    return DBGCCmdHlpConvert(&pDbgc->CmdHlp, pArg, enmType, true /*fConvSyms*/, pResult);
}


/**
 * Physical host address (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
DECLCALLBACK(int) dbgcOpAddrHostPhys(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpAddrPhys\n"));
    return DBGCCmdHlpConvert(&pDbgc->CmdHlp, pArg, DBGCVAR_TYPE_HC_PHYS, true /*fConvSyms*/, pResult);
}


/**
 * Host address (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
DECLCALLBACK(int) dbgcOpAddrHost(PDBGC pDbgc, PCDBGCVAR pArg, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpAddrHost\n"));
    return DBGCCmdHlpConvert(&pDbgc->CmdHlp, pArg, DBGCVAR_TYPE_HC_FLAT, true /*fConvSyms*/, pResult);
}


/**
 * Bitwise not (unary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpAddrFar(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpAddrFar\n"));
    int     rc;

    switch (pArg1->enmType)
    {
        case DBGCVAR_TYPE_STRING:
            rc = dbgcSymbolGet(pDbgc, pArg1->u.pszString, DBGCVAR_TYPE_NUMBER, pResult);
            if (RT_FAILURE(rc))
                return rc;
            break;
        case DBGCVAR_TYPE_NUMBER:
            *pResult = *pArg1;
            break;

        case DBGCVAR_TYPE_GC_FLAT:
        case DBGCVAR_TYPE_GC_FAR:
        case DBGCVAR_TYPE_GC_PHYS:
        case DBGCVAR_TYPE_HC_FLAT:
        case DBGCVAR_TYPE_HC_PHYS:
        case DBGCVAR_TYPE_UNKNOWN:
        default:
            return VERR_PARSE_INCORRECT_ARG_TYPE;
    }
    pResult->u.GCFar.sel = (RTSEL)pResult->u.u64Number;

    /* common code for the two types we support. */
    switch (pArg2->enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
            pResult->u.GCFar.off = pArg2->u.GCFlat;
            pResult->enmType    = DBGCVAR_TYPE_GC_FAR;
            break;

        case DBGCVAR_TYPE_HC_FLAT:
            pResult->u.pvHCFlat = (void *)(uintptr_t)pArg2->u.GCFlat;
            pResult->enmType    = DBGCVAR_TYPE_GC_FAR;
            break;

        case DBGCVAR_TYPE_NUMBER:
            pResult->u.GCFar.off = (RTGCPTR)pArg2->u.u64Number;
            pResult->enmType    = DBGCVAR_TYPE_GC_FAR;
            break;

        case DBGCVAR_TYPE_STRING:
        {
            DBGCVAR Var;
            rc = dbgcSymbolGet(pDbgc, pArg2->u.pszString, DBGCVAR_TYPE_NUMBER, &Var);
            if (RT_FAILURE(rc))
                return rc;
            pResult->u.GCFar.off = (RTGCPTR)Var.u.u64Number;
            pResult->enmType    = DBGCVAR_TYPE_GC_FAR;
            break;
        }

        case DBGCVAR_TYPE_GC_FAR:
        case DBGCVAR_TYPE_GC_PHYS:
        case DBGCVAR_TYPE_HC_PHYS:
        case DBGCVAR_TYPE_UNKNOWN:
        default:
            return VERR_PARSE_INCORRECT_ARG_TYPE;
    }
    return VINF_SUCCESS;

}


/**
 * Multiplication operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpMult(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpMult\n"));
    DBGC_GEN_ARIT_POINTER_TO_THE_LEFT(pArg1, pArg2);
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, *, false);
}


/**
 * Division operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpDiv(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpDiv\n"));
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, /, true);
}


/**
 * Modulus operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpMod(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpMod\n"));
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, %, false);
}


/**
 * Addition operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpAdd(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpAdd\n"));

    /*
     * An addition operation will return (when possible) the left side type in the
     * expression. We make an omission for numbers, where we'll take the right side
     * type instead. An expression where only the left hand side is a string we'll
     * use the right hand type assuming that the string is a symbol.
     */
    if (    (pArg1->enmType == DBGCVAR_TYPE_NUMBER && pArg2->enmType != DBGCVAR_TYPE_STRING)
        ||  (pArg1->enmType == DBGCVAR_TYPE_STRING && pArg2->enmType != DBGCVAR_TYPE_STRING))
    {
        PCDBGCVAR pTmp = pArg2;
        pArg2 = pArg1;
        pArg1 = pTmp;
    }
    DBGCVAR     Sym1, Sym2;
    if (pArg1->enmType == DBGCVAR_TYPE_STRING)
    {
        int rc = dbgcSymbolGet(pDbgc, pArg1->u.pszString, DBGCVAR_TYPE_ANY, &Sym1);
        if (RT_FAILURE(rc))
            return rc;
        pArg1 = &Sym1;

        rc = dbgcSymbolGet(pDbgc, pArg2->u.pszString, DBGCVAR_TYPE_ANY, &Sym2);
        if (RT_FAILURE(rc))
            return rc;
        pArg2 = &Sym2;
    }

    int         rc;
    DBGCVAR     Var;
    DBGCVAR     Var2;
    switch (pArg1->enmType)
    {
        /*
         * GC Flat
         */
        case DBGCVAR_TYPE_GC_FLAT:
            switch (pArg2->enmType)
            {
                case DBGCVAR_TYPE_HC_FLAT:
                case DBGCVAR_TYPE_HC_PHYS:
                    return VERR_PARSE_INVALID_OPERATION;
                default:
                    *pResult = *pArg1;
                    rc = dbgcOpAddrFlat(pDbgc, pArg2, &Var);
                    if (RT_FAILURE(rc))
                        return rc;
                    pResult->u.GCFlat += pArg2->u.GCFlat;
                    break;
            }
            break;

        /*
         * GC Far
         */
        case DBGCVAR_TYPE_GC_FAR:
            switch (pArg2->enmType)
            {
                case DBGCVAR_TYPE_HC_FLAT:
                case DBGCVAR_TYPE_HC_PHYS:
                    return VERR_PARSE_INVALID_OPERATION;
                case DBGCVAR_TYPE_NUMBER:
                    *pResult = *pArg1;
                    pResult->u.GCFar.off += (RTGCPTR)pArg2->u.u64Number;
                    break;
                default:
                    rc = dbgcOpAddrFlat(pDbgc, pArg1, pResult);
                    if (RT_FAILURE(rc))
                        return rc;
                    rc = dbgcOpAddrFlat(pDbgc, pArg2, &Var);
                    if (RT_FAILURE(rc))
                        return rc;
                    pResult->u.GCFlat += pArg2->u.GCFlat;
                    break;
            }
            break;

        /*
         * GC Phys
         */
        case DBGCVAR_TYPE_GC_PHYS:
            switch (pArg2->enmType)
            {
                case DBGCVAR_TYPE_HC_FLAT:
                case DBGCVAR_TYPE_HC_PHYS:
                    return VERR_PARSE_INVALID_OPERATION;
                default:
                    *pResult = *pArg1;
                    rc = dbgcOpAddrPhys(pDbgc, pArg2, &Var);
                    if (RT_FAILURE(rc))
                        return rc;
                    if (Var.enmType != DBGCVAR_TYPE_GC_PHYS)
                        return VERR_PARSE_INVALID_OPERATION;
                    pResult->u.GCPhys += Var.u.GCPhys;
                    break;
            }
            break;

        /*
         * HC Flat
         */
        case DBGCVAR_TYPE_HC_FLAT:
            *pResult = *pArg1;
            rc = dbgcOpAddrHost(pDbgc, pArg2, &Var2);
            if (RT_FAILURE(rc))
                return rc;
            rc = dbgcOpAddrFlat(pDbgc, &Var2, &Var);
            if (RT_FAILURE(rc))
                return rc;
            pResult->u.pvHCFlat = (char *)pResult->u.pvHCFlat + (uintptr_t)Var.u.pvHCFlat;
            break;

        /*
         * HC Phys
         */
        case DBGCVAR_TYPE_HC_PHYS:
            *pResult = *pArg1;
            rc = dbgcOpAddrHostPhys(pDbgc, pArg2, &Var);
            if (RT_FAILURE(rc))
                return rc;
            pResult->u.HCPhys += Var.u.HCPhys;
            break;

        /*
         * Numbers (see start of function)
         */
        case DBGCVAR_TYPE_NUMBER:
            *pResult = *pArg1;
            switch (pArg2->enmType)
            {
                case DBGCVAR_TYPE_SYMBOL:
                case DBGCVAR_TYPE_STRING:
                    rc = dbgcSymbolGet(pDbgc, pArg2->u.pszString, DBGCVAR_TYPE_NUMBER, &Var);
                    if (RT_FAILURE(rc))
                        return rc;
                case DBGCVAR_TYPE_NUMBER:
                    pResult->u.u64Number += pArg2->u.u64Number;
                    break;
                default:
                    return VERR_PARSE_INVALID_OPERATION;
            }
            break;

        default:
            return VERR_PARSE_INVALID_OPERATION;

    }
    return VINF_SUCCESS;
}


/**
 * Subtraction operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpSub(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpSub\n"));

    /*
     * An subtraction operation will return the left side type in the expression.
     * However, if the left hand side is a number and the right hand a pointer of
     * some kind we'll convert the left hand side to the same type as the right hand.
     * Any strings will be attempted resolved as symbols.
     */
    DBGCVAR     Sym1, Sym2;
    if (    pArg2->enmType == DBGCVAR_TYPE_STRING
        &&  (   pArg1->enmType == DBGCVAR_TYPE_NUMBER
             || pArg1->enmType == DBGCVAR_TYPE_STRING))
    {
        int rc = dbgcSymbolGet(pDbgc, pArg2->u.pszString, DBGCVAR_TYPE_ANY, &Sym2);
        if (RT_FAILURE(rc))
            return rc;
        pArg2 = &Sym2;
    }

    if (pArg1->enmType == DBGCVAR_TYPE_STRING)
    {
        DBGCVARTYPE enmType;
        switch (pArg2->enmType)
        {
            case DBGCVAR_TYPE_NUMBER:
                enmType = DBGCVAR_TYPE_ANY;
                break;
            case DBGCVAR_TYPE_GC_FLAT:
            case DBGCVAR_TYPE_GC_PHYS:
            case DBGCVAR_TYPE_HC_FLAT:
            case DBGCVAR_TYPE_HC_PHYS:
                enmType = pArg2->enmType;
                break;
            case DBGCVAR_TYPE_GC_FAR:
                enmType = DBGCVAR_TYPE_GC_FLAT;
                break;

            default:
            case DBGCVAR_TYPE_STRING:
                AssertMsgFailed(("Can't happen\n"));
                enmType = DBGCVAR_TYPE_STRING;
                break;
        }
        if (enmType != DBGCVAR_TYPE_STRING)
        {
            int rc = dbgcSymbolGet(pDbgc, pArg1->u.pszString, DBGCVAR_TYPE_ANY, &Sym1);
            if (RT_FAILURE(rc))
                return rc;
            pArg1 = &Sym1;
        }
    }
    else if (pArg1->enmType == DBGCVAR_TYPE_NUMBER)
    {
        PFNDBGCOPUNARY pOp = NULL;
        switch (pArg2->enmType)
        {
            case DBGCVAR_TYPE_GC_FAR:
            case DBGCVAR_TYPE_GC_FLAT:
                pOp = dbgcOpAddrFlat;
                break;
            case DBGCVAR_TYPE_GC_PHYS:
                pOp = dbgcOpAddrPhys;
                break;
            case DBGCVAR_TYPE_HC_FLAT:
                pOp = dbgcOpAddrHost;
                break;
            case DBGCVAR_TYPE_HC_PHYS:
                pOp = dbgcOpAddrHostPhys;
                break;
            case DBGCVAR_TYPE_NUMBER:
                break;
            default:
            case DBGCVAR_TYPE_STRING:
                AssertMsgFailed(("Can't happen\n"));
                break;
        }
        if (pOp)
        {
            int rc = pOp(pDbgc, pArg1, &Sym1);
            if (RT_FAILURE(rc))
                return rc;
            pArg1 = &Sym1;
        }
    }


    /*
     * Normal processing.
     */
    int         rc;
    DBGCVAR     Var;
    DBGCVAR     Var2;
    switch (pArg1->enmType)
    {
        /*
         * GC Flat
         */
        case DBGCVAR_TYPE_GC_FLAT:
            switch (pArg2->enmType)
            {
                case DBGCVAR_TYPE_HC_FLAT:
                case DBGCVAR_TYPE_HC_PHYS:
                    return VERR_PARSE_INVALID_OPERATION;
                default:
                    *pResult = *pArg1;
                    rc = dbgcOpAddrFlat(pDbgc, pArg2, &Var);
                    if (RT_FAILURE(rc))
                        return rc;
                    pResult->u.GCFlat -= pArg2->u.GCFlat;
                    break;
            }
            break;

        /*
         * GC Far
         */
        case DBGCVAR_TYPE_GC_FAR:
            switch (pArg2->enmType)
            {
                case DBGCVAR_TYPE_HC_FLAT:
                case DBGCVAR_TYPE_HC_PHYS:
                    return VERR_PARSE_INVALID_OPERATION;
                case DBGCVAR_TYPE_NUMBER:
                    *pResult = *pArg1;
                    pResult->u.GCFar.off -= (RTGCPTR)pArg2->u.u64Number;
                    break;
                default:
                    rc = dbgcOpAddrFlat(pDbgc, pArg1, pResult);
                    if (RT_FAILURE(rc))
                        return rc;
                    rc = dbgcOpAddrFlat(pDbgc, pArg2, &Var);
                    if (RT_FAILURE(rc))
                        return rc;
                    pResult->u.GCFlat -= pArg2->u.GCFlat;
                    break;
            }
            break;

        /*
         * GC Phys
         */
        case DBGCVAR_TYPE_GC_PHYS:
            switch (pArg2->enmType)
            {
                case DBGCVAR_TYPE_HC_FLAT:
                case DBGCVAR_TYPE_HC_PHYS:
                    return VERR_PARSE_INVALID_OPERATION;
                default:
                    *pResult = *pArg1;
                    rc = dbgcOpAddrPhys(pDbgc, pArg2, &Var);
                    if (RT_FAILURE(rc))
                        return rc;
                    if (Var.enmType != DBGCVAR_TYPE_GC_PHYS)
                        return VERR_PARSE_INVALID_OPERATION;
                    pResult->u.GCPhys -= Var.u.GCPhys;
                    break;
            }
            break;

        /*
         * HC Flat
         */
        case DBGCVAR_TYPE_HC_FLAT:
            *pResult = *pArg1;
            rc = dbgcOpAddrHost(pDbgc, pArg2, &Var2);
            if (RT_FAILURE(rc))
                return rc;
            rc = dbgcOpAddrFlat(pDbgc, &Var2, &Var);
            if (RT_FAILURE(rc))
                return rc;
            pResult->u.pvHCFlat = (char *)pResult->u.pvHCFlat - (uintptr_t)Var.u.pvHCFlat;
            break;

        /*
         * HC Phys
         */
        case DBGCVAR_TYPE_HC_PHYS:
            *pResult = *pArg1;
            rc = dbgcOpAddrHostPhys(pDbgc, pArg2, &Var);
            if (RT_FAILURE(rc))
                return rc;
            pResult->u.HCPhys -= Var.u.HCPhys;
            break;

        /*
         * Numbers (see start of function)
         */
        case DBGCVAR_TYPE_NUMBER:
            *pResult = *pArg1;
            switch (pArg2->enmType)
            {
                case DBGCVAR_TYPE_SYMBOL:
                case DBGCVAR_TYPE_STRING:
                    rc = dbgcSymbolGet(pDbgc, pArg2->u.pszString, DBGCVAR_TYPE_NUMBER, &Var);
                    if (RT_FAILURE(rc))
                        return rc;
                case DBGCVAR_TYPE_NUMBER:
                    pResult->u.u64Number -= pArg2->u.u64Number;
                    break;
                default:
                    return VERR_PARSE_INVALID_OPERATION;
            }
            break;

        default:
            return VERR_PARSE_INVALID_OPERATION;

    }
    return VINF_SUCCESS;
}


/**
 * Bitwise shift left operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpBitwiseShiftLeft(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpBitwiseShiftLeft\n"));
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, <<, false);
}


/**
 * Bitwise shift right operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpBitwiseShiftRight(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpBitwiseShiftRight\n"));
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, >>, false);
}


/**
 * Bitwise and operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpBitwiseAnd(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpBitwiseAnd\n"));
    DBGC_GEN_ARIT_POINTER_TO_THE_LEFT(pArg1, pArg2);
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, &, false);
}


/**
 * Bitwise exclusive or operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpBitwiseXor(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpBitwiseXor\n"));
    DBGC_GEN_ARIT_POINTER_TO_THE_LEFT(pArg1, pArg2);
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, ^, false);
}


/**
 * Bitwise inclusive or operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpBitwiseOr(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpBitwiseOr\n"));
    DBGC_GEN_ARIT_POINTER_TO_THE_LEFT(pArg1, pArg2);
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, |, false);
}


/**
 * Boolean and operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpBooleanAnd(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpBooleanAnd\n"));
    /** @todo force numeric return value? */
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, &&, false);
}


/**
 * Boolean or operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpBooleanOr(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpBooleanOr\n"));
    /** @todo force numeric return value? */
    DBGC_GEN_ARIT_BINARY_OP(pDbgc, pArg1, pArg2, pResult, ||, false);
}


/**
 * Range to operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpRangeLength(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpRangeLength\n"));

    /*
     * Make result. Strings needs to be resolved into symbols.
     */
    if (pArg1->enmType == DBGCVAR_TYPE_STRING)
    {
        int rc = dbgcSymbolGet(pDbgc, pArg1->u.pszString, DBGCVAR_TYPE_ANY, pResult);
        if (RT_FAILURE(rc))
            return rc;
    }
    else
        *pResult = *pArg1;

    /*
     * Convert 2nd argument to element count.
     */
    pResult->enmRangeType = DBGCVAR_RANGE_ELEMENTS;
    switch (pArg2->enmType)
    {
        case DBGCVAR_TYPE_NUMBER:
            pResult->u64Range = pArg2->u.u64Number;
            break;

        case DBGCVAR_TYPE_STRING:
        {
            int rc = dbgcSymbolGet(pDbgc, pArg2->u.pszString, DBGCVAR_TYPE_NUMBER, pResult);
            if (RT_FAILURE(rc))
                return rc;
            pResult->u64Range = pArg2->u.u64Number;
            break;
        }

        default:
            return VERR_PARSE_INVALID_OPERATION;
    }

    return VINF_SUCCESS;
}


/**
 * Range to operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpRangeLengthBytes(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpRangeLengthBytes\n"));
    int rc = dbgcOpRangeLength(pDbgc, pArg1, pArg2, pResult);
    if (RT_SUCCESS(rc))
        pResult->enmRangeType = DBGCVAR_RANGE_BYTES;
    return rc;
}


/**
 * Range to operator (binary).
 *
 * @returns VINF_SUCCESS on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
static DECLCALLBACK(int) dbgcOpRangeTo(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult)
{
    LogFlow(("dbgcOpRangeTo\n"));

    /*
     * Calc number of bytes between the two args.
     */
    DBGCVAR Diff;
    int rc = dbgcOpSub(pDbgc, pArg2, pArg1, &Diff);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Use the diff as the range of Arg1.
     */
    *pResult = *pArg1;
    pResult->enmRangeType = DBGCVAR_RANGE_BYTES;
    switch (Diff.enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
            pResult->u64Range = (RTGCUINTPTR)Diff.u.GCFlat;
            break;
        case DBGCVAR_TYPE_GC_PHYS:
            pResult->u64Range = Diff.u.GCPhys;
            break;
        case DBGCVAR_TYPE_HC_FLAT:
            pResult->u64Range = (uintptr_t)Diff.u.pvHCFlat;
            break;
        case DBGCVAR_TYPE_HC_PHYS:
            pResult->u64Range = Diff.u.HCPhys;
            break;
        case DBGCVAR_TYPE_NUMBER:
            pResult->u64Range = Diff.u.u64Number;
            break;

        case DBGCVAR_TYPE_GC_FAR:
        case DBGCVAR_TYPE_STRING:
        default:
            AssertMsgFailed(("Impossible!\n"));
            return VERR_PARSE_INVALID_OPERATION;
    }

    return VINF_SUCCESS;
}


/**
 * Searches for an operator descriptor which matches the start of
 * the expression given us.
 *
 * @returns Pointer to the operator on success.
 * @param   pDbgc           The debug console instance.
 * @param   pszExpr         Pointer to the expression string which might start with an operator.
 * @param   fPreferBinary   Whether to favour binary or unary operators.
 *                          Caller must assert that it's the desired type! Both types will still
 *                          be returned, this is only for resolving duplicates.
 * @param   chPrev          The previous char. Some operators requires a blank in front of it.
 */
PCDBGCOP dbgcOperatorLookup(PDBGC pDbgc, const char *pszExpr, bool fPreferBinary, char chPrev)
{
    PCDBGCOP    pOp = NULL;
    for (unsigned iOp = 0; iOp < RT_ELEMENTS(g_aOps); iOp++)
    {
        if (     g_aOps[iOp].szName[0] == pszExpr[0]
            &&  (!g_aOps[iOp].szName[1] || g_aOps[iOp].szName[1] == pszExpr[1])
            &&  (!g_aOps[iOp].szName[2] || g_aOps[iOp].szName[2] == pszExpr[2]))
        {
            /*
             * Check that we don't mistake it for some other operator which have more chars.
             */
            unsigned j;
            for (j = iOp + 1; j < RT_ELEMENTS(g_aOps); j++)
                if (    g_aOps[j].cchName > g_aOps[iOp].cchName
                    &&  g_aOps[j].szName[0] == pszExpr[0]
                    &&  (!g_aOps[j].szName[1] || g_aOps[j].szName[1] == pszExpr[1])
                    &&  (!g_aOps[j].szName[2] || g_aOps[j].szName[2] == pszExpr[2]) )
                    break;
            if (j < RT_ELEMENTS(g_aOps))
                continue;       /* we'll catch it later. (for theoretical +,++,+++ cases.) */
            pOp = &g_aOps[iOp];

            /*
             * Preferred type?
             */
            if (g_aOps[iOp].fBinary == fPreferBinary)
                break;
        }
    }

    if (pOp)
        Log2(("dbgcOperatorLookup: pOp=%p %s\n", pOp, pOp->szName));
    NOREF(pDbgc); NOREF(chPrev);
    return pOp;
}

