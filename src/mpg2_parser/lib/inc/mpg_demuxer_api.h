/*
 ***********************************************************************
 * Copyright (c) 2005-2015, Freescale Semiconductor, Inc.
 *
 * Copyright 2017-2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */
/*****************************************************************************
 * mpg_demuxer_api.h
 * Description:
 * MPEG demuxer API header file.
 *
 *
 ****************************************************************************/

#ifndef MPG_DEMUXER_API_H
#define MPG_DEMUXER_API_H

#include "fsl_datatype.h"
#include "mpeg2_epson_exvi.h"
#include "mpeg2_parser_api.h"
#include "outputarray_manage.h"

#define NO_PROGRAM_SUPPORT_MAX 64
#define MAX_PACKET_FRAMES 32

#define UNREGISTERED_STREAM (-1)

/*defines for flag in callback function :DemuxOutput  */
#define FSL_MPG_DEMUX_PTS_VALID ((U32)(1 << 13))
#define FSL_MPG_DEMUX_DTS_VALID ((U32)(1 << 14))
#define FSL_MPG_DEMUX_NEW_PES ((U32)(1 << 15))
// #define FSL_MPG_PTS_CALCED                ((U32)(1<<12))

// #define FSL_MPG_MPEG2_FrameSTART  ((U32)(1<<16))
#define LANGUAGE_MAX_LEN (8)

typedef enum {
    FSL_MPG_DEMUX_NOMEDIA = 0,
    FSL_MPG_DEMUX_AUDIO_STREAM,
    FSL_MPG_DEMUX_VIDEO_STREAM,
    FSL_MPG_DEMUX_UNKNOWN_MEDIA
} FSL_MPG_DEMUX_MEDIA_TYPE_T;

typedef enum {
    FSL_MPEG_A_CH_MODE_STEREO = 0,
    FSL_MPEG_A_CH_MODE_JOINTSTEREO,
    FSL_MPEG_A_CH_MODE_DUALCHANNEL,
    FSL_MPEG_A_CH_MODE_SINGLECHANNEL,
    FSL_MPEG_A_CH_MODE_UNKNOWN
} FSL_MPG_DEMUX_CHANNEL_MODE_T;

typedef enum {
    FSL_MPG_DEMUX_NOVIDEO = 0,
    FSL_MPG_DEMUX_MPEG2_VIDEO,
    FSL_MPG_DEMUX_H264_VIDEO,
    FSL_MPG_DEMUX_MP4_VIDEO,
    FSL_MPG_DEMUX_HEVC_VIDEO,
    FSL_MPG_DEMUX_AVS_VIDEO,
    FSL_MPG_DEMUX_UNKNOWN_VIDEO
} FSL_MPG_DEMUX_VIDEO_TYPE_T;

typedef enum {
    FSL_MPG_DEMUX_NOAUDIO = 0,
    FSL_MPG_DEMUX_MP1_AUDIO,
    FSL_MPG_DEMUX_MP2_AUDIO,
    FSL_MPG_DEMUX_MP3_AUDIO,
    FSL_MPG_DEMUX_AAC_AUDIO,
    FSL_MPG_DEMUX_AC3_AUDIO,
    FSL_MPG_DEMUX_AC4_AUDIO,
    FSL_MPG_DEMUX_EAC3_AUDIO,
    FSL_MPG_DEMUX_DTS_AUDIO,
    FSL_MPG_DEMUX_DTS_HD_AUDIO,
    FSL_MPG_DEMUX_DTS_UHD_AUDIO,
    FSL_MPG_DEMUX_PCM_AUDIO,
    FSL_MPG_DEMUX_UNKNOWN_AUDIO
} FSL_MPG_DEMUX_AUDIO_TYPE_T;

typedef enum {
    FSL_MPG_DEMUX_ER_BSAC = 1,
    FSL_MPG_DEMUX_AAC_RAW = 2,
    FSL_MPG_DEMUX_AAC_ADTS = 3,
    FSL_MPG_DEMUX_AAC_ADIF = 4
} FSL_MPG_DEMUX_AAC_SUB_TYPE_T;

typedef enum {
    FSL_MPG_DEMUX_MP4_START = 0,
    FSL_MPG_DEMUX_MP4_VOS,
    FSL_MPG_DEMUX_MP4_VOP
} FSL_MPG_DEMUX_MP4_STATE_T;

typedef struct FSL_VIDEO_PROPERTY_S {
    FSL_MPG_DEMUX_VIDEO_TYPE_T enuVideoType;
    U32 uliVideoWidth;
    U32 uliVideoHeight;
    U32 uliVideoBitRate;
    U32 uliFRNumerator;
    U32 uliFRDenominator;
    U32 uliScanType;
} FSL_VIDEO_PROPERTY_T;

typedef struct FSL_AUDIO_PROPERTY_S {
    FSL_MPG_DEMUX_AUDIO_TYPE_T enuAudioType;
    U32 enuAudioSubType;
    U32 uliAudioSampleRate;
    U16 usiAudioChannels;
    FSL_MPG_DEMUX_CHANNEL_MODE_T enuAudioChannelMode;
    U32 uliAudioBitRate;
    U16 usiAudioBitsPerSample;
    U8 subStreamID;
} FSL_AUDIO_PROPERTY_T;

// this framebuffer is defined for framedetection.
#define TFRAMEBUFFER_SIZE (10 + 4)

typedef struct FSL_MPEGSTREAM_S {
    FSL_MPG_DEMUX_MEDIA_TYPE_T enuStreamType;
    U32 uliPropertyValid;
    U32 streamId;
    U32 streamPID;
    U32 isEnabled;
    U64 fileOffset;
    U32 isBlocked;
    U32 isFirstAfterSeek;
    U32 prevSampleSize;     /*size of the sample just read, for CBR stream trick mode */
    U8* frameBuffer;        // buffer to contain one frame of data
    U32 frameBufferLength;  // buffer length
    U32 frameBufferFilled;  // Filled buffer Length

    U8 tFrameBuffer[TFRAMEBUFFER_SIZE];
    U32 tFrameBufferFilled;
    U64 tFrameBufferSync;
    U32 frameStartPos[MAX_PACKET_FRAMES];  // How many buffer starts in the framebuffer and the
                                           // positions
    U32 frameTypes[MAX_PACKET_FRAMES];
    U32 frameStartCounts;
    U64 frameBufferPTS;   // PTS of the current FrameBuffer
    U64 currentPTS;       // PTS of the current PES
    U32 frameBufferFlag;  // is PTS of the frameBufferValid
    U32 sampleBytesLeft;  // bytes of current sample data not output yet, with the possible padding
                          // byte.
    U32 payloadInsert;

    U32 lastSampleOffset;  // the offset of last sample
    U32 lastFourBytes;     // The last fourbytes of last sample
    U32 isLastSampleSync;

    FSL_MPG_DEMUX_MP4_STATE_T lastMP4State;

    U32 filedCounter;
    U32 lastFiledType;
    U64 lastPTS;    // the last valid PTS
    U64 cachedPTS;  // cahcedPTS of last PES
    U64 startTime;
    U64 endTime;
    U64 usDuration;
    U32 scanForDuration;

    OutputBufArray outputArray;

    U8* codecSpecInformation;
    U32 codecSpecInfoSize;
    U32 isSyncFinished;
    U64 lastSyncPosition;
    U64 lastFramePosition;
    U32 lastFrameFlag;
    EPSON_EXVI EXVI;
    void* pParser;
    void* curr_bufContext;

    union {
        FSL_VIDEO_PROPERTY_T VideoProperty;
        FSL_AUDIO_PROPERTY_T AudioProperty;
    } MediaProperty;

    U32 isSPSfindAfterSeek; /*H.264 SPS header*/
    U32 frameNumPre;        /*record previous frame num*/
    U32 streamNum;
    int has_frame_buffer;
    bool isNoFrameBoundary;
    bool isAudioPresentationChanged;
    U64 maxPTSDelta;
} FSL_MPEGSTREAM_T;

typedef struct STREAM_BUFFER_S {
    U32 PID;
    U8* pBuf;
    U32 nSize;
    U32 nPESNum;
} STREAM_BUFFER_T;

typedef struct FSL_MPG_DEMUX_SYSINFO_S {
    U32 uliNoStreams;
    U32 uliPSFlag;
    FSL_MPEGSTREAM_T Stream[MAX_MPEG2_STREAMS];
    STREAM_BUFFER_T TempStreamBuf;

} FSL_MPG_DEMUX_SYSINFO_T;

typedef struct FSL_MPG_DEMUX_PSI_S {
    U8 IsTS;
    U8 Programs;
    U16 ProgramNumber[NO_PROGRAM_SUPPORT_MAX];

} FSL_MPG_DEMUX_PSI_T;

#define SUPPORT_SEEK(type)                                                       \
    (FSL_MPG_DEMUX_MPEG2_VIDEO == (type) || FSL_MPG_DEMUX_MP4_VIDEO == (type) || \
     FSL_MPG_DEMUX_H264_VIDEO == (type) || FSL_MPG_DEMUX_HEVC_VIDEO == (type))

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif /*MPG_DEMUXER_API_H   */
