
/*
 ***********************************************************************
 * Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */
#include <string.h>
#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "avi_parser_api.h"
#include "avi_utils.h"
#include "avi.h"

static void asciiToUnicodeString(uint8* src, uint16* des) {
    uint32 size;
    uint32 i;

    size = (uint32)strlen((char*)src);

    for (i = 0; i < size; i++) {
        des[i] = (uint16)src[i];
    }
    des[i] = 0;
}

static void destroy(BaseAtomPtr s) {
    UserDataAtomPtr self = (UserDataAtomPtr)s;

    if (self->data) {
        LOCALFree(self->data);
        self->data = NULL;
    }

    if (self->unicodeString) {
        LOCALFree(self->unicodeString);
        self->unicodeString = NULL;
    }

    destroyBaseAtom(s);
}

int32 parseUserDataAtom(UserDataAtomPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                        void* appContext) {
    int32 err = PARSER_SUCCESS;
    UserDataAtomPtr self = NULL;

    self = LOCALCalloc(1, sizeof(UserDataAtom));
    TESTMALLOC(self)
    COPY_ATOM(self, proto)
    PRINT_INHERITANCE
    self->destroy = destroy;

    if (self->size) {
        self->data = (uint8*)LOCALCalloc(1, self->size + 2);
        TESTMALLOC(self->data)
        GETBYTES(self->data, self->size);
        /* don't know its ASCII or UNICODE, whether is null-terminated, so add \0\0 for safety */
        self->dataSize = self->size + 1;

        /* use unicode now! */
        self->stringLength = self->size;
        self->unicodeString = (uint16*)LOCALCalloc(1, (self->stringLength + 1) * sizeof(uint16));
        TESTMALLOC(self->unicodeString)

        AVIMSG("user data string: %s\n", self->data);
        asciiToUnicodeString(self->data, self->unicodeString);
    }

bail:
    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);
    return err;
}
