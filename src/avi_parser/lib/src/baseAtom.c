/*
 ***********************************************************************
 * Copyright (c) 2005-2012, 2014, Freescale Semiconductor, Inc.
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

void destroyBaseAtom(BaseAtomPtr atom) {
    LOCALFree(atom);
}

static int32 parseBaseAtom(BaseAtomPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                           void* appContext) {
    int32 err = PARSER_SUCCESS;
    BaseAtomPtr self = NULL;
    uint32 bytesLeft;

    self = LOCALCalloc(1, sizeof(BaseAtom));
    TESTMALLOC(self)
    COPY_ATOM(self, proto)
    PRINT_INHERITANCE

    /* skip the bytes not read yet */
    bytesLeft = self->size - self->bytesRead;
    SKIPBYTES_FORWARD(bytesLeft);

bail:
    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);
    return err;
}

int32 createAtom(BaseAtomPtr* outAtom, BaseAtomPtr parent, AviInputStream* inputStream,
                 void* appContext) {
    int32 err = PARSER_SUCCESS;
    uint32 tag;
    uint32 realSize; /* real atom size, could be ODD number */
    uint32 size;     /* EVEN-rounded atom size, for bytes reading */
    BaseAtom protoAtom;
    BaseAtomPtr self = &protoAtom;

    *outAtom = NULL;

    /* warning: shall NOT call GET32 to get tag & size, because "size" does NOT include the 2 fields
     * and "bytesRead" shall not increase */
    err = read32(inputStream, &tag, appContext);
    if (err)
        goto bail;
    err = read32(inputStream, &size, appContext);
    if (err)
        goto bail;

    realSize = size;
    size = (size + 1) & (~1);

    INIT_ATOM(tag, size, realSize, 0, parent, parent->isLive, parent->au_flag);

    switch (tag) {
        case ListTag: /*list atom, check list type */
            GET32(type);
            switch (self->type) {
                case HeaderListTag:
                    AVIMSG("LIST hdrl (%d): \n", realSize);
                    err = parseHeaderList((HeaderListPtr*)outAtom, self, inputStream, appContext);
                    break;

                case StreamHeaderListTag:
                    AVIMSG("LIST strl (%d): \n", realSize);
                    err = parseStreamHeaderList((StreamHeaderListPtr*)outAtom, self, inputStream,
                                                appContext);
                    break;

                case MovieListTag:
                    AVIMSG("LIST movi (%u): \n",
                           realSize); /* for large size movies, the bit31 will be used */
                    err = parseMovieList((MovieListPtr*)outAtom, self, inputStream, appContext);
                    break;

                case InfoListTag:
                    AVIMSG("LIST INFO (%d): \n", realSize);
                    err = parseInfoList((InfoListPtr*)outAtom, self, inputStream, appContext);
                    break;

                default: {
                    AVIMSG("unkown atom (%d): LIST ", realSize);
                    PrintTag(self->type);
                    err = parseBaseAtom(outAtom, self, inputStream, appContext);
                }
            }
            break;

        /* non-list atoms */
        case RIFFTag:
            AVIMSG("RIFF (%u): \n", realSize);
            err = parseRIFF((RiffTitlePtr*)outAtom, self, inputStream, appContext);
            break;

        case AviHeaderTag:
            AVIMSG("avih (%d): \n", realSize);
            err = parseAviHeader((MainAviHeaderPtr*)outAtom, self, inputStream, appContext);
            break;

        case StreamHeaderTag:
            AVIMSG("strh (%d): \n", realSize);
            err = parseStreamHeader((StreamHeaderPtr*)outAtom, self, inputStream, appContext);
            break;

        case StreamFormatTag:
            AVIMSG("strf (%d): \n", realSize);
            err = parseStreamFormat((StreamFormatPtr*)outAtom, self, inputStream, appContext);
            break;

        case StreamHeaderDataTag:
            AVIMSG("strd(%d): \n", realSize);
            err = parseStreamHeaderData((StreamHeaderDataPtr*)outAtom, self, inputStream,
                                        appContext);
            break;

        case StreamNameTag:
            AVIMSG("strn (%d): \n", realSize);
            err = parseStreamName((StreamNamePtr*)outAtom, self, inputStream, appContext);
            break;

        case Idx1Tag:
            AVIMSG("idx1 (%d): \n", realSize);
            err = parseIdx1((Idx1Ptr*)outAtom, self, inputStream, appContext);
            break;

        case Avi2IndexTag:
            AVIMSG("indx (%d): \n", realSize);
            err = parseAvi2BaseIndex((BaseIndexPtr*)outAtom, self, inputStream, appContext);
            break;

        case LanguageTag:
        case RatingTag:
        case NameTag:
        case ArtistTag:
        case CreationDateTag:
        case GenreTag:
        case CopyrightTag:
        case CommentsTag:
        case IDFVTag:
        case IDPNTag:
        case ISFTTag:
        case LocationTag:
        case KeywordsTag:
            AVIMSG("user data (%d): ", realSize);
            PrintTag(tag);
            err = parseUserDataAtom((UserDataAtomPtr*)outAtom, self, inputStream, appContext);
            break;

        default:
            if (isAvi2IndexTag(tag)) {
                AVIMSG("ix## (%d): ", realSize);
                PrintTag(tag);
                err = parseAvi2BaseIndex((BaseIndexPtr*)outAtom, self, inputStream, appContext);
            } else {
                AVIMSG("unknown atom (%d): ", realSize);
                PrintTag(tag);
                if (((0 == tag) && (0 == realSize)) || ((realSize + 8) >= parent->size)) {
                    /* TODO: need method find more invalid tags & quit parsing
                    For known atom tags, not apply the size limitation because idx1 & movi are
                    permitted to be cut short.*/
                    BAILWITHERROR(AVI_ERR_INVALID_ATOM)
                }
                err = parseBaseAtom(outAtom, self, inputStream, appContext);
            }
    }
bail:

    return err;
}

/* whther is index chunk ID 'ix##" of AVI 2.0 */
bool isAvi2IndexTag(uint32 tag) {
    bool ret = FALSE;
    uint8* id = (uint8*)&tag;

    if (('i' == id[0]) && ('x' == id[1])) {
        if (('0' <= id[2]) && ('9' >= id[2]) && ('0' <= id[3]) && ('9' >= id[3]))
            ret = TRUE;
    }
    return ret;
}
