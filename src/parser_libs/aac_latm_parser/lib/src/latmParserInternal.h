/*
***********************************************************************
* Copyright (c) 2015, Freescale Semiconductor, Inc.
*
* Copyright 2024, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
#ifndef AAC_LATM_PARSER_INTERNAL_H
#define AAC_LATM_PARSER_INTERNAL_H

#include "fsl_types.h"
#include "fsl_parser.h"
#include "utils.h"

#define SEPARATOR " "

#define BASELINE_SHORT_NAME "BLN_MAD-MMLAYER_LATMPARSER_01.00.00"

#ifdef __WINCE
#define OS_NAME "_WINCE"
#else
#define OS_NAME ""
#endif

#ifdef DEMO_VERSION
#define CODEC_RELEASE_TYPE "_DEMO"
#else
#define CODEC_RELEASE_TYPE ""
#endif

/* user define suffix */
#define VERSION_STR_SUFFIX ""

#define CODEC_VERSION_STR                                                                  \
    (BASELINE_SHORT_NAME OS_NAME CODEC_RELEASE_TYPE SEPARATOR VERSION_STR_SUFFIX SEPARATOR \
     "build on" SEPARATOR __DATE__ SEPARATOR __TIME__)

const int AAC_LATM_SAMPLE_RATES[16] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                       22050, 16000, 12000, 11025, 8000,  7350};

const uint8_t AAC_LATM_CHANNELS[8] = {0, 1, 2, 3, 4, 5, 6, 8};

typedef enum {
    BLOCK_SCE,
    BLOCK_CPE,
    BLOCK_CCE,
    BLOCK_LFE,
    BLOCK_DSE,
    BLOCK_PCE,
    BLOCK_FIL,
    BLOCK_END,
} RAW_DATA_BLOCK_TYPE;

typedef enum {
    CHANNEL_NULL = 0,
    CHANNEL_FRONT,
    CHANNEL_BACK,
    CHANNEL_SIDE,
    CHANNEL_LFE,
    CHANNEL_CC
} LATM_CHANNEL_POSITION;

typedef struct AAC_LATM_SPEC_CONFIG {
    int objectType;
    int samplingIndex;
    int sampleRate;
    int channelConfig;
    int channels;
    int extObjectType;
    int extSamplingIndex;
    int extSampleRate;
    int extChannelConfig;
} AAC_LATM_SPEC_CONFIG;

typedef struct AAC_LATM_PARSER {
    AAC_LATM_SPEC_CONFIG sConfig;
    GetBitContext gb;
    ParserMemoryOps* memOps;
    uint32 payloadStartBitOffset;
    uint32 payloadStartOffset;
    uint32 payloadLen;
    uint8* pCodecData;
    uint32 nCodecDataSize;
    int muxVersionA;
    int frameLenType;
    uint32 frameLen;
    uint8* pBuf;
    uint32 bufSize;
    uint32 bufOffset;
    uint32 bGotConfig;
} AAC_LATM_PARSER;

#define AAC_LATM_MKTAG(a, b, c, d) (a | (b << 8) | (c << 16) | (d << 24))
#define AAC_LATM_MKBETAG(a, b, c, d) (d | (c << 8) | (b << 16) | (a << 24))

#define AAC_LATM_SYNC_WORD 0x2B7
#define AAC_LATM_SYNC_WORD_BYTES 2

#define AAC_LATM_ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))
#define AAC_LATM_MIN(a, b) ((a) > (b) ? (b) : (a))

int LatmGetValue(GetBitContext* gb);

int GetAudioObjectType(GetBitContext* gb);

int GetSampleRate(GetBitContext* gb, int* pIndex);

void ParseChannelMap(AAC_LATM_PARSER* h, uint8 layoutMap[][3], int num, int type);

int ParseProgramConfigElement(AAC_LATM_PARSER* h, uint8 (*layoutMap)[3]);

int ParseGASpecificConfig(AAC_LATM_PARSER* h, int channelConfig);

int ParseLatmAudioSpecificConfig(AAC_LATM_PARSER* h, int ascLen);

int ParseAudioMuxConfig(AAC_LATM_PARSER* h);

int ParsePayloadLengthInfo(AAC_LATM_PARSER* h);

int ParseAudioMuxElement(AAC_LATM_PARSER* h);

#endif

/* EOF */
