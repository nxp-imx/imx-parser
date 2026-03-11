/*
 ***********************************************************************
 * Copyright (c) 2015-2016, Freescale Semiconductor, Inc.
 * Copyright 2017-2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsl_types.h"
#include "fsl_parser.h"
#include "h264ParserInner.h"
#include "h264parser.h"
#include "utils.h"

#ifdef PARSER_H264_DBG
#define PARSER_H264_LOG printf
#define PARSER_H264_ERR printf
#else
#define PARSER_H264_LOG(...)
#define PARSER_H264_ERR(...)
#endif
#define ASSERT(exp)                                                                   \
    if (!(exp)) {                                                                     \
        PARSER_H264_ERR("%s: %d : assert condition !!!\r\n", __FUNCTION__, __LINE__); \
    }

#define H264_NALU_BAK_SIZE (1024 * 1024)
#define H264_NALU_STARTCODE_SIZE 4

static __inline int H264Clip(int a, int amin, int amax) {
    if (a < amin)
        return amin;
    else if (a > amax)
        return amax;
    else
        return a;
}

static void* H264Malloc(H264Context* h, unsigned int size) {
    void* ptr = NULL;

    if (size > (INT_MAX - 16))
        return NULL;

    ptr = h->memOps->Malloc(size);

    return ptr;
}

static void H264Free(H264Context* h, void* ptr) {
    if (ptr)
        h->memOps->Free(ptr);
}

static void H264FreeP(H264Context* h, void* arg) {
    void** ptr = (void**)arg;
    if (*ptr != NULL)
        H264Free(h, *ptr);
    *ptr = NULL;
}

static void* H264MallocSize(H264Context* h, unsigned int size) {
    void* ptr = H264Malloc(h, size);
    if (ptr)
        memset(ptr, 0, size);
    return ptr;
}

static void* H264Realloc(H264Context* h, void* ptr, unsigned int size) {
    if (size > (INT_MAX - 16))
        return NULL;

    return h->memOps->ReAlloc(ptr, size);
}

static int H264CheckDimensions(unsigned int width, unsigned int height) {
    if ((int)width > 0 && (int)height > 0 && (width + 128) * (uint64_t)(height + 128) < INT_MAX / 8)
        return 0;

    return -1;
}

static __inline int H264ParseHrdParameters(H264Context* h, SPS* sps) {
    int cpb_cnt, i;

    cpb_cnt = get_ue_golomb_31(&h->gb) + 1;

    if ((unsigned int)cpb_cnt > MAX_SPS_COUNT) {
        return -1;
    }

    skip_nbits(&h->gb, 8);

    for (i = 0; i < cpb_cnt; i++) {
        get_ue_golomb(&h->gb);
        get_ue_golomb(&h->gb);
        get_bits1(&h->gb);
    }
    sps->initial_cpb_removal_delay_len = get_nbits(&h->gb, 5) + 1;
    sps->cpb_removal_delay_len = get_nbits(&h->gb, 5) + 1;
    sps->dpb_output_delay_len = get_nbits(&h->gb, 5) + 1;
    sps->time_offset_len = get_nbits(&h->gb, 5);
    sps->cpb_cnt = cpb_cnt;
    return 0;
}

static __inline int H264ParseVUIParameters(H264Context* h, SPS* pSps) {
    int aspect_ratio_info_present_flag;

    aspect_ratio_info_present_flag = get_bits1(&h->gb);

    if (aspect_ratio_info_present_flag) {
        if (EXTENDED_SAR == get_nbits(&h->gb, 8)) {
            pSps->sar.num = get_nbits(&h->gb, 16);
            pSps->sar.den = get_nbits(&h->gb, 16);
        }
    } else {
        pSps->sar.num = pSps->sar.den = 0;
    }

    if (get_bits1(&h->gb)) {
        skip_nbits(&h->gb, 1);
    }

    if (get_bits1(&h->gb)) { /*video_signal_type_present_flag*/
        skip_nbits(&h->gb, 4);
        if (get_bits1(&h->gb)) { /*colour_description_present_flag*/
            skip_nbits(&h->gb, 24);
        }
    }

    if (get_bits1(&h->gb)) {   /* chroma_location_info_present_flag */
        get_ue_golomb(&h->gb); /* chroma_sample_location_type_top_field */
        get_ue_golomb(&h->gb); /* chroma_sample_location_type_bottom_field */
    }

    if (get_bits1(&h->gb)) {
        pSps->num_units_in_tick = get_nbits_long(&h->gb, 32);
        pSps->time_scale = get_nbits_long(&h->gb, 32);
        if (!pSps->num_units_in_tick || !pSps->time_scale) {
            return -1;
        }
        skip_nbits(&h->gb, 1);
    }

    pSps->nal_hrd_param_present_flag = get_bits1(&h->gb);
    if (pSps->nal_hrd_param_present_flag)
        if (H264ParseHrdParameters(h, pSps) < 0)
            return -1;
    pSps->vcl_hrd_param_present_flag = get_bits1(&h->gb);
    if (pSps->vcl_hrd_param_present_flag)
        if (H264ParseHrdParameters(h, pSps) < 0)
            return -1;
    if (pSps->nal_hrd_param_present_flag || pSps->vcl_hrd_param_present_flag)
        skip_nbits(&h->gb, 1);
    pSps->pic_struct_present_flag = get_bits1(&h->gb);

    if (get_bits1(&h->gb)) { /*bitstream_restriction_flag*/
        int num_reorder_frames;
        skip_nbits(&h->gb, 1);
        get_ue_golomb(&h->gb);
        get_ue_golomb(&h->gb);
        get_ue_golomb(&h->gb);
        get_ue_golomb(&h->gb);
        num_reorder_frames = get_ue_golomb(&h->gb);
        get_ue_golomb(&h->gb);

        if ((unsigned int)num_reorder_frames > 16U)
            return -1;
    }

    return 0;
}

static int H264ParseBufferingPeriod(H264Context* h) {
    unsigned int sps_id;
    int sched_sel_index;
    SPS* pSps;

    sps_id = get_ue_golomb_31(&h->gb);
    if (sps_id > 31 || !h->sps_buffers[sps_id]) {
        return -1;
    }
    pSps = h->sps_buffers[sps_id];

    if (pSps->nal_hrd_param_present_flag) {
        for (sched_sel_index = 0; sched_sel_index < pSps->cpb_cnt; sched_sel_index++) {
            h->initial_cpb_removal_delay[sched_sel_index] =
                    get_nbits(&h->gb, pSps->initial_cpb_removal_delay_len);
            skip_nbits(&h->gb, pSps->initial_cpb_removal_delay_len);
        }
    }
    if (pSps->vcl_hrd_param_present_flag) {
        for (sched_sel_index = 0; sched_sel_index < pSps->cpb_cnt; sched_sel_index++) {
            h->initial_cpb_removal_delay[sched_sel_index] =
                    get_nbits(&h->gb, pSps->initial_cpb_removal_delay_len);
            skip_nbits(&h->gb, pSps->initial_cpb_removal_delay_len);
        }
    }

    h->sei_buffering_period_present = 1;
    return 0;
}

static int H264ParsePictureTiming(H264Context* h) {
    if (NULL == h->sps)
        return 0;

    if (h->sps->vcl_hrd_param_present_flag || h->sps->nal_hrd_param_present_flag) {
        h->sei_cpb_removal_delay = get_nbits(&h->gb, h->sps->cpb_removal_delay_len);
        h->sei_dpb_output_delay = get_nbits(&h->gb, h->sps->dpb_output_delay_len);
    }
    if (h->sps->pic_struct_present_flag) {
        unsigned int k = 0, num_clock_ts = 0;

        h->sps->pic_struct = get_nbits(&h->gb, 4);
        if (h->sps->pic_struct > 8)
            return -1;

        num_clock_ts = SEI_NUM_CLOCK_TS_TABLE[h->sps->pic_struct];

        for (k = 0; k < num_clock_ts; k++) {
            if (get_bits1(&h->gb)) {
                unsigned int full_ts_flag;
                skip_nbits(&h->gb, 8);
                full_ts_flag = get_bits1(&h->gb);
                skip_nbits(&h->gb, 10);
                if (full_ts_flag) {
                    skip_nbits(&h->gb, 17);
                } else {
                    if (get_bits1(&h->gb)) {
                        skip_nbits(&h->gb, 6);
                        if (get_bits1(&h->gb)) {
                            skip_nbits(&h->gb, 6);
                            if (get_bits1(&h->gb))
                                skip_nbits(&h->gb, 5);
                        }
                    }
                }
                if (h->sps->time_offset_len > 0) {
                    skip_nbits(&h->gb, h->sps->time_offset_len);
                }
            }
        }
    }
    return 0;
}

static int H264ParseSei(H264Context* h) {
    while (get_bits_count(&h->gb) + 16 < h->gb.size_in_bits) {
        int len = 0, sei_type = 0;

        do {
            sei_type += show_nbits(&h->gb, 8);
        } while (255 == get_nbits(&h->gb, 8));

        do {
            len += show_nbits(&h->gb, 8);
        } while (255 == get_nbits(&h->gb, 8));

        switch (sei_type) {
            case SEI_TYPE_USER_DATA_UNREGISTERED:
                skip_nbits(&h->gb, 8 * len);
                break;
            case SEI_TYPE_PIC_TIMING:
                if (H264ParsePictureTiming(h) < 0)
                    return -1;
                break;
            case SEI_BUFFERING_PERIOD:
                if (H264ParseBufferingPeriod(h) < 0)
                    return -1;
                break;
            case SEI_TYPE_RECOVERY_POINT:
                h->sei_recovery_frame_cnt = get_ue_golomb(&h->gb);
                skip_nbits(&h->gb, 4);
                break;
            default:
                skip_nbits(&h->gb, 8 * len);
        }
        align_get_bits(&h->gb);
    }

    return 0;
}

static int H264GetSpsID(H264Context* h) {
    unsigned int pps_id, sps_id;

    pps_id = get_ue_golomb(&h->gb);
    sps_id = get_ue_golomb_31(&h->gb);

    if (pps_id >= MAX_PPS_COUNT || sps_id >= MAX_SPS_COUNT) {
        return -1;
    }

    h->pps_array[pps_id] = pps_id;
    h->map_sps_id[pps_id] = sps_id;
    return 0;
}

static void H264ParseScalingList(H264Context* h, uint8_t* factors, int size,
                                 const uint8_t* jvt_list, const uint8_t* fallback_list) {
    int i, lastScale = 8, nextScale = 8, deltaScale;
    const uint8_t* scan = size == 16 ? ZIGZAG_SCAN : ZIGZAG_DIRECT;
    if (!get_bits1(&h->gb))
        memcpy(factors, fallback_list, size * sizeof(uint8_t));
    else {
        for (i = 0; i < size; i++) {
            if (nextScale != 0) {
                deltaScale = get_se_golomb(&h->gb);
                nextScale = (lastScale + deltaScale) & 0xFF;
            }
            if (!i && !nextScale) {
                memcpy(factors, jvt_list, size * sizeof(uint8_t));
                break;
            }
            lastScale = factors[scan[i]] = nextScale == 0 ? lastScale : nextScale;
        }
    }
}

static void H264ParseScalingMatrices(H264Context* h, SPS* pSps, int is_sps,
                                     uint8_t (*scaling_mtx4)[16], uint8_t (*scaling_mtx8)[64]) {
    int scaling_matrix_flag = !is_sps && pSps->scaling_matrix_present;
    const uint8_t* fallback_mtx[4] = {
            scaling_matrix_flag ? pSps->scaling_matrix4[0] : DEFAULT_SCALING4[0],
            scaling_matrix_flag ? pSps->scaling_matrix4[3] : DEFAULT_SCALING4[1],
            scaling_matrix_flag ? pSps->scaling_matrix8[0] : DEFAULT_SCALING8[0],
            scaling_matrix_flag ? pSps->scaling_matrix8[1] : DEFAULT_SCALING8[1]};
    if (get_bits1(&h->gb)) {
        H264ParseScalingList(h, scaling_mtx4[0], 16, DEFAULT_SCALING4[0],
                             fallback_mtx[0]);  // Intra, Y
        H264ParseScalingList(h, scaling_mtx4[1], 16, DEFAULT_SCALING4[0],
                             scaling_mtx4[0]);  // Intra, Cr
        H264ParseScalingList(h, scaling_mtx4[2], 16, DEFAULT_SCALING4[0],
                             scaling_mtx4[1]);  // Intra, Cb
        H264ParseScalingList(h, scaling_mtx4[3], 16, DEFAULT_SCALING4[1],
                             fallback_mtx[1]);  // Inter, Y
        H264ParseScalingList(h, scaling_mtx4[4], 16, DEFAULT_SCALING4[1],
                             scaling_mtx4[3]);  // Inter, Cr
        H264ParseScalingList(h, scaling_mtx4[5], 16, DEFAULT_SCALING4[1],
                             scaling_mtx4[4]);  // Inter, Cb
        H264ParseScalingList(h, scaling_mtx8[0], 64, DEFAULT_SCALING8[0],
                             fallback_mtx[2]);  // Intra, Y
        H264ParseScalingList(h, scaling_mtx8[1], 64, DEFAULT_SCALING8[1],
                             fallback_mtx[3]);  // Inter, Y
    }
}

static int H264ParseSeqParameterSet(H264Context* h) {
    int profile_idc, level_idc, i;
    unsigned int sps_id;
    SPS* pSps;

    profile_idc = get_nbits(&h->gb, 8);
    get_nbits(&h->gb, 8);
    level_idc = get_nbits(&h->gb, 8);
    sps_id = get_ue_golomb_31(&h->gb);

    if (sps_id >= MAX_SPS_COUNT) {
        return -1;
    }
    pSps = H264MallocSize(h, sizeof(SPS));
    if (pSps == NULL)
        return -1;

    pSps->profile_idc = profile_idc;
    pSps->level_idc = level_idc;

    memset(pSps->scaling_matrix4, 16, sizeof(pSps->scaling_matrix4));
    memset(pSps->scaling_matrix8, 16, sizeof(pSps->scaling_matrix8));
    pSps->scaling_matrix_present = 0;

    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
        profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
        profile_idc == 128 || profile_idc == 138 || profile_idc == 144) {
        pSps->chroma_format_idc = get_ue_golomb_31(&h->gb);
        if (pSps->chroma_format_idc == 3)
            pSps->separate_colour_plane_flag = get_bits1(&h->gb);
        pSps->bit_depth_luma = get_ue_golomb(&h->gb) + 8;
        pSps->bit_depth_chroma = get_ue_golomb(&h->gb) + 8;
        pSps->qpprime_y_zero_transform_bypass_flag = get_bits1(&h->gb);
        H264ParseScalingMatrices(h, pSps, 1, pSps->scaling_matrix4, pSps->scaling_matrix8);
    } else if (profile_idc == 66 || profile_idc == 77 || profile_idc == 88) {
        pSps->chroma_format_idc = 1;
        pSps->bit_depth_luma = 8;
        pSps->bit_depth_chroma = 8;
    } else {
        /* unknown profile */
        goto fail;
    }

    pSps->log2_max_frame_num = get_ue_golomb(&h->gb) + 4;
    pSps->pic_order_cnt_type = get_ue_golomb_31(&h->gb);

    if (0 == pSps->pic_order_cnt_type) {
        pSps->log2_max_pic_order_cnt_lsb = get_ue_golomb(&h->gb) + 4;
    } else if (1 == pSps->pic_order_cnt_type) {
        pSps->delta_pic_order_always_zero_flag = get_bits1(&h->gb);
        pSps->offset_for_non_ref_pic = get_se_golomb(&h->gb);
        pSps->offset_for_top_to_bottom_field = get_se_golomb(&h->gb);
        pSps->poc_cycle_length = get_ue_golomb(&h->gb);

        if ((unsigned)pSps->poc_cycle_length >= H264_ARRAY_ELEMS(pSps->offset_for_ref_frame)) {
            goto fail;
        }

        for (i = 0; i < pSps->poc_cycle_length; i++)
            pSps->offset_for_ref_frame[i] = get_se_golomb(&h->gb);
    } else if (pSps->pic_order_cnt_type != 2) {
        goto fail;
    }

    pSps->max_num_ref_frames = get_ue_golomb_31(&h->gb);
    if ((unsigned int)pSps->max_num_ref_frames > MAX_PICTURE_COUNT - 2 ||
        (unsigned int)pSps->max_num_ref_frames >= 32U) {
        goto fail;
    }
    pSps->gaps_in_frame_num_allowed_flag = get_bits1(&h->gb);
    pSps->pic_width_in_mbs = get_ue_golomb(&h->gb) + 1;
    pSps->pic_height_in_map_units = get_ue_golomb(&h->gb) + 1;
    if (pSps->pic_width_in_mbs >= INT_MAX / 16 || pSps->pic_height_in_map_units >= INT_MAX / 16 ||
        H264CheckDimensions(16 * pSps->pic_width_in_mbs, 16 * pSps->pic_height_in_map_units)) {
        goto fail;
    }

    pSps->frame_mbs_only_flag = get_bits1(&h->gb);
    if (!pSps->frame_mbs_only_flag)
        pSps->mb_adaptive_frame_field_flag = get_bits1(&h->gb);
    else
        pSps->mb_adaptive_frame_field_flag = 0;

    pSps->direct_8x8_inference_flag = get_bits1(&h->gb);
    if (!pSps->frame_mbs_only_flag && !pSps->direct_8x8_inference_flag) {
        goto fail;
    }

    pSps->frame_cropping_flag = get_bits1(&h->gb);
    if (pSps->frame_cropping_flag) {
        pSps->frame_crop_left_offset = get_ue_golomb(&h->gb);
        pSps->frame_crop_right_offset = get_ue_golomb(&h->gb);
        pSps->frame_crop_top_offset = get_ue_golomb(&h->gb);
        pSps->frame_crop_bottom_offset = get_ue_golomb(&h->gb);
    } else {
        pSps->frame_crop_left_offset = pSps->frame_crop_right_offset = pSps->frame_crop_top_offset =
                pSps->frame_crop_bottom_offset = 0;
    }
    pSps->vui_param_present_flag = get_bits1(&h->gb);
    if (pSps->vui_param_present_flag)
        if (H264ParseVUIParameters(h, pSps) < 0)
            goto fail;

    H264Free(h, h->sps_buffers[sps_id]);
    h->sps_buffers[sps_id] = pSps;
    h->sps = pSps;
    return 0;
fail:
    H264Free(h, pSps);
    return -1;
}

static int H264FindNalStartCode(H264Parser* parser, const uint8_t* p, uint32 start, uint32 size) {
    uint32 i = start + 2;
    uint32 state = parser->state;

    if (size < 3 + start)
        goto fail;

    if (state == 0 && p[0] == 1)
        goto fail;

    while (i < size) {
        if (p[i] == 0)
            i++;
        else if (p[i] == 1 && p[i - 1] == 0 && p[i - 2] == 0) {
            return i < 3 ? 0 : (i - 3);
        } else
            i += 2;
    }

fail:
    parser->state = 0;
    if (size >= 3)
        parser->state = p[size - 1] + (p[size - 2] << 8) + (p[size - 3] << 16);
    else {
        i = 0;
        while (i < size) {
            parser->state += p[i++];
        }
    }

    return -1;
}

static const uint8_t* H264DecodeNal(H264Parser* parser, const uint8_t* pInput, uint32* dst_len,
                                    uint32* consumed, uint32 len) {
    H264Context* h = &(parser->h264_ctx);
    uint32 i, si, di;
    uint8_t* pOutput;
    uint32 buf_idx;

    h->nal_unit_type = pInput[0] & 0x1F;
    h->nal_ref_idc = pInput[0] >> 5;

    pInput++;
    len--;

    for (i = 0; i + 1 < len; i += 2) {
        if (pInput[i])
            continue;
        if (i > 0 && pInput[i - 1] == 0)
            i--;
        if (i + 2 < len && pInput[i + 1] == 0 && pInput[i + 2] <= 3) {
            if (pInput[i + 2] != 3)
                len = i;
            break;
        }
    }

    if (i >= len - 1) {
        *dst_len = len;
        *consumed = len + 1;
        return pInput;
    }

    buf_idx = h->nal_unit_type == NAL_DPC ? 1 : 0;
    pOutput = parser->rbsp_buffer[buf_idx];

    if (NULL == pOutput)
        return NULL;
    else if (H264_NALU_BAK_SIZE < len + H264_INPUT_BUFFER_PADDING_SIZE) {
        pOutput = H264Realloc(h, pOutput, len + H264_INPUT_BUFFER_PADDING_SIZE);
        if (NULL == pOutput)
            return NULL;
        else
            parser->rbsp_buffer[buf_idx] = pOutput;
    }

    memcpy(pOutput, pInput, i);
    si = di = i;
    while (si + 2 < len) {
        if (pInput[si] == 0 && pInput[si + 1] == 0 && pInput[si + 2] <= 3) {
            if (pInput[si + 2] != 3)
                goto next_start_code;
            else {
                pOutput[di++] = 0;
                pOutput[di++] = 0;
                si += 3;
                continue;
            }
        } else if (pInput[si + 2] > 3) {
            pOutput[di++] = pInput[si++];
            pOutput[di++] = pInput[si++];
        }
        pOutput[di++] = pInput[si++];
    }

    while (si < len) pOutput[di++] = pInput[si++];

next_start_code:

    memset(pOutput + di, 0, H264_INPUT_BUFFER_PADDING_SIZE);
    *dst_len = di;
    *consumed = si + 1;  //+1 for the header
    return pOutput;
}

int CreateH264Parser(H264ParserHandle* pHandle, ParserMemoryOps* pMemOps, uint32 flags) {
    H264Parser* parser = (H264Parser*)pMemOps->Malloc(sizeof(H264Parser));
    if (NULL == parser)
        return -1;
    memset(parser, 0, sizeof(H264Parser));
    parser->memOps = parser->h264_ctx.memOps = pMemOps;
    parser->h264_ctx.last_nal_offset = -1;
    memset(parser->h264_ctx.pps_array, -1, sizeof(int) * MAX_PPS_COUNT);
    parser->pHalfNALUBuffer = pMemOps->Malloc(H264_NALU_BAK_SIZE);
    parser->rbsp_buffer[0] = (uint8*)pMemOps->Malloc(H264_NALU_BAK_SIZE);
    parser->rbsp_buffer[1] = (uint8*)pMemOps->Malloc(H264_NALU_BAK_SIZE);
    parser->rbsp_buffer_size[0] = parser->rbsp_buffer_size[1] = H264_NALU_BAK_SIZE;
    parser->state = 0xff;
    parser->is_next_first_nalu = 1;
    if (NULL == parser->pHalfNALUBuffer)
        return -1;

    parser->pOutputBuffer = (uint8*)pMemOps->Malloc(sizeof(SeiPosition));
    if (NULL == parser->pOutputBuffer)
        return -1;
    parser->bufferSize = sizeof(SeiPosition);

    if (flags & FLAG_OUTPUT_H264_SEI_POS_DATA)
        parser->h264_ctx.add_sei_pos = TRUE;
    *pHandle = (H264ParserHandle)parser;
    return 0;
}

int ResetH264Context(H264Context* h) {

    // Don't reset map_sps_id, pps_array and sps_buffers because they are related to
    // key frames, or parser can't recogise some key frames after seek.
    if (NULL != h) {
        h->nal_ref_idc = 0;
        h->nal_unit_type = 0;
        h->pict_type = 0;
        h->picture_structure = 0;
        h->key_frame = 0;
        h->frame_num = -1;
        h->is_skip_parse_nal = 0;
        h->sliceNum = 0;
        h->field_count = 0;
        h->is_first_field_I = 0;
        h->last_frame_num = 0;
        h->last_nal_offset = 0;
        h->sei_cpb_removal_delay = 0;
        h->sei_dpb_output_delay = 0;
        h->sei_recovery_frame_cnt = -1;
        h->sei_buffering_period_present = 0;
        h->dpb_output_delay_len = 0;
        h->initial_cpb_removal_delay_len = 0;
        memset(&h->sei_pos, 0, sizeof(SeiPosition));
        memset(h->initial_cpb_removal_delay, 0, MAX_SPS_COUNT * sizeof(int));
        memset(h->anSliceStruct, 0, MAX_SLICE_NUM * sizeof(h->anSliceStruct[0]));
        h->frame_size = 0;
    }
    return 0;
}

int ResetH264Parser(H264ParserHandle handle) {
    H264Parser* parser = (H264Parser*)handle;
    ResetH264Context(&(parser->h264_ctx));
    parser->is_get_half_nalu = parser->is_find_frame_end = parser->is_find_frame_start =
            parser->nal_start_code_found = parser->last_nal_payload_start = 0;
    parser->state = 0xff;

    if (parser->pOutputBuffer) {
        memset(parser->pOutputBuffer, 0, parser->bufferSize);
        parser->outputSize = 0;
    }

    if (parser->pHalfNALUBuffer) {
        memset(parser->pHalfNALUBuffer, 0, parser->halfNALUSize);
        parser->halfNALUSize = 0;
    }
    return 0;
}

int DeleteH264Context(H264Context* h) {
    ParserMemoryOps* memOps;
    int i = 0;

    if (NULL != h) {
        memOps = h->memOps;

        for (i = 0; i < MAX_SPS_COUNT; i++) {
            H264FreeP(h, h->sps_buffers + i);
            *(h->sps_buffers + i) = NULL;
        }

        memset(h, 0, sizeof(H264Context));

        h->memOps = memOps;
        h->frame_num = h->sei_recovery_frame_cnt = -1;
        memset(h->pps_array, -1, sizeof(int) * MAX_PPS_COUNT);
    }
    return 0;
}

/* Free all the buffers and contexts in h264 parser */
int DeleteH264Parser(H264ParserHandle handle) {
    H264Parser* parser = (H264Parser*)handle;
    H264Context* h;
    ParserMemoryOps* memOps;

    if (parser == NULL)
        return -1;

    h = &(parser->h264_ctx);
    memOps = parser->memOps;

    if (parser->pOutputBuffer)
        H264FreeP(h, &(parser->pOutputBuffer));

    DeleteH264Context(h);
    H264FreeP(h, &(parser->rbsp_buffer[0]));
    H264FreeP(h, &(parser->rbsp_buffer[1]));
    parser->rbsp_buffer[0] = parser->rbsp_buffer[1] = NULL;
    memOps->Free(parser->pHalfNALUBuffer);
    memOps->Free(parser);
    return 0;
}

static int H264FindOneNALU(H264Parser* parser, uint8* data, uint32 size, uint32* nal_len) {

    int end;
    uint32 type;

    if (parser->avcc) {
        if (size < 4) {
            return -1;
        } else {
            *nal_len = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
            *nal_len += 4;
            parser->is_find_frame_end = 1;
            return 0;
        }
    }

    if (!parser->nal_start_code_found || parser->is_get_half_nalu)
        end = H264FindNalStartCode(parser, data, 0, size);
    else
        end = H264FindNalStartCode(parser, data, 3, size);

    if (end < 0) {
        parser->is_get_half_nalu = 1;
        *nal_len = size;
        return -1;  // end not found
    } else {
        parser->nal_start_code_found = 1;
    }

    *nal_len = end;
    type = data[*nal_len + 4] & 0x1F;
    if (type >= 6 && type <= 9 && parser->is_find_frame_start) {
        parser->is_find_frame_end = 1;
        parser->is_find_frame_start = 0;
        parser->nal_start_code_found = 0;
    } else if (type == 1 || type == 2 || type == 5) {
        if (!parser->is_find_frame_start) {
            parser->is_find_frame_start = 1;
        } else if (data[*nal_len + 5] & 0x80) {
            parser->is_find_frame_end = 1;
            parser->is_find_frame_start = 0;
            parser->nal_start_code_found = 0;
        }
    }

    return 0;
}

static int H264ParseOneNALU(H264Parser* parser, uint8* data, uint32 size) {
    H264Context* h = &(parser->h264_ctx);
    uint32 nal_type = data[0] & 0x1F;
    uint32 slice_type = 0, pps_id;
    const uint8_t* ptr;
    uint32 dst_len = 0, consumed = 0;

    if (h->is_skip_parse_nal)
        return 0;
    else if (parser->is_next_first_nalu) {
        parser->is_next_first_nalu = 0;
        h->sliceNum = 0;
        h->key_frame = 0;
        h->sei_dpb_output_delay = 0;
        h->sei_buffering_period_present = 0;
        h->frame_num = -1;
        h->sei_recovery_frame_cnt = -1;
        h->sei_cpb_removal_delay = -1;
    }

    if (nal_type == NAL_IDR_SLICE || nal_type == NAL_SLICE) {
        size = size > 20 ? 20 : size;
    }

    ptr = H264DecodeNal(parser, data, &dst_len, &consumed, size);
    if (init_get_bits(&h->gb, ptr, 8 * dst_len) < 0)
        return 0;
    switch (nal_type) {
        case NAL_PPS:
            H264GetSpsID(h);
            break;
        case NAL_SPS:
            H264ParseSeqParameterSet(h);
            break;
        case NAL_SEI:
            if (h->sei_pos.size == 0) {
                h->sei_pos.offset = parser->outputSize + 4;
                h->sei_pos.size = size;
            }
            H264ParseSei(h);
            break;
        case NAL_IDR_SLICE:
            h->key_frame = 1;
            __attribute__((fallthrough));
        case NAL_SLICE:
            get_ue_golomb(&h->gb);
            slice_type = get_ue_golomb_31(&h->gb);
            h->pict_type = GOLOMB_TO_PICT_TYPE[slice_type % 5];
            if (h->sei_recovery_frame_cnt >= 0 || I_TYPE == h->pict_type)
                h->key_frame = 1;

            pps_id = get_ue_golomb(&h->gb);
            if (pps_id >= MAX_PPS_COUNT || h->pps_array[pps_id] < 0)
                break;
            h->sps = h->sps_buffers[h->map_sps_id[pps_id]];
            if (NULL == h->sps)
                break;

            h->frame_num = get_nbits(&h->gb, h->sps->log2_max_frame_num);
            if (!h->sps->frame_mbs_only_flag) {
                if (get_bits1(&h->gb)) /* field_pic_flag */
                    h->picture_structure = get_bits1(&h->gb) + PICT_TOP_FIELD;
                else
                    h->picture_structure = PICT_FRAME;
            } else
                h->picture_structure = PICT_FRAME;

            if (h->sliceNum >= MAX_SLICE_NUM)
                break;

            h->anSliceStruct[h->sliceNum] = h->picture_structure;
            h->sliceNum++;
            h->is_skip_parse_nal = 1;
            break;
        default:
            break;
    }

    return nal_type;
}

static uint8* H264CopyBuffer(H264Context* h, uint8* dst, uint32* dst_size, uint32 dst_alloc_size,
                             uint8* src, uint32 src_size) {
    if (!dst)
        return NULL;
    if (dst_alloc_size < *dst_size + src_size) {
        dst = (uint8*)H264Realloc(h, dst, (unsigned int)(*dst_size + src_size));
        if (!dst)
            return NULL;
    }
    memcpy(dst + *dst_size, src, src_size);
    *dst_size += src_size;
    return dst;
}

H264ParserRetCode ParseH264Stream(H264ParserHandle handle, uint8* in_data, uint32 in_size,
                                  uint32 flags, uint32* consumed_size)

{
    H264ParserRetCode retCode = H264PARSER_SUCCESS;
    H264Parser* parser = (H264Parser*)handle;
    H264Context* h = &(parser->h264_ctx);
    int ret = 0;
    uint32 nal_len = 0;
    uint8* data = in_data;
    uint32 size = in_size;

    if (NULL == parser)
        return H264PARSER_ERROR;

    *consumed_size = 0;
    if (flags != SEARCH_KEY_FRMAE && h->add_sei_pos && h->sei_pos.size > 0 &&
        parser->outputSize == 0) {
        // need output sei position data after output last video buffer
        goto get_one_frame;
    }
    while (*consumed_size < in_size) {
        ret = H264FindOneNALU(parser, data, size, &nal_len);
        *consumed_size += nal_len;
        if (ret < 0 || parser->is_get_half_nalu == 1) {  // last nalu is half nalu
            parser->pHalfNALUBuffer =
                    H264CopyBuffer(h, parser->pHalfNALUBuffer, &(parser->halfNALUSize),
                                   H264_NALU_BAK_SIZE, data, nal_len);
            if (NULL == parser->pHalfNALUBuffer)
                return H264PARSER_ERROR;
        }
        if (0 == ret || SEARCH_KEY_FRMAE == flags) {
            if (parser->is_get_half_nalu == 1 && parser->halfNALUSize > 4) {
                H264ParseOneNALU(parser, parser->pHalfNALUBuffer + 4, parser->halfNALUSize - 4);
                memset(parser->pHalfNALUBuffer, 0, parser->halfNALUSize);
                parser->halfNALUSize = parser->is_get_half_nalu = 0;
            } else if (nal_len > 4)
                H264ParseOneNALU(parser, data + 4, nal_len - 4);
        }

        if (flags != SEARCH_KEY_FRMAE)
            parser->outputSize += nal_len;

        if (parser->is_find_frame_end) {
            // find one frame/field
            if (h->frame_num < 0)
                h->key_frame = 0;
            parser->is_find_frame_end = h->is_skip_parse_nal = 0;
            h->frame_num = -1;
            if (h->picture_structure == PICT_FRAME ||
                (h->field_count == 1 && h->picture_structure == PICT_BOTTOM_FIELD)) {
                if (h->is_first_field_I) {
                    h->pict_type = I_TYPE;
                    h->key_frame = 1;
                }
                h->field_count = h->is_first_field_I = 0;
                goto get_one_frame;
            } else if (h->picture_structure == PICT_TOP_FIELD) {
                h->field_count++;
                if (h->pict_type == I_TYPE)
                    h->is_first_field_I = 1;
            } else
                goto get_one_frame;
        }

        size -= nal_len;
        data += nal_len;
        nal_len = 0;
    }

    // in search key frame mode, no need to get the whole frame,
    // parser just want to locate key frame
    if (flags == SEARCH_KEY_FRMAE) {
        goto get_one_frame;
    } else {
        return retCode;
    }

get_one_frame:
    if (flags == SEARCH_KEY_FRMAE) {
        if (h->key_frame) {
            h->is_skip_parse_nal = 0;
        } else {
            return H264PARSER_SUCCESS;  // SEARCH_KEY_FRMAE only return key frames.
        }
    }
    parser->is_next_first_nalu = 1;
    parser->state = 0xFF;
    return H264PARSER_HAS_ONE_FRAME;
}

H264ParserRetCode ParseH264Field(H264ParserHandle handle, uint8* in_data, uint32 in_size,
                                 uint32* is_sync) {
    H264ParserRetCode retCode = H264PARSER_HAS_ONE_FRAME;
    H264Parser* parser = (H264Parser*)handle;
    H264Context* h = &(parser->h264_ctx);
    int ret = 0;
    uint32 nal_len = 0;
    uint8* data = in_data;
    uint32 size = in_size;
    uint32 consumed_size = 0;

    h->sliceNum = 0;

    /* return immediately for non-interlaced stream */
    if (h->sps && h->sps->frame_mbs_only_flag)
        return H264PARSER_HAS_ONE_FRAME;

    while (consumed_size < in_size) {
        ret = H264FindOneNALU(parser, data, size, &nal_len);
        if (ret < 0 || nal_len > size) {
            parser->nal_start_code_found = 0;
            parser->is_get_half_nalu = 0;
            return H264PARSER_HAS_ONE_FRAME;
        }

        if (nal_len > 4)
            H264ParseOneNALU(parser, data + 4, nal_len - 4);

        if (h->is_skip_parse_nal) {
            // the main frame parameters have been parsed, no need to parse the rest
            h->is_skip_parse_nal = 0;
            if (h->frame_num < 0)
                h->key_frame = 0;

            if (h->anSliceStruct[0] == PICT_FRAME) {
                retCode = H264PARSER_HAS_ONE_FRAME;
            } else if (((h->anSliceStruct[0] == PICT_TOP_FIELD) &&
                        (h->anSliceStruct[h->sliceNum - 1] == PICT_BOTTOM_FIELD)) ||
                       ((h->anSliceStruct[0] == PICT_BOTTOM_FIELD) &&
                        (h->anSliceStruct[h->sliceNum - 1] == PICT_TOP_FIELD))) {
                retCode = H264PARSER_HAS_ONE_FRAME;

            } else if (((h->anSliceStruct[0] == PICT_TOP_FIELD) &&
                        (h->anSliceStruct[h->sliceNum - 1] == PICT_TOP_FIELD)) ||
                       ((h->anSliceStruct[0] == PICT_BOTTOM_FIELD) &&
                        (h->anSliceStruct[h->sliceNum - 1] == PICT_BOTTOM_FIELD))) {
                h->field_count = (h->field_count + 1) % 2;

                if (h->field_count == 1) {
                    retCode = H264PARSER_SUCCESS;
                } else {
                    retCode = H264PARSER_HAS_ONE_FRAME;
                }
            }
        }
        consumed_size += nal_len;
        size -= nal_len;
        data += nal_len;
        nal_len = 0;
    }

    if (retCode == H264PARSER_HAS_ONE_FRAME) {
        *is_sync = h->key_frame;
        h->key_frame = 0;
        h->field_count = 0;
        h->frame_num = -1;
    }
    return retCode;
}

H264ParserRetCode ParseH264CodecDataFrame(H264ParserHandle handle, uint8* codecdata, uint32 size,
                                          H264HeaderInfo* header) {
    H264Parser* parser = (H264Parser*)handle;
    H264Context* h = &(parser->h264_ctx);
    uint32 offset = 0;
    h->sliceNum = 0;
    uint32 min_size = 8;

    if (codecdata == NULL || header == NULL)
        return H264PARSER_ERROR;

    parser->avcc = FALSE;
    parser->nal_start_code_found = 0;
    h->is_skip_parse_nal = 0;

    if ((size >= min_size) && (codecdata[0] == 0x01) && (codecdata[4] >> 2 == 0x3F) &&
        (codecdata[5] >> 5 == 0x7)) {
        /* AVCC format
         * ISO/IEC 14496-15 NAL unit structured video
         *      bits        data
         *      8           version (always 0x01)
         *      8           avc profile
         *      8           avc compatibility
         *      8           avc level
         *      6           reserved (all bits on)
         *      2           NALULengthSizeMinus1
         *      3           reserved (all bits on)
         *      5           number of SPS NALUs (usually 1)
         *      16          SPS size
         *      N           variable SPS NALU data
         *      8           number of PPS NALUs
         *      16          PPS size
         *      N           variable PPS NALU data
         */
        uint32 sps_num, sps_size;
        uint32 pps_num, pps_size;
        uint32 i;

        parser->avcc = TRUE;

        sps_num = codecdata[5] & 0x1F;
        offset += 6;

        for (i = 0; i < sps_num; i++) {
            sps_size = (codecdata[offset] << 8 | codecdata[offset + 1]);
            offset += 2;
            if (sps_size + offset > size) {
                return H264PARSER_ERROR;
            }
            H264ParseOneNALU(parser, codecdata + offset, sps_size);
            offset += sps_size;
        }

        pps_num = codecdata[offset];
        offset++;

        for (i = 0; i < pps_num; i++) {
            pps_size = (codecdata[offset] << 8 | codecdata[offset + 1]);
            offset += 2;
            if (pps_size + offset > size) {
                return H264PARSER_ERROR;
            }
            H264ParseOneNALU(parser, codecdata + offset, pps_size);
            offset += pps_size;
        }
    } else {
        while (offset < size) {
            int end = H264FindNalStartCode(parser, codecdata, offset, size);
            if (end < 0) {
                break;
            }

            offset += (end - offset);
            if (offset + 4 > size)
                offset = size - 4;

            if (offset < size - 4) {
                (void)H264ParseOneNALU(parser, codecdata + offset + 4, size - offset - 4);
                offset += 4;
            }
        }
    }

    if (h->sps) {
        SPS* sps = h->sps;
        header->width = sps->pic_width_in_mbs << 4;
        header->height = sps->pic_height_in_map_units << 4;
        if (0 == sps->frame_mbs_only_flag) {
            header->height <<= 1;
            header->scanType = VIDEO_SCAN_INTERLACED;
        } else {
            header->scanType = VIDEO_SCAN_PROGRESSIVE;
        }

        if (sps->time_scale != 0 && sps->num_units_in_tick != 0) {
            header->frameNumerator = sps->time_scale / 2;
            header->frameDenominator = sps->num_units_in_tick;
        } else {
            header->frameNumerator = 0;
            header->frameDenominator = 1;
        }

        // TODO: refer to H264 spec: Table D-1 Interpretation of pic_struct
        // if (sps->pic_struct_present_flag) {
        //     if (sps->pic_struct > 0 && sps->pic_struct < 7)
        //         header->scanType = VIDEO_SCAN_INTERLACED;
        // } else {
        //     if (h->picture_structure == PICT_TOP_FIELD || h->picture_structure ==
        //     PICT_BOTTOM_FIELD)
        //         header->scanType = VIDEO_SCAN_INTERLACED;
        // }

        // possible switching between frame and field
        // if (sps->mb_adaptive_frame_field_flag)
        //     header->interlaced = 1;

        return H264PARSER_SUCCESS;
    }

    return H264PARSER_ERROR;
}

H264ParserRetCode GetH264SeiPositionData(H264Parser* parser, FrameInfo* frame) {
    H264Context* h;
    if (NULL == parser)
        return H264PARSER_ERROR;

    h = &(parser->h264_ctx);

    memset(parser->pOutputBuffer, 0, parser->bufferSize);
    memcpy(parser->pOutputBuffer, &h->sei_pos, sizeof(SeiPosition));

    frame->buffer = parser->pOutputBuffer;
    frame->alloc_size = parser->bufferSize;
    frame->data_size = sizeof(SeiPosition);
    frame->pts = PARSER_UNKNOWN_TIME_STAMP;
    frame->flags = FLAG_SAMPLE_H264_SEI_POS_DATA;

    parser->outputSize = 0;
    memset(&h->sei_pos, 0, sizeof(SeiPosition));
    return H264PARSER_SUCCESS;
}

H264ParserRetCode GetH264FrameBuffer(H264ParserHandle handle, FrameInfo* frame) {
    H264Parser* parser = (H264Parser*)handle;
    H264Context* h = NULL;
    H264ParserRetCode retCode = H264PARSER_SUCCESS;
    bool findSei = FALSE;

    if (NULL == parser)
        return H264PARSER_ERROR;

    h = &(parser->h264_ctx);

    findSei = (h->add_sei_pos && h->sei_pos.size > 0);

    if (findSei && parser->outputSize == 0) {
        return GetH264SeiPositionData(parser, frame);
    }

    frame->flags = 0;
    if (parser->h264_ctx.key_frame)
        frame->flags |= FLAG_SYNC_SAMPLE;
    if (findSei)
        frame->flags |=
                FLAG_SAMPLE_NOT_FINISHED;  // video frame is not finished, need to wait sei pos data

    parser->outputSize = 0;

    return retCode;
}

H264ParserRetCode FindH264KeyFrame(H264ParserHandle handle, uint8* data, uint32 size) {
    uint32 i = 2;
    uint32 nal_type;
    bool findSlice = FALSE, findSei = FALSE;
    H264ParserRetCode ret = H264PARSER_SUCCESS;
    H264Parser* parser = (H264Parser*)handle;
    H264Context* h;

    if (NULL == parser)
        return H264PARSER_ERROR;

    h = &(parser->h264_ctx);

    while (i < size) {
        if (data[i] == 0)
            i++;
        else if (data[i] == 1 && data[i - 1] == 0 && data[i - 2] == 0) {
            // find one nal, only parse IDR/SLICE/SEI to detect if it is key frame
            nal_type = data[i + 1] & 0x1F;
            if (nal_type == NAL_SEI) {
                init_get_bits(&h->gb, data + i + 2, 8 * (size - i - 1));
                H264ParseSei(h);
                if (h->sei_recovery_frame_cnt >= 0) {
                    return ret;
                }
                findSei = TRUE;
            } else if (nal_type == NAL_IDR_SLICE) {
                return ret;
            } else if (nal_type == NAL_SLICE) {
                int32 slice_type;
                init_get_bits(&h->gb, data + i + 2, 8 * (size - i - 1));
                get_ue_golomb(&h->gb);
                slice_type = get_ue_golomb_31(&h->gb);
                h->pict_type = GOLOMB_TO_PICT_TYPE[slice_type % 5];
                if (I_TYPE == h->pict_type) {
                    return ret;
                }
                findSlice = TRUE;
            }
            if (findSei && findSlice)
                break;
            i++;
        } else
            i += 2;
    }

    return H264PARSER_ERROR;
}
