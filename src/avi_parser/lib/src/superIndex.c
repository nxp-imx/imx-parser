
/*
 ***********************************************************************
 * Copyright (c) 2010-2011,2016 Freescale Semiconductor Inc.,
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

static void destroy(BaseAtomPtr s) {
    SuperIndexPtr self = (SuperIndexPtr)s;

    if (self->entries) {
        alignedFree(self->entries);
        self->entries = NULL;
    }

    destroyBaseAtom(s);
}

int32 parseSuperIndex(SuperIndexPtr* outAtom, BaseIndexPtr proto, AviInputStream* inputStream,
                      void* appContext) {
    int32 err = PARSER_SUCCESS;
    SuperIndexPtr self = NULL;

    self = (SuperIndexPtr)LOCALCalloc(1, sizeof(SuperIndex));
    TESTMALLOC(self)

    COPY_BASE_INDEX(self, proto)
    PRINT_INHERITANCE
    self->destroy = destroy;

    if (self->size < MIN_INDEX_SIZE)
        BAILWITHERROR(AVI_ERR_WRONG_AVI2_INDEX_SIZE)

    GETBYTES((uint8*)self->reserved, 12);

    if (4 != self->longsPerEntry) /* 4 dword */
        BAILWITHERROR(AVI_ERR_WRONG_AVI2_INDEX_ENTRY_SIZE)

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
int32 loadSuperIndex(AviObjectPtr aviObj, uint32 trackNum, SuperIndexPtr indx,
                     AVStreamPtr videoStream) {
    int32 err = PARSER_SUCCESS;
    SuperIndexPtr self = indx;
    void* appContext = aviObj->appContext;
    AviInputStream* inputStream = aviObj->inputStream;
    uint32 i;
    uint32 indexTabSize;

    if (LocalFileSeek(inputStream, self->entriesFileOffet, SEEK_SET, appContext))
        BAILWITHERROR(PARSER_READ_ERROR)

    if (0 == self->entriesInUse)
        BAILWITHERROR(AVI_ERR_EMPTY_INDEX)

    indexTabSize = self->entriesInUse * sizeof(SuperIndexEntry);
    self->entries = (SuperIndexEntryPtr)alignedMalloc(indexTabSize, sizeof(uint64));
    TESTMALLOC(self->entries)
    GETBYTES((uint8*)self->entries, indexTabSize);

#ifdef DEBUG_SHOW_ATOM_CONTENT
    AVIMSG("AVI2.0 super index, Size per entry: %d, ", self->longsPerEntry);
    AVIMSG("Index type: %d, subtype: %d, ", self->indexType, self->indexSubType);
    AVIMSG("Entries in use: %d, chunk ID: ", self->entriesInUse);
    PrintTag(self->chunkId);
    {
        int32 i;
        for (i = 0; i < self->entriesInUse; i++) {
            AVIMSG("\tEntry %d: size %d, duration %d, offset %lld\n", i, self->entries[i].size,
                   self->entries[i].duration, self->entries[i].offset);
        }
    }
#endif

    for (i = 0; i < self->entriesInUse; i++) {
        BaseAtomPtr atom = NULL;

        if (LocalFileSeek(inputStream, self->entries[i].offset, SEEK_SET, appContext))
            BAILWITHERROR(PARSER_READ_ERROR)

        CREATE_ATOM(atom)

        if (isAvi2IndexTag(atom->tag) &&
            (AVI_INDEX_OF_CHUNKS == ((BaseIndexPtr)atom)->indexType)) /* TODO: it still can be super
                                                                         index. Not support yet */
            err = loadStandardIndex(aviObj, trackNum, (StdIndexPtr)atom, videoStream);
        else
            err = AVI_ERR_INDEX_TYPE_NOT_SUPPORTED;

        atom->destroy((BaseAtomPtr)atom); /* need not retain the standard index atom */
        atom = NULL;
    }

    AVIMSG("\nTrk %d, %llu samples are indexed\n", trackNum, aviObj->streams[trackNum]->numIndexEntries);

bail:

    return err;
}
