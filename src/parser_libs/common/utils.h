/*
***********************************************************************
* Copyright (c) 2015, Freescale Semiconductor, Inc.
* Copyright 2017, 2020, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsl_types.h"

#ifndef ANDROID
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef short int16_t;
typedef int int32_t;
#if defined __WINCE || defined WIN32
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
typedef char int8_t;
#else
// remove typedef of uint64_t and int64_t
// got them from head file stdint.h
#include <stdint.h>
#endif
#endif

#ifdef INT_MAX
#undef INT_MAX
#endif
#define INT_MAX 0x7FFFFFFF

#ifdef INT_MIN
#undef INT_MIN
#endif
#define INT_MIN 0x80000000

#define MIN_CACHE_BITS 25

static const int8_t SE_GOLOMB_VLC_CODE[512] = {
        16,  17, 17,  17, 17,  17, 17,  17, 17,  17, 17,  17, 17, 17, 17, 17, 8,  -8, 9,  -9, 10,
        -10, 11, -11, 12, -12, 13, -13, 14, -14, 15, -15, 4,  4,  4,  4,  -4, -4, -4, -4, 5,  5,
        5,   5,  -5,  -5, -5,  -5, 6,   6,  6,   6,  -6,  -6, -6, -6, 7,  7,  7,  7,  -7, -7, -7,
        -7,  2,  2,   2,  2,   2,  2,   2,  2,   2,  2,   2,  2,  2,  2,  2,  2,  -2, -2, -2, -2,
        -2,  -2, -2,  -2, -2,  -2, -2,  -2, -2,  -2, -2,  -2, 3,  3,  3,  3,  3,  3,  3,  3,  3,
        3,   3,  3,   3,  3,   3,  3,   -3, -3,  -3, -3,  -3, -3, -3, -3, -3, -3, -3, -3, -3, -3,
        -3,  -3, 1,   1,  1,   1,  1,   1,  1,   1,  1,   1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,   1,  1,   1,  1,   1,  1,   1,  1,   1,  1,   1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,   1,  1,   1,  1,   1,  1,   1,  1,   1,  1,   1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,   1,  1,   -1, -1,  -1, -1,  -1, -1,  -1, -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1,  -1, -1,  -1, -1,  -1, -1,  -1, -1,  -1, -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1,  -1, -1,  -1, -1,  -1, -1,  -1, -1,  -1, -1,  -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1,  -1, -1,  -1, 0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,   0,  0,   0,  0,   0,  0,   0,
};

static const uint8_t UE_GOLOMB_VLC_CODE[512] = {
        31, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 32, 15, 16, 17, 18, 19, 20, 21,
        22, 23, 24, 25, 26, 27, 28, 29, 30, 7,  7,  7,  7,  8,  8,  8,  8,  9,  9,  9,  9,  10, 10,
        10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 3,  3,  3,  3,  3,
        3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,  4,
        4,  4,  4,  4,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  6,  6,  6,
        6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  6,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
        1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
        2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
        2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,  2,
        2,  2,  2,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
        0,  0,  0,  0,  0,  0};

static const uint8_t GOLOMB_VLC_LEN[512] = {
        14, 13, 12, 12, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9, 9, 9, 9, 9, 9, 9,
        9,  9,  9,  9,  9,  9,  9,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7, 7, 7, 7, 7, 7, 7, 7, 7,
        7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  7,  5,  5,  5, 5, 5, 5, 5, 5, 5, 5, 5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5, 5, 5, 5, 5, 5, 5, 5, 5,
        5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5,  5, 5, 5, 5, 5, 5, 5, 5, 5,
        5,  5,  5,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, 3, 3, 3, 3, 3, 3, 3, 3,
        3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, 3, 3, 3, 3, 3, 3, 3, 3,
        3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, 3, 3, 3, 3, 3, 3, 3, 3,
        3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, 3, 3, 3, 3, 3, 3, 3, 3,
        3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3, 3, 3, 3, 3, 3, 3, 3, 3,
        3,  3,  3,  3,  3,  3,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1,  1};

static const uint8_t DEFAULT_SCALING8[2][64] = {
        {6,  10, 13, 16, 18, 23, 25, 27, 10, 11, 16, 18, 23, 25, 27, 29, 13, 16, 18, 23, 25, 27,
         29, 31, 16, 18, 23, 25, 27, 29, 31, 33, 18, 23, 25, 27, 29, 31, 33, 36, 23, 25, 27, 29,
         31, 33, 36, 38, 25, 27, 29, 31, 33, 36, 38, 40, 27, 29, 31, 33, 36, 38, 40, 42},
        {9,  13, 15, 17, 19, 21, 22, 24, 13, 13, 17, 19, 21, 22, 24, 25, 15, 17, 19, 21, 22, 24,
         25, 27, 17, 19, 21, 22, 24, 25, 27, 28, 19, 21, 22, 24, 25, 27, 28, 30, 21, 22, 24, 25,
         27, 28, 30, 32, 22, 24, 25, 27, 28, 30, 32, 33, 24, 25, 27, 28, 30, 32, 33, 35}};

static const uint8_t DEFAULT_SCALING4[2][16] = {
        {6, 13, 20, 28, 13, 20, 28, 32, 20, 28, 32, 37, 28, 32, 37, 42},
        {10, 14, 20, 24, 14, 20, 24, 27, 20, 24, 27, 30, 24, 27, 30, 34}};

static const uint8_t ZIGZAG_SCAN[16] = {
        0 + 0 * 4, 1 + 0 * 4, 0 + 1 * 4, 0 + 2 * 4, 1 + 1 * 4, 2 + 0 * 4, 3 + 0 * 4, 2 + 1 * 4,
        1 + 2 * 4, 0 + 3 * 4, 1 + 3 * 4, 2 + 2 * 4, 3 + 1 * 4, 3 + 2 * 4, 2 + 3 * 4, 3 + 3 * 4,
};

static const uint8_t ZIGZAG_DIRECT[64] = {
        0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
        41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
        30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

#define READ32(a)                                                          \
    ((((const uint8_t*)(a))[0] << 24) | (((const uint8_t*)(a))[1] << 16) | \
     (((const uint8_t*)(a))[2] << 8) | ((const uint8_t*)(a))[3])

typedef struct GetBitContext {
    const uint8_t *buffer, *buffer_end;
    int index;
    int size_in_bits;
} GetBitContext;

typedef struct ParserContext {
    void* priv_data;
    uint8* buf;
    uint32 buf_size;
    uint32 index;
    uint32 state;
    int frame_start_found;
} ParserContext;

static __inline uint32_t clz(uint32_t x) {
    uint32 cnt = 0;
    for (; x > 0; x >>= 1) {
        cnt++;
    }
    return 32 - cnt;
}

#define av_log2(x) (31 - clz((x) | 1))

static __inline unsigned int get_cache(GetBitContext* s) {
    return READ32(s->buffer + (s->index >> 3)) << (s->index & 7);
}

static __inline int get_bits_count(const GetBitContext* s) {
    return s->index;
}

static __inline int get_bits_left(const GetBitContext* s) {
    return (s->size_in_bits - s->index);
}

static __inline void skip_bits_long(GetBitContext* s, int n) {
    s->index += n;
}

static __inline void skip_nbits(GetBitContext* s, int n) {
    s->index += n;
}

static __inline unsigned int show_nbits(GetBitContext* s, int n) {
    uint32 tmp, cache;

    cache = get_cache(s);
    tmp = cache >> (32 - n);

    return tmp;
}

static __inline unsigned int get_nbits(GetBitContext* s, int n) {
    uint32 tmp;

    tmp = show_nbits(s, n);
    skip_nbits(s, n);

    return tmp;
}

static __inline unsigned int get_bits1(GetBitContext* s) {
    return get_nbits(s, 1);
}

static __inline unsigned int get_nbits_long(GetBitContext* s, int n) {
    int bit_size = 16;
    if (n <= 17)
        return get_nbits(s, n);
    else {
        int ret;
        ret = get_nbits(s, bit_size) << (n - bit_size);
        return ret | get_nbits(s, n - bit_size);
    }
}

static __inline unsigned int show_nbits_long(GetBitContext* s, int n) {
    if (n <= MIN_CACHE_BITS) {
        return show_nbits(s, n);
    } else {
        GetBitContext gb = *s;
        return get_nbits_long(&gb, n);
    }
}

static __inline void align_get_bits(GetBitContext* s) {
    int size = (-get_bits_count(s)) & 7;
    if (size)
        skip_nbits(s, size);
}

static __inline int init_get_bits(GetBitContext* s, const uint8_t* buffer, int bit_size) {
    int buffer_size;
    int ret = 0;

    if (bit_size > INT_MAX - 7 || bit_size < 0 || !buffer) {
        bit_size = 0;
        buffer = NULL;
        ret = -1;
    }

    buffer_size = (bit_size + 7) >> 3;

    s->buffer = buffer;
    s->buffer_end = buffer + buffer_size;
    s->size_in_bits = bit_size;
    s->index = 0;

    return ret;
}

static __inline int get_ue_golomb_31(GetBitContext* gb) {
    uint32 cache;

    cache = get_cache(gb);
    cache >>= 32 - 9;
    skip_nbits(gb, GOLOMB_VLC_LEN[cache]);

    return UE_GOLOMB_VLC_CODE[cache];
}

static __inline int get_ue_golomb(GetBitContext* gb) {
    uint32 cache;

    cache = get_cache(gb);

    if (cache >= (1 << 27)) {
        cache >>= 32 - 9;
        skip_nbits(gb, GOLOMB_VLC_LEN[cache]);
        return UE_GOLOMB_VLC_CODE[cache];
        ;
    } else {
        int log = (av_log2(cache) << 1) - 31;
        cache >>= log;
        cache--;
        skip_nbits(gb, 32 - log);

        return cache;
    }
}

static __inline unsigned get_ue_golomb_long(GetBitContext* gb) {
    unsigned log;
    uint32 cache;

    cache = get_cache(gb);
    log = 31 - av_log2(cache);
    skip_bits_long(gb, log);

    return get_nbits_long(gb, log + 1) - 1;
}

static __inline int get_se_golomb(GetBitContext* gb) {
    unsigned int cache;
    int log;

    cache = get_cache(gb);

    if (cache >= (1 << 27)) {
        cache >>= 32 - 9;
        skip_nbits(gb, GOLOMB_VLC_LEN[cache]);
        return SE_GOLOMB_VLC_CODE[cache];
    } else {
        log = (av_log2(cache) << 1) - 31;
        cache >>= log;
        skip_nbits(gb, 32 - log);

        if (cache & 1)
            cache = -(cache >> 1);
        else
            cache = (cache >> 1);

        return cache;
    }
}

typedef struct FrameInfo {
    int64 pts;
    uint32 data_size;
    uint32 alloc_size;
    uint8* buffer;
    uint32 flags;
} FrameInfo;

typedef enum {
    PARSER_HAS_ONE_FRAME = 1,
    PARSER_CORRUPTED_FRAME,
    PARSER_SMALL_OUTPUT_BUFFER,
    PARSER_ERROR,
} ParserRetCode;

#endif
/*EOF*/
