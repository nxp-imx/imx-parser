
/*
 ***********************************************************************
 * Copyright (c) 2010-2012,2016 Freescale Semiconductor, Inc.
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

/* Only support video/audio track now */
int32 seekTrack(AviObjectPtr self, AVStreamPtr stream, uint64* usTime, uint32 flag) {
    int32 err = PARSER_SUCCESS;
    uint64 usTargetTime = *usTime;
    uint64 targetTime;   /* scaled target time */
    uint64 usSampleTime; /* sample time of the matched entry */
    uint64 firstIndex;
    uint64 lastIndex;
    uint64 midIndex = 0;
    indexEntryPtr entry = NULL;
    indexEntryPtr entry1;
    indexEntryPtr entry2;
    uint64 pts;
    bool found = FALSE;

    if (0 == usTargetTime) /* seek to the beginning of the track, always succeeds no matter index
                              table exist or not! */
    {
        seek2TrackStart(self, stream);
        AVIMSG("seek to BOS\n");
        goto bail;
    } else if ((stream->usDuration <= usTargetTime) && (SEEK_FLAG_NO_EARLIER == flag)) {
        /* seek to the end of the track. For other flag (no later/nearest),
        seek to last sync sample after check PTS! */
        *usTime = stream->usDuration;
        seek2TrackEnd(self, stream);
        AVIMSG("seek to EOS\n");
        BAILWITHERROR(PARSER_EOS)
    }

    if ((NULL == stream->indexTab) || (0 == stream->numIndexEntries)) {
        AVIMSG("trk %d has no index, seek to EOS\n", stream->streamIndex);
        seek2TrackEnd(self, stream); /* sometimes maybe only primary stream has index */
        BAILWITHERROR(PARSER_EOS)
    }

    getScaledTime(stream, usTargetTime, &targetTime);

    if ((targetTime < stream->indexTab[0].pts) &&
        (SEEK_FLAG_NO_EARLIER != flag)) /* mainly for subtitle.*/
    {
        seek2TrackStart(self, stream);
        *usTime = 0;
        AVIMSG("Target time %d less than 1st index pts %d, seek to BOS\n", (uint32)targetTime,
               (uint32)stream->indexTab[0].pts);
        goto bail;
    }
#if 0
    if ( (TRUE == self->bCorruptedIdx) &&
        (targetTime > stream->indexTab[stream->numIndexEntries-1].pts) )
    {
        AVIMSG("Corrupted Idx (%d): Target %d, but last index pts %d",
            stream->streamIndex, (uint32)targetTime, (uint32)stream->indexTab[stream->numIndexEntries-1].pts);
        err = PARSER_ERR_NOT_SEEKABLE;
        goto bail;
    }
#endif
    /* binary search */
    firstIndex = 0;
    lastIndex = stream->numIndexEntries - 1;

    while (firstIndex < lastIndex) {
        midIndex = (lastIndex + firstIndex) / 2;
        entry = stream->indexTab + midIndex;
        pts = entry->pts;
        getSampleTime(stream, pts, &usSampleTime);
        if (usSampleTime == usTargetTime) {
            found = TRUE;
            AVIMSG("idx %llu, match\n", midIndex);
            break;
        } else if (usSampleTime < usTargetTime) {
            if (firstIndex < midIndex)
                firstIndex = midIndex;
            else
                break;
        } else {
            if (lastIndex > midIndex)
                lastIndex = midIndex;
            else
                break;
        }
    }

    if (FALSE == found) {
        uint64 usPTS_1, usPTS_2;

        AVIMSG("pts [%llu ,\t", (stream->indexTab + firstIndex)->pts);
        AVIMSG("%llu]\n", (stream->indexTab + lastIndex)->pts);

        entry1 = stream->indexTab + firstIndex;
        entry2 = stream->indexTab + lastIndex;

        getSampleTime(stream, entry1->pts, &(usPTS_1));
        getSampleTime(stream, entry2->pts, &(usPTS_2));

        if (entry1 == entry2)
            entry = entry1;
        else {
            if (SEEK_FLAG_NO_LATER == flag) /* NO_LATER */
            {
                if (usTargetTime >= usPTS_2) {
                    entry = entry2;
                    usSampleTime = usPTS_2;
                } else {
                    entry = entry1;
                    usSampleTime = usPTS_1;
                }

                /* video's matched PTS will be used a the segment start time.
                Audio & subtitle must make sure there is at least one sample after the segment start
                time (target time). Otherwise deadlock will happen in framework preroll, nder
                file-based reading mode! Subtitles are interleaved no more than 500ms prior to the
                video timestamp to which they belong. No more than 1 subtitle will be interleaved
                before the corresponding video frame. And each subtitle is indexed and align to the
                following video frame PTS. So we only need to double check audio!*/
                if ((MEDIA_AUDIO == stream->mediaType) && (usTargetTime > usSampleTime)) {
                    bool hasRiskForPreroll = FALSE;

                    /* Is near track EOS and is there possible sample following this entry?
                    A/V track duration is accurate as long as the index exists.*/
                    if (entry == (stream->indexTab + stream->numIndexEntries -
                                  1)) { /* this is the last indexed sample, may be end of track! */
#if 0
                        uint64 usTimeToEnd;

                        getSampleTime(stream, stream->cumLength - (uint64)entry->pts, &usTimeToEnd);
                        if(usTimeToEnd < ONE_SECOND_IN_US)
                        {
                            getSampleTime(stream, (uint64)entry->pts, &usSampleTime);
                            AVIMSG("\t Matched sample pts %lld us very near to EOS, only %lld us to the end.", usSampleTime, usTimeToEnd);
                            hasRiskForPreroll = TRUE;
                        }
#endif
                        if (stream->usDuration < (usTargetTime + ONE_SECOND_IN_US)) {
                            AVIMSG("\t Target time %lld us exceeds or very near to track duration "
                                   "%lld us.",
                                   usTargetTime, stream->usDuration);
                            hasRiskForPreroll = TRUE;
                        }

                        if (hasRiskForPreroll) {
                            AVIMSG("\t Risk for preroll! Seek to EOS\n");
                            *usTime = stream->usDuration;
                            seek2TrackEnd(self, stream);
                            BAILWITHERROR(PARSER_EOS)
                        }
                    }
                }
            } else if (SEEK_FLAG_NO_EARLIER == flag) /* NO_EARLIER */
            {
                entry = entry2;
            } else /* NEAREST */
            {
                /* compare time stamp */
                if (usTargetTime >= usPTS_2) /* target pts may be larger than the last entry */
                    entry = entry2;
                else if (usTargetTime - usPTS_1 <=
                         usPTS_2 - usTargetTime) /* pick the nearest one */
                    entry = entry1;
                else
                    entry = entry2;
            }
        }
    }

    getSampleTime(stream, (uint64)entry->pts, &usSampleTime);

    if ((SEEK_FLAG_NO_EARLIER == flag) &&
        (usSampleTime < usTargetTime)) { /* need further modification because of error in
                                            calculating scaled target time */
        if (found)
            lastIndex = midIndex;

        if ((lastIndex + 1) < stream->numIndexEntries) {
            entry++;
            AVIMSG("\t index entry increase by 1\n");
            getSampleTime(stream, (uint64)entry->pts, &usSampleTime);
        } else {
            entry = NULL;
            *usTime = stream->usDuration;
            seek2TrackEnd(self, stream);
            AVIMSG("\t Seek to EOS\n");
            BAILWITHERROR(PARSER_EOS)
        }
    }

    *usTime = usSampleTime;
    seek2Entry(stream, entry);

    AVIMSG("\t finally match idx %u, pts %u, file offset %lld\n",
           (uint32)(entry - stream->indexTab), (uint32)entry->pts, stream->fileOffset);

bail:
    AVIMSG("\t Actual seeked time in us %u\n", (uint32)*usTime);

    return err;
}

/* seek to the nearest sync sample from current reading position,
IF no sync sample can be found, this function will return EOS or BOS.

Note:
1. The stream's sample counter (sampleOffset) will not increase or decrease, as long as the sample
has not been read.
2. For subtitle, since several subtitle sample may share the following video key frame PTS,
    some may be skipped in trick mode because only 1 of them will be picked.
3. First A/V is always indexed as key frame, PTS starts from 0.
   And 1st subtitle PTS is forced to be ZERO. */
int32 seek2SyncSample(AviObjectPtr self, AVStreamPtr stream, uint32 direction) {
    int32 err = PARSER_SUCCESS;
    bool forward;
    uint64 targetTime;
    uint64 firstIndex;
    uint64 lastIndex;
    uint64 midIndex;
    indexEntryPtr entry = NULL;
    uint64 pts;
    bool found = FALSE;

    /* get the next sync sample */
    if (FLAG_FORWARD == direction) {
        if (!stream->isOneSampleGotAfterSkip) {
            if (stream->sampleOffset)
                return PARSER_SUCCESS; /* nothing need to do, still align to the sync sample */
            /* Otherwise, means from the movie start,
              first sample will be special, because 1st sample may not be a key frame
             (an index table error or really true ), need to search the index table
             to assure right data &PTS */
        }

        targetTime = stream->sampleOffset;

    } else {
        if ((1 >= (int64)stream->sampleOffset) &&
            stream->isOneSampleGotAfterSkip) { /* for both CBR & VBR, just read the 1st sample.
                                               not move sample counter, still at sample 1, ready for
                                               FF or 1X !*/
            AVIMSG("trk %d, already reach BOS\n", stream->streamIndex);
            stream->bos = TRUE;
            return PARSER_BOS;
        }

        /* Step backward if and only if a sample was just read or on EOS */
        if (stream->isOneSampleGotAfterSkip || (stream->fileOffset >= self->moviEnd)) {
            if (FALSE == stream->isCbr) /* VBR stream */
            {
                targetTime = stream->sampleOffset - 2;
            } else /* CBR stream */
            {
                targetTime = stream->sampleOffset - stream->prevSampleSize - 1;
            }

            if ((0 > (int64)targetTime) || (targetTime < stream->indexTab[0].pts)) {
                AVIMSG("Warning! trk %d, no preivious sync sample can be found. return BOS\n",
                       stream->streamIndex);
                stream->bos = TRUE;
                BAILWITHERROR(PARSER_BOS)
            }
        }

        if (!stream->isOneSampleGotAfterSkip)
            return PARSER_SUCCESS;

        stream->sampleOffset = (uint64)targetTime;
    }

    if (FLAG_FORWARD == direction)
        forward = TRUE;
    else
        forward = FALSE;

    if ((NULL == stream->indexTab) || (0 == stream->numIndexEntries)) {
        BAILWITHERROR(AVI_ERR_NO_INDEX)
    }

    stream->isOneSampleGotAfterSkip = FALSE;

    /* binary search */
    firstIndex = 0;
    lastIndex = stream->numIndexEntries - 1;

    while (firstIndex < lastIndex) {
        midIndex = (lastIndex + firstIndex) / 2;
        entry = stream->indexTab + midIndex;
        pts = entry->pts;
        if (pts == targetTime) {
            found = TRUE;
            break;
        } else if (pts < targetTime) {
            if (firstIndex < midIndex)
                firstIndex = midIndex;
            else
                break;
        } else {
            if (lastIndex > midIndex)
                lastIndex = midIndex;
            else
                break;
        }
    }

    if (FALSE == found) {
        if (FALSE == forward) /* previous sync sample */
            entry = stream->indexTab + firstIndex;

        else /* next sync sample */
        {
            entry = stream->indexTab + lastIndex;
            if (entry->pts < targetTime) {
                AVIMSG("no next sync sample can be found. return EOS\n");
                seek2TrackEnd(self, stream);
                BAILWITHERROR(PARSER_EOS)
            }
        }
    }

    seek2Entry(stream, entry);

bail:
    return err;
}

/* seeking to track start shall always to the 1st sample of the track.
It means when the index table is present, the 1st sample shall always be indexed!*/
void seek2TrackStart(AviObjectPtr self, AVStreamPtr stream) {
    stream->sampleOffset = 0;

#if 1
    if (stream->indexTab && stream->numIndexEntries) {
        /* Assume 1st sample is always indexed ! Otherwise, bad AV sync.
        This is a dangerous assumption because index table may be wrong!*/
        stream->fileOffset = (uint64)stream->indexTab[0].offset;
        AVIMSG("trk %d, seek file offset to 1st sample:  %lld\n", stream->streamIndex,
               stream->fileOffset);
    } else
#endif
    {
        /* First several key frames may NOT be marked in the index table (usually index table
        error), but they shall not be missed in playback! So seeking to the movie start is safer
        than to 1st indexed sample offset! But trick mode heavily depends on the real file offset,
        eg rewinding when subtitle is present. Seeking all tracks to movie start will make logic
        error when selecting a track to read!*/
        stream->fileOffset = self->moviList + 4;
        AVIMSG("trk %d, seek file offset to movie start:  %lld\n", stream->streamIndex,
               stream->fileOffset);
    }

    stream->indexOffset = 0;

    resetTrackReadingStatus(stream);
}

void seek2TrackEnd(AviObjectPtr self, AVStreamPtr stream) {
    if (stream->indexTab && stream->numIndexEntries)
        stream->sampleOffset = stream->cumLength; /* not be negative for trick mode */
    else
        stream->sampleOffset = -1; /* set invalid value */

    stream->fileOffset =
            self->moviEnd; /* will not return EOS, next sample reading will return EOS */
    stream->indexOffset = -1;

    resetTrackReadingStatus(stream);
}

void seek2Entry(AVStreamPtr stream, indexEntryPtr entry) {
    stream->sampleOffset = (uint64)entry->pts;
    stream->fileOffset = (uint64)entry->offset;
    stream->indexOffset = (uint64)(entry - stream->indexTab);

    resetTrackReadingStatus(stream);
}

/* verify a index of a stream or a specific tag (may not belong to any stream.
set argument "stream" or "streamTag" */
int32 verifySampleIndex(uint32 sampleSize, int64 sampleFileOffset, AVStreamPtr stream,
                        uint32 streamTag, AviInputStream* inputStream, void* appContext) {
    int32 err = PARSER_SUCCESS;
    int64 fileSize;
    int64 curFilePos;
    uint32 chunkTag;
    uint32 chunkSize;

    bool tagMatched = FALSE;

    /* back the file offset */
    curFilePos = LocalFileTell(inputStream, appContext);

    fileSize = LocalFileSize(inputStream, appContext);
    if (fileSize <= sampleFileOffset) {
        AVIMSG("index give bad sample offset %lld (exeed file scope)\n", sampleFileOffset);
        BAILWITHERROR(AVI_ERR_WRONG_INDEX_SAMPLE_OFFSET)
    }

    /* seek to the sample's position to check the size.
    Warning: fseek still return 0 while seeking position exceeds 32 bit.*/
    if (LocalFileSeek(inputStream, sampleFileOffset, SEEK_SET, appContext)) {
        AVIMSG("index give bad sample offset %lld\n", sampleFileOffset);
        BAILWITHERROR(AVI_ERR_WRONG_INDEX_SAMPLE_OFFSET)
    }

    /* stream tag match ? */
    err = read32(inputStream, &chunkTag, appContext);
    if (err)
        goto bail;

    if (stream) /* match a stream's tag */
    {
        if ((chunkTag == stream->tag) ||
            ((MEDIA_VIDEO == stream->mediaType) && (chunkTag == stream->uncompressedVideoTag)))
            tagMatched = TRUE;
    } else if (chunkTag == streamTag) /* match a specific tag */
        tagMatched = TRUE;

    if (!tagMatched) {
        AVIMSG("chunk tag is not match stream tag ");
        PrintTag(chunkTag);
        BAILWITHERROR(AVI_ERR_WRONG_INDEX_SAMPLE_OFFSET)
    }

    err = read32(inputStream, &chunkSize, appContext);
    if (err)
        goto bail;

    if (chunkSize != sampleSize) {
        AVIMSG("chunk size NOT match:  %d\n", chunkSize);
        BAILWITHERROR(AVI_ERR_WRONG_INDEX_SAMPLE_SIZE)
    }

bail:
    /* always restore the file offset no matter whether sample is valid or not*/
    if (LocalFileSeek(inputStream, curFilePos, SEEK_SET, appContext))
        BAILWITHERROR(PARSER_SEEK_ERROR)

    return err;
}

bool tryIndexAudioEntry(AVStreamPtr stream, AVStreamPtr baseStream, uint64 sampleOffset) {
    bool entryIndexed = FALSE;
    indexEntryPtr entry;

    if (baseStream) /* secondary stream */
    {
        uint64 usPTS;

        if (0 == baseStream->numIndexEntries)
            return FALSE;

        if (stream->indexDone)
            return FALSE; /* index table is full */

        getSampleTime(stream, stream->cumLength, &usPTS);

        if (usPTS > stream->usPeerEntryPTS) {
            /* index previous audio sample */
            entry = &stream->indexTab[stream->numIndexEntries];
            entry->pts = stream->prevEntry.pts;
            entry->offset = stream->prevEntry.offset;

            entryIndexed = TRUE;
            stream->numIndexEntries++;

            if (stream->numIndexEntries >= baseStream->numIndexEntries) {
                stream->indexDone = TRUE; /* index table is full, all peer entries are found */
            } else {
                /* update peer pts*/
                getSampleTime(baseStream, (uint64)baseStream->indexTab[stream->numIndexEntries].pts,
                              &stream->usPeerEntryPTS);
            }
        }

        stream->prevEntry.pts = (TIME_STAMP)stream->cumLength;
        stream->prevEntry.offset = (OFFSET)sampleOffset;
    } else /* primary stream ( no base stream) */
    {

        if ((0 == stream->numIndexEntries) ||
            (stream->cumLength >
             (stream->prevIndexedEntryPTS +
              stream->defaultAudioIndexInterval))) { /* first entry or interval is big enough */

            entry = &stream->indexTab[stream->numIndexEntries];
            entry->pts = (TIME_STAMP)stream->cumLength;
            entry->offset = (OFFSET)sampleOffset;

            entryIndexed = TRUE;
            stream->numIndexEntries++;
            stream->prevIndexedEntryPTS = stream->cumLength;
        }
    }

    return entryIndexed;
}

/* every subtitle sample is indexed, and it use PTS of the following video key frame.
It means if the 1st subtitle pts is after 1st video frame, it pts is not be 0 (2nd video key frame
pts). This is not consist with divx spec but is possible. If the 1st sample pts is not marked 0, it
will cause trouble in seeking and trick mode (1st sample missing or dead loop near BOS). So force to
set the 1st PTS to ZERO! */
bool tryIndexTextEntry(AVStreamPtr stream, AVStreamPtr baseStream, uint64 sampleOffset) {
    bool entryIndexed = TRUE; /* every text sample is indexed */
    indexEntryPtr entry;
    uint64 peerSampleOffset; /* absolute file offset */

    if ((NULL == baseStream) || (0 == baseStream->numIndexEntries))
        return FALSE;

    peerSampleOffset = baseStream->indexTab[stream->peerEntryNum].offset;

    while ((sampleOffset > peerSampleOffset)) {
        if ((stream->peerEntryNum + 1) < baseStream->numIndexEntries) {
            stream->peerEntryNum++;
            peerSampleOffset = baseStream->indexTab[stream->peerEntryNum].offset;
        } else {
            break;
        }
    }

    entry = &stream->indexTab[stream->numIndexEntries];
    entry->pts = baseStream->indexTab[stream->peerEntryNum].pts;
    if (!stream->numIndexEntries)
        entry->pts = 0;
    entry->offset = (OFFSET)sampleOffset;

    stream->numIndexEntries++;

    return entryIndexed;
}

void checkInterleavingDepth(AviObjectPtr self) {
    uint32 i;
    AVStreamPtr stream;
    AVStreamPtr firstVideoStream = NULL;
    AVStreamPtr firstAudioStream = NULL;

    /* back up all track's initial sample offset!
       File-based reading mode is not suitable for large interleaving or non-interleaving file */
    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];
        if (stream->numIndexEntries && stream->indexTab)
            stream->firstSampleFileOffset = (uint64)stream->indexTab[0].offset;

        if ((MEDIA_VIDEO == stream->mediaType) && !firstVideoStream)
            firstVideoStream = stream;

        if ((MEDIA_AUDIO == stream->mediaType) && !firstAudioStream)
            firstAudioStream = stream;
    }

    if (firstVideoStream && firstAudioStream) {
        uint64 firstVideoFileOffset = firstVideoStream->firstSampleFileOffset;
        uint64 firstAudioFileOffset = firstAudioStream->firstSampleFileOffset;
        uint64 interleaveDepthInBytes;

        interleaveDepthInBytes = (firstVideoFileOffset >= firstAudioFileOffset)
                                         ? (firstVideoFileOffset - firstAudioFileOffset)
                                         : (firstAudioFileOffset - firstVideoFileOffset);

        AVIMSG("1st video offset %lld, 1st audio offset %lld -> AV initial offset %lld bytes\n",
               firstVideoFileOffset, firstAudioFileOffset, interleaveDepthInBytes);

        if (MAX_AV_INTERLEAVE_DEPTH < interleaveDepthInBytes) {
            AVIMSG("The AV interleaving is too deep! ");
            self->isDeepInterleaving = TRUE;
        } else if (!firstVideoFileOffset || !firstAudioFileOffset) {
            if (!firstVideoFileOffset) {
                AVIMSG("Main video track is empty! ");
            } else if (!firstAudioFileOffset) {
                AVIMSG("Main audio track is empty! ");
            }
            self->isDeepInterleaving = TRUE;
        }

        if (self->isDeepInterleaving) {
            AVIMSG("Can not support file-based reading mode!\n");
            self->readMode = PARSER_READ_MODE_TRACK_BASED;
        } else {
            AVIMSG("The AV interleaving is acceptiable!\n");
        }
    }
}
