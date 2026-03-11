/*
***********************************************************************
* Copyright 2009-2016, Freescale Semiconductor, Inc.
* Copyright 2017-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
#define EMBL_HEADER_MAX_SIZE (16 * 1024 * 1024)
#define ATTACHMENT_MAX_SIZE (10 * 1024 * 1024)
#define SUPPORT_PART_CLUSTER  // for case: MP4_1080p_30fps_10Mbps_AAC_**.mkv, cluster contain about
                              // 5 seconds data, the performance is poor
#define SUPPORT_CORRUPT_CLUSTER  // ENGR00141990:test7.mkv
#define SUPPORT_PRE_SCAN_CLUSTER
#define OPTIMIZATION_CLUSTER_ENTRY  // reduce the unit size for every blockgroup/simpleblock header

// #define TRY_FUZZY_SEEK            //fast seek: not support now !!!
#ifdef SUPPORT_PART_CLUSTER
// we divide the whole cluster parsing into several sub-cluster parsing
#define MAX_CLUSTER_BLOCK_CNT 50
#define MAX_CLUSTER_BLOCK_SIZE (1024 * 1024)
#define MAX_CLUSTER_DURATION_MS (500)        //(1000)
#define MAX_CLUSTER_DURATION_MS_FIRST (100)  //(1000)
#endif
#ifdef SUPPORT_CORRUPT_CLUSTER
// for bad cluster, we will skip it, but not return eos/error directly.
// we return eos/error only when the number of continuous bad clusters is bigger than 'maxcnt'
#define MAX_CONTINUOUS_BAD_CLUSTER (2)
#endif
#ifdef SUPPORT_PRE_SCAN_CLUSTER
// purpose:
//     -fast seek
//     -verify error duration in segment info
//     -implement seek -1: for those clips missing duration info
#define MAX_CLUSTER_CNT (5000)  // avoid overhead

#define CLUSTER_INTERVAL (1)

// revise movie duration: ENGR00141988:test2.mkv (4.75s => 47.5s)
#define REVISE_MOVIE_DURARION

// using original duration if difference is smaller than the threshold
#define ALLOWED_DURARION_ERROR_SECOND (5)

// skip duration revision if duration is bigger than the threshold
#define MAX_DURARION_THRESHOLD_MINUTE (1)
#endif

#define MAX_CLUSTER_UNIT_MS (150)

#define MAX_STREAM_PACKAGE_SIZE (100 * 1024 * 1024)
#define DEFAULT_AUDIO_SAMPLERATE (8000)

#include "matroska.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #define MKV_DEBUG
#ifdef MKV_DEBUG
#ifdef ANDROID

#define LOG_PRINTF LogOutput
#else
#define LOG_PRINTF printf
// #define LOG_PRINTF(...)
#endif
#define ASSERT(exp)                                                              \
    if (!(exp)) {                                                                \
        LOG_PRINTF("%s: %d : assert condition !!!\r\n", __FUNCTION__, __LINE__); \
    }
#else
#define LOG_PRINTF(...)
#define ASSERT(exp)
#endif

// #define MKV_TIMER
#ifdef MKV_TIMER
#include <sys/time.h>
#define MKV_TIMER_START() mkv_timer_start()
#define MKV_TIMER_STOP(str) mkv_timer_stop(str)
#define MKV_TIMER_LOG LOG_PRINTF
#else
#define MKV_TIMER_START()
#define MKV_TIMER_STOP(str)
#endif

#ifdef __WINCE
#define MKV_ERROR_LOG(...)
#define MKV_LOG(...)
#else
#define MKV_ERROR_LOG LOG_PRINTF
#define MKV_LOG LOG_PRINTF
#endif

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

typedef enum {
    MKV_AUDIO_TYPE_UNKNOWN = 0,
    MKV_AUDIO_PCM_U8,    /* PCM, 8 pits per sample */
    MKV_AUDIO_PCM_S16LE, /* PCM, little-endian, 16 bits per sample */
    MKV_AUDIO_PCM_S24LE, /* PCM, little-endian, 24 bits per sample */
    MKV_AUDIO_PCM_S32LE, /* PCM, little-endian, 32 bits per sample */
    MKV_AUDIO_PCM_S16BE, /* PCM, big-endian, 16 bits per sample */
    MKV_AUDIO_PCM_S24BE, /* PCM, big-endian, 24 bits per sample */
    MKV_AUDIO_PCM_S32BE, /* PCM, big-endian, 32 bits per sample */
    MKV_AUDIO_PCM_ALAW,
    MKV_AUDIO_PCM_MULAW,
    MKV_AUDIO_ADPCM,
    MKV_AUDIO_MP3,       /* MPEG-1/2 Layer 1,2,3 */
    MKV_AUDIO_AAC,       /* MPEG-4 AAC, 14496-3 */
    MKV_AUDIO_MPEG2_AAC, /* MPEG-2 AAC, 13818-7 */
    MKV_AUDIO_AC3,
    MKV_AUDIO_WMA1,
    MKV_AUDIO_WMA2,
    MKV_AUDIO_WMA3,
    MKV_AUDIO_AMR_NB,      /* Adaptive Multi-Rate - narrow band */
    MKV_AUDIO_AMR_WB,      /* Adaptive Multi-Rate - Wideband */
    MKV_AUDIO_AMR_WB_PLUS, /* Extended Adaptive Multi-Rate - Wideband */
    MKV_AUDIO_DTS,
    MKV_AUDIO_VORBIS,
    MKV_AUDIO_FLAC,
    MKV_AUDIO_NELLYMOSER,
    MKV_AUIDO_SPEEX,
    MKV_REAL_AUDIO_144,
    MKV_REAL_AUDIO_288,
    MKV_REAL_AUDIO_COOK,
    MKV_REAL_AUDIO_ATRC,
    MKV_AUDIO_EC3,
    MKV_AUDIO_OPUS,
    MKV_AUDIO_ADPCM_IMA,

    MKV_VIDEO_TYPE_UNKNOWN = 100,
    MKV_VIDEO_UNCOMPRESSED, /* uncompressed video, every frame is a key frame */
    MKV_VIDEO_MPEG2,        /* MPEG-2 video, ISO/IEC 13818-2 */
    MKV_VIDEO_MPEG4,        /* MPEG-4 video, ISO/IEC 14496-2 */
    MKV_VIDEO_MS_MPEG4_V2,  /* Microsoft MPEG-4 video version 2, fourcc 'mp42'*/
    MKV_VIDEO_MS_MPEG4_V3,  /* Microsoft MPEG-4 video version 3, fourcc 'mp43' */
    MKV_VIDEO_H263,         /* ITU-T H.263 */
    MKV_VIDEO_H264,         /* H.264, ISO/IEC 14496-10 */
    MKV_VIDEO_MJPG,         /* Motion JPEG */
    MKV_VIDEO_DIVX3,        /* version 3*/
    MKV_VIDEO_DIVX4,        /* version 4*/
    MKV_VIDEO_DIVX5_6,      /* version 5 & 6*/
    MKV_VIDEO_XVID,
    MKV_VIDEO_WMV7,
    MKV_VIDEO_WMV8,
    MKV_VIDEO_WMV9,
    MKV_VIDEO_WMV9A,         /* WMV9 Advanced profile */
    MKV_VIDEO_WVC1,          /* VC-1 */
    MKV_VIDEO_SORENSON_H263, /* Sorenson Spark video, by Adobe, not standard H.263 */
    MKV_FLV_SCREEN_VIDEO,    /* Screen video version 1*/
    MKV_FLV_SCREEN_VIDEO_2,  /* Screen video version 2 */
    MKV_VIDEO_VP6,
    MKV_VIDEO_VP6A,
    MKV_VIDEO_VP7,
    MKV_VIDEO_VP8,
    MKV_REAL_VIDEO_RV10,
    MKV_REAL_VIDEO_RV20,
    MKV_REAL_VIDEO_RV30,
    MKV_REAL_VIDEO_RV40,
    MKV_VIDEO_VP9,
    MKV_VIDEO_HEVC,
    MKV_VIDEO_AV1,
    // SubTitle type
    MKV_SUBTITLE_UNKNOWN = 200,
    MKV_SUBTITLE_TEXT,
    MKV_SUBTITLE_SSA,
    MKV_SUBTITLE_ASS
} InternalCodecType;

typedef struct AudioCodecTags {
    char str[20];
    int strsize;
    InternalCodecType codec_type;
} AudioCodecTags;

typedef struct VideoCodecTags {
    char str[20];
    int strsize;
    InternalCodecType codec_type;
} VideoCodecTags;

typedef struct TrackMediaTags {
    char str[20];
    int strsize;
    InternalCodecType codec_type;
} TrackMediaTags;

#define MKTAG(a, b, c, d) (a | (b << 8) | (c << 16) | (d << 24))
#define MKBETAG(a, b, c, d) (d | (c << 8) | (b << 16) | (a << 24))

typedef struct _ImageCodecTags {
    InternalCodecType codec_type;
    unsigned int tag;
} ImageCodecTags;

typedef struct _WaveCodecTags {
    InternalCodecType codec_type;
    unsigned int tag;
} WaveCodecTags;

#define MEDIA_SSA 0x11

const AudioCodecTags mkv_audio_codec_tags[] = {
        {"A_AAC", sizeof("A_AAC"), MKV_AUDIO_AAC},
        {"A_AC3", sizeof("A_AC3"), MKV_AUDIO_AC3},
        {"A_DTS", sizeof("A_DTS"), MKV_AUDIO_DTS},
        {"A_EAC3", sizeof("A_EAC3"), MKV_AUDIO_EC3},
        {"A_FLAC", sizeof("A_FLAC"), MKV_AUDIO_FLAC},
        {"A_MLP", sizeof("A_MLP"), MKV_AUDIO_TYPE_UNKNOWN},
        {"A_MPEG/L2", sizeof("A_MPEG/L2"), MKV_AUDIO_MP3},
        {"A_MPEG/L1", sizeof("A_MPEG/L1"), MKV_AUDIO_MP3},
        {"A_MPEG/L3", sizeof("A_MPEG/L3"), MKV_AUDIO_MP3},
        {"A_PCM/FLOAT/IEEE", sizeof("A_PCM/FLOAT/IEEE"), MKV_AUDIO_PCM_U8},
        {"A_PCM/INT/BIG", sizeof("A_PCM/INT/BIG"), MKV_AUDIO_PCM_S16BE},  // MKV_AUDIO_PCM_U8},
        {"A_PCM/INT/LIT", sizeof("A_PCM/INT/LIT"), MKV_AUDIO_PCM_S16LE},
        {"A_QUICKTIME/QDM2", sizeof("A_QUICKTIME/QDM2"), MKV_AUDIO_TYPE_UNKNOWN},
        {"A_REAL/14_4", sizeof("A_REAL/14_4"), MKV_REAL_AUDIO_144},
        {"A_REAL/28_8", sizeof("A_REAL/28_8"), MKV_REAL_AUDIO_288},
        {"A_REAL/ATRC", sizeof("A_REAL/ATRC"), MKV_REAL_AUDIO_ATRC},
        {"A_REAL/COOK", sizeof("A_REAL/COOK"), MKV_REAL_AUDIO_COOK},
        {"A_TRUEHD", sizeof("A_TRUEHD"), MKV_AUDIO_TYPE_UNKNOWN},
        {"A_TTA1", sizeof("A_TTA1"), MKV_AUDIO_TYPE_UNKNOWN},
        {"A_VORBIS", sizeof("A_VORBIS"), MKV_AUDIO_VORBIS},
        {"A_OPUS", sizeof("A_OPUS"), MKV_AUDIO_OPUS},
        {"A_WAVPACK4", sizeof("A_WAVPACK4"), MKV_AUDIO_TYPE_UNKNOWN},
};

const VideoCodecTags mkv_video_codec_tags[] = {
        {"V_DIRAC", sizeof("V_DIRAC"), MKV_VIDEO_TYPE_UNKNOWN},
        {"V_MJPEG", sizeof("V_MJPEG"), MKV_VIDEO_MJPG},
        {"V_MPEG1", sizeof("V_MPEG1"), MKV_VIDEO_MPEG2},
        {"V_MPEG2", sizeof("V_MPEG2"), MKV_VIDEO_MPEG2},
        {"V_MPEG4/ISO/ASP", sizeof("V_MPEG4/ISO/ASP"), MKV_VIDEO_MPEG4},
        {"V_MPEG4/ISO/AP", sizeof("V_MPEG4/ISO/AP"), MKV_VIDEO_MPEG4},
        {"V_MPEG4/ISO/SP", sizeof("V_MPEG4/ISO/SP"), MKV_VIDEO_MPEG4},
        {"V_MPEG4/ISO/AVC", sizeof("V_MPEG4/ISO/AVC"), MKV_VIDEO_H264},
        {"V_MPEG4/MS/V3", sizeof("V_MPEG4/MS/V3"), MKV_VIDEO_MS_MPEG4_V3},
        {"V_MPEGH/ISO/HEVC", sizeof("V_MPEGH/ISO/HEVC"), MKV_VIDEO_HEVC},
        {"V_REAL/RV10", sizeof("V_REAL/RV10"), MKV_REAL_VIDEO_RV10},
        {"V_REAL/RV20", sizeof("V_REAL/RV20"), MKV_REAL_VIDEO_RV20},
        {"V_REAL/RV30", sizeof("V_REAL/RV30"), MKV_REAL_VIDEO_RV30},
        {"V_REAL/RV40", sizeof("V_REAL/RV40"), MKV_REAL_VIDEO_RV40},
        {"V_SNOW", sizeof("V_SNOW"), MKV_VIDEO_TYPE_UNKNOWN},
        {"V_THEORA", sizeof("V_THEORA"), MKV_VIDEO_TYPE_UNKNOWN},
        {"V_UNCOMPRESSED", sizeof("V_UNCOMPRESSED"), MKV_VIDEO_UNCOMPRESSED},
        {"V_VP8", sizeof("V_VP8"), MKV_VIDEO_VP8},
        {"V_VP9", sizeof("V_VP9"), MKV_VIDEO_VP9},
        {"V_AV1", sizeof("V_AV1"), MKV_VIDEO_AV1},

};

const TrackMediaTags mkv_track_media_tags[] = {
        {"S_TEXT/UTF8", sizeof("S_TEXT/UTF8"), MKV_SUBTITLE_TEXT},    // MEDIA_TEXT},
        {"S_TEXT/ASCII", sizeof("S_TEXT/ASCII"), MKV_SUBTITLE_TEXT},  // MEDIA_TEXT},
        {"S_TEXT/ASS", sizeof("S_TEXT/ASS"), MKV_SUBTITLE_ASS},       // MEDIA_TEXT},
        {"S_TEXT/SSA", sizeof("S_TEXT/SSA"), MKV_SUBTITLE_SSA},       // MEDIA_TEXT},
        {"S_ASS", sizeof("S_ASS"), MKV_SUBTITLE_ASS},                 // MEDIA_TYPE_UNKNOWN},
        {"S_SSA", sizeof("S_SSA"), MKV_SUBTITLE_SSA},                 // MEDIA_TYPE_UNKNOWN},
        {"S_VOBSUB", sizeof("S_VOBSUB"), MKV_SUBTITLE_UNKNOWN},       // MEDIA_TYPE_UNKNOWN},
};

const ImageCodecTags mkv_image_codec_tags[] = {
        {MKV_VIDEO_H264, MKTAG('H', '2', '6', '4')},
        {MKV_VIDEO_H264, MKTAG('h', '2', '6', '4')},
        {MKV_VIDEO_H264, MKTAG('X', '2', '6', '4')},
        {MKV_VIDEO_H264, MKTAG('x', '2', '6', '4')},
        {MKV_VIDEO_H264, MKTAG('a', 'v', 'c', '1')},
        {MKV_VIDEO_H264, MKTAG('V', 'S', 'S', 'H')},
        {MKV_VIDEO_H263, MKTAG('H', '2', '6', '3')},
        {MKV_VIDEO_H263, MKTAG('X', '2', '6', '3')},
        {MKV_VIDEO_H263, MKTAG('T', '2', '6', '3')},
        {MKV_VIDEO_H263, MKTAG('L', '2', '6', '3')},
        {MKV_VIDEO_H263, MKTAG('V', 'X', '1', 'K')},
        {MKV_VIDEO_H263, MKTAG('Z', 'y', 'G', 'o')},
        {MKV_VIDEO_H263, MKTAG('H', '2', '6', '3')},
        {MKV_VIDEO_H263, MKTAG('I', '2', '6', '3')},
        {MKV_VIDEO_H263, MKTAG('H', '2', '6', '1')},
        {MKV_VIDEO_H263, MKTAG('U', '2', '6', '3')},
        {MKV_VIDEO_H263, MKTAG('v', 'i', 'v', '1')},
        {MKV_VIDEO_MPEG4, MKTAG('F', 'M', 'P', '4')},
        {MKV_VIDEO_DIVX4, MKTAG('D', 'I', 'V', 'X')},
        {MKV_VIDEO_DIVX5_6, MKTAG('D', 'X', '5', '0')},
        {MKV_VIDEO_XVID, MKTAG('X', 'V', 'I', 'D')},
        {MKV_VIDEO_MPEG4, MKTAG('M', 'P', '4', 'S')},
        {MKV_VIDEO_MPEG4, MKTAG('M', '4', 'S', '2')},
        {MKV_VIDEO_MPEG4, MKTAG(4, 0, 0, 0)},
        {MKV_VIDEO_XVID, MKTAG('D', 'I', 'V', '1')},
        {MKV_VIDEO_MPEG4, MKTAG('B', 'L', 'Z', '0')},
        {MKV_VIDEO_MPEG4, MKTAG('m', 'p', '4', 'v')},
        {MKV_VIDEO_MPEG4, MKTAG('U', 'M', 'P', '4')},
        {MKV_VIDEO_MPEG4, MKTAG('W', 'V', '1', 'F')},
        {MKV_VIDEO_MPEG4, MKTAG('S', 'E', 'D', 'G')},
        {MKV_VIDEO_MPEG4, MKTAG('R', 'M', 'P', '4')},
        {MKV_VIDEO_MPEG4, MKTAG('3', 'I', 'V', '2')},
        {MKV_VIDEO_MPEG4, MKTAG('F', 'F', 'D', 'S')},
        {MKV_VIDEO_MPEG4, MKTAG('F', 'V', 'F', 'W')},
        {MKV_VIDEO_MPEG4, MKTAG('D', 'C', 'O', 'D')},
        {MKV_VIDEO_MPEG4, MKTAG('M', 'V', 'X', 'M')},
        {MKV_VIDEO_MPEG4, MKTAG('P', 'M', '4', 'V')},
        {MKV_VIDEO_MPEG4, MKTAG('S', 'M', 'P', '4')},
        {MKV_VIDEO_MPEG4, MKTAG('D', 'X', 'G', 'M')},
        {MKV_VIDEO_MPEG4, MKTAG('V', 'I', 'D', 'M')},
        {MKV_VIDEO_MPEG4, MKTAG('M', '4', 'T', '3')},
        {MKV_VIDEO_MPEG4, MKTAG('G', 'E', 'O', 'X')},
        {MKV_VIDEO_MPEG4, MKTAG('H', 'D', 'X', '4')},
        {MKV_VIDEO_MPEG4, MKTAG('D', 'M', 'K', '2')},
        {MKV_VIDEO_MPEG4, MKTAG('D', 'I', 'G', 'I')},
        {MKV_VIDEO_MPEG4, MKTAG('I', 'N', 'M', 'C')},
        {MKV_VIDEO_MPEG4, MKTAG('E', 'P', 'H', 'V')},
        {MKV_VIDEO_MPEG4, MKTAG('E', 'M', '4', 'A')},
        {MKV_VIDEO_MPEG4, MKTAG('M', '4', 'C', 'C')},
        {MKV_VIDEO_MPEG4, MKTAG('S', 'N', '4', '0')},
        {MKV_VIDEO_DIVX3, MKTAG('D', 'I', 'V', '3')},
        {MKV_VIDEO_MS_MPEG4_V3, MKTAG('M', 'P', '4', '3')},
        {MKV_VIDEO_MS_MPEG4_V3, MKTAG('M', 'P', 'G', '3')},
        {MKV_VIDEO_DIVX5_6, MKTAG('D', 'I', 'V', '5')},
        {MKV_VIDEO_DIVX5_6, MKTAG('D', 'I', 'V', '6')},
        {MKV_VIDEO_DIVX4, MKTAG('D', 'I', 'V', '4')},
        {MKV_VIDEO_DIVX3, MKTAG('D', 'V', 'X', '3')},
        {MKV_VIDEO_MS_MPEG4_V3, MKTAG('A', 'P', '4', '1')},
        {MKV_VIDEO_MS_MPEG4_V3, MKTAG('C', 'O', 'L', '1')},
        {MKV_VIDEO_MS_MPEG4_V3, MKTAG('C', 'O', 'L', '0')},
        {MKV_VIDEO_MS_MPEG4_V2, MKTAG('M', 'P', '4', '2')},
        {MKV_VIDEO_MS_MPEG4_V2, MKTAG('D', 'I', 'V', '2')},
        {MKV_VIDEO_MPEG4, MKTAG('M', 'P', 'G', '4')},
        {MKV_VIDEO_MPEG4, MKTAG('M', 'P', '4', '1')},
        {MKV_VIDEO_WMV7, MKTAG('W', 'M', 'V', '1')},
        {MKV_VIDEO_WMV8, MKTAG('W', 'M', 'V', '2')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'v', 's', 'd')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'v', 'h', 'd')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'v', 'h', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'v', 's', 'l')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'v', '2', '5')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'v', '5', '0')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('c', 'd', 'v', 'c')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('C', 'D', 'V', 'H')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'v', 'c', ' ')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'v', 'h', '1')},
        {MKV_VIDEO_MPEG2, MKTAG('m', 'p', 'g', '1')},
        {MKV_VIDEO_MPEG2, MKTAG('m', 'p', 'g', '2')},
        {MKV_VIDEO_MPEG2, MKTAG('m', 'p', 'g', '2')},
        {MKV_VIDEO_MPEG2, MKTAG('M', 'P', 'E', 'G')},
        {MKV_VIDEO_MPEG2, MKTAG('P', 'I', 'M', '1')},
        {MKV_VIDEO_MPEG2, MKTAG('P', 'I', 'M', '2')},
        {MKV_VIDEO_MPEG2, MKTAG('V', 'C', 'R', '2')},
        {MKV_VIDEO_MPEG2, MKTAG(1, 0, 0, 16)},
        {MKV_VIDEO_MPEG2, MKTAG(2, 0, 0, 16)},
        {MKV_VIDEO_MPEG4, MKTAG(4, 0, 0, 16)},
        {MKV_VIDEO_MPEG4, MKTAG('D', 'V', 'R', ' ')},
        {MKV_VIDEO_MPEG4, MKTAG('M', 'M', 'E', 'S')},
        {MKV_VIDEO_MPEG4, MKTAG('L', 'M', 'P', '2')},
        {MKV_VIDEO_MJPG, MKTAG('M', 'J', 'P', 'G')},
        {MKV_VIDEO_MJPG, MKTAG('L', 'J', 'P', 'G')},
        {MKV_VIDEO_MJPG, MKTAG('d', 'm', 'b', '1')},
        {MKV_VIDEO_MJPG, MKTAG('L', 'J', 'P', 'G')},
        {MKV_VIDEO_MJPG, MKTAG('J', 'P', 'G', 'L')},
        {MKV_VIDEO_MJPG, MKTAG('M', 'J', 'L', 'S')},
        {MKV_VIDEO_MJPG, MKTAG('j', 'p', 'e', 'g')},
        {MKV_VIDEO_MJPG, MKTAG('I', 'J', 'P', 'G')},
        {MKV_VIDEO_MJPG, MKTAG('A', 'V', 'R', 'n')},
        {MKV_VIDEO_MJPG, MKTAG('A', 'C', 'D', 'V')},
        {MKV_VIDEO_MJPG, MKTAG('Q', 'I', 'V', 'G')},
        {MKV_VIDEO_MJPG, MKTAG('S', 'L', 'M', 'J')},
        {MKV_VIDEO_MJPG, MKTAG('C', 'J', 'P', 'G')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('H', 'F', 'Y', 'U')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('F', 'F', 'V', 'H')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('C', 'Y', 'U', 'V')},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG(0, 0, 0, 0)},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG(3, 0, 0, 0)},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG('I', '4', '2', '0')},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG('Y', 'U', 'Y', '2')},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG('Y', '4', '2', '2')},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG('Y', 'V', '1', '2')},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG('U', 'Y', 'V', 'Y')},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG('I', 'Y', 'U', 'V')},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG('Y', '8', '0', '0')},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG('H', 'D', 'Y', 'C')},
        {MKV_VIDEO_UNCOMPRESSED, MKTAG('Y', 'V', 'U', '9')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('v', '2', '1', '0')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('I', 'V', '3', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('I', 'V', '3', '2')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('I', 'V', '4', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('I', 'V', '5', '0')},
        {MKV_VIDEO_VP6, MKTAG('V', 'P', '3', '1')},
        {MKV_VIDEO_VP6, MKTAG('V', 'P', '3', '0')},
        {MKV_VIDEO_VP6, MKTAG('V', 'P', '5', '0')},
        {MKV_VIDEO_VP6, MKTAG('V', 'P', '6', '0')},
        {MKV_VIDEO_VP6, MKTAG('V', 'P', '6', '1')},
        {MKV_VIDEO_VP6, MKTAG('V', 'P', '6', '2')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('A', 'S', 'V', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('A', 'S', 'V', '2')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('V', 'C', 'R', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('F', 'F', 'V', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('X', 'x', 'a', 'n')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('L', 'M', '2', '0')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('m', 'r', 'l', 'e')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG(1, 0, 0, 0)},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG(2, 0, 0, 0)},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('M', 'S', 'V', 'C')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('m', 's', 'v', 'c')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('C', 'R', 'A', 'M')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('c', 'r', 'a', 'm')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('W', 'H', 'A', 'M')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('w', 'h', 'a', 'm')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('c', 'v', 'i', 'd')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('D', 'U', 'C', 'K')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('P', 'V', 'E', 'Z')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('M', 'S', 'Z', 'H')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('Z', 'L', 'I', 'B')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('S', 'N', 'O', 'W')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('4', 'X', 'M', 'V')},
        {MKV_VIDEO_SORENSON_H263, MKTAG('F', 'L', 'V', '1')},
        {MKV_FLV_SCREEN_VIDEO, MKTAG('F', 'S', 'V', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('V', 'P', '6', 'F')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('s', 'v', 'q', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('t', 's', 'c', 'c')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('U', 'L', 'T', 'I')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('V', 'I', 'X', 'L')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('Q', 'P', 'E', 'G')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('Q', '1', '.', '0')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('Q', '1', '.', '1')},
        {MKV_VIDEO_WMV9, MKTAG('W', 'M', 'V', '3')},
        {MKV_VIDEO_WVC1, MKTAG('W', 'V', 'C', '1')},
        {MKV_VIDEO_WMV9A, MKTAG('W', 'M', 'V', 'A')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('L', 'O', 'C', 'O')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('W', 'N', 'V', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('A', 'A', 'S', 'C')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('R', 'T', '2', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('F', 'P', 'S', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('t', 'h', 'e', 'o')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('T', 'M', '2', '0')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('C', 'S', 'C', 'D')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('Z', 'M', 'B', 'V')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('K', 'M', 'V', 'C')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('C', 'A', 'V', 'S')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('M', 'J', '2', 'C')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('V', 'M', 'n', 'c')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('t', 'g', 'a', ' ')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('M', 'P', 'N', 'G')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('P', 'N', 'G', '1')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('c', 'l', 'j', 'r')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'r', 'a', 'c')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('a', 'z', 'p', 'r')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('R', 'P', 'Z', 'A')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('r', 'p', 'z', 'a')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('S', 'P', '5', '4')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('A', 'U', 'R', 'A')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('A', 'U', 'R', '2')},
        {MKV_VIDEO_TYPE_UNKNOWN, MKTAG('d', 'p', 'x', ' ')},
        {MKV_VIDEO_HEVC, MKTAG('H', 'M', '1', '0')},
};

const WaveCodecTags mkv_wave_codec_tags[] = {
        {MKV_AUDIO_PCM_S16LE, 0x0001},
        {MKV_AUDIO_PCM_U8, 0x0001},
        {MKV_AUDIO_PCM_S24LE, 0x0001},
        {MKV_AUDIO_PCM_S32LE, 0x0001},
        {MKV_AUDIO_ADPCM, 0x0002},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0003},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0003},
        {MKV_AUDIO_PCM_ALAW, 0x0006},
        {MKV_AUDIO_PCM_MULAW, 0x0007},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x000A},
        {MKV_AUDIO_ADPCM_IMA, 0x0011},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0020},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0022},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0031},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0045},
        {MKV_AUDIO_MP3, 0x0050},
        {MKV_AUDIO_MP3, 0x0055},
        {MKV_AUDIO_AMR_NB, 0x0057},
        {MKV_AUDIO_AMR_WB, 0x0058},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0061},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0062},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0069},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0075},
        {MKV_AUDIO_AAC, 0x00ff},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0130},
        {MKV_AUDIO_WMA1, 0x0160},
        {MKV_AUDIO_WMA2, 0x0161},
        {MKV_AUDIO_WMA3, 0x0162},
        {MKV_AUDIO_WMA3, 0x0163},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0200},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0270},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x0401},
        {MKV_AUDIO_AC3, 0x2000},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x2001},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x2048},
        {MKV_AUDIO_TYPE_UNKNOWN, 0x2048},
        {MKV_AUDIO_PCM_MULAW, 0x6c75},
        {MKV_AUDIO_AAC, 0x706d},
        {MKV_AUDIO_AAC, 0x4143},
        {MKV_AUDIO_FLAC, 0xF1AC},
        {MKV_AUDIO_TYPE_UNKNOWN, ('S' << 8) + 'F'},
        {MKV_AUDIO_VORBIS, ('V' << 8) + 'o'},
        {MKV_AUDIO_PCM_S16LE, MKTAG('R', 'A', 'W', 'A')},
        {MKV_AUDIO_MP3, MKTAG('L', 'A', 'M', 'E')},
        {MKV_AUDIO_MP3, MKTAG('M', 'P', '3', ' ')},
};

static int matroska_add_codec_priv(MKVReaderContext* pctx, mkvStream* stream);

#ifdef MKV_TIMER
static struct timeval mkv_tm_beg;
static struct timeval mkv_tm_end;

void mkv_timer_start() {
    gettimeofday(&mkv_tm_beg, 0);
}

void mkv_timer_stop(char* description) {
    unsigned int tm_1, tm_2;
    unsigned int totalus;
    gettimeofday(&mkv_tm_end, 0);

    tm_1 = mkv_tm_beg.tv_sec * 1000000 + mkv_tm_beg.tv_usec;
    tm_2 = mkv_tm_end.tv_sec * 1000000 + mkv_tm_end.tv_usec;
    totalus = (tm_2 - tm_1);
    MKV_TIMER_LOG("%s: total time(us): %d \r\n", description, totalus);
}
#endif

int mkv_codec_get_type(unsigned int tag, int image) {
    uint32 i;
    /*
        for(i=0; i <sizeof(mkv_image_codec_tags)/sizeof(mkv_image_codec_tags[0]); i++)
        {
            if(   toupper((tag >> 0)&0xFF) == toupper((mkv_image_codec_tags[i].tag >> 0)&0xFF)
               && toupper((tag >> 8)&0xFF) == toupper((mkv_image_codec_tags[i].tag >> 8)&0xFF)
               && toupper((tag >>16)&0xFF) == toupper((mkv_image_codec_tags[i].tag >>16)&0xFF)
               && toupper((tag >>24)&0xFF) == toupper((mkv_image_codec_tags[i].tag >>24)&0xFF))
                return (int)mkv_image_codec_tags[i].codec_type;
        }
    */
    if (image) {
        for (i = 0; i < sizeof(mkv_image_codec_tags) / sizeof(mkv_image_codec_tags[0]); i++) {
            if (toupper((tag >> 0) & 0xFF) == toupper((mkv_image_codec_tags[i].tag >> 24) & 0xFF) &&
                toupper((tag >> 8) & 0xFF) == toupper((mkv_image_codec_tags[i].tag >> 16) & 0xFF) &&
                toupper((tag >> 16) & 0xFF) == toupper((mkv_image_codec_tags[i].tag >> 8) & 0xFF) &&
                toupper((tag >> 24) & 0xFF) == toupper((mkv_image_codec_tags[i].tag >> 0) & 0xFF))
                return (int)mkv_image_codec_tags[i].codec_type;
        }

        return (int)MKV_VIDEO_TYPE_UNKNOWN;
    } else {
        for (i = 0; i < sizeof(mkv_wave_codec_tags) / sizeof(mkv_wave_codec_tags[0]); i++) {
            if (toupper((tag >> 0) & 0xFF) == toupper((mkv_wave_codec_tags[i].tag >> 0) & 0xFF) &&
                toupper((tag >> 8) & 0xFF) == toupper((mkv_wave_codec_tags[i].tag >> 8) & 0xFF) &&
                toupper((tag >> 16) & 0xFF) == toupper((mkv_wave_codec_tags[i].tag >> 16) & 0xFF) &&
                toupper((tag >> 24) & 0xFF) == toupper((mkv_wave_codec_tags[i].tag >> 24) & 0xFF))
                return (int)mkv_wave_codec_tags[i].codec_type;
        }

        return (int)MKV_AUDIO_TYPE_UNKNOWN;
    }
}

int leading_zeros_byte(unsigned char x) {
    int clz = 0;

    if (!x)
        return 8;

    if (x >> 4)
        x = x >> 4;
    else
        clz += 4;

    if (x >> 2)
        x = x >> 2;
    else
        clz += 2;

    if (x >> 1)
        x = x >> 1;
    else
        clz++;

    if (!x)
        clz++;

    return clz;
}

vint read_vint_value(void* ptr, int vsize) {
    vint val = 0;
    unsigned char* p = NULL;

    if (!ptr || !vsize || vsize > 8)
        return 0;

    p = (unsigned char*)ptr;

    val = ((vint)(*p++)) - ((vint)1 << (8 - vsize));

    while (--vsize) val = (val << 8) | (*p++);

    return val;
}

uint64 read_uint_value(void* ptr, int vsize) {
    uint64 val = 0;
    unsigned char* p = NULL;

    if (!ptr || vsize <= 0 || vsize > 8)
        return 0;

    p = (unsigned char*)ptr;

    while (vsize--) val = (val << 8) | (*p++);

    return val;
}

double read_float_value(void* ptr, int vsize) {
    uint64 val = 0;
    double fval = 0.0;
    float fval32 = 0.0;

    if (!ptr || ((vsize != 4) && (vsize != 8)))
        return 0;

    val = read_uint_value(ptr, vsize);

    if (vsize == 4) {
        memcpy(&fval32, &val, vsize);
        return fval32;
    } else {
        memcpy(&fval, &val, vsize);
        return fval;
    }
}

vint read_vint_val_size(void* ptr, int* psize) {
    int vsize = 0;
    int read_size = 0;
    unsigned char* p = NULL;

    if (!ptr || !psize)
        return 0;

    read_size = *psize;

    p = (unsigned char*)ptr;

    vsize = leading_zeros_byte(p[0]) + 1;

    *psize = 0;

    if (read_size < vsize)
        return 0;

    if (vsize > 8)
        return 0;

    *psize = vsize;

    return read_vint_value(p, vsize);
}

vint read_svint_val_size(void* ptr, int* psize) {
    int n = 0;
    vint vval = 0;

    vval = read_vint_val_size(ptr, psize);

    if (*psize == 0)
        return 0;

    n = *psize;

    vval = vval - (((uint64)1 << (7 * n - 1)) - 1);

    return vval;
}

static uint64 ebml_unknown_length[8] = {
        0x7F,           0x3FFF,           0x1FFFFF,           0x0FFFFFFF,
        0x07FFFFFFFFLL, 0x03FFFFFFFFFFLL, 0x01FFFFFFFFFFFFLL, 0x00FFFFFFFFFFFFFFLL};

int64 read_one_ebml_info(void* ptr, int64 size, ebml_info* pinfo) {
    int vsize = 0;
    int64 bytes = 0;
    uint64 rem_size = 0;
    unsigned char* p = NULL;

    if (!ptr || !size || !pinfo)
        return 0;

    rem_size = size;
    p = (unsigned char*)ptr;

    vsize = leading_zeros_byte(p[0]) + 1;

    if (rem_size < (uint64)vsize)
        goto EBML_FAIL;

    pinfo->s_id = read_uint_value(p, vsize);

    p += vsize;
    bytes += vsize;
    rem_size -= vsize;

    vsize = leading_zeros_byte(p[0]) + 1;

    if (rem_size < (uint64)vsize || vsize > 8)
        goto EBML_FAIL;

    pinfo->s_size = read_vint_value(p, vsize);

    // check unknown length in ebml syntax
    if (ebml_unknown_length[vsize - 1] == pinfo->s_size)
        pinfo->s_size = 0x7FFFFFFFFFFFFFFFULL;

    p += vsize;
    bytes += vsize;
    rem_size -= vsize;

    pinfo->s_data = (char*)p;
    pinfo->s_offset = (int64)((unsigned char*)p - (unsigned char*)ptr);

    bytes += pinfo->s_size;

    return bytes;

EBML_FAIL:

    return 0;
}

#define INCREASE_BUFFER_COUNT(ptr, cnt, inc, type)                                   \
    if (!((cnt) % (inc))) {                                                          \
        char* buf = NULL;                                                            \
        int size = (((cnt) / (inc)) + 1) * (inc) * sizeof(type);                     \
                                                                                     \
        buf = alloc_stream_buffer(pbs, size, NON_STREAM);                            \
        if (!buf) {                                                                  \
            MKV_ERROR_LOG("INCREASE_BUFFER_COUNT : alloc_stream_buffer failed .\n"); \
            return -1;                                                               \
        }                                                                            \
                                                                                     \
        if (ptr) {                                                                   \
            memcpy(buf, (ptr), size - (inc) * sizeof(type));                         \
            free_stream_buffer(pbs, (char*)(ptr), NON_STREAM);                       \
        }                                                                            \
                                                                                     \
        (ptr) = (type*)buf;                                                          \
    }

static char* read_stream_data(ByteStream* pbs, int* psize) {
    int bytes = 0;
    char* p = NULL;

    p = (char*)alloc_stream_buffer(pbs, *psize, STREAM_MODE);
    if (!p) {
        MKV_ERROR_LOG("read_stream_data : alloc_stream_buffer return NULL .\n");
        return NULL;
    }

    bytes = read_stream_buffer(pbs, &p, *psize);
    if (bytes < pbs->default_max_ebml_offset) {
        if (!eof_stream_buffer(pbs)) {
            MKV_ERROR_LOG("read_stream_data : can't read enough bytes .\n");
            return NULL;
        }
    }

    *psize = bytes;

    return p;
}

static char* read_non_stream_data(ByteStream* pbs, int size) {
    int bytes = 0;
    char* p = NULL;

    p = (char*)alloc_stream_buffer(pbs, size, NON_STREAM);
    if (!p) {
        MKV_ERROR_LOG("read_stream_data_force : alloc_stream_buffer return NULL .\n");
        return NULL;
    }

    bytes = read_stream_buffer(pbs, &p, size);
    if (bytes != size) {
        MKV_ERROR_LOG("read_stream_data_force : can't read enough bytes .\n");
        return NULL;
    }

    return p;
}

static int64 read_next_ebml_info(ByteStream* pbs, ebml_info* pinfo) {
    char* p = NULL;
    int bytes = pbs->default_cachesize;  // INIT_READ_HEADER_LEN;

    p = read_stream_data(pbs, &bytes);
    if (!p)
        return 0;

    if (!bytes) {
        return 0;
    }

    return read_one_ebml_info(p, bytes, pinfo);
}

static int64 read_stream_ebml_info(ByteStream* pbs, uint64 seekpos, ebml_info* pinfo) {
    if (MKV_INVALID_SEEK_POS == seek_stream_buffer(pbs, seekpos, 0)) {
        MKV_ERROR_LOG("read_stream_ebml_info : seek_stream_buffer failed .\n");
        return -1;
    }

    return read_next_ebml_info(pbs, pinfo);
}

int read_ebml_master_header(MKVReaderContext* pctx) {
    char* p = NULL;
    int64 bytes = 0;
    int64 length = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    bytes = read_stream_ebml_info(pbs, 0, &einfo);
    if (bytes <= 0) {
        MKV_ERROR_LOG("read_ebml_master_header : read_stream_ebml_info failed .\n");
        return -1;
    }

    if (einfo.s_id != FSL_EBML_ID_HEADER) {
        MKV_ERROR_LOG("read_ebml_master_header : invalid ebml master ID .\n");
        return -1;
    }

    pctx->ebml_header_size = bytes;
    pctx->ebml_master_size = einfo.s_offset;

    pctx->ectx.ebml_version = FSL_EBML_VERSION;
    pctx->ectx.ebml_reader_version = FSL_EBML_VERSION;

    pctx->ectx.ebml_id_maxlen = 4;
    pctx->ectx.ebml_size_maxlen = 8;
    pctx->ectx.ebml_doctype_version = 1;
    pctx->ectx.ebml_doctype_reader_version = 1;

    p = einfo.s_data;
    length = bytes - einfo.s_offset;

    if (length > EMBL_HEADER_MAX_SIZE) {
        MKV_ERROR_LOG("read_ebml_master_header: too large length =%lld\n", (long long)length);
        return -1;
    }

    while (length > 0) {
        bytes = read_one_ebml_info(p, bytes, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_ebml_master_header : read_one_ebml_info failed .\n");
            return -1;
        }

        length -= bytes;
        p += bytes;

        switch (einfo.s_id) {
            case FSL_EBML_ID_EBMLVERSION:

                pctx->ectx.ebml_version = (int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_EBMLREADVERSION:

                pctx->ectx.ebml_reader_version =
                        (int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_EBMLMAXIDLENGTH:

                pctx->ectx.ebml_id_maxlen = (int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_EBMLMAXSIZELENGTH:

                pctx->ectx.ebml_size_maxlen = (int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_DOCTYPE:

                if (einfo.s_size < EBML_DOCTYPE_LEN) {
                    memcpy(pctx->ectx.ebml_doctype, einfo.s_data, (int)einfo.s_size);
                    pctx->ectx.ebml_doctype[einfo.s_size] = 0;
                } else {
                    MKV_ERROR_LOG("read_ebml_master_header : not enough buffer for doc type! \n");
                }

                break;
            case FSL_EBML_ID_DOCTYPEVERSION:

                pctx->ectx.ebml_doctype_version =
                        (int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_DOCTYPEREADVERSION:

                pctx->ectx.ebml_doctype_reader_version =
                        (int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            default:
                MKV_ERROR_LOG("read_ebml_master_header : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }
    }

    if (FSL_EBML_VERSION != pctx->ectx.ebml_version || pctx->ectx.ebml_id_maxlen > 4 ||
        pctx->ectx.ebml_size_maxlen > 8 ||
        (strncmp(pctx->ectx.ebml_doctype, MKV_DOCTYPE, sizeof(MKV_DOCTYPE)) &&
         strncmp(pctx->ectx.ebml_doctype, WEBM_DOCTYPE, sizeof(WEBM_DOCTYPE)))) {
        MKV_ERROR_LOG("read_ebml_master_header : unsupported ebml features:\n");
        MKV_ERROR_LOG("version %d, max id length %d, max size length %d\n", pctx->ectx.ebml_version,
                      pctx->ectx.ebml_id_maxlen, pctx->ectx.ebml_size_maxlen);
        MKV_ERROR_LOG("doctype \"%s\", doctype version %d .max size length %d\n",
                      pctx->ectx.ebml_doctype, pctx->ectx.ebml_doctype_version,
                      pctx->ectx.ebml_size_maxlen);

        return -1;
    }

    return 0;
}

static int read_segment_seek_entry(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_seek_entry : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_SEEKENTRY:

                INCREASE_BUFFER_COUNT(pctx->sctx.seek_list, pctx->sctx.seek_count,
                                      SEEK_ENTRY_INC_NUM, SeekEntry);

                pctx->sctx.seek_count++;

                read_segment_seek_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_SEEKID:

                idx = pctx->sctx.seek_count - 1;
                pctx->sctx.seek_list[idx].seekid =
                        (unsigned long)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_SEEKPOSITION:

                idx = pctx->sctx.seek_count - 1;
                pctx->sctx.seek_list[idx].seekpos =
                        read_uint_value(einfo.s_data, (int)einfo.s_size);
                MKV_LOG("seek list[%d]: seek pos: %lld \r\n", idx,
                        pctx->sctx.seek_list[idx].seekpos);
                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_seek_entry : unhandled ID 0x%X \n", (uint32)einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}
#if 0
static void asciiToUnicodeString(uint8 * src, uint16 * des)
{
    uint32 size;
    uint32 i;

    size = (uint32)strlen(src);

    for(i = 0; i < size; i ++ )
    {
        des[i] = (uint16)src[i];        
    }
    des[i] = 0;  

}
#endif
static int read_segment_id_info(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_id_info : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_TIMECODESCALE:

                pctx->sctx.info.time_code_scale =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_MUXINGAPP:

                if (einfo.s_size < sizeof(pctx->sctx.info.muxing_app.str)) {
                    memcpy(pctx->sctx.info.muxing_app.str, einfo.s_data, (int)einfo.s_size);
                    pctx->sctx.info.muxing_app.str[einfo.s_size] = 0;
                    pctx->sctx.info.muxing_app.strlen = (uint32)einfo.s_size;
                }

                break;
            case MATROSKA_ID_WRITINGAPP:

                if (einfo.s_size < sizeof(pctx->sctx.info.writing_app.str)) {
                    memcpy(pctx->sctx.info.writing_app.str, einfo.s_data, (int)einfo.s_size);
                    pctx->sctx.info.writing_app.str[einfo.s_size] = 0;
                    pctx->sctx.info.writing_app.strlen = (uint32)einfo.s_size;
                }

                break;
            case MATROSKA_ID_TITLE:

                if (einfo.s_size < sizeof(pctx->sctx.info.title.str)) {
                    pctx->sctx.info.title.strlen = (uint32)einfo.s_size;
                    memcpy(pctx->sctx.info.title.str, einfo.s_data, (int)einfo.s_size);
                    pctx->sctx.info.title.str[einfo.s_size] = 0;
                    PARSERMSG("Seg Title : %s \n", pctx->sctx.info.title.str);
                }

                break;
            case MATROSKA_ID_DURATION:

                pctx->sctx.info.duration = read_float_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_DATEUTC:

                pctx->sctx.info.date_utc = (int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_SEGMENTUID:

                if (einfo.s_size != 16) {
                    MKV_ERROR_LOG("read_segment_id_info : invalid segment ID size .\n");
                }

                memcpy(pctx->sctx.info.uid, einfo.s_data, 16);

                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_id_info : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_track_video_colour_mastering_metadata(MKVReaderContext* pctx,
                                                              TrackEntry* track_entry,
                                                              uint64 offset, int64 length) {
    int64 bytes = 0;
    ebml_info einfo;
    float rval = 0.0f;
    ByteStream* pbs = NULL;
    VideoColourMasteringMetadata* meta = NULL;

    if (!pctx || track_entry == NULL || track_entry->colorMetadataPtr == NULL)
        return -1;

    pbs = &pctx->bs;
    meta = track_entry->colorMetadataPtr;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_track_video : read_stream_ebml_info failed .\n");
            return -1;
        }

        rval = read_float_value(einfo.s_data, (int)einfo.s_size);

        switch (einfo.s_id) {
            case MATROSKA_ID_VIDEOPRIMARYRX:
                meta->PrimaryRChromaticityX = rval;
                break;

            case MATROSKA_ID_VIDEOPRIMARYRY:
                meta->PrimaryRChromaticityY = rval;
                break;

            case MATROSKA_ID_VIDEOPRIMARYGX:
                meta->PrimaryGChromaticityX = rval;
                break;

            case MATROSKA_ID_VIDEOPRIMARYGY:
                meta->PrimaryGChromaticityY = rval;
                break;

            case MATROSKA_ID_VIDEOPRIMARYBX:
                meta->PrimaryBChromaticityX = rval;
                break;

            case MATROSKA_ID_VIDEOPRIMARYBY:
                meta->PrimaryBChromaticityY = rval;
                break;

            case MATROSKA_ID_VIDEOWHITEPOINTX:
                meta->WhitePointChromaticityX = rval;
                break;

            case MATROSKA_ID_VIDEOWHITEPOINTY:
                meta->WhitePointChromaticityY = rval;
                break;

            case MATROSKA_ID_VIDEOLUMINANCEMAX:
                meta->LuminanceMax = rval;
                break;

            case MATROSKA_ID_VIDEOLUMINANCEMIN:
                meta->LuminanceMin = rval;
                break;

            default:
                MKV_ERROR_LOG(
                        "read_segment_track_video mastering_metadata : unhandled ID 0x%llX \n",
                        einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_track_video_colour(MKVReaderContext* pctx, TrackEntry* track_entry,
                                           uint64 offset, int64 length) {
    int64 bytes = 0;
    ebml_info einfo;
    unsigned int rval = 0;
    ByteStream* pbs = NULL;
    VideoColour* colour = NULL;

    if (!pctx || track_entry == NULL || track_entry->colorPtr == NULL)
        return -1;

    pbs = &pctx->bs;

    colour = track_entry->colorPtr;
    colour->MatrixCoefficients = 2;
    colour->TransferCharacteristics = 2;
    colour->Range = 0;
    colour->Primaries = 2;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_track_video_colour : read_stream_ebml_info failed .\n");
            return -1;
        }

        rval = (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

        switch (einfo.s_id) {
            case MATROSKA_ID_VIDEOMATRIXCOEFFICIENTS:
                colour->MatrixCoefficients = rval;
                break;

            case MATROSKA_ID_VIDEOBITSPERCHANNEL:
                colour->BitsPerChannel = rval;
                break;

            case MATROSKA_ID_VIDEOCHROMASUBSAMPLINGHORZ:
                colour->ChromaSubsamplingHorz = rval;
                break;

            case MATROSKA_ID_VIDEOCHROMASUBSAMPLINGVERT:
                colour->ChromaSubsamplingVert = rval;
                break;

            case MATROSKA_ID_VIDEOCBSUBSAMPLINGHORZ:
                colour->CbSubsamplingHorz = rval;
                break;

            case MATROSKA_ID_VIDEOCBSUBSAMPLINGVERT:
                colour->CbSubsamplingVert = rval;
                break;

            case MATROSKA_ID_VIDEOCHROMASITINGHORZ:
                colour->ChromaSitingHorz = rval;
                break;

            case MATROSKA_ID_VIDEOCHROMASITINGVERT:
                colour->ChromaSitingVert = rval;
                break;

            case MATROSKA_ID_VIDEORANGE:
                colour->Range = rval;
                break;

            case MATROSKA_ID_VIDEOTRANSFERCHARACTERISTICS:
                colour->TransferCharacteristics = rval;
                break;

            case MATROSKA_ID_VIDEOPRIMARIES:
                colour->Primaries = rval;
                break;

            case MATROSKA_ID_VIDEOMAXCLL:
                colour->MaxCLL = rval;
                break;

            case MATROSKA_ID_VIDEOMAXFALL:
                colour->MaxFALL = rval;
                break;

            case MATROSKA_ID_VIDEOMASTERINGMETADATA:
                track_entry->colorMetadataPtr = (VideoColourMasteringMetadata*)alloc_stream_buffer(
                        pbs, sizeof(VideoColourMasteringMetadata), NON_STREAM);
                memset(track_entry->colorMetadataPtr, 0, sizeof(VideoColourMasteringMetadata));
                read_segment_track_video_colour_mastering_metadata(
                        pctx, track_entry, offset + einfo.s_offset, (int)einfo.s_size);
                break;

            default:
                MKV_ERROR_LOG("read_segment_track_video_colour : unhandled ID 0x%llX \n",
                              einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_track_video(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int64 bytes = 0;
    ebml_info einfo;
    unsigned int rval = 0;
    ByteStream* pbs = NULL;
    TrackEntry* track_entry = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;
    track_entry = &pctx->sctx.track_list[pctx->sctx.track_count - 1];

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_track_video : read_stream_ebml_info failed .\n");
            return -1;
        }

        rval = (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

        switch (einfo.s_id) {
            case MATROSKA_ID_VIDEOPIXELWIDTH:

                track_entry->vinfo.pixel_width = rval;

                break;
            case MATROSKA_ID_VIDEOPIXELHEIGHT:

                track_entry->vinfo.pixel_height = rval;

                break;
            case MATROSKA_ID_VIDEOFLAGINTERLACED:

                track_entry->vinfo.flag_interlaced = rval;

                break;
            case MATROSKA_ID_VIDEODISPLAYWIDTH:

                track_entry->vinfo.display_width = rval;

                break;
            case MATROSKA_ID_VIDEODISPLAYHEIGHT:

                track_entry->vinfo.display_height = rval;

                break;
            case MATROSKA_ID_VIDEOPIXELCROPB:

                track_entry->vinfo.pixel_crop_bottom = rval;

                break;
            case MATROSKA_ID_VIDEOPIXELCROPT:

                track_entry->vinfo.pixel_crop_top = rval;

                break;
            case MATROSKA_ID_VIDEOPIXELCROPL:

                track_entry->vinfo.pixel_crop_left = rval;

                break;
            case MATROSKA_ID_VIDEOPIXELCROPR:

                track_entry->vinfo.pixel_crop_right = rval;

                break;
            case MATROSKA_ID_VIDEODISPLAYUNIT:

                track_entry->vinfo.display_unit = rval;

                break;
            case MATROSKA_ID_VIDEOFRAMERATE:

                track_entry->vinfo.frame_rate = read_float_value(einfo.s_data, (int)einfo.s_size);

                break;

            case MATROSKA_ID_VIDEOCOLOUR:
                track_entry->colorPtr =
                        (VideoColour*)alloc_stream_buffer(pbs, sizeof(VideoColour), NON_STREAM);
                memset(track_entry->colorPtr, 0, sizeof(VideoColour));
                read_segment_track_video_colour(pctx, track_entry, offset + einfo.s_offset,
                                                (int)einfo.s_size);
                break;

            default:
                MKV_ERROR_LOG("read_segment_track_video : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    pctx->sctx.has_video_track = TRUE;

    return 0;
}

static int read_segment_track_audio(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;
    TrackEntry* track_entry = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;
    track_entry = &pctx->sctx.track_list[pctx->sctx.track_count - 1];

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_track_audio : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_AUDIOSAMPLINGFREQ:

                track_entry->ainfo.sampling_frequency =
                        (unsigned int)read_float_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_AUDIOOUTSAMPLINGFREQ:

                track_entry->ainfo.output_sampling_freq =
                        (unsigned int)read_float_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_AUDIOCHANNELS:
                track_entry->ainfo.channels =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_AUDIOBITDEPTH:

                track_entry->ainfo.bitdepth =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            default:
                MKV_ERROR_LOG("read_segment_track_audio : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_track_content_encodings(MKVReaderContext* pctx, uint64 offset,
                                                int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;
    TrackEntry* track_entry = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;
    track_entry = &pctx->sctx.track_list[pctx->sctx.track_count - 1];

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG(
                    "read_segment_track_content_encodings : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_TRACKCONTENTENCODING:

                INCREASE_BUFFER_COUNT(track_entry->content_encoding_list,
                                      track_entry->content_encoding_count, TRACK_CONTECT_INC_NUM,
                                      ContentEncodingInfo);

                idx = track_entry->content_encoding_count++;

                track_entry->content_encoding_list[idx].content_encoding_order = 0;
                track_entry->content_encoding_list[idx].content_encoding_scope = 1;
                track_entry->content_encoding_list[idx].content_encoding_type = 0;
                track_entry->content_encoding_list[idx].compression.content_compression_algo = 0;
                track_entry->content_encoding_list[idx]
                        .compression.content_compression_settings.binary_ptr = NULL;
                track_entry->content_encoding_list[idx]
                        .compression.content_compression_settings.binary_offset = 0;
                track_entry->content_encoding_list[idx]
                        .compression.content_compression_settings.binary_size = 0;

                track_entry->content_encoding_list[idx].encryption.enc_algo = 0;
                track_entry->content_encoding_list[idx].encryption.enc_key_id.binary_ptr = NULL;
                track_entry->content_encoding_list[idx].encryption.enc_key_id.binary_offset = 0;
                track_entry->content_encoding_list[idx].encryption.enc_key_id.binary_size = 0;

                read_segment_track_content_encodings(pctx, offset + einfo.s_offset,
                                                     (int)einfo.s_size);

                break;
            case MATROSKA_ID_ENCODINGORDER:

                idx = track_entry->content_encoding_count - 1;
                track_entry->content_encoding_list[idx].content_encoding_order =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_ENCODINGSCOPE:

                idx = track_entry->content_encoding_count - 1;
                track_entry->content_encoding_list[idx].content_encoding_scope =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_ENCODINGTYPE:

                idx = track_entry->content_encoding_count - 1;
                track_entry->content_encoding_list[idx].content_encoding_type =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_ENCODINGCOMPRESSION:

                read_segment_track_content_encodings(pctx, offset + einfo.s_offset,
                                                     (int)einfo.s_size);

                break;
            case MATROSKA_ID_ENCODINGCOMPALGO:

                idx = track_entry->content_encoding_count - 1;
                track_entry->content_encoding_list[idx].compression.content_compression_algo =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_ENCODINGCOMPSETTINGS:
                track_entry->content_encoding_list[idx]
                        .compression.content_compression_settings.binary_ptr = NULL;
                track_entry->content_encoding_list[idx]
                        .compression.content_compression_settings.binary_size = (int)einfo.s_size;
                track_entry->content_encoding_list[idx]
                        .compression.content_compression_settings.binary_offset =
                        offset + einfo.s_offset;
                break;

            case MATROSKA_ID_ENCODINGCONTENTENCRYPTION:
                read_segment_track_content_encodings(pctx, offset + einfo.s_offset,
                                                     (int)einfo.s_size);
                break;

            case MATROSKA_ID_ENCODINGCONTENTENCALGO:
                idx = track_entry->content_encoding_count - 1;
                track_entry->content_encoding_list[idx].encryption.enc_algo =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);
                break;

            case MATROSKA_ID_ENCODINGCONTENTENCKEYID:
                idx = track_entry->content_encoding_count - 1;

                track_entry->content_encoding_list[idx].encryption.enc_key_id.binary_size =
                        (int)einfo.s_size;
                track_entry->content_encoding_list[idx].encryption.enc_key_id.binary_offset =
                        offset + einfo.s_offset;
                seek_stream_buffer(
                        pbs,
                        track_entry->content_encoding_list[idx].encryption.enc_key_id.binary_offset,
                        1);
                track_entry->content_encoding_list[idx].encryption.enc_key_id.binary_ptr =
                        read_non_stream_data(pbs, (int)einfo.s_size);
                break;
            case MATROSKA_ID_ENCODINGCONTENTENCAESSETTINGS:
                read_segment_track_content_encodings(pctx, offset + einfo.s_offset,
                                                     (int)einfo.s_size);
                break;
            case MATROSKA_ID_ENCODINGAESSETTINGSCIPHERMODE:
                idx = track_entry->content_encoding_count - 1;
                track_entry->content_encoding_list[idx].encryption.aes_settings_cipher_mode =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);
                break;
            default:
                MKV_ERROR_LOG("read_segment_track_content_encodings : unhandled ID 0x%llX \n",
                              einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_track_entry(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_track_entry : read_stream_ebml_info failed .\n");
            return -1;
        }
        MKV_LOG("read_segment_track_entry length %llx, offset %llx, einfo.s_id %llx\n", length,
                offset, einfo.s_id);

        switch (einfo.s_id) {
            case MATROSKA_ID_TRACKENTRY:

                INCREASE_BUFFER_COUNT(pctx->sctx.track_list, pctx->sctx.track_count,
                                      TRACK_ENTRY_INC_NUM, TrackEntry);

                idx = pctx->sctx.track_count++;

                memset(&pctx->sctx.track_list[idx], 0, sizeof(TrackEntry));

                // set default value
                pctx->sctx.track_list[idx].flag_enabled = 1;
                pctx->sctx.track_list[idx].flag_default = 1;
                pctx->sctx.track_list[idx].flag_forced = 0;
                pctx->sctx.track_list[idx].flag_lacing = 0;

                strcpy(pctx->sctx.track_list[idx].track_language.str, "eng");
                pctx->sctx.track_list[idx].track_language.strlen = 3;

                pctx->sctx.track_list[idx].vinfo.has_this_info = 0;
                pctx->sctx.track_list[idx].ainfo.has_this_info = 0;
                pctx->sctx.track_list[idx].content_encoding_count = 0;
                pctx->sctx.track_list[idx].content_encoding_list = NULL;
                pctx->sctx.track_list[idx].attach_link_count = 0;
                pctx->sctx.track_list[idx].attach_link_list = NULL;
                pctx->sctx.track_list[idx].codec_priv.binary_ptr = NULL;
                pctx->sctx.track_list[idx].codec_priv.binary_offset = 0;
                pctx->sctx.track_list[idx].codec_priv.binary_size = 0;

                read_segment_track_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;

#ifdef SUPPORT_MKV_DRM
            case MATROSKA_ID_TRACKSDATA:
                pctx->sctx.bHasDRMHdr = TRUE;
                memset(&(pctx->sctx.stDRM_Hdr), 0, sizeof(Tracks_Data_t));
                read_segment_track_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);
                break;

            case MATROSKA_ID_TRACKSDATAVER:
                pctx->sctx.stDRM_Hdr.version = read_uint_value(einfo.s_data, (int)einfo.s_size);
                break;

            case MATROSKA_ID_TRACKSDATASIZE:
                pctx->sctx.stDRM_Hdr.drmHdrSize = read_uint_value(einfo.s_data, (int)einfo.s_size);
                break;

            case MATROSKA_ID_TRACKSDATABUF: {
                uint8* pBuf = NULL;
                uint32 nSize = pctx->sctx.stDRM_Hdr.drmHdrSize + 4;
                pBuf = (uint8*)alloc_stream_buffer(pbs, nSize, NON_STREAM);
                if (pBuf) {
                    if (einfo.s_size < nSize)
                        nSize = einfo.s_size;
                    memcpy(pBuf, einfo.s_data, nSize);
                    pctx->sctx.stDRM_Hdr.pdrmHdr = pBuf;
                } else {
                    MKV_ERROR_LOG("allocate DRM header bufer failed .\n");
                    pctx->sctx.bHasDRMHdr = FALSE;
                }
            }

            break;
#endif

            case MATROSKA_ID_TRACKNUMBER:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].track_num =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                /* Workaround to avoid track_num is not sequence, which will cause error */
                if (pctx->sctx.track_list[idx].track_num != (unsigned int)idx + 1)
                    pctx->sctx.track_list[idx].track_num = idx + 1;
                break;
            case MATROSKA_ID_TRACKUID:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].track_uid =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKTYPE:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].track_type =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKFLAGENABLED:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].flag_enabled =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKFLAGDEFAULT:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].flag_default =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKFLAGFORCED:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].flag_forced =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKFLAGLACING:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].flag_lacing =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKMINCACHE:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].min_cache =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKMAXCACHE:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].max_cache =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKDEFAULTDURATION:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].default_duration =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKTIMECODESCALE:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].track_time_code_scale =
                        read_float_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKNAME:

                idx = pctx->sctx.track_count - 1;
                if (einfo.s_size < sizeof(pctx->sctx.track_list[idx].track_name.str)) {
                    memcpy(pctx->sctx.track_list[idx].track_name.str, einfo.s_data,
                           (int)einfo.s_size);
                    pctx->sctx.track_list[idx].track_name.str[einfo.s_size] = 0;
                    pctx->sctx.track_list[idx].track_name.strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG(
                            "read_segment_track_entry : not enough buffer for track name! \n");
                }

                break;
            case MATROSKA_ID_TRACKLANGUAGE:

                idx = pctx->sctx.track_count - 1;
                if (einfo.s_size < sizeof(pctx->sctx.track_list[idx].track_language.str)) {
                    memcpy(pctx->sctx.track_list[idx].track_language.str, einfo.s_data,
                           (int)einfo.s_size);
                    pctx->sctx.track_list[idx].track_language.str[einfo.s_size] = 0;
                    pctx->sctx.track_list[idx].track_language.strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG(
                            "read_segment_track_entry : not enough buffer for track language! \n");
                }

                break;
            case MATROSKA_ID_CODECID:

                idx = pctx->sctx.track_count - 1;
                if (einfo.s_size < sizeof(pctx->sctx.track_list[idx].track_codecid.str)) {
                    memcpy(pctx->sctx.track_list[idx].track_codecid.str, einfo.s_data,
                           (int)einfo.s_size);
                    pctx->sctx.track_list[idx].track_codecid.str[einfo.s_size] = 0;
                    pctx->sctx.track_list[idx].track_codecid.strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG("read_segment_track_entry : not enough buffer for codec id! \n");
                }

                break;
            case MATROSKA_ID_CODECPRIVATE:

                pctx->sctx.track_list[idx].codec_priv.binary_size = (int)einfo.s_size;
                pctx->sctx.track_list[idx].codec_priv.binary_offset = offset + einfo.s_offset;
                MKV_LOG("codec private: offset: %lld, size: %d \r\n",
                        pctx->sctx.track_list[idx].codec_priv.binary_offset,
                        pctx->sctx.track_list[idx].codec_priv.binary_size);
                break;
            case MATROSKA_ID_CODECNAME:

                idx = pctx->sctx.track_count - 1;
                if (einfo.s_size < sizeof(pctx->sctx.track_list[idx].codec_name.str)) {
                    memcpy(pctx->sctx.track_list[idx].codec_name.str, einfo.s_data,
                           (int)einfo.s_size);
                    pctx->sctx.track_list[idx].codec_name.str[einfo.s_size] = 0;
                    pctx->sctx.track_list[idx].codec_name.strlen = (uint32)einfo.s_size;

                } else {
                    MKV_ERROR_LOG(
                            "read_segment_track_entry : not enough buffer for codec name! \n");
                }

                break;
            case MATROSKA_ID_CODECDELAY:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].codecDelay =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);
                break;

            case MATROSKA_ID_SEEKPREROLL:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].seekPreRoll =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);
                break;

            case MATROSKA_ID_ATTACHLINK:

                INCREASE_BUFFER_COUNT(pctx->sctx.track_list[idx].attach_link_list,
                                      pctx->sctx.track_list[idx].attach_link_count,
                                      ATTACH_LINK_INC_NUM, unsigned int);

                pctx->sctx.track_list[idx]
                        .attach_link_list[pctx->sctx.track_list[idx].attach_link_count++] =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKVIDEO:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].vinfo.has_this_info = 1;
                pctx->sctx.track_list[idx].vinfo.pixel_crop_bottom = 0;
                pctx->sctx.track_list[idx].vinfo.pixel_crop_top = 0;
                pctx->sctx.track_list[idx].vinfo.pixel_crop_left = 0;
                pctx->sctx.track_list[idx].vinfo.pixel_crop_right = 0;
                pctx->sctx.track_list[idx].vinfo.display_unit = 0;

                read_segment_track_video(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKAUDIO:

                idx = pctx->sctx.track_count - 1;
                pctx->sctx.track_list[idx].ainfo.has_this_info = 1;
                pctx->sctx.track_list[idx].ainfo.sampling_frequency = DEFAULT_AUDIO_SAMPLERATE;
                pctx->sctx.track_list[idx].ainfo.output_sampling_freq = 0;
                pctx->sctx.track_list[idx].ainfo.channels = 1;
                pctx->sctx.track_list[idx].ainfo.bitdepth = 16;

                read_segment_track_audio(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKCONTENTENCODINGS:

                read_segment_track_content_encodings(pctx, offset + einfo.s_offset,
                                                     (int)einfo.s_size);

                break;
            case MATROSKA_ID_TRACKMAXBLKADDID:
            case MATROSKA_ID_CODECDECODEALL:
            case MATROSKA_ID_CODECINFOURL:
            case MATROSKA_ID_CODECDOWNLOADURL:
                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_track_entry : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_cluster_blockgroup(MKVReaderContext* pctx, uint64 offset, int64 length,
                                           int* ref_timecode) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;
    ClusterEntry* cluster_entry = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;
    cluster_entry = &pctx->sctx.cluster_list[pctx->sctx.cluster_count - 1];

    INCREASE_BUFFER_COUNT(cluster_entry->group_list, cluster_entry->group_count,
                          BLOCK_GROUP_INC_NUM, BlockGroup);

    idx = cluster_entry->group_count++;

    cluster_entry->group_list[idx].block.binary_ptr = NULL;
    cluster_entry->group_list[idx].block.binary_size = 0;
    cluster_entry->group_list[idx].block.binary_offset = 0;
    cluster_entry->group_list[idx].reference_block_count = 0;
    cluster_entry->group_list[idx].reference_block_list = NULL;
    cluster_entry->group_list[idx].block_duration = MKV_NOPTS_VALUE;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_cluster_blockgroup : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_BLOCK:

                cluster_entry->group_list[idx].block.binary_size = (int)einfo.s_size;
                cluster_entry->group_list[idx].block.binary_offset = offset + einfo.s_offset;

                // Get the Block Data here, which can help non-backward seeking for file reading
                seek_stream_buffer(pbs, offset + einfo.s_offset, 1);
                cluster_entry->group_list[idx].block.binary_ptr =
                        read_non_stream_data(pbs, (int)einfo.s_size);

                break;
            case MATROSKA_ID_BLOCKREFERENCE:

                INCREASE_BUFFER_COUNT(cluster_entry->group_list[idx].reference_block_list,
                                      cluster_entry->group_list[idx].reference_block_count,
                                      REFERENCE_BLOCK_INC_NUM, int);
                *ref_timecode = (int)read_uint_value(einfo.s_data, (int)einfo.s_size);
                cluster_entry->group_list[idx].reference_block_list
                        [cluster_entry->group_list[idx].reference_block_count++] = *ref_timecode;
                MKV_LOG("block ref time: %d \r\n", *ref_timecode);
                break;
            case MATROSKA_ID_BLOCKDURATION:

                cluster_entry->group_list[idx].block_duration =
                        (int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
#ifdef SUPPORT_MKV_DRM
            case MATROSKA_ID_DRMINFO: {
                uint32 nKeySize;

                nKeySize = einfo.s_size;
                if (DRM_FRAME_KEY_SIZE < einfo.s_size) {
                    nKeySize = DRM_FRAME_KEY_SIZE;
                    PARSERMSG("DRM INFO size %d > (default size %d)\n", einfo.s_size,
                              DRM_FRAME_KEY_SIZE);
                }

                memcpy(pctx->frameKey, einfo.s_data, nKeySize);
            } break;
#endif
            default:
                MKV_ERROR_LOG("read_segment_cluster_blockgroup : unhandled ID 0x%llX \n",
                              einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

#ifdef SUPPORT_PRE_SCAN_CLUSTER
static int read_segment_prescan_cluster_index_entry(ByteStream* pbs, MKVReaderContext* pctx,
                                                    int64 base, int64 off, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    int max_cnt = 3;  // reduce parser time for current entry, to avoid case: timecode is located
                      // after blockgroup/simpleblock
    int cur_cnt = 0;
    int valid_cnt = 0;
    int64 offset = 0;

    if (!pctx)
        return -1;
    if (!pbs)
        return -1;

    INCREASE_BUFFER_COUNT(pctx->sctx.prescan_cluster_index_list,
                          pctx->sctx.prescan_cluster_index_count, CLUSTER_INDEX_INC_NUM,
                          ClusterIndexNode);

    idx = pctx->sctx.prescan_cluster_index_count++;

    pctx->sctx.prescan_cluster_index_list[idx].timecode = 0;
    pctx->sctx.prescan_cluster_index_list[idx].offset = base;
    pctx->sctx.prescan_cluster_index_list[idx].s_offset = off;
    pctx->sctx.prescan_cluster_index_list[idx].s_size = length;

    offset = base + off;
    while ((length > 0) && (valid_cnt < 1) && (cur_cnt < max_cnt)) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG(
                    "read_segment_cluster_index_entry : read_stream_ebml_info failed: bytes: %lld, "
                    "length: %lld \r\n",
                    bytes, length);
            return -1;
        }
        cur_cnt++;
        switch (einfo.s_id) {
            case MATROSKA_ID_CLUSTERTIMECODE:
                pctx->sctx.prescan_cluster_index_list[idx].timecode =
                        read_uint_value(einfo.s_data, (int)einfo.s_size);
                MKV_LOG("timecode: %lld \r\n", pctx->sctx.prescan_cluster_index_list[idx].timecode);
                valid_cnt++;
                break;
            case MATROSKA_ID_CLUSTERPOSITION:
                break;
            case MATROSKA_ID_CLUSTERPREVSIZE:
                break;
            case MATROSKA_ID_BLOCKGROUP:
            case MATROSKA_ID_SIMPLEBLOCK:
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_cluster_index_entry : unhandled ID 0x%llX \n",
                              einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    if (valid_cnt < 1) {
        // discard this entry
        pctx->sctx.prescan_cluster_index_count--;
    }
    return 0;
}
#endif

static int read_segment_cue_track_position(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    uint64 rval = 0;
    ByteStream* pbs = NULL;
    CuePointEntry* cue_entry = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;
    cue_entry = &pctx->sctx.cue_list[pctx->sctx.cue_count - 1];

    if (pctx->sctx.track_count > 0) {
        INCREASE_BUFFER_COUNT(cue_entry->cue_track_list, cue_entry->cue_track_count,
                              pctx->sctx.track_count, CueTrackPosition);
    } else {
        INCREASE_BUFFER_COUNT(cue_entry->cue_track_list, cue_entry->cue_track_count,
                              CUE_TRACK_INC_NUM, CueTrackPosition);
    }
    idx = cue_entry->cue_track_count++;

    cue_entry->cue_track_list[idx].cue_track = 0;
    cue_entry->cue_track_list[idx].cue_cluster_position = 0;
    cue_entry->cue_track_list[idx].cue_block_number = 1;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_cue_track_position : read_stream_ebml_info failed .\n");
            return -1;
        }

        rval = read_uint_value(einfo.s_data, (int)einfo.s_size);

        switch (einfo.s_id) {
            case MATROSKA_ID_CUETRACK:

                cue_entry->cue_track_list[idx].cue_track = (unsigned int)rval;

                break;
            case MATROSKA_ID_CUECLUSTERPOSITION:

                cue_entry->cue_track_list[idx].cue_cluster_position = rval;

                break;
            case MATROSKA_ID_CUEBLOCKNUMBER:

                cue_entry->cue_track_list[idx].cue_block_number = (unsigned int)rval;

                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_cuepoint_entry(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_cuepoint_entry : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_POINTENTRY:

                INCREASE_BUFFER_COUNT(pctx->sctx.cue_list, pctx->sctx.cue_count, CUE_POINT_INC_NUM,
                                      CuePointEntry);

                idx = pctx->sctx.cue_count++;

                pctx->sctx.cue_list[idx].cue_track_count = 0;
                pctx->sctx.cue_list[idx].cue_track_list = NULL;

                read_segment_cuepoint_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_CUETIME:

                idx = pctx->sctx.cue_count - 1;
                pctx->sctx.cue_list[idx].cue_time =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_CUETRACKPOSITION:

                read_segment_cue_track_position(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_cuepoint_entry : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_chapter_tracks(MKVReaderContext* pctx, uint64 offset, int64 length,
                                       ChapterTracks* chap_track) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx || !chap_track)
        return -1;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_chapter_tracks : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_CHAPTRACKNUMBER:

                INCREASE_BUFFER_COUNT(chap_track->chapter_track_number,
                                      chap_track->chapter_track_count, CHAPTER_TRACK_INC_NUM,
                                      unsigned int);

                idx = chap_track->chapter_track_count++;

                chap_track->chapter_track_number[idx] =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;

            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_chapter_tracks : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_chapter_display(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;
    ChapterAtom* pChapter_Atom;
    ChapterEdition* chap_entry = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    chap_entry = &pctx->sctx.chapter_list[pctx->sctx.chapter_count - 1];

    pChapter_Atom = &chap_entry->chapter_atom_list[chap_entry->chapter_atom_count - 1];

    INCREASE_BUFFER_COUNT(pChapter_Atom->chapter_display_list, pChapter_Atom->chapter_display_count,
                          CHAPTER_ATOM_INC_NUM, ChapterDisplay);
    idx = pChapter_Atom->chapter_display_count++;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_chapter_tracks : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_CHAPSTRING:

                if (einfo.s_size <
                    sizeof(pChapter_Atom->chapter_display_list[idx].chapter_string.str)) {
                    memcpy(pChapter_Atom->chapter_display_list[idx].chapter_string.str,
                           einfo.s_data, (int)einfo.s_size);
                    pChapter_Atom->chapter_display_list[idx].chapter_string.str[einfo.s_size] = 0;
                    pChapter_Atom->chapter_display_list[idx].chapter_string.strlen =
                            (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG(
                            "read_segment_chapter_tracks : not enough buffer for chapstring! \n");
                }

                break;
            case MATROSKA_ID_CHAPLANG:

                if (einfo.s_size <
                    sizeof(pChapter_Atom->chapter_display_list[idx].chapter_language.str)) {
                    memcpy(pChapter_Atom->chapter_display_list[idx].chapter_language.str,
                           einfo.s_data, (int)einfo.s_size);
                    pChapter_Atom->chapter_display_list[idx].chapter_language.str[einfo.s_size] = 0;
                    pChapter_Atom->chapter_display_list[idx].chapter_language.strlen =
                            (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG(
                            "read_segment_chapter_tracks : not enough buffer for chaplanguage! \n");
                }

                break;
            case MATROSKA_ID_CHAPCOUNTRY:

                if (einfo.s_size <
                    sizeof(pChapter_Atom->chapter_display_list[idx].chapter_country.str)) {
                    memcpy(pChapter_Atom->chapter_display_list[idx].chapter_country.str,
                           einfo.s_data, (int)einfo.s_size);
                    pChapter_Atom->chapter_display_list[idx].chapter_country.str[einfo.s_size] = 0;
                    pChapter_Atom->chapter_display_list[idx].chapter_country.strlen =
                            (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG(
                            "read_segment_chapter_tracks : not enough buffer for chaplanguage! \n");
                }
                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_chapter_tracks : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_chapter_atom(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;
    ChapterEdition* chap_entry = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;
    chap_entry = &pctx->sctx.chapter_list[pctx->sctx.chapter_count - 1];

    INCREASE_BUFFER_COUNT(chap_entry->chapter_atom_list, chap_entry->chapter_atom_count,
                          CHAPTER_ATOM_INC_NUM, ChapterAtom);

    idx = chap_entry->chapter_atom_count++;

    chap_entry->chapter_atom_list[idx].chapter_flag_hidden = 0;
    chap_entry->chapter_atom_list[idx].chapter_flag_enabled = 1;
    chap_entry->chapter_atom_list[idx].chapter_display_count = 0;
    chap_entry->chapter_atom_list[idx].chapter_display_list = NULL;
    chap_entry->chapter_atom_list[idx].chapter_track.chapter_track_count = 0;
    chap_entry->chapter_atom_list[idx].chapter_track.chapter_track_number = NULL;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_chapter_atom : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_CHAPTERUID:

                chap_entry->chapter_atom_list[idx].chapter_uid =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_CHAPTERTIMESTART:

                chap_entry->chapter_atom_list[idx].chapter_time_start =
                        (unsigned int)(read_uint_value(einfo.s_data, (int)einfo.s_size) /
                                       pctx->sctx.info.time_code_scale);

                break;
            case MATROSKA_ID_CHAPTERTIMEEND:

                chap_entry->chapter_atom_list[idx].chapter_time_end =
                        (unsigned int)(read_uint_value(einfo.s_data, (int)einfo.s_size) /
                                       pctx->sctx.info.time_code_scale);

                break;
            case MATROSKA_ID_CHAPTERFLAGHIDDEN:

                chap_entry->chapter_atom_list[idx].chapter_flag_hidden =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_CHAPTERFLAGENABLED:

                chap_entry->chapter_atom_list[idx].chapter_flag_enabled =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_CHAPTERPSEGMETUID:

                chap_entry->chapter_atom_list[idx].chapter_segment_uid =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_CHAPTERPSEGMENTEDITIONUID:

                chap_entry->chapter_atom_list[idx].chapter_segment_edition_uid =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_CHAPTERPTRACKS:

                read_segment_chapter_tracks(pctx, offset + einfo.s_offset, (int)einfo.s_size,
                                            &chap_entry->chapter_atom_list[idx].chapter_track);

                break;
            case MATROSKA_ID_CHAPTERDISPLAY:

                read_segment_chapter_display(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_chapter_atom : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_chapter_edition(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_chapter_editions : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_EDITIONENTRY:

                INCREASE_BUFFER_COUNT(pctx->sctx.chapter_list, pctx->sctx.chapter_count,
                                      CHAPTER_EDITION_INC_NUM, ChapterEdition);

                idx = pctx->sctx.chapter_count++;

                pctx->sctx.chapter_list[idx].edition_flag_hidden = 0;
                pctx->sctx.chapter_list[idx].edition_flag_default = 0;
                pctx->sctx.chapter_list[idx].edition_flag_ordered = 0;
                pctx->sctx.chapter_list[idx].chapter_atom_count = 0;
                pctx->sctx.chapter_list[idx].chapter_atom_list = NULL;

                read_segment_chapter_edition(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_EDITIONUID:

                idx = pctx->sctx.chapter_count - 1;
                pctx->sctx.chapter_list[idx].edition_uid =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_EDITIONFLAGHIDDEN:

                idx = pctx->sctx.chapter_count - 1;
                pctx->sctx.chapter_list[idx].edition_flag_hidden =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_EDITIONFLAGDEFAULT:

                idx = pctx->sctx.chapter_count - 1;
                pctx->sctx.chapter_list[idx].edition_flag_default =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_EDITIONFLAGORDERED:

                idx = pctx->sctx.chapter_count - 1;
                pctx->sctx.chapter_list[idx].edition_flag_ordered =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_CHAPTERATOM:

                read_segment_chapter_atom(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_chapter_editions : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_attachment(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_attachment : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_ATTACHEDFILE:

                INCREASE_BUFFER_COUNT(pctx->sctx.attach_list, pctx->sctx.attach_count,
                                      ATTACH_FILE_INC_NUM, Attachments);

                pctx->sctx.attach_count++;

                read_segment_attachment(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_FILEDESC:

                idx = pctx->sctx.attach_count - 1;
                if (einfo.s_size < sizeof(pctx->sctx.attach_list[idx].file_discription.str)) {
                    memcpy(pctx->sctx.attach_list[idx].file_discription.str, einfo.s_data,
                           (int)einfo.s_size);
                    pctx->sctx.attach_list[idx].file_discription.str[einfo.s_size] = 0;
                    pctx->sctx.attach_list[idx].file_discription.strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG(
                            "read_segment_attachment : not enough buffer for file discription! \n");
                }

                break;
            case MATROSKA_ID_FILENAME:

                idx = pctx->sctx.attach_count - 1;
                if (einfo.s_size < sizeof(pctx->sctx.attach_list[idx].file_name.str)) {
                    memcpy(pctx->sctx.attach_list[idx].file_name.str, einfo.s_data,
                           (int)einfo.s_size);
                    pctx->sctx.attach_list[idx].file_name.str[einfo.s_size] = 0;
                    pctx->sctx.attach_list[idx].file_name.strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG("read_segment_attachment : not enough buffer for file name! \n");
                }

                break;
            case MATROSKA_ID_FILEMIMETYPE:

                idx = pctx->sctx.attach_count - 1;
                if (einfo.s_size < sizeof(pctx->sctx.attach_list[idx].file_minetype.str)) {
                    memcpy(pctx->sctx.attach_list[idx].file_minetype.str, einfo.s_data,
                           (int)einfo.s_size);
                    pctx->sctx.attach_list[idx].file_minetype.str[einfo.s_size] = 0;
                    pctx->sctx.attach_list[idx].file_minetype.strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG(
                            "read_segment_attachment : not enough buffer for file minetype! \n");
                }

                break;
            case MATROSKA_ID_FILEDATA:

                idx = pctx->sctx.attach_count - 1;
                pctx->sctx.attach_list[idx].file_data.binary_ptr = NULL;
                pctx->sctx.attach_list[idx].file_data.binary_size = (int)einfo.s_size;
                pctx->sctx.attach_list[idx].file_data.binary_offset = offset + einfo.s_offset;

                if ((int)einfo.s_size > 0 && (int)einfo.s_size < ATTACHMENT_MAX_SIZE) {
                    pctx->sctx.attach_list[idx].file_data.binary_ptr =
                            alloc_stream_buffer(pbs, (int)einfo.s_size, NON_STREAM);
                    seek_stream_buffer(pbs, pctx->sctx.attach_list[idx].file_data.binary_offset, 1);
                    read_stream_buffer(pbs, &pctx->sctx.attach_list[idx].file_data.binary_ptr,
                                       (int)einfo.s_size);
                }

                break;
            case MATROSKA_ID_FILEUID:

                idx = pctx->sctx.attach_count - 1;
                pctx->sctx.attach_list[idx].file_uid =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            default:
                MKV_ERROR_LOG("read_segment_attachment : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_tag_targets(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;
    TagTarget* tag_target = NULL;

    if (!pctx)
        return -1;
    tag_target = &pctx->sctx.tag_list[pctx->sctx.tag_count - 1].tag_target;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_tag_targets : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_TAGTARGETS_TYPEVALUE:

                tag_target->target_type_value =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TAGTARGETS_TYPE:

                if (einfo.s_size < sizeof(tag_target->target_type.str)) {
                    memcpy(tag_target->target_type.str, einfo.s_data, (int)einfo.s_size);
                    tag_target->target_type.str[einfo.s_size] = 0;
                    tag_target->target_type.strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG(
                            "read_segment_attachment : not enough buffer for tag target type! \n");
                }

                break;
            case MATROSKA_ID_TAGTARGETS_TRACKUID:

                INCREASE_BUFFER_COUNT(tag_target->track_uid, tag_target->track_uid_count,
                                      TAG_UID_INC_NUM, unsigned int);
                idx = tag_target->track_uid_count++;
                tag_target->track_uid[idx] =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TAGTARGETS_EDITIONUID:

                INCREASE_BUFFER_COUNT(tag_target->edition_uid, tag_target->edition_uid_count,
                                      TAG_UID_INC_NUM, unsigned int);
                idx = tag_target->edition_uid_count++;
                tag_target->edition_uid[idx] =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TAGTARGETS_CHAPTERUID:

                INCREASE_BUFFER_COUNT(tag_target->chapter_uid, tag_target->chapter_uid_count,
                                      TAG_UID_INC_NUM, unsigned int);
                idx = tag_target->chapter_uid_count++;
                tag_target->chapter_uid[idx] =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TAGTARGETS_ATTACHUID:

                INCREASE_BUFFER_COUNT(tag_target->attach_uid, tag_target->attach_uid_count,
                                      TAG_UID_INC_NUM, unsigned int);
                idx = tag_target->attach_uid_count++;
                tag_target->attach_uid[idx] =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            default:
                MKV_ERROR_LOG("read_segment_tag_targets : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_simple_tag(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;
    TagEntry* tag_entry = NULL;
    SimpleTag* simple_tag = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;
    tag_entry = &pctx->sctx.tag_list[pctx->sctx.tag_count - 1];

    INCREASE_BUFFER_COUNT(tag_entry->simple_tag_list, tag_entry->simple_tag_count, TAG_LIST_INC_NUM,
                          SimpleTag);
    simple_tag = &tag_entry->simple_tag_list[tag_entry->simple_tag_count++];

    simple_tag->tag_name_count = 0;
    simple_tag->tag_name = NULL;
    strcpy(simple_tag->tag_language.str, "und");
    simple_tag->tag_language.strlen = 3;
    simple_tag->tag_original = 1;
    simple_tag->tag_binary.binary_ptr = NULL;
    simple_tag->tag_binary.binary_offset = 0;
    simple_tag->tag_binary.binary_size = 0;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_simple_tag : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_TAGNAME:

                INCREASE_BUFFER_COUNT(simple_tag->tag_name, simple_tag->tag_name_count,
                                      TAG_LIST_INC_NUM, StringOrUtf8);

                idx = simple_tag->tag_name_count++;

                if (einfo.s_size < sizeof(simple_tag->tag_name[idx].str)) {
                    memcpy(simple_tag->tag_name[idx].str, einfo.s_data, (int)einfo.s_size);
                    simple_tag->tag_name[idx].str[einfo.s_size] = 0;
                    simple_tag->tag_name[idx].strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG("read_segment_attachment : not enough buffer for tag name! \n");
                }

                break;
            case MATROSKA_ID_TAGLANG:

                if (einfo.s_size < sizeof(simple_tag->tag_language.str)) {
                    memcpy(simple_tag->tag_language.str, einfo.s_data, (int)einfo.s_size);
                    simple_tag->tag_language.str[einfo.s_size] = 0;
                    simple_tag->tag_language.strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG(
                            "read_segment_attachment : not enough buffer for tag language! \n");
                }

                break;
            case MATROSKA_ID_TAGORIGINAL:

                simple_tag->tag_original =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TAGDEFAULT:

                simple_tag->tag_default =
                        (unsigned int)read_uint_value(einfo.s_data, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TAGSTRING:

                if (einfo.s_size < sizeof(simple_tag->tag_string.str)) {
                    memcpy(simple_tag->tag_string.str, einfo.s_data, (int)einfo.s_size);
                    simple_tag->tag_string.str[einfo.s_size] = 0;
                    simple_tag->tag_string.strlen = (uint32)einfo.s_size;
                } else {
                    MKV_ERROR_LOG("read_segment_attachment : not enough buffer for tag string! \n");
                }

                break;
            case MATROSKA_ID_TAGBINARY:

                simple_tag->tag_binary.binary_ptr = NULL;
                simple_tag->tag_binary.binary_size = (int)einfo.s_size;
                simple_tag->tag_binary.binary_offset = offset + einfo.s_offset;

                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_simple_tag : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static int read_segment_tag_entry(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int idx = 0;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_segment_tag_entry : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_TAG:

                INCREASE_BUFFER_COUNT(pctx->sctx.tag_list, pctx->sctx.tag_count, TAG_LIST_INC_NUM,
                                      TagEntry);
                idx = pctx->sctx.tag_count++;

                pctx->sctx.tag_list[idx].tag_target.target_type_value = 50;
                pctx->sctx.tag_list[idx].tag_target.track_uid_count = 0;
                pctx->sctx.tag_list[idx].tag_target.track_uid = NULL;
                pctx->sctx.tag_list[idx].tag_target.edition_uid_count = 0;
                pctx->sctx.tag_list[idx].tag_target.edition_uid = NULL;
                pctx->sctx.tag_list[idx].tag_target.chapter_uid_count = 0;
                pctx->sctx.tag_list[idx].tag_target.chapter_uid = NULL;
                pctx->sctx.tag_list[idx].tag_target.attach_uid_count = 0;
                pctx->sctx.tag_list[idx].tag_target.attach_uid = NULL;
                pctx->sctx.tag_list[idx].simple_tag_count = 0;
                pctx->sctx.tag_list[idx].simple_tag_list = NULL;

                read_segment_tag_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TAGTARGETS:

                read_segment_tag_targets(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_SIMPLETAG:

                read_segment_simple_tag(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_tag_entry : unhandled ID 0x%llX \n", einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    return 0;
}

static bool is_all_non_clusters_read(MKVReaderContext* pctx) {
    if (pctx->sctx.is_seek_head_read && pctx->sctx.is_seg_info_read && pctx->sctx.is_tracks_read)
        return TRUE;
    return FALSE;
}

int read_segment_master_header(MKVReaderContext* pctx, uint64 offset, int64 length) {
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;
    bool newSegment = FALSE;
    int i = 0;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    while (length > 0) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (einfo.s_id == MATROSKA_ID_SEGMENT && bytes > length) {
            length -= einfo.s_offset;
            offset += einfo.s_offset;
            newSegment = TRUE;
            pctx->segment_start = offset;
            MKV_LOG("read_segment_master_header new MATROSKA_ID_SEGMENT "
                    "bytes=%lld,len=%lld,segment_start=%lld\n",
                    bytes, length, offset);
            continue;
        }

        if (!bytes || length < bytes) {
            if (eof_stream_buffer(pbs))
                return 0;

            MKV_ERROR_LOG("read_segment_master_header : read_stream_ebml_info failed .\n");
            return -1;
        }

        switch (einfo.s_id) {
            case MATROSKA_ID_SEEKHEAD:
                pctx->sctx.is_seek_head_read = TRUE;
                if (newSegment) {
                    if (pctx->sctx.seek_list)
                        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.seek_list, NON_STREAM);
                    pctx->sctx.seek_list = NULL;
                    pctx->sctx.seek_count = 0;
                    MKV_LOG("read_segment_master_header free MATROSKA_ID_SEEKHEAD \n");
                }
                read_segment_seek_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_INFO:
                pctx->sctx.is_seg_info_read = TRUE;
                read_segment_id_info(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;

            case MATROSKA_ID_TRACKS:
                /*H264_MP51_640x368_23.98_476_mp3_48_2_nostreamfound.mkv has entries for tracks in
                 * seekhead, the track elements will be parsed while parsing level 1 elements, but
                 * when parsing child elements in seekhead(level 2 element),
                 * read_segment_track_entry will be called again, and the same elements(track
                 * entries) in file will be parsed twice,thus got wrong track count.*/

                if (newSegment && pctx->sctx.is_tracks_read) {
                    for (i = 0; i < pctx->sctx.track_count; ++i)
                        close_segment_track_entry(pctx, &pctx->sctx.track_list[i]);

                    if (pctx->sctx.track_list)
                        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.track_list, NON_STREAM);
                    pctx->sctx.track_list = NULL;
                    pctx->sctx.track_count = 0;
                    pctx->sctx.is_tracks_read = FALSE;
                    MKV_LOG("read_segment_master_header free MATROSKA_ID_TRACKS \n");
                }
                if (!pctx->sctx.is_tracks_read) {
                    pctx->sctx.is_tracks_read = TRUE;
                    read_segment_track_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);
                }

                break;
            case MATROSKA_ID_CLUSTER:
                if (!pctx->sctx.first_cluster_pos)
                    pctx->sctx.first_cluster_pos = offset;
                if (is_all_non_clusters_read(pctx))
                    return 0;
                break;
            case MATROSKA_ID_CUES:
                if (newSegment) {
                    clear_matroska_cue_list(pctx);
                }

                MKV_TIMER_START();
                read_segment_cuepoint_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);
                MKV_TIMER_STOP("read cuepoint");
                MKV_LOG("read_segment_master_header cuepoints: %d \r\n", pctx->sctx.cue_count);
                break;
            case MATROSKA_ID_CHAPTERS:

                if (newSegment) {
                    for (i = 0; i < pctx->sctx.chapter_count; i++)
                        close_segment_chapter_edition(pctx, &pctx->sctx.chapter_list[i]);

                    if (pctx->sctx.mkv_chapterMenu.pChapterList) {
                        free_stream_buffer(&pctx->bs,
                                           (char*)pctx->sctx.mkv_chapterMenu.pChapterList,
                                           NON_STREAM);
                        pctx->sctx.mkv_chapterMenu.pChapterList = NULL;
                    }

                    if (pctx->sctx.chapter_list)
                        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.chapter_list, NON_STREAM);
                    pctx->sctx.chapter_list = NULL;
                    pctx->sctx.chapter_count = 0;
                    MKV_LOG("read_segment_master_header free MATROSKA_ID_CHAPTERS \n");
                }

                pctx->sctx.is_chapters_read = TRUE;
                read_segment_chapter_edition(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_ATTACHMENTS:

                if (newSegment && pctx->sctx.is_attatchments_read) {
                    for (i = 0; i < pctx->sctx.attach_count; i++)
                        close_segment_attachment(pctx, &pctx->sctx.attach_list[i]);

                    if (pctx->sctx.attach_list)
                        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.attach_list, NON_STREAM);
                    pctx->sctx.attach_list = NULL;
                    pctx->sctx.attach_count = 0;
                    MKV_LOG("read_segment_master_header free MATROSKA_ID_ATTACHMENTS \n");
                }

                pctx->sctx.is_attatchments_read = TRUE;
                read_segment_attachment(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case MATROSKA_ID_TAGS:

                if (newSegment) {
                    for (i = 0; i < pctx->sctx.tag_count; i++)
                        close_segment_tags_entry(pctx, &pctx->sctx.tag_list[i]);

                    if (pctx->sctx.tag_list)
                        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.tag_list, NON_STREAM);
                    pctx->sctx.tag_list = NULL;
                    pctx->sctx.tag_count = 0;
                    MKV_LOG("read_segment_master_header free MATROSKA_ID_TAGS \n");
                }

                pctx->sctx.is_tags_read = TRUE;
                read_segment_tag_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);

                break;
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                // don't care me here
                break;
            default:
                MKV_ERROR_LOG("read_segment_master_header : unhandled ID 0x%X, off: %lld \n",
                              (uint32)einfo.s_id, offset);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    if (!pctx->sctx.first_cluster_pos)
        return -1;

    return 0;
}

#ifdef SUPPORT_PRE_SCAN_CLUSTER
int read_segment_prescan_cluster_index_list(MKVReaderContext* pctx) {
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;
    uint64 before_pos;
    int64 offset;
    int64 length;
    int max_cluster = 0;

    int total_clusterid_cnt = 0;
    int ret = 0;

    if (!pctx)
        return -1;

    max_cluster = pctx->sctx.prescan_cluster_maxcnt;

    before_pos = get_stream_position(&pctx->bs);

    pbs = &pctx->bs;  // use global ByteStream instance directly

    MKV_LOG("read_segment_prescan_cluster_index_list");

    offset = pctx->sctx.prescan_segment_offset;
    length = pctx->sctx.prescan_segment_size;

    while ((length > 0) && (total_clusterid_cnt < max_cluster)) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("read_cluser_index failed \r\n");
            goto FINISH_READ_CLUSTER;
        }

        MKV_LOG("bytes: %lld, length: %lld, offset: %lld, einfo.s_id: %lld\n", bytes, length,
                offset, einfo.s_id);

        switch (einfo.s_id) {
            case MATROSKA_ID_CLUSTER:
                total_clusterid_cnt++;
                ret = read_segment_prescan_cluster_index_entry(pbs, pctx, offset, einfo.s_offset,
                                                               (int)einfo.s_size);
                if (ret == -1) {
                    MKV_ERROR_LOG("error: matroska_load_cluster \r\n");
                    goto FINISH_READ_CLUSTER;
                }
                MKV_LOG("find cluster: offset: %lld, length: %lld \r\n", offset,
                        einfo.s_offset + einfo.s_size);
                break;
            default:
                MKV_LOG("%s: skip ID 0x%llX \r\n", __FUNCTION__, einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

FINISH_READ_CLUSTER:

    pctx->sctx.prescan_cluster_totalcnt = total_clusterid_cnt;
    pctx->sctx.prescan_cluster_done = TRUE;
    MKV_LOG("total cluster: %d, cluster count: %d, interval: %d, cache size: %d, max ebml offset: "
            "%d \r\n",
            total_clusterid_cnt, pctx->sctx.prescan_cluster_index_count, 0, pbs->default_cachesize,
            pbs->default_max_ebml_offset);

    seek_stream_buffer(&pctx->bs, before_pos, 0);

    return 0;
}
#endif

int read_matroska_segment_header(MKVReaderContext* pctx) {
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;

    bytes = read_stream_ebml_info(pbs, pctx->ebml_header_size, &einfo);
    if (!bytes) {
        MKV_ERROR_LOG("read_matroska_segment_header : can't read stream ebml .\n");
        return -1;
    }

    if (einfo.s_id != MATROSKA_ID_SEGMENT) {
        MKV_ERROR_LOG("read_matroska_segment_header : invalid segment master ID .\n");
        return -1;
    }

    pctx->segm_header_size = bytes;
    pctx->segm_master_size = einfo.s_offset;
    pctx->segment_start = pctx->ebml_header_size + pctx->segm_master_size;
    pctx->sctx.seek_count = 0;
    pctx->sctx.seek_list = NULL;
    pctx->sctx.track_count = 0;
    pctx->sctx.track_list = NULL;
    pctx->sctx.cluster_count = 0;
    pctx->sctx.cluster_list = NULL;
#ifdef SUPPORT_CORRUPT_CLUSTER
    pctx->sctx.continuous_bad_cluster_cnt = 0;
#endif
    pctx->sctx.cue_count = 0;
    pctx->sctx.cue_list = NULL;
    pctx->sctx.chapter_count = 0;
    pctx->sctx.chapter_list = NULL;
    pctx->sctx.attach_count = 0;
    pctx->sctx.attach_list = NULL;
    pctx->sctx.tag_count = 0;
    pctx->sctx.tag_list = NULL;
    pctx->sctx.first_cluster_pos = 0;
    pctx->sctx.info.time_code_scale = 1000000;

#ifdef SUPPORT_PRE_SCAN_CLUSTER
    pctx->sctx.prescan_cluster_index_list = NULL;
    pctx->sctx.prescan_cluster_index_count = 0;
    pctx->sctx.prescan_segment_offset = pctx->ebml_header_size + pctx->segm_master_size;
    pctx->sctx.prescan_segment_size = bytes - einfo.s_offset;
    pctx->sctx.prescan_cluster_interval = CLUSTER_INTERVAL;
    pctx->sctx.prescan_cluster_maxcnt = MAX_CLUSTER_CNT;
    pctx->sctx.prescan_cluster_totalcnt = 0;
    pctx->sctx.prescan_cluster_done = FALSE;
    pctx->sctx.prescan_fuzzy_seek_enable = 0;
#endif

#ifdef SUPPORT_MKV_DRM
    pctx->sctx.bHasDRMHdr = FALSE;
#endif
    pctx->sctx.has_cue = FALSE;
    pctx->sctx.cue_parsed = FALSE;

    return read_segment_master_header(pctx, pctx->segment_start, bytes - einfo.s_offset);
}

uint32 InitGetBits(BitsCtx* p, uint8* buf, uint32 size) {
    if (!p) {
        return 0;
    }

    p->buf_a = 0;
    p->buf_b = 0;
    p->bitcnt = 32;
    p->buf_idx = 0;
    p->buf_ptr = buf;
    p->bufsize = size;

    if (p->buf_idx + 4 < p->bufsize) {
        p->buf_b = (uint32)((buf[0] << 24) + (buf[1] << 16) + (buf[2] << 8) + buf[3]);
    } else {
        uint32 i = 0;
        uint32 rem = 0;

        while (i < p->bufsize - p->buf_idx) rem = (rem << 8) | buf[i++];

        p->buf_b = rem << (32 - i * 8);
    }

    p->buf_idx += 4;

    return 1;
}

uint32 GetBits(BitsCtx* p, uint32 n) {
    int32 nbit = 0;
    uint32 out = 0;
    uint8* pbuf = NULL;
    uint32 bitcnt = p->bitcnt;
    uint32 bit_a = p->buf_a;

    nbit = (n + bitcnt) - 32;

    if (nbit <= 0) {
        out = (bit_a << bitcnt >> (32 - n));
        p->bitcnt += n;
    } else {
        uint32 bit_b = p->buf_b;
        out = ((bit_a << bitcnt >> (32 - n)) | (bit_b >> (32 - nbit)));

        p->bitcnt = nbit;
        p->buf_a = bit_b;

        pbuf = &p->buf_ptr[p->buf_idx];
        if (p->buf_idx + 4 < p->bufsize) {
            p->buf_b = (uint32)((pbuf[0] << 24) + (pbuf[1] << 16) + (pbuf[2] << 8) + pbuf[3]);
        } else if (p->buf_idx < p->bufsize) {
            uint32 i = 0;
            uint32 rem = 0;

            while (i < p->bufsize - p->buf_idx) rem = (rem << 8) | pbuf[i++];

            p->buf_b = rem << (32 - i * 8);
        } else
            p->buf_b = 0;

        p->buf_idx += 4;
    }

    return out;
}

static uint32 InitPutBits(BitsCtx* pbc, void* buf) {
    if (!pbc)
        return 0;

    pbc->buf_a = 0;
    pbc->bitcnt = 0;
    pbc->buf_idx = 0;
    pbc->buf_ptr = (uint8*)buf;

    return 1;
}

static uint32 PutBits(BitsCtx* pbc, uint32 val, uint32 len) {
    uint32 shift = 0;
    uint32 rem_cnt = 0;

    if (!pbc)
        return 0;

    rem_cnt = pbc->bitcnt + len;

    if (32 <= rem_cnt)
        shift = 0;
    else
        shift = 32 - rem_cnt;

    pbc->buf_a |= (val << shift);

    while (rem_cnt >= 8) {
        pbc->buf_ptr[pbc->buf_idx++] = (pbc->buf_a >> 24) & 0xFF;
        pbc->buf_a <<= 8;
        rem_cnt -= 8;
    };

    if (pbc->bitcnt + len > 32) {
        pbc->buf_a = val >> (64 - pbc->bitcnt + len);
    }

    pbc->bitcnt = rem_cnt;

    return pbc->buf_idx * 8 + pbc->bitcnt;
}

static int matroska_decode_buffer(MKVReaderContext* pctx, uint8** buf, int* buf_size,
                                  TrackEntry* track) {
    ContentEncodingInfo* encodings = track->content_encoding_list;
    ByteStream* pbs = &pctx->bs;
    uint8* data = *buf;
    int tempLen;
    (void)buf_size;

    if (MATROSKA_TRACK_ENCODING_COMP_ALGO_HEADERSTRIP ==
        encodings[0].compression.content_compression_algo) {
        tempLen = encodings[0].compression.content_compression_settings.binary_size;
        if (tempLen && !encodings[0].compression.content_compression_settings.binary_ptr) {
            encodings[0].compression.content_compression_settings.binary_ptr =
                    alloc_stream_buffer(pbs, tempLen + 8, NON_STREAM);
            data = (uint8*)encodings[0].compression.content_compression_settings.binary_ptr;
            seek_stream_buffer(
                    pbs, encodings[0].compression.content_compression_settings.binary_offset, 1);
            read_stream_buffer(pbs, (char**)(&data), tempLen);
        }
        return tempLen;

    } else {
        return -1;
    }
}

static void matroska_execute_seekhead(MKVReaderContext* pctx) {
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = &pctx->bs;
    int i, seek_count = pctx->sctx.seek_count;
    uint64 segment_start = pctx->segment_start;
    SeekEntry* seek_list = pctx->sctx.seek_list;
    uint64 offset, before_pos = get_stream_position(pbs);

    for (i = 0; i < seek_count; i++) {
        offset = seek_list[i].seekpos + segment_start;

        if (seek_list[i].seekpos <= before_pos || seek_list[i].seekid == MATROSKA_ID_SEEKHEAD ||
            seek_list[i].seekid == MATROSKA_ID_CLUSTER)
            continue;

        if (seek_stream_buffer(pbs, offset, 0) == MKV_INVALID_SEEK_POS) {
            MKV_ERROR_LOG("matroska_execute_seekhead : seek_stream_buffer failed.\n");
            continue;
        }

        bytes = read_stream_ebml_info(pbs, offset, &einfo);
        if (!bytes) {
            MKV_ERROR_LOG("matroska_execute_seekhead : can't read stream ebml .\n");
            continue;
        }

        if (seek_list[i].seekid == MATROSKA_ID_CUES) {
            pctx->sctx.has_cue = TRUE;
            MKV_LOG("skip cue list");
            continue;
        }

        seek_stream_buffer(pbs, offset, 0);
        read_segment_master_header(pctx, offset, bytes);
    }

    seek_stream_buffer(pbs, before_pos, 0);
}

TrackEntry* matroska_find_track_by_num(MKVReaderContext* pctx, int track_num) {
    int track_count = pctx->sctx.track_count;
    TrackEntry* tracks = pctx->sctx.track_list;
    int i;

    for (i = 0; i < track_count; i++) {
        if (tracks[i].track_num == (unsigned int)track_num) {
            return &tracks[i];
        }
    }

    MKV_ERROR_LOG("matroska_find_track_by_num : Invalid track number %d\n", track_num);

    return NULL;
}

static int index_compare(const void* arg1, const void* arg2) {
    CueIndex* idx1 = (CueIndex*)arg1;
    CueIndex* idx2 = (CueIndex*)arg2;

    return idx1->pts > idx2->pts ? 1 : -1;
}

#define MKV_KEY_FRAME_FLAG 0x1
#define MKV_CUE_LIST_FLAG 0x2
static int index_search_timestamp(mkvStream* stream, int64 desired_timestamp, int flags);

static int mkv_add_index_entry(MKVReaderContext* pctx, mkvStream* stream, uint64 pos, int64 pts,
                               int block_num, int flags, bool search) {
    int idx_pos = 0;
    CueIndex* index = NULL;
    ByteStream* pbs = &pctx->bs;

    if (!stream)
        return -1;

    if (search) {
        idx_pos = index_search_timestamp(stream, pts, SEEK_FLAG_NEAREST);
    } else
        idx_pos = stream->index_count - 1;

    // idx_pos is smaller than stream->index_count after search
    if ((idx_pos >= 0) && (stream->index_list[idx_pos].pos == pos)) {
        if (stream->index_list[idx_pos].block_num == (uint32)block_num)
            return 1;

        // only add one audio index in one cluster
        if (stream->track_type == MATROSKA_TRACK_TYPE_AUDIO)
            return 1;

        // some file 's cue list has no block num value and pos is cluster posoion.
        // however, video frame maybe not the first sample in the cluster.
        // modify block number get from cue list to the actual block number.
        if ((stream->index_list[idx_pos].flags & MKV_CUE_LIST_FLAG) &&
            (stream->index_list[idx_pos].block_num == 1)) {
            stream->index_list[idx_pos].block_num = block_num;
            stream->index_list[idx_pos].flags = MKV_KEY_FRAME_FLAG;
            return 1;
        }
    }

    INCREASE_BUFFER_COUNT(stream->index_list, stream->index_count, STREAM_LIST_INC_NUM, CueIndex);

    index = &stream->index_list[stream->index_count++];

    index->pos = pos;
    index->pts = pts;
    index->flags = flags;
    index->block_num = block_num;

    return 0;
}

static int mkv_index_quick_sort(mkvStream* stream) {
    if (!stream)
        return -1;
    qsort(stream->index_list, stream->index_count, sizeof(CueIndex), index_compare);

    return 0;
}

static int matroska_aac_profile(char* codec_id) {
    static const char* const aac_profiles[] = {"MAIN", "LC", "SSR"};
    int profile;

    for (profile = 0; profile < (int)(sizeof(aac_profiles) / sizeof(aac_profiles[0])); profile++)
        if (strstr(codec_id, aac_profiles[profile]))
            break;
    return profile + 1;
}

const int mpeg4audio_sample_rates[16] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                         22050, 16000, 12000, 11025, 8000,  7350};

static int matroska_aac_sri(int samplerate) {
    int sri;

    for (sri = 0; sri < (int)(sizeof(mpeg4audio_sample_rates) / sizeof(mpeg4audio_sample_rates[0]));
         sri++) {
        if (mpeg4audio_sample_rates[sri] == samplerate)
            break;
    }

    return sri;
}

#define CHANNEL_FRONT_LEFT 0x1
#define CHANNEL_FRONT_RIGHT 0x2
#define CHANNEL_FRONT_CENTER 0x4
#define CHANNEL_LOW_FREQUENCY 0x8
#define CHANNEL_BACK_LEFT 0x10
#define CHANNEL_BACK_RIGHT 0x20

#define MKV_INPUT_BUFFER_PADDING_SIZE 16

static int mkv_get_wave_type(WaveFormatEx* waveinfo, unsigned char* pb, int size,
                             int* extradata_offset) {
    uint32 codecid = 0;

    codecid = waveinfo->wFormatTag = *(unsigned short*)pb;
    pb += 2;

    waveinfo->u16Channels = *(unsigned short*)pb;
    pb += 2;

    waveinfo->u32SamplesPerSec = *(unsigned long*)pb;
    pb += 4;

    waveinfo->nAvgBytesPerSec = (*(unsigned long*)pb);
    pb += 4;

    waveinfo->nBlockAlign = *(unsigned short*)pb;
    pb += 2;

    if (size == 14) { /* We're dealing with plain vanilla WAVEFORMAT */
        waveinfo->u16BitsPerSample = 8;
    } else {
        waveinfo->u16BitsPerSample = *(unsigned short*)pb;
        pb += 2;
    }

    if (size >= 18) {                      /* We're obviously dealing with WAVEFORMATEX */
        int cbSize = *(unsigned short*)pb; /* cbSize */
        pb += 2;

        *extradata_offset = 18;

        size -= 18;
        cbSize = (size < cbSize) ? size : cbSize;
        if (cbSize >= 22 && codecid == 0xfffe) { /* WAVEFORMATEXTENSIBLE */
            waveinfo->u16BitsPerSample = *(unsigned short*)pb;
            pb += 2;

            waveinfo->dwChannelMask = *(unsigned long*)pb; /* dwChannelMask */
            pb += 4;

            codecid = *(unsigned long*)pb; /* 4 first bytes of GUID */
            *extradata_offset += 22;       /* need skip the 22byte in the wavformatextensible
                                              2(u16BitsperSample)+4(dwChannelMask)+16(GUID) */
        } else if (cbSize >= 6 && (codecid == 0x162 || codecid == 0x163)) {
            waveinfo->u16BitsPerSample = *(unsigned short*)pb;
            pb += 2;

            waveinfo->dwChannelMask = *(unsigned long*)pb; /* dwChannelMask */
        } else if (codecid == 0x161) {
            switch (waveinfo->u16Channels) {
                case 1:
                    waveinfo->dwChannelMask = CHANNEL_FRONT_CENTER;
                    break;
                case 2:
                    waveinfo->dwChannelMask = CHANNEL_FRONT_LEFT | CHANNEL_FRONT_RIGHT;
                    break;
                case 6:
                    // Only to support pseudo V3 streams
                    waveinfo->dwChannelMask =
                            (CHANNEL_FRONT_LEFT | CHANNEL_FRONT_RIGHT | CHANNEL_FRONT_CENTER |
                             CHANNEL_BACK_LEFT | CHANNEL_BACK_RIGHT | CHANNEL_LOW_FREQUENCY);
                    break;
                default:
                    break;
            }

        } else if (codecid == 0x160) {
            switch (waveinfo->u16Channels) {
                case 1:
                    waveinfo->dwChannelMask = CHANNEL_FRONT_CENTER;
                    break;
                case 2:
                    waveinfo->dwChannelMask = CHANNEL_FRONT_LEFT | CHANNEL_FRONT_RIGHT;
                    break;
                default:
                    break;
            }
        }
    }

    return mkv_codec_get_type(codecid, 0);
}

static int is_existed_track(MKVReaderContext* pctx, TrackEntry* track) {
    int i = 0;
    TrackEntry* atrack = NULL;

    if (!pctx || !track)
        return 0;

    if (!pctx->stream_count || !pctx->stream_list)
        return 0;

    for (i = 0; i < pctx->stream_count; i++) {
        atrack = matroska_find_track_by_num(pctx, pctx->stream_list[i].stream_index);
        if (atrack == NULL)
            return 0;

        if (track == atrack)
            return 1;

        if (track->track_num == atrack->track_num && track->track_uid == atrack->track_uid &&
            track->track_type == atrack->track_type &&
            track->flag_enabled == atrack->flag_enabled &&
            track->flag_default == atrack->flag_default &&
            track->flag_forced == atrack->flag_forced &&
            track->flag_lacing == atrack->flag_lacing &&
            track->default_duration == atrack->default_duration &&
            track->track_time_code_scale == atrack->track_time_code_scale &&
            !strcmp(track->track_codecid.str, atrack->track_codecid.str)) {
            if (track->track_type == MATROSKA_TRACK_TYPE_VIDEO && track->vinfo.has_this_info &&
                atrack->vinfo.has_this_info) {
                if (track->vinfo.pixel_width == atrack->vinfo.pixel_width &&
                    track->vinfo.pixel_height == atrack->vinfo.pixel_height &&
                    track->vinfo.flag_interlaced == atrack->vinfo.flag_interlaced &&
                    track->vinfo.pixel_crop_bottom == atrack->vinfo.pixel_crop_bottom &&
                    track->vinfo.pixel_crop_top == atrack->vinfo.pixel_crop_top &&
                    track->vinfo.pixel_crop_left == atrack->vinfo.pixel_crop_left &&
                    track->vinfo.pixel_crop_right == atrack->vinfo.pixel_crop_right &&
                    track->vinfo.display_width == atrack->vinfo.display_width &&
                    track->vinfo.display_height == atrack->vinfo.display_height &&
                    track->vinfo.display_unit == atrack->vinfo.display_unit) {
                    return 1;
                }
            } else if (track->track_type == MATROSKA_TRACK_TYPE_AUDIO &&
                       track->ainfo.has_this_info && atrack->ainfo.has_this_info) {
                if (track->ainfo.sampling_frequency == atrack->ainfo.sampling_frequency &&
                    track->ainfo.channels == atrack->ainfo.channels &&
                    track->ainfo.bitdepth == atrack->ainfo.bitdepth) {
                    return 1;
                }
            } else if (track->track_type == MATROSKA_TRACK_TYPE_SUBTITLE) {
                return 1;
            }
        }
    }

    return 0;
}

/*
 * Sampling Frequency look up table
 * The look up index is found in the
 * header of an ADTS packet
 */
static const int32 ADTSSampleFreqTable[16] = {
        96000, /* 96000 Hz */
        88200, /* 88200 Hz */
        64000, /* 64000 Hz */
        48000, /* 48000 Hz */
        44100, /* 44100 Hz */
        32000, /* 32000 Hz */
        24000, /* 24000 Hz */
        22050, /* 22050 Hz */
        16000, /* 16000 Hz */
        12000, /* 12000 Hz */
        11025, /* 11025 Hz */
        8000,  /*  8000 Hz */
        7350,  /*  7350 Hz */
        -1,    /* future use */
        -1,    /* future use */
        -1     /* escape value */
};

#define MATROSKA_TIME_BASE 1000000
#define SWAP_16(x) ((x) = (((x) >> 8) & 0xff) + (((x) & 0xff) << 8))
#define SWAP_32(x)                                                                            \
    ((x) = (((x) >> 24) & 0xff) + ((((x) >> 16) & 0xff) << 8) + ((((x) >> 8) & 0xff) << 16) + \
           (((x) & 0xff) << 24))

int read_matroska_file_header(MKVReaderContext* pctx) {
    uint64 position = 0;
    ByteStream* pbs = &pctx->bs;

    if (!pctx)
        return -1;

    if (-1 == read_ebml_master_header(pctx))
        return -1;

    if (-1 == read_matroska_segment_header(pctx))
        return -1;

    matroska_execute_seekhead(pctx);

    pctx->duration_us = (uint64)pctx->sctx.info.duration;

    if (pctx->sctx.info.time_code_scale) {
        pctx->duration_us = (uint64)pctx->duration_us * pctx->sctx.info.time_code_scale * 1000 /
                            MATROSKA_TIME_BASE;
    }
    matroska_file_update_track(pctx);

    matroska_get_codec_data_from_frame(pctx);

    MKV_LOG("seek the first cluster pos: %lld \r\n", pctx->sctx.first_cluster_pos);
    seek_stream_buffer(pbs, pctx->sctx.first_cluster_pos, 1);
    position = get_stream_position(pbs);

    if (position != pctx->sctx.first_cluster_pos) {
        MKV_ERROR_LOG("read_matroska_file_header : Seek to first cluster failed\n");
    }

    return 0;
}

int matroska_file_update_track(MKVReaderContext* pctx) {
    int i;
    uint32 j;
    int track_count = 0;

    mkvStream* stream = NULL;
    TrackEntry* track_list = NULL;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    pbs = &pctx->bs;
    pctx->stream_count = 0;
    pctx->stream_list = NULL;

    track_list = pctx->sctx.track_list;
    track_count = pctx->sctx.track_count;

    for (i = 0; i < track_count; i++) {
        int extradata_size = 0;
        uint8* extradata = NULL;
        int extradata_offset = 0;
        TrackEntry* track = &track_list[i];
        track->stream = NULL;
        if (track->track_type != MATROSKA_TRACK_TYPE_VIDEO &&
            track->track_type != MATROSKA_TRACK_TYPE_AUDIO &&
            track->track_type != MATROSKA_TRACK_TYPE_SUBTITLE) {
            MKV_ERROR_LOG("read_matroska_file_header : Unknown or unsupported track type %d \n",
                          track->track_type);
            continue;
        }

        if (!track->track_codecid.str[0])
            continue;

        if (is_existed_track(pctx, track))
            continue;

        INCREASE_BUFFER_COUNT(pctx->stream_list, pctx->stream_count, STREAM_LIST_INC_NUM,
                              mkvStream);

        stream = &pctx->stream_list[pctx->stream_count];
        memset(stream, 0, sizeof(mkvStream));

        stream->track_enable = 0;
        stream->track_type = MATROSKA_TRACK_TYPE_NONE;
        stream->ext_tag_list = matroska_create_track_ext_taglist(pbs);

        if (track->track_type == MATROSKA_TRACK_TYPE_VIDEO) {
            if (!track->vinfo.has_this_info)
                continue;

            if (!track->default_duration) {
                if (track->vinfo.frame_rate != 0.0)
                    track->default_duration = (unsigned int)(1000000000 / track->vinfo.frame_rate);
                else
                    track->default_duration = 0;
            }
            if (!track->vinfo.display_width)
                track->vinfo.display_width = track->vinfo.pixel_width;
            if (!track->vinfo.display_height)
                track->vinfo.display_height = track->vinfo.pixel_height;

            stream->track_type = MATROSKA_TRACK_TYPE_VIDEO;

            for (j = 0; j < sizeof(mkv_video_codec_tags) / sizeof(mkv_video_codec_tags[0]); j++) {
                if (!strncmp(mkv_video_codec_tags[j].str, track->track_codecid.str,
                             mkv_video_codec_tags[j].strsize - 1)) {
                    stream->codec_type = mkv_video_codec_tags[j].codec_type;
                    break;
                }
            }
        } else if (track->track_type == MATROSKA_TRACK_TYPE_AUDIO) {
            if (!track->ainfo.has_this_info)
                continue;

            if (!track->ainfo.output_sampling_freq)
                track->ainfo.output_sampling_freq = track->ainfo.sampling_frequency;

            stream->track_type = MATROSKA_TRACK_TYPE_AUDIO;
            for (j = 0; j < sizeof(mkv_audio_codec_tags) / sizeof(mkv_audio_codec_tags[0]); j++) {
                if (!strncmp(mkv_audio_codec_tags[j].str, track->track_codecid.str,
                             mkv_audio_codec_tags[j].strsize - 1)) {
                    stream->codec_type = mkv_audio_codec_tags[j].codec_type;
                    break;
                }
            }
        } else if (track->track_type == MATROSKA_TRACK_TYPE_SUBTITLE) {
            stream->track_type = MATROSKA_TRACK_TYPE_SUBTITLE;

            for (j = 0; j < sizeof(mkv_track_media_tags) / sizeof(mkv_track_media_tags[0]); j++) {
                if (!strncmp(mkv_track_media_tags[j].str, track->track_codecid.str,
                             mkv_track_media_tags[j].strsize)) {
                    stream->codec_type = mkv_track_media_tags[j].codec_type;
                    break;
                }
            }
        }

        if (!strcmp(track->track_codecid.str, "V_MS/VFW/FOURCC") &&
            track->codec_priv.binary_size >= 40 && track->codec_priv.binary_offset != 0) {
            uint32 video_fourcc = 0;
            if (!track->codec_priv.binary_ptr) {
                track->codec_priv.binary_ptr =
                        alloc_stream_buffer(pbs, track->codec_priv.binary_size, NON_STREAM);
                seek_stream_buffer(pbs, track->codec_priv.binary_offset, 1);
                read_stream_buffer(pbs, &track->codec_priv.binary_ptr,
                                   track->codec_priv.binary_size);
            }
            video_fourcc = (uint32)read_uint_value(track->codec_priv.binary_ptr + 16, 4);
            stream->codec_type = (uint8)mkv_codec_get_type(video_fourcc, 1);
            extradata_offset = 40;
            // do not output codec data for HEVC in V_MS/VFW/FOURCC
            // for clip HEVC_848x480_24fps_MP2_48Khz_2ch_320Kbps_Sintel-Trailer.mkv
            if (MKV_VIDEO_HEVC == stream->codec_type)
                extradata_offset = track->codec_priv.binary_size;
        } else if (!strcmp(track->track_codecid.str, "A_MS/ACM") &&
                   track->codec_priv.binary_size >= 18 && track->codec_priv.binary_offset != 0) {
            if (!track->codec_priv.binary_ptr) {
                track->codec_priv.binary_ptr =
                        alloc_stream_buffer(pbs, track->codec_priv.binary_size, NON_STREAM);
                seek_stream_buffer(pbs, track->codec_priv.binary_offset, 1);
                read_stream_buffer(pbs, &track->codec_priv.binary_ptr,
                                   track->codec_priv.binary_size);
            }

            stream->has_wave_info = 1;
            memset(&stream->waveinfo, 0, sizeof(WaveFormatEx));
            stream->codec_type = (uint8)mkv_get_wave_type(
                    &stream->waveinfo, (unsigned char*)track->codec_priv.binary_ptr,
                    track->codec_priv.binary_size, &extradata_offset);
        } else if (!strcmp(track->track_codecid.str, "V_QUICKTIME") &&
                   (track->codec_priv.binary_size >= 86) &&
                   (track->codec_priv.binary_offset != 0)) {
            /*
            track->video.fourcc = AV_RL32(track->codec_priv.data);
            codec_id=ff_codec_get_id(codec_movvideo_tags, track->video.fourcc);
            */
        } else if (stream->codec_type == MKV_AUDIO_PCM_S16LE) {
            switch (track->ainfo.bitdepth) {
                case 8:
                    stream->codec_type = MKV_AUDIO_PCM_U8;
                    break;
                case 24:
                    stream->codec_type = MKV_AUDIO_PCM_S24LE;
                    break;
                case 32:
                    stream->codec_type = MKV_AUDIO_PCM_S32LE;
                    break;
            }
        } else if (stream->codec_type == MKV_AUDIO_PCM_S16BE) {
            switch (track->ainfo.bitdepth) {
                case 8:
                    stream->codec_type = MKV_AUDIO_PCM_U8;
                    break;
                case 24:
                    stream->codec_type = MKV_AUDIO_PCM_S24BE;
                    break;
                case 32:
                    stream->codec_type = MKV_AUDIO_PCM_S32BE;
                    break;
            }
        }
        /*
        else if (codec_id==CODEC_ID_PCM_F32LE && track->audio.bitdepth==64) {
            codec_id = CODEC_ID_PCM_F64LE;
        }*/
        else if ((stream->codec_type == MKV_AUDIO_AAC || stream->codec_type == MKV_AUDIO_AC3) &&
                 !track->codec_priv.binary_size) {
            int profile = matroska_aac_profile(track->track_codecid.str);
            int sri = matroska_aac_sri(track->ainfo.sampling_frequency);
            extradata = (uint8*)alloc_stream_buffer(pbs, 5, NON_STREAM);
            if (extradata == NULL)
                return -1;
            extradata[0] = ((uint8)profile << 3) | (((uint8)sri & 0x0E) >> 1);
            extradata[1] = (((uint8)sri & 0x01) << 7) | ((uint8)track->ainfo.channels << 3);
            if (strstr(track->track_codecid.str, "SBR")) {
                sri = matroska_aac_sri(track->ainfo.output_sampling_freq);
                extradata[2] = 0x56;
                extradata[3] = 0xE5;
                extradata[4] = 0x80 | ((uint8)sri << 3);
                extradata_size = 5;
            } else
                extradata_size = 2;
        } else if (stream->codec_type == MKV_AUDIO_AAC && track->codec_priv.binary_size) {
            BitsCtx bcx;
            uint32 nAudioObjType, nSampleRateIndex, nExtensionSampleRateIndex;

            track->codec_priv.binary_ptr =
                    alloc_stream_buffer(pbs, track->codec_priv.binary_size, NON_STREAM);
            seek_stream_buffer(pbs, track->codec_priv.binary_offset, 1);
            read_stream_buffer(pbs, &track->codec_priv.binary_ptr, track->codec_priv.binary_size);
            InitGetBits(&bcx, (uint8*)track->codec_priv.binary_ptr, track->codec_priv.binary_size);

            nAudioObjType = GetBits(&bcx, 5);
            nSampleRateIndex = GetBits(&bcx, 4);
            if (nSampleRateIndex == 0xf)
                nSampleRateIndex = GetBits(&bcx, 24);
            (void)GetBits(&bcx, 4);//nChannelConfig

            if (track->ainfo.sampling_frequency == DEFAULT_AUDIO_SAMPLERATE)
                track->ainfo.sampling_frequency = ADTSSampleFreqTable[nSampleRateIndex];

            // MP4AUDIO_SBR or MP4AUDIO_PS
            if (nAudioObjType == 5 || nAudioObjType == 29) {
                nExtensionSampleRateIndex = GetBits(&bcx, 4);
                if (nExtensionSampleRateIndex == 0xf)
                    nExtensionSampleRateIndex = GetBits(&bcx, 24);

                nAudioObjType = GetBits(&bcx, 5);
            }
            if (nAudioObjType == 2)  // MP4AUDIO_AAC_LC
            {
                // Get GA specific
                (void)GetBits(&bcx, 1);//frame_len
                (void)GetBits(&bcx, 1); //dependsOnCoreCoder
            }
        } else if (stream->codec_type == MKV_REAL_VIDEO_RV10 ||
                   stream->codec_type == MKV_REAL_VIDEO_RV20 ||
                   stream->codec_type == MKV_REAL_VIDEO_RV30 ||
                   stream->codec_type == MKV_REAL_VIDEO_RV40) {
            extradata_offset = 0;
            stream->check_pts_pos = 1;
            stream->rvinfo.width = track->vinfo.pixel_width;
            stream->rvinfo.height = track->vinfo.pixel_height;
        } else if (stream->codec_type == MKV_REAL_AUDIO_144) {
            track->ainfo.channels = 1;
            track->ainfo.output_sampling_freq = DEFAULT_AUDIO_SAMPLERATE;
        } else if ((stream->codec_type == MKV_REAL_AUDIO_288 ||
                    stream->codec_type == MKV_REAL_AUDIO_COOK ||
                    stream->codec_type == MKV_REAL_AUDIO_ATRC) &&
                   track->codec_priv.binary_size >= 46) {
            char* data = NULL;

            if (!track->codec_priv.binary_ptr) {
                track->codec_priv.binary_ptr =
                        alloc_stream_buffer(pbs, track->codec_priv.binary_size, NON_STREAM);
                seek_stream_buffer(pbs, track->codec_priv.binary_offset, 1);
                read_stream_buffer(pbs, &track->codec_priv.binary_ptr,
                                   track->codec_priv.binary_size);
            }

            stream->has_ra_info = 1;
            stream->rainfo.sub_packet_cnt = 0;

            data = track->codec_priv.binary_ptr + 24;

            stream->rainfo.coded_framesize = *(uint32*)data;
            data += 16;
            SWAP_32(stream->rainfo.coded_framesize);

            stream->rainfo.sub_packet_h = *(uint16*)data;
            data += 2;
            SWAP_16(stream->rainfo.sub_packet_h);

            stream->rainfo.frame_size = *(uint16*)data;
            data += 2;
            SWAP_16(stream->rainfo.frame_size);

            stream->rainfo.sub_packet_size = *(uint16*)data;
            SWAP_16(stream->rainfo.sub_packet_size);

            stream->rainfo.audio_buf = (uint8*)alloc_stream_buffer(
                    pbs, (int)(stream->rainfo.frame_size * stream->rainfo.sub_packet_h),
                    NON_STREAM);

            if (stream->codec_type == MKV_REAL_AUDIO_288) {
                stream->rainfo.block_align = (uint16)stream->rainfo.coded_framesize;
                track->codec_priv.binary_size = 0;
            } else {
                stream->rainfo.block_align = stream->rainfo.sub_packet_size;
                extradata_offset = 78;
            }
        }

        track->codec_priv.binary_size -= extradata_offset;

        // check validity of this stream
        if (stream->track_type == MATROSKA_TRACK_TYPE_NONE) {
            MKV_ERROR_LOG("read_matroska_file_header : Unknown or unsupported stream type %d \n",
                          stream->track_type);
            continue;
        }

        pctx->stream_count++;
        // track->stream = stream; should not set point here; Fix issue [ENGR00253655]
        // if stream count is larger than STREAM_LIST_INC_NUM, stream_list prt may change.
        // so can't set stream pointer to track here, set the pointer when all stream parsed

        stream->track_index = (uint8)i;
        stream->stream_index = (uint8)track->track_num;

        if (track->content_encoding_count > 1) {
            MKV_ERROR_LOG(
                    "read_matroska_file_header : Multiple combined encodings not supported\n");
        } else if (track->content_encoding_count == 1) {
            if (track->content_encoding_list[0].content_encoding_type ||
                (track->content_encoding_list[0].compression.content_compression_algo !=
                 MATROSKA_TRACK_ENCODING_COMP_ALGO_HEADERSTRIP)) {
                track->content_encoding_list[0].content_encoding_scope = 0;
                MKV_ERROR_LOG("read_matroska_file_header : Unsupported encoding type");
            } else if (track->codec_priv.binary_size &&
                       track->content_encoding_list[0].content_encoding_scope & 0x2) {
                char* codec_priv = track->codec_priv.binary_ptr;
                int offset = matroska_decode_buffer(pctx, (uint8**)(&track->codec_priv.binary_ptr),
                                                    &track->codec_priv.binary_size, track);
                if (offset < 0) {
                    track->codec_priv.binary_ptr = NULL;
                    track->codec_priv.binary_size = 0;
                    MKV_ERROR_LOG(
                            "read_matroska_file_header : Failed to decode codec private data\n");
                } else if (offset > 0) {
                    track->codec_priv.binary_ptr = alloc_stream_buffer(
                            pbs, track->codec_priv.binary_size + offset, NON_STREAM);
                    memcpy(track->codec_priv.binary_ptr,
                           track->content_encoding_list[0]
                                   .compression.content_compression_settings.binary_ptr,
                           offset);
                    memcpy(track->codec_priv.binary_ptr + offset, codec_priv,
                           track->codec_priv.binary_size);
                    track->codec_priv.binary_size += offset;
                }
                if (codec_priv != track->codec_priv.binary_ptr)
                    free_stream_buffer(pbs, codec_priv, NON_STREAM);
            }
        }

        if (track->track_time_code_scale < 0.01)
            track->track_time_code_scale = 1.0;

        if (track->default_duration) {
            stream->duration = (uint64)(track->default_duration * track->track_time_code_scale);
        }

        if (extradata) {
            stream->codec_extra_data = extradata;
            stream->codec_extra_size = extradata_size;
        } else if (track->codec_priv.binary_offset && track->codec_priv.binary_size > 0
                   /*&& (stream->codec_type != MKV_AUDIO_AAC)*/) {
            stream->codec_extra_data = (uint8*)alloc_stream_buffer(
                    pbs, track->codec_priv.binary_size + MKV_INPUT_BUFFER_PADDING_SIZE, NON_STREAM);
            if (stream->codec_extra_data == NULL)
                return -1;
            stream->codec_extra_size = track->codec_priv.binary_size;

            if (!track->codec_priv.binary_ptr) {
                uint8* ptr = stream->codec_extra_data;
                seek_stream_buffer(pbs, track->codec_priv.binary_offset, 1);
                read_stream_buffer(pbs, (char**)(&ptr), track->codec_priv.binary_size);
                if (ptr != stream->codec_extra_data)
                    return -1;
            } else {
                memcpy(stream->codec_extra_data, track->codec_priv.binary_ptr + extradata_offset,
                       track->codec_priv.binary_size);
            }
        }

        matroska_add_codec_priv(pctx, stream);

        matroska_check_codec_data(pctx, stream);

        // read and setup tag list for crypto key
        if (track->track_type == MATROSKA_TRACK_TYPE_VIDEO) {
            uint8* data;
            uint32 dataSize;
            if (0 == matroska_get_track_crypto_key(pctx, track, &dataSize, &data)) {
                matroska_add_track_ext_tag(pbs, stream->ext_tag_list,
                                           FSL_PARSER_TRACKEXTTAG_CRPYTOKEY, 0, dataSize, data);
            }
        }
    }

    // set the stream pointer to track structure here
    //  Fix issue [ENGR00253655]
    // not all tracks can be recognized, track count may be larger than stream count,
    // find track index from each stream
    for (i = 0; i < pctx->stream_count; i++) {
        mkvStream* stream = &(pctx->stream_list[i]);
        int track_index = stream->track_index;
        if (track_index < track_count)
            track_list[track_index].stream = stream;
    }

    return 0;
}

int matroska_initialize_index(MKVReaderContext* pctx) {
    int i, j;
    int cue_count = 0;
    int cue_scale = 1;
    CuePointEntry* cue_list = NULL;
    int64 bytes = 0;
    ebml_info einfo;
    ByteStream* pbs = NULL;

    if (!pctx)
        return -1;

    if (pctx->isLive)
        return -1;

    if (0 == pctx->sctx.seek_count)
        return -1;
    for (i = 0; i < pctx->sctx.seek_count; i++) {
        if (pctx->sctx.seek_list[i].seekid == MATROSKA_ID_CUES)
            break;
    }
    if (i == pctx->sctx.seek_count)
        return -1;

    pbs = &pctx->bs;
    bytes = read_stream_ebml_info(pbs, pctx->segment_start + pctx->sctx.seek_list[i].seekpos,
                                  &einfo);
    if (!bytes) {
        if (eof_stream_buffer(pbs)) {
            seek_stream_buffer(pbs, pctx->sctx.first_cluster_pos, 1);
            pctx->sctx.cue_parsed = TRUE;
            return 0;
        }

        MKV_ERROR_LOG("%s : read_stream_ebml_info failed .\n", __FUNCTION__);
        return -1;
    }
    if (einfo.s_id != MATROSKA_ID_CUES) {
        MKV_ERROR_LOG("error cue location: %lld \r\n",
                      pctx->segment_start + pctx->sctx.seek_list[i].seekpos);
        MKV_LOG("to play normally, reseek to first cluster pos: %lld \r\n",
                pctx->sctx.first_cluster_pos);
        seek_stream_buffer(pbs, pctx->sctx.first_cluster_pos, 1);
        return -1;
    }

    clear_matroska_cue_list(pctx);

    MKV_TIMER_START();
    read_segment_cuepoint_entry(
            pctx, pctx->segment_start + pctx->sctx.seek_list[i].seekpos + einfo.s_offset,
            (int)einfo.s_size);
    MKV_TIMER_STOP("read cuepoint");
    MKV_LOG("cue points: %d \r\n", pctx->sctx.cue_count);

    seek_stream_buffer(pbs, pctx->sctx.first_cluster_pos, 1);
    cue_list = pctx->sctx.cue_list;
    cue_count = pctx->sctx.cue_count;

    for (i = 0; i < pctx->stream_count; i++) {
        if (pctx->stream_list[i].index_list)
            free_stream_buffer(&pctx->bs, (char*)pctx->stream_list[i].index_list, NON_STREAM);
        pctx->stream_list[i].index_list = NULL;
        pctx->stream_list[i].index_count = 0;
    }

    if (cue_list && pctx->sctx.info.time_code_scale &&
        (uint64)cue_list[0].cue_time > 100000000000000ULL / pctx->sctx.info.time_code_scale) {
        MKV_ERROR_LOG("read_matroska_file_header : Working around broken index.\n");
        cue_scale = pctx->sctx.info.time_code_scale;
    }

    if (cue_list == NULL) {
        pctx->sctx.cue_parsed = TRUE;
        return 0;
    }

    MKV_LOG("FOR BEGIN \r\n");
    MKV_TIMER_START();
    for (i = 0; i < cue_count; i++) {
        int cue_pos_count = cue_list[i].cue_track_count;
        CueTrackPosition* cue_pos_list = cue_list[i].cue_track_list;

        for (j = 0; j < cue_pos_count; j++) {
            TrackEntry* track = matroska_find_track_by_num(pctx, cue_pos_list[j].cue_track);
            if (track && track->stream) {
                uint64 pos = 0;
                uint64 pts = 0;
                int block_num = 1;

                pts = cue_list[i].cue_time / cue_scale;
                pos = cue_pos_list[j].cue_cluster_position + pctx->segment_start;
                block_num = cue_pos_list[j].cue_block_number;

                mkv_add_index_entry(pctx, (mkvStream*)track->stream, pos, pts, block_num,
                                    MKV_CUE_LIST_FLAG, FALSE);
            }
        }
    }
    MKV_TIMER_STOP("FOR END");

    MKV_TIMER_START();
    // sort index list for all tracks
    for (i = 0; i < pctx->sctx.track_count; i++) {
        TrackEntry* track = &pctx->sctx.track_list[i];
        if (track && track->stream)
            mkv_index_quick_sort((mkvStream*)track->stream);
    }
    MKV_TIMER_STOP("SORT END");
    pctx->sctx.cue_parsed = TRUE;

    return 0;
}

int matroska_import_index(MKVReaderContext* pctx, uint32 tracknum, uint8* buffer, uint32 size) {
    int allocsize = 0;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;
    ByteStream* pbs = &pctx->bs;

    if (!pctx)
        return -1;

    if (size % sizeof(CueIndex))
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    if (stream->index_list)
        free_stream_buffer(&pctx->bs, (char*)stream->index_list, NON_STREAM);

    stream->index_count = size / sizeof(CueIndex);

    allocsize = ((stream->index_count / STREAM_LIST_INC_NUM) + 1) * STREAM_LIST_INC_NUM *
                sizeof(CueIndex);

    stream->index_list = (CueIndex*)alloc_stream_buffer(pbs, allocsize, NON_STREAM);
    if (!stream->index_list)
        return -1;

    memcpy(stream->index_list, buffer, size);

    return 0;
}

int matroska_export_index(MKVReaderContext* pctx, uint32 tracknum, uint8* buffer, uint32* size) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *size = stream->index_count * sizeof(CueIndex);

    if (!buffer)
        return 0;

    if (!stream->index_list)
        return 0;

    memcpy(buffer, stream->index_list, *size);

    return 0;
}

int matroska_is_seekable(MKVReaderContext* pctx) {
    int i;

    if (!pctx)
        return -1;

    if (pctx->isLive) {
        MKV_ERROR_LOG("matroska_is_seekable false isLive \n");
        return 0;
    }

    if (!pctx->sctx.info.duration) {
        MKV_ERROR_LOG("matroska_is_seekable false no duration \n");
        return 0;
    }

    if (pctx->sctx.has_cue) {
        MKV_ERROR_LOG("matroska_is_seekable true has_cue \n");
        return 1;
    }

    // check cue points
    if (pctx->sctx.cue_count > 1 && pctx->sctx.cue_list) {
        MKV_ERROR_LOG("matroska_is_seekable true cue_count > 1 \n");
        return 1;
    }

    if (!pctx->stream_count || !pctx->stream_list) {
        MKV_ERROR_LOG("matroska_is_seekable false no stream_count \n");
        return 0;
    }

    for (i = 0; i < pctx->stream_count; i++) {
        if (pctx->stream_list[i].index_count > 1 && pctx->stream_list[i].index_list)
            return 1;
    }

    if (!pctx->sctx.has_video_track)
        return 1;

    // from 1 seconds to 10 minutes
    if (pctx->duration_us > 1000000 && pctx->duration_us < (60 * 10) * 1000000)
        return 1;

    MKV_ERROR_LOG("matroska_is_seekable false duration_us > 10 minutes \n");
    return 0;
}

int matroska_get_trackcount(MKVReaderContext* pctx) {
    if (!pctx)
        return -1;

    return pctx->stream_count;
}

static int matroska_find_tag_from_taglist(MKVReaderContext* pctx, uint32 tracknum, uint32 level,
                                          const char* name, uint32* len, uint8** value) {
    TagEntry* tag_list = NULL;
    int i = 0;
    int j = 0;
    unsigned int target_track_uid = 0;
    unsigned int track_uid = 0;
    TrackEntry* track = NULL;
    SimpleTag* tag = NULL;
    bool find = FALSE;

    if (!pctx || !name || !value || !len)
        return -1;

    if (!pctx->sctx.is_tags_read || pctx->sctx.tag_count <= 0) {
        return -1;
    }

    if (tracknum > 0) {
        track = matroska_find_track_by_num(pctx, tracknum);
        if (!track)
            return -1;
        target_track_uid = track->track_uid;
    }

    for (i = 0; i < pctx->sctx.tag_count; i++) {
        tag_list = &(pctx->sctx.tag_list[i]);

        if (tag_list->simple_tag_count == 0)
            continue;

        if (tag_list->tag_target.track_uid)
            track_uid = *tag_list->tag_target.track_uid;

        if (track_uid != 0 && tracknum > 0 && track_uid != target_track_uid)
            continue;

        if (tag_list->tag_target.target_type_value < level)
            continue;

        if (!strncmp(MATROSKA_TAG_ID_TITLE, name, strlen(name)) && 0 == level &&
            tag_list->tag_target.target_type_value >= 70)
            continue;

        if (!strncmp(MATROSKA_TAG_ID_ARTIST, name, strlen(name)) && 0 == level &&
            tag_list->tag_target.target_type_value >= 50)
            continue;

        if (!strncmp(MATROSKA_TAG_ID_TOTAL_PARTS, name, strlen(name)) && 0 == level &&
            tag_list->tag_target.target_type_value >= 60)
            continue;

        if (!strncmp(MATROSKA_TAG_ID_PART_NUMBER, name, strlen(name)) && 0 == level &&
            tag_list->tag_target.target_type_value >= 50)
            continue;

        for (j = 0; j < tag_list->simple_tag_count; j++) {
            tag = &(tag_list->simple_tag_list[j]);
            if (tag->tag_name_count > 0 && tag->tag_name->strlen > 0 &&
                !strncmp(tag->tag_name->str, name, strlen(name))) {
                find = TRUE;
                *value = (uint8*)tag->tag_string.str;
                *len = tag->tag_string.strlen;
                break;
            }
        }
        if (find)
            break;
    }

    if (find)
        return 0;
    else
        return -1;
}

int matroska_get_userdata(MKVReaderContext* pctx, uint32 id, uint8** buffer, uint32* size) {
    if (!pctx || !buffer || !size)
        return -1;

    switch (id) {
        case USER_DATA_TITLE:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_TITLE, size, buffer);

            if (pctx->sctx.info.title.strlen > 0) {
                *buffer = (uint8*)pctx->sctx.info.title.str;
                *size = pctx->sctx.info.title.strlen;
            }
            break;

        case USER_DATA_TOOL:
            *buffer = (uint8*)pctx->sctx.info.writing_app.str;
            *size = pctx->sctx.info.writing_app.strlen;
            break;

        case USER_DATA_LANGUAGE:
            *buffer = NULL;
            *size = 0;
            break;

        case USER_DATA_GENRE:
            if (0 !=
                matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_GENRE, size, buffer)) {
                *buffer = NULL;
                *size = 0;
            }
            break;

        case USER_DATA_ARTIST:
            if (0 !=
                matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_ARTIST, size, buffer)) {
                *buffer = NULL;
                *size = 0;
            }
            break;

        case USER_DATA_COPYRIGHT:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_PRODUCTION_COPYRIGHT, size,
                                           buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_COPYRIGHT, size, buffer);
            break;

        case USER_DATA_COMMENTS:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_COMMENTS, size, buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_COMMENT, size, buffer);
            break;

        case USER_DATA_CREATION_DATE:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DATE, size, buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DATE_PURCHASED, size,
                                           buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DATE_WRITTEN, size, buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DATE_DIGITIZED, size,
                                           buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DATE_TAGGED, size, buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DATE_ENCODED, size, buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DATE_RECORDED, size, buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DATE_RELEASED, size, buffer);
            break;

        case USER_DATA_RATING:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_RATING, size, buffer);
            break;

        case USER_DATA_ALBUM:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 50, MATROSKA_TAG_ID_TITLE, size, buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_ALBUM, size, buffer);
            break;

        case USER_DATA_COMPOSER:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_COMPOSER, size, buffer);
            break;

        case USER_DATA_DIRECTOR:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DIRECTOR, size, buffer);
            break;

        case USER_DATA_CREATOR:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_ENCODED_BY, size, buffer);
            break;

        case USER_DATA_PRODUCER:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_PRODUCER, size, buffer);
            break;

        case USER_DATA_PERFORMER:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_ACCOMPANIMENT, size, buffer);
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_LEAD_PERFORMER, size,
                                           buffer);
            break;

        case USER_DATA_MOVIEWRITER:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_WRITTEN_BY, size, buffer);
            break;

        case USER_DATA_DESCRIPTION:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_DESCRIPTION, size, buffer);
            break;

        case USER_DATA_TRACKNUMBER:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_PART_NUMBER, size, buffer);
            break;

        case USER_DATA_TOTALTRACKNUMBER:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_TOTAL_PARTS, size, buffer);
            break;

        case USER_DATA_DISCNUMBER:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 50, MATROSKA_TAG_ID_PART_NUMBER, size, buffer);
            break;

        case USER_DATA_AUTHOR:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_AUTHOR, size, buffer);
            break;

        case USER_DATA_PUBLISHER:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_PUBLISHER, size, buffer);
            break;

        case USER_DATA_KEYWORDS:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 0, MATROSKA_TAG_ID_KEYWORDS, size, buffer);
            break;

        case USER_DATA_ALBUMARTIST:
            *buffer = NULL;
            *size = 0;
            matroska_find_tag_from_taglist(pctx, 0, 50, MATROSKA_TAG_ID_ARTIST, size, buffer);
            break;

        default:

            *buffer = NULL;
            *size = 0;
            break;
    }

    return 0;
}

int matroska_get_artwork(MKVReaderContext* pctx, UserDataFormat* format, uint8** buffer,
                         uint32* size) {
    if (!pctx || !format || !buffer || !size)
        return -1;

    *format = USER_DATA_FORMAT_JPEG;
    *buffer = NULL;
    *size = 0;

    if (pctx->sctx.attach_count > 0) {
        Attachments* artwork = pctx->sctx.attach_list;
        if (artwork->file_minetype.strlen > 0) {
            if (!strncmp(artwork->file_minetype.str, "image/jpeg", artwork->file_minetype.strlen)) {
                *format = USER_DATA_FORMAT_JPEG;
            } else if (!strncmp(artwork->file_minetype.str, "image/png",
                                artwork->file_minetype.strlen)) {
                *format = USER_DATA_FORMAT_PNG;
            } else if (!strncmp(artwork->file_minetype.str, "image/bmp",
                                artwork->file_minetype.strlen)) {
                *format = USER_DATA_FORMAT_BMP;
            } else if (!strncmp(artwork->file_minetype.str, "image/gif",
                                artwork->file_minetype.strlen))
                *format = USER_DATA_FORMAT_GIF;
        }
        if (artwork->file_data.binary_size > 0) {
            *size = artwork->file_data.binary_size;
            *buffer = (uint8*)artwork->file_data.binary_ptr;
        }
    }

    return 0;
}

int matroska_get_chapter_menu(MKVReaderContext* pctx, uint8** pBuffer, uint32* dwLen) {
    ChapterMenu* pMenu;
    ChapterEdition* pList;
    uint32 i;
    ChapterInfo* pChapterList;
    ByteStream* pbs = &pctx->bs;

    if (FALSE == pctx->sctx.is_chapters_read) {
        *pBuffer = NULL;
        *dwLen = 0;
        return 0;
    } else {
        pMenu = &pctx->sctx.mkv_chapterMenu;
        pList = pctx->sctx.chapter_list;

        pMenu->EditionUID = pList->edition_uid;
        pMenu->EdtionFlags = pList->edition_flag_default;
        pMenu->dwChapterNum = pList->chapter_atom_count;

        if (NULL != pMenu->pChapterList) {
            free_stream_buffer(pbs, (char*)pMenu->pChapterList, NON_STREAM);
            pMenu->pChapterList = NULL;
        }
        pChapterList = (ChapterInfo*)alloc_stream_buffer(
                pbs, sizeof(ChapterInfo) * pMenu->dwChapterNum, NON_STREAM);
        memset(pChapterList, 0, sizeof(ChapterInfo) * pMenu->dwChapterNum);

        for (i = 0; i < pMenu->dwChapterNum; i++) {
            pChapterList[i].ChapterUID = pList->chapter_atom_list[i].chapter_uid;
            pChapterList[i].dwStartTime = pList->chapter_atom_list[i].chapter_time_start;
            pChapterList[i].dwStopTime = pList->chapter_atom_list[i].chapter_time_end;
            if (NULL != pList->chapter_atom_list[i].chapter_display_list) {
                pChapterList[i].dwTitleSize =
                        pList->chapter_atom_list[i].chapter_display_list[0].chapter_string.strlen;
                pChapterList[i].Title =
                        pList->chapter_atom_list[i].chapter_display_list[0].chapter_string.str;
            }
        }
        pMenu->pChapterList = pChapterList;
        *pBuffer = (uint8*)pMenu;
        *dwLen = sizeof(ChapterMenu);
    }

    return 0;
}

#ifdef REVISE_MOVIE_DURARION
__attribute__((unused))
static int read_cluster_entry_timescode(MKVReaderContext* pctx, int64 base, int64 off, int64 length,
                                        int64* cluster_timecode) {
    int64 bytes = 0;
    ebml_info einfo;
    int max_cnt = 20;  // reduce parser time for current entry, to avoid case: timecode is located
                       // after blockgroup/simpleblock
    int cur_cnt = 0;
    int valid_cnt = 0;
    int64 offset = 0;

    ByteStream* pbs = NULL;
    uint64 before_pos;

    if (!pctx)
        return -1;

    before_pos = get_stream_position(&pctx->bs);

    pbs = &pctx->bs;  // use global ByteStream instance directly

    /*
        cache: max(id+offset+uint)=24 bytes
        max left: max(id+offset+uint)=24
    */

    offset = base + off;
    MKV_LOG("%s: base: %lld, off: %lld, size: %lld \r\n", __FUNCTION__, base, off, length);
    while ((length > 0) && (valid_cnt < 1) && (cur_cnt < max_cnt)) {
        bytes = read_stream_ebml_info(pbs, offset, &einfo);

        if (!bytes || length < bytes) {
            MKV_ERROR_LOG("%s : read_stream_ebml_info failed: bytes: %lld, length: %lld \r\n",
                          __FUNCTION__, bytes, length);
            seek_stream_buffer(
                    &pctx->bs, before_pos,
                    1);  // since context may not be protected, we need to set force =1 !!!!
            return -1;
        }
        cur_cnt++;
        switch (einfo.s_id) {
            case MATROSKA_ID_CLUSTERTIMECODE:
                *cluster_timecode = (int64)read_uint_value(einfo.s_data, (int)einfo.s_size);
                MKV_LOG("timecode: %lld \r\n", *cluster_timecode);
                valid_cnt++;
                break;
            case MATROSKA_ID_CLUSTERPOSITION:
            case MATROSKA_ID_CLUSTERPREVSIZE:
            case MATROSKA_ID_BLOCKGROUP:
            case MATROSKA_ID_SIMPLEBLOCK:
            case FSL_EBML_ID_VOID:
            case FSL_EBML_ID_CRC32:
                break;
            default:
                MKV_ERROR_LOG("read_segment_cluster_index_entry : unhandled ID 0x%llX \n",
                              einfo.s_id);
                break;
        }

        length -= bytes;
        offset += bytes;
    }

    if (valid_cnt < 1) {
        // discard this entry
        *cluster_timecode = -1;
    }
    seek_stream_buffer(&pctx->bs, before_pos, 0);

    return 0;
}

int verify_movie_duration(MKVReaderContext* pctx, double ori_duration, double* revised_duration) {
    // set default
    *revised_duration = ori_duration;

    if (!pctx)
        return -1;

    if (ori_duration * pctx->sctx.info.time_code_scale / 1000000000 >
        (MAX_DURARION_THRESHOLD_MINUTE * 60)) {
        MKV_LOG("the movie is too large, discard duration revision \r\n");
        return 0;
    }

    if (!pctx->sctx.prescan_cluster_done) {
        MKV_TIMER_START();
        read_segment_prescan_cluster_index_list(pctx);
        MKV_TIMER_STOP("read cluster");
    }

    if (pctx->sctx.prescan_cluster_index_count <= 1) {
        MKV_LOG("no enough info to revise duration \r\n");
        return 0;
    }

    if (pctx->sctx.prescan_cluster_totalcnt >= pctx->sctx.prescan_cluster_maxcnt) {
        MKV_LOG("the clip is too large, no integrated cluster info to revise duration \r\n");
        return 0;
    }

    // now, let's try to revise the duration
    {
        int index_cluster_cnt = pctx->sctx.prescan_cluster_index_count;
        int64 last_cluster_time = 0;
        int64 last2_cluster_time = 0;
        int64 estimate_interval = 0;
        double estimate_duration;

        last_cluster_time = pctx->sctx.prescan_cluster_index_list[index_cluster_cnt - 1].timecode;
        last2_cluster_time = pctx->sctx.prescan_cluster_index_list[index_cluster_cnt - 2].timecode;

        estimate_interval = last_cluster_time - last2_cluster_time;
        estimate_duration = last_cluster_time + estimate_interval;
        MKV_LOG("estimate_duration=%lf,duration=%lf", estimate_duration, pctx->sctx.info.duration);
        if ((estimate_duration - (double)pctx->sctx.info.duration) *
                    pctx->sctx.info.time_code_scale / 1000000000 >
            ALLOWED_DURARION_ERROR_SECOND) {
            MKV_ERROR_LOG("verify duration: %lf \r\n", estimate_duration);
            *revised_duration = estimate_duration;
        } else {
            MKV_LOG("duration is correct \r\n");
        }
    }

    return 0;
}
#endif

int matroska_get_movie_duration(MKVReaderContext* pctx, uint64* duration) {
    double dur;
    if (!pctx || !duration)
        return -1;

#ifdef REVISE_MOVIE_DURARION
    verify_movie_duration(pctx, (double)pctx->sctx.info.duration, &dur);
#else
    dur = pctx->sctx.info.duration;
#endif
    if (pctx->sctx.info.time_code_scale)
        *duration = (uint64)(dur * (double)pctx->sctx.info.time_code_scale + 500) / 1000;
    else
        *duration = (uint64)dur;
    return 0;
}

// always return movie duration for each track
int matroska_get_track_duration(MKVReaderContext* pctx, uint32 tracknum, uint64* duration) {
    if (!pctx || !duration)
        return -1;
    (void)tracknum;
    *duration = (uint64)pctx->sctx.info.duration;
    if (pctx->sctx.info.time_code_scale)
        *duration = (uint64)((double)pctx->sctx.info.duration *
                                     (double)pctx->sctx.info.time_code_scale +
                             500) /
                    1000;

    return 0;
}

int matroska_get_track_position(MKVReaderContext* pctx, uint32 tracknum, uint64* timestamp) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !timestamp)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *timestamp = (uint64)stream->position;

    if (pctx->sctx.info.time_code_scale)
        *timestamp = (uint64)*timestamp * pctx->sctx.info.time_code_scale / 1000;

    return 0;
}

typedef struct {
    uint32 inner_type;
    uint32 decoder_type;
    uint32 decoder_subtype;
} TrackTypeMap;

static TrackTypeMap mkv_audio_track_map_tab[] = {
        {MKV_AUDIO_TYPE_UNKNOWN, AUDIO_TYPE_UNKNOWN, AUDIO_TYPE_UNKNOWN},
        {MKV_AUDIO_PCM_U8, AUDIO_PCM, AUDIO_PCM_U8},
        {MKV_AUDIO_PCM_S16LE, AUDIO_PCM, AUDIO_PCM_S16LE},
        {MKV_AUDIO_PCM_S24LE, AUDIO_PCM, AUDIO_PCM_S24LE},
        {MKV_AUDIO_PCM_S32LE, AUDIO_PCM, AUDIO_PCM_S32LE},
        {MKV_AUDIO_PCM_S16BE, AUDIO_PCM, AUDIO_PCM_S16BE},
        {MKV_AUDIO_PCM_S24BE, AUDIO_PCM, AUDIO_PCM_S24BE},
        {MKV_AUDIO_PCM_S32BE, AUDIO_PCM, AUDIO_PCM_S32BE},
        {MKV_AUDIO_PCM_ALAW, AUDIO_PCM_ALAW, AUDIO_PCM_ALAW},
        {MKV_AUDIO_PCM_MULAW, AUDIO_PCM_MULAW, AUDIO_PCM_MULAW},
        {MKV_AUDIO_ADPCM, AUDIO_ADPCM, AUDIO_ADPCM_MS},
        {MKV_AUDIO_MP3, AUDIO_MP3, AUDIO_MP3},
        {MKV_AUDIO_AAC, AUDIO_AAC, AUDIO_AAC_RAW},
        {MKV_AUDIO_MPEG2_AAC, AUDIO_MPEG2_AAC, AUDIO_MPEG2_AAC},
        {MKV_AUDIO_AC3, AUDIO_AC3, AUDIO_AC3},
        {MKV_AUDIO_WMA1, AUDIO_WMA, AUDIO_WMA1},
        {MKV_AUDIO_WMA2, AUDIO_WMA, AUDIO_WMA2},
        {MKV_AUDIO_WMA3, AUDIO_WMA, AUDIO_WMA3},
        {MKV_AUDIO_AMR_NB, AUDIO_AMR, AUDIO_AMR_NB},
        {MKV_AUDIO_AMR_WB, AUDIO_AMR, AUDIO_AMR_WB},
        {MKV_AUDIO_AMR_WB_PLUS, AUDIO_AMR, AUDIO_AMR_WB_PLUS},
        {MKV_AUDIO_DTS, AUDIO_DTS, AUDIO_DTS},
        {MKV_AUDIO_VORBIS, AUDIO_VORBIS, AUDIO_VORBIS},
        {MKV_AUDIO_FLAC, AUDIO_FLAC, AUDIO_FLAC},
        {MKV_AUDIO_NELLYMOSER, AUDIO_NELLYMOSER, AUDIO_NELLYMOSER},
        {MKV_AUIDO_SPEEX, AUIDO_SPEEX, AUIDO_SPEEX},
        {MKV_REAL_AUDIO_144, AUDIO_REAL, REAL_AUDIO_COOK},
        {MKV_REAL_AUDIO_288, AUDIO_REAL, REAL_AUDIO_COOK},
        {MKV_REAL_AUDIO_COOK, AUDIO_REAL, REAL_AUDIO_COOK},
        {MKV_REAL_AUDIO_ATRC, AUDIO_REAL, REAL_AUDIO_ATRC},
        {MKV_REAL_AUDIO_ATRC, AUDIO_REAL, REAL_AUDIO_ATRC},
        {MKV_REAL_AUDIO_ATRC, AUDIO_REAL, REAL_AUDIO_ATRC},
        {MKV_REAL_AUDIO_ATRC, AUDIO_REAL, REAL_AUDIO_ATRC},
        {MKV_REAL_AUDIO_ATRC, AUDIO_REAL, REAL_AUDIO_ATRC},
        {MKV_AUDIO_EC3, AUDIO_EC3, AUDIO_EC3},
        {MKV_AUDIO_OPUS, AUDIO_OPUS, AUDIO_OPUS},
        {MKV_AUDIO_ADPCM_IMA, AUDIO_ADPCM, AUDIO_IMA_ADPCM},
};

static TrackTypeMap mkv_video_track_map_tab[] = {
        {MKV_VIDEO_TYPE_UNKNOWN, VIDEO_TYPE_UNKNOWN, VIDEO_TYPE_UNKNOWN},
        {MKV_VIDEO_UNCOMPRESSED, VIDEO_UNCOMPRESSED, VIDEO_UNCOMPRESSED},
        {MKV_VIDEO_MPEG2, VIDEO_MPEG2, VIDEO_MPEG2},
        {MKV_VIDEO_MPEG4, VIDEO_MPEG4, VIDEO_MPEG4},
        {MKV_VIDEO_MS_MPEG4_V2, VIDEO_MS_MPEG4, VIDEO_MS_MPEG4_V2},
        {MKV_VIDEO_MS_MPEG4_V3, VIDEO_MS_MPEG4, VIDEO_MS_MPEG4_V3},
        {MKV_VIDEO_H263, VIDEO_H263, VIDEO_H263},
        {MKV_VIDEO_H264, VIDEO_H264, VIDEO_H264},
        {MKV_VIDEO_MJPG, VIDEO_MJPG, VIDEO_MJPG},
        {MKV_VIDEO_DIVX3, VIDEO_DIVX, VIDEO_DIVX3},
        {MKV_VIDEO_DIVX4, VIDEO_DIVX, VIDEO_DIVX4},
        {MKV_VIDEO_DIVX5_6, VIDEO_DIVX, VIDEO_DIVX5_6},
        {MKV_VIDEO_XVID, VIDEO_XVID, VIDEO_XVID},
        {MKV_VIDEO_WMV7, VIDEO_WMV, VIDEO_WMV7},
        {MKV_VIDEO_WMV8, VIDEO_WMV, VIDEO_WMV8},
        {MKV_VIDEO_WMV9, VIDEO_WMV, VIDEO_WMV9},
        {MKV_VIDEO_WMV9A, VIDEO_WMV, VIDEO_WMV9A},
        {MKV_VIDEO_WVC1, VIDEO_WMV, VIDEO_WVC1},
        {MKV_VIDEO_SORENSON_H263, VIDEO_SORENSON_H263, VIDEO_SORENSON_H263},
        {MKV_FLV_SCREEN_VIDEO, VIDEO_FLV_SCREEN, FLV_SCREEN_VIDEO},
        {MKV_FLV_SCREEN_VIDEO_2, VIDEO_FLV_SCREEN, FLV_SCREEN_VIDEO_2},
        {MKV_VIDEO_VP6, VIDEO_ON2_VP, VIDEO_VP6},
        {MKV_VIDEO_VP6A, VIDEO_ON2_VP, VIDEO_VP6A},
        {MKV_VIDEO_VP7, VIDEO_ON2_VP, VIDEO_VP7},
        {MKV_VIDEO_VP8, VIDEO_ON2_VP, VIDEO_VP8},
        {MKV_REAL_VIDEO_RV30, VIDEO_REAL, REAL_VIDEO_RV30},
        {MKV_REAL_VIDEO_RV40, VIDEO_REAL, REAL_VIDEO_RV40},
        {MKV_REAL_VIDEO_RV10, VIDEO_REAL, REAL_VIDEO_RV10},
        {MKV_REAL_VIDEO_RV20, VIDEO_REAL, REAL_VIDEO_RV20},
        {MKV_VIDEO_VP9, VIDEO_ON2_VP, VIDEO_VP9},
        {MKV_VIDEO_HEVC, VIDEO_HEVC, VIDEO_HEVC},
        {MKV_VIDEO_AV1, VIDEO_AV1, VIDEO_AV1},
};

static TrackTypeMap mkv_subtitle_track_map_tab[] = {
        {MKV_SUBTITLE_UNKNOWN, TXT_TYPE_UNKNOWN, TXT_TYPE_UNKNOWN},
        {MKV_SUBTITLE_TEXT, TXT_SUBTITLE_TEXT, TXT_TYPE_UNKNOWN},
        {MKV_SUBTITLE_SSA, TXT_SUBTITLE_SSA, TXT_TYPE_UNKNOWN},
        {MKV_SUBTITLE_ASS, TXT_SUBTITLE_ASS, TXT_TYPE_UNKNOWN}};

int matroska_get_track_type(MKVReaderContext* pctx, uint32 tracknum, uint32* mediaType,
                            uint32* decoderType, uint32* decoderSubtype) {
    int i = 0;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !mediaType || !decoderType || !decoderSubtype)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    switch (stream->track_type) {
        case MATROSKA_TRACK_TYPE_NONE:

            *mediaType = MEDIA_TYPE_UNKNOWN;

            break;
        case MATROSKA_TRACK_TYPE_VIDEO:

            *mediaType = MEDIA_VIDEO;

            *decoderType = VIDEO_TYPE_UNKNOWN;
            *decoderSubtype = VIDEO_TYPE_UNKNOWN;

            for (i = 0;
                 i < (int)(sizeof(mkv_video_track_map_tab) / sizeof(mkv_video_track_map_tab[0]));
                 i++) {
                if (stream->codec_type == mkv_video_track_map_tab[i].inner_type) {
                    *decoderType = mkv_video_track_map_tab[i].decoder_type;
                    *decoderSubtype = mkv_video_track_map_tab[i].decoder_subtype;
                    break;
                }
            }

            break;
        case MATROSKA_TRACK_TYPE_AUDIO:

            *mediaType = MEDIA_AUDIO;

            *decoderType = AUDIO_TYPE_UNKNOWN;
            *decoderSubtype = AUDIO_TYPE_UNKNOWN;

            for (i = 0;
                 i < (int)(sizeof(mkv_audio_track_map_tab) / sizeof(mkv_audio_track_map_tab[0]));
                 i++) {
                if (stream->codec_type == mkv_audio_track_map_tab[i].inner_type) {
                    *decoderType = mkv_audio_track_map_tab[i].decoder_type;
                    *decoderSubtype = mkv_audio_track_map_tab[i].decoder_subtype;
                    break;
                }
            }

            break;
        case MATROSKA_TRACK_TYPE_COMPLEX:

            *mediaType = MEDIA_TYPE_UNKNOWN;

            break;
        case MATROSKA_TRACK_TYPE_LOGO:

            *mediaType = MEDIA_TYPE_UNKNOWN;

            break;
        case MATROSKA_TRACK_TYPE_SUBTITLE:

            *mediaType = MEDIA_TEXT;
            *decoderType = VIDEO_TYPE_UNKNOWN;
            *decoderSubtype = VIDEO_TYPE_UNKNOWN;

            for (i = 0; i < (int)(sizeof(mkv_subtitle_track_map_tab) /
                                  sizeof(mkv_subtitle_track_map_tab[0]));
                 i++) {
                if (stream->codec_type == mkv_subtitle_track_map_tab[i].inner_type) {
                    *decoderType = mkv_subtitle_track_map_tab[i].decoder_type;
                    *decoderSubtype = mkv_subtitle_track_map_tab[i].decoder_subtype;
                    break;
                }
            }

            break;
        case MATROSKA_TRACK_TYPE_CONTROL:

            *mediaType = MEDIA_TYPE_UNKNOWN;

            break;
        default:

            *mediaType = MEDIA_TYPE_UNKNOWN;

            break;
    }

    return 0;
}

int matroska_get_language(MKVReaderContext* pctx, uint32 tracknum, char* langcode) {
    TrackEntry* track = NULL;

    if (!pctx || !langcode)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    strncpy(langcode, track->track_language.str, 3);
    langcode[3] = '\0';

    return 0;
}

int matroska_get_max_samplesize(MKVReaderContext* pctx, uint32 tracknum, uint32* size) {
    if (!pctx || !size)
        return -1;
    (void)tracknum;
    *size = 0;

    return 0;
}

int matroska_get_bitrate(MKVReaderContext* pctx, uint32 tracknum, uint32* bitrate) {
    uint8* value;
    uint32 size = 0;
    if (!pctx || !bitrate)
        return -1;

    *bitrate = 0;

    if (0 ==
        matroska_find_tag_from_taglist(pctx, tracknum, 0, MATROSKA_TAG_ID_BPS, &size, &value)) {
        *bitrate = atoi((char*)value);
    }

    return 0;
}

int matroska_get_track_crypto_key(MKVReaderContext* pctx, TrackEntry* track, uint32* len,
                                  uint8** data) {
    if (!pctx || !len || !data)
        return -1;

    if (!track)
        return -1;

    if (track->content_encoding_count <= 0)
        return -1;

    if (track->content_encoding_list[0].encryption.enc_algo != 5 ||
        track->content_encoding_list[0].encryption.enc_key_id.binary_size == 0 ||
        track->content_encoding_list[0].encryption.enc_key_id.binary_ptr == NULL)
        return -1;

    *len = track->content_encoding_list[0].encryption.enc_key_id.binary_size;
    *data = (uint8*)track->content_encoding_list[0].encryption.enc_key_id.binary_ptr;

    return 0;
}

int matroska_get_sample_duration(MKVReaderContext* pctx, uint32 tracknum, uint64* duration) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !duration)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *duration = track->default_duration / 1000;

    return 0;
}

int matroska_get_video_frame_width(MKVReaderContext* pctx, uint32 tracknum, uint32* width) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !width)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *width = track->vinfo.pixel_width;

    return 0;
}

int matroska_get_video_frame_height(MKVReaderContext* pctx, uint32 tracknum, uint32* height) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !height)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *height = track->vinfo.pixel_height;

    return 0;
}

int matroska_get_video_pixelbits(MKVReaderContext* pctx, uint32 tracknum, uint32* bitcount) {
    if (!pctx || !bitcount)
        return -1;
    (void)tracknum;
    *bitcount = 0;

    return 0;
}

int matroska_get_audio_channels(MKVReaderContext* pctx, uint32 tracknum, uint32* channels) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !channels)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *channels = track->ainfo.channels;

    return 0;
}

int matroska_get_audio_samplerate(MKVReaderContext* pctx, uint32 tracknum, uint32* samplerate) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !samplerate)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *samplerate = track->ainfo.sampling_frequency;

    return 0;
}

int matroska_get_audio_samplebits(MKVReaderContext* pctx, uint32 tracknum, uint32* samplebits) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !samplebits)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *samplebits = track->ainfo.bitdepth;

    return 0;
}

int matroska_get_extra_data(MKVReaderContext* pctx, uint32 tracknum, uint8** data, uint32* size) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !data || !size)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *size = 0;
    *data = NULL;

    if (!stream->codec_extra_data || !stream->codec_extra_size) {
        return 0;
    }

    *data = stream->codec_extra_data;
    *size = stream->codec_extra_size;

    return 0;
}

int matroska_get_wave_format(MKVReaderContext* pctx, uint32 tracknum, WaveFormatEx** waveinfo) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !waveinfo)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *waveinfo = NULL;
    if (!stream->has_wave_info)
        return 0;

    *waveinfo = &stream->waveinfo;

    return 0;
}

int matroska_get_text_width(MKVReaderContext* pctx, uint32 tracknum, uint32* width) {
    if (!pctx || !width)
        return -1;
    *width = 0;
    (void)tracknum;
    return 0;
}

int matroska_get_text_height(MKVReaderContext* pctx, uint32 tracknum, uint32* height) {
    if (!pctx || !height)
        return -1;
    *height = 0;
    (void)tracknum;
    return 0;
}

int close_content_encoding_info(MKVReaderContext* pctx, ContentEncodingInfo* pinfo) {
    if (!pctx || !pinfo)
        return 0;

    if (pinfo->compression.content_compression_settings.binary_ptr)
        free_stream_buffer(&pctx->bs, pinfo->compression.content_compression_settings.binary_ptr,
                           NON_STREAM);
    pinfo->compression.content_compression_settings.binary_ptr = NULL;

    if (pinfo->encryption.enc_key_id.binary_ptr)
        free_stream_buffer(&pctx->bs, pinfo->encryption.enc_key_id.binary_ptr, NON_STREAM);
    pinfo->encryption.enc_key_id.binary_ptr = NULL;
    return 0;
}

int close_segment_track_entry(MKVReaderContext* pctx, TrackEntry* track_entry) {
    int i = 0;

    if (!pctx || !track_entry)
        return 0;

    if (track_entry->codec_priv.binary_ptr)
        free_stream_buffer(&pctx->bs, (char*)track_entry->codec_priv.binary_ptr, NON_STREAM);
    track_entry->codec_priv.binary_ptr = NULL;

    if (track_entry->attach_link_list)
        free_stream_buffer(&pctx->bs, (char*)track_entry->attach_link_list, NON_STREAM);
    track_entry->attach_link_list = NULL;

    for (i = 0; i < track_entry->content_encoding_count; i++)
        close_content_encoding_info(pctx, &track_entry->content_encoding_list[i]);

    if (track_entry->content_encoding_list)
        free_stream_buffer(&pctx->bs, (char*)track_entry->content_encoding_list, NON_STREAM);
    track_entry->content_encoding_list = NULL;

    if (track_entry->colorMetadataPtr)
        free_stream_buffer(&pctx->bs, (char*)track_entry->colorMetadataPtr, NON_STREAM);
    track_entry->colorMetadataPtr = NULL;

    if (track_entry->colorPtr)
        free_stream_buffer(&pctx->bs, (char*)track_entry->colorPtr, NON_STREAM);
    track_entry->colorPtr = NULL;

    return 0;
}

int clost_cluster_block_group(MKVReaderContext* pctx, BlockGroup* pblock) {
    if (!pctx || !pblock)
        return 0;

    if (pblock->block.binary_ptr)
        free_stream_buffer(&pctx->bs, (char*)pblock->block.binary_ptr, NON_STREAM);
    pblock->block.binary_ptr = NULL;

    if (pblock->reference_block_list)
        free_stream_buffer(&pctx->bs, (char*)pblock->reference_block_list, NON_STREAM);
    pblock->reference_block_list = NULL;

    return 0;
}

int close_segment_cluster_entry(MKVReaderContext* pctx, ClusterEntry* cluster_entry) {
    int i = 0;

    if (!pctx || !cluster_entry)
        return 0;

    for (i = 0; i < cluster_entry->group_count; i++)
        clost_cluster_block_group(pctx, &cluster_entry->group_list[i]);

    if (cluster_entry->group_list)
        free_stream_buffer(&pctx->bs, (char*)cluster_entry->group_list, NON_STREAM);
    cluster_entry->group_list = NULL;

    for (i = 0; i < cluster_entry->simple_block_count; i++) {
        if (cluster_entry->simple_block_list[i].binary_ptr) {
            free_stream_buffer(&pctx->bs, (char*)cluster_entry->simple_block_list[i].binary_ptr,
                               NON_STREAM);
            cluster_entry->simple_block_list[i].binary_ptr = NULL;
        }
    }

    if (cluster_entry->simple_block_list)
        free_stream_buffer(&pctx->bs, (char*)cluster_entry->simple_block_list, NON_STREAM);
    cluster_entry->simple_block_list = NULL;

    return 0;
}

int close_segment_cuepoint_entry(MKVReaderContext* pctx, CuePointEntry* cue_entry) {
    if (!pctx || !cue_entry)
        return 0;

    if (cue_entry->cue_track_list) {
        free_stream_buffer(&pctx->bs, (char*)cue_entry->cue_track_list, NON_STREAM);
        cue_entry->cue_track_list = NULL;
    }

    return 0;
}

int close_segment_chapter_tracks(MKVReaderContext* pctx, ChapterTracks* chapter_track) {
    if (!pctx || !chapter_track)
        return 0;

    if (chapter_track->chapter_track_number)
        free_stream_buffer(&pctx->bs, (char*)chapter_track->chapter_track_number, NON_STREAM);
    chapter_track->chapter_track_number = NULL;
    return 0;
}

int close_segment_chapter_display(MKVReaderContext* pctx, ChapterDisplay* chapter_disp) {
    if (!pctx || !chapter_disp)
        return 0;

    return 0;
}

int close_segment_chapter_edition(MKVReaderContext* pctx, ChapterEdition* chapter_entry) {
    int i, j;

    if (!pctx || !chapter_entry)
        return 0;

    for (i = 0; i < chapter_entry->chapter_atom_count; i++) {
        close_segment_chapter_tracks(pctx, &chapter_entry->chapter_atom_list[i].chapter_track);

        for (j = 0; j < chapter_entry->chapter_atom_list[i].chapter_display_count; j++)
            close_segment_chapter_display(
                    pctx, &chapter_entry->chapter_atom_list[i].chapter_display_list[j]);

        if (chapter_entry->chapter_atom_list[i].chapter_display_list)
            free_stream_buffer(&pctx->bs,
                               (char*)chapter_entry->chapter_atom_list[i].chapter_display_list,
                               NON_STREAM);
        chapter_entry->chapter_atom_list[i].chapter_display_list = NULL;
    }

    if (chapter_entry->chapter_atom_list)
        free_stream_buffer(&pctx->bs, (char*)chapter_entry->chapter_atom_list, NON_STREAM);
    chapter_entry->chapter_atom_list = NULL;

    return 0;
}

int close_segment_attachment(MKVReaderContext* pctx, Attachments* attach_entry) {
    if (!pctx || !attach_entry)
        return 0;

    if (attach_entry->file_data.binary_ptr)
        free_stream_buffer(&pctx->bs, (char*)attach_entry->file_data.binary_ptr, NON_STREAM);
    attach_entry->file_data.binary_ptr = NULL;

    return 0;
}

int close_segment_tag_targets(MKVReaderContext* pctx, TagTarget* tag_target) {
    if (!pctx || !tag_target)
        return 0;

    if (tag_target->track_uid)
        free_stream_buffer(&pctx->bs, (char*)tag_target->track_uid, NON_STREAM);
    tag_target->track_uid = NULL;

    if (tag_target->edition_uid)
        free_stream_buffer(&pctx->bs, (char*)tag_target->edition_uid, NON_STREAM);
    tag_target->edition_uid = NULL;

    if (tag_target->chapter_uid)
        free_stream_buffer(&pctx->bs, (char*)tag_target->chapter_uid, NON_STREAM);
    tag_target->chapter_uid = NULL;

    if (tag_target->attach_uid)
        free_stream_buffer(&pctx->bs, (char*)tag_target->attach_uid, NON_STREAM);
    tag_target->attach_uid = NULL;

    return 0;
}

int close_segment_simple_tag(MKVReaderContext* pctx, SimpleTag* simple_tag) {
    if (!pctx || !simple_tag)
        return 0;

    if (simple_tag->tag_name)
        free_stream_buffer(&pctx->bs, (char*)simple_tag->tag_name, NON_STREAM);
    simple_tag->tag_name = NULL;

    if (simple_tag->tag_binary.binary_ptr)
        free_stream_buffer(&pctx->bs, (char*)simple_tag->tag_binary.binary_ptr, NON_STREAM);
    simple_tag->tag_binary.binary_ptr = NULL;

    return 0;
}

int close_segment_tags_entry(MKVReaderContext* pctx, TagEntry* tag_entry) {
    int i = 0;

    if (!pctx || !tag_entry)
        return 0;

    close_segment_tag_targets(pctx, &tag_entry->tag_target);

    for (i = 0; i < tag_entry->simple_tag_count; i++)
        close_segment_simple_tag(pctx, &tag_entry->simple_tag_list[i]);

    if (tag_entry->simple_tag_list)
        free_stream_buffer(&pctx->bs, (char*)tag_entry->simple_tag_list, NON_STREAM);
    tag_entry->simple_tag_list = NULL;

    return 0;
}

static void matroska_destory_packet(MKVReaderContext* pctx, mkv_packet* packet) {
    ByteStream* pbs = NULL;

    if (!pctx || !packet)
        return;

    pbs = &pctx->bs;

    free_stream_buffer(pbs, (char*)packet->data, NON_STREAM);

    packet->data = NULL;
    packet->size = 0;
}

int matroska_free_packet(MKVReaderContext* pctx, mkv_packet* packet) {

    if (!pctx || !packet)
        return -1;

    if (packet) {
        if (packet->data)
            matroska_destory_packet(pctx, packet);

        packet->data = NULL;
        packet->size = 0;
    }

    return 0;
}

void matroska_clear_queue(MKVReaderContext* pctx, uint32 tracknum) {
    int n;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;
    ByteStream* pbs = &pctx->bs;

    if (!pctx)
        return;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return;

    if (stream->packets) {
        for (n = 0; n < stream->num_packets; n++) {
            matroska_free_packet(pctx, stream->packets[n]);
            free_stream_buffer(pbs, (char*)stream->packets[n], NON_STREAM);
        }
        free_stream_buffer(pbs, (char*)stream->packets, NON_STREAM);
    }

    pctx->done = 0;
    stream->packets = NULL;
    stream->num_packets = 0;

    stream->last_pts = 0;
    stream->last_pts_pos = 0;

    stream->rainfo.sub_packet_cnt = 0;
}

static void matroska_clear_queue_all(MKVReaderContext* pctx) {
    int i = 0;

    pctx->prev_pkt = NULL;

    for (i = 0; i < pctx->stream_count; i++) {
        matroska_clear_queue(pctx, pctx->stream_list[i].stream_index);
    }
}

void clear_matroska_cue_list(MKVReaderContext* pctx) {
    if (!pctx)
        return;

    if (pctx->sctx.cue_count > 0) {
        int i = 0;
        for (i = 0; i < pctx->sctx.cue_count; i++) {
            close_segment_cuepoint_entry(pctx, &pctx->sctx.cue_list[i]);
        }
        pctx->sctx.cue_count = 0;
    }

    if (pctx->sctx.cue_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.cue_list, NON_STREAM);
    pctx->sctx.cue_list = NULL;

    return;
}

int close_matroska_file_header(MKVReaderContext* pctx) {
    int i = 0;

    if (!pctx)
        return 0;

    matroska_clear_queue_all(pctx);

    if (pctx->sctx.seek_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.seek_list, NON_STREAM);
    pctx->sctx.seek_list = NULL;

    for (i = 0; i < pctx->sctx.track_count; i++)
        close_segment_track_entry(pctx, &pctx->sctx.track_list[i]);

    if (pctx->sctx.track_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.track_list, NON_STREAM);
    pctx->sctx.track_list = NULL;

    for (i = 0; i < pctx->sctx.cluster_count; i++)
        close_segment_cluster_entry(pctx, &pctx->sctx.cluster_list[i]);

    if (pctx->sctx.cluster_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.cluster_list, NON_STREAM);
    pctx->sctx.cluster_list = NULL;

    clear_matroska_cue_list(pctx);

    for (i = 0; i < pctx->sctx.chapter_count; i++)
        close_segment_chapter_edition(pctx, &pctx->sctx.chapter_list[i]);

    if (pctx->sctx.mkv_chapterMenu.pChapterList) {
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.mkv_chapterMenu.pChapterList, NON_STREAM);
        pctx->sctx.mkv_chapterMenu.pChapterList = NULL;
    }

    if (pctx->sctx.chapter_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.chapter_list, NON_STREAM);
    pctx->sctx.chapter_list = NULL;

    for (i = 0; i < pctx->sctx.attach_count; i++)
        close_segment_attachment(pctx, &pctx->sctx.attach_list[i]);

    if (pctx->sctx.attach_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.attach_list, NON_STREAM);
    pctx->sctx.attach_list = NULL;

    for (i = 0; i < pctx->sctx.tag_count; i++)
        close_segment_tags_entry(pctx, &pctx->sctx.tag_list[i]);

    if (pctx->sctx.tag_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.tag_list, NON_STREAM);
    pctx->sctx.tag_list = NULL;

    clear_matroska_stream_list(pctx);

#ifdef SUPPORT_PRE_SCAN_CLUSTER
    if (pctx->sctx.prescan_cluster_index_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.prescan_cluster_index_list, NON_STREAM);
    pctx->sctx.prescan_cluster_index_list = NULL;
#endif

#ifdef SUPPORT_MKV_DRM
    if (pctx->sctx.stDRM_Hdr.pdrmHdr)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.stDRM_Hdr.pdrmHdr, NON_STREAM);
    pctx->sctx.stDRM_Hdr.pdrmHdr = NULL;
#endif

    return 0;
}

int clear_matroska_stream_list(MKVReaderContext* pctx) {
    int i = 0;
    if (!pctx)
        return 0;

    for (i = 0; i < pctx->stream_count; i++) {
        if (pctx->stream_list[i].rainfo.audio_buf)
            free_stream_buffer(&pctx->bs, (char*)pctx->stream_list[i].rainfo.audio_buf, NON_STREAM);
        pctx->stream_list[i].rainfo.audio_buf = NULL;

        if (pctx->stream_list[i].index_list)
            free_stream_buffer(&pctx->bs, (char*)pctx->stream_list[i].index_list, NON_STREAM);
        pctx->stream_list[i].index_list = NULL;

        if (pctx->stream_list[i].codec_extra_data)
            free_stream_buffer(&pctx->bs, (char*)pctx->stream_list[i].codec_extra_data, NON_STREAM);
        pctx->stream_list[i].codec_extra_data = NULL;

        if (pctx->stream_list[i].ext_tag_list)
            matroska_delete_track_ext_taglist(&pctx->bs, pctx->stream_list[i].ext_tag_list);
        pctx->stream_list[i].ext_tag_list = NULL;
    }

    if (pctx->stream_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->stream_list, NON_STREAM);
    pctx->stream_list = NULL;
    pctx->stream_count = 0;

    return 0;
}
#if 0
int is_matroska_file_type(char *ptr, int size)
{
    uint64 n;
    ebml_info einfo;

    if(!ptr || size < 8) return 0;

    n = read_one_ebml_info(ptr, size, &einfo);
    if(!n) return 0;

    if (einfo.s_id != FSL_EBML_ID_HEADER)
        return 0;

    if (size < einfo.s_offset + einfo.s_size)
        return 0;

    for (n = 0; n <= einfo.s_size-(sizeof(MKV_DOCTYPE)-1); n++)
    {
        if (!memcmp(ptr+n, MKV_DOCTYPE, sizeof(MKV_DOCTYPE)-1))
            return 1;
    }

    return 0;
}
#endif

int index_search_timestamp(mkvStream* stream, int64 desired_timestamp, int flags) {
    int a, b, m;
    int64 timestamp;
    CueIndex* entries;

    if (!stream)
        return -1;

    a = -1;
    b = stream->index_count;
    entries = stream->index_list;

    if (!b || !entries)
        return -1;

    while (b - a > 1) {
        m = (a + b) >> 1;
        timestamp = entries[m].pts;
        if (timestamp >= desired_timestamp)
            b = m;
        if (timestamp <= desired_timestamp)
            a = m;
    }

    m = b;

    if (flags == SEEK_FLAG_NO_EARLIER) {
        while (m < stream->index_count &&
               entries[m].pts < (uint64)desired_timestamp)  // ts>=pos: ENGR142866
        {
            m++;
        }
    } else if (flags == SEEK_FLAG_NO_LATER) {
        while (m > 0 && entries[m].pts > (uint64)desired_timestamp)  // ts<=pos
        {
            m--;
        }
    }
    if (m >= stream->index_count)
        m = stream->index_count - 1;

    return m;
}

int matroska_file_seek(MKVReaderContext* pctx, uint64 utime, uint32 flags, uint32 trackType) {
    int i;
    int idx_pos;
    uint64 seek_pos = 0;
    uint64 timestamp = 0;
    mkvStream* stream = NULL;
    TrackEntry* track = NULL;
    uint64 seek_ts = 0x7FFFFFFF;  // pts, in ms
    ByteStream* pbs = &pctx->bs;

    if (!pctx)
        return -1;

    MKV_LOG("%s: utime: %lld, flags: 0x%X \r\n", __FUNCTION__, utime, flags);
    if (0 == utime && flags == SEEK_FLAG_NEAREST) {
        seek_ts = 0;
        seek_pos = pctx->sctx.first_cluster_pos;
        goto _CLEAR_QUEUE_SEEK;
    }

    if (!pctx->sctx.cue_parsed) {
        MKV_LOG("matroska_file_seek parse cue list BEGIN");
        matroska_initialize_index(pctx);
        MKV_LOG("matroska_file_seek parse cue list END");
    }

    pctx->remaining_sample = FALSE;

    if (pctx->sctx.info.time_code_scale) {
        seek_ts = utime * 1000 / pctx->sctx.info.time_code_scale;
    } else {
        seek_ts = utime;
    }

    if (pctx->sctx.cue_count > 0) {
        for (i = 0; i < pctx->stream_count; i++) {
            stream = &pctx->stream_list[i];

            track = matroska_find_track_by_num(pctx, stream->stream_index);
            if (!track)
                continue;

            track->end_timecode = 0;

            track->is_sample_data_remained = FALSE;
            track->seek_to_EOS = FALSE;
            track->seek_to_BOS = FALSE;

            if (!stream->index_count || !stream->index_list)
                continue;

            idx_pos = index_search_timestamp(stream, seek_ts, flags);
            if (idx_pos == -1)
                continue;
            if (stream->index_count == idx_pos + 1)
                track->seek_to_EOS = TRUE;
            if (0 == idx_pos)
                track->seek_to_BOS = TRUE;

            if (stream->track_type == MATROSKA_TRACK_TYPE_VIDEO || !pctx->sctx.has_video_track) {
                seek_ts = stream->index_list[idx_pos].pts;
                seek_pos = stream->index_list[idx_pos].pos;
                if (stream->index_list[idx_pos].block_num > 1)
                    pctx->skip_to_timecode = seek_ts;

                track->last_ts = seek_ts;
                if (pctx->sctx.info.time_code_scale)
                    track->last_ts = seek_ts * pctx->sctx.info.time_code_scale / 1000;

                break;
            }
        }
    } else {
        if (!pctx->sctx.prescan_cluster_done) {
            MKV_TIMER_START();
            read_segment_prescan_cluster_index_list(pctx);
            MKV_TIMER_STOP("seek read cluster");
        }

        if (trackType == MATROSKA_TRACK_TYPE_VIDEO) {
            int cluster_index = 0;
            MKV_LOG("matroska_file_seek matroska_find_cluster ts=%lld", seek_ts);

            cluster_index = matroska_find_cluster(pctx, seek_ts);
            if (cluster_index < 0)
                return -1;

            seek_pos = pctx->sctx.prescan_cluster_index_list[cluster_index].offset;
            seek_ts = pctx->sctx.prescan_cluster_index_list[cluster_index].timecode;

            MKV_LOG("matroska_file_seek no cue video pos=%lld, ts=%lld", (long long)seek_pos,
                    (long long)seek_ts);

            pctx->skip_to_keyframe = 1;
        } else if (pctx->sctx.has_video_track) {
            MKV_LOG("matroska_file_seek audio, do nothing");
            return 0;
        } else if (!pctx->sctx.has_video_track) {  // pure audio
            int cluster_index = 0;

            cluster_index = matroska_find_cluster(pctx, seek_ts);
            if (cluster_index < 0)
                return -1;

            seek_pos = pctx->sctx.prescan_cluster_index_list[cluster_index].offset;
            // seek ts should be target ts, just seek to the position then skip to target frames

            pctx->skip_to_timecode = seek_ts;
            MKV_LOG("matroska_file_seek no cue audio pos=%lld, pts=%lld", (long long)seek_pos,
                    (long long)seek_ts);
        }
    }

    // can't find seek position
    if (seek_ts == 0x7FFFFFFF) {
        MKV_LOG("can't find seek position, do nothing \r\n");
        return 0;  // it is not error , specially for case: utime=0
    }

_CLEAR_QUEUE_SEEK:

    timestamp = seek_ts;

    matroska_clear_queue_all(pctx);

    MKV_LOG("seek pos: %lld \r\n", seek_pos);
    seek_stream_buffer(pbs, seek_pos, 1);

    for (i = 0; i < pctx->stream_count; i++) {
        stream = &pctx->stream_list[i];
        stream->position = timestamp;
        stream->last_seek_pos = timestamp;
    }

    return 0;
}

int matroska_track_seek(MKVReaderContext* pctx, uint32 tracknum, uint64 utime, uint32 flags) {
    int idx_pos;
    uint64 seek_pos = 0;
    uint64 timestamp = 0;
    mkvStream* stream = NULL;
    TrackEntry* track = NULL;
    uint64 seek_ts = 0x7FFFFFFF;
    ByteStream* pbs = &pctx->bs;

    if (!pctx)
        return -1;

    if (!utime) {
        seek_ts = 0;
        seek_pos = pctx->sctx.first_cluster_pos;
        goto _CLEAR_QUEUE_SEEK;
    }

    if (pctx->stream_count < (int)tracknum || !pctx->stream_list)
        return -1;

    stream = &pctx->stream_list[tracknum - 1];

    if (!stream->index_count || !stream->index_list)
        return -1;

    if (pctx->sctx.info.time_code_scale)
        utime = utime * 1000000 / pctx->sctx.info.time_code_scale;

    track = matroska_find_track_by_num(pctx, stream->stream_index);
    if (!track)
        return -1;

    track->end_timecode = 0;

    idx_pos = index_search_timestamp(stream, utime, flags);
    if (idx_pos == -1)
        return -1;

    seek_ts = stream->index_list[idx_pos].pts;
    seek_pos = stream->index_list[idx_pos].pos;

    // can't find seek position
    if (seek_ts == 0x7FFFFFFF)
        return -1;

_CLEAR_QUEUE_SEEK:

    timestamp = seek_ts;

    matroska_clear_queue_all(pctx);

#ifdef SUPPORT_CORRUPT_CLUSTER
    // clear flag
    pctx->sctx.continuous_bad_cluster_cnt = 0;
#endif

    seek_stream_buffer(pbs, seek_pos, 1);

    if (stream) {
        stream->position = timestamp;
        stream->last_seek_pos = timestamp;
    }

    return 0;
}

static int matroska_deliver_packet(MKVReaderContext* pctx, mkv_packet* packet, uint32 tracknum) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;
    ByteStream* pbs = &pctx->bs;

    if (!pctx || !packet)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    if (stream->stream_index != tracknum)
        return -1;

    if (stream->num_packets > 0) {
        if (stream->check_pts_pos && stream->last_pts_pos == 0)
            return -1;

        memcpy(packet, stream->packets[0], sizeof(mkv_packet));

        if (pctx->prev_pkt == stream->packets[0])
            pctx->prev_pkt = NULL;

        free_stream_buffer(pbs, (char*)stream->packets[0], NON_STREAM);

        if (stream->num_packets > 1) {
            memmove(&stream->packets[0], &stream->packets[1],
                    (stream->num_packets - 1) * sizeof(mkv_packet*));
        } else {
            free_stream_buffer(pbs, (char*)stream->packets, NON_STREAM);
            stream->packets = NULL;
            stream->last_pts = 0;
            stream->last_pts_pos = 0;
        }

        stream->num_packets--;
        stream->last_pts_pos--;
        stream->position = packet->pts;
        stream->packets_size -= packet->size;

        return 0;
    }

    return -1;
}

static void matroska_init_packet(mkv_packet* packet) {
    packet->pts = (uint64)(0xFFFFFFFFFFFFFFFFULL);
    packet->pos = (uint64)(0xFFFFFFFFFFFFFFFFULL);
    packet->duration = 0;
    packet->flags = 0;
    packet->stream_index = 0;
}

static int matroska_new_packet(MKVReaderContext* pctx, mkv_packet* packet, int size) {
    uint8* data = NULL;
    ByteStream* pbs = &pctx->bs;

    data = (uint8*)alloc_stream_buffer(pbs, size + MKV_INPUT_BUFFER_PADDING_SIZE, NON_STREAM);
    if (!data)
        return -1;

    memset(data + size, 0, MKV_INPUT_BUFFER_PADDING_SIZE);

    matroska_init_packet(packet);

    packet->data = data;
    packet->size = size;

    return 0;
}

static void matroska_fix_ass_packet(MKVReaderContext* pctx, mkv_packet* pkt,
                                    uint64 display_duration) {
    ByteStream* pbs = &pctx->bs;
    char *line, *layer = NULL, *ptr = (char*)pkt->data, *end = ptr + pkt->size;

    for (; *ptr != ',' && ptr < end - 1; ptr++);
    if (*ptr == ',')
        layer = ++ptr;
    for (; *ptr != ',' && ptr < end - 1; ptr++);
    if (*ptr == ',') {
        int64 end_pts = pkt->pts + display_duration;
        int sc = (int)(pctx->sctx.info.time_code_scale * pkt->pts / 10000000);
        int ec = (int)(pctx->sctx.info.time_code_scale * end_pts / 10000000);
        int sh, sm, ss, eh, em, es, len;
        sh = sc / 360000;
        sc -= 360000 * sh;
        sm = sc / 6000;
        sc -= 6000 * sm;
        ss = sc / 100;
        sc -= 100 * ss;
        eh = ec / 360000;
        ec -= 360000 * eh;
        em = ec / 6000;
        ec -= 6000 * em;
        es = ec / 100;
        ec -= 100 * es;
        *ptr++ = '\0';
        len = 50 + end - ptr + MKV_INPUT_BUFFER_PADDING_SIZE;
        if (!(line = alloc_stream_buffer(pbs, len, NON_STREAM)))
            return;
        sprintf(line, "Dialogue: %s,%d:%02d:%02d.%02d,%d:%02d:%02d.%02d,%s\r\n", layer, sh, sm, ss,
                sc, eh, em, es, ec, ptr);
        free_stream_buffer(pbs, (char*)pkt->data, NON_STREAM);
        pkt->data = (uint8*)line;
        pkt->size = (uint32)strlen(line);
    }
}
__attribute__((unused))
static uint32 CreateADTSHeaderInfoForAAC(void* buf, uint32 frame_len, uint32 samplerate,
                                         uint32 audiochannels) {
    uint16 i = 0;
    BitsCtx bcx;
    uint32 sample_idx = 0;

    for (i = 0; i < sizeof(ADTSSampleFreqTable) / sizeof(ADTSSampleFreqTable[0]); i++) {
        if (ADTSSampleFreqTable[i] == (int32)samplerate) {
            sample_idx = i;
            break;
        }
    }

    InitPutBits(&bcx, buf);

    // adts fixed header
    PutBits(&bcx, 0xfff, 12);         // syncword
    PutBits(&bcx, 0x1, 1);            // ID
    PutBits(&bcx, 0x0, 2);            // layer
    PutBits(&bcx, 0x1, 1);            // protection-absent
    PutBits(&bcx, 0x1, 2);            // profile
    PutBits(&bcx, sample_idx, 4);     // sampling_frequency_indx
    PutBits(&bcx, 0x0, 1);            // private bit
    PutBits(&bcx, audiochannels, 3);  // channel configuration
    PutBits(&bcx, 0x0, 1);            // orignial/copy
    PutBits(&bcx, 0x0, 1);            // home

    // adts variable header
    PutBits(&bcx, 0x0, 1);         // copyright identification-bit
    PutBits(&bcx, 0x0, 1);         // copyright identification-start
    PutBits(&bcx, frame_len, 13);  // frame_length
    PutBits(&bcx, 0x0, 11);        // adts_buffer_fullness
    PutBits(&bcx, 0x0, 2);         // number_of_raw_data_blocks_in_frame

    return 0;
}

static int matroska_process_packet(MKVReaderContext* pctx, mkv_packet* packet) {
    uint8* ptr = NULL;
    uint32 rem_bytes = 0;
    mkvStream* stream = NULL;
    ByteStream* pbs = NULL;
    TrackEntry* track = NULL;

    if (!pctx || !packet)
        return -1;

    pbs = &pctx->bs;

    track = matroska_find_track_by_num(pctx, packet->stream_index);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    ptr = packet->data;
#ifdef SUPPORT_MKV_DRM
    ;
    ;  // add decrypt A/V data block here
    if ((pctx->sctx.bHasDRMHdr) && (pctx->bHasDrmLib)) {
        drmAPI_s* pDRMAPI = &(pctx->sDrmAPI);
        drmErrorCodes_t decrypt_ret;

        if (MATROSKA_TRACK_TYPE_VIDEO == stream->track_type) {
            decrypt_ret = pDRMAPI->drmDecryptVideo(pctx->drmContext, packet->data, packet->size,
                                                   pctx->frameKey);
            if (DRM_SUCCESS != decrypt_ret) {
                PARSERMSG("Failed to Decrypt Video (ErrCode 0x%08x) \n", decrypt_ret);
                return (MKV_ERR_DRM_OTHERS);
            }
        } else if (MATROSKA_TRACK_TYPE_AUDIO == stream->track_type) {
            decrypt_ret = pDRMAPI->drmDecryptAudio(pctx->drmContext, packet->data, packet->size);
            if (DRM_SUCCESS != decrypt_ret) {
                PARSERMSG("Failed to Decrypt Audio (ErrCode 0x%08x) \n", decrypt_ret);
                return (MKV_ERR_DRM_OTHERS);
            }
        }
    }
#endif

    if (stream->track_type == MATROSKA_TRACK_TYPE_VIDEO) {
        if (stream->codec_type == MKV_VIDEO_H264 && !(pctx->flags & FLAG_H264_NO_CONVERT)) {
            uint32 nal_size = 0x01000000;
            uint32 mark_size = stream->h264_mark_size;
            uint8* new_ptr = NULL;
            uint32 offset = 0;
            uint32 start_code = 0x01000000;

            if (0 == mark_size)
                mark_size = 4;
            if (packet->size > 4) {
                if (4 == mark_size)
                    nal_size = (ptr[3] << 24) | (ptr[2] << 16) | (ptr[1] << 8) | ptr[0];
                else if (3 == mark_size)
                    nal_size = (ptr[2] << 16) | (ptr[1] << 8) | ptr[0];
                else if (2 == mark_size)
                    nal_size = (ptr[0] << 8) | ptr[1];
            }

            if (nal_size == 0x01000000)
                return 0;

            rem_bytes = packet->size;

            if (4 != mark_size) {
                new_ptr = (uint8*)alloc_stream_buffer(pbs, packet->size + 64, NON_STREAM);
            }

            while (rem_bytes > 0) {
                if (4 == mark_size) {
                    nal_size = (ptr[3] << 24) | (ptr[2] << 16) | (ptr[1] << 8) | ptr[0];
                    ptr[0] = ptr[1] = ptr[2] = 0;
                    ptr[3] = 0x1;
                    SWAP_32(nal_size);
                    offset += mark_size;
                    ptr += mark_size;
                    if (offset + nal_size > (uint32)packet->size) {
                        break;  // wrong length, add this to avoid write out of buffer range
                    }

                    offset += nal_size;
                    ptr += nal_size;
                    rem_bytes -= (mark_size + nal_size);
                } else if (3 == mark_size) {
                    nal_size = (ptr[2] << 24) | (ptr[1] << 16) | (ptr[0] << 8);
                    memcpy(new_ptr + offset, (void*)(&start_code), 4);
                    offset += 4;
                    SWAP_32(nal_size);

                    ptr += mark_size;
                    if (offset + nal_size > (uint32)packet->size + 64) {
                        break;  // wrong length, add this to avoid write out of new buffer range
                    }
                    memcpy(new_ptr + offset, ptr, nal_size);
                    ptr += (nal_size);
                    offset += nal_size;
                    rem_bytes -= (mark_size + nal_size);
                } else if (2 == mark_size) {
                    nal_size = (ptr[0] << 8) | ptr[1];
                    memcpy(new_ptr + offset, (void*)(&start_code), 4);
                    // add start cpde 0x00000001
                    offset += 4;
                    ptr += mark_size;
                    if (offset + nal_size > (uint32)packet->size + 64) {
                        break;  // wrong length, add this to avoid write out of new buffer range
                    }
                    memcpy(new_ptr + offset, ptr, nal_size);
                    ptr += (nal_size);
                    offset += nal_size;
                    rem_bytes -= (mark_size + nal_size);
                }
            }
            if (new_ptr && (3 == mark_size || 2 == mark_size)) {
                free_stream_buffer(pbs, (char*)packet->data, NON_STREAM);

                packet->data = new_ptr;
                packet->size = offset;
            }
        } else if (stream->codec_type == MKV_REAL_VIDEO_RV10 ||
                   stream->codec_type == MKV_REAL_VIDEO_RV20 ||
                   stream->codec_type == MKV_REAL_VIDEO_RV30 ||
                   stream->codec_type == MKV_REAL_VIDEO_RV40) {
            uint16 flags = 0;
            uint32 timestamp = 0;
            uint32 frame_size = 0;
            uint8* new_ptr = NULL;
            uint32 isvalid, offset;
            uint32 segment_number = 0;
            uint32 segment_number_bak = 0;
            uint32 malloc_len = packet->size + 64;

            segment_number = ptr[0] + 1;
            segment_number_bak = segment_number;
            frame_size = packet->size - (segment_number * 8) - 1;

            if (stream->rvinfo.insert_header)
                malloc_len += MALONE_PAYLOAD_HEADER_SIZE + segment_number * 16;

            ptr = new_ptr = (uint8*)alloc_stream_buffer(pbs, malloc_len, NON_STREAM);
            if (!ptr)
                return -1;

            // frame size
            SWAP_32(frame_size);
            *(uint32*)ptr = frame_size;
            ptr += 4;

            // timestamp
            timestamp = (uint32)packet->pts;
            SWAP_32(timestamp);
            *(uint32*)ptr = timestamp;
            ptr += 4;

            // sequence number
            *(uint32*)ptr = 0;
            ptr += 2;

            // flags
            flags = (uint16)packet->flags;
            SWAP_16(flags);
            *(uint32*)ptr = flags;
            ptr += 2;

            // last packet
            *(uint32*)ptr = 0;
            ptr += 4;

            // flags
            SWAP_32(segment_number);
            *(uint32*)ptr = segment_number;
            ptr += 4;

            memcpy(ptr, packet->data + 1, packet->size - 1);

            SWAP_32(segment_number);
            while (segment_number--) {
                isvalid = *(uint32*)ptr;
                SWAP_32(isvalid);
                *(uint32*)ptr = isvalid;
                ptr += 4;

                offset = *(uint32*)ptr;
                SWAP_32(offset);
                *(uint32*)ptr = offset;
                ptr += 4;
            }

            free_stream_buffer(pbs, (char*)packet->data, NON_STREAM);

            packet->data = new_ptr;
            packet->size += (20 - 1);

            if ((stream->num_packets > 0) && (stream->last_pts + 1 != packet->pts) &&
                (stream->num_packets != stream->last_pts_pos)) {
                uint64 last_pts = stream->packets[stream->last_pts_pos]->pts;
                uint64 frame_duration =
                        (packet->pts - last_pts) / (stream->num_packets - stream->last_pts_pos);

                while (stream->last_pts_pos < stream->num_packets) {
                    stream->packets[stream->last_pts_pos]->pts = last_pts;
                    last_pts += frame_duration;
                    stream->last_pts_pos++;
                };
            }

            stream->last_pts = packet->pts;

            if (stream->rvinfo.insert_header) {
                uint32 header_len = 20 + 8 * segment_number_bak;
                uint32* segment_offset = (uint32*)alloc_stream_buffer(
                        pbs, segment_number_bak * sizeof(uint32), NON_STREAM);
                uint32* segment_len = (uint32*)alloc_stream_buffer(
                        pbs, segment_number_bak * sizeof(uint32), NON_STREAM);
                uint32 frame_len = ((new_ptr[0] << 24) | (new_ptr[1] << 16) | (new_ptr[2] << 8) |
                                    (new_ptr[3]));
                uint32 i = 0;
                int tar_len = 0;
                uint8* tar_ptr = (uint8*)alloc_stream_buffer(pbs, malloc_len, NON_STREAM);
                uint8* curr_ptr = tar_ptr;

                if (!tar_ptr || !segment_offset || !segment_len) {
                    free_stream_buffer(pbs, (char*)segment_offset, NON_STREAM);
                    free_stream_buffer(pbs, (char*)segment_len, NON_STREAM);
                    return -1;
                }
                MKV_LOG("find one rv packet: insert_header size: %d, pts: %lld, pos: %lld "
                        "frame_len=%d\r\n",
                        packet->size, packet->pts, packet->pos, frame_len);

                if ((int)frame_len > packet->size) {
                    MKV_ERROR_LOG("insert_header rv packet size=%d,frame len %d", packet->size,
                                  frame_len);
                    free_stream_buffer(pbs, (char*)tar_ptr, NON_STREAM);
                    free_stream_buffer(pbs, (char*)segment_offset, NON_STREAM);
                    free_stream_buffer(pbs, (char*)segment_len, NON_STREAM);
                    return -1;
                }

                for (i = 0; i < segment_number_bak; i++) {
                    segment_offset[i] =
                            ((new_ptr[20 + 8 * i + 4] << 24) | (new_ptr[20 + 8 * i + 5] << 16) |
                             (new_ptr[20 + 8 * i + 6] << 8) | (new_ptr[20 + 8 * i + 7]));
                }

                for (i = 0; i < segment_number_bak; i++) {
                    if (i == segment_number_bak - 1)
                        segment_len[i] = frame_len - segment_offset[i];
                    else
                        segment_len[i] = segment_offset[i + 1] - segment_offset[i];
                }

                set_rv_pic(curr_ptr, header_len, &stream->rvinfo);
                memcpy(curr_ptr + MALONE_PAYLOAD_HEADER_SIZE, new_ptr, header_len);
                curr_ptr += (header_len + MALONE_PAYLOAD_HEADER_SIZE);
                tar_len += (header_len + MALONE_PAYLOAD_HEADER_SIZE);
                new_ptr += header_len;

                for (i = 0; i < segment_number_bak; i++) {
                    set_rv_slice(curr_ptr, segment_len[i], &stream->rvinfo);
                    memcpy(curr_ptr + MALONE_PAYLOAD_HEADER_SIZE, new_ptr + segment_offset[i],
                           segment_len[i]);
                    curr_ptr += MALONE_PAYLOAD_HEADER_SIZE + segment_len[i];
                    tar_len += MALONE_PAYLOAD_HEADER_SIZE + segment_len[i];
                }

                free_stream_buffer(pbs, (char*)segment_offset, NON_STREAM);
                free_stream_buffer(pbs, (char*)segment_len, NON_STREAM);
                free_stream_buffer(pbs, (char*)packet->data, NON_STREAM);
                packet->data = tar_ptr;

                if (packet->size + MALONE_PAYLOAD_HEADER_SIZE + (int)segment_number_bak * 16 !=
                    tar_len) {
                    MKV_ERROR_LOG("insert_header size error size=%d,segment_number=%d,tar_len=%d",
                                  packet->size, segment_number_bak, tar_len);
                    free_stream_buffer(pbs, (char*)tar_ptr, NON_STREAM);
                    return -1;
                }
                packet->size = tar_len;
            }
        }
    } else if (stream->track_type ==
               MATROSKA_TRACK_TYPE_AUDIO) { /*
                                               if(stream->codec_type == MKV_AUDIO_AAC)
                                               {
                                                   packet->data -= 7;
                                                   ptr = packet->data;

                                                   CreateADTSHeaderInfoForAAC(ptr, packet->size,
                                               track->ainfo.sampling_frequency,
                                               track->ainfo.channels); packet->size += 7;
                                               }
                                               else if(stream->codec_type == MKV_AUDIO_AC3)
                                               {

                                                   }
                                               */
    }

    return 0;
}

#define CODEC_VERSION (0x5 << 24)  // FOURCC_WMV3_WMV
#define NUM_FRAMES 0xFFFFFF
#define SET_HDR_EXT 0x80000000
#define RCV_HEADER_LEN 24

int matroska_VC1_create_rcvheader(unsigned char* codec_priv, unsigned long priv_size,
                                  unsigned char* buffer, unsigned long buffer_size,
                                  unsigned long width, unsigned long height) {
    unsigned long HdrExtDataLen;
    unsigned int value = 0;
    int i = 0;
    (void)priv_size;

    // Number of Frames, Header Extension Bit, Codec Version
    value = NUM_FRAMES | SET_HDR_EXT | CODEC_VERSION;
    buffer[i++] = (unsigned char)value;
    buffer[i++] = (unsigned char)(value >> 8);
    buffer[i++] = (unsigned char)(value >> 16);
    buffer[i++] = (unsigned char)(value >> 24);

    // Header Extension Size
    // ASF Parser gives 5 bytes whereas the VPU expects only 4 bytes, so limiting it
    HdrExtDataLen = 4;
    buffer[i++] = (unsigned char)HdrExtDataLen;
    buffer[i++] = (unsigned char)(HdrExtDataLen >> 8);
    buffer[i++] = (unsigned char)(HdrExtDataLen >> 16);
    buffer[i++] = (unsigned char)(HdrExtDataLen >> 24);

    memcpy(buffer + i, codec_priv, HdrExtDataLen);
    i += HdrExtDataLen;

    // Height
    buffer[i++] = (unsigned char)height;
    buffer[i++] = (unsigned char)(((height >> 8) & 0xff));
    buffer[i++] = (unsigned char)(((height >> 16) & 0xff));
    buffer[i++] = (unsigned char)(((height >> 24) & 0xff));
    // Width
    buffer[i++] = (unsigned char)width;
    buffer[i++] = (unsigned char)(((width >> 8) & 0xff));
    buffer[i++] = (unsigned char)(((width >> 16) & 0xff));
    buffer[i++] = (unsigned char)(((width >> 24) & 0xff));
    // Frame Size
    buffer[i++] = (unsigned char)buffer_size;
    buffer[i++] = (unsigned char)(buffer_size >> 8);
    buffer[i++] = (unsigned char)(buffer_size >> 16);
    buffer[i++] = (unsigned char)((buffer_size >> 24) | 0x80);

    return 0;
}

#define READ16(x) (*(uint8*)x | (*((uint8*)x + 1) << 8))
static int matroska_add_packet(MKVReaderContext* pctx, mkv_packet* packet, mkvStream* stream);

static int matroska_add_codec_priv(MKVReaderContext* pctx, mkvStream* stream) {
    int offset = 0;
    uint8* new_pkt = NULL;
    uint8* codec_priv = NULL;
    ByteStream* pbs = NULL;

    if (!pctx || !stream)
        return -1;

    pbs = &pctx->bs;

    if (!stream->codec_extra_size)
        return 0;

    codec_priv = stream->codec_extra_data;
    if (!codec_priv)
        return -1;

    new_pkt = (uint8*)alloc_stream_buffer(pbs, stream->codec_extra_size + 64, NON_STREAM);
    if (!new_pkt)
        return -1;

    if (stream->track_type == MATROSKA_TRACK_TYPE_VIDEO) {
        if (stream->codec_type == MKV_VIDEO_H264 && !(pctx->flags & FLAG_H264_NO_CONVERT)) {
            uint8 i, pps_num = 0;
            uint16 sps_size, pps_size;
            uint32 start_code = 0x01000000;
            uint8 reserved = 0;
            codec_priv++;//version
            codec_priv++;//profile
            codec_priv++;//profile_compat
            codec_priv++;//level
            reserved = *codec_priv++;

            stream->h264_mark_size = 4;  // default marker size is 4 bytes
            if (reserved != 0xff) {
                stream->h264_mark_size = (reserved & 0x03) + 1;
            }
            reserved = *codec_priv++;
            if (reserved != 0xe1) {
                free_stream_buffer(pbs, (char*)new_pkt, NON_STREAM);
                return -1;
            }

            sps_size = READ16(codec_priv);
            SWAP_16(sps_size);
            codec_priv += 2;

            offset = 0;
            memcpy(new_pkt + offset, &start_code, 4);
            offset += 4;

            memcpy(new_pkt + offset, codec_priv, sps_size);
            codec_priv += sps_size;
            offset += sps_size;

            pps_num = *codec_priv++;

            for (i = 0; i < pps_num; i++) {
                pps_size = READ16(codec_priv);
                SWAP_16(pps_size);
                codec_priv += 2;

                memcpy(new_pkt + offset, &start_code, 4);
                offset += 4;

                memcpy(new_pkt + offset, codec_priv, pps_size);
                codec_priv += pps_size;
                offset += pps_size;
            }
        } else if (stream->codec_type >= MKV_REAL_VIDEO_RV10 &&
                   stream->codec_type <= MKV_REAL_VIDEO_RV40 &&
                   (pctx->flags & FLAG_VIDEO_INSERT_HEADER)) {
            offset = stream->codec_extra_size;
            stream->rvinfo.insert_header = TRUE;

            if (get_rv_info(codec_priv, stream->codec_extra_size, &stream->rvinfo) < 0)
                return -1;

            set_rv_seq(new_pkt, (uint32)offset, &stream->rvinfo);
            memcpy(new_pkt + MALONE_PAYLOAD_HEADER_SIZE, codec_priv, stream->codec_extra_size);
            offset += MALONE_PAYLOAD_HEADER_SIZE;
        } else {
            free_stream_buffer(pbs, (char*)new_pkt, NON_STREAM);
            return 0;
        }
    } else if (stream->track_type == MATROSKA_TRACK_TYPE_AUDIO) {
        if (stream->codec_type == MKV_AUDIO_VORBIS) {
            int newLen;
            if (codec_priv[0] != 2 || stream->codec_extra_size <= 3) {
                free_stream_buffer(pbs, (char*)new_pkt, NON_STREAM);
                return -1;
            }
            newLen = stream->codec_extra_size - 3;
            free_stream_buffer(pbs, (char*)new_pkt, NON_STREAM);
            new_pkt = (uint8*)alloc_stream_buffer(pbs, newLen, NON_STREAM);
            memcpy(new_pkt, codec_priv + 3, newLen);
            offset = newLen;
        } else {
            free_stream_buffer(pbs, (char*)new_pkt, NON_STREAM);
            return 0;
        }
    } else if (MATROSKA_TRACK_TYPE_SUBTITLE == stream->track_type) {
        free_stream_buffer(pbs, (char*)new_pkt, NON_STREAM);
        return 0;
    }

    free_stream_buffer(pbs, (char*)stream->codec_extra_data, NON_STREAM);
    stream->codec_extra_data = new_pkt;
    stream->codec_extra_size = offset;

    return 0;
}

static void matroska_merge_packets(MKVReaderContext* pctx, mkv_packet* out, mkv_packet* in,
                                   bool bText) {
#ifdef __WINCE
#define NEWLINE_LEN 2
#elif defined(WIN32)
#define NEWLINE_LEN 2
#else
#define NEWLINE_LEN 1
#endif

    int new_package_size;
    char* lineEnd;
    ByteStream* pbs = &pctx->bs;
    matroska_process_packet(pctx, in);

    new_package_size = out->size + in->size;

    if (bText)
        new_package_size += NEWLINE_LEN;

    out->data = (uint8*)realloc_stream_buffer(pbs, (char*)out->data, out->size, new_package_size,
                                              NON_STREAM);

    if (bText) {
        lineEnd = (char*)out->data + out->size;

        // for windows
        if (NEWLINE_LEN > 1) {
            *lineEnd = 0x0d;
            out->size += 1;
            lineEnd++;
        }
        *lineEnd = 0x0a;
        out->size += 1;
    }
    memcpy(out->data + out->size, in->data, in->size);
    out->size += in->size;

    matroska_destory_packet(pctx, in);

    free_stream_buffer(pbs, (char*)in, NON_STREAM);
}

static int matroska_add_packet(MKVReaderContext* pctx, mkv_packet* packet, mkvStream* stream) {
    int idx = 0;
    ByteStream* pbs = NULL;

    if (!pctx || !packet || !stream)
        return -1;

    pbs = &pctx->bs;

    matroska_process_packet(pctx, packet);

    INCREASE_BUFFER_COUNT(stream->packets, stream->num_packets, PACKET_PTR_INC_NUM, mkv_packet*);

    idx = stream->num_packets++;
    stream->packets_size += packet->size;

    stream->packets[idx] = packet;

    return 0;
}

static int matroska_parse_block(MKVReaderContext* pctx, uint8* data, int size, uint64 pos,
                                uint64 cluster_time, uint64 duration, int is_keyframe,
                                int64 cluster_pos, int64* pBlock_offset_ms, int block_num) {
    int res = 0;
    char* p = NULL;
    int16 block_time = 0;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;
    mkv_packet* packet = NULL;
    uint32* lace_size = NULL;
    ByteStream* pbs = &pctx->bs;
    uint64 timecode = MKV_NOPTS_VALUE;
    int n, flags, lace_num, vsize, track_num;
    bool provideBlockDuration = FALSE;
    int lace_type = 0;
    vsize = size;

    if (data == NULL) {
        if (MKV_INVALID_SEEK_POS == seek_stream_buffer(pbs, pos, 1)) {
            MKV_ERROR_LOG("matroska_parse_block : can not seek to block position %lld\n", pos);
            return -1;
        }

        p = read_non_stream_data(pbs, size);
        if (!p) {
            return -1;
        }

        data = (uint8*)p;
    } else {
        p = (char*)data;
    }

    track_num = (int)read_vint_val_size(data, &vsize);

    data += vsize;
    size -= vsize;

    track = matroska_find_track_by_num(pctx, track_num);
    if (size <= 3 || !track || !track->stream) {
        MKV_ERROR_LOG("matroska_parse_block : Invalid stream %d or size %u\n", track_num, size);
        res = -1;
        goto BLOCK_FINISHED;
    }

    stream = (mkvStream*)track->stream;
    if (!stream) {
        MKV_ERROR_LOG("matroska_parse_block : Invalid stream %d\n", track_num);
        res = -1;
        goto BLOCK_FINISHED;
    }

    if (!stream->track_enable) {
        res = 0;  // skip this track without any error
        MKV_ERROR_LOG("matroska_parse_block : track_enable false\n");
        goto BLOCK_FINISHED;
    }

    if (duration != MKV_NOPTS_VALUE) {
        provideBlockDuration = TRUE;
    }

    if (duration == MKV_NOPTS_VALUE && pctx->sctx.info.time_code_scale)
        duration = track->default_duration / pctx->sctx.info.time_code_scale;

    block_time = (data[0] << 8) | data[1];

#ifdef SUPPORT_PART_CLUSTER
    //*pBlock_duration=duration;    //in generally, it is zero, so it is useless
    *pBlock_offset_ms = (int64)block_time * pctx->sctx.info.time_code_scale / 1000000;  //=> ms ??
#endif

    data += 2;
    flags = *data++;
    size -= 3;
    if (is_keyframe == -1)
        is_keyframe = flags & 0x80 ? PKT_FLAG_KEY : 0;

    if (cluster_time != (uint64)-1 && (block_time >= 0 || cluster_time >= (uint64)(-block_time))) {
        timecode = cluster_time + block_time;

        if (track->track_type == MATROSKA_TRACK_TYPE_VIDEO ||
            track->track_type == MATROSKA_TRACK_TYPE_AUDIO) {
            if (timecode < stream->last_seek_pos && pctx->direction == FLAG_FORWARD) {
                MKV_LOG("skip block(track type %d): timecode: %lld, last seek pos: %lld  \r\n",
                        track->track_type, timecode, stream->last_seek_pos);
                goto BLOCK_FINISHED;
            }
        }

        if (track->track_type == MATROSKA_TRACK_TYPE_SUBTITLE && timecode < track->end_timecode)
            is_keyframe = 0;
        if (is_keyframe) {
            int ret = 0;
            MKV_LOG("parser one keyframe (type: %d): pos: %lld, timecode: %lld \r\n",
                    track->track_type, cluster_pos, timecode);
            ret = mkv_add_index_entry(pctx, stream, (uint64)cluster_pos, timecode, block_num,
                                      MKV_KEY_FRAME_FLAG, TRUE);
            if (ret == 0)
                mkv_index_quick_sort(stream);
        }

        track->end_timecode = max(track->end_timecode, timecode + duration);
    }

    if (pctx->skip_to_keyframe) {
        if (is_keyframe && track->track_type == MATROSKA_TRACK_TYPE_VIDEO) {
            pctx->skip_to_keyframe = 0;
        } else {
            MKV_LOG("skip to key block \r\n");
            goto BLOCK_FINISHED;
        }

    } else if (pctx->skip_to_timecode > 0) {
        if (timecode < pctx->skip_to_timecode) {
            MKV_LOG("skip_to_timecode \r\n");
            goto BLOCK_FINISHED;
        }
        pctx->skip_to_timecode = 0;
    }

    lace_type = (flags & 0x06) >> 1;

    if (lace_type == 0x0) {
        lace_num = 1;
        lace_size = (uint32*)alloc_stream_buffer(pbs, sizeof(uint32), NON_STREAM);
        lace_size[0] = size;
    } else if (lace_type >= 0x1 && lace_type <= 0x3) {
        lace_num = (uint8)(*data) + 1;
        data += 1;
        size -= 1;
        lace_size = (uint32*)alloc_stream_buffer(pbs, lace_num * sizeof(uint32), NON_STREAM);
        memset(lace_size, 0, lace_num * sizeof(uint32));
    }

    switch (lace_type) {
        case 0x1: {
            uint8 temp;
            uint32 total_size = 0;
            n = 0;
            while (res == 0 && n < lace_num - 1) {
                do {
                    if (size == 0) {
                        res = -1;
                        break;
                    }
                    temp = *data;
                    lace_size[n] += temp;
                    data += 1;
                    size -= 1;
                    if (temp != 0xff)
                        break;
                } while (1);
                total_size += lace_size[n];
                n++;
            }
            lace_size[n] = size - total_size;
            break;
        }
        case 0x2: {
            for (n = 0; n < lace_num; n++) {
                lace_size[n] = size / lace_num;
            }
            break;
        }
        case 0x3: {
            uint32 total_size;
            int64 temp_num;

            vsize = size;
            n = (int)read_vint_val_size(data, &vsize);
            if (vsize < 0) {
                MKV_ERROR_LOG("matroska_parse_block : wrong lace size\n");
                break;
            }
            total_size = n;
            data += vsize;
            size -= vsize;
            lace_size[0] = total_size;
            n = 1;
            while (res == 0 && n < lace_num - 1) {
                vsize = size;

                temp_num = read_svint_val_size(data, &vsize);

                if (vsize < 0) {
                    MKV_ERROR_LOG("matroska_parse_block : wrong lace size< 0\n");
                    break;
                }
                data += vsize;
                size -= vsize;
                lace_size[n] = (int32)temp_num + lace_size[n - 1];
                total_size += lace_size[n];
                n++;
            }
            lace_size[n] = size - total_size;
            break;
        }
    }

    if (res == 0) {
        for (n = 0; n < lace_num; n++) {
            if (stream->codec_type == MKV_REAL_AUDIO_288 ||
                stream->codec_type == MKV_REAL_AUDIO_COOK ||
                stream->codec_type == MKV_REAL_AUDIO_ATRC) {
                int a = stream->rainfo.block_align;
                int sps = stream->rainfo.sub_packet_size;
                int cfs = stream->rainfo.coded_framesize;
                int h = stream->rainfo.sub_packet_h;
                int y = stream->rainfo.sub_packet_cnt;
                int w = stream->rainfo.frame_size;
                int x;
                uint8* audio_buf = stream->rainfo.audio_buf;

                if (!stream->rainfo.pkt_cnt) {
                    if (stream->codec_type == MKV_REAL_AUDIO_288) {
                        for (x = 0; x < h / 2; x++)
                            memcpy(audio_buf + x * 2 * w + y * cfs, data + x * cfs, cfs);
                    } else {
                        for (x = 0; x < w / sps; x++)
                            memcpy(audio_buf + sps * (h * x + ((h + 1) / 2) * (y & 1) + (y >> 1)),
                                   data + x * sps, sps);
                    }

                    if (++stream->rainfo.sub_packet_cnt >= h) {
                        stream->rainfo.pkt_cnt = (uint16)(h * w / a);
                        stream->rainfo.sub_packet_cnt = 0;
                    }
                }

                while (stream->rainfo.pkt_cnt) {
                    packet = (mkv_packet*)alloc_stream_buffer(pbs, sizeof(mkv_packet), NON_STREAM);

                    if (matroska_new_packet(pctx, packet, a) < 0) {
                        MKV_ERROR_LOG("matroska_parse_cluster : matroska_new_packet failed\n");
                        free_stream_buffer(pbs, (char*)packet, NON_STREAM);
                        res = -1;
                        break;
                    }

                    memcpy(packet->data, audio_buf + a * (h * w / a - stream->rainfo.pkt_cnt--), a);

                    packet->pos = pos;
                    packet->stream_index = stream->stream_index;

                    packet->pts = timecode;
                    packet->pos = pos;
                    MKV_LOG("add one audio packet: size: %d, pts: %lld, pos: %lld \r\n",
                            packet->size, packet->pts, packet->pos);
                    matroska_add_packet(pctx, packet, stream);
                }
            } else {
                ContentEncodingInfo* encodings = track->content_encoding_list;
                int offset = 0, pkt_size = lace_size[n];
                uint8* pkt_data = data;

                if (encodings && encodings->content_encoding_scope & 0x1) {
                    offset = matroska_decode_buffer(pctx, &pkt_data, &pkt_size, track);
                    if (offset < 0)
                        continue;
                }

                packet = (mkv_packet*)alloc_stream_buffer(pbs, sizeof(mkv_packet), NON_STREAM);

                if (matroska_new_packet(pctx, packet, pkt_size + offset) < 0) {
                    MKV_ERROR_LOG("matroska_parse_cluster : matroska_new_packet failed\n");
                    free_stream_buffer(pbs, (char*)packet, NON_STREAM);
                    res = -1;
                    break;
                }

                //        packet->data += 7;//preserve 7 bytes for ADTS header

                if (offset)
                    memcpy(packet->data,
                           encodings->compression.content_compression_settings.binary_ptr, offset);
                memcpy(packet->data + offset, pkt_data, pkt_size);

                if (pkt_data != data)
                    free_stream_buffer(pbs, (char*)pkt_data, NON_STREAM);

                if (n == 0)
                    packet->flags = is_keyframe;
                packet->stream_index = stream->stream_index;

                packet->pts = timecode;
                packet->pos = pos;
                MKV_LOG("add one packet before: size: %d, pts: %lld, pos: %lld \r\n", packet->size,
                        packet->pts, packet->pos);

                if (track->track_type == MATROSKA_TRACK_TYPE_SUBTITLE &&
                    stream->codec_type == MKV_SUBTITLE_TEXT)
                    packet->duration = duration;
                else if (track->track_type != MATROSKA_TRACK_TYPE_SUBTITLE)
                    packet->duration = duration;

                if (track->track_type == MATROSKA_TRACK_TYPE_SUBTITLE &&
                    ((stream->codec_type == MKV_SUBTITLE_SSA) ||
                     (stream->codec_type == MKV_SUBTITLE_ASS)))
                    matroska_fix_ass_packet(pctx, packet, duration);

                if (pctx->prev_pkt && timecode != MKV_NOPTS_VALUE &&
                    pctx->prev_pkt->pts == timecode &&
                    stream->codec_type >= MKV_VIDEO_TYPE_UNKNOWN &&
                    stream->codec_type != MKV_VIDEO_VP8 && stream->codec_type != MKV_VIDEO_VP9 &&
                    pctx->prev_pkt->stream_index == stream->stream_index) {
                    bool bText = FALSE;
                    MKV_LOG("merge one video packet: size: %d, pts: %lld, pos: %lld \r\n",
                            packet->size, packet->pts, packet->pos);

                    if (track->track_type == MATROSKA_TRACK_TYPE_SUBTITLE &&
                        stream->codec_type == MKV_SUBTITLE_TEXT)
                        bText = TRUE;

                    matroska_merge_packets(pctx, pctx->prev_pkt, packet, bText);
                } else {
                    MKV_LOG("add one packet: size: %d, pts: %lld, pos: %lld \r\n", packet->size,
                            packet->pts, packet->pos);
                    matroska_add_packet(pctx, packet, stream);
                    pctx->prev_pkt = packet;
                    if (stream->packets_size > MAX_STREAM_PACKAGE_SIZE) {
                        MKV_ERROR_LOG("matroska_parse_block packets_size > 100M");
                        pctx->done = 1;
                        res = -1;
                        break;
                    }
                }
            }

            if (duration != MKV_NOPTS_VALUE) {
                uint64 ts_padding = 1;
                if (stream->codec_type == MKV_AUDIO_VORBIS)
                    ts_padding = 0;
                else if (stream->codec_type == MKV_AUDIO_AAC)
                    ts_padding = 0;

                if (provideBlockDuration) {
                    timecode = duration ? timecode + duration / lace_num : timecode + ts_padding;
                } else {
                    timecode = duration ? timecode + duration : timecode + ts_padding;
                }
            }
            data += lace_size[n];
        }
    }

    free_stream_buffer(pbs, (char*)lace_size, NON_STREAM);

BLOCK_FINISHED:

    if (p)
        free_stream_buffer(pbs, p, NON_STREAM);

    return res;
}

int matroska_parse_cluster(MKVReaderContext* pctx, bool fileMode) {
    ebml_info einfo;
    uint64 offset = 0;
    int64 cluster_pos;
    int is_keyframe = 0;
    int64 i, bytes, retval;
    ByteStream* pbs = NULL;
    ClusterEntry* cluster = NULL;
    int64 block_offset = 0;  // time(ms) related to current cluster

    if (!pctx)
        return -1;

    pbs = &pctx->bs;
    cluster = pctx->sctx.cluster_list;
    MKV_LOG("matroska_parse_cluster cluster cnt: %d \r\n", pctx->sctx.cluster_count);

    if (cluster && (1 == pctx->sctx.cluster_count)) {
        uint64 pos;
        pos = get_stream_position(pbs);
        if (pos != (uint64)cluster->cluster_cur_file_pos) {
            // seek operation occur !!!!we need to discard and free current cluster
            MKV_LOG("seek occur: free current cluster pos=%lld,cluster_cur_file_pos=%lld", pos,
                    cluster->cluster_cur_file_pos);
            goto FREE_CLUSTER;
        } else {
            if (1 == cluster->cluster_not_finished) {
                MKV_LOG("continue last cluster \r\n");
                goto CLUSTER_LOOP;
            }
        }
    }

    cluster_pos = offset = get_stream_position(pbs);
    MKV_LOG("stream offset : %lld\r\n", offset);
    bytes = read_next_ebml_info(pbs, &einfo);
    if (!bytes) {
        MKV_ERROR_LOG("matroska_parse_cluster : EOS detected\n");
        pctx->done = 1;
        return 0;
    }

    switch (einfo.s_id) {
        case MATROSKA_ID_CLUSTER:
            // similar process with read_segment_cluster_entry()
            {
                uint64 off;
                int64 length;

                pbs = &pctx->bs;
                MKV_LOG("matroska_parse_cluster MATROSKA_ID_CLUSTER \n");

                INCREASE_BUFFER_COUNT(pctx->sctx.cluster_list, pctx->sctx.cluster_count,
                                      CLUSTER_ENTRY_INC_NUM, ClusterEntry);
                ASSERT(0 == pctx->sctx.cluster_count);
                cluster = &pctx->sctx.cluster_list[pctx->sctx.cluster_count++];
                cluster->timecode = 0;
                cluster->group_count = 0;
                cluster->group_list = NULL;
                cluster->simple_block_count = 0;
                cluster->simple_block_list = NULL;

                cluster->cluster_not_finished = 0;
                cluster->cluster_left_length = (int)einfo.s_size;
                cluster->cluster_next_blk_offset = offset + einfo.s_offset;
                cluster->cluster_time_off_ms = 0;
                /*backup some variables*/
                cluster->cluster_s_off = einfo.s_offset;
                cluster->cluster_s_size = einfo.s_size;
                cluster->cluster_offset = offset;

                if (fileMode)
                    cluster->simple_block_count_next = 0;

            CLUSTER_LOOP:
                /*avoid missing init value introduced by goto statement*/
                ASSERT(cluster == pctx->sctx.cluster_list);
                length = cluster->cluster_left_length;
                off = cluster->cluster_next_blk_offset;
                /*restore backup variables*/
                einfo.s_offset = cluster->cluster_s_off;
                einfo.s_size = cluster->cluster_s_size;
                offset = cluster->cluster_offset;
                cluster_pos = offset;
                while (length > 0) {
                    int blk;
                    ebml_info ebmlinfo;
                    BinaryData* pbdata = NULL;
                    int ref_timecode;
#if 0            
                bytes = read_stream_ebml_info(pbs, off, &ebmlinfo);
#else
                    // we must re-seek stream !!! since read_non_stream_data() may effect
                    // read_stream_data()
                    if (MKV_INVALID_SEEK_POS == seek_stream_buffer(pbs, off, 1)) {
                        MKV_ERROR_LOG("seek failure: off: 0x%llX \r\n", off);
                        goto CLUSTER_FAIL;
                    }
                    bytes = read_next_ebml_info(pbs, &ebmlinfo);
#endif
                    MKV_LOG("CLUSTER_LOOP einfo.s_id : 0x%llx bytes=%lld, length=%lld\r\n",
                            ebmlinfo.s_id, bytes, length);

                    if (!bytes || length < bytes) {
                        MKV_ERROR_LOG(
                                "read_segment_cluster_entry : read_stream_ebml_info failed .\n");
                        goto CLUSTER_FAIL;  // return -1;
                    }

                    block_offset = 0;
                    switch (ebmlinfo.s_id) {
                        case MATROSKA_ID_CLUSTERTIMECODE:

                            cluster->timecode =
                                    read_uint_value(ebmlinfo.s_data, (int)ebmlinfo.s_size);
                            MKV_LOG("cluster time: %lld \r\n", cluster->timecode);
                            break;
                        case MATROSKA_ID_CLUSTERPOSITION:

                            cluster->position = (unsigned int)read_uint_value(ebmlinfo.s_data,
                                                                              (int)ebmlinfo.s_size);

                            break;
                        case MATROSKA_ID_CLUSTERPREVSIZE:

                            cluster->prevsize = (unsigned int)read_uint_value(ebmlinfo.s_data,
                                                                              (int)ebmlinfo.s_size);

                            break;
                        case MATROSKA_ID_BLOCKGROUP:
                            ref_timecode = -1;
                            is_keyframe = 0;  // for block group, it is 0 or 1.
                            read_segment_cluster_blockgroup(pctx, off + ebmlinfo.s_offset,
                                                            (int)ebmlinfo.s_size, &ref_timecode);

                            /*parser block*/

                            blk = cluster->group_count - 1;

                            if (NULL == cluster->group_list)
                                break;
                            if (cluster->group_list[blk].block.binary_size > 0) {
                                MKV_LOG("parse block group: off: 0x%llX, size: %d \r\n",
                                        cluster->group_list[blk].block.binary_offset,
                                        cluster->group_list[blk].block.binary_size);
                                if ((ref_timecode == 0) || (ref_timecode == -1)) {
                                    //(1) no MATROSKA_ID_BLOCKREFERENCE or (2) value is set to 0
                                    is_keyframe = 1;
                                }
                                retval = matroska_parse_block(
                                        pctx, (uint8*)cluster->group_list[blk].block.binary_ptr,
                                        cluster->group_list[blk].block.binary_size,
                                        cluster->group_list[blk].block.binary_offset,
                                        cluster->timecode, cluster->group_list[blk].block_duration,
                                        is_keyframe, cluster_pos, &block_offset, blk + 1);
                                // reset block buffer pointer as buffer is release at
                                // matroska_parse_block()
                                cluster->group_list[blk].block.binary_ptr = NULL;
                                if (retval == -1) {
                                    MKV_LOG("parse block group: ret= -1");
                                    goto CLUSTER_FAIL;
                                }
                            }
                            break;
                        case MATROSKA_ID_SIMPLEBLOCK:

                            INCREASE_BUFFER_COUNT(cluster->simple_block_list,
                                                  cluster->simple_block_count, SIMPLE_BLOCK_INC_NUM,
                                                  BinaryData);

                            pbdata = &cluster->simple_block_list[cluster->simple_block_count++];

                            pbdata->binary_ptr = NULL;
                            pbdata->binary_size = (int)ebmlinfo.s_size;
                            pbdata->binary_offset = off + ebmlinfo.s_offset;

                            /*parser block*/
                            blk = cluster->simple_block_count - 1;
                            if (cluster->simple_block_list[blk].binary_size > 0) {
                                MKV_LOG("parse simple block : off: 0x%llX, size: %d \r\n",
                                        pbdata->binary_offset, pbdata->binary_size);
                                is_keyframe = -1;  // for simple block: it is -1
                                retval = matroska_parse_block(
                                        pctx, (uint8*)pbdata->binary_ptr, pbdata->binary_size,
                                        pbdata->binary_offset, cluster->timecode, MKV_NOPTS_VALUE,
                                        is_keyframe, cluster_pos, &block_offset, blk + 1);
                                if (retval == -1) {
                                    MKV_LOG("parse simple block: ret= -1");
                                    goto CLUSTER_FAIL;
                                }
                            }
                            break;
                        case FSL_EBML_ID_VOID:
                        case FSL_EBML_ID_CRC32:
                            break;

                        default:
                            MKV_ERROR_LOG("read_segment_cluster_entry : unhandled ID 0x%llX \n",
                                          ebmlinfo.s_id);
                            break;
                    }

                    length -= bytes;
                    off += bytes;
#if 0  // move to above
                //we must re-seek stream !!! since read_non_stream_data() may effect read_stream_data()
                if(MKV_INVALID_SEEK_POS == seek_stream_buffer(pbs, off, 1))
                {
                    MKV_ERROR_LOG("seek failure: off: 0x%llX \r\n",off); 
                    goto CLUSTER_FAIL;
                }
#endif
                    /*check whether return to improve performance*/
                    if (!fileMode) {
                        if (block_offset - cluster->cluster_time_off_ms > MAX_CLUSTER_UNIT_MS) {
                            MKV_LOG("cluster is not finished: off_ms: %lld, blk_off: %lld, length: "
                                    "%lld, off: 0x%llX \r\n",
                                    cluster->cluster_time_off_ms, block_offset, length, off);
                            cluster->cluster_not_finished = 1;
                            cluster->cluster_left_length = length;
                            cluster->cluster_next_blk_offset = off;
                            cluster->cluster_time_off_ms = block_offset;
                            cluster->cluster_cur_file_pos = get_stream_position(pbs);
                            return 0;
                        }
                    } else {
                        if (block_offset != cluster->cluster_time_off_ms) {
                            MKV_LOG("cluster is not finished: off_ms: %lld, blk_off: %lld, length: "
                                    "%lld, off: 0x%llX \r\n",
                                    cluster->cluster_time_off_ms, block_offset, length, off);
                            cluster->cluster_not_finished = 1;
                            cluster->cluster_left_length = length;
                            cluster->cluster_next_blk_offset = off;
                            cluster->cluster_time_off_ms = block_offset;
                            cluster->simple_block_count_next = cluster->simple_block_count;
                            cluster->cluster_cur_file_pos = get_stream_position(pbs);
                            return 0;
                        }
                    }
                }

                cluster->cluster_not_finished = 0;
                MKV_LOG("one cluster is finished \r\n");
                goto CLUSTER_OK;  // return 0;
            }

        CLUSTER_FAIL: {
            MKV_ERROR_LOG("matroska_parse_cluster : read_segment_cluster_entry failed\n");
#ifdef SUPPORT_CORRUPT_CLUSTER
            pctx->sctx.continuous_bad_cluster_cnt++;
            MKV_LOG("continuous_bad_cluster_cnt: %d \r\n", pctx->sctx.continuous_bad_cluster_cnt);
            if (pctx->sctx.continuous_bad_cluster_cnt <= MAX_CONTINUOUS_BAD_CLUSTER) {
                MKV_LOG("CLUSTER_FAIL embl offset=%lld,size=%lld,id=%llx", einfo.s_offset,
                        einfo.s_size, einfo.s_id);
                // don't set done=1 and continue next cluster
                offset += (uint64)(einfo.s_offset + einfo.s_size);

                MKV_LOG("CLUSTER_FAIL offset is %lld", offset);
                goto FINISH_CLUSTER;
            }
#endif
            pctx->done = 1;
            return 0;
        }

        CLUSTER_OK:
#ifdef SUPPORT_CORRUPT_CLUSTER
            // current cluster is correct, clear the bad flag
            pctx->sctx.continuous_bad_cluster_cnt = 0;
            MKV_LOG("continuous_bad_cluster_cnt: %d \r\n", pctx->sctx.continuous_bad_cluster_cnt);
#endif
            break;

        case MATROSKA_ID_SEGMENT:

            pctx->segment_start = offset;
            pctx->segm_header_size = bytes;
            pctx->segm_master_size = einfo.s_offset;
            pctx->sctx.first_cluster_pos = 0;

            MKV_LOG("MATROSKA_ID_SEGMENT offset=%lld length=%lld \n", offset,
                    bytes - einfo.s_offset);
            if (0 == read_segment_master_header(pctx, offset, bytes - einfo.s_offset)) {
                mkv_packet* packet = NULL;
                mkvStream* stream = NULL;

                clear_matroska_stream_list(pctx);

                matroska_file_update_track(pctx);
                offset = pctx->sctx.first_cluster_pos;

                for (i = 0; i < pctx->stream_count; i++) {
                    stream = &pctx->stream_list[i];

                    // enable track by default
                    stream->force_enable = 1;
                    stream->track_enable = 1;

                    if (stream->codec_extra_data == NULL || stream->codec_extra_size == 0)
                        continue;

                    packet = (mkv_packet*)alloc_stream_buffer(pbs, sizeof(mkv_packet), NON_STREAM);

                    if (matroska_new_packet(pctx, packet, stream->codec_extra_size) < 0) {
                        MKV_ERROR_LOG("matroska_parse_cluster : matroska_new_packet failed\n");
                        free_stream_buffer(pbs, (char*)packet, NON_STREAM);
                        continue;
                    }

                    memcpy(packet->data, stream->codec_extra_data, stream->codec_extra_size);

                    packet->pos = 0;
                    packet->pts = 0;
                    packet->stream_index = stream->stream_index;
                    packet->flags = PKT_FLAG_CODEC_DATA;
                    MKV_LOG("add PKT_FLAG_CODEC_DATA package \n");
                    matroska_add_packet(pctx, packet, stream);
                }

                MKV_LOG("MATROSKA_ID_SEGMENT cluster_pos=%llx \n", cluster_pos);
                goto FINISH_CLUSTER;
            }

            break;
        case MATROSKA_ID_INFO:

            retval = read_segment_id_info(pctx, offset + einfo.s_offset, (int)einfo.s_size);
            if (retval == -1) {
                MKV_ERROR_LOG("matroska_parse_cluster : read_segment_id_info failed\n");
                pctx->done = 1;
                return 0;
            }

            break;
        case MATROSKA_ID_CUES:
            if (!fileMode)
                break;

            clear_matroska_cue_list(pctx);

            MKV_TIMER_START();
            retval = read_segment_cuepoint_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);
            MKV_TIMER_STOP("read cuepoint");
            if (retval == -1) {
                pctx->done = 1;
                return 0;
            }

            MKV_LOG("parse cluster cuepoints: %d \r\n", pctx->sctx.cue_count);

            break;

        case MATROSKA_ID_TAGS:
            if (!fileMode)
                break;

            retval = read_segment_tag_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);
            if (retval == -1) {
                MKV_ERROR_LOG("matroska_parse_cluster : read_segment_tag_entry failed\n");
                pctx->done = 1;
                return 0;
            }

            break;

        case MATROSKA_ID_SEEKHEAD:
            if (!fileMode)
                break;

            if (pctx->sctx.seek_count > 0) {
                if (pctx->sctx.seek_list)
                    free_stream_buffer(&pctx->bs, (char*)pctx->sctx.seek_list, NON_STREAM);
                pctx->sctx.seek_list = NULL;
                pctx->sctx.seek_count = 0;
                MKV_LOG("free MATROSKA_ID_SEEKHEAD");
            }

            retval = read_segment_seek_entry(pctx, offset + einfo.s_offset, (int)einfo.s_size);
            if (retval == -1) {
                MKV_ERROR_LOG("matroska_parse_cluster : read_segment_seek_entry failed\n");
                pctx->done = 1;
                return 0;
            }

            matroska_execute_seekhead(pctx);

            break;
        case MATROSKA_ID_TRACKS:
            MKV_LOG("MATROSKA_ID_TRACKS \n");
            break;
        case FSL_EBML_ID_VOID:
        case FSL_EBML_ID_CRC32:
            break;
        case FSL_EBML_ID_HEADER:
            MKV_LOG("parse cluster FSL_EBML_ID_HEADER offset=%llx,einfo.s_offset=%llx,bytes=%llx, "
                    "einfo.s_size=%llx \n",
                    offset, einfo.s_offset, bytes, einfo.s_size);
            pctx->ebml_header_size = bytes;
            pctx->ebml_master_size = einfo.s_offset;

            pctx->ectx.ebml_version = FSL_EBML_VERSION;
            pctx->ectx.ebml_reader_version = FSL_EBML_VERSION;

            pctx->ectx.ebml_id_maxlen = 4;
            pctx->ectx.ebml_size_maxlen = 8;
            pctx->ectx.ebml_doctype_version = 1;
            pctx->ectx.ebml_doctype_reader_version = 1;

            break;

        default:

            MKV_ERROR_LOG("matroska_parse_cluster : invalid ebml ID 0x%X!\n", (int)einfo.s_id);
            pctx->done = 1;
            return 0;

            break;
    }

    offset += (uint64)(einfo.s_offset + einfo.s_size);

    if (!pctx->sctx.cluster_count || !pctx->sctx.cluster_list) {
        MKV_ERROR_LOG("matroska_parse_cluster : no cluster found\n");
        seek_stream_buffer(pbs, offset, 1);
        return 0;
    }

#if defined(SUPPORT_CORRUPT_CLUSTER)
FINISH_CLUSTER:
#endif

    seek_stream_buffer(pbs, offset, 1);

FREE_CLUSTER:
    for (i = 0; i < pctx->sctx.cluster_count; i++)
        close_segment_cluster_entry(pctx, &pctx->sctx.cluster_list[i]);

    if (pctx->sctx.cluster_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.cluster_list, NON_STREAM);
    pctx->sctx.cluster_list = NULL;
    pctx->sctx.cluster_count = 0;

    return 0;
}

int matroska_get_packet_size(MKVReaderContext* pctx, uint32 tracknum) {
    int retval = 0;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    if (!stream->track_enable)
        return -1;

    if (stream->stream_index != tracknum)
        return -1;

    if (stream->has_ra_info)
        return stream->rainfo.sub_packet_size;

    while (!stream->num_packets) {
        if (pctx->done)
            return 1;

        retval = matroska_parse_cluster(pctx, FALSE);

        if (retval == -1)
            return -1;
    }

    return stream->packets[0]->size;
}

int matroska_get_packet(MKVReaderContext* pctx, mkv_packet* packet, uint32 tracknum) {
    int retval = 0;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!pctx || !packet)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    if (!stream->track_enable)
        return -1;

    while (matroska_deliver_packet(pctx, packet, tracknum)) {
        if (pctx->done)
            return 1;

        if (stream->track_type == MATROSKA_TRACK_TYPE_SUBTITLE)
            return PARSER_NOT_READY;

        MKV_TIMER_START();
        retval = matroska_parse_cluster(pctx, FALSE);
        MKV_TIMER_STOP("parse cluster");

        if (retval == -1)
            return -1;
    }

    return 0;
}

int matroska_get_next_packet_from_cluster(MKVReaderContext* pctx, uint32* tracknum) {
    int retval = 0;
    uint32 i;
    TrackEntry* track;

    if (!pctx || !tracknum)
        return -1;
CLSUTER_LOOP:
    if (pctx->done)
        return 1;  // PARSER_EOS

    for (i = 1; i <= (uint32)pctx->stream_count; i++) {
        track = matroska_find_track_by_num(pctx, i);
        retval = matroska_deliver_packet(pctx, &track->packet, i);
        if (retval == 0) {
            *tracknum = i;
            return 0;
        }
    }

    MKV_TIMER_START();
    retval = matroska_parse_cluster(pctx, TRUE);
    MKV_TIMER_STOP("parse cluster");

    if (retval != -1)
        goto CLSUTER_LOOP;

    return -1;
}

TrackExtTagList* matroska_create_track_ext_taglist(ByteStream* pbs) {
    TrackExtTagList* out = NULL;
    if (pbs == NULL)
        return NULL;
    out = (TrackExtTagList*)alloc_stream_buffer(pbs, sizeof(TrackExtTagList), NON_STREAM);
    if (out) {
        out->num = 0;
        out->m_ptr = NULL;
        return out;
    } else
        return NULL;
}

int matroska_add_track_ext_tag(ByteStream* pbs, TrackExtTagList* list, uint32 index, uint32 type,
                               uint32 size, uint8* data) {
    TrackExtTagItem* curr = NULL;
    TrackExtTagItem* temp = NULL;
    if (pbs == NULL || list == NULL || data == NULL)
        return -1;

    temp = (TrackExtTagItem*)alloc_stream_buffer(pbs, sizeof(TrackExtTagItem), NON_STREAM);
    if (temp == NULL)
        return -1;

    temp->index = index;
    temp->type = type;
    temp->size = size;
    temp->data = data;
    temp->nextItemPtr = NULL;

    if (list->m_ptr == NULL) {
        list->m_ptr = temp;
    } else {
        curr = list->m_ptr;
        while (curr != NULL && curr->nextItemPtr != NULL) {
            curr = curr->nextItemPtr;
        }
        curr->nextItemPtr = temp;
    }

    list->num++;

    return 0;
}

int matroska_delete_track_ext_taglist(ByteStream* pbs, TrackExtTagList* list) {
    TrackExtTagItem* curr = NULL;
    TrackExtTagItem* temp = NULL;
    if (pbs == NULL || list == NULL)
        return -1;

    curr = list->m_ptr;
    while (curr != NULL) {
        temp = curr;
        curr = curr->nextItemPtr;
        free_stream_buffer(pbs, (char*)temp, NON_STREAM);
        list->num--;
    }

    free_stream_buffer(pbs, (char*)list, NON_STREAM);
    return 0;
}

int matroska_get_track_ext_taglist(MKVReaderContext* pctx, uint32 tracknum,
                                   TrackExtTagList** list) {
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;
    if (!pctx || !list)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    *list = stream->ext_tag_list;
    return 0;
}

int32 matroska_get_video_color_info(MKVReaderContext* pctx, uint32 tracknum, int32* primaries,
                                    int32* transfer, int32* coeff, int32* fullRange) {
    TrackEntry* track = NULL;
    if (!pctx || !primaries || !transfer || !coeff || !fullRange)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    if (track->track_type != MATROSKA_TRACK_TYPE_VIDEO)
        return -1;

    if (!track->colorPtr)
        return -1;

    *primaries = (int32)track->colorPtr->Primaries;
    *transfer = (int32)track->colorPtr->TransferCharacteristics;
    *coeff = (int32)track->colorPtr->MatrixCoefficients;

    if (track->colorPtr->Range == 2)
        *fullRange = 1;
    else
        *fullRange = 0;

    return 0;
}

int32 matroska_get_video_hdr_color_info(MKVReaderContext* pctx, uint32 tracknum,
                                        VideoHDRColorInfo* pInfo) {
    TrackEntry* track = NULL;
    if (!pctx || !pInfo)
        return -1;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;

    if (track->track_type != MATROSKA_TRACK_TYPE_VIDEO)
        return -1;

    if (!track->colorPtr)
        return -1;

    pInfo->maxCLL = track->colorPtr->MaxCLL;
    pInfo->maxFALL = (int32)track->colorPtr->MaxFALL;

    if (track->colorMetadataPtr == NULL) {
        pInfo->hasMasteringMetadata = 0;
    } else {
        pInfo->hasMasteringMetadata = 1;
        pInfo->PrimaryRChromaticityX = track->colorMetadataPtr->PrimaryRChromaticityX;
        pInfo->PrimaryRChromaticityY = track->colorMetadataPtr->PrimaryRChromaticityY;
        pInfo->PrimaryGChromaticityX = track->colorMetadataPtr->PrimaryGChromaticityX;
        pInfo->PrimaryGChromaticityY = track->colorMetadataPtr->PrimaryGChromaticityY;
        pInfo->PrimaryBChromaticityX = track->colorMetadataPtr->PrimaryBChromaticityX;
        pInfo->PrimaryBChromaticityY = track->colorMetadataPtr->PrimaryBChromaticityY;
        pInfo->WhitePointChromaticityX = track->colorMetadataPtr->WhitePointChromaticityX;
        pInfo->WhitePointChromaticityY = track->colorMetadataPtr->WhitePointChromaticityY;
        pInfo->LuminanceMax = track->colorMetadataPtr->LuminanceMax;
        pInfo->LuminanceMin = track->colorMetadataPtr->LuminanceMin;
    }

    return 0;
}

int matroska_find_cluster(MKVReaderContext* pctx, uint64 target_ts) {
    int index = -1;
    int i, j, k;
    uint64 ts = 0;

    if (pctx->sctx.prescan_cluster_totalcnt == 0 || pctx->sctx.prescan_cluster_index_list == NULL)
        return index;

    i = 0;
    j = pctx->sctx.prescan_cluster_totalcnt;

    // for no cue list seek, find the cluster which ts < target_ts
    while (i < j) {
        k = i + (j - i) / 2;
        if (k > j)
            break;
        ts = pctx->sctx.prescan_cluster_index_list[k].timecode;
        MKV_LOG("matroska_find_cluster timecode=%lld,target_ts=%lld", (long long)ts,
                (long long)target_ts);
        if (ts <= target_ts)
            i = k + 1;
        else
            j = k;

        if (i > j)
            break;
    }

    if (i != j || i > pctx->sctx.prescan_cluster_totalcnt || i <= 0) {
        MKV_ERROR_LOG("matroska_find_cluster error");
    }

    index = i - 1;
    if (pctx->sctx.prescan_cluster_index_list[index].timecode > target_ts) {
        MKV_ERROR_LOG("matroska_find_cluster error");
    }

    return index;
}

int matroska_parser_flush_track(MKVReaderContext* pctx) {
    ByteStream* pbs = NULL;
    int i;

    if (!pctx)
        return -1;

    MKV_LOG("matroska_parser_flush_track \n");

    pbs = &pctx->bs;

    if (!clear_stream_cache(pbs))
        return -1;

    for (i = 0; i < pctx->sctx.cluster_count; i++)
        close_segment_cluster_entry(pctx, &pctx->sctx.cluster_list[i]);

    if (pctx->sctx.cluster_list)
        free_stream_buffer(&pctx->bs, (char*)pctx->sctx.cluster_list, NON_STREAM);
    pctx->sctx.cluster_list = NULL;
    pctx->sctx.cluster_count = 0;
    MKV_LOG("matroska_parser_clear_cache success pctx->sctx.cluster_count=%d\n",
            pctx->sctx.cluster_count);
    seek_stream_buffer(pbs, 0, 1);

    return 0;
}

void matroska_check_codec_data(MKVReaderContext* pctx, mkvStream* stream) {
    if (!pctx || !stream)
        return;

    if (stream->codec_extra_size != 0 && stream->codec_extra_data)
        return;

    if (stream->track_type == MATROSKA_TRACK_TYPE_VIDEO) {
        switch (stream->codec_type) {
            case MKV_VIDEO_MPEG2:
            case MKV_VIDEO_MPEG4:
            case MKV_VIDEO_VP9:
                stream->get_codecdata_from_frame = 1;
                break;
            default:
                break;
        }
    }

    return;
}

int matroska_get_codec_data_from_frame(MKVReaderContext* pctx) {
    int track_count = 0;
    int i;
    mkvStream* stream = NULL;
    TrackEntry* track = NULL;
    ByteStream* pbs = NULL;
    bool enable = FALSE;
    uint64 before_pos;
    if (!pctx)
        return -1;

    track_count = pctx->sctx.track_count;

    for (i = 0; i < track_count; i++) {
        track = &pctx->sctx.track_list[i];
        if (!track)
            continue;
        stream = (mkvStream*)track->stream;
        if (!stream)
            continue;
        if (stream->get_codecdata_from_frame) {
            enable = TRUE;
            stream->force_enable = 1;
            stream->track_enable = 1;
        }
    }

    if (!enable)
        return 0;

    pbs = &pctx->bs;
    before_pos = get_stream_position(pbs);

    seek_stream_buffer(pbs, pctx->sctx.first_cluster_pos, 1);

    for (i = 0; i < track_count; i++) {
        track = &pctx->sctx.track_list[i];
        stream = (mkvStream*)track->stream;

        if (stream->get_codecdata_from_frame && stream->force_enable && stream->track_enable) {
            MKV_LOG("get_codecdata_from_frame\n");
            int ret = matroska_get_packet(pctx, &track->packet, track->track_num);
            if (ret == 0) {
                matroska_parse_codec_data_from_frame(pctx, &track->packet, stream);
                stream->get_codecdata_from_frame = 0;
            }

            matroska_free_packet(pctx, &track->packet);
            stream->force_enable = 0;
            stream->track_enable = 0;
        }
    }

    matroska_clear_queue_all(pctx);
    seek_stream_buffer(pbs, before_pos, 1);
    return 0;
}

int matroska_parse_codec_data_from_frame(MKVReaderContext* pctx, mkv_packet* packet,
                                         mkvStream* stream) {
    if (!pctx || !packet || !stream)
        return -1;

    if (!stream->get_codecdata_from_frame || stream->codec_extra_size != 0 ||
        stream->codec_extra_data) {
        return -1;
    }

    switch (stream->codec_type) {
        case MKV_VIDEO_VP9:
            matroska_add_VP9_codec_data(pctx, packet, stream);
            break;
        default:
            break;
    }
    return 0;
}

int matroska_add_VP9_codec_data(MKVReaderContext* pctx, mkv_packet* packet, mkvStream* stream) {
    BitReader br;
    uint32 version;
    uint32 high;
    uint32 profile;
    uint32 show_existing_frame;
    uint32 frame_type;
    uint32 show_frame;
    uint32 error_resilient_mode;
    uint32 bit_depth = 8;
    int32 subsampling = 0;
    uint32 csd_size = 6;
    ByteStream* pbs = NULL;
    uint8* new_buf = NULL;

    if (!pctx || !packet || !stream)
        return -1;

    pbs = &pctx->bs;

    BitReaderInit(&br, packet->data, packet->size);
    if (br.getBits(&br, 2) != 0b10)
        return -1;

    version = br.getBits(&br, 1);
    high = br.getBits(&br, 1);
    profile = (high << 1) + version;

    if (profile == 3) {
        if (0 != br.getBits(&br, 1))
            return -1;
        if (packet->size < 2)
            return -1;
    }

    show_existing_frame = br.getBits(&br, 1);
    if (show_existing_frame)
        return -1;

    if (br.numBitsLeft(&br) < 3)
        return -1;

    frame_type = br.getBits(&br, 1);
    show_frame = br.getBits(&br, 1);
    error_resilient_mode = br.getBits(&br, 1);

    // key frame's type is 0
    if (frame_type == 0) {
        if (br.numBitsLeft(&br) < 24)
            return -1;
        if (br.getBits(&br, 24) != 0x498342)
            return -1;
        if (0 != matroska_get_VP9_colorconfig(&br, profile, &bit_depth, &subsampling)) {
            return -1;
        }
    } else {
        uint32 intra_only = 0;
        if (!show_frame)
            intra_only = br.getBits(&br, 1);

        if (!error_resilient_mode && !br.skipBits(&br, 2))
            return -1;

        if (!intra_only)
            return -1;

        if (br.numBitsLeft(&br) < 24) {
            return -1;
        }
        if (br.getBits(&br, 24) != 0x498342)
            return -1;

        if (profile > 0 &&
            0 != matroska_get_VP9_colorconfig(&br, profile, &bit_depth, &subsampling)) {
            return -1;
        } else {
            subsampling = 3;
        }
    }

    if (subsampling > 0)
        csd_size += 3;

    new_buf = (uint8*)alloc_stream_buffer(pbs, csd_size, NON_STREAM);

    if (!new_buf)
        return -1;

    stream->codec_extra_data = new_buf;
    stream->codec_extra_size = csd_size;

    MKV_LOG("add codec data size=%d,buf=%p\n", csd_size, new_buf);

    // https://www.webmproject.org/docs/container/#vp9-codec-feature-metadata-codecprivate
    // Profile: ID = 1, Len = 1
    *new_buf = 0x01;
    new_buf++;
    *new_buf = 0x01;
    new_buf++;
    *new_buf = (uint8)profile;
    new_buf++;

    // Bit Depth: ID = 3, Len = 1
    *new_buf = 0x03;
    new_buf++;
    *new_buf = 0x01;
    new_buf++;
    *new_buf = (uint8)bit_depth;
    new_buf++;

    if (subsampling > 0) {
        // Chroma Subsampling: ID = 4, Len = 1
        *new_buf = 0x04;
        new_buf++;
        *new_buf = 0x01;
        new_buf++;
        *new_buf = (uint8)subsampling;
    }

    return 0;
}

int matroska_get_VP9_colorconfig(BitReader* bits, uint32 profile, uint32* bit_depth,
                                 int32* subsampling) {
    uint32 subsampling_x = 0;
    uint32 subsampling_y = 0;

    if (!bits || !bit_depth || !subsampling)
        return -1;

    if (bits->numBitsLeft(bits) < 1)
        return -1;

    if (profile >= 2)
        *bit_depth = bits->getBits(bits, 1) ? 12 : 10;
    else
        *bit_depth = 8;

    if (bits->numBitsLeft(bits) < 3)
        return -1;

    uint32 colorspace = bits->getBits(bits, 3);
    if (colorspace != 7 /*SRGB*/) {
        bits->getBits(bits, 1);
        if (profile == 1 || profile == 3) {
            if (bits->numBitsLeft(bits) < 2)
                return -1;

            subsampling_x = bits->getBits(bits, 1);
            subsampling_y = bits->getBits(bits, 1);
            *subsampling = (subsampling_x << 1) + subsampling_y;
        } else {
            *subsampling = 0x3;
        }
    } else if (profile == 1 || profile == 3) {
        *subsampling = 0;
    }

    return 0;
}
