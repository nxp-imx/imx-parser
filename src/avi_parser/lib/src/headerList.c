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

static void destroy(BaseAtomPtr s) {
    HeaderListPtr self = (HeaderListPtr)s;
    uint32 i;

    DESTROY_ATOM(avih)

    for (i = 0; i < self->numStreams; i++) {
        DESTROY_ATOM(strl[i])
    }
    self->numStreams = 0;

    destroyBaseAtom(s);
}

/* parsing the header list 'hdrl' */
int32 parseHeaderList(HeaderListPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                      void* appContext) {
    int32 err = PARSER_SUCCESS;
    HeaderListPtr self = NULL;

    self = LOCALCalloc(1, sizeof(HeaderList));
    TESTMALLOC(self)

    COPY_ATOM(self, proto)
    PRINT_INHERITANCE

    self->destroy = destroy;
    self->numStreams = 0;

    /* main avi header 'avih' */
    GET_ATOM(avih)

    while (self->size > (self->bytesRead + 8)) {
        if (MAX_AVI_TRACKS <= self->numStreams) {
            AVIMSG("... Warning: MAX stream number reached %d. Following tracks will be "
                   "overlooked!\n",
                   self->numStreams);
            break;
        }

        /* header list 'strl' */
        GET_ATOM(strl[self->numStreams])

        if (StreamHeaderListTag !=
            self->strl[self->numStreams]->type) { /* not a stream header list */
            AVIMSG("... Discard non-strl LIST atom found in LIST hdrl\n");
            self->strl[self->numStreams]->destroy(self->strl[self->numStreams]);
            self->strl[self->numStreams] = NULL;
        } else
            self->numStreams++;
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
