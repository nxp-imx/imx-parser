
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

typedef struct _stdIndexEntry {
    uint32 offset;
    uint32 size;                    /* bit 31 is set if this is NOT a key frame */
} stdIndexEntry, *stdIndexEntryPtr; /* standard index entry */

#define AVI2_KEY_FRAME 0X80000000 /* the bit 32  of "size" is set to 1 if this is NOT a key frame \
                                   */

int32 parseStandardIndex(StdIndexPtr* outAtom, BaseIndexPtr proto, AviInputStream* inputStream,
                         void* appContext) {
    int32 err = PARSER_SUCCESS;
    StdIndexPtr self = NULL;

    self = (StdIndexPtr)LOCALCalloc(1, sizeof(StdIndex));
    TESTMALLOC(self)

    COPY_BASE_INDEX(self, proto)
    PRINT_INHERITANCE

    if (self->size < MIN_INDEX_SIZE)
        BAILWITHERROR(AVI_ERR_WRONG_AVI2_INDEX_SIZE)

    GET64(baseOffset);
    GET32(reserved3);

#ifdef DEBUG_SHOW_ATOM_CONTENT
    AVIMSG("standard index, base offset: %lld\n", self->baseOffset);
#endif

    self->entriesFileOffet = LocalFileTell(inputStream, appContext);

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

/**
 * function to load the super index of a stream
 *
 * @param aviObj [in] Pointer of the avi core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param indx [in] index atom of this track.
 * @param videoStream [in] For a video track, this is NULL. Will determine the index table size by
 * finding all the key frames. For a non-video track, this is the pointer to its peer video track.
 * This track's index table size is same as the video peer. And each entry is a peer of the video
 * index entry.
 * @return
 */
int32 loadStandardIndex(AviObjectPtr aviObj, uint32 trackNum, StdIndexPtr indx,
                        AVStreamPtr baseStream) {
    int32 err = PARSER_SUCCESS;
    StdIndexPtr self = indx;
    void* appContext = aviObj->appContext;
    AviInputStream* inputStream = aviObj->inputStream;
    bool protected = aviObj->protected;

    stdIndexEntryPtr tmpBuffer = NULL; /* temp buffer to read index table */
    uint32 tmpBufferSize;              /* in entries */
    uint32 numSamplesLeft;
    uint32 numSamplesToRead;

    AVStreamPtr stream = aviObj->streams[trackNum];

    uint32 i;
    uint32 sampleSize; /* real sample size, removing the key frame flag */
    int64 sampleOffset;
    indexEntryPtr entry;
    bool indexThisSample;

    AVIMSG("base offset %lld\n", self->baseOffset);
    if (self->baseOffset > aviObj->fileSize)
        BAILWITHERROR(AVI_ERR_CORRUPTED_INDEX)

    if (0 == self->entriesInUse)
        BAILWITHERROR(AVI_ERR_EMPTY_INDEX)

    /* If it's first std index chunk for this track, allocate memory for index table.
    otherwise just using untill full */
    if (NULL == stream->indexTab) {
        if (0 != stream->indexTabSize)
            BAILWITHERROR(PARSER_ERR_UNKNOWN)

        if (!baseStream) {
            /* primary(base) track's index table size is not pre-known, depending on the number of
             * key frames or pts interval */
            stream->indexTabSize = IDX_TBL_SIZE;
        } else {
            /* secondary track's index table size is usually same as the peer primary track, can
             * grow later */
            stream->indexTabSize = baseStream->indexTabSize;
        }
        stream->indexTab = LOCALMalloc(stream->indexTabSize * sizeof(indexEntry));
        TESTMALLOC(stream->indexTab)
    }

    tmpBufferSize = (self->entriesInUse < MAX_IDX_ENTRY_READ_PER_TIME)
                            ? self->entriesInUse
                            : MAX_IDX_ENTRY_READ_PER_TIME;
    tmpBuffer = (stdIndexEntryPtr)LOCALMalloc(tmpBufferSize *
                                              sizeof(stdIndexEntry)); /* shall be 4-bytes aligned */
    TESTMALLOC(tmpBuffer)

    if (LocalFileSeek(inputStream, self->entriesFileOffet, SEEK_SET, appContext))
        BAILWITHERROR(PARSER_READ_ERROR)

    numSamplesLeft = self->entriesInUse;
    while (numSamplesLeft) {
        numSamplesToRead = (tmpBufferSize < numSamplesLeft) ? tmpBufferSize : numSamplesLeft;
        err = readData(inputStream, tmpBuffer, numSamplesToRead * sizeof(stdIndexEntry),
                       appContext);
        if (PARSER_SUCCESS != err) {
            AVIMSG("fail\n");
            goto bail;
        }
        numSamplesLeft -= numSamplesToRead;

        /* filter out video key entries */
        for (i = 0; i < numSamplesToRead; i++) {
            indexThisSample = FALSE;
            sampleSize = tmpBuffer[i].size & (~AVI2_KEY_FRAME);
            sampleOffset = tmpBuffer[i].offset + (OFFSET)self->baseOffset - 8;
            if (sampleOffset <= (int64)aviObj->fileSize)
                stream->lastSampleFileOffset = sampleOffset;

            if (!stream->indexDone) {
                if (MEDIA_VIDEO == stream->mediaType) /* Video: filter out video key frames and
                                                         increase index table size if need */
                {
                    if (0 == (AVI2_KEY_FRAME & tmpBuffer[i].size)) {
                        entry = &stream->indexTab[stream->numIndexEntries];
                        entry->pts = (TIME_STAMP)stream->numSamples; /* video is always VBR */
                        entry->offset = (OFFSET)sampleOffset;
                        if (protected)
                            entry->offset -= 18; /* step to the drm chunk ahead */

                        indexThisSample = TRUE;
                        stream->numIndexEntries++;
                    }
                } else if (MEDIA_AUDIO ==
                           stream->mediaType) /* audio: find entries for their video peer */
                {
                    indexThisSample = tryIndexAudioEntry(stream, baseStream, sampleOffset);
                } else /* text (subtitle) */
                {
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
                    int64 sampleOffset = tmpBuffer[i].offset + self->baseOffset - 8;
                    if (PARSER_SUCCESS != verifySampleIndex(sampleSize, sampleOffset, stream, 0,
                                                            inputStream, appContext)) {
                        AVIMSG("ERR!Idx abnormal sample size %d (suggested %d), entries left %d, i "
                               "%d\n",
                               sampleSize, stream->suggestedBufferSize, numSamplesLeft, i);
                        BAILWITHERROR(AVI_ERR_CORRUPTED_INDEX)
                    }
                }
                stream->maxSampleSize = sampleSize;
            }

            if (stream->isCbr)
                stream->cumLength += sampleSize;
            else
                stream->cumLength++;

            stream->numSamples++;
        }
    }

    AVIMSG("\nTrack %d, %llu samples are indexed\n", trackNum, stream->numIndexEntries);

bail:
    if (tmpBuffer) {
        LOCALFree(tmpBuffer);
        tmpBuffer = NULL;
    }

    return err;
}
