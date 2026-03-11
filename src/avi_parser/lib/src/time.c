
/*
 ***********************************************************************
 * Copyright (c) 2005-2012, 2014, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "avi_parser_api.h"
#include "avi_utils.h"
#include "avi.h"

void getScaledTime(AVStreamPtr stream, uint64 usTime, uint64* scaledTime) {
    uint64 pts;

    if (MEDIA_VIDEO == stream->mediaType) {
        pts = (uint64)usTime * stream->rate / ((uint64)stream->scale * 1000000);
    } else if (MEDIA_AUDIO == stream->mediaType) {
        if (stream->isCbr) {
            pts = (uint64)usTime * (stream->bytesPerSec) / 1000000;
        } else {
            pts = (uint64)usTime * stream->sampleRate / ((uint64)stream->audioFrameSize * 1000000);
        }
    } else { /* text borrows video's scale & rate */
        pts = (uint64)usTime * stream->rate / ((uint64)stream->scale * 1000000);
    }

    *scaledTime = pts;
}

/* handle time conversion of avi tracks */

void getSampleTime(AVStreamPtr stream, uint64 scaledTime, uint64* usTime) {
    uint64 usSampleTime;

    if (MEDIA_VIDEO == stream->mediaType) {
        if (stream->rate)
            usSampleTime = (uint64)scaledTime * stream->scale * 1000000 / stream->rate;
        else
            usSampleTime = -1;
    } else if (MEDIA_AUDIO == stream->mediaType) {
        if (stream->isCbr) {
            usSampleTime = (uint64)scaledTime * 1000000 / stream->bytesPerSec;
        } else
            usSampleTime =
                    (uint64)scaledTime * stream->audioFrameSize * 1000000 / stream->sampleRate;
    } else if (MEDIA_TEXT ==
               stream->mediaType) { /* subtile, share video's enty pts, scale & rate */
        if (stream->rate)
            usSampleTime = (uint64)scaledTime * stream->scale * 1000000 / stream->rate;
        else
            usSampleTime = -1;
    } else
        usSampleTime = 0;

    *usTime = usSampleTime;
}

/* calculate the time stamp & duration in us */
void getTimestamp(AVStreamPtr stream, uint32 sampleSize, uint64* usStartTime, uint64* usDuration) {
    uint64 pts;
    uint64 usSampleDuration;
    uint64 sampleOffset = stream->sampleOffset;  // + stream->startDelay;

    if (MEDIA_VIDEO == stream->mediaType) /* video stream */
    {
        if (stream->rate)
            pts = sampleOffset * stream->scale * 1000000.0 / stream->rate;
        else
            pts = -1;
        usSampleDuration = stream->usFixedSampleDuration;
    }

    else if (MEDIA_AUDIO == stream->mediaType) /* audio stream */
    {
        if (stream->isCbr) {
            if (stream->decoderType == AUDIO_ADPCM) {
                if (stream->rate)
                    pts = sampleOffset / stream->blockAlign * stream->scale * 1000 / stream->rate *
                          1000;
                else
                    pts = -1;
                usSampleDuration = stream->usFixedSampleDuration;
            } else {
                pts = sampleOffset * 1000000.0 / stream->bytesPerSec;
                usSampleDuration = sampleSize * 1000000.0 / stream->bytesPerSec;
            }
        } else {
            pts = (sampleOffset * 1000000.0 * stream->audioFrameSize / stream->sampleRate);
            usSampleDuration = stream->usFixedSampleDuration;
        }
    }

    else  // if(MEDIA_TEXT == stream->mediaType) /* other stream */
    {     /* TODO: parser subtitle time code */
        if (stream->rate)
            pts = sampleOffset * stream->scale * 1000000.0 /
                  stream->rate; /* text borrows video's scale & rate */
        else
            pts = -1;
        usSampleDuration = 1000000; /* one second */
    }

    *usStartTime = pts;
    *usDuration = usSampleDuration;
}
