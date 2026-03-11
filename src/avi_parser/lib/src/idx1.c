
/*
 ***********************************************************************
 * Copyright (c) 2010-2012, Freescale Semiconductor, Inc.
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

/* load AVI1.0 index (idx1) from file */

#define AVIIF_KEYFRAME 0x10

typedef struct _idx1Entry {
    uint32 id;
    uint32 flags;
    uint32 offset;
    uint32 size;
} idx1Entry, *idx1EntryPtr; /* idx1 entry */

static int32 loadPrimaryStreamIndex(AviObjectPtr aviObj, Idx1Ptr idx1, AVStreamPtr stream,
                                    idx1EntryPtr tmpBuffer,
                                    uint32 tmpBufferSize); /* size of buffer in samples */

static int32 loadSecondaryStreamsIndex(AviObjectPtr aviObj, Idx1Ptr idx1, AVStreamPtr baseStream,
                                       idx1EntryPtr tmpBuffer,
                                       uint32 tmpBufferSize); /* size of buffer in samples */

static int32 readIdx1Entries(AviInputStream* s, void* buffer, uint32 numEntriesToRead,
                             void* context);

/* parsing the movie list 'movi'.
Note: here we only check the integrity of the idx1, but not parsing its entries.
Index entries can be loaded in loadIdx1() after the user confirms to play the clip,
or imported from outside database if the clip's index has be exported before */
int32 parseIdx1(Idx1Ptr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                void* appContext) {
    int32 err = PARSER_SUCCESS;
    Idx1Ptr self = NULL;
    RiffTitlePtr riff;
    MovieListPtr movi;

    idx1Entry entry;
    uint32 numSamplesLeft;

    self = LOCALCalloc(1, sizeof(Idx1));
    TESTMALLOC(self)

    COPY_ATOM(self, proto)
    PRINT_INHERITANCE

    self->idx1Start = LocalFileTell(inputStream, appContext);
    self->numEntries = self->size / sizeof(idx1Entry);
    AVIMSG("Totallty %d samples in index table\n", self->numEntries);

    if (self->size < 16) /* at least one data chunk */
        BAILWITHERROR(AVI_ERR_WRONG_IDX1_LIST_SIZE)
    else {
        int64 fileSize, tmp;

        fileSize = LocalFileSize(inputStream, appContext);
        tmp = fileSize - self->idx1Start;
        if (tmp < self->size) {
            self->realSize = tmp;
        }
    }

    /* use relative offset or absolute offset ? verify the entry */
    riff = (RiffTitlePtr)self->parent;
    movi = (MovieListPtr)riff->movi;
    if (NULL == movi)
        BAILWITHERROR(AVI_ERR_NO_MOVIE_LIST)

    numSamplesLeft = self->numEntries;
    while (numSamplesLeft) {
        err = readData(inputStream, &entry, sizeof(idx1Entry), appContext);
        if (PARSER_SUCCESS != err) {
            AVIMSG("fail to read a valid idx1 entry\n");
            goto bail;
        }
        self->bytesRead += sizeof(idx1Entry);
        numSamplesLeft--;
        AVIMSG("idx1 entry size %d, offset %d, tag ", entry.size, entry.offset);
        PrintTag(entry.id);

        if (FALSE == isValidTag(entry.id))
            BAILWITHERROR(AVI_ERR_WRONG_CHUNK_TAG)

        err = verifySampleIndex(entry.size, entry.offset + movi->moviList, NULL, entry.id,
                                inputStream, appContext);
        if (PARSER_SUCCESS == err) {
            AVIMSG("idx1 use relative offset from movi list\n");
            self->useAbsoluteOffset = FALSE;
            break;
        } else {
            err = verifySampleIndex(entry.size, entry.offset, NULL, entry.id, inputStream,
                                    appContext);
            if (PARSER_SUCCESS == err) {
                AVIMSG("idx1 use absolute offset from the file beginnig\n");
                self->useAbsoluteOffset = TRUE;
                break;
            } else {
                AVIMSG("check next entry\n");
            }
        }
    }

    if (PARSER_SUCCESS != err) {
        AVIMSG("failed to verify idx1 entry offset\n");
        BAILWITHERROR(AVI_ERR_WRONG_INDEX_SAMPLE_OFFSET)
    }

    if (self->size > self->bytesRead) {
        uint32 bytesLeft;
        bytesLeft = self->size - self->bytesRead;
        SKIPBYTES_FORWARD(bytesLeft);
    }

bail:

    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);

    return err;
}

int32 loadIdx1(AviObjectPtr aviObj) {
    int32 err = PARSER_SUCCESS;
    RiffTitlePtr riff = (RiffTitlePtr)aviObj->riff;
    Idx1Ptr self = (Idx1Ptr)riff->idx1;

    AVStreamPtr baseStream;

    idx1EntryPtr tmpBuffer = NULL; /* temp buffer to read index table */
    uint32 tmpBufferSize;          /* in samples */

    uint32 i;

    if (0 == self->numEntries)
        BAILWITHERROR(AVI_ERR_NO_INDEX)

    if (self->size > self->realSize) {
        aviObj->bCorruptedIdx = TRUE;
    }
    /* allocate the temp buffer to read index from file */
    tmpBufferSize = (self->numEntries < MAX_IDX_ENTRY_READ_PER_TIME) ? self->numEntries
                                                                     : MAX_IDX_ENTRY_READ_PER_TIME;
    tmpBuffer = (idx1EntryPtr)LOCALMalloc(tmpBufferSize *
                                          sizeof(idx1Entry)); /* shall be 4-bytes aligned */
    TESTMALLOC(tmpBuffer)

    /******************************************************************
    1st scan:
    select primary stream and load its idex table (1st video or 1st audio)
    ******************************************************************/
    if (INVALID_TRACK_NUM == (int32)aviObj->primaryStreamNum)
        BAILWITHERROR(AVI_ERR_NO_PRIMARY_TRACK)

    if (aviObj->primaryStreamNum >= aviObj->numStreams)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    baseStream = aviObj->streams[aviObj->primaryStreamNum];
    err = loadPrimaryStreamIndex(aviObj, self, baseStream, tmpBuffer, tmpBufferSize);
    if (PARSER_SUCCESS != err) {
        AVIMSG("idx1 primary track index failed. err %d\n", err);
        goto bail;
    }

    /******************************************************************
    2nd scan:
    to establish the index for other audio streams.
    (Text tracks don't have its own index table and share video's index table)
    Even if primary track indexes no key frames, still need to scan other tracks to correct
    duration.
    ******************************************************************/
    if (1 < aviObj->numStreams) {
        err = loadSecondaryStreamsIndex(aviObj, self, baseStream, tmpBuffer, tmpBufferSize);
        if (PARSER_SUCCESS != err) {
            AVIMSG("idx1 secondary tracks index failed. err %d\n", err);
            goto bail;
        }
    }

bail:

    for (i = 0; i < aviObj->numStreams; i++) {
        AVIMSG("trk %d, %llu samples are indexed\n", i, aviObj->streams[i]->numIndexEntries);
    }

    if (tmpBuffer) {
        LOCALFree(tmpBuffer);
        tmpBuffer = NULL;
    }
    return err;
}

static int32 loadPrimaryStreamIndex(AviObjectPtr aviObj, Idx1Ptr idx1, AVStreamPtr stream,
                                    idx1EntryPtr tmpBuffer,
                                    uint32 tmpBufferSize) /* size of buffer in samples */
{
    int32 err = PARSER_SUCCESS;
    AviInputStream* inputStream = aviObj->inputStream;
    void* appContext = aviObj->appContext;
    bool protected = aviObj->protected;
    bool useAbsoluteOffset = idx1->useAbsoluteOffset;

    uint32 numSamplesLeft;
    uint32 numSamplesToRead;
    uint32 sampleSize;
    uint64 sampleOffset;
    bool indexThisSample;

    uint32 i;
    indexEntryPtr entry;
    uint32 streamTag = stream->tag;
    uint32 uncompressedVideoTag = stream->uncompressedVideoTag;
    uint32 chunkID;

    /* allocate the index table for base stream */
    stream->indexTabSize = IDX_TBL_SIZE;
    stream->indexTab = LOCALMalloc(stream->indexTabSize * sizeof(indexEntry));
    TESTMALLOC(stream->indexTab)

    /* start 1st scan */
    if (LocalFileSeek(inputStream, idx1->idx1Start, SEEK_SET, appContext))
        BAILWITHERROR(PARSER_SEEK_ERROR)

    numSamplesLeft = idx1->numEntries;
    while (numSamplesLeft) {
        numSamplesToRead = (tmpBufferSize < numSamplesLeft) ? tmpBufferSize : numSamplesLeft;

        numSamplesToRead = readIdx1Entries(inputStream, tmpBuffer, numSamplesToRead, appContext);
        if (!numSamplesToRead)
            goto bail;

        numSamplesLeft -= numSamplesToRead;

        /* filter out audio sample entries by size/byte count*/
        for (i = 0; i < numSamplesToRead; i++) {
            chunkID = tmpBuffer[i].id;
            if ((tmpBuffer[i].offset <= aviObj->fileSize) && (!aviObj->bCorruptedIdx))
                stream->lastSampleFileOffset =
                        (tmpBuffer[i].offset + tmpBuffer[i].size + aviObj->moviList);

            if ((streamTag == chunkID) ||
                ((uncompressedVideoTag == chunkID) && (MEDIA_VIDEO == stream->mediaType))) {
                indexThisSample = FALSE;
                sampleSize = tmpBuffer[i].size;
                sampleOffset = tmpBuffer[i].offset;
                if (!useAbsoluteOffset)
                    sampleOffset += (OFFSET)aviObj->moviList;

                if (MEDIA_VIDEO == stream->mediaType) /* always index 1st frame */
                {
                    if ((AVIIF_KEYFRAME & tmpBuffer[i].flags) ||
                        (0 == stream->numIndexEntries)) /* little-endian */
                    {
                        indexThisSample = TRUE;
                        entry = &stream->indexTab[stream->numIndexEntries];
                        entry->pts = (TIME_STAMP)stream->numSamples;
                        entry->offset = (OFFSET)sampleOffset;
                        if (protected)
                            entry->offset -= 18; /* step to the drm chunk ahead */
                        stream->numIndexEntries++;
                    }
                } else if (MEDIA_AUDIO == stream->mediaType) {
                    indexThisSample = tryIndexAudioEntry(stream, NULL, sampleOffset);
                } else
                    BAILWITHERROR(AVI_ERR_NO_PRIMARY_TRACK)

                if (indexThisSample &&
                    (stream->numIndexEntries >=
                     stream->indexTabSize)) { /* index table is not large enough */
                    stream->indexTabSize += IDX_TBL_SIZE;
                    stream->indexTab = LOCALReAlloc(stream->indexTab,
                                                    stream->indexTabSize * sizeof(indexEntry));
                    TESTMALLOC(stream->indexTab)
                }

                if (sampleSize > stream->maxSampleSize) {
                    if (stream->suggestedBufferSize) {
                        AVIMSG("large sample size %u found, suggested buffer size %u\n", sampleSize,
                               stream->suggestedBufferSize);
                        if (PARSER_SUCCESS != verifySampleIndex(sampleSize, sampleOffset, stream, 0,
                                                                inputStream, appContext)) {
                            AVIMSG("ERR!Idx1 abnormal sample size %d (suggested %d), entries left "
                                   "%d, i %d\n",
                                   sampleSize, stream->suggestedBufferSize, numSamplesLeft, i);
                            BAILWITHERROR(AVI_ERR_CORRUPTED_INDEX)
                        }
                    }
                    stream->maxSampleSize = sampleSize;
                }

                if (stream->isCbr)
                    stream->cumLength += sampleSize;  // tmpBuffer[i].size;
                else
                    stream->cumLength++;

                stream->numSamples++;
            }
        }
    }
    /* 1st scan end */

bail:
    return err;
}

/* load non-primary audio & subtitle streams.
Even primary track has no key frames, scan these tracks can help to correct track's duration.*/
static int32 loadSecondaryStreamsIndex(AviObjectPtr aviObj, Idx1Ptr idx1, AVStreamPtr baseStream,
                                       idx1EntryPtr tmpBuffer,
                                       uint32 tmpBufferSize) /* size of buffer in samples */
{
    int32 err = PARSER_SUCCESS;
    AviInputStream* inputStream = aviObj->inputStream;
    void* appContext = aviObj->appContext;
    bool protected = aviObj->protected;
    bool useAbsoluteOffset = idx1->useAbsoluteOffset;

    uint32 numSamplesLeft;
    uint32 numSamplesToRead;
    uint32 sampleSize;
    uint64 sampleOffset;
    bool indexThisSample;

    uint32 i;
    indexEntryPtr entry;

    AVStreamPtr stream;
    uint32 streamIndex;

    /* start 2nd scan */
    if (LocalFileSeek(inputStream, idx1->idx1Start, SEEK_SET, appContext))
        BAILWITHERROR(PARSER_SEEK_ERROR)

    numSamplesLeft = idx1->numEntries;
    while (numSamplesLeft) {
        numSamplesToRead = (tmpBufferSize < numSamplesLeft) ? tmpBufferSize : numSamplesLeft;
        numSamplesToRead = readIdx1Entries(inputStream, tmpBuffer, numSamplesToRead, appContext);
        if (!numSamplesToRead)
            goto bail;

        numSamplesLeft -= numSamplesToRead;

        /* filter out audio entries */
        for (i = 0; i < numSamplesToRead; i++) {
            uint8* tag;
            tag = (uint8*)&tmpBuffer[i].id;

            /* there may be more than one video streams */
            if ((('w' == tag[2]) && ('b' == tag[3])) || (('s' == tag[2]) && ('b' == tag[3])) ||
                (('d' == tag[2]) && ('c' == tag[3])) || (('d' == tag[2]) && ('b' == tag[3]))) {
                streamIndex = STREAM_NUM_FROM_TAG(tmpBuffer[i].id);
                if (MAX_AVI_TRACKS <= streamIndex)
                    continue;

                stream = aviObj->streams[streamIndex];

                if (baseStream == stream) {
                    continue; /* base stream already indexed */
                }

                indexThisSample = FALSE;
                sampleSize = tmpBuffer[i].size;
                sampleOffset = tmpBuffer[i].offset;
                if ((tmpBuffer[i].offset <= aviObj->fileSize) && (!aviObj->bCorruptedIdx))
                    stream->lastSampleFileOffset =
                            (tmpBuffer[i].offset + tmpBuffer[i].size + aviObj->moviList);
                if (!useAbsoluteOffset)
                    sampleOffset += (OFFSET)aviObj->moviList;

                if (NULL == stream->indexTab) { /* allocate other stream's index table, same size as
                                                   video */
                    stream->indexTabSize = baseStream->indexTabSize;
                    stream->indexTab = LOCALMalloc(stream->indexTabSize * sizeof(indexEntry));
                    TESTMALLOC(stream->indexTab)
                }

                if (!stream->indexDone) {
                    /* For audio, only index entries if peer video can be found.
                    Otherwise, only check max sample size & accumulate the size*/

                    if (MEDIA_VIDEO == stream->mediaType) /* Video: filter out video key frames and
                                                             increase index table size if need */
                    {
                        if ((AVIIF_KEYFRAME & tmpBuffer[i].flags) /* little-endian */
                            ||
                            !stream->numSamples) /* always assume 1st video frame is a key frame */
                        {
                            indexThisSample = TRUE;
                            entry = &stream->indexTab[stream->numIndexEntries];
                            entry->pts = (TIME_STAMP)stream->numSamples; /* video is always VBR */
                            entry->offset = (OFFSET)sampleOffset;
                            if (protected)
                                entry->offset -= 18; /* step to the drm chunk ahead */

                            stream->numIndexEntries++;
                        }
                    } else if (MEDIA_AUDIO ==
                               stream->mediaType) /* audio: find entries for their video peer */
                    {
                        if (baseStream->numIndexEntries)
                            indexThisSample = tryIndexAudioEntry(stream, baseStream, sampleOffset);
                    } else /* text (subtitle) */
                    {
                        if (baseStream->numIndexEntries)
                            indexThisSample = tryIndexTextEntry(stream, baseStream, sampleOffset);
                    }

                    if (indexThisSample && (stream->numIndexEntries >= stream->indexTabSize)) {
                        /* index table is not large enough, enlarge it! */
                        stream->indexTabSize += IDX_TBL_SIZE;
                        stream->indexTab = LOCALReAlloc(stream->indexTab,
                                                        stream->indexTabSize * sizeof(indexEntry));
                        TESTMALLOC(stream->indexTab)
                    }
                }

                if (sampleSize > stream->maxSampleSize) {
                    if (stream->suggestedBufferSize) {
                        if (PARSER_SUCCESS != verifySampleIndex(sampleSize, sampleOffset, stream, 0,
                                                                inputStream, appContext)) {
                            AVIMSG("ERR!Idx1 abnormal sample size %d (suggested %d), entries left "
                                   "%d, i %d\n",
                                   sampleSize, stream->suggestedBufferSize, numSamplesLeft, i);
                            BAILWITHERROR(AVI_ERR_CORRUPTED_INDEX)
                        }
                    }
                    stream->maxSampleSize = sampleSize;
                }

                if (stream->isCbr)
                    stream->cumLength += sampleSize;  // tmpBuffer[i].size;
                else
                    stream->cumLength++;

                stream->numSamples++;
            } else {
                /* unknown stream type, just skip */
                continue;
            }

            if (streamIndex >= aviObj->numStreams)
                continue;
        }
    }

    /* If the audio track has has only 1 audio sample. It will be lost in the scan. index the 1st
    sample! Only if primary stream has key frames, prevEntry is valid!*/
    for (i = 0; i < aviObj->numStreams; i++) {
        stream = aviObj->streams[i];
        if (stream == baseStream)
            continue;

        if ((!stream->numIndexEntries) && stream->numSamples &&
            (MEDIA_AUDIO == stream->mediaType) && (baseStream->numIndexEntries)) {
            entry = &stream->indexTab[0];
            entry->pts = stream->prevEntry.pts;
            entry->offset = stream->prevEntry.offset;
            stream->numIndexEntries = 1;
            AVIMSG("Audio trk %d is not empty but no sample is indexed. Index the last one! pts "
                   "%u, offset %u\n",
                   i, (uint32)entry->pts, (uint32)entry->offset);
        }
    }

    AVIMSG("idx1 read OK\n");

bail:
    return err;
}

static int32 readIdx1Entries(AviInputStream* s, void* buffer, uint32 numEntriesToRead,
                             void* context) {
    int32 size, sizeRead;
    int32 numEntriesGot = 0;

    size = numEntriesToRead * sizeof(idx1Entry);
    sizeRead = LocalFileRead(s, buffer, size, context);

    if (0 < sizeRead)
        numEntriesGot = sizeRead / sizeof(idx1Entry);

    if (!numEntriesGot) {
        AVIMSG("idx 1 is truncated, no more entries!\n");
    }

    return numEntriesGot;
}
