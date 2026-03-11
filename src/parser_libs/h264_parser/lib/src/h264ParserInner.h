/*
 ***********************************************************************
 * Copyright (c) 2015-2016, Freescale Semiconductor, Inc.
 * Copyright 2017-2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */
#include "fsl_types.h"
#include "fsl_parser.h"
#include "h264parser.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SPLIT_INNER_H
#define SPLIT_INNER_H

#define SEPARATOR " "

#define BASELINE_SHORT_NAME "BLN_MAD-MMLAYER_H264PARSER_02.00.00"

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

#define MIN_CACHE_BITS 25
#define MAX_THREADS 16
#define MAX_LEVEL 64
#define MAX_RUN 64

#define P_TYPE 0
#define B_TYPE 1
#define I_TYPE 2
#define SP_TYPE 3
#define SI_TYPE 4

/* picture type */
#define PICT_TOP_FIELD 1
#define PICT_BOTTOM_FIELD 2
#define PICT_FRAME 3

#define DECLARE_ALIGNED(n, t, v) t __attribute__((aligned(n))) v
#define DECLARE_ASM_CONST(n, t, v) static const t attribute_used __attribute__((aligned(n))) v

#define DECLARE_ALIGNED_16(t, v, ...) DECLARE_ALIGNED(16, t, v)
#define DECLARE_ALIGNED_8(t, v, ...) DECLARE_ALIGNED(8, t, v)

#define MAX_SPS_COUNT 32
#define MAX_PPS_COUNT 256
#define MAX_SLICES 16
#define MAX_DELAYED_PIC_COUNT 16
#define MAX_MMCO_COUNT 66
#define EXTENDED_SAR 255
#define MAX_PICTURE_COUNT 32
#define MAX_SLICE_NUM 256

#define ENOMEM (-2)
#define END_NOT_FOUND (-100)

static const uint8_t SEI_NUM_CLOCK_TS_TABLE[9] = {1, 1, 1, 2, 2, 3, 3, 2, 3};

static const uint8_t GOLOMB_TO_PICT_TYPE[5] = {P_TYPE, B_TYPE, I_TYPE, SP_TYPE, SI_TYPE};

typedef enum {
    NAL_SLICE = 1,
    NAL_DPA,
    NAL_DPB,
    NAL_DPC,
    NAL_IDR_SLICE,
    NAL_SEI,
    NAL_SPS,
    NAL_PPS,
    NAL_AUD,
    NAL_END_SEQUENCE,
    NAL_END_STREAM,
    NAL_FILLER_DATA,
    NAL_SPS_EXT,
    NAL_AUXILIARY_SLICE = 19
} NAL_TYPE;

typedef enum {
    SEI_BUFFERING_PERIOD = 0,
    SEI_TYPE_PIC_TIMING = 1,
    SEI_TYPE_USER_DATA_UNREGISTERED = 5,
    SEI_TYPE_RECOVERY_POINT = 6
} SEI_Type;

typedef struct H264Rational {
    int num;
    int den;
} H264Rational;

typedef struct SPS {
    int level_idc;
    int profile_idc;
    int chroma_format_idc;
    int pic_order_cnt_type;

    int log2_max_pic_order_cnt_lsb;  ///< log2_max_pic_order_cnt_lsb_minus4 + 4
    int log2_max_frame_num;          ///< log2_max_frame_num_minus4 + 4
    int frame_mbs_only_flag;
    int direct_8x8_inference_flag;
    int vui_param_present_flag;
    int gaps_in_frame_num_allowed_flag;
    int delta_pic_order_always_zero_flag;
    int vcl_hrd_param_present_flag;
    int separate_colour_plane_flag;
    int mb_adaptive_frame_field_flag;
    int nal_hrd_param_present_flag;
    int pic_struct_present_flag;
    int pic_struct;
    int qpprime_y_zero_transform_bypass_flag;
    int frame_cropping_flag;
    int scaling_matrix_present;
    int bit_depth_luma;    ///< bit_depth_luma_minus8 + 8
    int bit_depth_chroma;  ///< bit_depth_chroma_minus8 + 8
    int offset_for_non_ref_pic;
    int offset_for_top_to_bottom_field;
    int poc_cycle_length;  ///< num_ref_frames_in_pic_order_cnt_cycle
    int max_num_ref_frames;
    int pic_height_in_map_units;  ///< pic_height_in_map_units_minus1 + 1
    int pic_width_in_mbs;         ///< pic_width_in_mbs_minus1 + 1
    int num_units_in_tick;
    int time_scale;

    unsigned int frame_crop_left_offset;
    unsigned int frame_crop_right_offset;
    unsigned int frame_crop_top_offset;
    unsigned int frame_crop_bottom_offset;

    short offset_for_ref_frame[256];
    uint8_t scaling_matrix4[6][16];
    uint8_t scaling_matrix8[2][64];
    int initial_cpb_removal_delay[32];
    H264Rational sar;

    int time_offset_len;
    int cpb_cnt;
    int initial_cpb_removal_delay_len;
    int cpb_removal_delay_len;
    int dpb_output_delay_len;
    int sei_dpb_output_delay;
} SPS;

typedef struct H264Context {
    ParserMemoryOps* memOps;
    GetBitContext gb;

    int nal_ref_idc;
    int nal_unit_type;
    int pict_type;
    int picture_structure;
    int key_frame;
    int frame_num;
    int is_skip_parse_nal;
    int sliceNum;
    int anSliceStruct[MAX_SLICE_NUM];
    int field_count;
    int is_first_field_I;
    int last_frame_num;
    int last_nal_offset;
    bool add_sei_pos;
    SeiPosition sei_pos;
    int sei_cpb_removal_delay;
    int sei_dpb_output_delay;
    int sei_recovery_frame_cnt;
    int sei_buffering_period_present;
    int dpb_output_delay_len;
    int initial_cpb_removal_delay_len;
    int initial_cpb_removal_delay[MAX_SPS_COUNT];

    uint32 frame_size;
    uint32 map_sps_id[MAX_PPS_COUNT];
    int pps_array[MAX_PPS_COUNT];

    SPS* sps_buffers[MAX_SPS_COUNT];
    SPS* sps;
} H264Context;

typedef struct H264Parser {
    H264ParserHandle h;
    ParserMemoryOps* memOps;
    ParserContext parser_ctx;
    H264Context h264_ctx;
    uint32 state;

    uint8* rbsp_buffer[2];
    unsigned int rbsp_buffer_size[2];

    uint8* pOutputBuffer;
    uint32 bufferSize;
    uint32 outputSize;
    int64 curr_pts;
    uint32 is_next_first_nalu;  // is the next nalu  is the first nalu of the next frame
    uint32 is_get_half_nalu;
    uint32 is_find_frame_start;
    uint32 is_find_frame_end;
    uint32 nal_start_code_found;
    uint32 last_nal_payload_start;

    uint8* pHalfNALUBuffer;
    uint32 halfNALUSize;
    bool avcc;

} H264Parser;

#define H264_INPUT_BUFFER_PADDING_SIZE 8

#define H264MAX(a, b) ((a) > (b) ? (a) : (b))
#define H264MAX3(a, b, c) H264MAX(H264MAX(a, b), c)
#define H264MIN(a, b) ((a) > (b) ? (b) : (a))
#define H264MIN3(a, b, c) H264MIN(H264MIN(a, b), c)
#define H264_ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))

#endif

/*EOF*/
