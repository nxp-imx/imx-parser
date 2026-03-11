/*
***********************************************************************
* Copyright (c) 2015, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef AAC_LATM_PARSER_H
#define AAC_LATM_PARSER_H

#include "fsl_types.h"
#include "fsl_parser.h"
#include "utils.h"

typedef void* AacLatmParserHandle;

typedef enum {
    LATMPARSER_SUCCESS,
    LATMPARSER_HAS_OUTPUT,
    LATMPARSER_NEED_MORE_DATA,
    LATMPARSER_NEED_BUFFER,
    LATMPARSER_ERROR,
} AacLatmParserRetCode;

typedef struct AAC_LATM_AUDIO_INFO {
    int nSampleRate;
    int nChannles;
    uint8* pCodecData;
    int nCodecDataSize;
} AAC_LATM_AUDIO_INFO;

/*
 * para pData[in]: input data
 * para size[in]: input data size
 * para pInfo[in/out]: aac latm audio info, like sample rate, channels and codec data
 */
AacLatmParserRetCode ParseAacLatmAudioInfo(uint8* pData, uint32 size, AAC_LATM_AUDIO_INFO* pInfo);

/*
 * para pData[in]: input data
 * para size[in]: input data size
 * para consumed[out]: the total consumed bytes
 */
AacLatmParserRetCode ParseAacLatmData(AacLatmParserHandle handle, uint8* pData, uint32 size,
                                      uint32* consumed);

AacLatmParserRetCode SetAacLatmBuffer(AacLatmParserHandle handle, FrameInfo* pFrame);

AacLatmParserRetCode GetAacLatmBuffer(AacLatmParserHandle handle, FrameInfo* pFrame);

AacLatmParserRetCode CreateAacLatmParser(AacLatmParserHandle* pHandle, ParserMemoryOps* pMemOps);

AacLatmParserRetCode DeleteAacLatmParser(AacLatmParserHandle handle);

#endif

/* EOF */
