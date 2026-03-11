/*
 ***********************************************************************
 * Copyright (c) 2015-2016, Freescale Semiconductor, Inc.
 * Copyright 2017, 2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include "fsl_types.h"
#include "fsl_parser.h"
#include "utils.h"

#ifndef HEVCPARSER_H
#define HEVCPARSER_H

// flags
#define SEARCH_KEY_FRMAE 0x1  // output when get KEY FRMAE
#define ONLY_I_FRMAE 0x2      // only output I frame
#define DROP_B_FRMAE 0x4      // only output I,P frames

typedef void* HevcParserHandle;

typedef enum {
    HevcPARSER_SUCCESS = 0,
    HevcPARSER_HAS_ONE_FRAME,
    HevcPARSER_CORRUPTED_FRAME,
    HevcPARSER_SMALL_OUTPUT_BUFFER,
    HevcPARSER_ERROR,
} HevcParserRetCode;

typedef struct HevcFrame {
    int64 pts;
    uint32 data_size;
    uint32 alloc_size;
    uint8* buffer;
    uint32 is_sync;
} HevcFrame;

typedef struct HevcVideoHeader {
    uint16 Len;
    uint16 HSize;
    uint16 VSize;
    uint32 BitRate;
    uint32 FRNumerator;
    uint32 FRDenominator;
} HevcVideoHeader;

int HevcParseVideoHeader(HevcVideoHeader* pVHeader, uint8* pData, int size);

/*
 * param pHandle[out] : handle of hevc parser
 * param pMemOps[in]: Memory operation callback table.Implements the functions to malloc, calloc,
 * realloc and free memory. param mode[in]: 1 is to parse complete hevc frames/fields, 0 is to parse
 * non-complete hevc frames/fields CreateHevcParser is to create h264 parser and return the hanldle
 */
int CreateHevcParser(HevcParserHandle* pHandle, ParserMemoryOps* pMemOps);

/* Reset the variables to 0 in hevc parser and free the buffers if the parser has allocated. */
int ResetHevcParser(HevcParserHandle handle);

/* Free all the buffers and contexts in hevc parser */
int DeleteHevcParser(HevcParserHandle handle);

/*
 * input buffer must be Filed/Frame, for filed input,
 * will return HEVCPARSER_HAS_ONE_FRMAE to indicate complete frame found.
 * Only parse the in_buf, not copy to output buffer
 *
 * param handle[in] : hevc parser handle
 * param in_data [in] : input bitstream buffer
 * param in_size [in] : input bitstream size in byte
 * param is_sync [out] : 1 for sync frame, 0 for non-sync frame
 */
HevcParserRetCode ParseHevcField(HevcParserHandle handle, uint8* in_data, uint32 in_size,
                                 uint32* is_sync);

/*
 * parse in_buf, copy hevc frame into output buffer, need requirment of the
 * input data.
 *
 * param handle[in] : hevc parser handle
 * param in_data [in] : input bitstream buffer
 * param in_size [in] : input bitstream size in byte
 * param flags [in] : parsing flag
 * param consumed_size [out] : consumed byte of the in_buf
 *
 */
HevcParserRetCode ParseHevcStream(HevcParserHandle handle, uint8* in_data, uint32 in_size,
                                  uint32 flags, uint32* consumed_size);

HevcParserRetCode GetHevcFrameBuffer(HevcParserHandle handle, FrameInfo* frame);

/*
 * parse data to detect a key frame
 *
 * param handle[in] : hevc parser handle
 * param data [in] : input bitstream buffer
 * param size [in] : input bitstream size in byte
 *
 * return HevcPARSER_SUCCESS when there is key frame in data
 */
HevcParserRetCode FindHevcKeyFrame(HevcParserHandle handle, uint8* data, uint32 size);

#endif
