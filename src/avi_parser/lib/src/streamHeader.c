
/*
 ***********************************************************************
 * Copyright (c) 2010-2011, Freescale Semiconductor Inc.,
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

int32 parseStreamHeader(StreamHeaderPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                        void* appContext) {
    int32 err = PARSER_SUCCESS;
    StreamHeaderPtr self = NULL;
    uint32 bytesLeft;

    self = LOCALCalloc(1, sizeof(StreamHeader));
    TESTMALLOC(self)
    COPY_ATOM(self, proto)
    PRINT_INHERITANCE

    GET32(fccType);
    GET32(fccHandler);
    GET32(flags);
    GET16(priority);
    GET16(language);
    GET32(initialFrames);

    GET32(scale);
    GET32(rate);
    GET32(start);
    GET32(length);
    GET32(suggestedBufferSize);
    GET32(quality);
    GET32(sampleSize);

    if (self->size > self->bytesRead + 8) {
        GET16(frame.left);
        GET16(frame.top);
        GET16(frame.right);
        GET16(frame.bottom);
    }

    AVIMSG("suggested buffer size: %d\n", self->suggestedBufferSize);

#ifdef DEBUG_SHOW_ATOM_CONTENT
    AVIMSG("\n");
    AVIMSG("stream type: ");
    PrintTag(self->fccType);
    AVIMSG("handler: ");
    PrintTag(self->fccHandler);
    AVIMSG("Flags:  0x%08x\n", self->flags);
    AVIMSG("prority: %d\n", self->priority);
    AVIMSG("language: 0x%x\n", self->language);
    AVIMSG("Initial frames: %d\n", self->initialFrames);

    AVIMSG("scale: %d\n", self->scale);
    AVIMSG("rate: %d\n", self->rate);
    AVIMSG("start: %d\n", self->start);
    AVIMSG("length: %d\n", self->length);

    AVIMSG("quality: %d\n", self->quality);
    AVIMSG("sample size: %d\n", self->sampleSize);
    AVIMSG("frame: (%d, %d, %d, %d)\n", self->frame.left, self->frame.top, self->frame.right,
           self->frame.bottom);
    AVIMSG("____________________________\n");
#endif

    bytesLeft = self->size - self->bytesRead;
    if (self->size > self->bytesRead) {
        SKIPBYTES_FORWARD(bytesLeft);
    }

bail:
    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);
    return err;
}
