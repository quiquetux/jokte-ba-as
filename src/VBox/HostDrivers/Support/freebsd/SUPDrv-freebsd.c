/* $Id: SUPDrv-freebsd.c $ */
/** @file
 * VBoxDrv - The VirtualBox Support Driver - FreeBSD specifics.
 */

/*
 * Copyright (c) 2007 knut st. osmundsen <bird-src-spam@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#define LOG_GROUP LOG_GROUP_SUP_DRV
/* Deal with conflicts first. */
#include <sys/param.h>
#undef PVM
#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/fcntl.h>
#include <sys/conf.h>
#include <sys/uio.h>

#include "../SUPDrvInternal.h"
#include <VBox/version.h>
#include <iprt/initterm.h>
#include <iprt/string.h>
#include <iprt/spinlock.h>
#include <iprt/process.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/err.h>
#include <iprt/asm.h>

#ifdef VBOX_WITH_HARDENING
# define VBOXDRV_PERM 0600
#else
# define VBOXDRV_PERM 0666
#endif

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static int VBoxDrvFreeBSDModuleEvent(struct module *pMod, int enmEventType, void *pvArg);
static int VBoxDrvFreeBSDLoad(void);
static int VBoxDrvFreeBSDUnload(void);
static void VBoxDrvFreeBSDClone(void *pvArg, struct ucred *pCred, char *pachName, int cchName, struct cdev **ppDev);

static d_fdopen_t   VBoxDrvFreeBSDOpen;
static d_close_t    VBoxDrvFreeBSDClose;
static d_ioctl_t    VBoxDrvFreeBSDIOCtl;
static int          VBoxDrvFreeBSDIOCtlSlow(PSUPDRVSESSION pSession, u_long ulCmd, caddr_t pvData, struct thread *pTd);


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
/**
 * Module info structure used by the kernel.
 */
static moduledata_t         g_VBoxDrvFreeBSDModule =
{
    "vboxdrv",
    VBoxDrvFreeBSDModuleEvent,
    NULL
};

/** Declare the module as a pseudo device. */
DECLARE_MODULE(vboxdrv,     g_VBoxDrvFreeBSDModule, SI_SUB_PSEUDO, SI_ORDER_ANY);
MODULE_VERSION(vboxdrv, 1);

/**
 * The /dev/vboxdrv character device entry points.
 */
static struct cdevsw        g_VBoxDrvFreeBSDChrDevSW =
{
    .d_version =        D_VERSION,
#if __FreeBSD_version > 800061
    .d_flags =          D_PSEUDO | D_TRACKCLOSE | D_NEEDMINOR,
#else
    .d_flags =          D_PSEUDO | D_TRACKCLOSE,
#endif
    .d_fdopen =         VBoxDrvFreeBSDOpen,
    .d_close =          VBoxDrvFreeBSDClose,
    .d_ioctl =          VBoxDrvFreeBSDIOCtl,
    .d_name =           "vboxdrv"
};

/** List of cloned device. Managed by the kernel. */
static struct clonedevs    *g_pVBoxDrvFreeBSDClones;
/** The dev_clone event handler tag. */
static eventhandler_tag     g_VBoxDrvFreeBSDEHTag;
/** Reference counter. */
static volatile uint32_t    g_cUsers;

/** The device extention. */
static SUPDRVDEVEXT         g_VBoxDrvFreeBSDDevExt;

/**
 * Module event handler.
 *
 * @param   pMod            The module structure.
 * @param   enmEventType    The event type (modeventtype_t).
 * @param   pvArg           Module argument. NULL.
 *
 * @return  0 on success, errno.h status code on failure.
 */
static int VBoxDrvFreeBSDModuleEvent(struct module *pMod, int enmEventType, void *pvArg)
{
    int rc;
    switch (enmEventType)
    {
        case MOD_LOAD:
            rc = VBoxDrvFreeBSDLoad();
            break;

        case MOD_UNLOAD:
            mtx_unlock(&Giant);
            rc = VBoxDrvFreeBSDUnload();
            mtx_lock(&Giant);
            break;

        case MOD_SHUTDOWN:
        case MOD_QUIESCE:
        default:
            return EOPNOTSUPP;
    }

    if (RT_SUCCESS(rc))
        return 0;
    return RTErrConvertToErrno(rc);
}


static int VBoxDrvFreeBSDLoad(void)
{
    g_cUsers = 0;

    /*
     * Initialize the runtime.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        Log(("VBoxDrvFreeBSDLoad:\n"));

        /*
         * Initialize the device extension.
         */
        rc = supdrvInitDevExt(&g_VBoxDrvFreeBSDDevExt, sizeof(SUPDRVSESSION));
        if (RT_SUCCESS(rc))
        {
            /*
             * Configure device cloning.
             */
            clone_setup(&g_pVBoxDrvFreeBSDClones);
            g_VBoxDrvFreeBSDEHTag = EVENTHANDLER_REGISTER(dev_clone, VBoxDrvFreeBSDClone, 0, 1000);
            if (g_VBoxDrvFreeBSDEHTag)
            {
                Log(("VBoxDrvFreeBSDLoad: returns successfully\n"));
                return VINF_SUCCESS;
            }

            printf("vboxdrv: EVENTHANDLER_REGISTER(dev_clone,,,) failed\n");
            clone_cleanup(&g_pVBoxDrvFreeBSDClones);
            rc = VERR_ALREADY_LOADED;
            supdrvDeleteDevExt(&g_VBoxDrvFreeBSDDevExt);
        }
        else
            printf("vboxdrv: supdrvInitDevExt failed, rc=%d\n", rc);
        RTR0Term();
    }
    else
        printf("vboxdrv: RTR0Init failed, rc=%d\n", rc);
    return rc;
}

static int VBoxDrvFreeBSDUnload(void)
{
    Log(("VBoxDrvFreeBSDUnload:\n"));

    if (g_cUsers > 0)
        return EBUSY;

    /*
     * Reserve what we did in VBoxDrvFreeBSDInit.
     */
    EVENTHANDLER_DEREGISTER(dev_clone, g_VBoxDrvFreeBSDEHTag);
    clone_cleanup(&g_pVBoxDrvFreeBSDClones);

    supdrvDeleteDevExt(&g_VBoxDrvFreeBSDDevExt);

    RTR0Term();

    memset(&g_VBoxDrvFreeBSDDevExt, 0, sizeof(g_VBoxDrvFreeBSDDevExt));

    Log(("VBoxDrvFreeBSDUnload: returns\n"));
    return VINF_SUCCESS;
}


/**
 * DEVFS event handler.
 */
static void VBoxDrvFreeBSDClone(void *pvArg, struct ucred *pCred, char *pszName, int cchName, struct cdev **ppDev)
{
    int iUnit;
    int rc;

    Log(("VBoxDrvFreeBSDClone: pszName=%s ppDev=%p\n", pszName, ppDev));

    /*
     * One device node per user, si_drv1 points to the session.
     * /dev/vboxdrv<N> where N = {0...255}.
     */
    if (!ppDev)
        return;
    if (dev_stdclone(pszName, NULL, "vboxdrv", &iUnit) != 1)
        return;
    if (iUnit >= 256 || iUnit < 0)
    {
        Log(("VBoxDrvFreeBSDClone: iUnit=%d >= 256 - rejected\n", iUnit));
        return;
    }

    Log(("VBoxDrvFreeBSDClone: pszName=%s iUnit=%d\n", pszName, iUnit));

    rc = clone_create(&g_pVBoxDrvFreeBSDClones, &g_VBoxDrvFreeBSDChrDevSW, &iUnit, ppDev, 0);
    Log(("VBoxDrvFreeBSDClone: clone_create -> %d; iUnit=%d\n", rc, iUnit));
    if (rc)
    {
#if __FreeBSD_version > 800061
        *ppDev = make_dev(&g_VBoxDrvFreeBSDChrDevSW, iUnit, UID_ROOT, GID_WHEEL, VBOXDRV_PERM, "vboxdrv%d", iUnit);
#else
        *ppDev = make_dev(&g_VBoxDrvFreeBSDChrDevSW, unit2minor(iUnit), UID_ROOT, GID_WHEEL, VBOXDRV_PERM, "vboxdrv%d", iUnit);
#endif
        if (*ppDev)
        {
            dev_ref(*ppDev);
            (*ppDev)->si_flags |= SI_CHEAPCLONE;
            Log(("VBoxDrvFreeBSDClone: Created *ppDev=%p iUnit=%d si_drv1=%p si_drv2=%p\n",
                 *ppDev, iUnit, (*ppDev)->si_drv1, (*ppDev)->si_drv2));
            (*ppDev)->si_drv1 = (*ppDev)->si_drv2 = NULL;
        }
        else
            OSDBGPRINT(("VBoxDrvFreeBSDClone: make_dev iUnit=%d failed\n", iUnit));
    }
    else
        Log(("VBoxDrvFreeBSDClone: Existing *ppDev=%p iUnit=%d si_drv1=%p si_drv2=%p\n",
             *ppDev, iUnit, (*ppDev)->si_drv1, (*ppDev)->si_drv2));
}



/**
 *
 * @returns 0 on success, errno on failure.
 *          EBUSY if the device is used by someone else.
 * @param   pDev    The device node.
 * @param   fOpen   The open flags.
 * @param   pTd     The thread.
 * @param   pFd     The file descriptor. FreeBSD 7.0 and later.
 * @param   iFd     The file descriptor index(?). Pre FreeBSD 7.0.
 */
#if __FreeBSD__ >= 7
static int VBoxDrvFreeBSDOpen(struct cdev *pDev, int fOpen, struct thread *pTd, struct file *pFd)
#else
static int VBoxDrvFreeBSDOpen(struct cdev *pDev, int fOpen, struct thread *pTd, int iFd)
#endif
{
    PSUPDRVSESSION pSession;
    int rc;

#if __FreeBSD_version < 800062
    Log(("VBoxDrvFreeBSDOpen: fOpen=%#x iUnit=%d\n", fOpen, minor2unit(minor(pDev))));
#else
    Log(("VBoxDrvFreeBSDOpen: fOpen=%#x iUnit=%d\n", fOpen, minor(dev2udev(pDev))));
#endif

    /*
     * Let's be a bit picky about the flags...
     */
    if (fOpen != (FREAD|FWRITE /*=O_RDWR*/))
    {
        Log(("VBoxDrvFreeBSDOpen: fOpen=%#x expected %#x\n", fOpen, O_RDWR));
        return EINVAL;
    }

    /*
     * Try grab it (we don't grab the giant, remember).
     */
    if (!ASMAtomicCmpXchgPtr(&pDev->si_drv1, (void *)0x42, NULL))
        return EBUSY;

    /*
     * Create a new session.
     */
    rc = supdrvCreateSession(&g_VBoxDrvFreeBSDDevExt, true /* fUser */, &pSession);
    if (RT_SUCCESS(rc))
    {
        /** @todo get (r)uid and (r)gid.
        pSession->Uid = stuff;
        pSession->Gid = stuff; */
        if (ASMAtomicCmpXchgPtr(&pDev->si_drv1, pSession, (void *)0x42))
        {
            ASMAtomicIncU32(&g_cUsers);
            return 0;
        }

        OSDBGPRINT(("VBoxDrvFreeBSDOpen: si_drv1=%p, expected 0x42!\n", pDev->si_drv1));
        supdrvCloseSession(&g_VBoxDrvFreeBSDDevExt, pSession);
    }

    return RTErrConvertToErrno(rc);
}


/**
 * Close a file device previously opened by VBoxDrvFreeBSDOpen
 *
 * @returns 0 on success.
 * @param   pDev        The device.
 * @param   fFile       The file descriptor flags.
 * @param   DevType     The device type (CHR.
 * @param   pTd         The calling thread.
 */
static int VBoxDrvFreeBSDClose(struct cdev *pDev, int fFile, int DevType, struct thread *pTd)
{
    PSUPDRVSESSION pSession = (PSUPDRVSESSION)pDev->si_drv1;
#if __FreeBSD_version < 800062
    Log(("VBoxDrvFreeBSDClose: fFile=%#x iUnit=%d pSession=%p\n", fFile, minor2unit(minor(pDev)), pSession));
#else
    Log(("VBoxDrvFreeBSDClose: fFile=%#x iUnit=%d pSession=%p\n", fFile, minor(dev2udev(pDev)), pSession));
#endif

    /*
     * Close the session if it's still hanging on to the device...
     */
    if (VALID_PTR(pSession))
    {
        supdrvCloseSession(&g_VBoxDrvFreeBSDDevExt, pSession);
        if (!ASMAtomicCmpXchgPtr(&pDev->si_drv1, NULL, pSession))
            OSDBGPRINT(("VBoxDrvFreeBSDClose: si_drv1=%p expected %p!\n", pDev->si_drv1, pSession));
        ASMAtomicDecU32(&g_cUsers);
        /* Don't use destroy_dev here because it may sleep resulting in a hanging user process. */
        destroy_dev_sched(pDev);
    }
    else
        OSDBGPRINT(("VBoxDrvFreeBSDClose: si_drv1=%p!\n", pSession));
    return 0;
}


/**
 * I/O control request.
 *
 * @returns depends...
 * @param   pDev        The device.
 * @param   ulCmd       The command.
 * @param   pvData      Pointer to the data.
 * @param   fFile       The file descriptor flags.
 * @param   pTd         The calling thread.
 */
static int VBoxDrvFreeBSDIOCtl(struct cdev *pDev, u_long ulCmd, caddr_t pvData, int fFile, struct thread *pTd)
{
    /*
     * Validate the input.
     */
    PSUPDRVSESSION pSession = (PSUPDRVSESSION)pDev->si_drv1;
    if (RT_UNLIKELY(!VALID_PTR(pSession)))
        return EINVAL;

    /*
     * Deal with the fast ioctl path first.
     */
    if (    ulCmd == SUP_IOCTL_FAST_DO_RAW_RUN
        ||  ulCmd == SUP_IOCTL_FAST_DO_HWACC_RUN
        ||  ulCmd == SUP_IOCTL_FAST_DO_NOP)
        return supdrvIOCtlFast(ulCmd, *(uint32_t *)pvData, &g_VBoxDrvFreeBSDDevExt, pSession);

    return VBoxDrvFreeBSDIOCtlSlow(pSession, ulCmd, pvData, pTd);
}


/**
 * Deal with the 'slow' I/O control requests.
 *
 * @returns 0 on success, appropriate errno on failure.
 * @param   pSession    The session.
 * @param   ulCmd       The command.
 * @param   pvData      The request data.
 * @param   pTd         The calling thread.
 */
static int VBoxDrvFreeBSDIOCtlSlow(PSUPDRVSESSION pSession, u_long ulCmd, caddr_t pvData, struct thread *pTd)
{
    PSUPREQHDR  pHdr;
    uint32_t    cbReq = IOCPARM_LEN(ulCmd);
    void       *pvUser = NULL;

    /*
     * Buffered request?
     */
    if ((IOC_DIRMASK & ulCmd) == IOC_INOUT)
    {
        pHdr = (PSUPREQHDR)pvData;
        if (RT_UNLIKELY(cbReq < sizeof(*pHdr)))
        {
            OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: cbReq=%#x < %#x; ulCmd=%#lx\n", cbReq, (int)sizeof(*pHdr), ulCmd));
            return EINVAL;
        }
        if (RT_UNLIKELY((pHdr->fFlags & SUPREQHDR_FLAGS_MAGIC_MASK) != SUPREQHDR_FLAGS_MAGIC))
        {
            OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: bad magic fFlags=%#x; ulCmd=%#lx\n", pHdr->fFlags, ulCmd));
            return EINVAL;
        }
        if (RT_UNLIKELY(    RT_MAX(pHdr->cbIn, pHdr->cbOut) != cbReq
                        ||  pHdr->cbIn < sizeof(*pHdr)
                        ||  pHdr->cbOut < sizeof(*pHdr)))
        {
            OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: max(%#x,%#x) != %#x; ulCmd=%#lx\n", pHdr->cbIn, pHdr->cbOut, cbReq, ulCmd));
            return EINVAL;
        }
    }
    /*
     * Big unbuffered request?
     */
    else if ((IOC_DIRMASK & ulCmd) == IOC_VOID && !cbReq)
    {
        /*
         * Read the header, validate it and figure out how much that needs to be buffered.
         */
        SUPREQHDR Hdr;
        pvUser = *(void **)pvData;
        int rc = copyin(pvUser, &Hdr, sizeof(Hdr));
        if (RT_UNLIKELY(rc))
        {
            OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: copyin(%p,Hdr,) -> %#x; ulCmd=%#lx\n", pvUser, rc, ulCmd));
            return rc;
        }
        if (RT_UNLIKELY((Hdr.fFlags & SUPREQHDR_FLAGS_MAGIC_MASK) != SUPREQHDR_FLAGS_MAGIC))
        {
            OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: bad magic fFlags=%#x; ulCmd=%#lx\n", Hdr.fFlags, ulCmd));
            return EINVAL;
        }
        cbReq = RT_MAX(Hdr.cbIn, Hdr.cbOut);
        if (RT_UNLIKELY(    Hdr.cbIn < sizeof(Hdr)
                        ||  Hdr.cbOut < sizeof(Hdr)
                        ||  cbReq > _1M*16))
        {
            OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: max(%#x,%#x); ulCmd=%#lx\n", Hdr.cbIn, Hdr.cbOut, ulCmd));
            return EINVAL;
        }

        /*
         * Allocate buffer and copy in the data.
         */
        pHdr = (PSUPREQHDR)RTMemTmpAlloc(cbReq);
        if (RT_UNLIKELY(!pHdr))
        {
            OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: failed to allocate buffer of %d bytes; ulCmd=%#lx\n", cbReq, ulCmd));
            return ENOMEM;
        }
        rc = copyin(pvUser, pHdr, Hdr.cbIn);
        if (RT_UNLIKELY(rc))
        {
            OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: copyin(%p,%p,%#x) -> %#x; ulCmd=%#lx\n",
                        pvUser, pHdr, Hdr.cbIn, rc, ulCmd));
            RTMemTmpFree(pHdr);
            return rc;
        }
    }
    else
    {
        Log(("VBoxDrvFreeBSDIOCtlSlow: huh? cbReq=%#x ulCmd=%#lx\n", cbReq, ulCmd));
        return EINVAL;
    }

    /*
     * Process the IOCtl.
     */
    int rc = supdrvIOCtl(ulCmd, &g_VBoxDrvFreeBSDDevExt, pSession, pHdr);
    if (RT_LIKELY(!rc))
    {
        /*
         * If unbuffered, copy back the result before returning.
         */
        if (pvUser)
        {
            uint32_t cbOut = pHdr->cbOut;
            if (cbOut > cbReq)
            {
                OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: too much output! %#x > %#x; uCmd=%#lx!\n", cbOut, cbReq, ulCmd));
                cbOut = cbReq;
            }
            rc = copyout(pHdr, pvUser, cbOut);
            if (RT_UNLIKELY(rc))
                OSDBGPRINT(("VBoxDrvFreeBSDIOCtlSlow: copyout(%p,%p,%#x) -> %d; uCmd=%#lx!\n", pHdr, pvUser, cbOut, rc, ulCmd));

            Log(("VBoxDrvFreeBSDIOCtlSlow: returns %d / %d ulCmd=%lx\n", 0, pHdr->rc, ulCmd));

            /* cleanup */
            RTMemTmpFree(pHdr);
        }
    }
    else
    {
        /*
         * The request failed, just clean up.
         */
        if (pvUser)
            RTMemTmpFree(pHdr);

        Log(("VBoxDrvFreeBSDIOCtlSlow: ulCmd=%lx pData=%p failed, rc=%d\n", ulCmd, pvData, rc));
        rc = EINVAL;
    }

    return rc;
}


/**
 * The SUPDRV IDC entry point.
 *
 * @returns VBox status code, see supdrvIDC.
 * @param   iReq        The request code.
 * @param   pReq        The request.
 */
int VBOXCALL SUPDrvFreeBSDIDC(uint32_t uReq, PSUPDRVIDCREQHDR pReq)
{
    PSUPDRVSESSION  pSession;

    /*
     * Some quick validations.
     */
    if (RT_UNLIKELY(!VALID_PTR(pReq)))
        return VERR_INVALID_POINTER;

    pSession = pReq->pSession;
    if (pSession)
    {
        if (RT_UNLIKELY(!VALID_PTR(pReq->pSession)))
            return VERR_INVALID_PARAMETER;
        if (RT_UNLIKELY(pSession->pDevExt != &g_VBoxDrvFreeBSDDevExt))
            return VERR_INVALID_PARAMETER;
    }
    else if (RT_UNLIKELY(uReq != SUPDRV_IDC_REQ_CONNECT))
        return VERR_INVALID_PARAMETER;

    /*
     * Do the job.
     */
    return supdrvIDC(uReq, &g_VBoxDrvFreeBSDDevExt, pSession, pReq);
}


void VBOXCALL   supdrvOSObjInitCreator(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession)
{
    NOREF(pObj);
    NOREF(pSession);
}


bool VBOXCALL   supdrvOSObjCanAccess(PSUPDRVOBJ pObj, PSUPDRVSESSION pSession, const char *pszObjName, int *prc)
{
    NOREF(pObj);
    NOREF(pSession);
    NOREF(pszObjName);
    NOREF(prc);
    return false;
}


bool VBOXCALL  supdrvOSGetForcedAsyncTscMode(PSUPDRVDEVEXT pDevExt)
{
    return false;
}


int  VBOXCALL   supdrvOSLdrOpen(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const char *pszFilename)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pszFilename);
    return VERR_NOT_SUPPORTED;
}


int  VBOXCALL   supdrvOSLdrValidatePointer(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, void *pv, const uint8_t *pbImageBits)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pv); NOREF(pbImageBits);
    return VERR_NOT_SUPPORTED;
}


int  VBOXCALL   supdrvOSLdrLoad(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage, const uint8_t *pbImageBits, PSUPLDRLOAD pReq)
{
    NOREF(pDevExt); NOREF(pImage); NOREF(pbImageBits); NOREF(pReq);
    return VERR_NOT_SUPPORTED;
}


void VBOXCALL   supdrvOSLdrUnload(PSUPDRVDEVEXT pDevExt, PSUPDRVLDRIMAGE pImage)
{
    NOREF(pDevExt); NOREF(pImage);
}


SUPR0DECL(int) SUPR0Printf(const char *pszFormat, ...)
{
    va_list va;
    char szMsg[256];
    int cch;

    va_start(va, pszFormat);
    cch = RTStrPrintfV(szMsg, sizeof(szMsg), pszFormat, va);
    va_end(va);

    printf("%s", szMsg);

    return cch;
}

