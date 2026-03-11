/*
***********************************************************************
* Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
* Copyright 2018, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include "fsl_osal.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "fsl_parser_drm.h"
#include "aac.h"

#include "memory_mgr.h"
#include "queue_mgr.h"
#include "fsl_parser_test.h"
#include "err_check.h"

static MovieErrStatistics g_movieErrStatistics = {0};
static uint32 g_audioBitDepth[] = {0,  1,  4, 8,
                                   16, 24, 32}; /* bit detph 0 is common, means unspecified */

extern bool g_dump_track_pts;

extern uint32 g_current_clip_number;
extern uint32 g_failed_clip_count;

extern uint64 g_usMaxPTSGitter;
extern FILE* fpErrLog;

extern int32 g_test_state;

extern int64 g_testFileSize;

void resetErrorStatus() {
    memset(&g_movieErrStatistics, 0, sizeof(MovieErrStatistics));
}

void ListErrors(const char* source_url, uint32 numTracks, Track* tracks, const int32 err) {
    uint64 usDuration;
    uint32 minutes;
    uint32 seconds;

    if (fpErrLog) {
        if (err || g_movieErrStatistics.errFlags ||
            (g_movieErrStatistics.unexpectedBackwardFileSeekCount >
             MAX_UNEXPECTED_BACKWARD_FILE_SEEK_COUNT)) {
            g_failed_clip_count++;
            fprintf(fpErrLog, "\nClip NO.%u fails or has risk. %u clips failed.\n",
                    g_current_clip_number, g_failed_clip_count);

            fprintf(fpErrLog, "%s\n", source_url);
            fprintf(fpErrLog, "Final err code %d. ", (int)err);
            if (err) {
                char* errMessage = fsl_osal_calloc(1, MAX_ERROR_MESSAGE_LENGTH);
                if (errMessage) {
                    displayErrorMessage(err, errMessage, MAX_ERROR_MESSAGE_LENGTH);
                    fprintf(fpErrLog, "%s", errMessage);
                    fsl_osal_free(errMessage);
                }
            }
            fprintf(fpErrLog, "\n");

            if (g_movieErrStatistics.errFlags & RISK_FAIL_LOAD_INDEX)
                fprintf(fpErrLog, "Fail to load index (err %d), index is missing or corrupted.\n",
                        (int)g_movieErrStatistics.errOnLoadIndex);

            if (!(g_movieErrStatistics.errFlags & RISK_FAIL_LOAD_INDEX) &&
                (g_movieErrStatistics.errFlags & RISK_NOT_SEEKABLE))
                fprintf(fpErrLog,
                        "Movie is not seekable although index loaded, no random access points "
                        "found?\n");

            if (g_movieErrStatistics.errFlags & RISK_INVALID_MOVIE_DURATION) {
                usDuration = g_movieErrStatistics.usMovieDuration;
                minutes = (uint32)(usDuration / 60000000);
                seconds = (uint32)(usDuration / 1000000 - minutes * 60);
                fprintf(fpErrLog, "Movie duration %lld us (%d m : %d s) may be invalid.\n",
                        usDuration, (int)minutes, (int)seconds);
            }

            if (g_movieErrStatistics.errFlags & RISK_INVALID_TRACK_DURATION) {
                usDuration = g_movieErrStatistics.usBadTrackDuration;
                minutes = (uint32)(usDuration / 60000000);
                seconds = (uint32)(usDuration / 1000000 - minutes * 60);
                fprintf(fpErrLog, "Track %u, invalid duration %lld us (%d m : %d s), ",
                        g_movieErrStatistics.trackNumOfBadDuration,
                        g_movieErrStatistics.usBadTrackDuration, (int)minutes, (int)seconds);

                usDuration = g_movieErrStatistics.usMovieDuration;
                minutes = (uint32)(usDuration / 60000000);
                seconds = (uint32)(usDuration / 1000000 - minutes * 60);
                fprintf(fpErrLog,
                        "while movie duration %lld us (%d m : %d s), track is empty or too "
                        "short!\n",
                        usDuration, (int)minutes, (int)seconds);
            }

            if (g_movieErrStatistics.errFlags & RISK_BAD_AV_SYNC) {
                fprintf(fpErrLog, "Potential bad AV sync!\t");
                usDuration = g_movieErrStatistics.usTrackLastPTSBadAVSync;
                minutes = (uint32)(usDuration / 60000000);
                seconds = (uint32)(usDuration / 1000000 - minutes * 60);
                fprintf(fpErrLog, "Trk %u end time %lld us (%d m : %d s), ",
                        g_movieErrStatistics.trackNumBadAVSync, usDuration, (int)minutes,
                        (int)seconds);

                usDuration = g_movieErrStatistics.usBaseTrackLastPTS;
                minutes = (uint32)(usDuration / 60000000);
                seconds = (uint32)(usDuration / 1000000 - minutes * 60);
                fprintf(fpErrLog, "while base track end time %lld us (%d m : %d s).\n", usDuration,
                        (int)minutes, (int)seconds);
            }

            if (g_movieErrStatistics.errFlags & RISK_ERR_CONCEALED) {
                usDuration = g_movieErrStatistics.usLastPtsErrConceal;
                minutes = (uint32)(usDuration / 60000000);
                seconds = (uint32)(usDuration / 1000000 - minutes * 60);
                fprintf(fpErrLog,
                        "Error concealment happened on track %u, sample %u, pts %lld us (%d m : %d "
                        "s), movie segment start time %lld us, play rate %dX\n",
                        g_movieErrStatistics.trackNumErrConceal,
                        g_movieErrStatistics.sampleCountErrConceal,
                        g_movieErrStatistics.usLastPtsErrConceal, (int)minutes, (int)seconds,
                        g_movieErrStatistics.usSegmentStartTimeErrConceal,
                        g_movieErrStatistics.playRateErrConceal);
            }

            if (g_movieErrStatistics.errFlags & RISK_DEEP_INTERLEAVING)
                fprintf(fpErrLog,
                        "CANNOT support file-based reading mode! Large interleaving movie or "
                        "parser disability ?\n");

            if (g_movieErrStatistics.errFlags & RISK_INVALID_SAMPLE_DURATION)
                fprintf(fpErrLog, "trk %u, invalid sample duration %lld us.\n",
                        g_movieErrStatistics.trackNumInvalidSampleDuration,
                        g_movieErrStatistics.usInvalidSampleDuration);

            if (g_movieErrStatistics.errFlags & RISK_INVALID_PTS_AFTER_SEEK)
                fprintf(fpErrLog,
                        "Track %d, 1st time stamp %lld us after seek may be invalid, segment start "
                        "time %lld us, gap %lld us. play rate %dX.\n",
                        (int)g_movieErrStatistics.trackNumOfInvalidPTSAfterSeek,
                        g_movieErrStatistics.usInvalidPTSAfterSeek,
                        g_movieErrStatistics.usSegmentStartTime,
                        g_movieErrStatistics.usInvalidPTSAfterSeek -
                                g_movieErrStatistics.usSegmentStartTime,
                        g_movieErrStatistics.playRateInvalidPTSAfterSeek);

            if (g_movieErrStatistics.errFlags & RISK_NON_MONO_PTS_CHANGE)
                fprintf(fpErrLog,
                        "Track %d, time stamp does not change monotonically at rate %dX.\n",
                        (int)g_movieErrStatistics.trackNumNonMonoPTS,
                        (int)g_movieErrStatistics.playRateNonMonoPTS);
            if (g_movieErrStatistics.usMaxPTSGitter)
                fprintf(fpErrLog, "Max PTS gitter %lld us\n", g_movieErrStatistics.usMaxPTSGitter);

            if (g_movieErrStatistics.errFlags & RISK_PTS_REPEAT)
                fprintf(fpErrLog,
                        "Track %d, time stamp %lld us repeats too many times at rate %dX. Possible "
                        "dead loop!\n",
                        (int)g_movieErrStatistics.trackNumPTSRepeat,
                        g_movieErrStatistics.usPTSRepeated,
                        (int)g_movieErrStatistics.playRatePTSRepeat);

            if (g_movieErrStatistics.errFlags & RISK_PTS_CHANGE_IN_ONE_SAMPLE)
                fprintf(fpErrLog,
                        "Track %d, sample %u, time stamp change within sample, from %lld to %lld "
                        "us.\n",
                        (int)g_movieErrStatistics.trackNumPtsChangeWithinSample,
                        g_movieErrStatistics.sampleNumPtsChangeWithinSample,
                        g_movieErrStatistics.usOriPtsOneSample,
                        g_movieErrStatistics.usNewPtsOneSample);

            if (g_movieErrStatistics.errFlags & RISK_PREROLL_FAIL) {
                fprintf(fpErrLog,
                        "Track %d, preroll may fail at rate %dX, last pts %lld us, movie segment "
                        "start time %lld us\n",
                        (int)g_movieErrStatistics.trackNumPrerollFail,
                        g_movieErrStatistics.playRatePrerollFail,
                        g_movieErrStatistics.usTrackLastPTS,
                        g_movieErrStatistics.usMovieSegmenStartTime);

                if ((MEDIA_TEXT == tracks[g_movieErrStatistics.trackNumPrerollFail].mediaType) &&
                    (0 > g_movieErrStatistics.playRatePrerollFail)) {
                    fprintf(fpErrLog,
                            "Rewind on a text track, please verify its 1st sample PTS is zero or "
                            "not.\n");
                }
            }

            if (g_movieErrStatistics.errFlags & RISK_INVALID_AUDIO_BIT_DEPTH)
                fprintf(fpErrLog, "Invalid audio bits per sample: %u\n",
                        g_movieErrStatistics.badAudioBitDepth);

            if (g_movieErrStatistics.errFlags & RISK_TOO_MANY_FILE_READ_ERROR)
                fprintf(fpErrLog, "Too many file reading errors, count: %u\n",
                        g_movieErrStatistics.fileReadErrCount);

            if (g_movieErrStatistics.errFlags & RISK_TOO_MANY_FILE_SEEK_ERROR)
                fprintf(fpErrLog, "Too many file seeking errors, count: %u\n",
                        g_movieErrStatistics.fileSeekErrCount);

            if (g_movieErrStatistics.errFlags & RISK_QUEUE_OVERFLOW) {
                fprintf(fpErrLog,
                        "Track %d, queue overflow at rate %dX, movie segment start time %lld us\n",
                        (int)g_movieErrStatistics.trackNumQueueOverflow,
                        (int)g_movieErrStatistics.playRateQueueOverflow,
                        g_movieErrStatistics.usSegmentStartTimeQueueOverflow);

                if (g_movieErrStatistics.errFlags & RISK_FAKE_QUEUE_OVERFLOW)
                    fprintf(fpErrLog,
                            "This is a fake queue overflow, only because some track ends too "
                            "early. Please check track duration\n");
            }

            if (g_movieErrStatistics.errFlags & RISK_REQUEST_HUGE_OUTPUT_BUFFER) {
                fprintf(fpErrLog,
                        "Track %d, request huge buffer of size %u bytes, at rate %dX, movie "
                        "segment start time %lld us\n",
                        (int)g_movieErrStatistics.trackNumHugeOutBuffer,
                        g_movieErrStatistics.hugeOutBufferSizeRequested,
                        (int)g_movieErrStatistics.playRateHugeOutBuffer,
                        g_movieErrStatistics.usSegmentStartTimeHugeOutBuffer);
            }

            if (g_movieErrStatistics.errFlags & RISK_REQUEST_ZERO_SIZE_OUTPUT_BUFFER)
                fprintf(fpErrLog, "Request zero-size output buffer.\n");

            if (g_movieErrStatistics.errFlags & RISK_INVALID_MALLOC_SIZE)
                fprintf(fpErrLog, "Allocate invalid memory size: %u bytes.\n",
                        g_movieErrStatistics.invalidMallocSize);

            if (g_movieErrStatistics.unexpectedBackwardFileSeekCount >
                MAX_UNEXPECTED_BACKWARD_FILE_SEEK_COUNT)
                fprintf(fpErrLog, "Too much backward file seeking for streaming, count %d.\n",
                        (int)g_movieErrStatistics.unexpectedBackwardFileSeekCount);

            fprintf(fpErrLog, "\n\n");
        } else {
            fprintf(fpErrLog, "Clip NO.%u is OK. %u clips failed.\n", g_current_clip_number,
                    g_failed_clip_count);
        }

        fclose(fpErrLog);
    }
    (void)numTracks;
    return;
}

void displayErrorMessage(int32 err, char * errMessage, uint32 errMessageBufferSize) {
    errMessage[0] = 0;
    switch (err) {
        case PARSER_NEED_MORE_DATA:
            strcpy(errMessage, "need more data");
            break;

        case PARSER_ERR_UNKNOWN:
            strcpy(errMessage, "unknown reason");
            break;

        case PARSER_ERR_INVALID_API:
            strcpy(errMessage, "invalid API, not implemented properly");
            break;

        case PARSER_NOT_IMPLEMENTED:
            strcpy(errMessage, "No support for some feature");
            break;

        case PARSER_ERR_INVALID_PARAMETER:
            strcpy(errMessage, "parameters are invalid");
            break;

        case PARSER_INSUFFICIENT_MEMORY:
            strcpy(errMessage, "insufficient memory");
            break;

        case PARSER_INSUFFICIENT_DATA:
            strcpy(errMessage, "insufficient data");
            break;

        case PARSER_ERR_NO_OUTPUT_BUFFER:
            strcpy(errMessage, "output buffer exhuasted, pipeline dead lock");
            break;

        case PARSER_FILE_OPEN_ERROR:
            strcpy(errMessage, "fail to open file");
            break;

        case PARSER_WRITE_ERROR:
            strcpy(errMessage, "file write error");
            break;

        case PARSER_READ_ERROR:
            strcpy(errMessage, "file read error");
            break;

        case PARSER_SEEK_ERROR:
            strcpy(errMessage, "file seek error");
            break;

        case PARSER_ILLEAGAL_FILE_SIZE:
            sprintf(errMessage, "file size %lld is invalid or exeeds parser's capacity",
                    g_testFileSize);
            break;

        case PARSER_ERR_INVALID_MEDIA:
            strcpy(errMessage, "invalid media, not supported");
            break;

        case PARSER_ERR_NOT_SEEKABLE:
            strcpy(errMessage, "movie is not seekable");
            break;

        case PARSER_ERR_CONCEAL_FAIL:
            strcpy(errMessage, "error concealment fail");
            break;

        case PARSER_ERR_MEMORY_ACCESS_VIOLATION:
            strcpy(errMessage, "intenal memory access violation");
            break;

        case PARSER_ERR_TRACK_DISABLED:
            strcpy(errMessage, "access a disabled track");
            break;

        case PARSER_ERR_INVALID_READ_MODE:
            strcpy(errMessage, "invalid read mode");
            break;

        case DRM_ERR_NOT_AUTHORIZED_USER:
            strcpy(errMessage, "not authorized user");
            break;

        case DRM_ERR_RENTAL_EXPIRED:
            strcpy(errMessage, "rental is expired");
            break;

        default:
            break;
    }
    (void)errMessageBufferSize;
    return;
}

void logError(uint32 errMask) {
    g_movieErrStatistics.errFlags |= errMask;
}

void logInvalidMalloc(uint32 size) {
    g_movieErrStatistics.errFlags |= RISK_INVALID_MALLOC_SIZE;
    g_movieErrStatistics.invalidMallocSize = size;
}

void logFileReadError() {
    g_movieErrStatistics.fileReadErrCount++;
}

int32 checkFileReadError() {
    int32 err = PARSER_SUCCESS;

    if (MAX_FILE_READ_FAILURE_COUNT < g_movieErrStatistics.fileReadErrCount) {
        err = PARSER_SUCCESS;
    }

    return err;
}

void logFileSeekError(char* filePath, int64 fileSize, int64 fileOffset) {
    g_movieErrStatistics.fileSeekErrCount++;

    if (fpErrLog) {
        fprintf(fpErrLog, "Clip %s, seek to invalid offset %lld, file size %lld\n", filePath,
                fileOffset, fileSize);
        fclose(fpErrLog);
        fpErrLog = fopen(ERR_LOG_FILE_NAME, "a");
    }
}

void clearFileSeekError() {
    g_movieErrStatistics.unexpectedBackwardFileSeekCount = 0;
}

int32 checkFileSeekError() {
    int32 err = PARSER_SUCCESS;

    if (MAX_FILE_SEEK_ERROR < g_movieErrStatistics.fileSeekErrCount) {
        PARSER_ERROR("file seek callback failed too many times!\n");
        g_movieErrStatistics.errFlags |= RISK_TOO_MANY_FILE_SEEK_ERROR;
        err = PARSER_SEEK_ERROR;
    }

    return err;
}

void checkBackwardFileSeek(int64 curFileOffset, int64 targetFileOffset, int32 playRate) {
    if ((targetFileOffset < curFileOffset) && (TEST_STATE_PLAYING == g_test_state) &&
        (playRate >= 0)) {
        PARSER_WARNNING(
                "\nWarning! Backward file seeking for live source, cur postion %lld, target %lld "
                "******\n",
                curFileOffset, targetFileOffset);
        g_movieErrStatistics.unexpectedBackwardFileSeekCount++;
    }
}

void logIndexError(int32 err) {
    g_movieErrStatistics.errFlags |= RISK_FAIL_LOAD_INDEX;
    g_movieErrStatistics.errOnLoadIndex = err;
}

int32 checkMovieDuration(uint64 usDuration) {
    int32 err = PARSER_SUCCESS;

    if ((MIN_VALID_DURATION_IN_US >= (int64)usDuration) ||
        (MAX_VALID_DURATION_IN_US < usDuration)) {
        g_movieErrStatistics.errFlags |= RISK_INVALID_MOVIE_DURATION;
    }

    g_movieErrStatistics.usMovieDuration = usDuration; /* for log bad av sync */

    return err;
}

int32 checkTrackDuration(Track* track, uint64 usMovieDuration) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum = track->trackNum;
    uint64 usTrackDuration = track->usDuration;
    uint32 mediaType = track->mediaType;

    if ((MEDIA_VIDEO != mediaType) && (MEDIA_AUDIO != mediaType) && (MEDIA_TEXT != mediaType))
        return err;

    if ((0 >= (int64)usTrackDuration) ||
        ((usTrackDuration + MAX_TRACK_DURATION_GAP_IN_US) < usMovieDuration)) {
        g_movieErrStatistics.errFlags |= RISK_INVALID_TRACK_DURATION;
        g_movieErrStatistics.usBadTrackDuration = usTrackDuration;
        g_movieErrStatistics.trackNumOfBadDuration = trackNum;
    }

    return err;
}

int32 checkAudioBitDepth(uint32 audioBitsPerSample) {
    int32 err = PARSER_SUCCESS;
    uint32 audioBitDepthIndex;
    bool isAudioBitDepthValid = FALSE;

    for (audioBitDepthIndex = 0; audioBitDepthIndex < (sizeof(g_audioBitDepth) / sizeof(uint32));
         audioBitDepthIndex++) {
        if (audioBitsPerSample == g_audioBitDepth[audioBitDepthIndex]) {
            PARSER_INFO(PARSER_INFO_STREAM, "\t bits per sample: %ld\n", audioBitsPerSample);
            isAudioBitDepthValid = TRUE;
            break;
        }
    }

    if (!isAudioBitDepthValid) {
        PARSER_INFO(PARSER_INFO_STREAM, "\t bits per sample: %ld ... invalid!\n",
                    audioBitsPerSample);
        g_movieErrStatistics.errFlags |= RISK_INVALID_AUDIO_BIT_DEPTH;
        g_movieErrStatistics.badAudioBitDepth = audioBitsPerSample;
    }

    return err;
}

int32 checkErrorConcealment(Track* track, uint32 sampleFlag, int32 playRate,
                            uint64 usMovieSegmentStartTime) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum = track->trackNum;

    if (FLAG_SAMPLE_ERR_CONCEALED & sampleFlag) {
        PARSER_INFO(PARSER_INFO_ERRORCONSEAL, "trk %d, ERR CONCEALED! samp %d: size %d\n", trackNum,
                    (uint32)track->sampleCount, track->sampleSize);

        if (!(g_movieErrStatistics.errFlags & RISK_ERR_CONCEALED)) {
            /* log the 1st error concealment position */
            g_movieErrStatistics.trackNumErrConceal = track->trackNum;
            g_movieErrStatistics.sampleCountErrConceal = track->sampleCount;
            g_movieErrStatistics.usLastPtsErrConceal = track->lastPts;
            g_movieErrStatistics.playRateErrConceal = playRate;
            g_movieErrStatistics.usSegmentStartTimeErrConceal = usMovieSegmentStartTime;
        }

        g_movieErrStatistics.errFlags |= RISK_ERR_CONCEALED;
    }
    if ((FLAG_SAMPLE_SUGGEST_SEEK & sampleFlag) || (PARSER_ERR_CONCEAL_FAIL == err)) {
        PARSER_INFO(PARSER_INFO_ERRORCONSEAL, "trk %d, SUGGEST SEEK!\n");
    }

    // bail:
    return err;
}

/* check whether AV stay in good sync, check last pts of all enabled a/v tracks.
under trick mode, only video is enabled, so always in sync */
void checkAVSync(uint32 numTracks, Track* tracks) {
    uint64 usBaseEndTime = 0;
    Track* track;
    uint32 i;

    for (i = 0; i < numTracks; i++) {
        track = tracks + i;

        if (!track->enabled)
            continue; /* */

        if ((MEDIA_VIDEO != track->mediaType) && (MEDIA_AUDIO != track->mediaType))
            continue; /* TODO: Check subtitle PTS if it's valid */

        if (track->isBSAC && (track->trackNum != track->firstBSACTrackNum))
            continue;

        if (0 == usBaseEndTime)
            usBaseEndTime = track->lastPts;

        if ((track->lastPts > (usBaseEndTime + DEFAULT_AV_SYNC_THRSHOLD_IN_US)) ||
            (usBaseEndTime > (track->lastPts + DEFAULT_AV_SYNC_THRSHOLD_IN_US))) {
            uint64 usTimeGap;

            if (track->lastPts > usBaseEndTime)
                usTimeGap = track->lastPts - usBaseEndTime;
            else
                usTimeGap = usBaseEndTime - track->lastPts;
            PARSER_WARNNING(
                    "\nWarning! Possbile bad AV sync! Base end time %lld us, trk %d end time %lld "
                    "us, ",
                    usBaseEndTime, i, track->lastPts);
            PARSER_WARNNING("time gap %lld us\n", usTimeGap);

            g_movieErrStatistics.errFlags |= RISK_BAD_AV_SYNC;
            g_movieErrStatistics.trackNumBadAVSync = i;
            g_movieErrStatistics.usTrackLastPTSBadAVSync = track->lastPts;
            g_movieErrStatistics.usBaseTrackLastPTS = usBaseEndTime;
        }
    }

    return;
}

int32 checkPreroll(Track* track, int32 playRate, uint64 usMovieSegmentStartTime) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum = track->trackNum;

    if (!track->enabled)
        return err;

    /* check risk of preroll failure */

    if (((0 < playRate) && (track->lastPts < usMovieSegmentStartTime)) ||
        ((0 > playRate) && (track->lastPts > usMovieSegmentStartTime))) {
        PARSER_WARNNING(
                "trk %d, preroll may fail at play rate %ldX! movie segment start time %lld us, "
                "track start time %lld, last sample pts %lld us.\n",
                trackNum, playRate, usMovieSegmentStartTime, track->usSegmentStartTime,
                track->lastPts);

        g_movieErrStatistics.errFlags |= RISK_PREROLL_FAIL;
        g_movieErrStatistics.trackNumPrerollFail = trackNum;
        g_movieErrStatistics.playRatePrerollFail = playRate;
        g_movieErrStatistics.usTrackLastPTS = track->lastPts;
        g_movieErrStatistics.usMovieSegmenStartTime = usMovieSegmentStartTime;
    }

    return err;
}

void logQueueOverflow(uint32 trackNum, int32 playRate, uint64 usMovieSegmentStartTime) {
    g_movieErrStatistics.errFlags |= RISK_QUEUE_OVERFLOW;
    g_movieErrStatistics.trackNumQueueOverflow = trackNum;
    g_movieErrStatistics.playRateQueueOverflow = playRate;
    g_movieErrStatistics.usSegmentStartTimeQueueOverflow = usMovieSegmentStartTime;
}

void logHugeOutputBufferRequest(uint32 trackNum, uint32 sizeRequested, int32 playRate,
                                uint64 usMovieSegmentStartTime) {
    g_movieErrStatistics.errFlags |= RISK_REQUEST_HUGE_OUTPUT_BUFFER;
    g_movieErrStatistics.trackNumHugeOutBuffer = trackNum;
    g_movieErrStatistics.hugeOutBufferSizeRequested = sizeRequested;
    g_movieErrStatistics.playRateHugeOutBuffer = playRate;
    g_movieErrStatistics.usSegmentStartTimeHugeOutBuffer = usMovieSegmentStartTime;
}

int32 checkPtsWithinSample(Track* track, uint64 sampleTimeStamp) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum = track->trackNum;

    if (sampleTimeStamp != track->usSampleTimeStamp &&
        sampleTimeStamp != (uint64)PARSER_UNKNOWN_TIME_STAMP) {
        PARSER_ERROR("trk %d, in sample %ld, invalid pts change from %lld to %lld us\n", trackNum,
                     track->sampleCount, track->usSampleTimeStamp, sampleTimeStamp);
        g_movieErrStatistics.errFlags |= RISK_PTS_CHANGE_IN_ONE_SAMPLE;
        g_movieErrStatistics.trackNumPtsChangeWithinSample = trackNum;
        g_movieErrStatistics.sampleNumPtsChangeWithinSample = track->sampleCount;
        g_movieErrStatistics.usOriPtsOneSample = track->usSampleTimeStamp;
        g_movieErrStatistics.usNewPtsOneSample = sampleTimeStamp;
        BAILWITHERROR(PARSER_ERR_UNKNOWN)
    }

bail:
    return err;
}

int32 checkSamplePTS(Track* track, int32 playRate) {
    uint32 err = PARSER_SUCCESS;
    uint64 sampleTimeStamp = track->usSampleTimeStamp;
    uint64 sampleDuration = track->usCurSampleDuration;

    if (g_dump_track_pts && track->fp_track_pts_dump)
        fprintf(track->fp_track_pts_dump, "%lld %d\n", sampleTimeStamp, track->sampleSize);

    if (!track->firstSampleDataGot) {
        track->firstSampleDataGot = TRUE;
        if (PARSER_SUCCESS != checkSegmentFirstPts(track, sampleTimeStamp, playRate))
            BAILWITHERROR(PARSER_ERR_UNKNOWN)
    } else {
        if (PARSER_SUCCESS != checkPtsGitter(track, sampleTimeStamp, playRate))
            BAILWITHERROR(PARSER_ERR_UNKNOWN)
    }

    if (PARSER_SUCCESS != checkPtsRepeat(track, sampleTimeStamp, sampleDuration, playRate))
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (PARSER_SUCCESS != checkSampleDuration(track, sampleDuration))
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if ((uint64)PARSER_UNKNOWN_TIME_STAMP != sampleTimeStamp) {
        if (0 < playRate)
            track->lastPts = sampleTimeStamp + sampleDuration;
        else
            track->lastPts =
                    sampleTimeStamp; /* assure PTS fall in segment time window when rewinding */
        track->lastPtsPure = sampleTimeStamp;
    }

bail:
    return err;
}

int32 checkSegmentFirstPts(Track* track, uint64 sampleTimeStamp, int32 playRate) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum = track->trackNum;

    if (((MAX_SEEK_TIME_ERROR_IN_US + sampleTimeStamp) < track->usSegmentStartTime) ||
        ((MAX_SEEK_TIME_ERROR_IN_US + track->usSegmentStartTime) < sampleTimeStamp) ||
        ((uint64)PARSER_UNKNOWN_TIME_STAMP == sampleTimeStamp))

    {
        if ((MEDIA_VIDEO == track->mediaType) || (MEDIA_AUDIO == track->mediaType) ||
            ((MEDIA_TEXT == track->mediaType) &&
             (TXT_DIVX_FEATURE_SUBTITLE !=
              track->decoderType))) { /* No way to get accurate PTS for divx subtitle */
            PARSER_WARNNING(
                    "trk %d, invalid pts %lld us after seek, segment start time %lld us, gap %lld "
                    "us. At rate %ldX.\n",
                    trackNum, sampleTimeStamp, track->usSegmentStartTime,
                    sampleTimeStamp - track->usSegmentStartTime, playRate);
            g_movieErrStatistics.errFlags |= RISK_INVALID_PTS_AFTER_SEEK;
            g_movieErrStatistics.trackNumOfInvalidPTSAfterSeek = trackNum;
            g_movieErrStatistics.usInvalidPTSAfterSeek = sampleTimeStamp;
            g_movieErrStatistics.usSegmentStartTime = track->usSegmentStartTime;
            g_movieErrStatistics.playRateInvalidPTSAfterSeek = playRate;
        }
    }

    return err;
}

int32 checkPtsGitter(Track* track, uint64 sampleTimeStamp, int32 playRate) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum = track->trackNum;

    if (((uint64)PARSER_UNKNOWN_TIME_STAMP != sampleTimeStamp) &&
        (0 > (playRate * (int64)(sampleTimeStamp - track->lastPtsPure)))) {
        PARSER_WARNNING(
                "trk %d, sample %d, non-mono pts %lld us, prev pts %lld us. play rate %ldX, "
                "segment start time %lld us\n",
                trackNum, track->sampleCount, sampleTimeStamp, track->lastPtsPure, playRate,
                track->usSegmentStartTime);
        g_movieErrStatistics.errFlags |= RISK_NON_MONO_PTS_CHANGE;
        g_movieErrStatistics.trackNumNonMonoPTS = trackNum;
        g_movieErrStatistics.playRateNonMonoPTS = playRate;

        if (0 == g_usMaxPTSGitter)
            track->ptsNoMonoCount++;
        else {
            uint64 usPTSGitter = 0;
            if ((0 < playRate) && ((sampleTimeStamp + g_usMaxPTSGitter) < track->lastPtsPure)) {
                usPTSGitter = track->lastPtsPure - sampleTimeStamp;
                track->ptsNoMonoCount++;
            }

            else if ((0 > playRate) &&
                     ((track->lastPtsPure + g_usMaxPTSGitter) < sampleTimeStamp)) {
                usPTSGitter = sampleTimeStamp - track->lastPtsPure;
                track->ptsNoMonoCount++;
            }

            if (g_movieErrStatistics.usMaxPTSGitter < usPTSGitter)
                g_movieErrStatistics.usMaxPTSGitter = usPTSGitter;
        }
    }

    return err;
}

int32 checkPtsRepeat(Track* track, uint64 sampleTimeStamp, uint64 sampleDuration, int32 playRate) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum = track->trackNum;

    if ((uint64)PARSER_UNKNOWN_TIME_STAMP != sampleTimeStamp) {
        if (sampleTimeStamp == track->lastPtsPure) /* PTS repeats ? */
        {
            track->ptsRepeatCount++;
            if (MAX_PTS_REPEAT_COUNT < track->ptsRepeatCount &&
                (!track->isMpeg2Video)) { /* possible dead loop */
                PARSER_ERROR(
                        "trk %d, sample %d, pts %lld us repeats too much at rate %dX. Dead loop!\n",
                        trackNum, track->sampleCount, sampleTimeStamp, playRate);
                g_movieErrStatistics.errFlags |= RISK_PTS_REPEAT;
                g_movieErrStatistics.trackNumPTSRepeat = trackNum;
                g_movieErrStatistics.usPTSRepeated = sampleTimeStamp;
                g_movieErrStatistics.playRatePTSRepeat = playRate;
                BAILWITHERROR(PARSER_ERR_UNKNOWN)
            }
        } else
            track->ptsRepeatCount = 0;
    }
    (void)sampleDuration;
bail:
    return err;
}

int32 checkSampleDuration(Track* track, uint64 sampleDuration) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum = track->trackNum;

    if (MAX_VALID_DURATION_IN_US < sampleDuration) {
        g_movieErrStatistics.errFlags |= RISK_INVALID_SAMPLE_DURATION;
        g_movieErrStatistics.trackNumInvalidSampleDuration = trackNum;
        g_movieErrStatistics.usInvalidSampleDuration = sampleDuration;
        BAILWITHERROR(PARSER_ERR_UNKNOWN)
    }

bail:
    return err;
}
