/*
***********************************************************************
* Copyright (c) 2015, Freescale Semiconductor, Inc.
* Copyright 2017-2018, 2020, 2025-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
#include <string.h>
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

#if defined WIN32 || __WINCE
#pragma warning(disable : 4100)
#pragma warning(disable : 4152)
#endif
#ifdef __WINCE
#define OS_NAME "_WINCE"
#else
#define OS_NAME ""
#endif

#define BASELINE_SHORT_NAME "APE_PARSER_01.00.00"

#define SEPARATOR " "
/* user define suffix */
#define VERSION_STR_SUFFIX ""

#define PARSER_VERSION_STR                                              \
    (BASELINE_SHORT_NAME OS_NAME SEPARATOR VERSION_STR_SUFFIX SEPARATOR \
     "build on" SEPARATOR __DATE__ SEPARATOR __TIME__)

char* ApeParserVersionInfo() {
    return PARSER_VERSION_STR;
}

/***************************************************************************************
 *
 *                Creation & Deletion
 *
 ***************************************************************************************/
int32 ApeCreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                      ParserOutputBufferOps* outputBufferOps, void* context,
                      FslParserHandle* parserHandle) {
    APE_PARSER* h = NULL;
    int32 ret;

    if (NULL == streamOps || NULL == memOps || NULL == outputBufferOps || NULL == context ||
        NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    *parserHandle = NULL;
    h = memOps->Malloc(sizeof(APE_PARSER));
    if (NULL == h)
        return PARSER_INSUFFICIENT_MEMORY;

    memset((void*)h, 0, sizeof(APE_PARSER));
    memcpy(&(h->streamOps), streamOps, sizeof(FslFileStream));
    memcpy(&(h->memOps), memOps, sizeof(ParserMemoryOps));
    memcpy(&(h->outputOps), outputBufferOps, sizeof(ParserOutputBufferOps));

    h->fileHandle = streamOps->Open(NULL, (const uint8*)"rb", context); /* Open a file or URL */
    if (NULL == h->fileHandle) {
        ret = PARSER_FILE_OPEN_ERROR;
        goto bail;
    }

    h->appContext = context;
    h->isLive = isLive;
    h->fileSize = (uint64)LocalFileSize(h->fileHandle, h->appContext);

    ret = ApeParserReadHeader(h);
    if (ret)
        goto bail;
    ret = ApeParserReadSeekTable(h);
    if (ret)
        goto bail;

    if (!h->isLive) {
        if (ApeParserReadTag(h) != PARSER_SUCCESS){
            PARSERMSG("APE PARSER parse tag failed\n");
        }
    }

    h->currFrameIndex = 0;
    h->currFrameOffset = 0;
    h->lastFrameIndex = h->header->totalFrames;
    h->dataChunkSize = h->fileSize - h->frames[0].offset - h->header->waveTailLen;
    h->totalSamples =
            h->header->finalFrameBlocks + (h->header->totalFrames - 1) * h->header->blocksPerFrame;
    if (h->header->sampleRate != 0) {
        h->usDuration = (uint64)h->totalSamples * 1000000 / h->header->sampleRate;
        h->header->bitRate = (uint64)h->dataChunkSize * 8 * 1000000 / h->usDuration;
    } else {
        h->usDuration = h->header->bitRate = 0;
    }

bail:
    if (PARSER_SUCCESS != ret) {
        h->memOps.Free(h);
        h = NULL;
        PARSERMSG("ApeCreateParser failed as 0x%08x", ret);
    }
    *parserHandle = h;

    return ret;
}

int32 ApeDeleteParser(FslParserHandle parserHandle) {
    APE_PARSER* h = (APE_PARSER*)parserHandle;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (h->header) {
        LocalFree(h->header);
        h->header = NULL;
    }
    if (h->seekTable) {
        LocalFree(h->seekTable);
        h->seekTable = NULL;
    }
    if (h->frames) {
        LocalFree(h->frames);
        h->frames = NULL;
    }

    if (h->bitTable) {
        LocalFree(h->bitTable);
        h->bitTable = NULL;
    }

    LocalFree(h);
    h = NULL;

    return PARSER_SUCCESS;
}

/************************************************************************************************************
 *
 *               Movie Properties
 *
 ************************************************************************************************************/
int32 ApeParserIsSeekable(FslParserHandle parserHandle, bool* seekable) {
    APE_PARSER* h = parserHandle;
    if (!h || !seekable)
        return PARSER_ERR_INVALID_PARAMETER;

    if (h->isLive)
        *seekable = 0;
    else
        *seekable = 1;

    PARSERMSG("is seekable: %d \r\n", *seekable);
    return PARSER_SUCCESS;
}

int32 ApeParserGetNumTracks(FslParserHandle parserHandle, uint32* numTracks) {
    int32 ret = PARSER_SUCCESS;
    if (!parserHandle || !numTracks)
        return PARSER_ERR_INVALID_PARAMETER;

    *numTracks = 1;

    PARSERMSG("Total Track Number: %d \r\n", *numTracks);
    return ret;
}

int32 ApeParserGetMetaData(FslParserHandle parserHandle, UserDataID userDataId,
                           UserDataFormat* userDataFormat, uint8** userData,
                           uint32* userDataLength) {
    APE_PARSER* h = NULL;

    if (!parserHandle || !userDataFormat || !userData || !userDataLength)
        return PARSER_ERR_INVALID_PARAMETER;

    h = (APE_PARSER*)parserHandle;

    return ApeParserDoGetMetaData(h, userDataId, userDataFormat, userData, userDataLength);
}

int32 ApeParserGetMovieDuration(FslParserHandle parserHandle, uint64* usDuration) {
    int32 ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;

    if (!parserHandle || !usDuration)
        return PARSER_ERR_INVALID_PARAMETER;

    *usDuration = h->usDuration;
    PARSERMSG("Movie Duration: %d ms \r\n", *usDuration);
    return ret;
}

/************************************************************************************************************
 *
 *              General Track Properties
 *
 ************************************************************************************************************/
int32 ApeParserGetTrackType(FslParserHandle parserHandle, uint32 trackNum, uint32* mediaType,
                            uint32* decoderType, uint32* decoderSubtype) {
    int ret = PARSER_SUCCESS;

    if (!parserHandle || !mediaType || !decoderType || !decoderSubtype)
        return PARSER_ERR_INVALID_PARAMETER;

    if (trackNum > 10)
        return PARSER_ERR_INVALID_PARAMETER;

    // Add the audio codec inform here
    *mediaType = MEDIA_AUDIO;
    *decoderType = AUDIO_APE;

    return ret;
}

int32 ApeParserGetTrackDuration(FslParserHandle parserHandle, uint32 trackNum, uint64* usDuration) {
    int32 ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;

    if (!h || !usDuration)
        return PARSER_ERR_INVALID_PARAMETER;

    *usDuration = h->usDuration;
    (void)trackNum;

    return ret;
}

int32 ApeParserGetBitRate(FslParserHandle parserHandle, uint32 trackNum, uint32* bitrate) {
    int ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;

    if (!h || !bitrate)
        return PARSER_ERR_INVALID_PARAMETER;

    *bitrate = h->header->bitRate;
    (void)trackNum;

    return ret;
}

/************************************************************************************************************
 *
 *               Audio Properties
 *
 ************************************************************************************************************/
int32 ApeParserGetAudioNumChannels(FslParserHandle parserHandle, uint32 trackNum,
                                   uint32* numchannels) {
    int ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;

    if (!h || !numchannels)
        return PARSER_ERR_INVALID_PARAMETER;

    *numchannels = h->header->channelsNum;
    (void)trackNum;

    return ret;
}

int32 ApeParserGetAudioSampleRate(FslParserHandle parserHandle, uint32 trackNum,
                                  uint32* sampleRate) {
    int ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;

    if (!h || !sampleRate)
        return PARSER_ERR_INVALID_PARAMETER;

    *sampleRate = h->header->sampleRate;
    (void)trackNum;

    return ret;
}

int32 ApeParserGetAudioBitsPerSample(FslParserHandle parserHandle, uint32 trackNum,
                                     uint32* bitsPerSample) {
    int ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;

    if (!h || !bitsPerSample)
        return PARSER_ERR_INVALID_PARAMETER;

    *bitsPerSample = h->header->bitsPerSample;
    (void)trackNum;

    return ret;
}

int32 ApeParserGetAudioBlockAlign(FslParserHandle parserHandle, uint32 trackNum,
                                  uint32* blockAlign) {
    int ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;

    if (!h || !blockAlign)
        return PARSER_ERR_INVALID_PARAMETER;

    *blockAlign = 8;
    (void)trackNum;

    return ret;
}

/************************************************************************************************************
 *
 *               Sample Reading, Seek & Trick Mode
 *
 ************************************************************************************************************/
int32 ApeParserGetReadMode(FslParserHandle parser_handle, uint32* readMode) {
    if (!parser_handle || !readMode)
        return PARSER_ERR_INVALID_PARAMETER;

    *readMode = PARSER_READ_MODE_FILE_BASED;

    return PARSER_SUCCESS;
}

int32 ApeParserSetReadMode(FslParserHandle parser_handle, uint32 readMode) {
    if (!parser_handle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (PARSER_READ_MODE_FILE_BASED != readMode) {
        PARSERMSG("SetReadMode is not PARSER_READ_MODE_FILE_BASED ! \r\n");
        return PARSER_NOT_IMPLEMENTED;
    }

    return PARSER_SUCCESS;
}

int32 ApeParserEnableTrack(FslParserHandle parserHandle, uint32 trackNum, bool enable) {
    int32 ret = PARSER_SUCCESS;
    (void)parserHandle;
    (void)trackNum;
    (void)enable;

    return ret;
}

int32 ApeParserGetFileNextSample(FslParserHandle parserHandle, uint32* trackNum,
                                 uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                                 uint64* usStartTime, uint64* usDuration, uint32* sampleFlags) {
    int32 ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;

    if (!trackNum || !sampleBuffer || !bufferContext || !dataSize || !usStartTime || !usDuration ||
        !sampleFlags || !h)
        return PARSER_ERR_INVALID_PARAMETER;

    *trackNum = 0;
    ret = ApeParserReadOneFrame(h, sampleBuffer, bufferContext, dataSize, usStartTime, usDuration,
                                sampleFlags);

    if (PARSER_SUCCESS != ret) {
        h->outputOps.ReleaseBuffer(0, *sampleBuffer, *bufferContext, h->appContext);
        (*sampleBuffer) = NULL;
    }
    PARSERMSG("getOneFrame %p %p %d %x", *sampleBuffer, *bufferContext, *dataSize, *sampleFlags);

    return ret;
}

int32 ApeParserSeek(FslParserHandle parserHandle, uint32 trackNum, uint64* usTime, uint32 flag) {
    int32 ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;
    (void)trackNum;

    if (!h || !usTime || h->isLive)
        return PARSER_ERR_INVALID_PARAMETER;

    ret = ApeParserDoSeek(h, usTime, flag);

    return ret;
}

int32 ApeParserGetCodecSpecificInfo(FslParserHandle parserHandle, uint32 trackNum, uint8** data,
                                    uint32* size) {
    int32 ret = PARSER_SUCCESS;
    APE_PARSER* h = (APE_PARSER*)parserHandle;
    uint8* pTemp;
    (void)trackNum;

    if (!h || !size || !data)
        return PARSER_ERR_INVALID_PARAMETER;

    *data = LocalMalloc(APE_CODEC_EXTRADATA_SIZE);
    TESTMALLOC(*data);
    pTemp = *data;
    write16(h->header->fileVersion, data);
    write16(h->header->compressionLevel, data);
    write16(h->header->formatFlags, data);

    *data = pTemp;
    *size = APE_CODEC_EXTRADATA_SIZE;

bail:
    if (ret) {
        LocalFree(*data);
        *data = NULL;
    }
    return ret;
}

/***************************************************************************************
 *                 DLL entry point
 ***************************************************************************************/
int32 FslParserQueryInterface(uint32 id, void** func) {
    int32 err = PARSER_SUCCESS;

    if (!func)
        return PARSER_ERR_INVALID_PARAMETER;

    *func = NULL;

    switch (id) {
        /* parser version information */
        case PARSER_API_GET_VERSION_INFO:
            *func = ApeParserVersionInfo;
            break;

        /* creation & deletion */
        case PARSER_API_CREATE_PARSER:
            *func = ApeCreateParser;
            break;

        case PARSER_API_DELETE_PARSER:
            *func = ApeDeleteParser;
            break;

        /* movie properties */
        case PARSER_API_IS_MOVIE_SEEKABLE:
            *func = ApeParserIsSeekable;
            break;

        case PARSER_API_GET_MOVIE_DURATION:
            *func = ApeParserGetMovieDuration;
            break;

        case PARSER_API_GET_META_DATA:
            *func = ApeParserGetMetaData;
            break;

        case PARSER_API_GET_NUM_TRACKS:
            *func = ApeParserGetNumTracks;
            break;

        /* generic track properties */
        case PARSER_API_GET_TRACK_TYPE:
            *func = ApeParserGetTrackType;
            break;

        case PARSER_API_GET_DECODER_SPECIFIC_INFO:
            *func = ApeParserGetCodecSpecificInfo;
            break;

        case PARSER_API_GET_TRACK_DURATION:
            *func = ApeParserGetTrackDuration;
            break;

        case PARSER_API_GET_BITRATE:
            *func = ApeParserGetBitRate;
            break;

        /* audio properties */
        case PARSER_API_GET_AUDIO_NUM_CHANNELS:
            *func = ApeParserGetAudioNumChannels;
            break;

        case PARSER_API_GET_AUDIO_SAMPLE_RATE:
            *func = ApeParserGetAudioSampleRate;
            break;
        case PARSER_API_GET_AUDIO_BLOCK_ALIGN:
            *func = ApeParserGetAudioBlockAlign;
            break;
        case PARSER_API_GET_AUDIO_BITS_PER_SAMPLE:
            *func = ApeParserGetAudioBitsPerSample;
            break;

        /* sample reading, seek & trick mode */
        case PARSER_API_GET_READ_MODE:
            *func = ApeParserGetReadMode;
            break;

        case PARSER_API_SET_READ_MODE:
            *func = ApeParserSetReadMode;
            break;

        case PARSER_API_ENABLE_TRACK:
            *func = ApeParserEnableTrack;
            break;

        case PARSER_API_GET_FILE_NEXT_SAMPLE:
            *func = ApeParserGetFileNextSample;
            break;

        case PARSER_API_SEEK:
            *func = ApeParserSeek;
            break;

            /* index table import/export */
#if 0
        case PARSER_API_INITIALIZE_INDEX:
            *func = ApeParserInitializeIndex;
            break;

        case PARSER_API_IMPORT_INDEX:
            *func = ApeParserImportIndex;
            break;

        case PARSER_API_EXPORT_INDEX:
            *func = ApeParserExportIndex;
            break;
#endif

        default:
            break; /* no support for other API */
    }

    return err;
}
