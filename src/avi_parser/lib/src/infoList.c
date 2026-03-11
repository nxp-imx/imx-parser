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
    InfoListPtr self = (InfoListPtr)s;

    DESTROY_ATOM(ilng)
    DESTROY_ATOM(irtd)
    DESTROY_ATOM(inam)
    DESTROY_ATOM(iart)
    DESTROY_ATOM(icrd)
    DESTROY_ATOM(ignr)
    DESTROY_ATOM(icop)
    DESTROY_ATOM(icmt)

    DESTROY_ATOM(isft)
    DESTROY_ATOM(idfv)
    DESTROY_ATOM(idpn)

    destroyBaseAtom(s);
}

/* parse 'LIST INFO' */
int32 parseInfoList(InfoListPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                    void* appContext) {
    int32 err = PARSER_SUCCESS;
    InfoListPtr self = NULL;

    self = LOCALCalloc(1, sizeof(InfoList));
    TESTMALLOC(self)

    COPY_ATOM(self, proto)
    PRINT_INHERITANCE
    self->destroy = destroy;

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
                AVIMSG("ERR!LIST INFO child atom size %d is abnormal.Invalid atom.\n", atom->size);
                atom->destroy(atom);
                BAILWITHERROR(AVI_ERR_INVALID_ATOM)
            }

            switch (atom->tag) {
                case LanguageTag:
                    self->ilng = atom;
                    break;

                case RatingTag:
                    self->irtd = atom;
                    break;

                case NameTag:
                    self->inam = atom;
                    break;

                case ArtistTag:
                    self->iart = atom;
                    break;

                case CreationDateTag:
                    self->icrd = atom;
                    break;

                case GenreTag:
                    self->ignr = atom;
                    break;

                case CopyrightTag:
                    self->icop = atom;
                    break;

                case CommentsTag:
                    self->icmt = atom;
                    break;

                case IDFVTag:
                    AVIMSG("DivX File Version INFO %s \n",
                           (char*)((UserDataAtomPtr)atom)->unicodeString);
                    self->idfv = atom;
                    break;

                case IDPNTag:
                    AVIMSG("DivX Profile Name %s \n",
                           (char*)((UserDataAtomPtr)atom)->unicodeString);
                    self->idpn = atom;
                    break;

                case ISFTTag:
                    AVIMSG("DivX Software Name %s \n",
                           (char*)((UserDataAtomPtr)atom)->unicodeString);
                    self->isft = atom;
                    break;

                case LocationTag:
                    self->iarl = atom;
                    break;

                case KeywordsTag:
                    self->ikey = atom;
                    break;

                default:
                    AVIMSG("... Discard not need atom found in LIST INFO\n");
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
