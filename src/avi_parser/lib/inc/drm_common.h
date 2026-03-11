/*
***********************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef __DRM_COMMON_H__
#define __DRM_COMMON_H__

#include "DrmApi.h"

typedef struct {
    drmErrorCodes_t (*drmInitSystem)(uint8_t* drmContext, uint32_t* drmContextLength);
    drmErrorCodes_t (*drmInitPlayback)(uint8_t* drmContext, uint8_t* strdData);

    drmErrorCodes_t (*drmQueryRentalStatus)(uint8_t* drmContext, uint8_t* rentalMessageFlag,
                                            uint8_t* useLimit, uint8_t* useCount);

    drmErrorCodes_t (*drmQueryCgmsa)(uint8_t* drmContext, uint8_t* cgmsaSignal);
    drmErrorCodes_t (*drmQueryAcptb)(uint8_t* drmContext, uint8_t* acptbSignal);
    drmErrorCodes_t (*drmQueryDigitalProtection)(uint8_t* drmContext,
                                                 uint8_t* digitalProtectionSignal);
    drmErrorCodes_t (*drmQueryIct)(uint8_t* drmContext, uint8_t* ict);

    drmErrorCodes_t (*drmCommitPlayback)(uint8_t* drmContext);
    drmErrorCodes_t (*drmFinalizePlayback)(uint8_t* drmContext);
    drmErrorCodes_t (*drmDecryptVideo)(uint8_t* drmContext, uint8_t* frame, uint32_t frameSize,
                                       uint8_t* drmFrameInfo);

    drmErrorCodes_t (*drmDecryptAudio)(uint8_t* drmContext, uint8_t* frame, uint32_t frameSize);

    drmErrorCodes_t (*drmGetLastError)(uint8_t* drmContext);
    drmErrorCodes_t (*drmSetRandomSample)(uint8_t* drmContext);
} drmAPI_s;

#endif  // __DRM_COMMON_H__