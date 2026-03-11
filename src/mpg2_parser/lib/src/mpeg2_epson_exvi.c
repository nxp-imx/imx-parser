/*
***********************************************************************
* Copyright (c) 2011-2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include "mpeg2_epson_exvi.h"

#define PES_MAX_HEADER_LEN 24

bool EPSON_ReadEXVI(unsigned char* pAddr, uint32 length,
                    EPSON_EXVI* pEXVI /* PES_private_data address */) {
    unsigned short usWidth;
    unsigned short usHeight;
    bool byKeyFrame;
    unsigned short usTimeIncrement;
    unsigned short usTimeResolution;
    unsigned int uiPES_Length;
    unsigned int len = 0;

    while (len <= length - 16) {
        if (len >= PES_MAX_HEADER_LEN)
            return FALSE;
        if (!(('E' == pAddr[0]) && ('X' == pAddr[1]) && ('V' == pAddr[2]) && ('I' == pAddr[3]))) {
            len++;
            pAddr++;
        } else
            break;
    }
    if (len > length - 16)
        return FALSE;

    usWidth = ((unsigned short)pAddr[4] << 5) | (unsigned short)(pAddr[5] & 0xFC) >> 3;

    usHeight = ((unsigned short)(pAddr[5] & 0x03) << 11) | (unsigned short)pAddr[6] << 3 |
               ((unsigned short)(pAddr[7] & 0xE0) >> 5);

    byKeyFrame = 0x10 == (pAddr[7] & 0x10);

    usTimeResolution =
            ((unsigned short)(pAddr[7] & 0x07) << 13) | (unsigned short)(pAddr[8] & 0xF8) << 5 |
            ((unsigned short)(pAddr[8] & 0x03) << 6) | ((unsigned short)(pAddr[9] & 0xFC) >> 2);

    usTimeIncrement = ((unsigned short)(pAddr[9] & 0x01) << 9) | (unsigned short)pAddr[10] << 1 |
                      ((unsigned short)(pAddr[11] & 0x80) >> 7);

    uiPES_Length = ((unsigned int)(pAddr[11] & 0x3F) << 24) | ((unsigned int)pAddr[12] << 16) |
                   ((unsigned int)(pAddr[13] & 0x80) << 8) |
                   ((unsigned int)(pAddr[13] & 0x3F) << 9) | ((unsigned int)pAddr[14] << 1) |
                   ((unsigned int)(pAddr[15] & 0x80) >> 7);

    pEXVI->vWidth = usWidth;
    pEXVI->vHeight = usHeight;
    pEXVI->frameNumerator = usTimeResolution;
    pEXVI->frameDemoninator = usTimeIncrement;
    pEXVI->PESLength = uiPES_Length;
    pEXVI->isKeyFrame = byKeyFrame;

    return TRUE;
}
