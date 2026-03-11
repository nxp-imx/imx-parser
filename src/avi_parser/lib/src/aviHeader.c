/*
 ***********************************************************************
 * Copyright (c) 2005-2011 by Freescale Semiconductor, Inc.
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

/* parsing the main avi header 'avih'*/
// int32 parseAviHeader(AviObject * self, uint32 size)
int32 parseAviHeader(MainAviHeaderPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                     void* appContext) {
    int32 err = PARSER_SUCCESS;
    MainAviHeaderPtr self = NULL;

    self = LOCALCalloc(1, sizeof(MainAviHeader));
    TESTMALLOC(self)

    COPY_ATOM(self, proto)
    PRINT_INHERITANCE

    /* 14 DWORD (56 bytes) */
    GET32(msPerFrame);
    GET32(maxBytesPerSec);
    GET32(reserved); /* reserved */

    GET32(flags);
    GET32(totalFrames);
    GET32(initialFrames); /* intial frames */

    GET32(numStreams);
    GET32(suggestedBufferSize); /* suggested buffer size */

    GET32(width);
    GET32(height);

    GET32(scale);
    GET32(rate);

    GET32(start);
    GET32(length);

#ifdef DEBUG_SHOW_ATOM_CONTENT
    AVIMSG("\n");
    AVIMSG("Micro Secondes per frame:  %d  -> frame rate %f\n", self->msPerFrame,
           1000000.0 / self->msPerFrame);
    AVIMSG("Max bytes per second: %d\n", self->maxBytesPerSec);
    AVIMSG("Flags:  0x%08x\n", self->flags);
    AVIMSG("Total frames: %d\n", self->totalFrames);
    AVIMSG("Initial frames: %d\n", self->initialFrames);
    AVIMSG("Number of streams: %d\n", self->numStreams);
    AVIMSG("Suggested buffer size: %d\n", self->suggestedBufferSize);

    AVIMSG("Width in pixels: %d\n", self->width);
    AVIMSG("Height in pixels: %d\n", self->height);

    AVIMSG("Scale: %d\n", self->scale);
    AVIMSG("Rate: %d\n", self->rate);

    AVIMSG("Start: %d\n", self->start);
    AVIMSG("Length: %d\n", self->length);
    AVIMSG("____________________________\n");
#endif

bail:
    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);

    return err;
}
