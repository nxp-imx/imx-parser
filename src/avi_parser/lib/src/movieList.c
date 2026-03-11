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

/*************************************************************************
Following the header information is a 'movi' list that contains the actual data in the streams
that is, the video frames and audio samples. The data chunks can reside directly in the 'movi' list,
or they might be grouped within 'rec ' lists.

The 'rec ' grouping implies that the grouped chunks should be read from disk all at once,
and is intended for files that are interleaved to play from CD-ROM.
**************************************************************************/

/* parsing the movie list 'movi' */
int32 parseMovieList(MovieListPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                     void* appContext) {
    int32 err = PARSER_SUCCESS;
    MovieListPtr self = NULL;
    int64 fileSize;

    self = LOCALCalloc(1, sizeof(MovieList));
    TESTMALLOC(self)

    COPY_ATOM(self, proto)
    PRINT_INHERITANCE

    self->moviList = LocalFileTell(inputStream, appContext);
    self->moviList -= 4; /* step backward before type field 'movi' */

    /* Warning: for a recorded movie, this size maybe invalid and no idx1 exists */

    /* where is the end of movie ?
    NOTE: the movie data can be cut short!check size here. */
    fileSize = LocalFileSize(inputStream, appContext);
    self->moviEnd = self->moviList + self->size;
    if (self->isLive) {
        fileSize = self->moviEnd;
    }
    AVIMSG("\nThe movie data end at %lld. Total file size %lld\n\n", self->moviEnd, fileSize);

    /* warning! the bit31 may be used because the movie atom size can exceed 2GB */
    if ((4 > self->size) || (fileSize < self->moviEnd)) {
        AVIMSG("\nWarning!The movie data is truncated! It's not as long as claimed. Movie data "
               "size %lld\n\n",
               (uint64)self->size);
        self->moviEnd = fileSize;
        self->cutShort = TRUE;

        /* not return err, the part of movie shall still be able to play */
    }

    /* seek to the end of movie chunk, for the following idx1 */
    if (!self->isLive) {
        if (LocalFileSeek(inputStream, self->moviEnd, SEEK_SET, appContext)) {
            AVIMSG("Can not seek to the end of movie\n");
            BAILWITHERROR(PARSER_SEEK_ERROR)
        }
    }

bail:

    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);

    return err;
}
