/** @file
 * Virtual Disk Image (VDI), Core Code.
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
#define LOG_GROUP LOG_GROUP_VD_VDI
#include <VBox/vd-plugin.h>
#define VBOX_VDICORE_VD /* Signal that the header is included from here. */
#include "VDICore.h"
#include <VBox/err.h>

#include <VBox/log.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/uuid.h>
#include <iprt/string.h>
#include <iprt/asm.h>

#define VDI_IMAGE_DEFAULT_BLOCK_SIZE _1M

/*******************************************************************************
*   Static Variables                                                           *
*******************************************************************************/

/** NULL-terminated array of supported file extensions. */
static const VDFILEEXTENSION s_aVdiFileExtensions[] =
{
    {"vdi", VDTYPE_HDD},
    {NULL, VDTYPE_INVALID}
};

/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
static unsigned getPowerOfTwo(unsigned uNumber);
static void vdiInitPreHeader(PVDIPREHEADER pPreHdr);
static int  vdiValidatePreHeader(PVDIPREHEADER pPreHdr);
static void vdiInitHeader(PVDIHEADER pHeader, uint32_t uImageFlags,
                          const char *pszComment, uint64_t cbDisk,
                          uint32_t cbBlock, uint32_t cbBlockExtra);
static int  vdiValidateHeader(PVDIHEADER pHeader);
static void vdiSetupImageDesc(PVDIIMAGEDESC pImage);
static int  vdiUpdateHeader(PVDIIMAGEDESC pImage);
static int  vdiUpdateBlockInfo(PVDIIMAGEDESC pImage, unsigned uBlock);
static int  vdiUpdateHeaderAsync(PVDIIMAGEDESC pImage, PVDIOCTX pIoCtx);
static int  vdiUpdateBlockInfoAsync(PVDIIMAGEDESC pImage, unsigned uBlock, PVDIOCTX pIoCtx);

/**
 * Internal: signal an error to the frontend.
 */
DECLINLINE(int) vdiError(PVDIIMAGEDESC pImage, int rc, RT_SRC_POS_DECL,
                         const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    if (pImage->pInterfaceError && pImage->pInterfaceErrorCallbacks)
        pImage->pInterfaceErrorCallbacks->pfnError(pImage->pInterfaceError->pvUser,
                                                   rc, RT_SRC_POS_ARGS,
                                                   pszFormat, va);
    va_end(va);
    return rc;
}

/**
 * Internal: signal an informational message to the frontend.
 */
DECLINLINE(int) vdiMessage(PVDIIMAGEDESC pImage, const char *pszFormat, ...)
{
    int rc = VINF_SUCCESS;
    va_list va;
    va_start(va, pszFormat);
    if (pImage->pInterfaceError && pImage->pInterfaceErrorCallbacks)
        rc = pImage->pInterfaceErrorCallbacks->pfnMessage(pImage->pInterfaceError->pvUser,
                                                          pszFormat, va);
    va_end(va);
    return rc;
}


DECLINLINE(int) vdiFileOpen(PVDIIMAGEDESC pImage, const char *pszFilename,
                            uint32_t fOpen)
{
    return pImage->pInterfaceIOCallbacks->pfnOpen(pImage->pInterfaceIO->pvUser,
                                                  pszFilename, fOpen,
                                                  &pImage->pStorage);
}

DECLINLINE(int) vdiFileClose(PVDIIMAGEDESC pImage)
{
    return pImage->pInterfaceIOCallbacks->pfnClose(pImage->pInterfaceIO->pvUser,
                                                   pImage->pStorage);
}

DECLINLINE(int) vdiFileDelete(PVDIIMAGEDESC pImage, const char *pszFilename)
{
    return pImage->pInterfaceIOCallbacks->pfnDelete(pImage->pInterfaceIO->pvUser,
                                                    pszFilename);
}

DECLINLINE(int) vdiFileMove(PVDIIMAGEDESC pImage, const char *pszSrc,
                            const char *pszDst, unsigned fMove)
{
    return pImage->pInterfaceIOCallbacks->pfnMove(pImage->pInterfaceIO->pvUser,
                                                  pszSrc, pszDst, fMove);
}

DECLINLINE(int) vdiFileGetFreeSpace(PVDIIMAGEDESC pImage, const char *pszFilename,
                                    int64_t *pcbFree)
{
    return pImage->pInterfaceIOCallbacks->pfnGetFreeSpace(pImage->pInterfaceIO->pvUser,
                                                          pszFilename, pcbFree);
}

DECLINLINE(int) vdiFileGetSize(PVDIIMAGEDESC pImage, uint64_t *pcbSize)
{
    return pImage->pInterfaceIOCallbacks->pfnGetSize(pImage->pInterfaceIO->pvUser,
                                                     pImage->pStorage, pcbSize);
}

DECLINLINE(int) vdiFileSetSize(PVDIIMAGEDESC pImage, uint64_t cbSize)
{
    return pImage->pInterfaceIOCallbacks->pfnSetSize(pImage->pInterfaceIO->pvUser,
                                                     pImage->pStorage, cbSize);
}

DECLINLINE(int) vdiFileWriteSync(PVDIIMAGEDESC pImage, uint64_t uOffset,
                                 const void *pvBuffer, size_t cbBuffer,
                                 size_t *pcbWritten)
{
    return pImage->pInterfaceIOCallbacks->pfnWriteSync(pImage->pInterfaceIO->pvUser,
                                                       pImage->pStorage, uOffset,
                                                       pvBuffer, cbBuffer, pcbWritten);
}

DECLINLINE(int) vdiFileReadSync(PVDIIMAGEDESC pImage, uint64_t uOffset,
                                void *pvBuffer, size_t cbBuffer, size_t *pcbRead)
{
    return pImage->pInterfaceIOCallbacks->pfnReadSync(pImage->pInterfaceIO->pvUser,
                                                      pImage->pStorage, uOffset,
                                                      pvBuffer, cbBuffer, pcbRead);
}

DECLINLINE(int) vdiFileFlushSync(PVDIIMAGEDESC pImage)
{
    return pImage->pInterfaceIOCallbacks->pfnFlushSync(pImage->pInterfaceIO->pvUser,
                                                       pImage->pStorage);
}

DECLINLINE(int) vdiFileReadUserAsync(PVDIIMAGEDESC pImage, uint64_t uOffset,
                                     PVDIOCTX pIoCtx, size_t cbRead)
{
    return pImage->pInterfaceIOCallbacks->pfnReadUserAsync(pImage->pInterfaceIO->pvUser,
                                                           pImage->pStorage,
                                                           uOffset, pIoCtx,
                                                           cbRead);
}

DECLINLINE(int) vdiFileWriteUserAsync(PVDIIMAGEDESC pImage, uint64_t uOffset,
                                      PVDIOCTX pIoCtx, size_t cbWrite,
                                      PFNVDXFERCOMPLETED pfnComplete,
                                      void *pvCompleteUser)
{
    return pImage->pInterfaceIOCallbacks->pfnWriteUserAsync(pImage->pInterfaceIO->pvUser,
                                                            pImage->pStorage,
                                                            uOffset, pIoCtx,
                                                            cbWrite,
                                                            pfnComplete,
                                                            pvCompleteUser);
}

DECLINLINE(int) vdiFileWriteMetaAsync(PVDIIMAGEDESC pImage, uint64_t uOffset,
                                      void *pvBuffer, size_t cbBuffer,
                                      PVDIOCTX pIoCtx,
                                      PFNVDXFERCOMPLETED pfnComplete,
                                      void *pvCompleteUser)
{
    return pImage->pInterfaceIOCallbacks->pfnWriteMetaAsync(pImage->pInterfaceIO->pvUser,
                                                            pImage->pStorage,
                                                            uOffset, pvBuffer,
                                                            cbBuffer, pIoCtx,
                                                            pfnComplete,
                                                            pvCompleteUser);
}

DECLINLINE(int) vdiFileFlushAsync(PVDIIMAGEDESC pImage, PVDIOCTX pIoCtx,
                                  PFNVDXFERCOMPLETED pfnComplete,
                                  void *pvCompleteUser)
{
    return pImage->pInterfaceIOCallbacks->pfnFlushAsync(pImage->pInterfaceIO->pvUser,
                                                        pImage->pStorage,
                                                        pIoCtx, pfnComplete,
                                                        pvCompleteUser);
}

DECLINLINE(size_t) vdiFileIoCtxSet(PVDIIMAGEDESC pImage, PVDIOCTX pIoCtx,
                                   int ch, size_t cbSet)
{
    return pImage->pInterfaceIOCallbacks->pfnIoCtxSet(pImage->pInterfaceIO->pvUser,
                                                      pIoCtx, ch, cbSet);
}


/**
 * Internal: Flush the image file to disk.
 */
static void vdiFlushImage(PVDIIMAGEDESC pImage)
{
    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        /* Save header. */
        int rc = vdiUpdateHeader(pImage);
        AssertMsgRC(rc, ("vdiUpdateHeader() failed, filename=\"%s\", rc=%Rrc\n",
                         pImage->pszFilename, rc));
        vdiFileFlushSync(pImage);
    }
}

/**
 * Internal: Free all allocated space for representing an image, and optionally
 * delete the image from disk.
 */
static int vdiFreeImage(PVDIIMAGEDESC pImage, bool fDelete)
{
    int rc = VINF_SUCCESS;

    /* Freeing a never allocated image (e.g. because the open failed) is
     * not signalled as an error. After all nothing bad happens. */
    if (pImage)
    {
        if (pImage->pStorage)
        {
            /* No point updating the file that is deleted anyway. */
            if (!fDelete)
                vdiFlushImage(pImage);

            vdiFileClose(pImage);
            pImage->pStorage = NULL;
        }

        if (pImage->paBlocks)
        {
            RTMemFree(pImage->paBlocks);
            pImage->paBlocks = NULL;
        }

        if (fDelete && pImage->pszFilename)
            vdiFileDelete(pImage, pImage->pszFilename);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * internal: return power of 2 or 0 if num error.
 */
static unsigned getPowerOfTwo(unsigned uNumber)
{
    if (uNumber == 0)
        return 0;
    unsigned uPower2 = 0;
    while ((uNumber & 1) == 0)
    {
        uNumber >>= 1;
        uPower2++;
    }
    return uNumber == 1 ? uPower2 : 0;
}

/**
 * Internal: Init VDI preheader.
 */
static void vdiInitPreHeader(PVDIPREHEADER pPreHdr)
{
    pPreHdr->u32Signature = VDI_IMAGE_SIGNATURE;
    pPreHdr->u32Version = VDI_IMAGE_VERSION;
    memset(pPreHdr->szFileInfo, 0, sizeof(pPreHdr->szFileInfo));
    strncat(pPreHdr->szFileInfo, VDI_IMAGE_FILE_INFO, sizeof(pPreHdr->szFileInfo)-1);
}

/**
 * Internal: check VDI preheader.
 */
static int vdiValidatePreHeader(PVDIPREHEADER pPreHdr)
{
    if (pPreHdr->u32Signature != VDI_IMAGE_SIGNATURE)
        return VERR_VD_VDI_INVALID_HEADER;

    if (    VDI_GET_VERSION_MAJOR(pPreHdr->u32Version) != VDI_IMAGE_VERSION_MAJOR
        &&  pPreHdr->u32Version != 0x00000002)    /* old version. */
        return VERR_VD_VDI_UNSUPPORTED_VERSION;

    return VINF_SUCCESS;
}

/**
 * Internal: translate VD image flags to VDI image type enum.
 */
static VDIIMAGETYPE vdiTranslateImageFlags2VDI(unsigned uImageFlags)
{
    if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
        return VDI_IMAGE_TYPE_FIXED;
    else if (uImageFlags & VD_IMAGE_FLAGS_DIFF)
        return VDI_IMAGE_TYPE_DIFF;
    else
        return VDI_IMAGE_TYPE_NORMAL;
}

/**
 * Internal: translate VDI image type enum to VD image type enum.
 */
static unsigned vdiTranslateVDI2ImageFlags(VDIIMAGETYPE enmType)
{
    switch (enmType)
    {
        case VDI_IMAGE_TYPE_NORMAL:
            return VD_IMAGE_FLAGS_NONE;
        case VDI_IMAGE_TYPE_FIXED:
            return VD_IMAGE_FLAGS_FIXED;
        case VDI_IMAGE_TYPE_DIFF:
            return VD_IMAGE_FLAGS_DIFF;
        default:
            AssertMsgFailed(("invalid VDIIMAGETYPE enmType=%d\n", (int)enmType));
            return VD_IMAGE_FLAGS_NONE;
    }
}

/**
 * Internal: Init VDI header. Always use latest header version.
 * @param   pHeader     Assumes it was initially initialized to all zeros.
 */
static void vdiInitHeader(PVDIHEADER pHeader, uint32_t uImageFlags,
                          const char *pszComment, uint64_t cbDisk,
                          uint32_t cbBlock, uint32_t cbBlockExtra)
{
    pHeader->uVersion = VDI_IMAGE_VERSION;
    pHeader->u.v1plus.cbHeader = sizeof(VDIHEADER1PLUS);
    pHeader->u.v1plus.u32Type = (uint32_t)vdiTranslateImageFlags2VDI(uImageFlags);
    pHeader->u.v1plus.fFlags = (uImageFlags & VD_VDI_IMAGE_FLAGS_ZERO_EXPAND) ? 1 : 0;
#ifdef VBOX_STRICT
    char achZero[VDI_IMAGE_COMMENT_SIZE] = {0};
    Assert(!memcmp(pHeader->u.v1plus.szComment, achZero, VDI_IMAGE_COMMENT_SIZE));
#endif
    pHeader->u.v1plus.szComment[0] = '\0';
    if (pszComment)
    {
        AssertMsg(strlen(pszComment) < sizeof(pHeader->u.v1plus.szComment),
                  ("HDD Comment is too long, cb=%d\n", strlen(pszComment)));
        strncat(pHeader->u.v1plus.szComment, pszComment, sizeof(pHeader->u.v1plus.szComment)-1);
    }

    /* Mark the legacy geometry not-calculated. */
    pHeader->u.v1plus.LegacyGeometry.cCylinders = 0;
    pHeader->u.v1plus.LegacyGeometry.cHeads = 0;
    pHeader->u.v1plus.LegacyGeometry.cSectors = 0;
    pHeader->u.v1plus.LegacyGeometry.cbSector = VDI_GEOMETRY_SECTOR_SIZE;
    pHeader->u.v1plus.u32Dummy = 0; /* used to be the translation value */

    pHeader->u.v1plus.cbDisk = cbDisk;
    pHeader->u.v1plus.cbBlock = cbBlock;
    pHeader->u.v1plus.cBlocks = (uint32_t)(cbDisk / cbBlock);
    if (cbDisk % cbBlock)
        pHeader->u.v1plus.cBlocks++;
    pHeader->u.v1plus.cbBlockExtra = cbBlockExtra;
    pHeader->u.v1plus.cBlocksAllocated = 0;

    /* Init offsets. */
    pHeader->u.v1plus.offBlocks = RT_ALIGN_32(sizeof(VDIPREHEADER) + sizeof(VDIHEADER1PLUS), VDI_DATA_ALIGN);
    pHeader->u.v1plus.offData = RT_ALIGN_32(pHeader->u.v1plus.offBlocks + (pHeader->u.v1plus.cBlocks * sizeof(VDIIMAGEBLOCKPOINTER)), VDI_DATA_ALIGN);

    /* Init uuids. */
    RTUuidCreate(&pHeader->u.v1plus.uuidCreate);
    RTUuidClear(&pHeader->u.v1plus.uuidModify);
    RTUuidClear(&pHeader->u.v1plus.uuidLinkage);
    RTUuidClear(&pHeader->u.v1plus.uuidParentModify);

    /* Mark LCHS geometry not-calculated. */
    pHeader->u.v1plus.LCHSGeometry.cCylinders = 0;
    pHeader->u.v1plus.LCHSGeometry.cHeads = 0;
    pHeader->u.v1plus.LCHSGeometry.cSectors = 0;
    pHeader->u.v1plus.LCHSGeometry.cbSector = VDI_GEOMETRY_SECTOR_SIZE;
}

/**
 * Internal: Check VDI header.
 */
static int vdiValidateHeader(PVDIHEADER pHeader)
{
    /* Check version-dependent header parameters. */
    switch (GET_MAJOR_HEADER_VERSION(pHeader))
    {
        case 0:
        {
            /* Old header version. */
            break;
        }
        case 1:
        {
            /* Current header version. */

            if (pHeader->u.v1.cbHeader < sizeof(VDIHEADER1))
            {
                LogRel(("VDI: v1 header size wrong (%d < %d)\n",
                       pHeader->u.v1.cbHeader, sizeof(VDIHEADER1)));
                return VERR_VD_VDI_INVALID_HEADER;
            }

            if (getImageBlocksOffset(pHeader) < (sizeof(VDIPREHEADER) + sizeof(VDIHEADER1)))
            {
                LogRel(("VDI: v1 blocks offset wrong (%d < %d)\n",
                       getImageBlocksOffset(pHeader), sizeof(VDIPREHEADER) + sizeof(VDIHEADER1)));
                return VERR_VD_VDI_INVALID_HEADER;
            }

            if (getImageDataOffset(pHeader) < (getImageBlocksOffset(pHeader) + getImageBlocks(pHeader) * sizeof(VDIIMAGEBLOCKPOINTER)))
            {
                LogRel(("VDI: v1 image data offset wrong (%d < %d)\n",
                       getImageDataOffset(pHeader), getImageBlocksOffset(pHeader) + getImageBlocks(pHeader) * sizeof(VDIIMAGEBLOCKPOINTER)));
                return VERR_VD_VDI_INVALID_HEADER;
            }

            break;
        }
        default:
            /* Unsupported. */
            return VERR_VD_VDI_UNSUPPORTED_VERSION;
    }

    /* Check common header parameters. */

    bool fFailed = false;

    if (    getImageType(pHeader) < VDI_IMAGE_TYPE_FIRST
        ||  getImageType(pHeader) > VDI_IMAGE_TYPE_LAST)
    {
        LogRel(("VDI: bad image type %d\n", getImageType(pHeader)));
        fFailed = true;
    }

    if (getImageFlags(pHeader) & ~VD_VDI_IMAGE_FLAGS_MASK)
    {
        LogRel(("VDI: bad image flags %08x\n", getImageFlags(pHeader)));
        fFailed = true;
    }

    if (   getImageLCHSGeometry(pHeader)
        && (getImageLCHSGeometry(pHeader))->cbSector != VDI_GEOMETRY_SECTOR_SIZE)
    {
        LogRel(("VDI: wrong sector size (%d != %d)\n",
               (getImageLCHSGeometry(pHeader))->cbSector, VDI_GEOMETRY_SECTOR_SIZE));
        fFailed = true;
    }

    if (    getImageDiskSize(pHeader) == 0
        ||  getImageBlockSize(pHeader) == 0
        ||  getImageBlocks(pHeader) == 0
        ||  getPowerOfTwo(getImageBlockSize(pHeader)) == 0)
    {
        LogRel(("VDI: wrong size (%lld, %d, %d, %d)\n",
              getImageDiskSize(pHeader), getImageBlockSize(pHeader),
              getImageBlocks(pHeader), getPowerOfTwo(getImageBlockSize(pHeader))));
        fFailed = true;
    }

    if (getImageBlocksAllocated(pHeader) > getImageBlocks(pHeader))
    {
        LogRel(("VDI: too many blocks allocated (%d > %d)\n"
                "     blocksize=%d disksize=%lld\n",
              getImageBlocksAllocated(pHeader), getImageBlocks(pHeader),
              getImageBlockSize(pHeader), getImageDiskSize(pHeader)));
        fFailed = true;
    }

    if (    getImageExtraBlockSize(pHeader) != 0
        &&  getPowerOfTwo(getImageExtraBlockSize(pHeader)) == 0)
    {
        LogRel(("VDI: wrong extra size (%d, %d)\n",
               getImageExtraBlockSize(pHeader), getPowerOfTwo(getImageExtraBlockSize(pHeader))));
        fFailed = true;
    }

    if ((uint64_t)getImageBlockSize(pHeader) * getImageBlocks(pHeader) < getImageDiskSize(pHeader))
    {
        LogRel(("VDI: wrong disk size (%d, %d, %lld)\n",
               getImageBlockSize(pHeader), getImageBlocks(pHeader), getImageDiskSize(pHeader)));
        fFailed = true;
    }

    if (RTUuidIsNull(getImageCreationUUID(pHeader)))
    {
        LogRel(("VDI: uuid of creator is 0\n"));
        fFailed = true;
    }

    if (RTUuidIsNull(getImageModificationUUID(pHeader)))
    {
        LogRel(("VDI: uuid of modifier is 0\n"));
        fFailed = true;
    }

    return fFailed ? VERR_VD_VDI_INVALID_HEADER : VINF_SUCCESS;
}

/**
 * Internal: Set up VDIIMAGEDESC structure by image header.
 */
static void vdiSetupImageDesc(PVDIIMAGEDESC pImage)
{
    pImage->uImageFlags        = getImageFlags(&pImage->Header);
    pImage->uImageFlags       |= vdiTranslateVDI2ImageFlags(getImageType(&pImage->Header));
    pImage->offStartBlocks     = getImageBlocksOffset(&pImage->Header);
    pImage->offStartData       = getImageDataOffset(&pImage->Header);
    pImage->uBlockMask         = getImageBlockSize(&pImage->Header) - 1;
    pImage->uShiftOffset2Index = getPowerOfTwo(getImageBlockSize(&pImage->Header));
    pImage->offStartBlockData  = getImageExtraBlockSize(&pImage->Header);
    pImage->cbTotalBlockData   =   pImage->offStartBlockData
                                 + getImageBlockSize(&pImage->Header);
}

/**
 * Internal: Create VDI image file.
 */
static int vdiCreateImage(PVDIIMAGEDESC pImage, uint64_t cbSize,
                          unsigned uImageFlags, const char *pszComment,
                          PCVDGEOMETRY pPCHSGeometry,
                          PCVDGEOMETRY pLCHSGeometry, PCRTUUID pUuid,
                          unsigned uOpenFlags, PFNVDPROGRESS pfnProgress,
                          void *pvUser, unsigned uPercentStart,
                          unsigned uPercentSpan)
{
    int rc;
    uint64_t cbTotal;
    uint64_t cbFill;
    uint64_t uOff;

    /* Special check for comment length. */
    if (   VALID_PTR(pszComment)
        && strlen(pszComment) >= VDI_IMAGE_COMMENT_SIZE)
    {
        rc = vdiError(pImage, VERR_VD_VDI_COMMENT_TOO_LONG, RT_SRC_POS, N_("VDI: comment is too long for '%s'"), pImage->pszFilename);
        goto out;
    }
    AssertPtr(pPCHSGeometry);
    AssertPtr(pLCHSGeometry);

    pImage->pInterfaceError = VDInterfaceGet(pImage->pVDIfsDisk, VDINTERFACETYPE_ERROR);
    if (pImage->pInterfaceError)
        pImage->pInterfaceErrorCallbacks = VDGetInterfaceError(pImage->pInterfaceError);

    /* Get I/O interface. */
    pImage->pInterfaceIO = VDInterfaceGet(pImage->pVDIfsImage, VDINTERFACETYPE_IOINT);
    AssertPtrReturn(pImage->pInterfaceIO, VERR_INVALID_PARAMETER);
    pImage->pInterfaceIOCallbacks = VDGetInterfaceIOInt(pImage->pInterfaceIO);
    AssertPtrReturn(pImage->pInterfaceIOCallbacks, VERR_INVALID_PARAMETER);

    vdiInitPreHeader(&pImage->PreHeader);
    vdiInitHeader(&pImage->Header, uImageFlags, pszComment, cbSize, VDI_IMAGE_DEFAULT_BLOCK_SIZE, 0);
    /* Save PCHS geometry. Not much work, and makes the flow of information
     * quite a bit clearer - relying on the higher level isn't obvious. */
    pImage->PCHSGeometry = *pPCHSGeometry;
    /* Set LCHS geometry (legacy geometry is ignored for the current 1.1+). */
    pImage->Header.u.v1plus.LCHSGeometry.cCylinders = pLCHSGeometry->cCylinders;
    pImage->Header.u.v1plus.LCHSGeometry.cHeads = pLCHSGeometry->cHeads;
    pImage->Header.u.v1plus.LCHSGeometry.cSectors = pLCHSGeometry->cSectors;
    pImage->Header.u.v1plus.LCHSGeometry.cbSector = VDI_GEOMETRY_SECTOR_SIZE;

    pImage->paBlocks = (PVDIIMAGEBLOCKPOINTER)RTMemAlloc(sizeof(VDIIMAGEBLOCKPOINTER) * getImageBlocks(&pImage->Header));
    if (!pImage->paBlocks)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }

    if (!(uImageFlags & VD_IMAGE_FLAGS_FIXED))
    {
        /* for growing images mark all blocks in paBlocks as free. */
        for (unsigned i = 0; i < pImage->Header.u.v1.cBlocks; i++)
            pImage->paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
    }
    else
    {
        /* for fixed images mark all blocks in paBlocks as allocated */
        for (unsigned i = 0; i < pImage->Header.u.v1.cBlocks; i++)
            pImage->paBlocks[i] = i;
        pImage->Header.u.v1.cBlocksAllocated = pImage->Header.u.v1.cBlocks;
    }

    /* Setup image parameters. */
    vdiSetupImageDesc(pImage);

    /* Create image file. */
    rc = vdiFileOpen(pImage, pImage->pszFilename,
                     VDOpenFlagsToFileOpenFlags(uOpenFlags & ~VD_OPEN_FLAGS_READONLY,
                                                true /* fCreate */));
    if (RT_FAILURE(rc))
    {
        rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: cannot create image '%s'"), pImage->pszFilename);
        goto out;
    }

    cbTotal =   pImage->offStartData
              + (uint64_t)getImageBlocks(&pImage->Header) * pImage->cbTotalBlockData;

    if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
    {
        /* Check the free space on the disk and leave early if there is not
         * sufficient space available. */
        int64_t cbFree = 0;
        rc = vdiFileGetFreeSpace(pImage, pImage->pszFilename, &cbFree);
        if (RT_SUCCESS(rc) /* ignore errors */ && ((uint64_t)cbFree < cbTotal))
        {
            rc = vdiError(pImage, VERR_DISK_FULL, RT_SRC_POS, N_("VDI: disk would overflow creating image '%s'"), pImage->pszFilename);
            goto out;
        }
    }

    if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
    {
        /*
         * Allocate & commit whole file if fixed image, it must be more
         * effective than expanding file by write operations.
         */
        rc = vdiFileSetSize(pImage, cbTotal);
    }
    else
    {
        /* Set file size to hold header and blocks array. */
        rc = vdiFileSetSize(pImage, pImage->offStartData);
    }
    if (RT_FAILURE(rc))
    {
        rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: setting image size failed for '%s'"), pImage->pszFilename);
        goto out;
    }

    /* Use specified image uuid */
    *getImageCreationUUID(&pImage->Header) = *pUuid;

    /* Generate image last-modify uuid */
    RTUuidCreate(getImageModificationUUID(&pImage->Header));

    /* Write pre-header. */
    rc = vdiFileWriteSync(pImage, 0, &pImage->PreHeader, sizeof(pImage->PreHeader), NULL);
    if (RT_FAILURE(rc))
    {
        rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: writing pre-header failed for '%s'"), pImage->pszFilename);
        goto out;
    }

    /* Write header. */
    rc = vdiFileWriteSync(pImage, sizeof(pImage->PreHeader),
                          &pImage->Header.u.v1plus,
                          sizeof(pImage->Header.u.v1plus), NULL);
    if (RT_FAILURE(rc))
    {
        rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: writing header failed for '%s'"), pImage->pszFilename);
        goto out;
    }

    rc = vdiFileWriteSync(pImage, pImage->offStartBlocks, pImage->paBlocks,
                          getImageBlocks(&pImage->Header) * sizeof(VDIIMAGEBLOCKPOINTER),
                          NULL);
    if (RT_FAILURE(rc))
    {
        rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: writing block pointers failed for '%s'"), pImage->pszFilename);
        goto out;
    }

    if (uImageFlags & VD_IMAGE_FLAGS_FIXED)
    {
        /* Fill image with zeroes. We do this for every fixed-size image since on some systems
         * (for example Windows Vista), it takes ages to write a block near the end of a sparse
         * file and the guest could complain about an ATA timeout. */

        /** @todo Starting with Linux 2.6.23, there is an fallocate() system call.
         *        Currently supported file systems are ext4 and ocfs2. */

        /* Allocate a temporary zero-filled buffer. Use a bigger block size to optimize writing */
        const size_t cbBuf = 128 * _1K;
        void *pvBuf = RTMemTmpAllocZ(cbBuf);
        if (!pvBuf)
        {
            rc = VERR_NO_MEMORY;
            goto out;
        }

        cbFill = (uint64_t)getImageBlocks(&pImage->Header) * pImage->cbTotalBlockData;
        uOff = 0;
        /* Write data to all image blocks. */
        while (uOff < cbFill)
        {
            unsigned cbChunk = (unsigned)RT_MIN(cbFill, cbBuf);

            rc = vdiFileWriteSync(pImage, pImage->offStartData + uOff,
                                  pvBuf, cbChunk, NULL);
            if (RT_FAILURE(rc))
            {
                rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: writing block failed for '%s'"), pImage->pszFilename);
                RTMemTmpFree(pvBuf);
                goto out;
            }

            uOff += cbChunk;

            if (pfnProgress)
            {
                rc = pfnProgress(pvUser,
                                 uPercentStart + uOff * uPercentSpan / cbFill);
                if (RT_FAILURE(rc))
                    goto out;
            }
        }
        RTMemTmpFree(pvBuf);
    }

out:
    if (RT_SUCCESS(rc) && pfnProgress)
        pfnProgress(pvUser, uPercentStart + uPercentSpan);

    if (RT_FAILURE(rc))
        vdiFreeImage(pImage, rc != VERR_ALREADY_EXISTS);
    return rc;
}

/**
 * Internal: Open a VDI image.
 */
static int vdiOpenImage(PVDIIMAGEDESC pImage, unsigned uOpenFlags)
{
    int rc;

    pImage->uOpenFlags = uOpenFlags;

    pImage->pInterfaceError = VDInterfaceGet(pImage->pVDIfsDisk, VDINTERFACETYPE_ERROR);
    if (pImage->pInterfaceError)
        pImage->pInterfaceErrorCallbacks = VDGetInterfaceError(pImage->pInterfaceError);

    /* Get I/O interface. */
    pImage->pInterfaceIO = VDInterfaceGet(pImage->pVDIfsImage, VDINTERFACETYPE_IOINT);
    AssertPtrReturn(pImage->pInterfaceIO, VERR_INVALID_PARAMETER);
    pImage->pInterfaceIOCallbacks = VDGetInterfaceIOInt(pImage->pInterfaceIO);
    AssertPtrReturn(pImage->pInterfaceIOCallbacks, VERR_INVALID_PARAMETER);

    /*
     * Open the image.
     */
    rc = vdiFileOpen(pImage, pImage->pszFilename,
                     VDOpenFlagsToFileOpenFlags(uOpenFlags, false /* fCreate */));
    if (RT_FAILURE(rc))
    {
        /* Do NOT signal an appropriate error here, as the VD layer has the
         * choice of retrying the open if it failed. */
        goto out;
    }

    /* Read pre-header. */
    rc = vdiFileReadSync(pImage, 0, &pImage->PreHeader, sizeof(pImage->PreHeader), NULL);
    if (RT_FAILURE(rc))
    {
        vdiError(pImage, rc, RT_SRC_POS, N_("VDI: error reading pre-header in '%s'"), pImage->pszFilename);
        rc = VERR_VD_VDI_INVALID_HEADER;
        goto out;
    }
    rc = vdiValidatePreHeader(&pImage->PreHeader);
    if (RT_FAILURE(rc))
    {
        rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: invalid pre-header in '%s'"), pImage->pszFilename);
        goto out;
    }

    /* Read header. */
    pImage->Header.uVersion = pImage->PreHeader.u32Version;
    switch (GET_MAJOR_HEADER_VERSION(&pImage->Header))
    {
        case 0:
            rc = vdiFileReadSync(pImage, sizeof(pImage->PreHeader),
                                 &pImage->Header.u.v0, sizeof(pImage->Header.u.v0),
                                 NULL);
            if (RT_FAILURE(rc))
            {
                rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: error reading v0 header in '%s'"), pImage->pszFilename);
                goto out;
            }
            break;
        case 1:
            rc = vdiFileReadSync(pImage, sizeof(pImage->PreHeader),
                                 &pImage->Header.u.v1, sizeof(pImage->Header.u.v1),
                                 NULL);
            if (RT_FAILURE(rc))
            {
                rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: error reading v1 header in '%s'"), pImage->pszFilename);
                goto out;
            }
            /* Convert VDI 1.1 images to VDI 1.1+ on open in read/write mode.
             * Conversion is harmless, as any VirtualBox version supporting VDI
             * 1.1 doesn't touch fields it doesn't know about. */
            if (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
                && GET_MINOR_HEADER_VERSION(&pImage->Header) == 1
                && pImage->Header.u.v1.cbHeader < sizeof(pImage->Header.u.v1plus))
            {
                pImage->Header.u.v1plus.cbHeader = sizeof(pImage->Header.u.v1plus);
                /* Mark LCHS geometry not-calculated. */
                pImage->Header.u.v1plus.LCHSGeometry.cCylinders = 0;
                pImage->Header.u.v1plus.LCHSGeometry.cHeads = 0;
                pImage->Header.u.v1plus.LCHSGeometry.cSectors = 0;
                pImage->Header.u.v1plus.LCHSGeometry.cbSector = VDI_GEOMETRY_SECTOR_SIZE;
            }
            else if (pImage->Header.u.v1.cbHeader >= sizeof(pImage->Header.u.v1plus))
            {
                /* Read the actual VDI 1.1+ header completely. */
                rc = vdiFileReadSync(pImage, sizeof(pImage->PreHeader),
                                     &pImage->Header.u.v1plus,
                                     sizeof(pImage->Header.u.v1plus), NULL);
                if (RT_FAILURE(rc))
                {
                    rc = vdiError(pImage, rc, RT_SRC_POS, N_("VDI: error reading v1.1+ header in '%s'"), pImage->pszFilename);
                    goto out;
                }
            }
            break;
        default:
            rc = vdiError(pImage, VERR_VD_VDI_UNSUPPORTED_VERSION, RT_SRC_POS, N_("VDI: unsupported major version %u in '%s'"), GET_MAJOR_HEADER_VERSION(&pImage->Header), pImage->pszFilename);
            goto out;
    }

    rc = vdiValidateHeader(&pImage->Header);
    if (RT_FAILURE(rc))
    {
        rc = vdiError(pImage, VERR_VD_VDI_INVALID_HEADER, RT_SRC_POS, N_("VDI: invalid header in '%s'"), pImage->pszFilename);
        goto out;
    }

    /* Setup image parameters by header. */
    vdiSetupImageDesc(pImage);

    /* Allocate memory for blocks array. */
    pImage->paBlocks = (PVDIIMAGEBLOCKPOINTER)RTMemAlloc(sizeof(VDIIMAGEBLOCKPOINTER) * getImageBlocks(&pImage->Header));
    if (!pImage->paBlocks)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }

    /* Read blocks array. */
    rc = vdiFileReadSync(pImage, pImage->offStartBlocks, pImage->paBlocks,
                         getImageBlocks(&pImage->Header) * sizeof(VDIIMAGEBLOCKPOINTER),
                         NULL);

out:
    if (RT_FAILURE(rc))
        vdiFreeImage(pImage, false);
    return rc;
}

/**
 * Internal: Save header to file.
 */
static int vdiUpdateHeader(PVDIIMAGEDESC pImage)
{
    int rc;
    switch (GET_MAJOR_HEADER_VERSION(&pImage->Header))
    {
        case 0:
            rc = vdiFileWriteSync(pImage, sizeof(VDIPREHEADER),
                                  &pImage->Header.u.v0, sizeof(pImage->Header.u.v0),
                                  NULL);
            break;
        case 1:
            if (pImage->Header.u.v1plus.cbHeader < sizeof(pImage->Header.u.v1plus))
                rc = vdiFileWriteSync(pImage, sizeof(VDIPREHEADER),
                                      &pImage->Header.u.v1, sizeof(pImage->Header.u.v1),
                                      NULL);
            else
                rc = vdiFileWriteSync(pImage, sizeof(VDIPREHEADER),
                                      &pImage->Header.u.v1plus, sizeof(pImage->Header.u.v1plus),
                                      NULL);
            break;
        default:
            rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
            break;
    }
    AssertMsgRC(rc, ("vdiUpdateHeader failed, filename=\"%s\" rc=%Rrc\n", pImage->pszFilename, rc));
    return rc;
}

/**
 * Internal: Save header to file - async version.
 */
static int vdiUpdateHeaderAsync(PVDIIMAGEDESC pImage, PVDIOCTX pIoCtx)
{
    int rc;
    switch (GET_MAJOR_HEADER_VERSION(&pImage->Header))
    {
        case 0:
            rc = vdiFileWriteMetaAsync(pImage, sizeof(VDIPREHEADER),
                                       &pImage->Header.u.v0,
                                       sizeof(pImage->Header.u.v0),
                                       pIoCtx, NULL, NULL);
            break;
        case 1:
            if (pImage->Header.u.v1plus.cbHeader < sizeof(pImage->Header.u.v1plus))
                rc = vdiFileWriteMetaAsync(pImage, sizeof(VDIPREHEADER),
                                           &pImage->Header.u.v1,
                                           sizeof(pImage->Header.u.v1),
                                           pIoCtx, NULL, NULL);
            else
                rc = vdiFileWriteMetaAsync(pImage, sizeof(VDIPREHEADER),
                                           &pImage->Header.u.v1plus,
                                           sizeof(pImage->Header.u.v1plus),
                                           pIoCtx, NULL, NULL);
            break;
        default:
            rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
            break;
    }
    AssertMsg(RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS,
              ("vdiUpdateHeader failed, filename=\"%s\" rc=%Rrc\n", pImage->pszFilename, rc));
    return rc;
}

/**
 * Internal: Save block pointer to file, save header to file.
 */
static int vdiUpdateBlockInfo(PVDIIMAGEDESC pImage, unsigned uBlock)
{
    /* Update image header. */
    int rc = vdiUpdateHeader(pImage);
    if (RT_SUCCESS(rc))
    {
        /* write only one block pointer. */
        rc = vdiFileWriteSync(pImage, pImage->offStartBlocks + uBlock * sizeof(VDIIMAGEBLOCKPOINTER),
                              &pImage->paBlocks[uBlock], sizeof(VDIIMAGEBLOCKPOINTER),
                              NULL);
        AssertMsgRC(rc, ("vdiUpdateBlockInfo failed to update block=%u, filename=\"%s\", rc=%Rrc\n",
                         uBlock, pImage->pszFilename, rc));
    }
    return rc;
}

/**
 * Internal: Save block pointer to file, save header to file - async version.
 */
static int vdiUpdateBlockInfoAsync(PVDIIMAGEDESC pImage, unsigned uBlock,
                                   PVDIOCTX pIoCtx)
{
    /* Update image header. */
    int rc = vdiUpdateHeaderAsync(pImage, pIoCtx);
    if (RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS)
    {
        /* write only one block pointer. */
        rc = vdiFileWriteMetaAsync(pImage,
                                   pImage->offStartBlocks + uBlock * sizeof(VDIIMAGEBLOCKPOINTER),
                                   &pImage->paBlocks[uBlock],
                                   sizeof(VDIIMAGEBLOCKPOINTER),
                                   pIoCtx, NULL, NULL);
        AssertMsg(RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS,
                  ("vdiUpdateBlockInfo failed to update block=%u, filename=\"%s\", rc=%Rrc\n",
                  uBlock, pImage->pszFilename, rc));
    }
    return rc;
}

/**
 * Internal: Flush the image file to disk - async version.
 */
static int vdiFlushImageAsync(PVDIIMAGEDESC pImage, PVDIOCTX pIoCtx)
{
    int rc = VINF_SUCCESS;

    if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
    {
        /* Save header. */
        rc = vdiUpdateHeaderAsync(pImage, pIoCtx);
        AssertMsg(RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS,
                  ("vdiUpdateHeaderAsync() failed, filename=\"%s\", rc=%Rrc\n",
                  pImage->pszFilename, rc));
        rc = vdiFileFlushAsync(pImage, pIoCtx, NULL, NULL);
        AssertMsg(RT_SUCCESS(rc) || rc == VERR_VD_ASYNC_IO_IN_PROGRESS,
                  ("Flushing data to disk failed rc=%Rrc\n", rc));
    }

    return rc;
}


/** @copydoc VBOXHDDBACKEND::pfnCheckIfValid */
static int vdiCheckIfValid(const char *pszFilename, PVDINTERFACE pVDIfsDisk,
                           PVDINTERFACE pVDIfsImage, VDTYPE *penmType)
{
    LogFlowFunc(("pszFilename=\"%s\"\n", pszFilename));
    int rc = VINF_SUCCESS;
    PVDIIMAGEDESC pImage;

    if (   !VALID_PTR(pszFilename)
        || !*pszFilename)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    pImage = (PVDIIMAGEDESC)RTMemAllocZ(sizeof(VDIIMAGEDESC));
    if (!pImage)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pImage->pszFilename = pszFilename;
    pImage->pStorage = NULL;
    pImage->paBlocks = NULL;
    pImage->pVDIfsDisk = pVDIfsDisk;
    pImage->pVDIfsImage = pVDIfsImage;

    rc = vdiOpenImage(pImage, VD_OPEN_FLAGS_INFO | VD_OPEN_FLAGS_READONLY);
    vdiFreeImage(pImage, false);
    RTMemFree(pImage);

    if (RT_SUCCESS(rc))
        *penmType = VDTYPE_HDD;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnOpen */
static int vdiOpen(const char *pszFilename, unsigned uOpenFlags,
                   PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                   VDTYPE enmType, void **ppBackendData)
{
    LogFlowFunc(("pszFilename=\"%s\" uOpenFlags=%#x pVDIfsDisk=%#p pVDIfsImage=%#p ppBackendData=%#p\n", pszFilename, uOpenFlags, pVDIfsDisk, pVDIfsImage, ppBackendData));
    int rc;
    PVDIIMAGEDESC pImage;

    /* Check open flags. All valid flags are supported. */
    if (uOpenFlags & ~VD_OPEN_FLAGS_MASK)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Check remaining arguments. */
    if (   !VALID_PTR(pszFilename)
        || !*pszFilename)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    pImage = (PVDIIMAGEDESC)RTMemAllocZ(sizeof(VDIIMAGEDESC));
    if (!pImage)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pImage->pszFilename = pszFilename;
    pImage->pStorage = NULL;
    pImage->paBlocks = NULL;
    pImage->pVDIfsDisk = pVDIfsDisk;
    pImage->pVDIfsImage = pVDIfsImage;

    rc = vdiOpenImage(pImage, uOpenFlags);
    if (RT_SUCCESS(rc))
        *ppBackendData = pImage;

out:
    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnCreate */
static int vdiCreate(const char *pszFilename, uint64_t cbSize,
                     unsigned uImageFlags, const char *pszComment,
                     PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                     PCRTUUID pUuid, unsigned uOpenFlags,
                     unsigned uPercentStart, unsigned uPercentSpan,
                     PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                     PVDINTERFACE pVDIfsOperation, void **ppBackendData)
{
    LogFlowFunc(("pszFilename=\"%s\" cbSize=%llu uImageFlags=%#x pszComment=\"%s\" pPCHSGeometry=%#p pLCHSGeometry=%#p Uuid=%RTuuid uOpenFlags=%#x uPercentStart=%u uPercentSpan=%u pVDIfsDisk=%#p pVDIfsImage=%#p pVDIfsOperation=%#p ppBackendData=%#p\n", pszFilename, cbSize, uImageFlags, pszComment, pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags, uPercentStart, uPercentSpan, pVDIfsDisk, pVDIfsImage, pVDIfsOperation, ppBackendData));
    int rc;
    PVDIIMAGEDESC pImage;

    PFNVDPROGRESS pfnProgress = NULL;
    void *pvUser = NULL;
    PVDINTERFACE pIfProgress = VDInterfaceGet(pVDIfsOperation,
                                              VDINTERFACETYPE_PROGRESS);
    PVDINTERFACEPROGRESS pCbProgress = NULL;
    if (pIfProgress)
    {
        pCbProgress = VDGetInterfaceProgress(pIfProgress);
        if (pCbProgress)
            pfnProgress = pCbProgress->pfnProgress;
        pvUser = pIfProgress->pvUser;
    }

    /* Check the image flags. */
    if ((uImageFlags & ~VD_VDI_IMAGE_FLAGS_MASK) != 0)
    {
        rc = VERR_VD_INVALID_TYPE;
        goto out;
    }

    /* Check open flags. All valid flags are supported. */
    if (uOpenFlags & ~VD_OPEN_FLAGS_MASK)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Check size. Maximum 4PB-3M. No tricks with adjusting the 1M block size
     * so far, which would extend the size. */
    cbSize = RT_ALIGN_64(cbSize, _1M);
    if (    !cbSize
        ||  cbSize >= _1P * 4 - _1M * 3)
    {
        rc = VERR_VD_INVALID_SIZE;
        goto out;
    }

    /* Check remaining arguments. */
    if (   !VALID_PTR(pszFilename)
        || !*pszFilename
        || cbSize < VDI_IMAGE_DEFAULT_BLOCK_SIZE
        || !VALID_PTR(pPCHSGeometry)
        || !VALID_PTR(pLCHSGeometry))
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    pImage = (PVDIIMAGEDESC)RTMemAllocZ(sizeof(VDIIMAGEDESC));
    if (!pImage)
    {
        rc = VERR_NO_MEMORY;
        goto out;
    }
    pImage->pszFilename = pszFilename;
    pImage->pStorage = NULL;
    pImage->paBlocks = NULL;
    pImage->pVDIfsDisk = pVDIfsDisk;
    pImage->pVDIfsImage = pVDIfsImage;

    rc = vdiCreateImage(pImage, cbSize, uImageFlags, pszComment,
                        pPCHSGeometry, pLCHSGeometry, pUuid, uOpenFlags,
                        pfnProgress, pvUser, uPercentStart, uPercentSpan);
    if (RT_SUCCESS(rc))
    {
        /* So far the image is opened in read/write mode. Make sure the
         * image is opened in read-only mode if the caller requested that. */
        if (uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            vdiFreeImage(pImage, false);
            rc = vdiOpenImage(pImage, uOpenFlags);
            if (RT_FAILURE(rc))
            {
                RTMemFree(pImage);
                goto out;
            }
        }
        *ppBackendData = pImage;
    }
    else
        RTMemFree(pImage);

out:
    LogFlowFunc(("returns %Rrc (pBackendData=%#p)\n", rc, *ppBackendData));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnRename */
static int vdiRename(void *pBackendData, const char *pszFilename)
{
    LogFlowFunc(("pBackendData=%#p pszFilename=%#p\n", pBackendData, pszFilename));

    int rc = VINF_SUCCESS;
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    /* Check arguments. */
    if (   !pImage
        || !pszFilename
        || !*pszFilename)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Close the image. */
    vdiFreeImage(pImage, false);

    /* Rename the file. */
    rc = vdiFileMove(pImage, pImage->pszFilename, pszFilename, 0);
    if (RT_FAILURE(rc))
    {
        /* The move failed, try to reopen the original image. */
        int rc2 = vdiOpenImage(pImage, pImage->uOpenFlags);
        if (RT_FAILURE(rc2))
            rc = rc2;

        goto out;
    }

    /* Update pImage with the new information. */
    pImage->pszFilename = pszFilename;

    /* Open the new image. */
    rc = vdiOpenImage(pImage, pImage->uOpenFlags);
    if (RT_FAILURE(rc))
        goto out;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnClose */
static int vdiClose(void *pBackendData, bool fDelete)
{
    LogFlowFunc(("pBackendData=%#p fDelete=%d\n", pBackendData, fDelete));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    rc = vdiFreeImage(pImage, fDelete);
    RTMemFree(pImage);

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnRead */
static int vdiRead(void *pBackendData, uint64_t uOffset, void *pvBuf,
                   size_t cbToRead, size_t *pcbActuallyRead)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pvBuf=%#p cbToRead=%zu pcbActuallyRead=%#p\n", pBackendData, uOffset, pvBuf, cbToRead, pcbActuallyRead));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uBlock;
    unsigned offRead;
    int rc;

    AssertPtr(pImage);
    Assert(!(uOffset % 512));
    Assert(!(cbToRead % 512));

    if (   uOffset + cbToRead > getImageDiskSize(&pImage->Header)
        || !VALID_PTR(pvBuf)
        || !cbToRead)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Calculate starting block number and offset inside it. */
    uBlock = (unsigned)(uOffset >> pImage->uShiftOffset2Index);
    offRead = (unsigned)uOffset & pImage->uBlockMask;

    /* Clip read range to at most the rest of the block. */
    cbToRead = RT_MIN(cbToRead, getImageBlockSize(&pImage->Header) - offRead);
    Assert(!(cbToRead % 512));

    if (pImage->paBlocks[uBlock] == VDI_IMAGE_BLOCK_FREE)
        rc = VERR_VD_BLOCK_FREE;
    else if (pImage->paBlocks[uBlock] == VDI_IMAGE_BLOCK_ZERO)
    {
        memset(pvBuf, 0, cbToRead);
        rc = VINF_SUCCESS;
    }
    else
    {
        /* Block present in image file, read relevant data. */
        uint64_t u64Offset = (uint64_t)pImage->paBlocks[uBlock] * pImage->cbTotalBlockData
                           + (pImage->offStartData + pImage->offStartBlockData + offRead);
        rc = vdiFileReadSync(pImage, u64Offset, pvBuf, cbToRead, NULL);
    }

    if (pcbActuallyRead)
        *pcbActuallyRead = cbToRead;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**@copydoc VBOXHDDBACKEND::pfnWrite */
static int vdiWrite(void *pBackendData, uint64_t uOffset, const void *pvBuf,
                    size_t cbToWrite, size_t *pcbWriteProcess,
                    size_t *pcbPreRead, size_t *pcbPostRead, unsigned fWrite)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pvBuf=%#p cbToWrite=%zu pcbWriteProcess=%#p pcbPreRead=%#p pcbPostRead=%#p\n", pBackendData, uOffset, pvBuf, cbToWrite, pcbWriteProcess, pcbPreRead, pcbPostRead));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uBlock;
    unsigned offWrite;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);
    Assert(!(uOffset % 512));
    Assert(!(cbToWrite % 512));

    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
    {
        rc = VERR_VD_IMAGE_READ_ONLY;
        goto out;
    }

    if (!VALID_PTR(pvBuf) || !cbToWrite)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* No size check here, will do that later.  For dynamic images which are
     * not multiples of the block size in length, this would prevent writing to
     * the last block. */

    /* Calculate starting block number and offset inside it. */
    uBlock = (unsigned)(uOffset >> pImage->uShiftOffset2Index);
    offWrite = (unsigned)uOffset & pImage->uBlockMask;

    /* Clip write range to at most the rest of the block. */
    cbToWrite = RT_MIN(cbToWrite, getImageBlockSize(&pImage->Header) - offWrite);
    Assert(!(cbToWrite % 512));

    do
    {
        if (!IS_VDI_IMAGE_BLOCK_ALLOCATED(pImage->paBlocks[uBlock]))
        {
            /* Block is either free or zero. */
            if (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_HONOR_ZEROES)
                && (   pImage->paBlocks[uBlock] == VDI_IMAGE_BLOCK_ZERO
                    || cbToWrite == getImageBlockSize(&pImage->Header)))
            {
                /* If the destination block is unallocated at this point, it's
                 * either a zero block or a block which hasn't been used so far
                 * (which also means that it's a zero block. Don't need to write
                 * anything to this block  if the data consists of just zeroes. */
                Assert(!(cbToWrite % 4));
                Assert(cbToWrite * 8 <= UINT32_MAX);
                if (ASMBitFirstSet((volatile void *)pvBuf, (uint32_t)cbToWrite * 8) == -1)
                {
                    pImage->paBlocks[uBlock] = VDI_IMAGE_BLOCK_ZERO;
                    *pcbPreRead = 0;
                    *pcbPostRead = 0;
                    break;
                }
            }

            if (   cbToWrite == getImageBlockSize(&pImage->Header)
                && !(fWrite & VD_WRITE_NO_ALLOC))
            {
                /* Full block write to previously unallocated block.
                 * Allocate block and write data. */
                Assert(!offWrite);
                unsigned cBlocksAllocated = getImageBlocksAllocated(&pImage->Header);
                uint64_t u64Offset = (uint64_t)cBlocksAllocated * pImage->cbTotalBlockData
                                   + (pImage->offStartData + pImage->offStartBlockData);
                rc = vdiFileWriteSync(pImage, u64Offset, pvBuf, cbToWrite, NULL);
                if (RT_FAILURE(rc))
                    goto out;
                pImage->paBlocks[uBlock] = cBlocksAllocated;
                setImageBlocksAllocated(&pImage->Header, cBlocksAllocated + 1);

                rc = vdiUpdateBlockInfo(pImage, uBlock);
                if (RT_FAILURE(rc))
                    goto out;

                *pcbPreRead = 0;
                *pcbPostRead = 0;
            }
            else
            {
                /* Trying to do a partial write to an unallocated block. Don't do
                 * anything except letting the upper layer know what to do. */
                *pcbPreRead = offWrite % getImageBlockSize(&pImage->Header);
                *pcbPostRead = getImageBlockSize(&pImage->Header) - cbToWrite - *pcbPreRead;
                rc = VERR_VD_BLOCK_FREE;
            }
        }
        else
        {
            /* Block present in image file, write relevant data. */
            uint64_t u64Offset = (uint64_t)pImage->paBlocks[uBlock] * pImage->cbTotalBlockData
                               + (pImage->offStartData + pImage->offStartBlockData + offWrite);
            rc = vdiFileWriteSync(pImage, u64Offset, pvBuf, cbToWrite, NULL);
        }
    } while (0);

    if (pcbWriteProcess)
        *pcbWriteProcess = cbToWrite;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnFlush */
static int vdiFlush(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    Assert(pImage);

    vdiFlushImage(pImage);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnGetVersion */
static unsigned vdiGetVersion(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uVersion;

    AssertPtr(pImage);

    if (pImage)
        uVersion = pImage->PreHeader.u32Version;
    else
        uVersion = 0;

    LogFlowFunc(("returns %#x\n", uVersion));
    return uVersion;
}

/** @copydoc VBOXHDDBACKEND::pfnGetSize */
static uint64_t vdiGetSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    uint64_t cbSize;

    AssertPtr(pImage);

    if (pImage)
        cbSize = getImageDiskSize(&pImage->Header);
    else
        cbSize = 0;

    LogFlowFunc(("returns %llu\n", cbSize));
    return cbSize;
}

/** @copydoc VBOXHDDBACKEND::pfnGetFileSize */
static uint64_t vdiGetFileSize(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    uint64_t cb = 0;

    AssertPtr(pImage);

    if (pImage)
    {
        uint64_t cbFile;
        if (pImage->pStorage)
        {
            int rc = vdiFileGetSize(pImage, &cbFile);
            if (RT_SUCCESS(rc))
                cb += cbFile;
        }
    }

    LogFlowFunc(("returns %lld\n", cb));
    return cb;
}

/** @copydoc VBOXHDDBACKEND::pfnGetPCHSGeometry */
static int vdiGetPCHSGeometry(void *pBackendData, PVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p\n", pBackendData, pPCHSGeometry));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->PCHSGeometry.cCylinders)
        {
            *pPCHSGeometry = pImage->PCHSGeometry;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_VD_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (PCHS=%u/%u/%u)\n", rc, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnSetPCHSGeometry */
static int vdiSetPCHSGeometry(void *pBackendData, PCVDGEOMETRY pPCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pPCHSGeometry=%#p PCHS=%u/%u/%u\n", pBackendData, pPCHSGeometry, pPCHSGeometry->cCylinders, pPCHSGeometry->cHeads, pPCHSGeometry->cSectors));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            rc = VERR_VD_IMAGE_READ_ONLY;
            goto out;
        }

        pImage->PCHSGeometry = *pPCHSGeometry;
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnGetLCHSGeometry */
static int vdiGetLCHSGeometry(void *pBackendData, PVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p\n", pBackendData, pLCHSGeometry));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        VDIDISKGEOMETRY DummyGeo = { 0, 0, 0, VDI_GEOMETRY_SECTOR_SIZE };
        PVDIDISKGEOMETRY pGeometry = getImageLCHSGeometry(&pImage->Header);
        if (!pGeometry)
            pGeometry = &DummyGeo;

        if (    pGeometry->cCylinders > 0
            &&  pGeometry->cHeads > 0
            &&  pGeometry->cSectors > 0)
        {
            pLCHSGeometry->cCylinders = pGeometry->cCylinders;
            pLCHSGeometry->cHeads = pGeometry->cHeads;
            pLCHSGeometry->cSectors = pGeometry->cSectors;
            rc = VINF_SUCCESS;
        }
        else
            rc = VERR_VD_GEOMETRY_NOT_SET;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (LCHS=%u/%u/%u)\n", rc, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnSetLCHSGeometry */
static int vdiSetLCHSGeometry(void *pBackendData, PCVDGEOMETRY pLCHSGeometry)
{
    LogFlowFunc(("pBackendData=%#p pLCHSGeometry=%#p LCHS=%u/%u/%u\n", pBackendData, pLCHSGeometry, pLCHSGeometry->cCylinders, pLCHSGeometry->cHeads, pLCHSGeometry->cSectors));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    PVDIDISKGEOMETRY pGeometry;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
        {
            rc = VERR_VD_IMAGE_READ_ONLY;
            goto out;
        }

        pGeometry = getImageLCHSGeometry(&pImage->Header);
        if (pGeometry)
        {
            pGeometry->cCylinders = pLCHSGeometry->cCylinders;
            pGeometry->cHeads = pLCHSGeometry->cHeads;
            pGeometry->cSectors = pLCHSGeometry->cSectors;
            pGeometry->cbSector = VDI_GEOMETRY_SECTOR_SIZE;

            /* Update header information in base image file. */
            vdiFlushImage(pImage);
        }
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnGetImageFlags */
static unsigned vdiGetImageFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uImageFlags;

    AssertPtr(pImage);

    if (pImage)
        uImageFlags = pImage->uImageFlags;
    else
        uImageFlags = 0;

    LogFlowFunc(("returns %#x\n", uImageFlags));
    return uImageFlags;
}

/** @copydoc VBOXHDDBACKEND::pfnGetOpenFlags */
static unsigned vdiGetOpenFlags(void *pBackendData)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uOpenFlags;

    AssertPtr(pImage);

    if (pImage)
        uOpenFlags = pImage->uOpenFlags;
    else
        uOpenFlags = 0;

    LogFlowFunc(("returns %#x\n", uOpenFlags));
    return uOpenFlags;
}

/** @copydoc VBOXHDDBACKEND::pfnSetOpenFlags */
static int vdiSetOpenFlags(void *pBackendData, unsigned uOpenFlags)
{
    LogFlowFunc(("pBackendData=%#p uOpenFlags=%#x\n", pBackendData, uOpenFlags));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;
    const char *pszFilename;

    /* Image must be opened and the new flags must be valid. */
    if (!pImage || (uOpenFlags & ~(VD_OPEN_FLAGS_READONLY | VD_OPEN_FLAGS_INFO | VD_OPEN_FLAGS_ASYNC_IO | VD_OPEN_FLAGS_SHAREABLE | VD_OPEN_FLAGS_SEQUENTIAL)))
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Implement this operation via reopening the image. */
    pszFilename = pImage->pszFilename;
    rc = vdiFreeImage(pImage, false);
    if (RT_FAILURE(rc))
        goto out;
    rc = vdiOpenImage(pImage, uOpenFlags);

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnGetComment */
static int vdiGetComment(void *pBackendData, char *pszComment,
                         size_t cbComment)
{
    LogFlowFunc(("pBackendData=%#p pszComment=%#p cbComment=%zu\n", pBackendData, pszComment, cbComment));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);

    if (pImage)
    {
        char *pszTmp = getImageComment(&pImage->Header);
        /* Make this foolproof even if the image doesn't have the zero
         * termination. With some luck the repaired header will be saved. */
        size_t cb = RTStrNLen(pszTmp, VDI_IMAGE_COMMENT_SIZE);
        if (cb == VDI_IMAGE_COMMENT_SIZE)
        {
            pszTmp[VDI_IMAGE_COMMENT_SIZE-1] = '\0';
            cb--;
        }
        if (cb < cbComment)
        {
            /* memcpy is much better than strncpy. */
            memcpy(pszComment, pszTmp, cb + 1);
        }
        else
            rc = VERR_BUFFER_OVERFLOW;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc comment=\"%s\"\n", rc, pszComment));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnGetComment */
static int vdiSetComment(void *pBackendData, const char *pszComment)
{
    LogFlowFunc(("pBackendData=%#p pszComment=\"%s\"\n", pBackendData, pszComment));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
            rc = VERR_VD_IMAGE_READ_ONLY;
        else
        {
            size_t cchComment = pszComment ? strlen(pszComment) : 0;
            if (cchComment >= VDI_IMAGE_COMMENT_SIZE)
            {
                LogFunc(("pszComment is too long, %d bytes!\n", cchComment));
                rc = VERR_VD_VDI_COMMENT_TOO_LONG;
                goto out;
            }

            /* we don't support old style images */
            if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
            {
                /*
                 * Update the comment field, making sure to zero out all of the previous comment.
                 */
                memset(pImage->Header.u.v1.szComment, '\0', VDI_IMAGE_COMMENT_SIZE);
                memcpy(pImage->Header.u.v1.szComment, pszComment, cchComment);

                /* write out new the header */
                rc = vdiUpdateHeader(pImage);
            }
            else
                rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
        }
    }
    else
        rc = VERR_VD_NOT_OPENED;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnGetUuid */
static int vdiGetUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        *pUuid = *getImageCreationUUID(&pImage->Header);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnSetUuid */
static int vdiSetUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);

    if (pImage)
    {
        if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
        {
            if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
                pImage->Header.u.v1.uuidCreate = *pUuid;
            /* Make it possible to clone old VDIs. */
            else if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 0)
                pImage->Header.u.v0.uuidCreate = *pUuid;
            else
            {
                LogFunc(("Version is not supported!\n"));
                rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
            }
        }
        else
            rc = VERR_VD_IMAGE_READ_ONLY;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnGetModificationUuid */
static int vdiGetModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        *pUuid = *getImageModificationUUID(&pImage->Header);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnSetModificationUuid */
static int vdiSetModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);

    if (pImage)
    {
        if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
        {
            if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
                pImage->Header.u.v1.uuidModify = *pUuid;
            /* Make it possible to clone old VDIs. */
            else if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 0)
                pImage->Header.u.v0.uuidModify = *pUuid;
            else
            {
                LogFunc(("Version is not supported!\n"));
                rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
            }
        }
        else
            rc = VERR_VD_IMAGE_READ_ONLY;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnGetParentUuid */
static int vdiGetParentUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        *pUuid = *getImageParentUUID(&pImage->Header);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnSetParentUuid */
static int vdiSetParentUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);

    if (pImage)
    {
        if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
        {
            if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
                pImage->Header.u.v1.uuidLinkage = *pUuid;
            /* Make it possible to clone old VDIs. */
            else if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 0)
                pImage->Header.u.v0.uuidLinkage = *pUuid;
            else
            {
                LogFunc(("Version is not supported!\n"));
                rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
            }
        }
        else
            rc = VERR_VD_IMAGE_READ_ONLY;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnGetParentModificationUuid */
static int vdiGetParentModificationUuid(void *pBackendData, PRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p pUuid=%#p\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc;

    AssertPtr(pImage);

    if (pImage)
    {
        *pUuid = *getImageParentModificationUUID(&pImage->Header);
        rc = VINF_SUCCESS;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc (%RTuuid)\n", rc, pUuid));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnSetParentModificationUuid */
static int vdiSetParentModificationUuid(void *pBackendData, PCRTUUID pUuid)
{
    LogFlowFunc(("pBackendData=%#p Uuid=%RTuuid\n", pBackendData, pUuid));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);

    if (pImage)
    {
        if (!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY))
        {
            if (GET_MAJOR_HEADER_VERSION(&pImage->Header) == 1)
                pImage->Header.u.v1.uuidParentModify = *pUuid;
            else
            {
                LogFunc(("Version is not supported!\n"));
                rc = VERR_VD_VDI_UNSUPPORTED_VERSION;
            }
        }
        else
            rc = VERR_VD_IMAGE_READ_ONLY;
    }
    else
        rc = VERR_VD_NOT_OPENED;

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnDump */
static void vdiDump(void *pBackendData)
{
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;

    vdiMessage(pImage, "Dumping VDI image \"%s\" mode=%s uOpenFlags=%X File=%#p\n",
                pImage->pszFilename,
                (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY) ? "r/o" : "r/w",
                pImage->uOpenFlags,
                pImage->pStorage);
    vdiMessage(pImage, "Header: Version=%08X Type=%X Flags=%X Size=%llu\n",
                pImage->PreHeader.u32Version,
                getImageType(&pImage->Header),
                getImageFlags(&pImage->Header),
                getImageDiskSize(&pImage->Header));
    vdiMessage(pImage, "Header: cbBlock=%u cbBlockExtra=%u cBlocks=%u cBlocksAllocated=%u\n",
                getImageBlockSize(&pImage->Header),
                getImageExtraBlockSize(&pImage->Header),
                getImageBlocks(&pImage->Header),
                getImageBlocksAllocated(&pImage->Header));
    vdiMessage(pImage, "Header: offBlocks=%u offData=%u\n",
                getImageBlocksOffset(&pImage->Header),
                getImageDataOffset(&pImage->Header));
    PVDIDISKGEOMETRY pg = getImageLCHSGeometry(&pImage->Header);
    if (pg)
        vdiMessage(pImage, "Header: Geometry: C/H/S=%u/%u/%u cbSector=%u\n",
                    pg->cCylinders, pg->cHeads, pg->cSectors, pg->cbSector);
    vdiMessage(pImage, "Header: uuidCreation={%RTuuid}\n", getImageCreationUUID(&pImage->Header));
    vdiMessage(pImage, "Header: uuidModification={%RTuuid}\n", getImageModificationUUID(&pImage->Header));
    vdiMessage(pImage, "Header: uuidParent={%RTuuid}\n", getImageParentUUID(&pImage->Header));
    if (GET_MAJOR_HEADER_VERSION(&pImage->Header) >= 1)
        vdiMessage(pImage, "Header: uuidParentModification={%RTuuid}\n", getImageParentModificationUUID(&pImage->Header));
    vdiMessage(pImage, "Image:  fFlags=%08X offStartBlocks=%u offStartData=%u\n",
                pImage->uImageFlags, pImage->offStartBlocks, pImage->offStartData);
    vdiMessage(pImage, "Image:  uBlockMask=%08X cbTotalBlockData=%u uShiftOffset2Index=%u offStartBlockData=%u\n",
                pImage->uBlockMask,
                pImage->cbTotalBlockData,
                pImage->uShiftOffset2Index,
                pImage->offStartBlockData);

    unsigned uBlock, cBlocksNotFree, cBadBlocks, cBlocks = getImageBlocks(&pImage->Header);
    for (uBlock=0, cBlocksNotFree=0, cBadBlocks=0; uBlock<cBlocks; uBlock++)
    {
        if (IS_VDI_IMAGE_BLOCK_ALLOCATED(pImage->paBlocks[uBlock]))
        {
            cBlocksNotFree++;
            if (pImage->paBlocks[uBlock] >= cBlocks)
                cBadBlocks++;
        }
    }
    if (cBlocksNotFree != getImageBlocksAllocated(&pImage->Header))
    {
        vdiMessage(pImage, "!! WARNING: %u blocks actually allocated (cBlocksAllocated=%u) !!\n",
                cBlocksNotFree, getImageBlocksAllocated(&pImage->Header));
    }
    if (cBadBlocks)
    {
        vdiMessage(pImage, "!! WARNING: %u bad blocks found !!\n",
                cBadBlocks);
    }
}

static int vdiAsyncRead(void *pBackendData, uint64_t uOffset, size_t cbToRead,
                        PVDIOCTX pIoCtx, size_t *pcbActuallyRead)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToRead=%zu pcbActuallyRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToRead, pcbActuallyRead));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uBlock;
    unsigned offRead;
    int rc;

    AssertPtr(pImage);
    Assert(!(uOffset % 512));
    Assert(!(cbToRead % 512));

    if (   uOffset + cbToRead > getImageDiskSize(&pImage->Header)
        || !VALID_PTR(pIoCtx)
        || !cbToRead)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* Calculate starting block number and offset inside it. */
    uBlock = (unsigned)(uOffset >> pImage->uShiftOffset2Index);
    offRead = (unsigned)uOffset & pImage->uBlockMask;

    /* Clip read range to at most the rest of the block. */
    cbToRead = RT_MIN(cbToRead, getImageBlockSize(&pImage->Header) - offRead);
    Assert(!(cbToRead % 512));

    if (pImage->paBlocks[uBlock] == VDI_IMAGE_BLOCK_FREE)
        rc = VERR_VD_BLOCK_FREE;
    else if (pImage->paBlocks[uBlock] == VDI_IMAGE_BLOCK_ZERO)
    {
        size_t cbSet;

        cbSet = vdiFileIoCtxSet(pImage, pIoCtx, 0, cbToRead);
        Assert(cbSet == cbToRead);

        rc = VINF_SUCCESS;
    }
    else
    {
        /* Block present in image file, read relevant data. */
        uint64_t u64Offset = (uint64_t)pImage->paBlocks[uBlock] * pImage->cbTotalBlockData
                           + (pImage->offStartData + pImage->offStartBlockData + offRead);
        rc = vdiFileReadUserAsync(pImage, u64Offset, pIoCtx, cbToRead);
    }

    if (pcbActuallyRead)
        *pcbActuallyRead = cbToRead;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

static int vdiAsyncWrite(void *pBackendData, uint64_t uOffset, size_t cbToWrite,
                         PVDIOCTX pIoCtx,
                         size_t *pcbWriteProcess, size_t *pcbPreRead,
                         size_t *pcbPostRead, unsigned fWrite)
{
    LogFlowFunc(("pBackendData=%#p uOffset=%llu pIoCtx=%#p cbToWrite=%zu pcbWriteProcess=%#p pcbPreRead=%#p pcbPostRead=%#p\n",
                 pBackendData, uOffset, pIoCtx, cbToWrite, pcbWriteProcess, pcbPreRead, pcbPostRead));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    unsigned uBlock;
    unsigned offWrite;
    int rc = VINF_SUCCESS;

    AssertPtr(pImage);
    Assert(!(uOffset % 512));
    Assert(!(cbToWrite % 512));

    if (pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY)
    {
        rc = VERR_VD_IMAGE_READ_ONLY;
        goto out;
    }

    if (!VALID_PTR(pIoCtx) || !cbToWrite)
    {
        rc = VERR_INVALID_PARAMETER;
        goto out;
    }

    /* No size check here, will do that later.  For dynamic images which are
     * not multiples of the block size in length, this would prevent writing to
     * the last block. */

    /* Calculate starting block number and offset inside it. */
    uBlock = (unsigned)(uOffset >> pImage->uShiftOffset2Index);
    offWrite = (unsigned)uOffset & pImage->uBlockMask;

    /* Clip write range to at most the rest of the block. */
    cbToWrite = RT_MIN(cbToWrite, getImageBlockSize(&pImage->Header) - offWrite);
    Assert(!(cbToWrite % 512));

    do
    {
        if (!IS_VDI_IMAGE_BLOCK_ALLOCATED(pImage->paBlocks[uBlock]))
        {
            /* Block is either free or zero. */
            if (   !(pImage->uOpenFlags & VD_OPEN_FLAGS_HONOR_ZEROES)
                && (   pImage->paBlocks[uBlock] == VDI_IMAGE_BLOCK_ZERO
                    || cbToWrite == getImageBlockSize(&pImage->Header)))
            {
#if 0 /** @todo Provide interface to check an I/O context for a specific value */
                /* If the destination block is unallocated at this point, it's
                 * either a zero block or a block which hasn't been used so far
                 * (which also means that it's a zero block. Don't need to write
                 * anything to this block  if the data consists of just zeroes. */
                Assert(!(cbToWrite % 4));
                Assert(cbToWrite * 8 <= UINT32_MAX);
                if (ASMBitFirstSet((volatile void *)pvBuf, (uint32_t)cbToWrite * 8) == -1)
                {
                    pImage->paBlocks[uBlock] = VDI_IMAGE_BLOCK_ZERO;
                    break;
                }
#endif
            }

            if (   cbToWrite == getImageBlockSize(&pImage->Header)
                && !(fWrite & VD_WRITE_NO_ALLOC))
            {
                /* Full block write to previously unallocated block.
                 * Allocate block and write data. */
                Assert(!offWrite);
                unsigned cBlocksAllocated = getImageBlocksAllocated(&pImage->Header);
                uint64_t u64Offset = (uint64_t)cBlocksAllocated * pImage->cbTotalBlockData
                                   + (pImage->offStartData + pImage->offStartBlockData);
                rc = vdiFileWriteUserAsync(pImage, u64Offset, pIoCtx, cbToWrite, NULL, NULL);
                if (RT_UNLIKELY(RT_FAILURE_NP(rc) && (rc != VERR_VD_ASYNC_IO_IN_PROGRESS)))
                    goto out;
                pImage->paBlocks[uBlock] = cBlocksAllocated;
                setImageBlocksAllocated(&pImage->Header, cBlocksAllocated + 1);

                rc = vdiUpdateBlockInfoAsync(pImage, uBlock, pIoCtx);
                if (RT_FAILURE(rc) && (rc != VERR_VD_ASYNC_IO_IN_PROGRESS))
                    goto out;

                *pcbPreRead = 0;
                *pcbPostRead = 0;
            }
            else
            {
                /* Trying to do a partial write to an unallocated block. Don't do
                 * anything except letting the upper layer know what to do. */
                *pcbPreRead = offWrite % getImageBlockSize(&pImage->Header);
                *pcbPostRead = getImageBlockSize(&pImage->Header) - cbToWrite - *pcbPreRead;
                rc = VERR_VD_BLOCK_FREE;
            }
        }
        else
        {
            /* Block present in image file, write relevant data. */
            uint64_t u64Offset = (uint64_t)pImage->paBlocks[uBlock] * pImage->cbTotalBlockData
                               + (pImage->offStartData + pImage->offStartBlockData + offWrite);
            rc = vdiFileWriteUserAsync(pImage, u64Offset, pIoCtx, cbToWrite, NULL, NULL);
        }
    } while (0);

    if (pcbWriteProcess)
        *pcbWriteProcess = cbToWrite;

out:
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

static int vdiAsyncFlush(void *pBackendData, PVDIOCTX pIoCtx)
{
    LogFlowFunc(("pBackendData=%#p\n", pBackendData));
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    Assert(pImage);

    rc = vdiFlushImageAsync(pImage, pIoCtx);
    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/** @copydoc VBOXHDDBACKEND::pfnCompact */
static int vdiCompact(void *pBackendData, unsigned uPercentStart,
                      unsigned uPercentSpan, PVDINTERFACE pVDIfsDisk,
                      PVDINTERFACE pVDIfsImage, PVDINTERFACE pVDIfsOperation)
{
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;
    void *pvBuf = NULL, *pvTmp = NULL;
    unsigned *paBlocks2 = NULL;

    int (*pfnParentRead)(void *, uint64_t, void *, size_t) = NULL;
    void *pvParent = NULL;
    PVDINTERFACE pIfParentState = VDInterfaceGet(pVDIfsOperation,
                                                 VDINTERFACETYPE_PARENTSTATE);
    PVDINTERFACEPARENTSTATE pCbParentState = NULL;
    if (pIfParentState)
    {
        pCbParentState = VDGetInterfaceParentState(pIfParentState);
        if (pCbParentState)
            pfnParentRead = pCbParentState->pfnParentRead;
        pvParent = pIfParentState->pvUser;
    }

    PFNVDPROGRESS pfnProgress = NULL;
    void *pvUser = NULL;
    PVDINTERFACE pIfProgress = VDInterfaceGet(pVDIfsOperation,
                                              VDINTERFACETYPE_PROGRESS);
    PVDINTERFACEPROGRESS pCbProgress = NULL;
    if (pIfProgress)
    {
        pCbProgress = VDGetInterfaceProgress(pIfProgress);
        if (pCbProgress)
            pfnProgress = pCbProgress->pfnProgress;
        pvUser = pIfProgress->pvUser;
    }

    do {
        AssertBreakStmt(pImage, rc = VERR_INVALID_PARAMETER);

        AssertBreakStmt(!(pImage->uOpenFlags & VD_OPEN_FLAGS_READONLY),
                        rc = VERR_VD_IMAGE_READ_ONLY);

        unsigned cBlocks;
        unsigned cBlocksToMove = 0;
        size_t cbBlock;
        cBlocks = getImageBlocks(&pImage->Header);
        cbBlock = getImageBlockSize(&pImage->Header);
        if (pfnParentRead)
        {
            pvBuf = RTMemTmpAlloc(cbBlock);
            AssertBreakStmt(VALID_PTR(pvBuf), rc = VERR_NO_MEMORY);
        }
        pvTmp = RTMemTmpAlloc(cbBlock);
        AssertBreakStmt(VALID_PTR(pvTmp), rc = VERR_NO_MEMORY);

        uint64_t cbFile;
        rc = vdiFileGetSize(pImage, &cbFile);
        AssertRCBreak(rc);
        unsigned cBlocksAllocated = (unsigned)((cbFile - pImage->offStartData - pImage->offStartBlockData) >> pImage->uShiftOffset2Index);
        if (cBlocksAllocated == 0)
        {
            /* No data blocks in this image, no need to compact. */
            rc = VINF_SUCCESS;
            break;
        }

        /* Allocate block array for back resolving. */
        paBlocks2 = (unsigned *)RTMemAlloc(sizeof(unsigned *) * cBlocksAllocated);
        AssertBreakStmt(VALID_PTR(paBlocks2), rc = VERR_NO_MEMORY);
        /* Fill out back resolving, check/fix allocation errors before
         * compacting the image, just to be on the safe side. Update the
         * image contents straight away, as this enables cancelling. */
        for (unsigned i = 0; i < cBlocksAllocated; i++)
            paBlocks2[i] = VDI_IMAGE_BLOCK_FREE;
        rc = VINF_SUCCESS;
        for (unsigned i = 0; i < cBlocks; i++)
        {
            VDIIMAGEBLOCKPOINTER ptrBlock = pImage->paBlocks[i];
            if (IS_VDI_IMAGE_BLOCK_ALLOCATED(ptrBlock))
            {
                if (ptrBlock < cBlocksAllocated)
                {
                    if (paBlocks2[ptrBlock] == VDI_IMAGE_BLOCK_FREE)
                        paBlocks2[ptrBlock] = i;
                    else
                    {
                        LogFunc(("Freed cross-linked block %u in file \"%s\"\n",
                                 i, pImage->pszFilename));
                        pImage->paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
                        rc = vdiUpdateBlockInfo(pImage, i);
                        if (RT_FAILURE(rc))
                            break;
                    }
                }
                else
                {
                    LogFunc(("Freed out of bounds reference for block %u in file \"%s\"\n",
                             i, pImage->pszFilename));
                    pImage->paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
                    rc = vdiUpdateBlockInfo(pImage, i);
                    if (RT_FAILURE(rc))
                        break;
                }
            }
        }
        if (RT_FAILURE(rc))
            break;

        /* Find redundant information and update the block pointers
         * accordingly, creating bubbles. Keep disk up to date, as this
         * enables cancelling. */
        for (unsigned i = 0; i < cBlocks; i++)
        {
            VDIIMAGEBLOCKPOINTER ptrBlock = pImage->paBlocks[i];
            if (IS_VDI_IMAGE_BLOCK_ALLOCATED(ptrBlock))
            {
                /* Block present in image file, read relevant data. */
                uint64_t u64Offset = (uint64_t)ptrBlock * pImage->cbTotalBlockData
                                   + (pImage->offStartData + pImage->offStartBlockData);
                rc = vdiFileReadSync(pImage, u64Offset, pvTmp, cbBlock, NULL);
                if (RT_FAILURE(rc))
                    break;

                if (ASMBitFirstSet((volatile void *)pvTmp, (uint32_t)cbBlock * 8) == -1)
                {
                    pImage->paBlocks[i] = VDI_IMAGE_BLOCK_ZERO;
                    rc = vdiUpdateBlockInfo(pImage, i);
                    if (RT_FAILURE(rc))
                        break;
                    paBlocks2[ptrBlock] = VDI_IMAGE_BLOCK_FREE;
                    /* Adjust progress info, one block to be relocated. */
                    cBlocksToMove++;
                }
                else if (pfnParentRead)
                {
                    rc = pfnParentRead(pvParent, (uint64_t)i * cbBlock, pvBuf, cbBlock);
                    if (RT_FAILURE(rc))
                        break;
                    if (!memcmp(pvTmp, pvBuf, cbBlock))
                    {
                        pImage->paBlocks[i] = VDI_IMAGE_BLOCK_FREE;
                        rc = vdiUpdateBlockInfo(pImage, i);
                        if (RT_FAILURE(rc))
                            break;
                        paBlocks2[ptrBlock] = VDI_IMAGE_BLOCK_FREE;
                        /* Adjust progress info, one block to be relocated. */
                        cBlocksToMove++;
                    }
                }
            }

            if (pCbProgress && pCbProgress->pfnProgress)
            {
                rc = pCbProgress->pfnProgress(pIfProgress->pvUser,
                                              (uint64_t)i * uPercentSpan / (cBlocks + cBlocksToMove) + uPercentStart);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        if (RT_FAILURE(rc))
            break;

        /* Fill bubbles with other data (if available). */
        unsigned cBlocksMoved = 0;
        unsigned uBlockUsedPos = cBlocksAllocated;
        for (unsigned i = 0; i < cBlocksAllocated; i++)
        {
            unsigned uBlock = paBlocks2[i];
            if (uBlock == VDI_IMAGE_BLOCK_FREE)
            {
                unsigned uBlockData = VDI_IMAGE_BLOCK_FREE;
                while (uBlockUsedPos > i && uBlockData == VDI_IMAGE_BLOCK_FREE)
                {
                    uBlockUsedPos--;
                    uBlockData = paBlocks2[uBlockUsedPos];
                }
                /* Terminate early if there is no block which needs copying. */
                if (uBlockUsedPos == i)
                    break;
                uint64_t u64Offset = (uint64_t)uBlockUsedPos * pImage->cbTotalBlockData
                                   + (pImage->offStartData + pImage->offStartBlockData);
                rc = vdiFileReadSync(pImage, u64Offset, pvTmp, cbBlock, NULL);
                u64Offset = (uint64_t)i * pImage->cbTotalBlockData
                          + (pImage->offStartData + pImage->offStartBlockData);
                rc = vdiFileWriteSync(pImage, u64Offset, pvTmp, cbBlock, NULL);
                pImage->paBlocks[uBlockData] = i;
                setImageBlocksAllocated(&pImage->Header, cBlocksAllocated - cBlocksMoved);
                rc = vdiUpdateBlockInfo(pImage, uBlockData);
                if (RT_FAILURE(rc))
                    break;
                paBlocks2[i] = uBlockData;
                paBlocks2[uBlockUsedPos] = VDI_IMAGE_BLOCK_FREE;
                cBlocksMoved++;
            }

            if (pCbProgress && pCbProgress->pfnProgress)
            {
                rc = pCbProgress->pfnProgress(pIfProgress->pvUser,
                                              (uint64_t)(cBlocks + cBlocksMoved) * uPercentSpan / (cBlocks + cBlocksToMove) + uPercentStart);

                if (RT_FAILURE(rc))
                    break;
            }
        }
        if (RT_FAILURE(rc))
            break;

        /* Update image header. */
        setImageBlocksAllocated(&pImage->Header, uBlockUsedPos);
        vdiUpdateHeader(pImage);

        /* Truncate the image to the proper size to finish compacting. */
        rc = vdiFileSetSize(pImage,
                              (uint64_t)uBlockUsedPos * pImage->cbTotalBlockData
                            + pImage->offStartData + pImage->offStartBlockData);
    } while (0);

    if (paBlocks2)
        RTMemTmpFree(paBlocks2);
    if (pvTmp)
        RTMemTmpFree(pvTmp);
    if (pvBuf)
        RTMemTmpFree(pvBuf);

    if (RT_SUCCESS(rc) && pCbProgress && pCbProgress->pfnProgress)
    {
        pCbProgress->pfnProgress(pIfProgress->pvUser,
                                 uPercentStart + uPercentSpan);
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


/** @copydoc VBOXHDDBACKEND::pfnResize */
static int vdiResize(void *pBackendData, uint64_t cbSize,
                     PCVDGEOMETRY pPCHSGeometry, PCVDGEOMETRY pLCHSGeometry,
                     unsigned uPercentStart, unsigned uPercentSpan,
                     PVDINTERFACE pVDIfsDisk, PVDINTERFACE pVDIfsImage,
                     PVDINTERFACE pVDIfsOperation)
{
    PVDIIMAGEDESC pImage = (PVDIIMAGEDESC)pBackendData;
    int rc = VINF_SUCCESS;

    PFNVDPROGRESS pfnProgress = NULL;
    void *pvUser = NULL;
    PVDINTERFACE pIfProgress = VDInterfaceGet(pVDIfsOperation,
                                              VDINTERFACETYPE_PROGRESS);
    PVDINTERFACEPROGRESS pCbProgress = NULL;
    if (pIfProgress)
    {
        pCbProgress = VDGetInterfaceProgress(pIfProgress);
        if (pCbProgress)
            pfnProgress = pCbProgress->pfnProgress;
        pvUser = pIfProgress->pvUser;
    }

    /*
     * Making the image smaller is not supported at the moment.
     * Resizing is also not supported for fixed size images and
     * very old images.
     */
    /** @todo implement making the image smaller, it is the responsibility of
     * the user to know what he's doing. */
    if (   cbSize < getImageDiskSize(&pImage->Header)
        || GET_MAJOR_HEADER_VERSION(&pImage->Header) == 0
        || pImage->uImageFlags & VD_IMAGE_FLAGS_FIXED)
        rc = VERR_NOT_SUPPORTED;
    else if (cbSize > getImageDiskSize(&pImage->Header))
    {
        unsigned cBlocksAllocated = getImageBlocksAllocated(&pImage->Header); /** < Blocks currently allocated, doesn't change during resize */
        uint32_t cBlocksNew = cbSize / getImageBlockSize(&pImage->Header);    /** < New number of blocks in the image after the resize */
        if (cbSize % getImageBlockSize(&pImage->Header))
            cBlocksNew++;

        uint32_t cBlocksOld      = getImageBlocks(&pImage->Header);           /** < Number of blocks before the resize. */
        uint64_t cbBlockspaceNew = cBlocksNew * sizeof(VDIIMAGEBLOCKPOINTER); /** < Required space for the block array after the resize. */
        uint64_t offStartDataNew = RT_ALIGN_32(pImage->offStartBlocks + cbBlockspaceNew, VDI_DATA_ALIGN); /** < New start offset for block data after the resize */

        if (   pImage->offStartData != offStartDataNew
            && cBlocksAllocated > 0)
        {
            /* Calculate how many sectors need to be relocated. */
            uint64_t cbOverlapping = offStartDataNew - pImage->offStartData;
            unsigned cBlocksReloc = cbOverlapping / getImageBlockSize(&pImage->Header);
            if (cbOverlapping % getImageBlockSize(&pImage->Header))
                cBlocksReloc++;

            /* Since only full blocks can be relocated the new data start is
             * determined by moving it block by block. */
            cBlocksReloc = RT_MIN(cBlocksReloc, cBlocksAllocated);
            offStartDataNew = pImage->offStartData;

            /* Do the relocation. */
            LogFlow(("Relocating %u blocks\n", cBlocksReloc));

            /*
             * Get the blocks we need to relocate first, they are appended to the end
             * of the image.
             */
            void *pvBuf = NULL, *pvZero = NULL;
            do
            {
                VDIIMAGEBLOCKPOINTER uBlock = 0;

                /* Allocate data buffer. */
                pvBuf = RTMemAllocZ(pImage->cbTotalBlockData);
                if (!pvBuf)
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }

                /* Allocate buffer for overwriting with zeroes. */
                pvZero = RTMemAllocZ(pImage->cbTotalBlockData);
                if (!pvZero)
                {
                    rc = VERR_NO_MEMORY;
                    break;
                }

                for (unsigned i = 0; i < cBlocksReloc; i++)
                {
                    /* Search the index in the block table. */
                    for (unsigned idxBlock = 0; idxBlock < cBlocksOld; idxBlock++)
                    {
                        if (pImage->paBlocks[idxBlock] == uBlock)
                        {
                            /* Read data and append to the end of the image. */
                            rc = vdiFileReadSync(pImage, offStartDataNew, pvBuf, pImage->cbTotalBlockData, NULL);
                            if (RT_FAILURE(rc))
                                break;

                            uint64_t offBlockAppend;
                            rc = vdiFileGetSize(pImage, &offBlockAppend);
                            if (RT_FAILURE(rc))
                                break;

                            rc = vdiFileWriteSync(pImage, offBlockAppend, pvBuf, pImage->cbTotalBlockData, NULL);
                            if (RT_FAILURE(rc))
                                break;

                            /* Zero out the old block area. */
                            rc = vdiFileWriteSync(pImage, offStartDataNew, pvZero, pImage->cbTotalBlockData, NULL);
                            if (RT_FAILURE(rc))
                                break;

                            /* Update block counter. */
                            pImage->paBlocks[idxBlock] = cBlocksAllocated - 1;

                            /*
                             * Decrease the block number of all other entries in the array.
                             * They were moved one block to the front.
                             * Doing it as a separate step iterating over the array again
                             * because an error while relocating the block might end up
                             * in a corrupted image otherwise.
                             */
                            for (unsigned idxBlock2 = 0; idxBlock2 < cBlocksOld; idxBlock2++)
                            {
                                if (   idxBlock2 != idxBlock
                                    && IS_VDI_IMAGE_BLOCK_ALLOCATED(pImage->paBlocks[idxBlock2]))
                                    pImage->paBlocks[idxBlock2]--;
                            }

                            /* Continue with the next block. */
                            break;
                        }
                    }

                    if (RT_FAILURE(rc))
                        break;

                    uBlock++;
                    offStartDataNew += pImage->cbTotalBlockData;
                }
            } while (0);

            if (pvBuf)
                RTMemFree(pvBuf);
            if (pvZero)
                RTMemFree(pvZero);
        }

        /*
         * We need to update the new offsets for the image data in the out of memory
         * case too because we relocated the blocks already.
         */
        pImage->offStartData = offStartDataNew;
        setImageDataOffset(&pImage->Header, offStartDataNew);

        /*
         * Relocation done, expand the block array and update the header with
         * the new data.
         */
        if (RT_SUCCESS(rc))
        {
            PVDIIMAGEBLOCKPOINTER paBlocksNew = (PVDIIMAGEBLOCKPOINTER)RTMemRealloc(pImage->paBlocks, cbBlockspaceNew);
            if (paBlocksNew)
            {
                pImage->paBlocks = paBlocksNew;

                /* Mark the new blocks as unallocated. */
                for (unsigned idxBlock = cBlocksOld; idxBlock < cBlocksNew; idxBlock++)
                    pImage->paBlocks[idxBlock] = VDI_IMAGE_BLOCK_FREE;
            }
            else
                rc = VERR_NO_MEMORY;

            /* Write the block array before updating the rest. */
            rc = vdiFileWriteSync(pImage, pImage->offStartBlocks, pImage->paBlocks,
                                  cbBlockspaceNew, NULL);

            if (RT_SUCCESS(rc))
            {
                /* Update size and new block count. */
                setImageDiskSize(&pImage->Header, cbSize);
                setImageBlocks(&pImage->Header, cBlocksNew);
                /* Update geometry. */
                pImage->PCHSGeometry = *pPCHSGeometry;

                PVDIDISKGEOMETRY pGeometry = getImageLCHSGeometry(&pImage->Header);
                if (pGeometry)
                {
                    pGeometry->cCylinders = pLCHSGeometry->cCylinders;
                    pGeometry->cHeads = pLCHSGeometry->cHeads;
                    pGeometry->cSectors = pLCHSGeometry->cSectors;
                    pGeometry->cbSector = VDI_GEOMETRY_SECTOR_SIZE;
                }
            }
        }

        /* Update header information in base image file. */
        vdiFlushImage(pImage);
    }
    /* Same size doesn't change the image at all. */

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}


VBOXHDDBACKEND g_VDIBackend =
{
    /* pszBackendName */
    "VDI",
    /* cbSize */
    sizeof(VBOXHDDBACKEND),
    /* uBackendCaps */
      VD_CAP_UUID | VD_CAP_CREATE_FIXED | VD_CAP_CREATE_DYNAMIC
    | VD_CAP_DIFF | VD_CAP_FILE | VD_CAP_ASYNC | VD_CAP_VFS,
    /* paFileExtensions */
    s_aVdiFileExtensions,
    /* paConfigInfo */
    NULL,
    /* hPlugin */
    NIL_RTLDRMOD,
    /* pfnCheckIfValid */
    vdiCheckIfValid,
    /* pfnOpen */
    vdiOpen,
    /* pfnCreate */
    vdiCreate,
    /* pfnRename */
    vdiRename,
    /* pfnClose */
    vdiClose,
    /* pfnRead */
    vdiRead,
    /* pfnWrite */
    vdiWrite,
    /* pfnFlush */
    vdiFlush,
    /* pfnGetVersion */
    vdiGetVersion,
    /* pfnGetSize */
    vdiGetSize,
    /* pfnGetFileSize */
    vdiGetFileSize,
    /* pfnGetPCHSGeometry */
    vdiGetPCHSGeometry,
    /* pfnSetPCHSGeometry */
    vdiSetPCHSGeometry,
    /* pfnGetLCHSGeometry */
    vdiGetLCHSGeometry,
    /* pfnSetLCHSGeometry */
    vdiSetLCHSGeometry,
    /* pfnGetImageFlags */
    vdiGetImageFlags,
    /* pfnGetOpenFlags */
    vdiGetOpenFlags,
    /* pfnSetOpenFlags */
    vdiSetOpenFlags,
    /* pfnGetComment */
    vdiGetComment,
    /* pfnSetComment */
    vdiSetComment,
    /* pfnGetUuid */
    vdiGetUuid,
    /* pfnSetUuid */
    vdiSetUuid,
    /* pfnGetModificationUuid */
    vdiGetModificationUuid,
    /* pfnSetModificationUuid */
    vdiSetModificationUuid,
    /* pfnGetParentUuid */
    vdiGetParentUuid,
    /* pfnSetParentUuid */
    vdiSetParentUuid,
    /* pfnGetParentModificationUuid */
    vdiGetParentModificationUuid,
    /* pfnSetParentModificationUuid */
    vdiSetParentModificationUuid,
    /* pfnDump */
    vdiDump,
    /* pfnGetTimeStamp */
    NULL,
    /* pfnGetParentTimeStamp */
    NULL,
    /* pfnSetParentTimeStamp */
    NULL,
    /* pfnGetParentFilename */
    NULL,
    /* pfnSetParentFilename */
    NULL,
    /* pfnAsyncRead */
    vdiAsyncRead,
    /* pfnAsyncWrite */
    vdiAsyncWrite,
    /* pfnAsyncFlush */
    vdiAsyncFlush,
    /* pfnComposeLocation */
    genericFileComposeLocation,
    /* pfnComposeName */
    genericFileComposeName,
    /* pfnCompact */
    vdiCompact,
    /* pfnResize */
    vdiResize
};
