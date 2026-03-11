/*
***********************************************************************
* Copyright (c) 2012-2013, Freescale Semiconductor, Inc.
* Copyright 2017-2018, 2021, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include <stdlib.h>
#include <string.h>

#include "fsl_types.h"
#include "fsl_parser.h"

#include "ID3ParserCore.h"
#include "ID3ParserInner.h"
#include "ID3Parser.h"

static int32 SetMetadata(ID3ObjPtr self, char* key, char* val, uint32 dwLen,
                         UserDataFormat eDataFormat) {
    MetaDataArray* ptMetaDataList = NULL;

    if ((self == NULL) || (key == NULL) || (val == NULL) || (dwLen == 0)) {
        // in case val = "", so need free it.
        // ArtWork data is directly point to somewhere in ID3.mData,
        // so shouldn't free.
        if (key != NULL && strcmp("artwork", key) != 0)
            SAFE_FREE(val);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    ptMetaDataList = &self->m_tMetaDataList;

    if (strcmp("album", key) == 0) {
        ptMetaDataList->Album.pData = (uint8*)val;
        ptMetaDataList->Album.iDataLen = dwLen;
        ptMetaDataList->Album.eDataFormat = eDataFormat;
    } else if (strcmp("artist", key) == 0) {
        ptMetaDataList->Artist.pData = (uint8*)val;
        ptMetaDataList->Artist.iDataLen = dwLen;
        ptMetaDataList->Artist.eDataFormat = eDataFormat;
    } else if (strcmp("copyright", key) == 0) {
        ptMetaDataList->CopyRight.pData = (uint8*)val;
        ptMetaDataList->CopyRight.iDataLen = dwLen;
        ptMetaDataList->CopyRight.eDataFormat = eDataFormat;
    } else if (strcmp("band", key) == 0) {
        ptMetaDataList->Band.pData = (uint8*)val;
        ptMetaDataList->Band.iDataLen = dwLen;
        ptMetaDataList->Band.eDataFormat = eDataFormat;
    } else if (strcmp("composer", key) == 0) {
        ptMetaDataList->Composer.pData = (uint8*)val;
        ptMetaDataList->Composer.iDataLen = dwLen;
        ptMetaDataList->Composer.eDataFormat = eDataFormat;
    } else if (strcmp("genre", key) == 0) {
        ptMetaDataList->Genre.pData = (uint8*)val;
        ptMetaDataList->Genre.iDataLen = dwLen;
        ptMetaDataList->Genre.eDataFormat = eDataFormat;
    } else if (strcmp("title", key) == 0) {
        ptMetaDataList->Title.pData = (uint8*)val;
        ptMetaDataList->Title.iDataLen = dwLen;
        ptMetaDataList->Title.eDataFormat = eDataFormat;
    } else if (strcmp("year", key) == 0) {
        ptMetaDataList->Year.pData = (uint8*)val;
        ptMetaDataList->Year.iDataLen = dwLen;
        ptMetaDataList->Year.eDataFormat = eDataFormat;
    } else if (strcmp("tracknumber", key) == 0) {
        ptMetaDataList->TrackNumber.pData = (uint8*)val;
        ptMetaDataList->TrackNumber.iDataLen = dwLen;
        ptMetaDataList->TrackNumber.eDataFormat = eDataFormat;
    } else if (strcmp("discnumber", key) == 0) {
        ptMetaDataList->DiscNumber.pData = (uint8*)val;
        ptMetaDataList->DiscNumber.iDataLen = dwLen;
        ptMetaDataList->DiscNumber.eDataFormat = eDataFormat;
    } else if (strcmp("artwork", key) == 0) {
        ptMetaDataList->ArtWork.pData = (uint8*)val;
        ptMetaDataList->ArtWork.iDataLen = dwLen;
        ptMetaDataList->ArtWork.eDataFormat = eDataFormat;
    } else if (strcmp("albumartist", key) == 0) {
        ptMetaDataList->AlbumArtist.pData = (uint8*)val;
        ptMetaDataList->AlbumArtist.iDataLen = dwLen;
        ptMetaDataList->AlbumArtist.eDataFormat = eDataFormat;
    } else if (strcmp("encoder-delay", key) == 0) {
        ptMetaDataList->EncoderDelay.pData = (uint8*)val;
        ptMetaDataList->EncoderDelay.iDataLen = dwLen;
        ptMetaDataList->EncoderDelay.eDataFormat = eDataFormat;
    } else if (strcmp("encoder-padding", key) == 0) {
        ptMetaDataList->EncoderPadding.pData = (uint8*)val;
        ptMetaDataList->EncoderPadding.iDataLen = dwLen;
        ptMetaDataList->EncoderPadding.eDataFormat = eDataFormat;
    } else {
        ID3MSG("unknown key: %s, val: %s\n", key, val);
    }

    return PARSER_SUCCESS;
}

#define AUDIO_PARSER_READ_SIZE (16 * 1024)

typedef struct tagMap {
    const char* key;
    const char* tag1;
    const char* tag2;
} Map;

static const Map g_kMap[] = {
        {"album", "TALB", "TAL"},    {"artist", "TPE1", "TP1"},      {"band", "TPE2", "TP2"},
        {"composer", "TCOM", "TCM"}, {"genre", "TCON", "TCO"},       {"title", "TIT2", "TT2"},
        {"year", "TYER", "TYE"},     {"tracknumber", "TRCK", "TRK"}, {"discnumber", "TPOS", "TPA"},
        {"copyright", "TCOP", NULL}, {"albumartist", "TPE2", "TP2"}};

static const uint32 g_kNumMapEntries = sizeof(g_kMap) / sizeof(g_kMap[0]);

static int32 ParserID3Tag(ID3ObjPtr self, uint64 startOffset) {
    int32 err = PARSER_SUCCESS;
    uint32 nReadLen = 10;
    uint32 nActuralRead = 0;
    uint32 nID3InfoLen = 0;
    bool isValid = FALSE;
    uint32 nFileSize = (uint32)self->m_qwFileSize;
    uint8* pTmpBuffer = NULL;
    uint32 i = 0;
    uint32 dataSize = 0;
    char* mime = NULL;
    void* data = NULL;

    self->m_tStreamOps.Seek(self->m_fileHandle, startOffset, SEEK_SET, self->m_context);

    pTmpBuffer = (uint8*)FSL_MALLOC(AUDIO_PARSER_READ_SIZE);
    TESTMALLOC(pTmpBuffer);
    memset(pTmpBuffer, 0, AUDIO_PARSER_READ_SIZE);

    if (nReadLen > nFileSize) {
        nReadLen = nFileSize;
    }

    nActuralRead =
            self->m_tStreamOps.Read(self->m_fileHandle, pTmpBuffer, nReadLen, self->m_context);
    if (nActuralRead != nReadLen) {
        err = PARSER_READ_ERROR;
        ID3MSG("ParserID3Tag, read expect %d, actual %d\n", (int)nReadLen, (int)nActuralRead);
        goto bail;
    }

    if (nActuralRead < 10) {
        err = PARSER_READ_ERROR;
        ID3MSG("ParserID3Tag, too little byte %d\n", (int)nActuralRead);
        goto bail;
    }

    // first try ID3V2
    if (0 == memcmp((void*)("ID3"), pTmpBuffer, 3)) {
        // Skip the ID3v2 header.
        const uint32 kHeaderLen = 10;
        const uint32 kMaxHeaderLen = 3 * 1024 * 1024 + kHeaderLen;
        uint32 len = ((pTmpBuffer[6] & 0x7f) << 21) | ((pTmpBuffer[7] & 0x7f) << 14) |
                     ((pTmpBuffer[8] & 0x7f) << 7) | (pTmpBuffer[9] & 0x7f);

        len += kHeaderLen;
        nID3InfoLen = len;
        if (nID3InfoLen > kMaxHeaderLen)
            nID3InfoLen = kMaxHeaderLen;

        if (nID3InfoLen > AUDIO_PARSER_READ_SIZE) {
            SAFE_FREE(pTmpBuffer);

            pTmpBuffer = (uint8*)FSL_MALLOC(nID3InfoLen);
            TESTMALLOC(pTmpBuffer);
            memset(pTmpBuffer, 0, nID3InfoLen);
        }

        self->m_tStreamOps.Seek(self->m_fileHandle, startOffset, SEEK_SET, self->m_context);
        nReadLen = nID3InfoLen;
        if (nReadLen > nFileSize) {
            nReadLen = nFileSize;
        }

        nActuralRead =
                self->m_tStreamOps.Read(self->m_fileHandle, pTmpBuffer, nReadLen, self->m_context);
        if (nActuralRead < nReadLen) {
            ID3MSG("read expect %d, actual %d\n", nReadLen, nActuralRead);
        }

        isValid = ID3V2Parse(&self->m_id3, (const char*)pTmpBuffer);
        if (isValid) {
            self->m_dwID3V2Size = nID3InfoLen;
        } else if (len > 3 * 1024 * 1024 && len < nFileSize) {
            self->m_dwID3V2Size = len;
        }
    }

    // then try ID3V1
    if (!isValid) {
        nReadLen = ID3V1_SIZE;
        memset(pTmpBuffer, 0, AUDIO_PARSER_READ_SIZE);
        self->m_tStreamOps.Seek(self->m_fileHandle, (int)0 - (int)ID3V1_SIZE, SEEK_END,
                                self->m_context); /*< ID3v1 */
        nActuralRead =
                self->m_tStreamOps.Read(self->m_fileHandle, pTmpBuffer, nReadLen, self->m_context);

        isValid = ID3V1Parse(&self->m_id3, (const char*)pTmpBuffer);

        if (!isValid) {
            goto bail;
        }
    }

    for (i = 0; i < g_kNumMapEntries; ++i) {
        Iterator it;
        char* s = NULL;

        IteratorInit(&it, &self->m_id3, g_kMap[i].tag1);
        if (Miss(&it) && g_kMap[i].tag2) {
            IteratorExit(&it);
            IteratorInit(&it, &self->m_id3, g_kMap[i].tag2);
        }

        if (Miss(&it)) {
            IteratorExit(&it);
            continue;
        }

        FetchFrameVal(&it, &s, FALSE);
        IteratorExit(&it);

        if (s) {
            SetMetadata(self, (char*)g_kMap[i].key, s, (uint32)strlen(s), USER_DATA_FORMAT_UTF8);
        }
    }

    {
        // parse id3 comments
        Iterator it;
        char* desc = NULL;
        char* value = NULL;

        IteratorInit(&it, &self->m_id3, "COM");
        if (Miss(&it)) {
            IteratorExit(&it);
            IteratorInit(&it, &self->m_id3, "COMM");
        }
        while (!Miss(&it)) {
            FetchFrameVal(&it, &desc, FALSE);
            FetchFrameVal(&it, &value, TRUE);  // to get more data
            if (desc && value && strlen(desc) > 3 && strcmp(desc + 3, "iTunSMPB") == 0) {
                int32 delay, padding;
                if (sscanf(value, " %*x %x %x %*x", &delay, &padding) == 2) {
                    self->m_dwEncoderDelay = delay;
                    self->m_dwEncoderPadding = padding;
                    SetMetadata(self, "encoder-delay", (char*)&self->m_dwEncoderDelay, 4,
                                USER_DATA_FORMAT_INT_LE);
                    SetMetadata(self, "encoder-padding", (char*)&self->m_dwEncoderPadding, 4,
                                USER_DATA_FORMAT_INT_LE);
                }
                break;
            }
            SAFE_FREE(desc);
            IteratorNext(&it);
        }
    }

    data = (void*)GetArtWork(&self->m_id3, &dataSize, &mime);
    if (data) {
        UserDataFormat eUDFormat;

        if (strcmp(mime, "image/jpeg") == 0) {
            eUDFormat = USER_DATA_FORMAT_JPEG;
        } else if (strcmp(mime, "image/png") == 0) {
            eUDFormat = USER_DATA_FORMAT_PNG;
        } else if (strcmp(mime, "image/bmp") == 0) {
            eUDFormat = USER_DATA_FORMAT_BMP;
        } else if (strcmp(mime, "image/gif") == 0) {
            eUDFormat = USER_DATA_FORMAT_GIF;
        } else {
            ID3MSG("un-expect pic type %s\n", mime);
            goto bail;
        }

        SetMetadata(self, "artwork", (char*)data, dataSize, eUDFormat);
    }

bail:
    SAFE_FREE(mime);
    SAFE_FREE(pTmpBuffer);

    return err;
}

int32 ID3ParserCreate2(FslFileStream* streamOps, ParserMemoryOps* memOps, FslFileHandle fileHandle,
                       void* context, ID3Parser* phParser, bool isConvert, uint64 startOffset) {
    int32 err = PARSER_SUCCESS;
    ID3ObjPtr self = NULL;
    uint64 qwOrigPos = 0;

    if ((NULL == streamOps) || (NULL == memOps) || (NULL == phParser)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    self = memOps->Calloc(1, sizeof(ID3Obj));
    TESTMALLOC(self);

    self->m_qwFileSize = streamOps->Size(fileHandle, context);

    self->m_context = context;
    self->m_fileHandle = fileHandle;
    self->m_tMemOps = *memOps;
    self->m_tStreamOps = *streamOps;

    ID3CoreInit(&self->m_id3, &self->m_tMemOps, isConvert);

    qwOrigPos = streamOps->Tell(fileHandle, context);

    ParserID3Tag(self, startOffset);

    streamOps->Seek(fileHandle, qwOrigPos, SEEK_SET, context);

    *phParser = (ID3Parser)self;

bail:

    return err;
}

int32 ID3ParserCreate(FslFileStream* streamOps, ParserMemoryOps* memOps, FslFileHandle fileHandle,
                      void* context, ID3Parser* phParser, bool isConvert) {
    return ID3ParserCreate2(streamOps, memOps, fileHandle, context, phParser, isConvert, 0);
}

static void MetaDataFree(ID3ObjPtr self) {
    MetaDataArray* ptMedaDataArray = NULL;

    if (NULL == self) {
        return;
    }

    ptMedaDataArray = &self->m_tMetaDataList;

    SAFE_FREE(ptMedaDataArray->Album.pData);
    SAFE_FREE(ptMedaDataArray->Artist.pData);
    SAFE_FREE(ptMedaDataArray->CopyRight.pData);
    SAFE_FREE(ptMedaDataArray->Band.pData);
    SAFE_FREE(ptMedaDataArray->Composer.pData);
    SAFE_FREE(ptMedaDataArray->Genre.pData);
    SAFE_FREE(ptMedaDataArray->Title.pData);
    SAFE_FREE(ptMedaDataArray->Year.pData);
    SAFE_FREE(ptMedaDataArray->TrackNumber.pData);
    SAFE_FREE(ptMedaDataArray->DiscNumber.pData);

    // ArtWork data is directly point to somewhere in ID3.mData,
    // so shouldn't free.

    return;
}

int32 ID3ParserDelete(ID3Parser hParser) {
    ID3ObjPtr self = (ID3ObjPtr)hParser;

    if (NULL == hParser) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    ID3CoreExit(&self->m_id3);

    MetaDataFree(self);

    SAFE_FREE(self);

    return PARSER_SUCCESS;
}

int32 ID3ParserGetID3V2Size(ID3Parser hParser, uint32* pdwSize) {
    ID3ObjPtr self = (ID3ObjPtr)hParser;

    if ((NULL == hParser) || (NULL == pdwSize)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *pdwSize = self->m_dwID3V2Size;

    return PARSER_SUCCESS;
}

int32 ID3ParserGetMetaData(ID3Parser hParser, UserDataID userDataId, UserDataFormat* userDataFormat,
                           uint8** userData, uint32* userDataLength) {
    ID3ObjPtr self = (ID3ObjPtr)hParser;
    MetaDataArray* ptMedaDataArray = NULL;

    if ((NULL == hParser) || (NULL == userDataFormat) || (NULL == userData) ||
        (NULL == userDataLength)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    ptMedaDataArray = &self->m_tMetaDataList;

    switch (userDataId) {
        case USER_DATA_ALBUM:
            *userDataFormat = ptMedaDataArray->Album.eDataFormat;
            *userData = ptMedaDataArray->Album.pData;
            *userDataLength = ptMedaDataArray->Album.iDataLen;
            break;

        case USER_DATA_ARTIST:
            *userDataFormat = ptMedaDataArray->Artist.eDataFormat;
            *userData = ptMedaDataArray->Artist.pData;
            *userDataLength = ptMedaDataArray->Artist.iDataLen;
            break;

        case USER_DATA_COPYRIGHT:
            *userDataFormat = ptMedaDataArray->CopyRight.eDataFormat;
            *userData = ptMedaDataArray->CopyRight.pData;
            *userDataLength = ptMedaDataArray->CopyRight.iDataLen;
            break;

        case USER_DATA_COMPOSER:
            *userDataFormat = ptMedaDataArray->Composer.eDataFormat;
            *userData = ptMedaDataArray->Composer.pData;
            *userDataLength = ptMedaDataArray->Composer.iDataLen;
            break;

        case USER_DATA_GENRE:
            *userDataFormat = ptMedaDataArray->Genre.eDataFormat;
            *userData = ptMedaDataArray->Genre.pData;
            *userDataLength = ptMedaDataArray->Genre.iDataLen;
            break;

        case USER_DATA_TITLE:
            *userDataFormat = ptMedaDataArray->Title.eDataFormat;
            *userData = ptMedaDataArray->Title.pData;
            *userDataLength = ptMedaDataArray->Title.iDataLen;
            break;

        case USER_DATA_CREATION_DATE:
            *userDataFormat = ptMedaDataArray->Year.eDataFormat;
            *userData = ptMedaDataArray->Year.pData;
            *userDataLength = ptMedaDataArray->Year.iDataLen;
            break;

        case USER_DATA_TRACKNUMBER:
            *userDataFormat = ptMedaDataArray->TrackNumber.eDataFormat;
            *userData = ptMedaDataArray->TrackNumber.pData;
            *userDataLength = ptMedaDataArray->TrackNumber.iDataLen;
            break;

        case USER_DATA_DISCNUMBER:
            *userDataFormat = ptMedaDataArray->DiscNumber.eDataFormat;
            *userData = ptMedaDataArray->DiscNumber.pData;
            *userDataLength = ptMedaDataArray->DiscNumber.iDataLen;
            break;
        case USER_DATA_ARTWORK:
            *userDataFormat = ptMedaDataArray->ArtWork.eDataFormat;
            *userData = ptMedaDataArray->ArtWork.pData;
            *userDataLength = ptMedaDataArray->ArtWork.iDataLen;
            break;
        case USER_DATA_ALBUMARTIST:
            *userDataFormat = ptMedaDataArray->AlbumArtist.eDataFormat;
            *userData = ptMedaDataArray->AlbumArtist.pData;
            *userDataLength = ptMedaDataArray->AlbumArtist.iDataLen;
            break;
        case USER_DATA_AUD_ENC_DELAY:
            *userDataFormat = ptMedaDataArray->EncoderDelay.eDataFormat;
            *userData = ptMedaDataArray->EncoderDelay.pData;
            *userDataLength = ptMedaDataArray->EncoderDelay.iDataLen;
            break;
        case USER_DATA_AUD_ENC_PADDING:
            *userDataFormat = ptMedaDataArray->EncoderPadding.eDataFormat;
            *userData = ptMedaDataArray->EncoderPadding.pData;
            *userDataLength = ptMedaDataArray->EncoderPadding.iDataLen;
            break;
        default:
            *userDataFormat = 0;
            *userData = NULL;
            *userDataLength = 0;
            break;
    }

    return PARSER_SUCCESS;
}
