/*
 ***********************************************************************
 * Copyright (c) 2012-2016, Freescale Semiconductor, Inc.
 * Copyright 2017, 2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include "fsl_types.h"
#include "fsl_parser.h"
#include "utils.h"

#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN
#endif

#ifndef SPLIT_H
#define SPLIT_H

// flags
#define SEARCH_KEY_FRMAE 0x1  // output when get KEY FRMAE
#define ONLY_I_FRMAE 0x2      // only output I frame
#define DROP_B_FRMAE 0x4      // only output I,P frames

#define H264_SEI_POSITION_DATA_SIZE 8

typedef void* H264ParserHandle;

typedef enum {
    H264PARSER_SUCCESS,
    H264PARSER_HAS_ONE_FRAME,
    H264PARSER_CORRUPTED_FRAME,
    H264PARSER_SMALL_OUTPUT_BUFFER,
    H264PARSER_ERROR,
} H264ParserRetCode;

typedef struct {
    uint32 width;
    uint32 height;
    uint32 frameNumerator;
    uint32 frameDenominator;
    uint32 bitrate;
    uint32 scanType;
} H264HeaderInfo;

/*
 * param pHandle[out] : handle of h264 parser
 * param pMemOps[in]: Memory operation callback table.Implements the functions to malloc, calloc,
 * realloc and free memory. param mode[in]: 1 is to parse complete h264 frames/fields, 0 is to parse
 * non-complete h264 frames/fields CreateH264Parser is to create h264 parser and return the hanldle
 */
EXTERN int CreateH264Parser(H264ParserHandle* pHandle, ParserMemoryOps* pMemOps, uint32 flags);

/* Reset the variables to 0 in h264 parser and free the buffers if the parser has allocated. */
EXTERN int ResetH264Parser(H264ParserHandle handle);

/* Free all the buffers and contexts in h264 parser */
EXTERN int DeleteH264Parser(H264ParserHandle handle);

/*
 * input buffer must be Filed/Frame, for filed input,
 * will return H264PARSER_HAS_ONE_FRMAE to indicate complete frame found.
 * Only parse the in_buf, not copy to output buffer
 *
 * param handle[in] : h264 parser handle
 * param in_data [in] : input bitstream buffer
 * param in_size [in] : input bitstream size in byte
 * param is_sync [out] : 1 for sync frame, 0 for non-sync frame
 */
EXTERN H264ParserRetCode ParseH264Field(H264ParserHandle handle, uint8* in_data, uint32 in_size,
                                        uint32* is_sync);

/*
 * parse in_buf, copy h264 frame into output buffer, need requirment of the
 * input data.
 *
 * param handle[in] : h264 parser handle
 * param in_data [in] : input bitstream buffer
 * param in_size [in] : input bitstream size in byte
 * param flags [in] : parsing flag
 * param consumed_size [out] : consumed byte of the in_buf
 *
 */
EXTERN H264ParserRetCode ParseH264Stream(H264ParserHandle handle, uint8* in_data, uint32 in_size,
                                         uint32 flags, uint32* consumed_size);

EXTERN H264ParserRetCode GetH264FrameBuffer(H264ParserHandle handle, FrameInfo* frame);

/*
 * parse data to detect a key frame
 *
 * param handle[in] : h264 parser handle
 * param data [in] : input bitstream buffer
 * param size [in] : input bitstream size in byte
 *
 * return H264PARSER_SUCCESS when there is key frame in data
 */
EXTERN H264ParserRetCode FindH264KeyFrame(H264ParserHandle handle, uint8* data, uint32 size);

/*
 * parse in_buf, copy h264 frame into output buffer, need requirment of the
 * input data.
 *
 * param handle[in] : h264 parser handle
 * param in_data [in] : input bitstream buffer
 * param in_size [in] : input bitstream size in byte
 * param frame_flag [out] : flags that indecate whether it is complete frame.
 *
 * return: H264PARSER_HAS_ONE_FRAME when it has one complete frame.
 */
#define H264_PARSER_SPS 0x1
#define H264_PARSER_PPS 0x2
#define H264_PARSER_FRAME 0x4

/*
 * parse h264 codec data, fill h264 header info
 *
 * param handle[in] : h264 parser handle
 * param codecdata [in] : codec data
 * param size [in] : codec data size in byte
 * param header [in & out] : fill info into h264 header
 *
 */
EXTERN H264ParserRetCode ParseH264CodecDataFrame(H264ParserHandle handle, uint8* codecdata,
                                                 uint32 size, H264HeaderInfo* header);

#endif
