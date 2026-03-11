/*
 ***********************************************************************
 * Copyright 2005-2013, Freescale Semiconductor, Inc.
 *
 * Copyright 2017-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */
/*****************************************************************************
 * demux_ts_defines.h
 * Description:
 * Define the macros for TS demuxer.
 *
 *
 ****************************************************************************/
#ifndef FSL_MPG_DEMUX_TS_DEFINES_H
#define FSL_MPG_DEMUX_TS_DEFINES_H

#define TS_PACKET_LENGTH 188
#define MIN_TS_STREAM_LEN (2 * 188 + 8)
#define TS_SYNC_BYTE 0x47
/* Scan 200 TS packet for right TS packet when seek */
#define SEEK_RESYNC_CNT 100
#define MIN_SCAN_STREAM_LEN 940 /* 5 ts packets */

/* default resolution */
#define VIDEO_WIDTH_DEFAULT 1920
#define VIDEO_HEIGHT_DEFAULT 1080

/*PID   */
#define PID_PAT 0x0000
#define PID_CAT 0x0001
#define PID_DT 0x0002
#define PID_NULL 0x1FFF

/*PSI table ID  */
#define TID_PAT 0x00
#define TID_CAT 0x01
#define TID_PMT 0x02
#define TID_DT 0x03

/*stream type in PMT  */
#define MPEG1_VIDEO 0x01
#define MPEG2_VIDEO 0x02
#define MPEG1_AUDIO 0x03
#define MPEG2_AUDIO 0x04
#define MPEG2_PRIVATEPES_ATSC 0x81
#define MPEG2_PRIVATEPES_DVB 0x06

#define MPEG2_H264 0x1B
#define MPEG2_HEVC 0x24
#define MPEG4_VIDEO 0x10

/*DVB BlueBook A038 Table 109*/
#define DVB_AC4 0x15
#define DVB_AUDIO_PRESELECTION 0x19
#define DVB_RESERVED_MAX 0x7F
#define DVB_DTS_HD 0x0E
#define DVB_DTS_UHD 0x21

/* AVS stream type*/
#define AVS_VIDEO 0x42
#define AVS_AUDIO 0x43
#define AVS_PRIVATE_DATE 0x44
#define AVS_PRIVATE_PES 0x45
#define AVS_AUX_DATA 0x46

#define AAC_LATM 0x11  // 13818-6 type B
#define WMV_V8 0xA0
#define WMV_V9
#define WMV_V9A
#define WMA_AUDIO

#define MPEG2_AAC 0x0F

// 0x80-0xFF User Private
#define MPEG2_AC3 0x81
#define MPEG2_LPCM 0x83
#define MPEG2_EAC3 0x87
#define MPEG2_DTS 0x8A

/*
 * DVB BlueBook A038
 * Table 12: Possible locations of descriptors
 */
#define MPEG2_DESC_DVB_AC3 0x6A
#define MPEG2_DESC_DVB_ENHANCED_AC3 0x7A
#define MPEG2_DESC_DVB_DTS 0x7B

/*
 * DVB BlueBook A038
 * Table 109: Possible locations of extended descriptors
 */
#define MPEG2_EXT_DESC_AC4 0X15
#define MPEG2_EXT_DESC_DTS_HD 0x0E
#define MPEG2_EXT_DESC_DTS_UHD 0x21

#endif /*FSL_MPG_DEMUX_TS_DEFINES_H   */
