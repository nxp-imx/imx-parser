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
    StreamHeaderDataPtr self = (StreamHeaderDataPtr)s;

    if (self->drmInfo) {
        LOCALFree(self->drmInfo);
        self->drmInfo = NULL;
    }

    destroyBaseAtom(s);
}

int32 parseStreamHeaderData(StreamHeaderDataPtr* outAtom, BaseAtomPtr proto,
                            AviInputStream* inputStream, void* appContext) {
    int32 err = PARSER_SUCCESS;
    StreamHeaderDataPtr self = NULL;

    self = LOCALCalloc(1, sizeof(StreamHeaderData));
    TESTMALLOC(self)

    COPY_ATOM(self, proto)
    PRINT_INHERITANCE
    self->destroy = destroy;

    /* check whether it's DRM info data or just some stream data for other usage */
    GET32(version);
    if ((AVI_DRM_V1 != self->version) && (AVI_DRM_V2 != self->version)) {
        AVIMSG("Not DRM info\n");
        self->version = AVI_DRM_INVALID_VERSION;
        goto bail;
    }

    GET32(drmInfoSize);
    if (MAX_AVI_DRM_INFO_SIZE < self->drmInfoSize) {
        AVIMSG("Not DRM info\n");
        self->drmInfoSize = 0;
        goto bail;
    }

    if (self->drmInfoSize) {
        self->drmInfo = LOCALCalloc(1, self->drmInfoSize);
        TESTMALLOC(self->drmInfo)

        GETBYTES(self->drmInfo, self->drmInfoSize);
    } else
        BAILWITHERROR(AVI_ERR_WRONG_DRM_INFO_SIZE)

#ifdef DEBUG_SHOW_ATOM_CONTENT
    AVIMSG("\n");
    AVIMSG("DRM version:  %d, info size: %d\n", self->version, self->drmInfoSize);
    AVIMSG("____________________________\n");
#endif

bail:

    /* Must skip the left bytes, strd may be used for non-DRM inforamtion such as codec description,
    and there may be padding bytes following DRM info.*/
    if (self->size > self->bytesRead) {
        uint32 bytesLeft;

        bytesLeft = self->size - self->bytesRead;
        SKIPBYTES_FORWARD(bytesLeft);
    }

    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);
    return err;
}
