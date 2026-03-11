/*
 ***********************************************************************
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2017, 2025-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "ape_parser_api.h"
#include "ape_parser.h"

#if 0

#include <android/log.h>
#define LOG_BUF_SIZE 1024
void LogOutput(char *fmt, ...)
{
    va_list ap;
    char buf[LOG_BUF_SIZE];
    va_start(ap, fmt);
    vsnprintf(buf, LOG_BUF_SIZE, fmt, ap);
    va_end(ap);
    __android_log_write(ANDROID_LOG_DEBUG, "APE_PARSER", buf);
    return;
}
#define LOG_PRINTF LogOutput
#endif

#define FSL_BIG_ENDIAN

#define APE_FRAME_EXTRA_SIZE 8
#define APE_SUBFRAME_SIZE 4608

typedef struct {
    const char* extension;
    UserDataFormat format;
} ArtWorkType;

static const ArtWorkType artWorkMap[] = {
        {"jpg", USER_DATA_FORMAT_JPEG}, {"jpeg", USER_DATA_FORMAT_JPEG},
        {"jps", USER_DATA_FORMAT_JPEG}, {"png", USER_DATA_FORMAT_PNG},
        {"bmp", USER_DATA_FORMAT_BMP},  {NULL, USER_DATA_FORMAT_MAX}};

int32 getArtWorkFormat(const char* filename) {
    uint32 i;
    const char* ext = strrchr(filename, '.');
    if (NULL == ext)
        return USER_DATA_FORMAT_MAX;

    ext++;
    for (i = 0; i < sizeof(artWorkMap) / sizeof(artWorkMap[0]); i++) {
        if (strcasecmp(artWorkMap[i].extension, ext) == 0)
            return artWorkMap[i].format;
    }
    return USER_DATA_FORMAT_MAX;
}

int32 readData(APE_PARSER* h, FslFileHandle s, void* buffer, uint32 size,
                             void* context) {
    uint32 sizeRead = 0;

    sizeRead = LocalFileRead(s, buffer, size, context);
    if (size != sizeRead)
        return PARSER_READ_ERROR;

    return PARSER_SUCCESS;
}

int32 skipData(APE_PARSER* h, FslFileHandle s, uint32 size, void* context) {

    int64 offset = 0;
    (void)h;

    offset = LocalFileTell(s, context);

    LocalFileSeek(s, offset + size, SEEK_SET, context);

    return PARSER_SUCCESS;
}

int32 read16(APE_PARSER* h, FslFileHandle s, uint16* outVal, void* context) {
    uint16 val;
    int32 sizeRead;

    sizeRead = LocalFileRead(s, &val, 2, context);
    if (2 != sizeRead)
        return PARSER_READ_ERROR;

#ifndef FSL_BIG_ENDIAN
    *outVal = ((val & 0X00FF) << 8) | ((val & 0XFF00) >> 8);
#else
    *outVal = val;
#endif

    return PARSER_SUCCESS;
}

int32 read32(APE_PARSER* h, FslFileHandle s, uint32* outVal, void* context) {
    uint32 val;
    int32 sizeRead;

    sizeRead = LocalFileRead(s, &val, 4, context);
    if (4 != sizeRead)
        return PARSER_READ_ERROR;

#ifndef FSL_BIG_ENDIAN
    *outVal = (val & 0X000000FF) << 24 | (val & 0X0000FF00) << 8 | (val & 0X00FF0000) >> 8 |
              (val & 0XFF000000) >> 24;

#else
    *outVal = val;
#endif

    return PARSER_SUCCESS;
}

int32 write16(uint16 src, uint8** dst) {
#ifndef FSL_BIG_ENDIAN
    *(uint16*)*dst = ((src & 0X00FF) << 8) | ((src & 0XFF00) >> 8);
#else
    *(uint16*)*dst = src;
#endif

    *dst += 2;

    return PARSER_SUCCESS;
}

int32 write32(uint32 src, uint8** dst) {
#ifndef FSL_BIG_ENDIAN
    *(uint32*)*dst = ((src & 0X000000FF) << 24) | ((src & 0X0000FF00) << 8) |
                     ((src & 0X00FF0000) >> 8) | ((src & 0XFF000000) >> 24);

#else
    *(uint32*)*dst = src;
#endif
    *dst += 4;

    return PARSER_SUCCESS;
}

int32 ApeParserSetMetaData(APE_PARSER* h, const char* key, uint8* value, uint32 len,
                                         UserDataFormat format) {
    MetaDataArray* pMetaDataList = &h->metaDataList;

    if (!pMetaDataList || !key || !value || len == 0)
        return PARSER_ERR_INVALID_PARAMETER;

    if (strcasecmp("album", key) == 0) {
        pMetaDataList->Album.pData = value;
        pMetaDataList->Album.iDataLen = len;
        pMetaDataList->Album.eDataFormat = format;
    } else if (strcasecmp("artist", key) == 0) {
        pMetaDataList->Artist.pData = value;
        pMetaDataList->Artist.iDataLen = len;
        pMetaDataList->Artist.eDataFormat = format;
    } else if (strcasecmp("copyright", key) == 0) {
        pMetaDataList->CopyRight.pData = value;
        pMetaDataList->CopyRight.iDataLen = len;
        pMetaDataList->CopyRight.eDataFormat = format;
    } else if (strcasecmp("band", key) == 0) {
        pMetaDataList->Band.pData = value;
        pMetaDataList->Band.iDataLen = len;
        pMetaDataList->Band.eDataFormat = format;
    } else if (strcasecmp("composer", key) == 0) {
        pMetaDataList->Composer.pData = value;
        pMetaDataList->Composer.iDataLen = len;
        pMetaDataList->Composer.eDataFormat = format;
    } else if (strcasecmp("genre", key) == 0) {
        pMetaDataList->Genre.pData = (uint8*)value;
        pMetaDataList->Genre.iDataLen = len;
        pMetaDataList->Genre.eDataFormat = format;
    } else if (strcasecmp("title", key) == 0) {
        pMetaDataList->Title.pData = value;
        pMetaDataList->Title.iDataLen = len;
        pMetaDataList->Title.eDataFormat = format;
    } else if (strcasecmp("year", key) == 0) {
        pMetaDataList->Year.pData = value;
        pMetaDataList->Year.iDataLen = len;
        pMetaDataList->Year.eDataFormat = format;
    } else if (strcasecmp("tracknumber", key) == 0) {
        pMetaDataList->TrackNumber.pData = value;
        pMetaDataList->TrackNumber.iDataLen = len;
        pMetaDataList->TrackNumber.eDataFormat = format;
    } else if (strcasecmp("discnumber", key) == 0) {
        pMetaDataList->DiscNumber.pData = value;
        pMetaDataList->DiscNumber.iDataLen = len;
        pMetaDataList->DiscNumber.eDataFormat = format;
    } else if (strcasecmp("artwork", key) == 0) {
        pMetaDataList->ArtWork.pData = value;
        pMetaDataList->ArtWork.iDataLen = len;
        pMetaDataList->ArtWork.eDataFormat = format;
    } else if (strcasecmp("albumartist", key) == 0) {
        pMetaDataList->AlbumArtist.pData = value;
        pMetaDataList->AlbumArtist.iDataLen = len;
        pMetaDataList->AlbumArtist.eDataFormat = format;
    } else if (strcasecmp("comment", key) == 0) {
        pMetaDataList->Comment.pData = value;
        pMetaDataList->Comment.iDataLen = len;
        pMetaDataList->Comment.eDataFormat = format;
    } else if (strcasecmp("Cover Art (Front)", key) == 0) {
        pMetaDataList->ArtWork.pData = value;
        pMetaDataList->ArtWork.iDataLen = len;
        pMetaDataList->ArtWork.eDataFormat = format;
    } else {
        PARSERMSG("unknown key: %s, val: %s\n", key, value);
    }

    return PARSER_SUCCESS;
}

int32 ApeParserDoGetMetaData(APE_PARSER* h, UserDataID userDataId,
                                           UserDataFormat* userDataFormat, uint8** userData,
                                           uint32* userDataLength) {
    MetaDataArray* pMetaDataList = NULL;

    if (!h || !userDataFormat || !userData || !userDataLength)
        return PARSER_ERR_INVALID_PARAMETER;

    pMetaDataList = &h->metaDataList;

    if (!pMetaDataList)
        return PARSER_ERR_INVALID_PARAMETER;

    switch (userDataId) {
        case USER_DATA_ALBUM:
            *userDataFormat = pMetaDataList->Album.eDataFormat;
            *userData = pMetaDataList->Album.pData;
            *userDataLength = pMetaDataList->Album.iDataLen;
            break;

        case USER_DATA_ARTIST:
            *userDataFormat = pMetaDataList->Artist.eDataFormat;
            *userData = pMetaDataList->Artist.pData;
            *userDataLength = pMetaDataList->Artist.iDataLen;
            break;

        case USER_DATA_COPYRIGHT:
            *userDataFormat = pMetaDataList->CopyRight.eDataFormat;
            *userData = pMetaDataList->CopyRight.pData;
            *userDataLength = pMetaDataList->CopyRight.iDataLen;
            break;

        case USER_DATA_COMPOSER:
            *userDataFormat = pMetaDataList->Composer.eDataFormat;
            *userData = pMetaDataList->Composer.pData;
            *userDataLength = pMetaDataList->Composer.iDataLen;
            break;

        case USER_DATA_GENRE:
            *userDataFormat = pMetaDataList->Genre.eDataFormat;
            *userData = pMetaDataList->Genre.pData;
            *userDataLength = pMetaDataList->Genre.iDataLen;
            break;

        case USER_DATA_TITLE:
            *userDataFormat = pMetaDataList->Title.eDataFormat;
            *userData = pMetaDataList->Title.pData;
            *userDataLength = pMetaDataList->Title.iDataLen;
            break;

        case USER_DATA_CREATION_DATE:
            *userDataFormat = pMetaDataList->Year.eDataFormat;
            *userData = pMetaDataList->Year.pData;
            *userDataLength = pMetaDataList->Year.iDataLen;
            break;

        case USER_DATA_TRACKNUMBER:
            *userDataFormat = pMetaDataList->TrackNumber.eDataFormat;
            *userData = pMetaDataList->TrackNumber.pData;
            *userDataLength = pMetaDataList->TrackNumber.iDataLen;
            break;

        case USER_DATA_DISCNUMBER:
            *userDataFormat = pMetaDataList->DiscNumber.eDataFormat;
            *userData = pMetaDataList->DiscNumber.pData;
            *userDataLength = pMetaDataList->DiscNumber.iDataLen;
            break;
        case USER_DATA_ARTWORK:
            *userDataFormat = pMetaDataList->ArtWork.eDataFormat;
            *userData = pMetaDataList->ArtWork.pData;
            *userDataLength = pMetaDataList->ArtWork.iDataLen;
            break;
        case USER_DATA_ALBUMARTIST:
            *userDataFormat = pMetaDataList->AlbumArtist.eDataFormat;
            *userData = pMetaDataList->AlbumArtist.pData;
            *userDataLength = pMetaDataList->AlbumArtist.iDataLen;
            break;
        case USER_DATA_COMMENTS:
            *userDataFormat = pMetaDataList->Comment.eDataFormat;
            *userData = pMetaDataList->Comment.pData;
            *userDataLength = pMetaDataList->Comment.iDataLen;
            break;
        default:
            *userDataFormat = 0;
            *userData = NULL;
            *userDataLength = 0;
            break;
    }

    return PARSER_SUCCESS;
}

int32 ApeParserReadOneItem(APE_PARSER* h) {
    FslFileHandle fileHandle = h->fileHandle;
    void* appContext = h->appContext;
    uint32 i;
    uint8 key[512];
    uint8* value = NULL;
    uint32 size, flags;
    int64 fileOffset;
    UserDataFormat format = USER_DATA_FORMAT_UTF8;

    memset(key, 0, sizeof(key));

    if (read32(h, fileHandle, &size, appContext))
        return PARSER_READ_ERROR;
    if (read32(h, fileHandle, &flags, appContext))
        return PARSER_READ_ERROR;

    for (i = 0; i < sizeof(key); i++) {
        if (readData(h, fileHandle, &key[i], 1, appContext))
            return PARSER_READ_ERROR;

        if (key[i] == 0)  // Item key terminator
            break;
        else if (key[i] < 0x20 || key[i] > 0x7E) {
            PARSERMSG("Invalid charactors\n");
            return PARSER_ERR_UNKNOWN;
        }
    }

    fileOffset = LocalFileTell(fileHandle, appContext);
    if (size > h->fileSize - fileOffset - APE_TAG_FOOTER_SIZE)
        return PARSER_ERR_UNKNOWN;

    if (flags & APE_TAG_FLAG_IS_BINARY) {
        uint8 buf[1024];
        uint32 bufLen = sizeof(buf);
        uint32 readLen = size < (bufLen - 1) ? size : (bufLen - 1);
        uint32 idx = 0;
        for (idx = 0; idx < readLen; idx++) {
            if (readData(h, fileHandle, &buf[idx], 1, appContext))
                return PARSER_READ_ERROR;
            if (buf[idx] == 0)  // Item key terminator
                break;
        }
        if (idx == readLen)
            buf[idx] = 0;
        else
            readLen = idx + 1;

        if (readLen < size)
            size -= readLen;
        else
            return PARSER_SUCCESS;

        format = getArtWorkFormat((const char*)buf);
        if (format == USER_DATA_FORMAT_MAX) {
            PARSERMSG("unknow file extension");
            skipData(h, fileHandle, size, appContext);
            return PARSER_SUCCESS;
        }
    }

    value = (uint8*)LocalMalloc(size + 1);
    if (!value)
        return PARSER_INSUFFICIENT_MEMORY;

    if (readData(h, fileHandle, value, size, appContext))
        return PARSER_READ_ERROR;

    value[size] = '\0';

    return ApeParserSetMetaData(h, (const char*)key, value, size + 1, format);
}

int32 ApeParserReadHeader(APE_PARSER* h) {
    int32 ret = PARSER_SUCCESS;
    FslFileHandle fileHandle = h->fileHandle;
    void* appContext = h->appContext;
    APE_HEADER* pHeader = h->header;
    uint32 i = 0;
    int32 fret;
    uint8* buf = NULL;
    uint32 bufSize = 0, junkDataLen = 0;

    fret = LocalFileSeek(fileHandle, 0, SEEK_SET, appContext);
    if (fret < 0)
        BAILWITHERROR(PARSER_SEEK_ERROR);
    junkDataLen = h->fileSize > MAX_JUNK_DATA_LENGTH ? MAX_JUNK_DATA_LENGTH : h->fileSize;

    buf = (uint8*)LocalMalloc(junkDataLen);
    TESTMALLOC(buf);
    bufSize = junkDataLen;
    ret = readData(h, fileHandle, buf, bufSize, appContext);
    if (ret)
        BAILWITHERROR(PARSER_READ_ERROR);

    for (i = 3; i < bufSize; i++) {
        if (buf[i - 3] == 'M' && buf[i - 2] == 'A' && buf[i - 1] == 'C' && buf[i] == ' ')
            break;
    }

    if (i == bufSize)
        BAILWITHERROR(PARSER_ERR_INVALID_MEDIA);

    fret = LocalFileSeek(fileHandle, i + 1, SEEK_SET, appContext);
    if (fret < 0)
        BAILWITHERROR(PARSER_SEEK_ERROR);

    pHeader = (APE_HEADER*)LocalMalloc(sizeof(APE_HEADER));
    TESTMALLOC(pHeader);

    ret = read16(h, fileHandle, &(pHeader->fileVersion), appContext);
    if (ret)
        goto bail;
    if (pHeader->fileVersion >= 3980) {
        ret = read16(h, fileHandle, &(pHeader->padding), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->descriptorLen), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->headerLen), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->seekTableLen), appContext);
        if (ret)
            goto bail;
        pHeader->seekTableLen /= 4;
        ret = read32(h, fileHandle, &(pHeader->waveHeaderLen), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->audioDataLen), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->audioDataLenHigh), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->waveTailLen), appContext);
        if (ret)
            goto bail;
        for (i = 0; i < 8; i++) {
            ret = read16(h, fileHandle, &(pHeader->md5[i]), appContext);
            if (ret)
                goto bail;
        }

        if (pHeader->descriptorLen > 52) {
            ret = skipData(h, fileHandle, pHeader->descriptorLen - 52, appContext);
            if (ret)
                goto bail;
        }

        ret = read16(h, fileHandle, &(pHeader->compressionLevel), appContext);
        if (ret)
            goto bail;
        ret = read16(h, fileHandle, &(pHeader->formatFlags), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->blocksPerFrame), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->finalFrameBlocks), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->totalFrames), appContext);
        if (ret)
            goto bail;
        ret = read16(h, fileHandle, &(pHeader->bitsPerSample), appContext);
        if (ret)
            goto bail;
        ret = read16(h, fileHandle, &(pHeader->channelsNum), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->sampleRate), appContext);
        if (ret)
            goto bail;
    } else {
        pHeader->descriptorLen = 0;
        pHeader->headerLen = 32;
        ret = read16(h, fileHandle, &(pHeader->compressionLevel), appContext);
        if (ret)
            goto bail;
        ret = read16(h, fileHandle, &(pHeader->formatFlags), appContext);
        if (ret)
            goto bail;
        ret = read16(h, fileHandle, &(pHeader->channelsNum), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->sampleRate), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->waveHeaderLen), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->waveTailLen), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->totalFrames), appContext);
        if (ret)
            goto bail;
        ret = read32(h, fileHandle, &(pHeader->finalFrameBlocks), appContext);
        if (ret)
            goto bail;

        if (pHeader->formatFlags & 4) {
            ret = skipData(h, fileHandle, 4, appContext);
            if (ret)
                goto bail;
            pHeader->headerLen += 4;
        }
        if (pHeader->formatFlags & 16) {
            ret = read32(h, fileHandle, &(pHeader->seekTableLen), appContext);
            if (ret)
                goto bail;
            pHeader->headerLen += 4;
        } else {
            pHeader->seekTableLen = pHeader->totalFrames;
        }

        if (pHeader->formatFlags & 1)
            pHeader->bitsPerSample = 8;
        else if (pHeader->formatFlags & 8)
            pHeader->bitsPerSample = 24;
        else
            pHeader->bitsPerSample = 16;

        if (pHeader->fileVersion >= 3950)
            pHeader->blocksPerFrame = 73728 * 4;
        else if (pHeader->fileVersion >= 3900 ||
                 (pHeader->fileVersion >= 3800 && pHeader->compressionLevel >= 4000))
            pHeader->blocksPerFrame = 73728;
        else
            pHeader->blocksPerFrame = 9216;

        if (!(pHeader->formatFlags & 32)) {
            ret = skipData(h, fileHandle, pHeader->waveHeaderLen, appContext);
            if (ret)
                goto bail;
        }
    }

    if (0 == pHeader->totalFrames || pHeader->seekTableLen < pHeader->totalFrames)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER);

bail:
    if (ret)
        LocalFree(pHeader);
    else
        h->header = pHeader;
    LocalFree(buf);
    return ret;
}

int32 ApeParserReadSeekTable(APE_PARSER* h) {
    int32 ret = PARSER_SUCCESS;
    FslFileHandle fileHandle = h->fileHandle;
    void* appContext = h->appContext;
    uint32* pSeekTable = NULL;
    uint32 seekTableLen = h->header->seekTableLen;
    uint32 totalFrames = h->header->totalFrames;
    APE_FRAME* pFrame = NULL;
    ;
    uint8* pBitTable = NULL;
    uint32 i = 0;
    int64 totalFileSize = 0, restFileSize = 0;
    uint64 startTime = 0;

    pSeekTable = LocalMalloc(seekTableLen * sizeof(uint32));
    TESTMALLOC(pSeekTable);

    for (i = 0; i < seekTableLen; i++) {
        ret = read32(h, fileHandle, &(pSeekTable[i]), appContext);
        if (ret)
            goto bail;
    }

    if (h->header->fileVersion < 3810) {
        pBitTable = LocalMalloc(sizeof(totalFrames));
        TESTMALLOC(h->bitTable);
        ret = readData(h, fileHandle, pBitTable, totalFrames, appContext);
        if (ret)
            goto bail;
    }

    pFrame = LocalMalloc(totalFrames * sizeof(APE_FRAME));
    TESTMALLOC(pFrame);

    pFrame[0].offset = h->header->descriptorLen + h->header->seekTableLen * 4 +
                       h->header->waveHeaderLen + h->header->headerLen;
    pFrame[0].blocks = h->header->blocksPerFrame;
    pFrame[0].skip = 0;
    for (i = 1; i < totalFrames; i++) {
        pFrame[i].offset = pSeekTable[i];
        pFrame[i].blocks = h->header->blocksPerFrame;
        pFrame[i].skip = (pFrame[i].offset - pFrame[0].offset) & 0x3;
        pFrame[i - 1].size = pFrame[i].offset - pFrame[i - 1].offset;
        if (h->header->bitRate > 0)
            pFrame[i - 1].timestamp = pFrame[i - 1].size * 8 / h->header->bitRate;
    }
    pFrame[totalFrames - 1].blocks = h->header->finalFrameBlocks;

    if (h->header->fileVersion < 3810) {
        for (i = 0; i < totalFrames; i++) {
            if (i < totalFrames - 1 && pBitTable[i + 1])
                pFrame[i].size += 4;
            pFrame[i].skip <<= 3;
            pFrame[i].skip += pBitTable[i];
        }
    }

    startTime = 0;
    for (i = 0; i < totalFrames; i++) {
        if (pFrame[i].skip) {
            pFrame[i].offset -= pFrame[i].skip;
            pFrame[i].size += pFrame[i].skip;
        }
        pFrame[i].size = (pFrame[i].size + 3) & ~3;
        pFrame[i].timestamp = startTime;
        startTime += (uint64)pFrame[i].blocks * 1000000 / h->header->sampleRate;
        PARSERMSG("frameSize %d ts %lld blocks %d sampleRate %d", pFrame[i].size,
                  pFrame[i].timestamp, pFrame[i].blocks, h->header->sampleRate);
    }

    /* calculate the last frame size from total file size, if available */
    totalFileSize = h->fileSize;
    restFileSize = totalFileSize - pFrame[totalFrames - 1].offset - h->header->waveTailLen;
    restFileSize -= (restFileSize & 0x3);
    if (restFileSize <= 0)
        pFrame[totalFrames - 1].size = h->header->finalFrameBlocks * 8;
    else
        pFrame[totalFrames - 1].size = restFileSize;

bail:
    if (ret) {
        LocalFree(pFrame);
        LocalFree(pSeekTable);
        LocalFree(pBitTable);
    } else {
        h->frames = pFrame;
        h->seekTable = pSeekTable;
        h->bitTable = pBitTable;
    }
    return ret;
}

int32 ApeParserReadOneFrame(APE_PARSER* h, uint8** sampleBuffer, void** bufferContext,
                                          uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                                          uint32* sampleFlags) {
    int32 ret = PARSER_SUCCESS;
    FslFileHandle fileHandle = h->fileHandle;
    void* appContext = h->appContext;
    APE_FRAME* pFrames = h->frames;
    int32 fret;
    uint8* pBuffer;
    uint32 frameSize = 0;
    uint32 actualDataSize = 0;
    (void)usDuration;

    if (h->currFrameIndex >= h->lastFrameIndex)
        return PARSER_EOS;

    fret = LocalFileSeek(fileHandle, pFrames[h->currFrameIndex].offset + h->currFrameOffset,
                         SEEK_SET, appContext);
    if (fret < 0)
        return PARSER_SEEK_ERROR;

    frameSize = pFrames[h->currFrameIndex].size - h->currFrameOffset;

    if (h->currFrameOffset == 0)
        frameSize += APE_FRAME_EXTRA_SIZE;

    *dataSize = frameSize;
    *sampleBuffer = h->outputOps.RequestBuffer(0, dataSize, bufferContext, h->appContext);
    if (!(*sampleBuffer))
        return PARSER_INSUFFICIENT_MEMORY;

    pBuffer = *sampleBuffer;

    if (h->currFrameOffset == 0) {
        write32(pFrames[h->currFrameIndex].blocks, sampleBuffer);
        write32(pFrames[h->currFrameIndex].skip, sampleBuffer);
        actualDataSize = *dataSize - APE_FRAME_EXTRA_SIZE;
        *usStartTime = pFrames[h->currFrameIndex].timestamp;
    } else {
        actualDataSize = *dataSize;
        *usStartTime = (uint64)(-1);
    }

    ret = readData(h, fileHandle, *sampleBuffer, actualDataSize, appContext);
    if (ret)
        return PARSER_READ_ERROR;

    *sampleBuffer = pBuffer;
    *sampleFlags |= FLAG_SYNC_SAMPLE;

    if (*dataSize < frameSize) {
        *sampleFlags |= FLAG_SAMPLE_NOT_FINISHED;
        h->currFrameOffset += actualDataSize;
    } else {
        h->currFrameOffset = 0;
        h->currFrameIndex++;
    }
    PARSERMSG("getOneFrame0 %p %p %d %lld %d flag %x", *sampleBuffer, *bufferContext, *dataSize,
              *usStartTime, h->currFrameIndex, *sampleFlags);

    return PARSER_SUCCESS;
}

int32 ApeParserDoSeek(APE_PARSER* h, uint64* usTime, uint32 flag) {

    APE_FRAME* pFrames = h->frames;
    uint32 i;
    uint32 totalFrames = h->header->totalFrames;
    uint64 seekTime = *usTime;
    uint64 seekTopTime, seekBottomTime;
    uint32 seekTopIndex, seekBottomIndex, seekIndex;

    if (0 == seekTime) {
        h->currFrameIndex = 0;
        h->currFrameOffset = 0;
        return PARSER_SUCCESS;
    } else {
        for (i = 0; i < totalFrames; i++) {
            if (pFrames[i].timestamp > seekTime)
                break;
        }
        if (i == totalFrames) {
            if (SEEK_FLAG_NO_EARLIER == flag)
                return PARSER_EOS;
            seekTopIndex = seekBottomIndex = i - 1;
            seekTopTime = seekBottomTime = pFrames[i - 1].timestamp;
        } else {
            seekTopIndex = i - 1;
            seekTopTime = pFrames[seekTopIndex].timestamp;
            seekBottomIndex = i;
            seekBottomTime = pFrames[seekBottomIndex].timestamp;
        }
    }

    if (SEEK_FLAG_NO_EARLIER == flag) {
        seekTime = seekBottomTime;
        seekIndex = seekBottomIndex;
    } else if (SEEK_FLAG_NEAREST == flag) {
        if (seekTime - seekTopTime < seekBottomTime - seekTime) {
            seekTime = seekTopTime;
            seekIndex = seekTopIndex;
        } else {
            seekTime = seekBottomTime;
            seekIndex = seekBottomIndex;
        }
    } else {  // no later
        seekTime = seekTopTime;
        seekIndex = seekTopIndex;
    }

    h->currFrameIndex = seekIndex;
    h->currFrameOffset = 0;
    *usTime = seekTime;
    PARSERMSG("seek to %lld frameIndex %d", *usTime, seekIndex);
    return PARSER_SUCCESS;
}

int32 ApeParserReadTag(APE_PARSER* h) {
    int32 ret = PARSER_SUCCESS;
    FslFileHandle fileHandle = h->fileHandle;
    void* appContext = h->appContext;
    int32 fret;
    uint8 buf[8];
    uint32 i, tagVersion, tagSize, fieldsNum, flag;

    if (h->isLive)
        return PARSER_ERR_INVALID_PARAMETER;

    if (h->fileSize <= APE_TAG_FOOTER_SIZE)
        return PARSER_SUCCESS;

    fret = LocalFileSeek(fileHandle, h->fileSize - APE_TAG_FOOTER_SIZE, SEEK_SET, appContext);
    if (fret < 0)
        return PARSER_SEEK_ERROR;

    ret = readData(h, fileHandle, buf, 8, appContext);
    if (ret)
        goto bail;
    if (strncmp((const char*)buf, "APETAGEX", 8))
        goto bail;

    ret = read32(h, fileHandle, &tagVersion, appContext);
    if (ret || tagVersion > APE_TAG_VERSION)
        goto bail;

    ret = read32(h, fileHandle, &tagSize, appContext);
    if (ret)
        goto bail;
    if (tagSize > APE_TAG_MAX_LENGTH || tagSize > h->fileSize - APE_TAG_FOOTER_SIZE)
        goto bail;  // too big tag size

    ret = read32(h, fileHandle, &fieldsNum, appContext);
    if (ret)
        goto bail;
    if (fieldsNum > APE_TAG_MAX_FIELD_NUM)
        goto bail;  // too many items

    ret = read32(h, fileHandle, &flag, appContext);
    if (ret)
        goto bail;
    if (APE_TAG_FLAG_IS_HEADER == flag)
        goto bail;  // wrong flag

    fret = LocalFileSeek(fileHandle, h->fileSize - tagSize, SEEK_SET, appContext);
    if (fret < 0)
        return PARSER_SEEK_ERROR;

    for (i = 0; i < fieldsNum; i++) {
        ret = ApeParserReadOneItem(h);
        if (ret)
            break;
    }

bail:
    if (ret) {
        PARSERMSG("APE parser read tag error %x", ret);
    }

    return PARSER_SUCCESS;
}

/* End of File*/
