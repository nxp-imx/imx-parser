/*
 ***********************************************************************
 * Copyright (c) 2015-2016, Freescale Semiconductor, Inc.
 * Copyright 2017, 2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */
#include "fsl_types.h"
#include "fsl_parser.h"
#include "hevcparser.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SPLIT_INNER_H
#define SPLIT_INNER_H

#define SEPARATOR " "

#define BASELINE_SHORT_NAME "BLN_MAD-MMLAYER_HEVCPARSER_02.00.00"

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

#define MAX_THREADS 16
#define MAX_LEVEL 64
#define MAX_RUN 64

#define DECLARE_ALIGNED(n, t, v) t __attribute__((aligned(n))) v
#define DECLARE_ASM_CONST(n, t, v) static const t attribute_used __attribute__((aligned(n))) v
#define DECLARE_ALIGNED_16(t, v, ...) DECLARE_ALIGNED(16, t, v)
#define DECLARE_ALIGNED_8(t, v, ...) DECLARE_ALIGNED(8, t, v)

/*spec 7.4.2.1 */
#define MAX_SUB_LAYERS 7
#define MAX_VPS_COUNT 16
#define MAX_SPS_COUNT 32
#define MAX_PPS_COUNT 256

#define ENOMEM (-2)
#define END_NOT_FOUND (-100)

#define HEVC_NAL_MASK 0xFFFFFF7E
#define HEVC_VPS_NAL 0x00000140
#define HEVC_SPS_NAL 0x00000142
#define IS_HEVC_SPS(x) ((x & HEVC_NAL_MASK) == HEVC_SPS_NAL ? 1 : 0)
#define IS_HEVC_VPS(x) ((x & HEVC_NAL_MASK) == HEVC_VPS_NAL ? 1 : 0)
#define IS_STARTCODE(x) (((x) & 0xFFFFFF) == 0x000001)

typedef enum {
    TRAIL_N = 0,
    TRAIL_R,
    TSA_N,
    TSA_R,
    STSA_N,
    STSA_R,
    RADL_N,
    RADL_R,
    RASL_N,
    RASL_R,
    RSV_VCL_N10,
    RSV_VCL_R11,
    RSV_VCL_N12,
    RSV_VCL_R13,
    RSV_VCL_N14,
    RSV_VCL_R15,
    BLA_W_LP,
    BLA_W_RADL,
    BLA_N_LP,
    IDR_W_RADL,
    IDR_N_LP,
    CRA_NUT,
    RSV_IRAP_VCL22,
    RSV_IRAP_VCL23,
    VPS_NUT = 32,
    SPS_NUT = 33,
    PPS_NUT = 34,
    AUD_NUT = 35,
    EOS_NUT = 36,
    EOB_NUT = 37,
    FD_NUT = 38,
    PREFIX_SEI_NUT = 39,
    SUFFIX_SEI_NUT = 40
} NAL_TYPE;

typedef enum {
    SEI_BUFFERING_PERIOD = 0,
    SEI_TYPE_PIC_TIMING = 1,
    SEI_TYPE_USER_DATA_UNREGISTERED = 5,
    SEI_TYPE_RECOVERY_POINT = 6
} SEI_Type;

typedef struct HEVCRational {
    int num;
    int den;
} HEVCRational;

typedef struct SPS {
    int level_idc;
    int profile_idc;
    int vps_id;
    int max_sub_layers;
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

    unsigned int width;
    unsigned int height;
    unsigned int conf_win_left_offset;
    unsigned int conf_win_right_offset;
    unsigned int conf_win_top_offset;
    unsigned int conf_win_bottom_offset;

    int time_offset_len;
    int cpb_cnt;
    int initial_cpb_removal_delay_len;
    int cpb_removal_delay_len;
    int dpb_output_delay_len;
    int sei_dpb_output_delay;
} SPS;

typedef struct HevcParser {
    HevcParserHandle h;
    ParserMemoryOps* memOps;
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
    uint32 key_frame;
} HevcParser;

typedef struct PTLCommon {
    uint8 profile_space;
    uint8 tier_flag;
    uint8 profile_idc;
    uint8 profile_compatibility_flag[32];
    uint8 level_idc;
    uint8 progressive_source_flag;
    uint8 interlaced_source_flag;
    uint8 non_packed_constraint_flag;
    uint8 frame_only_constraint_flag;
} PTLCommon;

typedef struct PTL {
    PTLCommon general_ptl;
    PTLCommon sub_layer_ptl[MAX_SUB_LAYERS];

    uint8 sub_layer_profile_present_flag[MAX_SUB_LAYERS];
    uint8 sub_layer_level_present_flag[MAX_SUB_LAYERS];
} PTL;

#define HEVC_INPUT_BUFFER_PADDING_SIZE 8

#define HEVCMAX(a, b) ((a) > (b) ? (a) : (b))
#define HEVCMAX3(a, b, c) H264MAX(H264MAX(a, b), c)
#define HEVCMIN(a, b) ((a) > (b) ? (b) : (a))
#define HEVCMIN3(a, b, c) H264MIN(H264MIN(a, b), c)
#define HEVC_ARRAY_ELEMS(a) (sizeof(a) / sizeof((a)[0]))

#endif

/*EOF*/
