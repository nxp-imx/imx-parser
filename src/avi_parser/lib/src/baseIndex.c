
/*
 ***********************************************************************
 * Copyright (c) 2010-2011, Freescale Semiconductor Inc.,
 * Copyright 2025-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "avi_parser_api.h"
#include "avi_utils.h"
#include "avi.h"

int32 parseAvi2BaseIndex(BaseIndexPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                         void* appContext) {
    int32 err = PARSER_SUCCESS;
    BaseIndexPtr self = NULL;

    self = (BaseIndexPtr)LOCALCalloc(1, sizeof(BaseIndex));
    TESTMALLOC(self)

    COPY_ATOM(self, proto)
    PRINT_INHERITANCE

    if (self->size < MIN_INDEX_SIZE)
        BAILWITHERROR(AVI_ERR_WRONG_AVI2_INDEX_SIZE)

    GET16(longsPerEntry);
    GET8(indexSubType);
    GET8(indexType);
    GET32(entriesInUse);
    GET32(chunkId);

#ifdef DEBUG_SHOW_ATOM_CONTENT
    AVIMSG("AVI2.0 index, Size per entry: %d, ", self->longsPerEntry);
    AVIMSG("Index type: %d, subtype: %d, ", self->indexType, self->indexSubType);
    AVIMSG("Entries in use: %d, chunk ID: ", self->entriesInUse);
    PrintTag(self->chunkId);
#endif

    if (AVI_INDEX_OF_INDEXES == self->indexType) {
        err = parseSuperIndex((SuperIndexPtr*)outAtom, self, inputStream, appContext);
        self->destroy((BaseAtomPtr)self); /* destroy the proto "self", it's not the output */
        return err;
    } else if (AVI_INDEX_OF_CHUNKS == self->indexType) {
        err = parseStandardIndex((StdIndexPtr*)outAtom, self, inputStream, appContext);
        self->destroy((BaseAtomPtr)self); /* destroy the proto "self", it's not the output */
        return err;
    } else {
        GETBYTES((uint8*)self->reserved, 12);
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
