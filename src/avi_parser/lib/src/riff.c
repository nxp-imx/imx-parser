
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

static void destroy(BaseAtomPtr s) {
    RiffTitlePtr self = (RiffTitlePtr)s;

    DESTROY_ATOM(hdrl)
    DESTROY_ATOM(movi)
    DESTROY_ATOM(idx1)
    DESTROY_ATOM(info)

    destroyBaseAtom(s);
}

int32 parseRIFF(RiffTitlePtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                void* appContext) {
    int32 err = PARSER_SUCCESS;
    RiffTitlePtr self = NULL;

    self = LOCALCalloc(1, sizeof(RiffTitle));
    TESTMALLOC(self)

    COPY_ATOM(self, proto)
    GET32(type); /* RIFF is LIST like, but we take it as non-list atom, so need to get type here */
    PRINT_INHERITANCE
    self->destroy = destroy;

    if (AVITag != self->type) {
        AVIMSG("ERR: RIFF AVI header not found\n");
        BAILWITHERROR(PARSER_ERR_INVALID_MEDIA)
    }

    // search for  ListTag , MMFMWK-7349
    uint32 maxSearchLength = 100 * 1024;
    uint32 searchLength = 0;
    while (searchLength < maxSearchLength) {
        uint32 tag;
        err = read32(inputStream, &tag, appContext);
        if (err)
            BAILWITHERROR(err);
        if (ListTag == tag) {
            if (LocalFileSeek(inputStream, -4, SEEK_CUR, appContext))
                BAILWITHERROR(PARSER_SEEK_ERROR);
            break;
        }
        searchLength += 4;
    }

    if (searchLength >= maxSearchLength) {
        AVIMSG("ERR: LIST tag not found at the beginning of the file\n");
        BAILWITHERROR(PARSER_ERR_INVALID_MEDIA)
    }

    GET_ATOM(hdrl)
    if ((ListTag != self->hdrl->tag) || (HeaderListTag != self->hdrl->type)) {
        AVIMSG("ERR: LIST 'hdrl ' not found at the beginning of the file\n");
        BAILWITHERROR(PARSER_ERR_INVALID_MEDIA)
    }

#if 1  // Work-around for ENGR131901: zero value in size field of RIFF thunk header
    if ((0 == self->size) && ((NULL != self->parent) && (0 != self->parent->size))) {
        self->size = self->parent->size;
    }
#endif
    /* 'movi' & 'idx1' is not at fixed postion, and other types atom may exist */
    while (self->size > (self->bytesRead + 8)) {
        BaseAtomPtr atom;

        CREATE_ATOM(atom)

        if ((ListTag == atom->tag) && (MovieListTag == atom->type)) {
            MovieListPtr movi;
            self->movi = atom;

            movi = (MovieListPtr)atom;
            if (movi->cutShort) {
                goto bail; /* no need to parse further, the movie data is cut short and no following
                              atoms exist */
            }

            if (self->isLive) {
                AviObjectPtr root = (AviObjectPtr)self->parent;
                root->fileSize = movi->moviEnd;
                /* don't read any more data to avoid seeking if it is live */
                break;
            }
        } else if ((ListTag == atom->tag) && (InfoListTag == atom->type)) {
            self->info = atom;
        } else if (Idx1Tag == atom->tag) {
            self->idx1 = atom;
        } else {
            if (ListTag == atom->tag) {
                AVIMSG("... Discard non-needed atom (%d): LIST ", atom->size);
                PrintTag(atom->type);
            } else {
                AVIMSG("... Discard non-needed atom (%d):  ", atom->size);
                PrintTag(atom->tag);
            }
            atom->destroy(atom);
        }
    }

    if (NULL == self->movi) {
        AVIMSG("ERR: movie list NOT found\n");
        BAILWITHERROR(AVI_ERR_NO_MOVIE_LIST)
    }

    if (NULL == self->idx1) {
        AVIMSG("... Warning: idx1 list NOT found\n"); /* not error, shall tolerate it */
    }

bail:
    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else if (self->movi) {
        AVIMSG("Warning! Parsing error after movie chunk. Still assume success.\n");
        *outAtom = self;
        err = PARSER_SUCCESS;
    } else
        self->destroy((BaseAtomPtr)self);

    return err;
}
