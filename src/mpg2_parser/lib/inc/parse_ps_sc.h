/*
 ***********************************************************************
 * Copyright 2005-2013, Freescale Semiconductor, Inc.
 * Copyright 2017-2022, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */
/*****************************************************************************
 * demux_ps_sc.h
 *
 * Description:
 * Define the start code for PS demuxer.
 ****************************************************************************/
#ifndef FSL_MPG_DEMUX_SC_H
#define FSL_MPG_DEMUX_SC_H

#include "mpeg2_parser_api.h"
#include "mpeg2_parser_internal.h"

#define PS_SC_PREFIX_MASK 0xFFFFFF00
#define PS_SC_PREFIX 0x00000100
#define PS_ID_MASK 0x000000FF

#define MPEG2_SEQUENCE_HEADER 0x000001B3
#define MPEG2_SEQUENCE_EXTENSION_HEADER 0x000001B5

#define IS_MPEG2SH(x) (x == MPEG2_SEQUENCE_HEADER ? 1 : 0)

#define IS_SC(x) (((x) & PS_SC_PREFIX_MASK) == PS_SC_PREFIX ? 1 : 0)
#define PS_ID(x) ((x) & PS_ID_MASK)

#define PS_PACK_HEADER 0x000001BA
#define PS_SYSTEM_HEADER 0x000001BB

#define PS_PACK_ID 0xBA
#define PS_SYSTEM_HEADER_ID 0xBB
#define PS_DIRECTORY_ID 0xFF
#define PS_PSM_ID 0xBC
#define PS_ECM_ID 0xF0
#define PS_EMM_ID 0xF1
#define PS_PRIV1 0xBD
#define PS_PRIV2 0xBF
#define PS_PADDING_STREAM_ID 0xBE
#define PS_ITUT_H2221_E_ID 0xF8
#define AC3_BLUERAY 0xFD
#define PS_EOS 0xB9
#define MPEG_SH 0xB3

#define IS_NO_PES_HEADER(x)                                                    \
    (PS_ID(x) == PS_PADDING_STREAM_ID || PS_ID(x) == PS_ITUT_H2221_E_ID ||     \
     PS_ID(x) == PS_PSM_ID || PS_ID(x) == PS_PRIV2 || PS_ID(x) == PS_ECM_ID || \
     PS_ID(x) == PS_EMM_ID || PS_ID(x) == PS_DIRECTORY_ID)

#define IS_MPG2_VIDEO_ID(x) (((x) & 0xF0) == 0xE0)

#define H264_NAL_MASK 0xFFFFFF1F
#define H264_SPS_NAL 0x00000107
// #define H264_NAL_MASK    0xFFFFFF00
// #define H264_SPS_NAL       0x00000100
#define IS_H264_SPS(x) ((x & H264_NAL_MASK) == H264_SPS_NAL ? 1 : 0)
#define IS_MP4_SH(x) ((x == 0x01B0) ? 1 : 0)
#define IS_MP4_VOP(x) ((x == 0x01B6) ? 1 : 0)
#define IS_MP4_PREFIX(x) ((x & 0xFFFFFF) == 0x01 ? 1 : 0)

#define PS_AUDIO_ID_MIN 0xC0
#define PS_AUDIO_ID_MAX 0xDF

#define PS_VIDEO_ID_MIN 0xE0
#define PS_VIDEO_ID_MAX 0xEF

#define PS_PES_ID_MIN 0xBC
#define PS_PES_ID_MAX 0xFF

#define IS_AUDIO_PES(x) ((PS_ID(x) >= PS_AUDIO_ID_MIN && PS_ID(x) <= PS_AUDIO_ID_MAX) ? 1 : 0)
#define IS_VIDEO_PES(x) ((PS_ID(x) >= PS_VIDEO_ID_MIN && PS_ID(x) <= PS_VIDEO_ID_MAX) ? 1 : 0)
// #define IS_H264_PES(x)        ((PS_ID(x)>=PS_H264_ID_MIN && PS_ID(x)<=PS_H264_ID_MAX)?1:0)
#define IS_MEDIA_PES(x) ((PS_ID(x) >= PS_AUDIO_ID_MIN && PS_ID(x) <= PS_VIDEO_ID_MAX) ? 1 : 0)
#define IS_PES(x) ((PS_ID(x) >= PS_PES_ID_MIN) ? 1 : 0)
#define IS_SH(x) ((PS_SYSTEM_HEADER == (x)) ? 1 : 0)
#define IS_PRIV1(x) ((PS_PRIV1 == PS_ID(x)) ? 1 : 0)
#define IS_AC3_BLUERAY(x) ((AC3_BLUERAY == PS_ID(x)) ? 1 : 0)

/*DVD audio sub-stream   */
#define SUBTITLE_ID_MIN 0x20
#define SUBTITLE_ID_MAX 0x3F

#define AC3_ID_MIN1 0x80
#define AC3_ID_MAX1 0x87
#define AC3_ID_MIN2 0xC0
#define AC3_ID_MAX2 0xCF

#define DTS_ID_MIN1 0x88
#define DTS_ID_MAX1 0x8F
#define DTS_ID_MIN2 0x98
#define DTS_ID_MAX2 0x9F

#define PCM_ID_MIN 0xA0
#define PCM_ID_MAX 0xA7

#define IS_SUB(x) ((((x) >= SUBTITLE_ID_MIN) && ((x) <= SUBTITLE_ID_MAX)) ? 1 : 0)
#define IS_AC3(x)                                       \
    (((((x) >= AC3_ID_MIN1) && ((x) <= AC3_ID_MAX1)) || \
      (((x) >= AC3_ID_MIN2) && ((x) <= AC3_ID_MAX2)))   \
             ? 1                                        \
             : 0)
#define IS_DTS(x)                                       \
    (((((x) >= DTS_ID_MIN1) && ((x) <= DTS_ID_MAX1)) || \
      (((x) >= DTS_ID_MIN2) && ((x) <= DTS_ID_MAX2)))   \
             ? 1                                        \
             : 0)
#define IS_PCM(x) ((((x) >= PCM_ID_MIN) && ((x) <= PCM_ID_MAX)) ? 1 : 0)

MPEG2_PARSER_ERROR_CODE MPEG2ParserProbe(MPEG2ObjectPtr pDemuxer);
MPEG2_PARSER_ERROR_CODE RemapProgram(MPEG2ObjectPtr pDemuxer);

#endif /*FSL_MPG_DEMUX_SC_H  */
