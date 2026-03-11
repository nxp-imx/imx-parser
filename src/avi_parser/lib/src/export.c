/*
 ***********************************************************************
 * Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include <string.h>

#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "avi_parser_api.h"
#include "avi_utils.h"
#include "avi.h"

#define AVI_INDEX_PREFIX "fsl_avi_" /* prefix of exported avi index, 8 bytes */
#define AVI_INDEX_PREFIX_SIZE 8

#define AVI_INDEX_VERSION                                             \
    2 /* version of exported index format. This format may change and \
      exported file index with a lower version shall not be used. */
#define AVI_INDEX_VERSION_SIZE 4

#define TRACK_INDEX_HEADER_SIZE                              \
    44 /* size(4) + trackNum (4) + maxSampleSize (4)         \
        + numEntries (8) + cumLength (8) +track duration (8) \
        + firstSampleOffset (8) */

#define MOVIE_INDX_HEADER_SIZE (AVI_INDEX_PREFIX_SIZE + AVI_INDEX_VERSION_SIZE)

/************************************************************

struct _track_index_entry
{
    uint32 trackIndexSizeInBytes;
    uint32 trackNum;
    uint32 maxSampleSize;
    uint64 numEntries;
    uint64 usTrackDuration;
    uint64 firstSampleOffset;

    indexEntry aChunkIndex[]

}aTrackIndex;

trackNum: track number, 0-based
maxSampleSize: max sample size of this track

*************************************************************/
static void export32(uint8* ptr, uint32 val) {
    *ptr = (uint8)(val & 0xff);
    ptr++;

    *ptr = (uint8)((val & 0xff00) >> 8);
    ptr++;

    *ptr = (uint8)((val & 0xff0000) >> 16);
    ptr++;
    *ptr = (uint8)((val & 0xff000000) >> 24);
    ptr++;
}

static void export64(uint8* ptr, uint64 val) {
    *ptr = (uint8)(val & 0xff);
    ptr++;

    *ptr = (uint8)((val & 0xff00) >> 8);
    ptr++;

    *ptr = (uint8)((val & 0xff0000) >> 16);
    ptr++;

    *ptr = (uint8)((val & 0xff000000) >> 24);
    ptr++;

    *ptr = (uint8)((val & 0xff00000000ULL) >> 32); /* ULL, unsigned long long */
    ptr++;

    *ptr = (uint8)((val & 0xff0000000000ULL) >> 40);
    ptr++;

    *ptr = (uint8)((val & 0xff000000000000ULL) >> 48);
    ptr++;

    *ptr = (uint8)((val & 0xff00000000000000ULL) >> 56);
    ptr++;
}

static void import32(uint8* ptr, uint32* val) {
    uint32 val32 = 0;

    val32 += (uint32)(*ptr);
    ptr++;

    val32 += ((uint32)(*ptr)) << 8;
    ptr++;

    val32 += ((uint32)(*ptr)) << 16;
    ptr++;

    val32 += ((uint32)(*ptr)) << 24;
    ptr++;

    *val = val32;
}

static void import64(uint8* ptr, uint64* val) {
    uint64 val64 = 0;

    val64 += (uint64)(*ptr);
    ptr++;

    val64 += ((uint64)(*ptr)) << 8;
    ptr++;

    val64 += ((uint64)(*ptr)) << 16;
    ptr++;

    val64 += ((uint64)(*ptr)) << 24;
    ptr++;

    val64 += ((uint64)(*ptr)) << 32;
    ptr++;

    val64 += ((uint64)(*ptr)) << 40;
    ptr++;

    val64 += ((uint64)(*ptr)) << 48;
    ptr++;

    val64 += ((uint64)(*ptr)) << 56;
    ptr++;

    *val = val64;
}

static int32 importTrackIndex(AviObjectPtr aviObj, uint32 trackNum, uint8* buffer, uint32 size) {
    int32 err = PARSER_SUCCESS;
    AVStreamPtr stream;
    uint32 trackNumImported;
    uint32 indexSizeImported;
    uint32 entriesSize;

    if (trackNum >= aviObj->numStreams)
        BAILWITHERROR(AVI_ERR_WRONG_TRACK_NUM)

    if (size < TRACK_INDEX_HEADER_SIZE)
        BAILWITHERROR(PARSER_INSUFFICIENT_DATA)

    stream = aviObj->streams[trackNum];
    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (stream->numIndexEntries)
        BAILWITHERROR(AVI_ERR_INDEX_ALREADY_LOADED) /* already loaded ? */

    AVIMSG("import trk %d:\n", trackNum);
    import32(buffer, &indexSizeImported);
    AVIMSG("\tindex size: %d\n", indexSizeImported);
    buffer += 4;

    import32(buffer, &trackNumImported);
    if (trackNumImported != trackNum)
        BAILWITHERROR(AVI_ERR_WRONG_TRACK_NUM)
    buffer += 4;

    import32(buffer, &stream->maxSampleSize);
    buffer += 4;
    AVIMSG("\tmax sample size: %u\n", stream->maxSampleSize);

    import64(buffer, &stream->numIndexEntries);
    AVIMSG("\tnumber of entries: %lld\n", stream->numIndexEntries);
    buffer += 8;

    import64(buffer, &stream->cumLength);
    AVIMSG("\tcumLength of entries: %lld\n", stream->cumLength);
    buffer += 8;

    import64(buffer, &stream->usDuration);
    AVIMSG("\tduration in us: %lld\n", stream->usDuration);
    buffer += 8;

    import64(buffer, &stream->firstSampleFileOffset);
    AVIMSG("\t1st sample file offset: %lld bytes\n", stream->firstSampleFileOffset);
    buffer += 8;

    entriesSize = (uint32)stream->numIndexEntries * sizeof(indexEntry);
    if (size < (TRACK_INDEX_HEADER_SIZE + entriesSize))
        BAILWITHERROR(PARSER_INSUFFICIENT_DATA)

    if (entriesSize) {
        stream->indexTabSize = entriesSize;
        stream->indexTab = LOCALMalloc(entriesSize);
        TESTMALLOC(stream->indexTab)

        memcpy(stream->indexTab, buffer, entriesSize);
    }

bail:
    return err;
}

static int32 exportTrackIndex(AviObjectPtr aviObj, uint32 trackNum, uint8* buffer, uint32* size) {
    int32 err = PARSER_SUCCESS;
    AVStreamPtr stream;
    uint32 indexTblSize; /* total size of the index table to export, in bytes */
    uint32 entriesSize;

    if (trackNum >= aviObj->numStreams)
        BAILWITHERROR(AVI_ERR_WRONG_TRACK_NUM)

    /* calculate the total size of index table needed at first */
    stream = aviObj->streams[trackNum];
    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    indexTblSize = TRACK_INDEX_HEADER_SIZE;
    entriesSize = (uint32)(stream->numIndexEntries * sizeof(indexEntry));
    indexTblSize += entriesSize;

    if (NULL == buffer) /* tell the buffer size needed */
    {
        *size = indexTblSize;
        AVIMSG("export trk %d, index table size: %u\n", trackNum, indexTblSize);
        goto bail;
    }

    if (indexTblSize > *size) /* buffer size as input */
    {
        AVIMSG("ERR!Buffer size %d is too small to export the index, at least %d bytes are "
               "needed!\n",
               *size, indexTblSize);
        BAILWITHERROR(PARSER_INSUFFICIENT_MEMORY)
    }
    *size = indexTblSize; /* actual size as output */

    export32(buffer, indexTblSize);
    buffer += 4;
    export32(buffer, trackNum);
    buffer += 4;
    export32(buffer, stream->maxSampleSize);
    buffer += 4;
    export64(buffer, stream->numIndexEntries);
    buffer += 8;
    export64(buffer, stream->cumLength);
    buffer += 8;
    export64(buffer, stream->usDuration);
    buffer += 8;
    export64(buffer, stream->firstSampleFileOffset);
    buffer += 8;

    if (entriesSize) { /* some track may have no index and never be seeked */
        memcpy(buffer, stream->indexTab, entriesSize);
    }

bail:
    return err;
}

int32 importIndex(AviObjectPtr self, uint8* buffer, uint32 size) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum;
    AVStreamPtr stream;
    uint32 trackIndexSizeInBytes;
    uint8 prefix[AVI_INDEX_PREFIX_SIZE + 1] = {0};
    uint32 indexVersion;

    if (self->indexLoaded)
        BAILWITHERROR(AVI_ERR_INDEX_ALREADY_LOADED)

    if (INVALID_TRACK_NUM == (int32)self->primaryStreamNum)
        BAILWITHERROR(AVI_ERR_NO_PRIMARY_TRACK)

    if (MOVIE_INDX_HEADER_SIZE > (int32)self->size) {
        AVIMSG("Size of index table to import %d is invalid! At least %d bytes\n",
               (int32)self->size, MOVIE_INDX_HEADER_SIZE);
        BAILWITHERROR(PARSER_INSUFFICIENT_DATA)
    }

    /* import and check prefix and version */
    memcpy(prefix, buffer, AVI_INDEX_PREFIX_SIZE);
    if (strcmp((char*)prefix, AVI_INDEX_PREFIX)) {
        AVIMSG("Index prefix string mismatch! Shall be %s\n", AVI_INDEX_PREFIX);
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)
    }
    buffer += AVI_INDEX_PREFIX_SIZE;
    size -= AVI_INDEX_PREFIX_SIZE;

    import32(buffer, &indexVersion);
    AVIMSG("version of index to import: %u\n", indexVersion);
    if (AVI_INDEX_VERSION != indexVersion) {
        AVIMSG("Index version mismatch! Shall be %u\n", AVI_INDEX_VERSION);
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)
    }
    buffer += AVI_INDEX_VERSION_SIZE;
    size -= AVI_INDEX_VERSION_SIZE;

    /* import index track by track */
    for (trackNum = 0; trackNum < self->numStreams; trackNum++) {
        stream = self->streams[trackNum];
        if (NULL == stream)
            BAILWITHERROR(PARSER_ERR_UNKNOWN)

        err = importTrackIndex(self, trackNum, buffer, size);
        if (err)
            goto bail;

        import32(buffer, &trackIndexSizeInBytes);

        buffer += trackIndexSizeInBytes;
        size -= trackIndexSizeInBytes;
    }

    self->seekable = TRUE;
    self->indexLoaded = TRUE;

    checkInterleavingDepth(self);

    /* really index any  key frames ? Only check the base stream */
    if (INVALID_TRACK_NUM == (int32)self->primaryStreamNum)
        BAILWITHERROR(AVI_ERR_NO_PRIMARY_TRACK)

    stream = self->streams[self->primaryStreamNum];
    if (0 == stream->numIndexEntries) {
        AVIMSG("Primary trk %u, No sync sample is actually indexed although index table is "
               "present\n",
               stream->streamIndex);
        self->seekable = FALSE;
    }

bail:
    return err;
}

/* buffer size as input, data size as output */
int32 exportIndex(AviObjectPtr self, uint8* buffer, uint32* size) {
    int32 err = PARSER_SUCCESS;
    uint32 trackNum;

    uint32 trackIndexSizeInBytes;
    uint32 totalIndexSizeInBytes;

    if (FALSE == self->indexLoaded)
        BAILWITHERROR(AVI_ERR_NO_INDEX) /*index table not loaded yet */

    if (!buffer) /* only get the index table size */
    {
        if (!self->indexSizeToExport) {
            totalIndexSizeInBytes = AVI_INDEX_PREFIX_SIZE + AVI_INDEX_VERSION_SIZE;
            for (trackNum = 0; trackNum < self->numStreams; trackNum++) {
                err = exportTrackIndex(self, trackNum, buffer, &trackIndexSizeInBytes);
                if (err)
                    break;

                AVIMSG("trk %d, index table size %u\n", trackNum, trackIndexSizeInBytes);
                totalIndexSizeInBytes += trackIndexSizeInBytes;
            }

            AVIMSG("Total index table size %u\n", totalIndexSizeInBytes);
            self->indexSizeToExport = totalIndexSizeInBytes;
        }

        *size = self->indexSizeToExport;
    } else {
        uint32 bufferSizeAvailable;

        bufferSizeAvailable = *size;

        if (!self->indexSizeToExport) {
            AVIMSG("Not query index table size yet!\n");
            BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER);
        }

        if (bufferSizeAvailable < self->indexSizeToExport) {
            AVIMSG("Memory is not enough to export index table! Need %u bytes.\n",
                   self->indexSizeToExport);
            BAILWITHERROR(PARSER_INSUFFICIENT_MEMORY);
        }

        /* export prefix and version */
        memcpy(buffer, AVI_INDEX_PREFIX, AVI_INDEX_PREFIX_SIZE);
        buffer += AVI_INDEX_PREFIX_SIZE;

        export32(buffer, AVI_INDEX_VERSION);
        buffer += AVI_INDEX_VERSION_SIZE;

        totalIndexSizeInBytes = AVI_INDEX_PREFIX_SIZE + AVI_INDEX_VERSION_SIZE;

        /* export index track by track */
        for (trackNum = 0; trackNum < self->numStreams; trackNum++) {
            trackIndexSizeInBytes = bufferSizeAvailable;
            err = exportTrackIndex(self, trackNum, buffer, &trackIndexSizeInBytes);
            if (err)
                break;

            AVIMSG("trk %d, index table size %u\n", trackNum, trackIndexSizeInBytes);
            totalIndexSizeInBytes += trackIndexSizeInBytes;
            bufferSizeAvailable -= trackIndexSizeInBytes;
            buffer += trackIndexSizeInBytes;
        }

        if (totalIndexSizeInBytes != self->indexSizeToExport) {
            AVIMSG("Index table size mismatch! %u vs %u byts\n", totalIndexSizeInBytes,
                   self->indexSizeToExport);
            BAILWITHERROR(PARSER_ERR_UNKNOWN)
        }

        AVIMSG("Total index table size %u\n", totalIndexSizeInBytes);
        *size = totalIndexSizeInBytes;
    }

bail:
    return err;
}
