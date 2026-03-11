/*
***********************************************************************
* Copyright (c) 2012-2016, Freescale Semiconductor, Inc.
* Copyright 2017-2022, 2024-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
*
***********************************************************************
*/
#include "mp3_parser_api.h"
#include "audio_parser_base.h"

#if defined WIN32 || __WINCE
#pragma warning(disable : 4100)
#pragma warning(disable : 4152)
#endif
#ifdef __WINCE
#define OS_NAME "_WINCE"
#else
#define OS_NAME ""
#endif

#define BASELINE_SHORT_NAME "MP3PARSER_04.00.00"

#define SEPARATOR " "
/* user define suffix */
#define VERSION_STR_SUFFIX ""

#define PARSER_VERSION_STR                                              \
    (BASELINE_SHORT_NAME OS_NAME SEPARATOR VERSION_STR_SUFFIX SEPARATOR \
     "build on" SEPARATOR __DATE__ SEPARATOR __TIME__)

EXTERN char* MP3ParserVersionInfo() {
    return PARSER_VERSION_STR;
}

/***************************************************************************************
 *
 *                Creation & Deletion
 *
 ***************************************************************************************/
EXTERN int32 MP3CreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                             ParserOutputBufferOps* outputBufferOps, void* context,
                             FslParserHandle* parserHandle) {
    uint32 flags = 0;

    if (NULL == streamOps || NULL == memOps || NULL == outputBufferOps || NULL == context ||
        NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (isLive) {
        flags |= FILE_FLAG_NON_SEEKABLE;
        flags |= FILE_FLAG_READ_IN_SEQUENCE;
    }

    return mp3_parser_open(parserHandle, flags, streamOps, memOps, outputBufferOps, context);
}

EXTERN int32 MP3CreateParser2(uint32 flags, FslFileStream* streamOps, ParserMemoryOps* memOps,
                              ParserOutputBufferOps* outputBufferOps, void* context,
                              FslParserHandle* parserHandle) {
    if (NULL == streamOps || NULL == memOps || NULL == outputBufferOps || NULL == context ||
        NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    return mp3_parser_open(parserHandle, flags, streamOps, memOps, outputBufferOps, context);
}

EXTERN int32 MP3DeleteParser(FslParserHandle parserHandle) {
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    return mp3_parser_close(parserHandle);
}

/***************************************************************************************
 *
 *                 Index Table Loading, Export & Import
 *
 ***************************************************************************************/
EXTERN int32 MP3ParserInitializeIndex(FslParserHandle parserHandle) {
    int32 ret = PARSER_SUCCESS;
    if (!parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    return ret;
}

EXTERN int32 MP3ParserImportIndex(FslParserHandle parserHandle, uint8* buffer, uint32 size) {
    int32 ret = PARSER_SUCCESS;

    if (!parserHandle || !size)
        return PARSER_ERR_INVALID_PARAMETER;

    ret = ImportIndexTable(&((mp3_parser_t*)parserHandle)->mp3_parser_core, buffer, size);

    return ret;
}

EXTERN int32 MP3ParserExportIndex(FslParserHandle parserHandle, uint8* buffer, uint32* size) {
    int32 ret = PARSER_SUCCESS;
    Audio_Parser_Base_t* pParserBase;

    if (!parserHandle || !size)
        return PARSER_ERR_INVALID_PARAMETER;

    pParserBase = &((mp3_parser_t*)parserHandle)->mp3_parser_core;

    if (NULL == buffer) {
        // app query the size of the index table
        *size = GetIndexTableSize(pParserBase);
        *size += sizeof(IndexTableHdr_t);
    } else {
        // implement the index table exporting
        uint32 nLocalSize;

        nLocalSize = GetIndexTableSize(pParserBase);
        if (*size < (nLocalSize + sizeof(IndexTableHdr_t))) {
            PARSERMSG("Index export buffer is not large enough\n\t");
            return PARSER_NOT_IMPLEMENTED;  //
        }
        // copy index table items here
        ret = ExportIndexTable(&((mp3_parser_t*)parserHandle)->mp3_parser_core, buffer);
    }

    return ret;
}

/************************************************************************************************************
 *
 *               Movie Properties
 *
 ************************************************************************************************************/
EXTERN int32 MP3ParserIsSeekable(FslParserHandle parserHandle, bool* seekable) {
    if (!parserHandle || !seekable)
        return PARSER_ERR_INVALID_PARAMETER;

    *seekable = 1;  // Seekable

    PARSERMSG("is seekable: %d \r\n", *seekable);
    return PARSER_SUCCESS;
}

EXTERN int32 MP3ParserGetNumTracks(FslParserHandle parserHandle, uint32* numTracks) {
    int32 ret = PARSER_SUCCESS;
    if (!parserHandle || !numTracks)
        return PARSER_ERR_INVALID_PARAMETER;

    *numTracks = 1;

    PARSERMSG("Total Track Number: %d \r\n", *numTracks);
    return ret;
}

EXTERN int32 MP3ParserGetMetaData(FslParserHandle parserHandle, UserDataID userDataId,
                                  UserDataFormat* userDataFormat, uint8** userData,
                                  uint32* userDataLength) {
    int32 ret = PARSER_SUCCESS;
    Audio_Parser_Base_t* pParserBase = &((mp3_parser_t*)parserHandle)->mp3_parser_core;

    if ((parserHandle == NULL) || (userDataFormat == NULL) || (userData == NULL) ||
        (userDataLength == NULL)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *userDataLength = 0;
    *userData = NULL;

#ifdef PARSER_MP3_LAME_ENC_TAG
    if (USER_DATA_AUD_ENC_DELAY == userDataId) {
        if (pParserBase->FrameInfo.lame_exist) {
            *userDataLength = 4;
            *userDataFormat = USER_DATA_FORMAT_INT_LE;
            *userData = (uint8*)(&(pParserBase->FrameInfo.enc_delay));
            goto bail;
        }
    }

    if (USER_DATA_AUD_ENC_PADDING == userDataId) {
        if (pParserBase->FrameInfo.lame_exist) {
            *userDataLength = 4;
            *userDataFormat = USER_DATA_FORMAT_INT_LE;
            *userData = (uint8*)(&(pParserBase->FrameInfo.enc_padding));
            goto bail;
        }
    }
#endif

    if (pParserBase->hID3 == NULL) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    ret = ID3ParserGetMetaData(pParserBase->hID3, userDataId, userDataFormat, userData,
                               userDataLength);

bail:
    return ret;
}

EXTERN int32 MP3ParserGetMovieDuration(FslParserHandle parserHandle, uint64* usDuration) {
    int32 ret = PARSER_SUCCESS;
    mp3_parser_t* pMp3Parser = NULL;

    if (!parserHandle || !usDuration)
        return PARSER_ERR_INVALID_PARAMETER;

    pMp3Parser = (mp3_parser_t*)parserHandle;

    if (MP3_DURATION_SCAN_THRESHOLD >= pMp3Parser->mp3_parser_core.nFileSize &&
        !(pMp3Parser->mp3_parser_core.LiveFlag & FILE_FLAG_NON_SEEKABLE)) {
        // Scan VBR Duration here
        MP3ParserGetTrackDuration(parserHandle, 0, usDuration);
    } else
        *usDuration = ((mp3_parser_t*)parserHandle)->mp3_parser_core.usDuration;
    PARSERMSG("Movie Duration: %d ms \r\n", *usDuration);
    return ret;
}

/************************************************************************************************************
 *
 *              General Track Properties
 *
 ************************************************************************************************************/
EXTERN int32 MP3ParserGetTrackType(FslParserHandle parserHandle, uint32 trackNum, uint32* mediaType,
                                   uint32* decoderType, uint32* decoderSubtype) {
    int ret = PARSER_SUCCESS;

    if (!parserHandle || !mediaType || !decoderType || !decoderSubtype)
        return PARSER_ERR_INVALID_PARAMETER;

    if (trackNum > 10)
        return PARSER_ERR_INVALID_PARAMETER;

    // Add the audio codec inform here
    *mediaType = MEDIA_AUDIO;
    *decoderType = AUDIO_MP3;

    return ret;
}

EXTERN int32 MP3ParserGetTrackDuration(FslParserHandle parserHandle, uint32 trackNum,
                                       uint64* usDuration) {
    int32 ret = PARSER_SUCCESS;

    if (!parserHandle || !usDuration)
        return PARSER_ERR_INVALID_PARAMETER;

    if (FALSE == ((mp3_parser_t*)parserHandle)->mp3_parser_core.bCBR)
        ParserCalculateVBRDuration(&(((mp3_parser_t*)parserHandle)->mp3_parser_core));

    *usDuration = ((mp3_parser_t*)parserHandle)->mp3_parser_core.usDuration;  // sample
    (void)trackNum;

    return ret;
}

EXTERN int32 MP3ParserGetCodecSpecificInfo(FslParserHandle parserHandle, uint32 trackNum,
                                           uint8** data, uint32* size) {
    int32 ret = PARSER_SUCCESS;

    if (!parserHandle || !size || !data)
        return PARSER_ERR_INVALID_PARAMETER;

    *size = 0;
    (void)trackNum;
    return ret;
}

EXTERN int32 MP3ParserGetBitRate(FslParserHandle parserHandle, uint32 trackNum, uint32* bitrate) {
    int ret = PARSER_SUCCESS;

    if (!parserHandle || !bitrate)
        return PARSER_ERR_INVALID_PARAMETER;

    *bitrate = ((mp3_parser_t*)parserHandle)->mp3_parser_core.nAvrageBitRate;
    (void)trackNum;
    return ret;
}

/************************************************************************************************************
 *
 *               Audio Properties
 *
 ************************************************************************************************************/
EXTERN int32 MP3ParserGetAudioNumChannels(FslParserHandle parserHandle, uint32 trackNum,
                                          uint32* numchannels) {
    if (!parserHandle || !numchannels)
        return PARSER_ERR_INVALID_PARAMETER;

    *numchannels = ((mp3_parser_t*)parserHandle)->mp3_parser_core.nChannels;
    (void)trackNum;
    return PARSER_SUCCESS;
}

EXTERN int32 MP3ParserGetAudioSampleRate(FslParserHandle parserHandle, uint32 trackNum,
                                         uint32* sampleRate) {
    if (!parserHandle || !sampleRate)
        return PARSER_ERR_INVALID_PARAMETER;

    *sampleRate = ((mp3_parser_t*)parserHandle)->mp3_parser_core.nSampleRate;
    (void)trackNum;
    return PARSER_SUCCESS;
}

EXTERN int32 MP3ParserGetAudioBitsPerSample(FslParserHandle parserHandle, uint32 trackNum,
                                            uint32* bitsPerSample) {
    if (!parserHandle || !bitsPerSample)
        return PARSER_ERR_INVALID_PARAMETER;

    *bitsPerSample = ((mp3_parser_t*)parserHandle)->mp3_parser_core.FileInfo.nBitPerSample;  // 16;
    (void)trackNum;
    return PARSER_SUCCESS;
}

EXTERN int32 MP3ParserGetAudioBlockAlign(FslParserHandle parserHandle, uint32 trackNum,
                                         uint32* blockAlign) {
    if (!parserHandle || !blockAlign)
        return PARSER_ERR_INVALID_PARAMETER;

    *blockAlign = 8;
    (void)trackNum;
    return PARSER_SUCCESS;
}

/************************************************************************************************************
 *
 *               Sample Reading, Seek & Trick Mode
 *
 ************************************************************************************************************/
EXTERN int32 MP3ParserGetReadMode(FslParserHandle parser_handle, uint32* readMode) {
    if (!parser_handle || !readMode)
        return PARSER_ERR_INVALID_PARAMETER;

    *readMode = PARSER_READ_MODE_FILE_BASED;

    return PARSER_SUCCESS;
}

EXTERN int32 MP3ParserSetReadMode(FslParserHandle parser_handle, uint32 readMode) {
    if (!parser_handle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (PARSER_READ_MODE_FILE_BASED != readMode) {
        PARSERMSG("SetReadMode is not PARSER_READ_MODE_FILE_BASED ! \r\n");
        return PARSER_NOT_IMPLEMENTED;
    }

    return PARSER_SUCCESS;
}

EXTERN int32 MP3ParserEnableTrack(FslParserHandle parserHandle, uint32 trackNum, bool enable) {
    int32 ret = PARSER_SUCCESS;
    (void)parserHandle;
    (void)trackNum;
    (void)enable;
    return ret;
}

EXTERN int32 MP3ParserGetFileNextSample(FslParserHandle parserHandle, uint32* trackNum,
                                        uint8** sampleBuffer, void** bufferContext,
                                        uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                                        uint32* sampleFlags) {
    int32 ret = PARSER_SUCCESS;
    mp3_parser_t* pMp3Parser = NULL;

    if (!trackNum || !sampleBuffer || !bufferContext || !dataSize || !usStartTime || !usDuration ||
        !sampleFlags)
        return PARSER_ERR_INVALID_PARAMETER;

    *trackNum = 0;
    pMp3Parser = (mp3_parser_t*)parserHandle;

    ret = GetNextSample(&(pMp3Parser->mp3_parser_core), &(pMp3Parser->outputOps),
                        pMp3Parser->appContext, sampleBuffer, bufferContext, dataSize, usStartTime,
                        usDuration, sampleFlags);
    return ret;
}

EXTERN int32 MP3ParserSeek(FslParserHandle parserHandle, uint32 trackNum, uint64* usTime,
                           uint32 flag) {
    int32 ret = PARSER_SUCCESS;

    if (!parserHandle || !usTime)
        return PARSER_ERR_INVALID_PARAMETER;

    ret = Seek(&(((mp3_parser_t*)parserHandle)->mp3_parser_core), usTime, flag);
    (void)trackNum;

    return ret;
}

/***************************************************************************************
 *                 DLL entry point
 ***************************************************************************************/
EXTERN int32 FslParserQueryInterface(uint32 id, void** func) {
    int32 err = PARSER_SUCCESS;

    if (!func)
        return PARSER_ERR_INVALID_PARAMETER;

    *func = NULL;

    switch (id) {
        /* parser version information */
        case PARSER_API_GET_VERSION_INFO:
            *func = MP3ParserVersionInfo;
            break;

        /* creation & deletion */
        case PARSER_API_CREATE_PARSER:
            *func = MP3CreateParser;
            break;

        case PARSER_API_CREATE_PARSER2:
            *func = MP3CreateParser2;
            break;

        case PARSER_API_DELETE_PARSER:
            *func = MP3DeleteParser;
            break;

        /* movie properties */
        case PARSER_API_IS_MOVIE_SEEKABLE:
            *func = MP3ParserIsSeekable;
            break;

        case PARSER_API_GET_MOVIE_DURATION:
            *func = MP3ParserGetMovieDuration;
            break;

        case PARSER_API_GET_META_DATA:
            *func = MP3ParserGetMetaData;
            break;

        case PARSER_API_GET_NUM_TRACKS:
            *func = MP3ParserGetNumTracks;
            break;

        /* generic track properties */
        case PARSER_API_GET_TRACK_TYPE:
            *func = MP3ParserGetTrackType;
            break;

        case PARSER_API_GET_DECODER_SPECIFIC_INFO:
            *func = MP3ParserGetCodecSpecificInfo;
            break;

        case PARSER_API_GET_TRACK_DURATION:
            *func = MP3ParserGetTrackDuration;
            break;

        case PARSER_API_GET_BITRATE:
            *func = MP3ParserGetBitRate;
            break;

        /* audio properties */
        case PARSER_API_GET_AUDIO_NUM_CHANNELS:
            *func = MP3ParserGetAudioNumChannels;
            break;

        case PARSER_API_GET_AUDIO_SAMPLE_RATE:
            *func = MP3ParserGetAudioSampleRate;
            break;
#if 0
        case PARSER_API_GET_AUDIO_BLOCK_ALIGN:
            *func = MP3ParserGetAudioBlockAlign;
            break;
#endif
        case PARSER_API_GET_AUDIO_BITS_PER_SAMPLE:
            *func = MP3ParserGetAudioBitsPerSample;
            break;

        /* sample reading, seek & trick mode */
        case PARSER_API_GET_READ_MODE:
            *func = MP3ParserGetReadMode;
            break;

        case PARSER_API_SET_READ_MODE:
            *func = MP3ParserSetReadMode;
            break;

        case PARSER_API_ENABLE_TRACK:
            *func = MP3ParserEnableTrack;
            break;

        case PARSER_API_GET_FILE_NEXT_SAMPLE:
            *func = MP3ParserGetFileNextSample;
            break;

        case PARSER_API_SEEK:
            *func = MP3ParserSeek;
            break;

        /* index table import/export */
        case PARSER_API_INITIALIZE_INDEX:
            *func = MP3ParserInitializeIndex;
            break;

        case PARSER_API_IMPORT_INDEX:
            *func = MP3ParserImportIndex;
            break;

        case PARSER_API_EXPORT_INDEX:
            *func = MP3ParserExportIndex;
            break;

        default:
            break; /* no support for other API */
    }

    return err;
}
