/*
***********************************************************************
* Copyright (c) 2005-2011 by Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef FSL_MP3_HEADER_INCLUDED
#define FSL_MP3_HEADER_INCLUDED

// #define MP3_SYNC      0xFFF
#define MP3_SYNC 0xFFE               /* 11 bits successiv '1' */
#define MPEG_AUDIO_FRAME_HEAD_SIZE 4 /* size of mpeg audio frame header size, in bytes */

/* mpeg audio context */
typedef struct {
    /* audio properites */
    int32 layer;
    int32 nb_channels;
    int32 bitrate;            /* bps */
    int32 sampling_frequency; /* in HZ */
    int32 nb_samples_per_frame;
    int32 frame_size; /* in bytes */

    /* file properties */
    int64 first_frame_offset; /* offset of 1st frame header, in bytes */
    int64 media_size; /* how many bytes are in the file, excluding the bytes before 1st valid audio
                         frame header */

    bool cbr; /* TRUE for CBR, FALSE for VBR */
    /* for VBR mp3 */
    int32 nb_frames; /* how many frames are in the file */
    uint8* toc;      /* if toc is found in VBR header, malloc this table */
} mp3_audio_context;

/* return the audio stream properties & mp3 file context */
int32 mpa_parse_frame_header(char* frame_buffer, int buf_size, bool check_two_frames,
                             mp3_audio_context* mpa_context);

#endif /* FSL_MP3_HEADER_INCLUDED */
