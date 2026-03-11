/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef MPEG2_EPSON_EXVI_H
#define MPEG2_EPSON_EXVI_H

#include "fsl_datatype.h"
#include "fsl_types.h"

typedef struct {
    uint32 vWidth;
    uint32 vHeight;
    bool isKeyFrame;
    uint32 frameNumerator;
    uint32 frameDemoninator;
    uint32 PESLength;
    bool isParsed;
} EPSON_EXVI;

bool EPSON_ReadEXVI(unsigned char* pAddr, uint32 length,
                    EPSON_EXVI* pEXVI /* PES_private_data address */);

#endif
