/************************************************************************
 *  Copyright (c) 2011-2013, Freescale Semiconductor, Inc.
 *  Copyright 2025-2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 ************************************************************************/

#include <string.h>
#include "ogg_parser_api.h"
#include "oggInternal.h"

extern const char* OggParserVersionInfo();

/*
Every core parser shall implement this function and tell a specific API function pointer.
If the queried API is not implemented, the parser shall set funtion pointer to NULL and return
PARSER_SUCCESS. */

int32 FslParserQueryInterface(uint32 id, void** func) {
    if (NULL == func)
        return PARSER_ERR_INVALID_PARAMETER;

    switch (id) {
        case PARSER_API_GET_VERSION_INFO:
            *func = (void*)OggParserVersionInfo;
            break;
        case PARSER_API_CREATE_PARSER:
            *func = (void*)OggCreateParser;
            break;
        case PARSER_API_DELETE_PARSER:
            *func = (void*)OggDeleteParser;
            break;

        /* movie properties */
        case PARSER_API_IS_MOVIE_SEEKABLE:
            *func = (void*)OggIsSeekable;
            break;
        case PARSER_API_GET_MOVIE_DURATION:
            *func = (void*)OggGetDuration;
            break;

        case PARSER_API_GET_META_DATA:
            *func = (void*)OggGetMetaData;
            break;

        case PARSER_API_GET_NUM_TRACKS:
            *func = (void*)OggGetNumTracks;
            break;

        /* generic track properties */
        case PARSER_API_GET_TRACK_TYPE:
            *func = (void*)OggGetTrackType;
            break;
        case PARSER_API_GET_TRACK_DURATION:
            *func = (void*)OggGetTrackDuration;
            break;
        case PARSER_API_GET_BITRATE:
            *func = (void*)OggGetBitRate;
            break;
        case PARSER_API_GET_DECODER_SPECIFIC_INFO:
            *func = (void*)OggParserGetDecSpecificInfo;
            break;

        /* video properties */
        case PARSER_API_GET_VIDEO_FRAME_WIDTH:
            *func = (void*)OggGetVideoFrameWidth;
            break;
        case PARSER_API_GET_VIDEO_FRAME_HEIGHT:
            *func = (void*)OggGetVideoFrameHeight;
            break;
        case PARSER_API_GET_VIDEO_FRAME_RATE:
            *func = (void*)OggGetVideoFrameRate;
            break;

        /* audio properties */
        case PARSER_API_GET_AUDIO_NUM_CHANNELS:
            *func = (void*)OggGetAudioNumChannels;
            break;
        case PARSER_API_GET_AUDIO_SAMPLE_RATE:
            *func = (void*)OggGetAudioSampleRate;
            break;

        /* sample reading, seek & trick mode */
        case PARSER_API_GET_READ_MODE:
            *func = (void*)OggGetReadMode;
            break;
        case PARSER_API_SET_READ_MODE:
            *func = (void*)OggSetReadMode;
            break;
        case PARSER_API_ENABLE_TRACK:
            *func = (void*)OggEnableTrack;
            break;
        case PARSER_API_GET_NEXT_SAMPLE:
            *func = NULL;
            break;
        case PARSER_API_GET_NEXT_SYNC_SAMPLE:
            *func = NULL;
            break;
        case PARSER_API_GET_FILE_NEXT_SAMPLE:
            *func = (void*)OggGetFileNextSample;
            break;
            // case PARSER_API_GET_DECODER_SPECIFIC_INFO:

        case PARSER_API_SEEK:
            *func = (void*)OggSeek;
            break;
        default:
            *func = NULL;
            break;
    }

    return PARSER_SUCCESS;
}

int32 OggCreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                      ParserOutputBufferOps* outputBufferOps, void* context,
                      FslParserHandle* parserHandle) {
    if ((NULL == streamOps) || (NULL == memOps) || (NULL == parserHandle) ||
        (NULL == outputBufferOps)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    return Ogg_CreateParserInternal(streamOps, memOps, outputBufferOps, context, isLive,
                                    parserHandle);
}

int32 OggSetReadMode(FslParserHandle parserHandle, uint32 readMode) {

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (PARSER_READ_MODE_TRACK_BASED == readMode) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    return PARSER_SUCCESS;
}

int32 OggGetReadMode(FslParserHandle parserHandle, uint32* readMode) {

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    *readMode = PARSER_READ_MODE_FILE_BASED;

    return PARSER_SUCCESS;
}

EXTERN int32 OggGetMetaData(FslParserHandle parserHandle, UserDataID userDataId,
                            UserDataFormat* userDataFormat, uint8** userData,
                            uint32* userDataLength) {
    int32 err = PARSER_SUCCESS;
    OggParserPtr pParser = (OggParserPtr)parserHandle;

    if ((parserHandle == NULL) || (userDataId >= USER_DATA_MAX) || (userDataFormat == NULL) ||
        (userData == NULL) || (userDataLength == NULL)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    switch (userDataId) {
        case USER_DATA_TITLE:
            *userDataFormat = pParser->m_tMetaDataTable.Title.eDataFormat;
            *userData = pParser->m_tMetaDataTable.Title.pData;
            *userDataLength = pParser->m_tMetaDataTable.Title.iDataLen;
            break;

        case USER_DATA_ALBUM:
            *userDataFormat = pParser->m_tMetaDataTable.Album.eDataFormat;
            *userData = pParser->m_tMetaDataTable.Album.pData;
            *userDataLength = pParser->m_tMetaDataTable.Album.iDataLen;
            break;

        case USER_DATA_TRACKNUMBER:
            *userDataFormat = pParser->m_tMetaDataTable.TrackNumber.eDataFormat;
            *userData = pParser->m_tMetaDataTable.TrackNumber.pData;
            *userDataLength = pParser->m_tMetaDataTable.TrackNumber.iDataLen;
            break;

        case USER_DATA_ARTIST:
            if (pParser->m_tMetaDataTable.Artist.iDataLen) {
                *userDataFormat = pParser->m_tMetaDataTable.Artist.eDataFormat;
                *userData = pParser->m_tMetaDataTable.Artist.pData;
                *userDataLength = pParser->m_tMetaDataTable.Artist.iDataLen;
            } else {
                *userDataFormat = pParser->m_tMetaDataTable.Performer.eDataFormat;
                *userData = pParser->m_tMetaDataTable.Performer.pData;
                *userDataLength = pParser->m_tMetaDataTable.Performer.iDataLen;
            }
            break;

        case USER_DATA_COPYRIGHT:
            *userDataFormat = pParser->m_tMetaDataTable.Copyright.eDataFormat;
            *userData = pParser->m_tMetaDataTable.Copyright.pData;
            *userDataLength = pParser->m_tMetaDataTable.Copyright.iDataLen;
            break;

        case USER_DATA_PRODUCER:
            *userDataFormat = pParser->m_tMetaDataTable.Organization.eDataFormat;
            *userData = pParser->m_tMetaDataTable.Organization.pData;
            *userDataLength = pParser->m_tMetaDataTable.Organization.iDataLen;
            break;

        case USER_DATA_DESCRIPTION:
            *userDataFormat = pParser->m_tMetaDataTable.Description.eDataFormat;
            *userData = pParser->m_tMetaDataTable.Description.pData;
            *userDataLength = pParser->m_tMetaDataTable.Description.iDataLen;
            break;

        case USER_DATA_GENRE:
            *userDataFormat = pParser->m_tMetaDataTable.Genre.eDataFormat;
            *userData = pParser->m_tMetaDataTable.Genre.pData;
            *userDataLength = pParser->m_tMetaDataTable.Genre.iDataLen;
            break;

        case USER_DATA_CREATION_DATE:
            *userDataFormat = pParser->m_tMetaDataTable.Date.eDataFormat;
            *userData = pParser->m_tMetaDataTable.Date.pData;
            *userDataLength = pParser->m_tMetaDataTable.Date.iDataLen;
            break;

        default:
            *userDataFormat = 0;
            *userData = NULL;
            *userDataLength = 0;
            break;
    }

    return err;
}

/**
 * function to tell how many streams in the stream in the program with ID progId
 * If it's PS or MPEG1SS just set progId=0
 * @param parserHandle [in] Handle of the Ogg core parser.
 * @param progId [in]  Prog Id of program.
 * @param numstreams[out] Number of Stream
 * @return
 */

int32 OggGetNumTracks(FslParserHandle parserHandle, uint32* numStreams) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    *numStreams = pParser->numofStreams;

    return PARSER_SUCCESS;
}

int32 OggEnableTrack(FslParserHandle parserHandle, uint32 numStream, bool enable) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    if (numStream > pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (enable)
        pParser->Streams[numStream].isEnabled = TRUE;
    else
        pParser->Streams[numStream].isEnabled = FALSE;

    return PARSER_SUCCESS;
}

int32 OggGetDuration(FslParserHandle parserHandle, uint64* usDuration) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    uint64 usLongestStreamDuration = 0;
    uint32 i;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    for (i = 0; i < pParser->numofStreams; i++) {
        if (pParser->Streams[i].usDuration > usLongestStreamDuration)
            usLongestStreamDuration = pParser->Streams[i].usDuration;
    }

    *usDuration = usLongestStreamDuration;
    return PARSER_SUCCESS;
}

/**
 * function to tell the movie duration.
 *
 * @param parserHandle [in] Handle of the Ogg core parser.
 * @param streamNum[in]        the streamNum of the stream
 * @param usDuration [out]  Duration in us.
 * @return
 */
int32 OggGetTrackDuration(FslParserHandle parserHandle, uint32 streamNum, uint64* usDuration) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (streamNum > pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    *usDuration = pParser->Streams[streamNum].usDuration;

    return PARSER_SUCCESS;
}

/* stream-level info */
/**
 * function to tell the media type of a stream (video, audio, subtitle...)
 *
 * @param parserHandle [in] Handle of the Ogg core parser.
 * @param streamNum [in] Number of the stream, 0-based.
 * @param type [out] Media type of the stream.
 * @return
 */
int32 OggGetTrackType(FslParserHandle parserHandle, uint32 streamNum, uint32* mediaType,
                      uint32* decoderType, uint32* decoderSubtype) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (streamNum >= pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    switch (pParser->Streams[streamNum].type) {
        case OGG_VORBIS:
            *mediaType = MEDIA_AUDIO;
            *decoderType = AUDIO_VORBIS;
            break;
        case OGG_SPEEX:
            *mediaType = MEDIA_AUDIO;
            *decoderType = AUIDO_SPEEX;
            break;
        case OGG_FLAC:
            *mediaType = MEDIA_AUDIO;
            *decoderType = AUDIO_FLAC;
            break;
        case OGG_FLAC_NEW:
            *mediaType = MEDIA_AUDIO;
            *decoderType = AUDIO_FLAC;
            break;
        case OGG_THEORA:
            *mediaType = MEDIA_VIDEO;
            *decoderType = VIDEO_TYPE_UNKNOWN;
            break;
        case OGG_VIDEO:
            *mediaType = MEDIA_VIDEO;
            *decoderType = VIDEO_TYPE_UNKNOWN;
            break;
        default:
            *mediaType = MEDIA_TYPE_UNKNOWN;
            break;
    }
    (void)decoderSubtype;

    return PARSER_SUCCESS;
}

/**
 * function to tell the bitrate of a stream.
 * For CBR stream, the real bitrate is given.
 * For VBR stream, 0 is given since the bitrate varies during the playback and Ogg parser does not
 * calculate the peak or average bit rate.
 *
 * @param parserHandle [in] Handle of the Ogg core parser.
 * @param streamNum [in] ID of the stream. [in] Number of the stream, 0-based.
 * @param bitrate [out] Bitrate. For CBR stream, this is the real bitrate.
 *                                            For VBR stream, the bitrate is 0 since max bitrate is
 * usually not available.
 * @return
 */
int32 OggGetBitRate(FslParserHandle parserHandle, uint32 streamNum, uint32* bitrate) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    if (streamNum >= pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (pParser->Streams[streamNum].type == OGG_VORBIS ||
        pParser->Streams[streamNum].type == OGG_SPEEX ||
        pParser->Streams[streamNum].type == OGG_FLAC ||
        pParser->Streams[streamNum].type == OGG_FLAC_NEW)
        *bitrate = pParser->Streams[streamNum].MediaProperty.AudioProperty.averBitrate;
    else if (pParser->Streams[streamNum].type == OGG_THEORA ||
             pParser->Streams[streamNum].type == OGG_VIDEO)
        *bitrate = pParser->Streams[streamNum].MediaProperty.VideoProperty.averBitRate;

    return PARSER_SUCCESS;
}

/**
 * function to tell the sample duration in us of a stream.
 * If the sample duration is not a constant (eg. some audio, subtilte), 0 is given.
 *
 * @param parserHandle [in] Handle of the Ogg core parser.
 * @param streamNum[in] ID of the stream.
 * @param usDuration [out] Sample duration in us. If sample duration is not a constant, this value
 * is 0.
 * @return
 */

int32 OggGetVideoFrameRate(FslParserHandle parserHandle, uint32 streamNum, uint32* rate,
                           uint32* scale) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    if (streamNum >= pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (pParser->Streams[streamNum].type == OGG_THEORA ||
        pParser->Streams[streamNum].type == OGG_VIDEO) {
        uint32 numerator, denominator;
        numerator = pParser->Streams[streamNum].MediaProperty.VideoProperty.fRNumerator;
        denominator = pParser->Streams[streamNum].MediaProperty.VideoProperty.fRDenominator;
        *rate = numerator;
        *scale = denominator;
        return PARSER_SUCCESS;
    }

    return PARSER_INVALID_TRACK_NUMBER;
}

/**
 * function to tell the width in pixels of a video stream.
 * @param parserHandle [in] Handle of the Ogg core parser.
 * @param streamNum [in] ID of the stream. It must point to a video stream.
 * @param width [out] Width in pixels.
 * @return
 */
int32 OggGetVideoFrameWidth(FslParserHandle parserHandle, uint32 streamNum, uint32* width) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (streamNum >= pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (pParser->Streams[streamNum].type == OGG_THEORA ||
        pParser->Streams[streamNum].type == OGG_VIDEO) {
        *width = pParser->Streams[streamNum].MediaProperty.VideoProperty.width;
        return PARSER_SUCCESS;
    } else
        return PARSER_ERR_INVALID_PARAMETER;
}

/**
 * function to tell the height in pixels of a video stream.
 * @param parserHandle [in] Handle of the Ogg core parser.
 * @param streamNum [in] ID of the stream. It must point to a video stream.
 * @param height [out] Height in pixels.
 * @return
 */
int32 OggGetVideoFrameHeight(FslParserHandle parserHandle, uint32 streamNum, uint32* height) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (streamNum >= pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (pParser->Streams[streamNum].type == OGG_THEORA ||
        pParser->Streams[streamNum].type == OGG_VIDEO) {
        *height = pParser->Streams[streamNum].MediaProperty.VideoProperty.height;
        return PARSER_SUCCESS;
    } else
        return PARSER_ERR_INVALID_PARAMETER;
}

/**
 * function to tell how many channels in an audio stream.
 *
 * @param parserHandle [in] Handle of the Ogg core parser.
 * @param streamNum [in] ID of the stream. It must point to an audio steam.
 * @param numchannels [out] Number of the channels. 1 mono, 2 stereo, or more for multiple channels.
 * @return
 */
int32 OggGetAudioNumChannels(FslParserHandle parserHandle, uint32 streamNum, uint32* numchannels) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    if (streamNum >= pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (pParser->Streams[streamNum].type == OGG_VORBIS ||
        pParser->Streams[streamNum].type == OGG_SPEEX ||
        pParser->Streams[streamNum].type == OGG_FLAC ||
        pParser->Streams[streamNum].type == OGG_FLAC_NEW) {
        *numchannels = pParser->Streams[streamNum].MediaProperty.AudioProperty.channels;
        return PARSER_SUCCESS;
    } else
        return PARSER_ERR_INVALID_PARAMETER;
}

/**
 * function to tell the audio sample rate (sampling frequency) of an audio stream.
 *
 * @param parserHandle [in] Handle of the Ogg core parser.
 * @param streamNum [in] ID of the stream. It must point to an audio stream.
 * @param sampleRate [out] Audio integer sample rate (sampling frequency).
 * @return
 */
int32 OggGetAudioSampleRate(FslParserHandle parserHandle, uint32 streamNum, uint32* sampleRate) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    if (streamNum >= pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (pParser->Streams[streamNum].type == OGG_VORBIS ||
        pParser->Streams[streamNum].type == OGG_SPEEX ||
        pParser->Streams[streamNum].type == OGG_FLAC ||
        pParser->Streams[streamNum].type == OGG_FLAC_NEW) {
        *sampleRate = pParser->Streams[streamNum].MediaProperty.AudioProperty.sampleRate;
        return PARSER_SUCCESS;
    } else
        return PARSER_ERR_INVALID_PARAMETER;
}

int32 OggGetFileNextSample(FslParserHandle parserHandle, uint32* streamNum, uint8** pSampleData,
                           void** pBufContext, uint32* dataSize, uint64* usPresTime,
                           uint64* usDuration, uint32* flag) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    ParserOutputBufferOps* pRequestBufferOps;
    uint32 i;
    OggStream* pStream;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if ((NULL == streamNum) || (NULL == pSampleData) || (NULL == pBufContext) ||
        (NULL == dataSize) || (NULL == usPresTime) || (NULL == usDuration) || (NULL == flag))
        return PARSER_ERR_INVALID_PARAMETER;

    *usDuration = PARSER_UNKNOWN_DURATION;
    pRequestBufferOps = pParser->pRequestBufferOps;

    for (i = 0; i < pParser->numofStreams; i++) {
        pStream = &(pParser->Streams[i]);
        if (pStream->cachedNum > pStream->cPacketIndex) {
            uint32 offset = pStream->pCachedPackets[pStream->cPacketIndex].offset;
            uint32 bufSize = pStream->pCachedPackets[pStream->cPacketIndex].size - offset;
            uint32 aSize = bufSize;

            if (NULL == (*pSampleData = pRequestBufferOps->RequestBuffer(i, &aSize, pBufContext,
                                                                         pParser->appContext)))
                return PARSER_ERR_NO_OUTPUT_BUFFER;
            if (aSize > bufSize)
                aSize = bufSize;

            *streamNum = i;

            memcpy(*pSampleData,
                   pStream->pCachedPackets[pStream->cPacketIndex].pBufferData + offset, aSize);

            *dataSize = aSize;
            *usPresTime = pStream->pCachedPackets[pStream->cPacketIndex].PTS;
            if (aSize + offset < pStream->pCachedPackets[pStream->cPacketIndex].size) {
                *flag = FLAG_SAMPLE_NOT_FINISHED;
                pStream->pCachedPackets[pStream->cPacketIndex].offset += aSize;
            } else {
                *flag = pStream->pCachedPackets[pStream->cPacketIndex].flag;
                if (pStream->pCachedPackets[pStream->cPacketIndex].pBufferData)
                    LocalFree(pStream->pCachedPackets[pStream->cPacketIndex].pBufferData);
                pStream->pCachedPackets[pStream->cPacketIndex].pBufferData = NULL;
                pStream->cPacketIndex++;
            }

            return PARSER_SUCCESS;
        }
    }

    return Ogg_ParseGetNextPacket(pParser, streamNum, pSampleData, pBufContext, dataSize,
                                  usPresTime, flag);
    //
}

int32 OggIsSeekable(FslParserHandle parserHandle, bool* seekable) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    uint64 usDuration = 0;
    uint32 str = 0;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    for (str = 0; str < pParser->numofStreams; str++)
        if (usDuration < pParser->Streams[str].usDuration)
            usDuration = pParser->Streams[str].usDuration;

    if (usDuration == 0)
        *seekable = 0;
    else
        *seekable = 1;

    return PARSER_SUCCESS;
}

// these code still need some change
int32 OggSeek(FslParserHandle parserHandle, uint32 streamNum, uint64* usTime, uint32 flag)

{
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    if (streamNum >= pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    return Ogg_SeekStream(pParser, streamNum, usTime, flag);
}

int32 OggParserGetDecSpecificInfo(FslParserHandle parserHandle, uint32 streamNum, uint8** data,
                                  uint32* size) {
    OggParserPtr pParser = (OggParserPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    if (streamNum >= pParser->numofStreams)
        return PARSER_ERR_INVALID_PARAMETER;
    if (pParser->Streams[streamNum].type == OGG_VORBIS) {
        *data = pParser->Streams[streamNum].pCodecInfo;
        *size = pParser->Streams[streamNum].codecInfoLen;
    } else {
        *data = NULL;
        *size = 0;
    }
    return PARSER_SUCCESS;
}

#ifdef CHECK_CRC
#ifdef TIME_PROFILE
extern uint64 tCRCtime;
#endif
#endif

static void MetaDataFree(OggParserPtr pParser) {
    MetaDataTable* pMetaDataTable = NULL;

    if (NULL == pParser)
        return;

    pMetaDataTable = &pParser->m_tMetaDataTable;

    SAFE_FREE(pMetaDataTable->Album.pData);
    SAFE_FREE(pMetaDataTable->Artist.pData);
    SAFE_FREE(pMetaDataTable->Copyright.pData);
    SAFE_FREE(pMetaDataTable->Date.pData);
    SAFE_FREE(pMetaDataTable->Description.pData);
    SAFE_FREE(pMetaDataTable->Genre.pData);
    SAFE_FREE(pMetaDataTable->Organization.pData);
    SAFE_FREE(pMetaDataTable->Performer.pData);
    SAFE_FREE(pMetaDataTable->Title.pData);
    SAFE_FREE(pMetaDataTable->TrackNumber.pData);

    return;
}

int32 OggDeleteParser(FslParserHandle parserHandle) {

    OggParserPtr pParser = (OggParserPtr)parserHandle;
    uint32 j, i = 0;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if ((pParser->inputStream->Close) && (NULL != pParser->fileHandle)) {
        pParser->inputStream->Close(pParser->fileHandle, pParser->appContext);
        pParser->fileHandle = NULL;
    }

    for (i = 0; i < pParser->numofStreams; i++) {
        if (NULL != pParser->Streams[i].prevPartialPacket.pBufferData) {
            LocalFree(pParser->Streams[i].prevPartialPacket.pBufferData);
            pParser->Streams[i].prevPartialPacket.pBufferData = NULL;
        }
        if (NULL != pParser->Streams[i].pCachedPackets) {
            for (j = 0; j < pParser->Streams[i].tPacketNum; j++) {
                if (NULL != pParser->Streams[i].pCachedPackets[j].pBufferData)
                    LocalFree(pParser->Streams[i].pCachedPackets[j].pBufferData);

                pParser->Streams[i].pCachedPackets[j].pBufferData = NULL;
            }

            LocalFree(pParser->Streams[i].pCachedPackets);
        }

        if (NULL != pParser->Streams[i].pCodecInfo) {
            LocalFree(pParser->Streams[i].pCodecInfo);
            pParser->Streams[i].pCodecInfo = NULL;
        }
    }

    MetaDataFree(pParser);

    if (NULL != pParser)
        LocalFree(pParser);

#ifdef CHECK_CRC
#ifdef TIME_PROFILE
    printf("CRC takes time:%llu\n", tCRCtime);
#endif
#endif

    return PARSER_SUCCESS;
}
