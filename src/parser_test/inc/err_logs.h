/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef FSL_PARSER_ERR_LOG_H
#define FSL_PARSER_ERR_LOG_H

#include <stdarg.h>
#include <stdio.h>
#include "fsl_types.h"

/*
// potenatil error issues, not reflected in final error code
#define MAX_ERROR_MESSAGE_LENGTH 255
#define ERR_LOG_FILE_NAME "err_parser_test.log"
#define MAX_TRACK_DURATION_GAP_IN_US (3*1000*1000)
#define MIN_VALID_DURATION_IN_US (3*1000*1000)
#define MAX_VALID_DURATION_IN_US (20*60*60*1000*1000LL) //20 hours

#define MAX_SEEK_TIME_ERROR_IN_US (10*1000*1000) //10 seconds. If user just change speed without
seeking,may exceeds this value, considerring key frame interval #define MAX_PTS_REPEAT_COUNT 3
#define MAX_PTS_NON_MONO_CHNAGE_COUNT 0 // not allow non-mono pts change by default

#define MAX_UNEXPECTED_BACKWARD_FILE_SEEK_COUNT 1
#define MAX_FILE_READ_FAILURE_COUNT 5
#define MAX_FILE_SEEK_ERROR 8


// potenatil risk flags
#define  RISK_FAIL_LOAD_INDEX           1
#define  RISK_NOT_SEEKABLE              (1 << 1)
#define  RISK_INVALID_MOVIE_DURATION    (1 << 2)
#define  RISK_INVALID_TRACK_DURATION    (1 << 3)

#define  RISK_BAD_AV_SYNC               (1 << 4)
#define  RISK_ERR_CONCEALED             (1 << 5)
#define  RISK_DEEP_INTERLEAVING         (1 << 6)

#define  RISK_INVALID_AUDIO_BIT_DEPTH    (1 << 7) // bit depth 0 is common
#define  RISK_INVALID_SAMPLE_DURATION    (1 << 8)

#define  RISK_UNEXPECTED_BACKWARD_FILE_SEEK     (1 << 9)
#define  RISK_TOO_MANY_FILE_READ_ERROR          (1 << 10)
#define  RISK_TOO_MANY_FILE_SEEK_ERROR          (1 << 11)

#define  RISK_PTS_CHANGE_IN_ONE_SAMPLE   (1 << 15) //PTS change in one sample
#define  RISK_INVALID_PTS_AFTER_SEEK     (1 << 16)
#define  RISK_NON_MONO_PTS_CHANGE        (1 << 17) // PTS does not change monotonically
#define  RISK_PTS_REPEAT                 (1 << 18) //too many pts repeat, usually means dead loop
#define  RISK_PREROLL_FAIL               (1 << 19)

#define  RISK_QUEUE_OVERFLOW        (1 << 20)
#define  RISK_FAKE_QUEUE_OVERFLOW   (1 << 21)


#define  RISK_REQUEST_ZERO_SIZE_OUTPUT_BUFFER   (1 << 24)
#define  RISK_ALLOC_ZERO_SIZE_MEMORY            (1 << 25)



typedef struct
{
    uint32 errFlags;

    // related value
    int32 errOnLoadIndex;

    uint64 usMovieDuration;  // movie duration

    uint32 trackNumOfBadDuration; //invalid track duration & number
    uint64 usBadTrackDuration;

    uint32 trackNumInvalidSampleDuration;
    uint64 usInvalidSampleDuration;

    uint32 trackNumBadAVSync;
    uint64 usTrackLastPTSBadAVSync;
    uint64 usBaseTrackLastPTS;

    uint32 trackNumOfInvalidPTSAfterSeek; // after seek, 1st pts is invalid comparing with the
segment start time uint64 usInvalidPTSAfterSeek; uint64 usSegmentStartTime; int32
playRateInvalidPTSAfterSeek;

    uint32 trackNumNonMonoPTS; // non-mono pts change at some rate
    int32  playRateNonMonoPTS;
    uint64  usMaxPTSGitter;

    uint32 trackNumPTSRepeat;
    uint64 usPTSRepeated;
    uint64 playRatePTSRepeat;

    uint32 trackNumPtsChangeWithinSample;
    uint32 sampleNumPtsChangeWithinSample;
    uint64 usOriPtsOneSample;
    uint64 usNewPtsOneSample;

    uint32 trackNumPrerollFail;
    int32  playRatePrerollFail;
    uint64 usTrackLastPTS;
    uint64 usMovieSegmenStartTime;

    uint32 trackNumQueueOverflow;
    int32  playRateQueueOverflow;
    uint64 usSegmentStartTimeQueueOverflow;

    uint32 badAudioBitDepth; // invalid audio bits per sample

    uint32 unexpectedBackwardFileSeekCount; // unexpected backward file seek counter
    uint32 fileReadErrCount; //file read error counter
    uint32 fileSeekErrCount; //file seek error counter

}MovieErrStatistics;

extern MovieErrStatistics g_movieErrStatistics;
*/

#define PARSER_LEVEL_ERROR 1
#define PARSER_LEVEL_WARNNING 2
#define PARSER_LEVEL_DEBUG 4
#define PARSER_LEVEL_INFO 8

#define PARSER_INFO_SEEK 1
#define PARSER_INFO_PTS 2
#define PARSER_INFO_DATASIZE 4
#define PARSER_INFO_FRAMESIZE 8
#define PARSER_INFO_BUFFER 16
#define PARSER_INFO_STREAM 32
#define PARSER_INFO_ERRORCONSEAL 64
#define PARSER_INFO_FILE 128
#define PARSER_INFO_PERF 256

#define PARSER_ERROR_ON 1
#define PARSER_WARNNING_ON 0
#define PARSER_DEBUG_ON 0
#define PARSER_INFO_ON 1

#define PARSER_INFO_SEEK_ON 0
#define PARSER_INFO_PTS_ON 0
#define PARSER_INFO_DATASIZE_ON 0
#define PARSER_INFO_FRAMESIZE_ON 0
#define PARSER_INFO_BUFFER_ON 0
#define PARSER_INFO_STREAM_ON 1
#define PARSER_INFO_ERRORCONSEAL_ON 0
#define PARSER_INFO_FILE_ON 0
#define PARSER_INFO_PERF_ON 0

typedef struct {
    uint32 level;
    uint32 type;
} LOG_CONTROL;

void parser_initlogstatus();
#ifndef __WINCE
void parser_printf(uint32 level, uint32 type, const char* fmt, ...);
#else
void __cdecl parser_printf(uint32 level, uint32 type, const char* fmt, ...);
#endif

#if !(defined(__WINCE) || defined(WIN32))
#define PARSER_INFO(infoType, fmt, args...) parser_printf(PARSER_LEVEL_INFO, infoType, fmt, ##args)
#define PARSER_ERROR(fmt, args...) parser_printf(PARSER_LEVEL_ERROR, 0, fmt, ##args)
#define PARSER_DEBUG(fmt, args...) parser_printf(PARSER_LEVEL_DEBUG, 0, fmt, ##args)
#define PARSER_WARNNING(fmt, args...) parser_printf(PARSER_LEVEL_WARNNING, 0, fmt, ##args)
#define PARSER_INFO_SAVE(infoType, fmt, args...) \
    do {                                         \
        PARSER_INFO(infoType, fmt, ##args);      \
        if (fpStreamInfo)                        \
            fprintf(fpStreamInfo, fmt, ##args);  \
    } while (0)
#else
#define PARSER_INFO(infoType, fmt, ...) \
    parser_printf(PARSER_LEVEL_INFO, infoType, fmt, __VA_ARGS__)  // printf("message from test\n\t")
#define PARSER_ERROR(fmt, ...) \
    parser_printf(PARSER_LEVEL_INFO, 0, fmt, __VA_ARGS__)  // printf("message from test\n\t")
#define PARSER_DEBUG(fmt, ...) \
    parser_printf(PARSER_LEVEL_INFO, 0, fmt, __VA_ARGS__)  // printf("message from test\n\t")
#define PARSER_WARNNING(fmt, ...) \
    parser_printf(PARSER_LEVEL_INFO, 0, fmt, __VA_ARGS__)  // printf("message from test\n\t")
#endif

#endif /* FSL_PARSER_ERR_LOG_H */
