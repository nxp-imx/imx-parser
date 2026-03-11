/*
 ***********************************************************************
 * Copyright 2005-2006 by Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#ifndef _MP3_PARSE_
#define _MP3_PARSE_
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
    MP3_OK,
    MP3_ERR
} mp3_err;

typedef enum {
    MAD_MODE_SINGLE_CHANNEL = 3, /* single channel */
    MAD_MODE_DUAL_CHANNEL = 2,   /* dual channel */
    MAD_MODE_JOINT_STEREO = 1,   /* joint (MS/intensity) stereo */
    MAD_MODE_STEREO = 0          /* normal LR stereo */
} MAD_MODE_T;

enum {
    FLAG_SUCCESS,
    FLAG_NEEDMORE_DATA
};

FRAME_INFO mp3_parser_parse_frame_header(char* frame_buffer, int buf_size, FRAME_INFO* in_info);
mp3_err mp3_check_next_frame_header(char* frame_buffer, int ref_mpeg_version, int ref_layer,
                                    int ref_sampling_frequency);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
