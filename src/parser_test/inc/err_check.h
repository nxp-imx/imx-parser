/*
 *  Copyright 2024-2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef FSL_PARSER_ERR_CHECK_H
#define FSL_PARSER_ERR_CHECK_H

#include "err_logs.h"

/* potenatil error issues, not reflected in final error code */
#define MAX_ERROR_MESSAGE_LENGTH 255
#define ERR_LOG_FILE_NAME "err_parser_test.log"
#define MAX_TRACK_DURATION_GAP_IN_US (3 * 1000 * 1000)
#define MIN_VALID_DURATION_IN_US (3 * 1000 * 1000)
#define MAX_VALID_DURATION_IN_US (20 * 60 * 60 * 1000 * 1000LL) /* 20 hours */

#define MAX_SEEK_TIME_ERROR_IN_US                                                \
    (10 * 1000 * 1000) /* 10 seconds. If user just change speed without seeking, \
                          may exceeds this value, considerring key frame interval.*/
#define MAX_PTS_REPEAT_COUNT 10
#define MAX_PTS_NON_MONO_CHNAGE_COUNT 0 /* not allow non-mono pts change by default */

#define MAX_UNEXPECTED_BACKWARD_FILE_SEEK_COUNT 1
#define MAX_FILE_READ_FAILURE_COUNT 5
#define MAX_FILE_SEEK_ERROR 8

#define MAX_OUTPUT_BUFFER_SIZE (3 * 1024 * 1024)

#define MAX_MEMORY_BLOCK_SIZE (100 * 1024 * 1024)

/* potenatil risk flags */
#define RISK_FAIL_LOAD_INDEX 1
#define RISK_NOT_SEEKABLE (1 << 1)
#define RISK_INVALID_MOVIE_DURATION (1 << 2)
#define RISK_INVALID_TRACK_DURATION (1 << 3)

#define RISK_BAD_AV_SYNC (1 << 4)
#define RISK_ERR_CONCEALED (1 << 5)
#define RISK_DEEP_INTERLEAVING (1 << 6)

#define RISK_INVALID_AUDIO_BIT_DEPTH (1 << 7) /* bit depth 0 is common */
#define RISK_INVALID_SAMPLE_DURATION (1 << 8)

#define RISK_UNEXPECTED_BACKWARD_FILE_SEEK (1 << 9)
#define RISK_TOO_MANY_FILE_READ_ERROR (1 << 10)
#define RISK_TOO_MANY_FILE_SEEK_ERROR (1 << 11)

#define RISK_REQUEST_HUGE_OUTPUT_BUFFER (1 << 13)

#define RISK_PTS_CHANGE_IN_ONE_SAMPLE (1 << 15) /* PTS change in one sample */
#define RISK_INVALID_PTS_AFTER_SEEK (1 << 16)
#define RISK_NON_MONO_PTS_CHANGE (1 << 17) /* PTS does not change monotonically */
#define RISK_PTS_REPEAT (1 << 18)          /* too many pts repeat, usually means dead loop */
#define RISK_PREROLL_FAIL (1 << 19)

#define RISK_QUEUE_OVERFLOW \
    (1 << 20) /* queue overflow, caused by invalid PTS or output buffer size */
#define RISK_FAKE_QUEUE_OVERFLOW (1 << 21)

#define RISK_REQUEST_ZERO_SIZE_OUTPUT_BUFFER (1 << 24)
#define RISK_INVALID_MALLOC_SIZE (1 << 25)

typedef struct {
    uint32 errFlags;

    /* related value */
    int32 errOnLoadIndex;

    uint64 usMovieDuration; /* movie duration */

    uint32 trackNumOfBadDuration; /* invalid track duration & number */
    uint64 usBadTrackDuration;

    uint32 trackNumInvalidSampleDuration;
    uint64 usInvalidSampleDuration;

    uint32 trackNumBadAVSync;
    uint64 usTrackLastPTSBadAVSync;
    uint64 usBaseTrackLastPTS;

    uint32 trackNumOfInvalidPTSAfterSeek; /* after seek, 1st pts is invalid comparing with the
                                             segment start time */
    uint64 usInvalidPTSAfterSeek;
    uint64 usSegmentStartTime;
    int32 playRateInvalidPTSAfterSeek;

    uint32 trackNumNonMonoPTS; /* non-mono pts change at some rate */
    int32 playRateNonMonoPTS;
    uint64 usMaxPTSGitter;

    uint32 trackNumPTSRepeat;
    uint64 usPTSRepeated;
    uint64 playRatePTSRepeat;

    uint32 trackNumPtsChangeWithinSample;
    uint32 sampleNumPtsChangeWithinSample;
    uint64 usOriPtsOneSample;
    uint64 usNewPtsOneSample;

    uint32 trackNumPrerollFail;
    int32 playRatePrerollFail;
    uint64 usTrackLastPTS;
    uint64 usMovieSegmenStartTime;

    uint32 trackNumQueueOverflow;
    int32 playRateQueueOverflow;
    uint64 usSegmentStartTimeQueueOverflow;

    uint32 trackNumHugeOutBuffer;
    uint32 hugeOutBufferSizeRequested;
    int32 playRateHugeOutBuffer;
    uint64 usSegmentStartTimeHugeOutBuffer;

    uint32 trackNumErrConceal;
    uint32 sampleCountErrConceal;
    uint64 usLastPtsErrConceal;
    int32 playRateErrConceal;
    uint64 usSegmentStartTimeErrConceal;

    uint32 badAudioBitDepth; /* invalid audio bits per sample */

    uint32 unexpectedBackwardFileSeekCount; /* unexpected backward file seek counter */
    uint32 fileReadErrCount;                /* file read error counter */
    uint32 fileSeekErrCount;                /* file seek error counter */

    uint32 invalidMallocSize;

} MovieErrStatistics;

void resetErrorStatus();
void ListErrors(const char* source_url, uint32 numTracks, Track* tracks, const int32 err);
void displayErrorMessage(int32 err, char * errMessage, uint32 errMessageBufferSize);

void logError(uint32 errMask);
void logInvalidMalloc(uint32 size);

/* file IO risk */
void logFileReadError();
int32 checkFileReadError();

void logFileSeekError(char* filePath, int64 fileSize, int64 fileOffset);
void clearFileSeekError();
int32 checkFileSeekError();
void checkBackwardFileSeek(int64 curFileOffset, int64 targetFileOffset, int32 playRate);

/* media static risk */
void logIndexError(int32 err);

int32 checkMovieDuration(uint64 usDuration);
int32 checkTrackDuration(Track* track, uint64 usMovieDuration);

int32 checkAudioBitDepth(uint32 audioBitsPerSample);

/* media dynamic risk */
int32 checkErrorConcealment(Track* track, uint32 sampleFlag, int32 playRate,
                            uint64 usMovieSegmentStartTime);

void checkAVSync(uint32 numTracks, Track* tracks);
int32 checkPreroll(Track* track, int32 playRate, uint64 usMovieSegmentStartTime);
void logQueueOverflow(uint32 trackNum, int32 playRate, uint64 usMovieSegmentStartTime);
void logHugeOutputBufferRequest(uint32 trackNum, uint32 sizeRequested, int32 playRate,
                                uint64 usMovieSegmentStartTime);

int32 checkSamplePTS(Track* track, int32 playRate);

int32 checkPtsWithinSample(Track* track, uint64 sampleTimeStamp);
int32 checkSegmentFirstPts(Track* track, uint64 sampleTimeStamp, int32 playRate);
int32 checkPtsGitter(Track* track, uint64 sampleTimeStamp, int32 playRate);
int32 checkPtsRepeat(Track* track, uint64 sampleTimeStamp, uint64 sampleDuration, int32 playRate);
int32 checkSampleDuration(Track* track, uint64 sampleDuration);

#endif /* FSL_PARSER_ERR_LOG_H */
