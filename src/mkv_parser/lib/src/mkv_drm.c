/*
***********************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifdef SUPPORT_MKV_DRM

#include "mkv_drm.h"
#include <string.h>
#include "matroska.h"
// #include "drm_common.h"

HANDLE fsl_osal_dll_open(const char* filename);
void* fsl_osal_dll_symbol(HANDLE handle, const char* symbol);
int32 fsl_osal_dll_close(HANDLE handle);

#if defined(__WINCE) || defined(WIN32)
void asciiToUnicodeString(const uint8* src, WCHAR* des) {
    uint32 size;
    uint32 i;

    size = (uint32)strlen(src);

    for (i = 0; i < size; i++) {
        des[i] = (uint32)src[i];
    }
    des[i] = 0;
}
#endif

HANDLE fsl_osal_dll_open(const char* filename) {
#if defined(__WINCE) || defined(WIN32)
    WCHAR wstrFileName[255];
    HMODULE handle = NULL;

    asciiToUnicodeString(filename, wstrFileName);
    handle = LoadLibrary(wstrFileName); /* TEXT("MyPuts.dll") need unicode string */

#else
    void* handle = NULL;
    handle = dlopen(filename, RTLD_NOW);
    if (NULL == handle) {
        DRMMSG("%s\n", dlerror());
    }
#endif

    return (HANDLE)handle;
}

/* returns 0 on success, and non-zero on error */
int32 fsl_osal_dll_close(HANDLE handle) {
    int32 ret;

#if defined(__WINCE) || defined(WIN32)
    bool err;

    err = FreeLibrary((HMODULE)handle);
    if (err)
        ret = 0; /* WINCE use TRUE on success */
    else
        ret = -1;

#else
    ret = dlclose((void*)handle);
#endif
    return ret;
}

void* fsl_osal_dll_symbol(HANDLE handle, const char* symbol) {
    void* addr = NULL;

#if defined(__WINCE)
    WCHAR wstrSymbol[255];
    asciiToUnicodeString(symbol, wstrSymbol);
    addr = (void*)GetProcAddress((HMODULE)handle, wstrSymbol);

#elif defined(WIN32)
    addr = (void*)GetProcAddress((HMODULE)handle, symbol);

#else
    addr = dlsym((void*)handle, symbol);
#endif

    return addr;
}

#if defined(__WINCE)
char sDrmLibName[] = "lib_divx_drm_arm11_wince.dll";
#elif defined(WIN32)
char sDrmLibName[] = "lib_divx_drm.dll";
#else
char sDrmLibName[] = "lib_divx_drm_arm11_elinux.so";
#endif

bool LoadDrmLibrary(MKVReaderContext* pParserObj) {
    bool ret = FALSE;
    HANDLE hDrmLib = NULL;

    if (NULL == pParserObj)
        goto bail;

    pParserObj->bHasDrmLib = FALSE;
    pParserObj->hDRMLib = NULL;
    hDrmLib = fsl_osal_dll_open(sDrmLibName);
    if (NULL == hDrmLib) {
        DRMMSG("can not open DRM library ! \n");
        goto bail;
    }

    pParserObj->bHasDrmLib = TRUE;
    // start load API
    pParserObj->sDrmAPI.drmInitSystem = fsl_osal_dll_symbol(hDrmLib, "drmInitSystem");
    pParserObj->sDrmAPI.drmInitPlayback = fsl_osal_dll_symbol(hDrmLib, "drmInitPlayback");

    pParserObj->sDrmAPI.drmQueryRentalStatus = fsl_osal_dll_symbol(hDrmLib, "drmQueryRentalStatus");
    pParserObj->sDrmAPI.drmQueryCgmsa = fsl_osal_dll_symbol(hDrmLib, "drmQueryCgmsa");
    pParserObj->sDrmAPI.drmQueryAcptb = fsl_osal_dll_symbol(hDrmLib, "drmQueryAcptb");
    pParserObj->sDrmAPI.drmQueryDigitalProtection =
            fsl_osal_dll_symbol(hDrmLib, "drmQueryDigitalProtection");
    pParserObj->sDrmAPI.drmQueryIct = fsl_osal_dll_symbol(hDrmLib, "drmQueryIct");

    pParserObj->sDrmAPI.drmCommitPlayback = fsl_osal_dll_symbol(hDrmLib, "drmCommitPlayback");
    pParserObj->sDrmAPI.drmFinalizePlayback = fsl_osal_dll_symbol(hDrmLib, "drmFinalizePlayback");
    pParserObj->sDrmAPI.drmDecryptVideo = fsl_osal_dll_symbol(hDrmLib, "drmDecryptVideo");
    pParserObj->sDrmAPI.drmDecryptAudio = fsl_osal_dll_symbol(hDrmLib, "drmDecryptAudio");
    pParserObj->sDrmAPI.drmGetLastError = fsl_osal_dll_symbol(hDrmLib, "drmGetLastError");
    pParserObj->sDrmAPI.drmSetRandomSample = fsl_osal_dll_symbol(hDrmLib, "drmSetRandomSample");
    if ((NULL == pParserObj->sDrmAPI.drmInitSystem) ||
        (NULL == pParserObj->sDrmAPI.drmInitPlayback) ||
        (NULL == pParserObj->sDrmAPI.drmQueryRentalStatus) ||
        (NULL == pParserObj->sDrmAPI.drmQueryCgmsa) ||
        (NULL == pParserObj->sDrmAPI.drmQueryDigitalProtection) ||
        (NULL == pParserObj->sDrmAPI.drmQueryIct) ||
        (NULL == pParserObj->sDrmAPI.drmCommitPlayback) ||
        (NULL == pParserObj->sDrmAPI.drmFinalizePlayback) ||
        (NULL == pParserObj->sDrmAPI.drmDecryptVideo) ||
        (NULL == pParserObj->sDrmAPI.drmDecryptAudio) ||
        (NULL == pParserObj->sDrmAPI.drmGetLastError) ||
        (NULL == pParserObj->sDrmAPI.drmSetRandomSample)) {
        DRMMSG("Get DRM API failed \n");
        fsl_osal_dll_close(hDrmLib);
        pParserObj->bHasDrmLib = FALSE;
        pParserObj->hDRMLib = NULL;
        memset((void*)(&pParserObj->sDrmAPI), 0, sizeof(drmAPI_s));
        ret = FALSE;
    } else {
        DRMMSG("Get API functions successfully!\n");
        ret = TRUE;
        pParserObj->hDRMLib = hDrmLib;
    }

bail:
    return ret;
}

bool UnloadDrmLibrary(MKVReaderContext* pParserObj) {
    bool ret = TRUE;

    if ((NULL == pParserObj) || (NULL == pParserObj->hDRMLib)) {
        ret = FALSE;
        goto bail;
    }

    fsl_osal_dll_close(pParserObj->hDRMLib);
    pParserObj->bHasDrmLib = FALSE;
    pParserObj->hDRMLib = NULL;
    memset((void*)(&pParserObj->sDrmAPI), 0, sizeof(drmAPI_s));

bail:
    return ret;
}

#endif  // SUPPORT_MKV_DRM