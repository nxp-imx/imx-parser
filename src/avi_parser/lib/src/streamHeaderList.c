/*
 ***********************************************************************
 * Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
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
    StreamHeaderListPtr self = (StreamHeaderListPtr)s;

    DESTROY_ATOM(strh)
    DESTROY_ATOM(strf)
    DESTROY_ATOM(strd)
    DESTROY_ATOM(strn)
    DESTROY_ATOM(indx)

    destroyBaseAtom(s);
}

/* parse 'strl' */
int32 parseStreamHeaderList(StreamHeaderListPtr* outAtom, BaseAtomPtr proto,
                            AviInputStream* inputStream, void* appContext) {
    int32 err = PARSER_SUCCESS;
    StreamHeaderListPtr self = NULL;

    self = LOCALCalloc(1, sizeof(StreamHeaderList));
    TESTMALLOC(self)

    COPY_ATOM(self, proto)
    PRINT_INHERITANCE
    self->destroy = destroy;
#if 0
    /* stream header 'strh' */
    GET_ATOM(strh)

    /* stream format 'strh' */
    GET_ATOM(strf)
#endif
    /* stream additional header data 'strd', optional, for DRM */
    while (self->size > self->bytesRead) {
        BaseAtomPtr atom;
        uint32 bytesLeft = self->size - self->bytesRead;

        if (8 >= bytesLeft) {
            SKIPBYTES_FORWARD(bytesLeft);
        } else {
            CREATE_ATOM(atom)

            if (self->bytesRead > self->size) { /* check size integrity: child size shall not exceed
                                                   the parent size */
                AVIMSG("ERR!LIST strl child atom size %d is abnormal.Invalid atom.\n", atom->size);
                atom->destroy(atom);
                BAILWITHERROR(AVI_ERR_INVALID_ATOM)
            }

            if (StreamHeaderDataTag == atom->tag)
                self->strd = atom;

            else if (StreamNameTag == atom->tag)
                self->strn = atom;

            else if (Avi2IndexTag == atom->tag)
                self->indx = atom;
#if 1
            else if (StreamHeaderTag == atom->tag)
                self->strh = atom;
            else if (StreamFormatTag == atom->tag)
                self->strf = atom;
#endif
            else {
                AVIMSG("... Discard unnecessary atom found in LIST strl\n");
                atom->destroy(atom);
            }
        }
    }

bail:
    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);
    return err;
}
