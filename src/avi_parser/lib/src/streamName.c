
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
    StreamNamePtr self = (StreamNamePtr)s;

    if (self->nameString) {
        LOCALFree(self->nameString);
        self->nameString = NULL;
    }

    destroyBaseAtom(s);
}

int32 parseStreamName(StreamNamePtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                      void* appContext) {
    int32 err = PARSER_SUCCESS;
    StreamNamePtr self = NULL;

    self = LOCALCalloc(1, sizeof(StreamName));
    TESTMALLOC(self)
    COPY_ATOM(self, proto)
    PRINT_INHERITANCE
    self->destroy = destroy;

    /* read the whole 'strn' data as stream name informaiton */
    self->nameSize = self->size - self->bytesRead;

    if ((MAX_AVI_STREAM_NAME_SIZE < self->nameSize) || (0 == self->nameSize)) {
        AVIMSG("invalid stream name length %d, overlook stream name\n", self->nameSize);
        self->nameSize = 0;
        goto bail;
    }

    self->nameString = LOCALMalloc(self->nameSize);
    TESTMALLOC(self->nameString)
    GETBYTES(self->nameString, self->nameSize);

bail:
    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);
    return err;
}
