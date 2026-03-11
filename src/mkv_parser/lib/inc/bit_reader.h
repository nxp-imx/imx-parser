/*
 * Copyright 2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _BIT_READER_H_
#define _BIT_READER_H_

#include "fsl_types.h"

typedef struct _BitReader {
    const uint8* data;
    uint32 size;
    uint32 bitPos;   // reading position of bit in current byte: 0 ~ 7
    uint32 bytePos;  // reading position of current byte in data: 0 ~ size-1

    // interfaces
    uint32 (*getBits)(struct _BitReader* this, uint32 n);
    bool (*skipBits)(struct _BitReader* this, uint32 n);
    uint32 (*numBitsLeft)(struct _BitReader* this);
} BitReader;

void BitReaderInit(BitReader* this, const uint8* data, uint32 size);

#endif  // _BIT_READER_H_
