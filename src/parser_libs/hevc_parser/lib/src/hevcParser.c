/*
 ***********************************************************************
 * Copyright (c) 2015-2016, Freescale Semiconductor, Inc.
 * Copyright 2017, 2024-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsl_types.h"
#include "fsl_parser.h"
#include "hevcParserInner.h"
#include "hevcparser.h"
#include "utils.h"

#ifdef PARSER_HEVC_DBG
#define PARSER_HEVC_LOG printf
#define PARSER_HEVC_ERR printf
#else
#define PARSER_HEVC_LOG(...)
#define PARSER_HEVC_ERR(...)
#endif
#define ASSERT(exp)                                                                   \
    if (!(exp)) {                                                                     \
        PARSER_HEVC_ERR("%s: %d : assert condition !!!\r\n", __FUNCTION__, __LINE__); \
    }

#define HEVC_NALU_BAK_SIZE (1024 * 1024)
#define HEVC_NALU_STARTCODE_SIZE 4
#define MAX_HEVC_VIDEO_HEADER_SIZE 512

#define HEVC_NAL_START_CODE 0x000001
#define HEVC_NAL_START_CODE_LEN 4

static void decode_profile_tier_level(GetBitContext* gb, PTLCommon* ptr) {
    int j;
    if (!gb || !ptr)
        return;

    ptr->profile_space = get_nbits(gb, 2);
    ptr->tier_flag = get_nbits(gb, 1);
    ptr->profile_idc = get_nbits(gb, 5);

    for (j = 0; j < 32; j++) ptr->profile_compatibility_flag[j] = get_bits1(gb);

    ptr->progressive_source_flag = get_bits1(gb);
    ptr->interlaced_source_flag = get_bits1(gb);
    ptr->non_packed_constraint_flag = get_bits1(gb);
    ptr->frame_only_constraint_flag = get_bits1(gb);

    skip_nbits(gb, 44);
}

static void HEVCParsePTL(GetBitContext* gb, PTL* ptr, int max_num_sub_layers) {
    int i;
    int max_layer_index = max_num_sub_layers - 1;

    if (!gb || !ptr)
        return;

    decode_profile_tier_level(gb, &ptr->general_ptl);
    ptr->general_ptl.level_idc = get_nbits(gb, 8);

    for (i = 0; i < max_layer_index; i++) {
        ptr->sub_layer_profile_present_flag[i] = get_bits1(gb);
        ptr->sub_layer_level_present_flag[i] = get_bits1(gb);
    }
    if (max_layer_index > 0) {
        for (i = max_layer_index; i < 8; i++) skip_nbits(gb, 2);
    }
    for (i = 0; i < max_layer_index; i++) {
        if (ptr->sub_layer_profile_present_flag[i])
            decode_profile_tier_level(gb, &ptr->sub_layer_ptl[i]);
        if (ptr->sub_layer_level_present_flag[i])
            ptr->sub_layer_ptl[i].level_idc = get_nbits(gb, 8);
    }
}

static void scaling_list_data(GetBitContext* gb) {
    int sizeId, matrixId;

    for (sizeId = 0; sizeId < 4; sizeId++) {
        for (matrixId = 0; matrixId < 6; matrixId += (sizeId == 3) ? 3 : 1) {
            /* scaling_list_pred_mode_flag[sizeId][matrixId] */
            if (!get_bits1(gb)) {
                get_ue_golomb_long(
                        gb); /* scaling_list_pred_matrix_id_delta[ sizeId ][ matrixId ] */
            } else {
                int i;
                int coefNum = HEVCMIN(64, 1 << (4 + (sizeId << 1)));

                if (sizeId > 1) {
                    get_se_golomb(gb); /* scaling_list_dc_coef_minus8[ sizeId − 2 ][ matrixId ] */
                }
                for (i = 0; i < coefNum; i++) {
                    get_se_golomb(gb); /* scaling_list_delta_coef */
                }
            }
        }
    }
}

#define EXTENDED_SAR 255

void vui_parameters(GetBitContext* gb, uint32* num, uint32* den) {
    /* aspect_ratio_info_present_flag */
    if (get_bits1(gb)) {
        /* aspect_ratio_idc */
        if (get_nbits(gb, 8) == EXTENDED_SAR) {
            skip_nbits(gb, 16); /* sar_width */
            skip_nbits(gb, 16); /* sar_height */
        }
    }
    /* overscan_info_present_flag */
    if (get_bits1(gb)) {
        skip_nbits(gb, 1); /* overscan_appropriate_flag */
    }

    /* video_signal_type_present_flag */
    if (get_bits1(gb)) {
        skip_nbits(gb, 3); /* video_format */
        skip_nbits(gb, 1); /* video_full_range */
        /* colour_description_present_flag */
        if (get_bits1(gb)) {
            skip_nbits(gb, 8); /* colour_primaries */
            skip_nbits(gb, 8); /* transfer_characteristics */
            skip_nbits(gb, 8); /* matrix_coeffs */
        }
    }
    /* chroma_loc_info_present_flag */
    if (get_bits1(gb)) {
        get_ue_golomb_long(gb); /* chroma_sample_loc_type_top_field */
        get_ue_golomb_long(gb); /* chroma_sample_loc_type_top_field */
    }

    skip_nbits(gb, 1); /* neutral_chroma_indication_flag */
    skip_nbits(gb, 1); /* field_seq_flag */
    skip_nbits(gb, 1); /* frame_field_info_present_flag */
    /* default_display_window_flag */
    if (get_bits1(gb)) {
        get_ue_golomb_long(gb); /* def_disp_win_left_offset */
        get_ue_golomb_long(gb); /* def_disp_win_right_offset */
        get_ue_golomb_long(gb); /* def_disp_win_top_offset */
        get_ue_golomb_long(gb); /* def_disp_win_bottom_offset */
    }
    /* vui_timing_info_present_flag */
    if (get_bits1(gb)) {
        *den = get_nbits_long(gb, 32); /* vui_num_units_in_tick */
        *num = get_nbits_long(gb, 32); /* vui_time_scale */
    }

    return;
}

static uint32 removePaddingData(uint8* src, uint8* dst, uint32 size) {
    uint32 si = 0, di = 0;

    while (si + 2 < size) {
        // remove escapes (very rare 1:2^22)
        if (src[si + 2] > 3) {
            dst[di++] = src[si++];
            dst[di++] = src[si++];
        } else if (src[si] == 0 && src[si + 1] == 0) {
            if (src[si + 2] == 3) {  // escape
                dst[di++] = 0;
                dst[di++] = 0;
                si += 3;
                continue;
            }
        }
        dst[di++] = src[si++];
    }

    while (si < size) {
        dst[di++] = src[si++];
    }

    return di;
}

int HevcParseVideoParameterSet(uint8* pData, int size, HevcVideoHeader* pVHeader) {
    uint8* dst = NULL;
    uint32 len = 0;
    GetBitContext gb;
    PTL ptl;
    int i = 0, j = 0;
    int vps_max_sub_layers_minus1;
    int vps_sub_layer_ordering_info_present_flag;
    int vps_max_layer_id;
    int vps_num_layer_sets_minus1;
    int vps_timing_info_present_flag;

    if (0 == size)
        return 1;

    dst = (uint8*)malloc(size);
    if (!dst)
        return 1;

    len = removePaddingData(pData, dst, size);
    memset(&gb, 0, sizeof(GetBitContext));
    init_get_bits(&gb, dst, 8 * len);

    skip_nbits(&gb, 6);
    vps_max_sub_layers_minus1 = get_nbits(&gb, 6);
    skip_nbits(&gb, 20);

    HEVCParsePTL(&gb, &ptl, vps_max_sub_layers_minus1 + 1);
    vps_sub_layer_ordering_info_present_flag = get_bits1(&gb);
    i = vps_sub_layer_ordering_info_present_flag ? 0 : vps_max_sub_layers_minus1;
    for (; i <= vps_max_sub_layers_minus1; i++) {
        get_ue_golomb_long(&gb); /* vps_max_dec_pic_buffering_minus1[i] */
        get_ue_golomb_long(&gb); /* vps_max_num_reorder_pics[i] */
        get_ue_golomb_long(&gb); /* vps_max_latency_increase_plus1[i] */
    }

    vps_max_layer_id = get_nbits(&gb, 6);
    vps_num_layer_sets_minus1 = get_ue_golomb_long(&gb);
    for (i = 1; i <= vps_num_layer_sets_minus1; i++) {
        for (j = 0; j <= vps_max_layer_id; j++) {
            skip_nbits(&gb, 1); /* layer_id_included_flag[i][j] */
        }
    }

    vps_timing_info_present_flag = get_bits1(&gb);
    if (vps_timing_info_present_flag) {
        pVHeader->FRDenominator = get_nbits_long(&gb, 32); /* vps_num_units_in_tick */
        pVHeader->FRNumerator = get_nbits_long(&gb, 32);   /* vps_time_scale */
    }

    free(dst);
    return 0;
}

int HevcParseSeqParameterSet(uint8* pData, int size, HevcVideoHeader* pVHeader) {
    int i = 0, len = 0, ret = 0;
    uint8* dst;
    SPS sps;
    int sps_id = 0;
    int num_short_term_ref_pic_sets;
    GetBitContext gb;
    PTL ptl;

    if (0 == size)
        return ret;

    dst = (uint8*)malloc(size);
    if (!dst)
        return ret;

    len = removePaddingData(pData, dst, size);
    memset(&gb, 0, sizeof(GetBitContext));
    init_get_bits(&gb, dst, 8 * len);

    sps.vps_id = get_nbits(&gb, 4);
    if (sps.vps_id >= MAX_VPS_COUNT)
        goto bail;

    sps.max_sub_layers = get_nbits(&gb, 3);
    sps.max_sub_layers++;
    if (sps.max_sub_layers > MAX_SUB_LAYERS)
        goto bail;

    skip_nbits(&gb, 1);
    memset(&ptl, 0, sizeof(PTL));
    HEVCParsePTL(&gb, &ptl, sps.max_sub_layers);

    sps_id = get_ue_golomb_long(&gb);
    if (sps_id >= MAX_SPS_COUNT)
        goto bail;
    sps.chroma_format_idc = get_ue_golomb_long(&gb);
    if (sps.chroma_format_idc != 1)
        goto bail;
    if (sps.chroma_format_idc == 3) {
        sps.separate_colour_plane_flag = get_bits1(&gb);
    }

    sps.width = get_ue_golomb_long(&gb);
    sps.height = get_ue_golomb_long(&gb);
    if ((int)sps.width <= 0 || (int)sps.height <= 0 ||
        (sps.width + 128) * (uint64)(sps.height + 128) >= 0x7FFFFFFF / 8)
        goto bail;

    pVHeader->HSize = sps.width;
    pVHeader->VSize = sps.height;

    /* conformance_window_flag */
    if (get_bits1(&gb)) {
        sps.conf_win_left_offset = get_ue_golomb_long(&gb);
        sps.conf_win_right_offset = get_ue_golomb_long(&gb);
        sps.conf_win_top_offset = get_ue_golomb_long(&gb);
        sps.conf_win_bottom_offset = get_ue_golomb_long(&gb);
    }

    sps.bit_depth_luma = get_ue_golomb_long(&gb) + 8;
    sps.bit_depth_chroma = get_ue_golomb_long(&gb) + 8;
    sps.log2_max_pic_order_cnt_lsb = get_ue_golomb_long(&gb) + 4;

    /* sps_sub_layer_ordering_info_present_flag */
    i = (get_bits1(&gb) ? 0 : sps.max_sub_layers - 1);
    for (; i < sps.max_sub_layers; i++) {
        get_ue_golomb_long(&gb); /* sps_max_dec_pic_buffering_minus1[i] */
        get_ue_golomb_long(&gb); /* sps_max_num_reorder_pics[i] */
        get_ue_golomb_long(&gb); /* sps_max_latency_increase_plus1[i] */
    }

    get_ue_golomb_long(&gb);
    get_ue_golomb_long(&gb);
    get_ue_golomb_long(&gb);
    get_ue_golomb_long(&gb);
    get_ue_golomb_long(&gb);
    get_ue_golomb_long(&gb);

    /* scaling_list_enabled_flag */
    if (get_bits1(&gb)) {
        /* sps_scaling_list_data_present_flag */
        if (get_bits1(&gb)) {
            scaling_list_data(&gb);
        }
    }

    /* amp_enabled_flag */
    /* sample_adaptive_offset_enabled_flag */
    skip_nbits(&gb, 2);
    /* pcm_enabled_flag */
    if (get_bits1(&gb)) {
        skip_nbits(&gb, 4);      /* pcm_sample_bit_depth_luma_minus1 */
        skip_nbits(&gb, 4);      /* pcm_sample_bit_depth_chroma_minus1 */
        get_ue_golomb_long(&gb); /* og2_min_pcm_luma_coding_block_size_minus3 */
        get_ue_golomb_long(&gb); /* og2_diff_max_min_pcm_luma_coding_block_size */
        skip_nbits(&gb, 1);      /* pcm_loop_filter_disabled_flag */
    }

    num_short_term_ref_pic_sets = get_ue_golomb_long(&gb);
    if (num_short_term_ref_pic_sets > 0) {
        // todo: st_ref_pic_set(i);
        ret = 0;
        goto bail;
    }

    /* long_term_ref_pics_present_flag */
    if (get_bits1(&gb)) {
        int num_long_term_ref_pics_sps = get_ue_golomb_long(&gb);
        for (i = 0; i < num_long_term_ref_pics_sps; i++) {
            skip_nbits(&gb, sps.log2_max_pic_order_cnt_lsb); /* lt_ref_pic_poc_lsb_sps[ i ] */
            skip_nbits(&gb, 1);                              /* used_by_curr_pic_lt_sps_flag[ i ] */
        }
    }

    skip_nbits(&gb, 1); /* sps_temporal_mvp_enabled_flag */
    skip_nbits(&gb, 1); /* strong_intra_smoothing_enabled_flag */

    /* vui_parameters_present_flag */
    if (get_bits1(&gb)) {
        vui_parameters(&gb, &(pVHeader->FRNumerator), &(pVHeader->FRDenominator));
    }

    ret = 0;

bail:
    if (dst != NULL)
        free(dst);

    return ret;
}

int HevcParseVideoHeader(HevcVideoHeader* pVHeader, uint8* pData, int size) {
    int offset = 0;
    uint32 FourBytes = 0xFFFFFFFF;
    uint32 SPSFound = 0, VPSFound = 0;
    uint32 SPSLen = 0, VPSLen = 0;
    uint32 SPSOffset = 0, VPSOffset = 0;
    int ret = 0;

    pVHeader->FRDenominator = 1;
    pVHeader->FRNumerator = 0;

    do {
        FourBytes = (FourBytes << 8) | pData[offset];
        if (IS_STARTCODE(FourBytes)) {
            if (SPSFound && 0 == SPSLen) {
                SPSLen = offset - SPSOffset - 3;
            }
            if (VPSFound && 0 == VPSLen) {
                VPSLen = offset - VPSOffset - 3;
            }
        }
        if (IS_HEVC_SPS(FourBytes)) {
            SPSFound = 1;
            SPSOffset = offset + 2;
        } else if (IS_HEVC_VPS(FourBytes)) {
            VPSFound = 1;
            VPSOffset = offset + 2;
        }
    } while (++offset < size);

    if (SPSFound) {
        if (0 == SPSLen)
            SPSLen = size - SPSOffset;
        ret = HevcParseSeqParameterSet(pData + SPSOffset, SPSLen, pVHeader);
    }

    if (pVHeader->FRNumerator == 0 && VPSFound) {
        if (0 == VPSLen)
            VPSLen = size - VPSOffset;
        ret |= HevcParseVideoParameterSet(pData + VPSOffset, VPSLen, pVHeader);
    }

    return ret;
}

static int HevcFindNalStartCode(HevcParser* parser, const uint8_t* p, uint32 start, uint32 size) {
    uint32 i = start + 2;
    (void)parser;

    if (size < 2 + start)
        return -1;

    while (i < size) {
        if (p[i] == 0)
            i++;
        else if (p[i] == 1 && p[i - 1] == 0 && p[i - 2] == 0) {
            if (i >= 3 && p[i - 3] == 0)
                return i - 3;
            else
                return i < 2 ? 0 : (i - 2);
        } else {
            i += 2;
        }
    }

    return -1;
}

static int HevcFindOneNALU(HevcParser* parser, uint8* data, uint32 size, uint32* nal_len) {
    int end, first_slice_segment_in_pic_flag;
    uint32 type;

    if (!parser->nal_start_code_found || parser->is_get_half_nalu)
        end = HevcFindNalStartCode(parser, data, 0, size);
    else
        end = HevcFindNalStartCode(parser, data, 2, size);

    if (end < 0) {
        parser->is_get_half_nalu = 1;
        *nal_len = size;
        return -1;  // end not found
    } else {
        parser->nal_start_code_found = 1;
        parser->is_get_half_nalu = 0;
    }

    *nal_len = end;
    if (data[end] == 0 && data[end + 1] == 0 && data[end + 2] == 0 && data[end + 3] == 1)
        data += (end + 4);
    else
        data += (end + 3);
    type = (data[0] >> 1) & 0x3F;
    if ((type >= 41 && type <= 44) || (type >= 48 && type <= 55) ||
        (type >= VPS_NUT && type <= AUD_NUT) || type == PREFIX_SEI_NUT) {
        if (parser->is_find_frame_start) {
            parser->is_find_frame_end = 1;
            parser->is_find_frame_start = 0;
        }
    } else if (type <= RASL_R || (type >= BLA_W_LP && type <= CRA_NUT)) {
        first_slice_segment_in_pic_flag = data[2];
        if (first_slice_segment_in_pic_flag) {
            if (!parser->is_find_frame_start) {
                parser->is_find_frame_start = 1;
                parser->key_frame = (type >= BLA_W_LP && type <= CRA_NUT);
            } else {
                parser->is_find_frame_start = 0;
                parser->is_find_frame_end = 1;
            }
        }
    }

    return 0;
}

int CreateHevcParser(HevcParserHandle* pHandle, ParserMemoryOps* pMemOps) {
    HevcParser* parser = (HevcParser*)pMemOps->Malloc(sizeof(HevcParser));
    if (NULL == parser)
        return -1;
    memset(parser, 0, sizeof(HevcParser));
    parser->memOps = pMemOps;
    parser->state = 0xff;
    *pHandle = (HevcParserHandle)parser;
    return 0;
}

int DeleteHevcParser(HevcParserHandle handle) {
    HevcParser* parser = (HevcParser*)handle;

    parser->memOps->Free(parser);
    parser = NULL;

    return PARSER_SUCCESS;
}

int ResetHevcParser(HevcParserHandle handle) {
    HevcParser* parser = (HevcParser*)handle;

    parser->key_frame = parser->is_get_half_nalu = parser->is_find_frame_end =
            parser->is_find_frame_start = parser->nal_start_code_found = 0;

    return PARSER_SUCCESS;
}

#if 0
HevcParserRetCode SetHevcFrameBuffer(HevcParserHandle handle, FrameInfo *frame)
{
    HevcParser *parser = (HevcParser*)handle;

    if (parser == NULL)
        return -1;
    parser->pOutputBuffer = frame->buffer;
    parser->bufferSize = frame->alloc_size;
    parser->outputSize = frame->data_size;
    parser->curr_pts   = frame->pts;

    return PARSER_SUCCESS;

}
#endif

HevcParserRetCode GetHevcFrameBuffer(HevcParserHandle handle, FrameInfo* frame) {
    HevcParser* parser = (HevcParser*)handle;

    frame->flags = parser->key_frame ? FLAG_SYNC_SAMPLE : 0;
    parser->key_frame = 0;

    return HevcPARSER_SUCCESS;
}

HevcParserRetCode ParseHevcStream(HevcParserHandle handle, uint8* in_data, uint32 in_size,
                                  uint32 flags, uint32* consumed_size) {
    HevcParserRetCode ret = HevcPARSER_SUCCESS;
    HevcParser* parser = (HevcParser*)handle;
    uint8* buf = in_data;
    uint32 bufSize = in_size;
    uint32 nalLen;

    while (*consumed_size < in_size) {
        ret = HevcFindOneNALU(parser, buf, bufSize, &nalLen);
        *consumed_size += nalLen;
        if (SEARCH_KEY_FRMAE == flags && parser->key_frame) {
            goto BAK_FRAME;
        } else if (parser->is_find_frame_end) {
            goto BAK_FRAME;
        }
        buf += nalLen;
        bufSize -= nalLen;
    }

BAK_FRAME:
    if (SEARCH_KEY_FRMAE != flags) {
        if (parser->is_find_frame_end) {
            ret = HevcPARSER_HAS_ONE_FRAME;
            parser->is_find_frame_end = 0;
            parser->nal_start_code_found = 0;
        }
    } else {
        if (parser->key_frame) {
            ret = HevcPARSER_HAS_ONE_FRAME;
        }
        ResetHevcParser(handle);
    }

    return ret;
}

HevcParserRetCode FindHevcKeyFrame(HevcParserHandle handle, uint8* data, uint32 size) {
    HevcParserRetCode ret = HevcPARSER_ERROR;
    uint32 i = 2;
    uint32 nal_type = 0;

    (void)handle;

    while (i < size) {
        if (data[i] == 0)
            i++;
        else if (data[i] == 1 && data[i - 1] == 0 && data[i - 2] == 0) {
            if (i + 1 < size) {
                nal_type = (data[i + 1] >> 1) & 0x3F;
                if (nal_type >= BLA_W_LP && nal_type <= CRA_NUT)
                    return HevcPARSER_SUCCESS;
            }
            i += 2;
        } else {
            i += 2;
        }
    }

    return ret;
}
