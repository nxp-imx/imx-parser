/************************************************************************
 * Copyright 2005-2016, Freescale Semiconductor, Inc.
 * Copyright 2017-2018, 2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ************************************************************************/

#ifndef FSL_PARSER_UNIT_TEST_H
#define FSL_PARSER_UNIT_TEST_H

#include "fsl_parser_drm.h"

/*--------------------------------- Version Infomation --------------------------------*/
#define SEPARATOR " "

#define BASELINE_SHORT_NAME "PARSER_TEST_02.00.00"

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

#if defined(__WINCE)
// #define DBGMSG(cond, fmt, ...) DEBUGMSG(cond, _T(fmt), __VA_ARGS__))
#define DBGMSG(fmt, ...) DEBUGMSG(1, (_T(fmt), __VA_ARGS__))

#elif defined(WIN32)
#define DBGMSG(fmt, ...) DEBUGMSG(1, (fmt, __VA_ARGS__))

#else /* linux platfom */
#ifdef DEBUG
#define DBGMSG printf
#else
#define DBGMSG(fmt...)
#endif
#endif

/*===============================================================================
  Constants & Types
  =============================================================================*/

#define MAX_FILE_PATH_LENGTH 255

#define CONFIG_OPTION "-c"
#define DUMP_TRACK_DATA "dump-track-data"
#define DUMP_TRACK_PTS "dump-track-pts"
#define EXPORT_INDEX_TABLE "export-index"
#define IMPORT_AVAILABLE_INDEX_TABLE "import_availble_index"
#define IS_LIVE_SOURCE "is-live" /* size of the string includes the '\0' */
#define CLIP_NAME "clip-name"
#define CLIP_LIST "clip-list"
#define USER_COMMAND_LIST "command-list"
#define FIRST_CLIP_NUMBER "first-clip-number"
#define LAST_CLIP_NUMBER "last-clip-number"
#define PARSER_LIB_PATH "parser_lib_path"
#define PARSER_LIB_PATH_A "parser_lib_path_a"
#define PARSER_LIB_PATH_B "parser_lib_path_b"
#define MD5_FILE_NAME_A "./md5sum_a.txt"
#define MD5_FILE_NAME_B "./md5sum_b.txt"
#define STREAM_INFO_FILE_NAME "./stream_info.txt"
#define REGRESSION_TEST "regression_test"
#define REGRESSION_TEST_LOG "regression_test_log.txt"

#define MAX_PTS_GITTER_IN_MS "max-pts-gitter-in-ms"

#define VIDEO_QUEUE_MAX_SIZE_BYTES "video-queue-max-size-bytes"
#define VIDEO_QUEUE_MAX_SIZE_BUFFERS "video-queue-max-size-buffers"
#define VIDEO_QUEUE_SINGLE_BUFFER_SIZE "video-queue-single-buffer-size"

#define AUDIO_QUEUE_MAX_SIZE_BYTES "audio-queue-max-size-bytes"
#define AUDIO_QUEUE_MAX_SIZE_BUFFERS "audio-queue-max-size-buffers"
#define AUDIO_QUEUE_SINGLE_BUFFER_SIZE "audio-queue-single-buffer-size"

#define TEXT_QUEUE_MAX_SIZE_BYTES "text-queue-max-size-bytes"
#define TEXT_QUEUE_MAX_SIZE_BUFFERS "text-queue-max-size-buffers"
#define TEXT_QUEUE_SINGLE_BUFFER_SIZE "text-queue-single-buffer-size"

#define HELP_DELIMITER "\t\t\t"

#define MAX_MEDIA_TRACKS 24

#define NAL_START_CODE_SIZE 4

#define STREAMING_BYTE_RATE (10 * 1024 * 1024)  // 5000 /* in bytes */
#define STREAMING_FAR_SEEK_THRESHOLD (10 * 1024 * 1024)

#define DEFAULT_AV_SYNC_THRSHOLD_IN_US (5000 * 1000)

/* default queue settings */
#define DEFAULT_DECODING_DELAY_IN_US (500 * 1000) /* 500 ms */

#define DEFAULT_VIDEO_QUEUE_DEPTH (10 * 1024 * 1024)
#define DEFAULT_AUDIO_QUEUE_DEPTH (500 * 1024)
#define DEFAULT_AUDIO_DEEP_QUEUE_DEPTH (1 * 1024 * 1024) /* PCM & DTS more easy to exhaust queue \
                                                          */
#define DEFAULT_TEXT_QUEUE_DEPTH (400 * 1024)
#define DEFAULT_UNKNOWN_QUEUE_DEPTH (100 * 1024)

/* default sample buffer size */
#define MAX_VIDEO_FRAME_SIZE (1024 * 1024)
#define MAX_NON_VIDEO_SAMPLE_SIZE (32 * 1024)

#define H264_NAL_OVERHEADER_SIZE (4 * 100) /* 4 bytes for NAL start code */

#define BAILWITHERROR(v) \
    {                    \
        err = (v);       \
        goto bail;       \
    }

typedef struct {
    uint32 maxSizeBytes; /* Gstreamer style */

    uint32 maxSizeBuffers; /* DShow style */
    uint32 singleBufferSize;

} tQueueSettings;

typedef struct {
    /* creation & deletion */
    FslParserVersionInfo getVersionInfo;
    FslCreateParser createParser;
    FslDeleteParser deleteParser;
    FslCreateParser2 createParser2;

    /* index export/import */
    FslParserInitializeIndex initializeIndex;
    FslParserImportIndex importIndex;
    FslParserExportIndex exportIndex;

    /* movie properties */
    FslParserIsSeekable isSeekable;
    FslParserGetMovieDuration getMovieDuration;
    FslParserGetUserData getUserData;
    FslParserGetMetaData getMetaData;

    FslParserGetNumTracks getNumTracks;

    FslParserGetNumPrograms getNumPrograms;
    FslParserGetProgramTracks getProgramTracks;

    /* generic track properties */
    FslParserGetTrackType getTrackType;
    FslParserGetTrackDuration getTrackDuration;
    FslParserGetLanguage getLanguage;
    FslParserGetBitRate getBitRate;
    FslParserGetDecSpecificInfo getDecoderSpecificInfo;
    FslParserGetTrackExtTag getTrackExtTag;
    FslParserGetPCR getPCR;

    /* video properties */
    FslParserGetVideoFrameWidth getVideoFrameWidth;
    FslParserGetVideoFrameHeight getVideoFrameHeight;
    FslParserGetVideoFrameRate getVideoFrameRate;
    FslParserGetVideoFrameRotation getVideoFrameRotation;
    FslParserGetVideoColorInfo getVideoColorInfo;
    FslParserGetVideoHDRColorInfo getVideoHDRColorInfo;
    FslParserGetVideoScanType getVideoScanType;
    FslParserGetVideoDisplayWidth getVideoDisplayWidth;
    FslParserGetVideoDisplayHeight getVideoDisplayHeight;
    FslParserGetVideoFrameCount getVideoFrameCount;
    FslParserGetVideoThumbnailTime getVideoThumbnailTime;

    /* audio properties */
    FslParserGetAudioNumChannels getAudioNumChannels;
    FslParserGetAudioSampleRate getAudioSampleRate;
    FslParserGetAudioBitsPerSample getAudioBitsPerSample;
    FslParserGetAudioBlockAlign getAudioBlockAlign;
    FslParserGetAudioChannelMask getAudioChannelMask;
    FslParserGetAudioBitsPerFrame getAudioBitsPerFrame;
    FslParserGetAudioPresentationNum getAudioPresentationNum;
    FslParserGetAudioPresentationInfo getAudioPresentationInfo;
    FslParserGetAudioMpeghInfo getAudioMpeghInfo;

    /* text/subtitle properties */
    FslParserGetTextTrackWidth getTextTrackWidth;
    FslParserGetTextTrackHeight getTextTrackHeight;
    FslParserGetTextTrackMime getTextTrackMime;

    /* sample reading, seek & trick mode */
    FslParserGetReadMode getReadMode;
    FslParserSetReadMode setReadMode;

    FslParserEnableTrack enableTrack;

    FslParserGetNextSample getNextSample;
    FslParserGetNextSyncSample getNextSyncSample;

    FslParserGetFileNextSample getFileNextSample;
    FslParserGetFileNextSyncSample getFileNextSyncSample;
    FslParserGetSampleCryptoInfo getSampleCryptoInfo;
    FslParserGetSampleInfo getSampleInfo;
    FslParserGetImageInfo getImageInfo;

    FslParserSeek seek;

    /* DRM function list */
    FslParserIsDRMProtected isDRMProtected;
    FslParserQueryDRMContentUsage queryDRMContenyUsage;
    FslParserQueryDRMOutputProtectionFlag queryDRMOutputProtectionFlag;
    FslParserDRMCommitPlayback DRMCommitPlayback;
    FslParserDRMFinalizePlayback DRMFinalizePlayback;

    /* flush track */
    FslParserFlush flushTrack;
} FslParserInterface;

typedef struct {
    /* for multi-thread */
    FslParserInterface* IParser;
    FslParserHandle parserHandle;

    char dump_file_path[255]; /* file path to dump this track's data */
    FILE* fp_track_dump;       /* file to dump track data */

    char pts_dump_file_path[255]; /* file path to dump this track's data */
    FILE* fp_track_pts_dump;       /* file to dump track pts */

    uint32 trackNum;
    bool seekable;

    uint32 mediaType;
    uint32 decoderType;
    uint32 decoderSubtype;

    uint32 bitrate; /* bitrate for CBR stream, 0 for VBR stream */
    uint64 usDuration;

    uint8* decoderSpecificInfo;
    uint32 decoderSpecificInfoSize;

    bool isMpeg2Video; /*Mpeg2 Video will not have mono-PTS check*/

    bool isH264Video; /* H.264 video, need to add NAL start code */
    uint32 NALLengthFieldSize;

    bool isAAC; /* AAC audio except BSAC, need to prepend ADTS header */
    sAACConfig aacConfig;
    uint8 ADTSHeader[ADTS_HEADER_SIZE];

    bool isBSAC;
    uint32 firstBSACTrackNum;

    bool isMKVText;
    bool enabled;

    /* buffer to dump data */
    HANDLE hQueue;
    tQueueSettings queueSettings;

    uint8* sampleBuffer; /* buffer to hold the whole sample, accumulate data from the data buffer */
    uint32 sampleBufferSize;

    uint8* NALWorkingBuffer; /* only for H264 video track */
    uint32 NALWorkingBufferSize;

    /* play counter */
    bool firstSampleDataGot; /* first sample of a new segement is got */
    bool isNewSample;

    uint64 usSegmentStartTime; /* this segment start time is this track's matched seeking time.
                                  Not same for all tracks.*/
    // uint64 usMovieSegmentStartTime; /* true unified movie segment start time, moved to playState
    // */

    bool eos;         /*whether this track reach EOS or BOS ? */
    uint64 byteCount; /* for trick mode test, how many bytes read totally */
    uint32 sampleCount;
    uint64 lastPts;     /* Previous PTS adding the duration, for selecting track. Unkown PTS are
                           overlooked.*/
    uint64 lastPtsPure; /* Previous PTS without adding the duration. Unkown PTS are overlooked. */
    uint32 ptsRepeatCount;
    uint32 ptsNoMonoCount;

    uint32 sampleSize;
    uint32 sampleBufferOffset;

    uint64 usSampleTimeStamp;   /* pts of current sample */
    uint64 usCurSampleDuration; /* not only for DivX subtitle now */

} Track;

typedef struct {
    bool isRunning;
    int32 errCode; /* err code of core paser, from the parser task */

    FslParserInterface* IParser;
    FslParserHandle parserHandle;

    uint32 numTracks;
    Track* tracks;
    uint32 readMode;

    int32 playRate; /* 1 for normal playback, >1 for fast forword, < 0 for rewind, 0 for halting! */
    uint64 usMovieSegmentStartTime; /* true unified movie segment start time. ie. segment end time
                                       for rewinding. */
    uint64 usMovieStopTime;         /*Play will stop when reach this timestamp*/
    HANDLE hPlayMutex; /* to protext rate, rate may be modified without pausing the task */

} PlayState, *PlayStatePtr;

enum {
    TEST_STATE_NULL = 0,
    TEST_STATE_CREATING_PARSER,
    TEST_STATE_PLAYING
}; /* test state for a clip */

typedef enum {
    TEST_SEQUENCE_A,
    TEST_SEQUENCE_B,
} TEST_SEQUENCE;

#endif /* FSL_PARSER_UNIT_TEST_H */
