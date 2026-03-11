/*
***********************************************************************
* Copyright (c) 2011-2016, Freescale Semiconductor, Inc.
*
* Copyright 2017-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
#include "mpeg2_parser_api.h"
#include "h264parser.h"
#include "latmParser.h"
#include "mpeg2_parser_internal.h"
#include "parse_ts.h"

// #define MPG2_PARSER_DBG
#ifdef MPG2_PARSER_DBG
#define MPG2_PARSER_LOG printf
#define MPG2_PARSER_ERR printf
#else
#define MPG2_PARSER_LOG(...)
#define MPG2_PARSER_ERR(...)
#endif

// if seek and fastward/backward operaion exist simultaneously, we should ignore index table to
// avoid the big jump: ENGR00260375
//  #define ALLOW_SEEK_AND_SPEED

extern const char* Mpeg2ParserVersionInfo();
/*
Every core parser shall implement this function and tell a specific API function pointer.
If the queried API is not implemented, the parser shall set funtion pointer to NULL and return
PARSER_SUCCESS. */

static void FreePMTInfoList(MPEG2ObjectPtr pDemuxer, PMTInfoList* pPMTInfoList);

int32 FslParserQueryInterface(uint32 id, void** func) {
    if (NULL == func)
        return PARSER_ERR_INVALID_PARAMETER;

    switch (id) {
        case PARSER_API_GET_VERSION_INFO:
            *func = (void*)Mpeg2ParserVersionInfo;
            break;
        case PARSER_API_CREATE_PARSER:
            *func = (void*)Mpeg2CreateParser;
            break;
        case PARSER_API_CREATE_PARSER2:
            *func = (void*)Mpeg2CreateParser2;
            break;
        case PARSER_API_DELETE_PARSER:
            *func = (void*)Mpeg2DeleteParser;
            break;

            /* movie properties */
        case PARSER_API_IS_MOVIE_SEEKABLE:
            *func = (void*)Mpeg2IsSeekable;
            break;
        case PARSER_API_GET_MOVIE_DURATION:
            *func = (void*)Mpeg2GetMovieDuration;
            break;

        case PARSER_API_GET_NUM_PROGRAMS:
            *func = (void*)Mpeg2GetNumPrograms;
            break;

        case PARSER_API_GET_PROGRAM_TRACKS:
            *func = (void*)Mpeg2GetProgramTracks;
            break;

        case PARSER_API_GET_NUM_TRACKS:
            *func = (void*)Mpeg2GetNumTracks;
            break;

        case PARSER_API_GET_META_DATA:
            *func = (void*)Mpeg2GetMetaData;
            break;

        case PARSER_API_GET_PCR:
            *func = (void*)Mpeg2GetPCR;
            break;

            /* generic track properties */
        case PARSER_API_GET_TRACK_TYPE:
            *func = (void*)Mpeg2GetTrackType;
            break;
        case PARSER_API_GET_TRACK_DURATION:
            *func = (void*)Mpeg2GetTrackDuration;
            break;
        case PARSER_API_GET_LANGUAGE:
            *func = (void*)Mpeg2GetLanguage;
            break;
        case PARSER_API_GET_BITRATE:
            *func = (void*)Mpeg2GetBitRate;
            break;
        case PARSER_API_GET_DECODER_SPECIFIC_INFO:
            *func = (void*)Mpeg2GetDecoderSpecificInfo;
            break;

            /* video properties */
        case PARSER_API_GET_VIDEO_FRAME_WIDTH:
            *func = (void*)Mpeg2GetVideoFrameWidth;
            break;
        case PARSER_API_GET_VIDEO_FRAME_HEIGHT:
            *func = (void*)Mpeg2GetVideoFrameHeight;
            break;
        case PARSER_API_GET_VIDEO_FRAME_RATE:
            *func = (void*)Mpeg2GetVideoFrameRate;
            break;
        case PARSER_API_GET_VIDEO_SCAN_TYPE:
            *func = (void*)Mpeg2GetVideoScanType;
            break;

            /* audio properties */
        case PARSER_API_GET_AUDIO_NUM_CHANNELS:
            *func = (void*)Mpeg2GetAudioNumChannels;
            break;
        case PARSER_API_GET_AUDIO_SAMPLE_RATE:
            *func = (void*)Mpeg2GetAudioSampleRate;
            break;
        case PARSER_API_GET_AUDIO_BITS_PER_SAMPLE:
            *func = (void*)Mpeg2GetAudioBitsPerSample;
            break;
        case PARSER_API_GET_AUDIO_PRESENTATION_NUM:
            *func = (void*)Mpeg2GetAudioPresentationNum;
            break;
        case PARSER_API_GET_AUDIO_PRESENTATION_INFO:
            *func = (void*)Mpeg2GetAudioPresentationInfo;
            break;

            /* sample reading, seek & trick mode */
        case PARSER_API_GET_READ_MODE:
            *func = (void*)Mpeg2GetReadMode;
            break;
        case PARSER_API_SET_READ_MODE:
            *func = (void*)Mpeg2SetReadMode;
            break;
        case PARSER_API_ENABLE_TRACK:
            *func = (void*)Mpeg2EnableTrack;
            break;
        case PARSER_API_GET_NEXT_SAMPLE:
            *func = (void*)Mpeg2GetNextSample;
            break;
        case PARSER_API_GET_NEXT_SYNC_SAMPLE:
            *func = (void*)Mpeg2GetNextSyncSample;
            break;
        case PARSER_API_GET_FILE_NEXT_SAMPLE:
            *func = (void*)Mpeg2GetFileNextSample;
            break;
        case PARSER_API_GET_FILE_NEXT_SYNC_SAMPLE:
            *func = (void*)Mpeg2GetFileNextSyncSample;
            break;
        case PARSER_API_SEEK:
            *func = (void*)Mpeg2Seek;
            break;
        case PARSER_API_INITIALIZE_INDEX:
            *func = (void*)Mpeg2ParserInitializeIndex;
            break;
        case PARSER_API_IMPORT_INDEX:
            *func = (void*)Mpeg2ParserImportIndex;
            break;
        case PARSER_API_EXPORT_INDEX:
            *func = (void*)Mpeg2ParserExportIndex;
            break;
        case PARSER_API_FLUSH_TRACK:
            *func = (void*)Mpeg2FlushTrack;
            break;
        default:
            *func = NULL;
            break;
    }

    return PARSER_SUCCESS;
}

int32 Mpeg2CreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                        ParserOutputBufferOps* outputBufferOps, void* context,
                        FslParserHandle* parserHandle) {
    uint32 flags = 0;

    if ((NULL == streamOps) || (NULL == memOps) || (NULL == parserHandle) ||
        (NULL == outputBufferOps)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if (isLive)
        flags |= (FILE_FLAG_NON_SEEKABLE | FILE_FLAG_READ_IN_SEQUENCE);

    return Mpeg2CreateParserInternal(streamOps, memOps, outputBufferOps, context, flags,
                                     parserHandle);
}

int32 Mpeg2CreateParser2(uint32 flags, FslFileStream* streamOps, ParserMemoryOps* memOps,
                         ParserOutputBufferOps* outputBufferOps, void* context,
                         FslParserHandle* parserHandle) {
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;

    if ((NULL == streamOps) || (NULL == memOps) || (NULL == parserHandle) ||
        (NULL == outputBufferOps)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    err = Mpeg2CreateParserInternal(streamOps, memOps, outputBufferOps, context, flags,
                                    parserHandle);

    return err;
}

int32 Mpeg2GetNumPrograms(FslParserHandle parserHandle, uint32* numPrograms) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;

    if ((parserHandle == NULL) || (numPrograms == NULL))
        return PARSER_ERR_INVALID_PARAMETER;

    if ((pDemuxer->TS_PSI.IsTS == 0) ||
        (pDemuxer->pDemuxContext->TSCnxt.PAT.NonProgramSelected == 1)) {
        *numPrograms = 1;  // or return PARSER_ILLEAGAL_OPERATION ?
    } else {
        *numPrograms = pDemuxer->pDemuxContext->TSCnxt.nParsedPMTs;
    }

    return PARSER_SUCCESS;
}

int32 Mpeg2GetProgramTracks(FslParserHandle parserHandle, uint32 programNum, uint32* numTracks,
                            uint32** ppTrackNumList) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    static uint32 adwTrackIdx[MAX_MPEG2_STREAMS] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
                                                    12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23};

    if ((parserHandle == NULL) || (numTracks == NULL) || (ppTrackNumList == NULL))
        return PARSER_ERR_INVALID_PARAMETER;

    if ((pDemuxer->TS_PSI.IsTS == 0) ||
        (pDemuxer->pDemuxContext->TSCnxt.PAT.NonProgramSelected == 1)) {
        *numTracks = pDemuxer->SystemInfo.uliNoStreams;
        *ppTrackNumList = adwTrackIdx;
        return PARSER_SUCCESS;
    }

    if (programNum >= pDemuxer->pDemuxContext->TSCnxt.nParsedPMTs) {
        return PARSER_ILLEAGAL_OPERATION;
    }

    *numTracks = pDemuxer->pDemuxContext->TSCnxt.PMT[programNum].ValidTrackNum;
    *ppTrackNumList = pDemuxer->pDemuxContext->TSCnxt.PMT[programNum].adwValidTrackIdx;

    return PARSER_SUCCESS;
}

int32 Mpeg2ParserInitializeIndex(
        FslParserHandle parserHandle) /*Loading index from the movie file */
{
    // if we need this API to initialize index?
    // index is initialized when video track is enabled, so just returen here
    // this function is not actually used, please see Mpeg2ParserInitalIndex in internal.c

    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;
    uint32 numStream;
    FSL_MPEGSTREAM_T* pStream;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (STREAMING_MODE == pDemuxer->playMode)
        return PARSER_ERR_INVALID_PARAMETER;

    for (numStream = 0; numStream < pDemuxer->SystemInfo.uliNoStreams; numStream++) {
        pStream = &(pDemuxer->SystemInfo.Stream[numStream]);
        if (pStream->enuStreamType != FSL_MPG_DEMUX_VIDEO_STREAM)
            continue;

        err = Mpeg2ParserInitialIndex(pDemuxer, numStream);
        if (err)
            return err;
    }

    return PARSER_SUCCESS;
}

int32 Mpeg2ParserImportIndex(FslParserHandle parserHandle, /* Import index from outside */
                             uint8* buffer, uint32 size) {
    uint32 dwTrackIdx = 0;
    uint32 dwCycle = 0;
    MPEG2_Index_Head* ptIndexHead = NULL;
    uint8* endbuffer = buffer + size;
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    MPEG2_Index* pIndex;
    uint32 dwItemSize, dwPtsSize;

    if (NULL == parserHandle || NULL == buffer || size < MPEG2_INDEXHEAD_SIZE) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    do {
        ptIndexHead = (MPEG2_Index_Head*)buffer;
        dwTrackIdx = ptIndexHead->dwTrackIdx;
        if (dwTrackIdx >= MAX_MPEG2_STREAMS) {
            return PARSER_ERR_INVALID_PARAMETER;
        }

        pIndex = &(pDemuxer->index[dwTrackIdx]);

        if (pIndex->pItem != NULL) {
            LOCALFree(pIndex->pItem);
            pIndex->pItem = NULL;
        }
        if (pIndex->pts != NULL) {
            LOCALFree(pIndex->pts);
            pIndex->pts = NULL;
        }

        memcpy(pIndex, buffer, MPEG2_INDEXHEAD_SIZE);
        buffer += MPEG2_INDEXHEAD_SIZE;

        // Check index valid code added here
        if (pIndex->version != 2) {
            return PARSER_ERR_INVALID_PARAMETER;
        }

        dwItemSize = pIndex->itemcount * pIndex->offsetbytes;
        dwPtsSize = pIndex->itemcount * sizeof(uint64);

        if (pIndex->itemcount > 0) {
            pIndex->pts = LOCALMalloc(dwPtsSize);
            pIndex->pItem = LOCALMalloc(dwItemSize);

            if (pIndex->pItem == NULL || pIndex->pts == NULL) {
                return PARSER_INSUFFICIENT_MEMORY;
            }

            memcpy(pIndex->pItem, buffer, dwItemSize);
            buffer += dwItemSize;
            memcpy(pIndex->pts, buffer, dwPtsSize);
            buffer += dwPtsSize;
        }

        dwCycle++;
        if (dwCycle > pDemuxer->SystemInfo.uliNoStreams) {
            return PARSER_ERR_UNKNOWN;
        }
    } while (buffer < endbuffer);

    return PARSER_SUCCESS;
}

int32 Mpeg2ParserExportIndex(FslParserHandle parserHandle, uint8* buffer, uint32* size) {
    uint32 dwIndexSize = 0;
    uint32 dwTrackIdx;
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    MPEG2_Index* pIndex;
    uint32 dwItemSize, dwPtsSize;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    for (dwTrackIdx = 0; dwTrackIdx < pDemuxer->SystemInfo.uliNoStreams; dwTrackIdx++) {
        pIndex = &(pDemuxer->index[dwTrackIdx]);
        if (pIndex->itemcount == 0 || pIndex->offsetbytes == 0) {
            continue;
        }

        dwItemSize = pIndex->itemcount * pIndex->offsetbytes;
        dwPtsSize = pIndex->itemcount * sizeof(uint64);

        if (buffer != NULL) {
            memcpy(buffer, pIndex, MPEG2_INDEXHEAD_SIZE);
            buffer += MPEG2_INDEXHEAD_SIZE;
            memcpy(buffer, pIndex->pItem, dwItemSize);
            buffer += dwItemSize;
            memcpy(buffer, pIndex->pts, dwPtsSize);
            buffer += dwPtsSize;
        }

        dwIndexSize += MPEG2_INDEXHEAD_SIZE + dwItemSize + dwPtsSize;
    }

    *size = dwIndexSize;

    return PARSER_SUCCESS;
}

int32 Mpeg2SetReadMode(FslParserHandle parserHandle, uint32 readMode) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (PARSER_READ_MODE_FILE_BASED == readMode) {
        pDemuxer->outputMode = OUTPUT_BYFILE;
    } else if (PARSER_READ_MODE_TRACK_BASED == readMode) {
        pDemuxer->outputMode = OUTPUT_BYTRACK;
    }

    return PARSER_SUCCESS;
}

int32 Mpeg2GetReadMode(FslParserHandle parserHandle, uint32* readMode) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (OUTPUT_BYFILE == pDemuxer->outputMode) {
        *readMode = PARSER_READ_MODE_FILE_BASED;
    } else {
        *readMode = PARSER_READ_MODE_TRACK_BASED;
    }

    return PARSER_SUCCESS;
}

/**
 * function to tell how many streams in the stream in the program with ID progId
 * If it's PS or MPEG1SS just set progId=0
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param progId [in]  Prog Id of program.
 * @param numstreams[out] Number of Stream
 * @return
 */

int32 Mpeg2GetNumTracks(FslParserHandle parserHandle, uint32* numStreams) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    *numStreams = pDemuxer->SystemInfo.uliNoStreams;

    return PARSER_SUCCESS;
}

int32 Mpeg2EnableTrack(FslParserHandle parserHandle, uint32 numStream, bool enable) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;
    FSL_MPG_DEMUX_CNXT_T* pCnxt;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    if (numStream >= pDemuxer->SystemInfo.uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    pCnxt = pDemuxer->pDemuxContext;
    if (enable) {
        pDemuxer->SystemInfo.Stream[numStream].isEnabled = TRUE;

        if (pCnxt->TSCnxt.bOutputPCR) {
            U32 programNum, streamPID, PCR_PID;

            streamPID = pDemuxer->SystemInfo.Stream[numStream].streamPID;
            programNum = programNumFromPID(pCnxt, streamPID);
            if (programNum < pCnxt->TSCnxt.nParsedPMTs) {
                PCR_PID = pCnxt->TSCnxt.PCRInfo[programNum].PID;
                EnablePID(pCnxt, PCR_PID);
            }
        }

        if (pDemuxer->SystemInfo.Stream[numStream].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM &&
            (pDemuxer->SystemInfo.Stream[numStream].MediaProperty.VideoProperty.enuVideoType ==
                     FSL_MPG_DEMUX_MPEG2_VIDEO ||
             pDemuxer->SystemInfo.Stream[numStream].MediaProperty.VideoProperty.enuVideoType ==
                     FSL_MPG_DEMUX_H264_VIDEO)) {
            if (pDemuxer->index[numStream].pItem == NULL)  // indextable is not initialized
                if ((err = Mpeg2ParserInitialIndex(pDemuxer, numStream)))
                    return err;
        }
    } else {
        pDemuxer->SystemInfo.Stream[numStream].isEnabled = FALSE;
        Mpeg2ResetOuputBuffer(pDemuxer, numStream);
    }

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE Mpeg2GetDecoderSpecificInfo(FslParserHandle parserHandle, uint32 streamNum,
                                                    uint8** data, uint32* size) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (streamNum > pDemuxer->SystemInfo.uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (pDemuxer->SystemInfo.Stream[streamNum].codecSpecInformation == NULL ||
        (pDemuxer->bNeedH264Convert == FALSE &&
         pDemuxer->SystemInfo.Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM &&
         pDemuxer->SystemInfo.Stream[streamNum].MediaProperty.VideoProperty.enuVideoType ==
                 FSL_MPG_DEMUX_H264_VIDEO)) {
        *data = NULL;
        *size = 0;
    } else {
        *data = pDemuxer->SystemInfo.Stream[streamNum].codecSpecInformation;
        *size = pDemuxer->SystemInfo.Stream[streamNum].codecSpecInfoSize;
    }

    return PARSER_SUCCESS;
}

int32 Mpeg2GetMovieDuration(FslParserHandle parserHandle, uint64* usDuration) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    *usDuration = pDemuxer->usLongestStreamDuration;

    return PARSER_SUCCESS;
}

/**
 * function to tell the movie duration.
 *
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum[in]        the streamNum of the stream
 * @param usDuration [out]  Duration in us.
 * @return
 */
int32 Mpeg2GetTrackDuration(FslParserHandle parserHandle, uint32 streamNum, uint64* usDuration) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (streamNum > pDemuxer->SystemInfo.uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    *usDuration = pDemuxer->SystemInfo.Stream[streamNum].usDuration;

    return PARSER_SUCCESS;
}

/* stream-level info */
/**
 * function to tell the media type of a stream (video, audio, subtitle...)
 *
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum [in] Number of the stream, 0-based.
 * @param type [out] Media type of the stream.
 * @return
 */
int32 Mpeg2GetTrackType(FslParserHandle parserHandle, uint32 streamNum, uint32* mediaType,
                        uint32* decoderType, uint32* decoderSubtype) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    *decoderSubtype = 0;
    switch (sysInfo->Stream[streamNum].enuStreamType) {
        case FSL_MPG_DEMUX_NOMEDIA:
            *mediaType = MEDIA_TYPE_UNKNOWN;
            break;
        case FSL_MPG_DEMUX_AUDIO_STREAM:
            *mediaType = MEDIA_AUDIO;

            switch (sysInfo->Stream[streamNum].MediaProperty.AudioProperty.enuAudioType) {
                case FSL_MPG_DEMUX_NOAUDIO:
                    *decoderType = AUDIO_TYPE_UNKNOWN;
                    break;
                case FSL_MPG_DEMUX_MP1_AUDIO:
                    *decoderType = AUDIO_MP3;
                    break;
                case FSL_MPG_DEMUX_MP2_AUDIO:
                    *decoderType = AUDIO_MP3;
                    break;
                case FSL_MPG_DEMUX_MP3_AUDIO:
                    *decoderType = AUDIO_MP3;
                    break;
                case FSL_MPG_DEMUX_AAC_AUDIO:
                    *decoderType = AUDIO_AAC;
                    *decoderSubtype = AUDIO_AAC_ADTS;

                    if (FSL_MPG_DEMUX_AAC_RAW ==
                        sysInfo->Stream[streamNum].MediaProperty.AudioProperty.enuAudioSubType) {
                        *decoderSubtype = AUDIO_AAC_RAW;
                    }
                    break;
                case FSL_MPG_DEMUX_AC3_AUDIO:
                    *decoderType = AUDIO_AC3;
                    break;
                case FSL_MPG_DEMUX_AC4_AUDIO:
                    *decoderType = AUDIO_AC4;
                    break;
                case FSL_MPG_DEMUX_EAC3_AUDIO:
                    *decoderType = AUDIO_EC3;
                    break;
                case FSL_MPG_DEMUX_DTS_AUDIO:
                    *decoderType = AUDIO_DTS;
                    break;
                case FSL_MPG_DEMUX_DTS_HD_AUDIO:
                    *decoderType = AUDIO_DTS_HD;
                    break;
                case FSL_MPG_DEMUX_DTS_UHD_AUDIO:
                    *decoderType = AUDIO_DTS_UHD;
                    break;
                case FSL_MPG_DEMUX_PCM_AUDIO:
                    *decoderType = AUDIO_PCM;

                    switch (sysInfo->Stream[streamNum]
                                    .MediaProperty.AudioProperty.usiAudioBitsPerSample) {
                        case 8:
                            *decoderSubtype = AUDIO_PCM_U8;
                            break;
                        case 16:
                            *decoderSubtype = AUDIO_PCM_S16BE;
                            break;
                        case 24:  // pcm-dvd, fix ENGR00223628
                            *decoderSubtype = AUDIO_PCM_DVD;
                            break;
                        case 32:
                            *decoderSubtype = AUDIO_PCM_S32BE;
                            break;
                        default:
                            *decoderSubtype = 0;
                            break;
                    }
                    break;
                case FSL_MPG_DEMUX_UNKNOWN_AUDIO:
                    *decoderType = AUDIO_TYPE_UNKNOWN;
                    break;
                default:
                    *decoderType = AUDIO_TYPE_UNKNOWN;
                    break;
            }
            break;
        case FSL_MPG_DEMUX_VIDEO_STREAM:
            *mediaType = MEDIA_VIDEO;
            switch (sysInfo->Stream[streamNum].MediaProperty.VideoProperty.enuVideoType) {
                case FSL_MPG_DEMUX_NOVIDEO:
                    *decoderType = VIDEO_TYPE_UNKNOWN;
                    break;
                case FSL_MPG_DEMUX_MPEG2_VIDEO:
                    *decoderType = VIDEO_MPEG2;
                    break;
                case FSL_MPG_DEMUX_H264_VIDEO:
                    *decoderType = VIDEO_H264;
                    break;
                case FSL_MPG_DEMUX_MP4_VIDEO:
                    *decoderType = VIDEO_MPEG4;
                    break;
                case FSL_MPG_DEMUX_HEVC_VIDEO:
                    *decoderType = VIDEO_HEVC;
                    break;
                case FSL_MPG_DEMUX_AVS_VIDEO:
                    *decoderType = VIDEO_AVS;
                    break;
                case FSL_MPG_DEMUX_UNKNOWN_VIDEO:
                    *decoderType = VIDEO_TYPE_UNKNOWN;
                    break;
            }
            break;
        default:
            *mediaType = MEDIA_TYPE_UNKNOWN;
            break;
    }
    return PARSER_SUCCESS;
}

/**
 * Function to tell the language of a track used.
 * This is helpful to select a video/audio/subtitle track or menu pages.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param threeCharCode [out] Three character language code.
 *                  See ISO 639-2/T for the set of three character codes.Eg. 'eng' for English.
 *                  Four special cases:
 *                  mis- "uncoded languages"
 *                  mul- "multiple languages"
 *                  und- "undetermined language"
 *                  zxx- "no linguistic content"
 * @return
 */
int32 Mpeg2GetLanguage(FslParserHandle parserHandle, uint32 streamNum, uint8* threeCharCode) {
#define LANGUAGE_VALID_SIZE 3
#define LANGUAGE_UNDEFINED "und"
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;
    U32 found = 0;
    if (parserHandle == NULL) {
        return PARSER_ERR_INVALID_PARAMETER;
    }
    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if (pDemuxer->TS_PSI.IsTS) {
        FSL_MPG_DEMUX_TS_CNXT_T* pTSCnxt = &pDemuxer->pDemuxContext->TSCnxt;
        U32 PID = pTSCnxt->Streams.ReorderedStreamPID[streamNum];
        U32 p;
        S32 i, j;
        for (p = 0; p < pTSCnxt->nPMTs; p++) {
            for (i = 0; i < pTSCnxt->PMT[p].Sections; i++) {
                for (j = 0; j < pTSCnxt->PMT[p].PMTSection[i].Streams; j++) {
                    if (pTSCnxt->PMT[p].PMTSection[i].StreamPID[j] == PID) {
                        MPG2_PARSER_LOG("%s: stream: %d, PID: 0x%X, language: %s: \r\n",
                                        __FUNCTION__, streamNum, PID,
                                        pTSCnxt->PMT[p].PMTSection[i].Language[j]);
                        if (0 != pTSCnxt->PMT[p].PMTSection[i].Language[j][0]) {
                            memcpy(threeCharCode, pTSCnxt->PMT[p].PMTSection[i].Language[j],
                                   LANGUAGE_VALID_SIZE);
                            found = 1;
                        } else {
                            // no language is detected in PMT.
                        }
                    }
                }
            }
        }
    } else {
        // now, does't detect language in PS clips
    }
    if (found == 0) {
        MPG2_PARSER_LOG("%s: no found valid language for stream num %d \r\n", __FUNCTION__,
                        streamNum);
        memcpy(threeCharCode, LANGUAGE_UNDEFINED, LANGUAGE_VALID_SIZE);
    }
    threeCharCode[LANGUAGE_VALID_SIZE] = 0;
    return PARSER_SUCCESS;
}

/**
 * function to tell the bitrate of a stream.
 * For CBR stream, the real bitrate is given.
 * For VBR stream, 0 is given since the bitrate varies during the playback and MPEG2 parser does not
 * calculate the peak or average bit rate.
 *
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum [in] ID of the stream. [in] Number of the stream, 0-based.
 * @param bitrate [out] Bitrate. For CBR stream, this is the real bitrate.
 *                                            For VBR stream, the bitrate is 0 since max bitrate is
 * usually not available.
 * @return
 */
int32 Mpeg2GetBitRate(FslParserHandle parserHandle, uint32 streamNum, uint32* bitrate) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM)
        *bitrate = sysInfo->Stream[streamNum].MediaProperty.VideoProperty.uliVideoBitRate;
    else if (sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM)
        *bitrate = sysInfo->Stream[streamNum].MediaProperty.AudioProperty.uliAudioBitRate;
    else
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

/**
 * function to tell the sample duration in us of a stream.
 * If the sample duration is not a constant (eg. some audio, subtilte), 0 is given.
 *
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum[in] ID of the stream.
 * @param usDuration [out] Sample duration in us. If sample duration is not a constant, this value
 * is 0.
 * @return
 */

int32 Mpeg2GetVideoFrameRate(FslParserHandle parserHandle, uint32 streamNum, uint32* rate,
                             uint32* scale) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
        uint32 numerator, denominator;
        numerator = sysInfo->Stream[streamNum].MediaProperty.VideoProperty.uliFRNumerator;
        denominator = sysInfo->Stream[streamNum].MediaProperty.VideoProperty.uliFRDenominator;
        *rate = numerator;
        *scale = denominator;

        return PARSER_SUCCESS;
    }

    return PARSER_INVALID_TRACK_NUMBER;
}

/**
 * function to tell the width in pixels of a video stream.
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum [in] ID of the stream. It must point to a video stream.
 * @param width [out] Width in pixels.
 * @return
 */
int32 Mpeg2GetVideoFrameWidth(FslParserHandle parserHandle, uint32 streamNum, uint32* width) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
        *width = sysInfo->Stream[streamNum].MediaProperty.VideoProperty.uliVideoWidth;
        return PARSER_SUCCESS;
    } else
        return PARSER_ERR_INVALID_PARAMETER;
}

/**
 * function to tell the height in pixels of a video stream.
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum [in] ID of the stream. It must point to a video stream.
 * @param height [out] Height in pixels.
 * @return
 */
int32 Mpeg2GetVideoFrameHeight(FslParserHandle parserHandle, uint32 streamNum, uint32* height) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
        *height = sysInfo->Stream[streamNum].MediaProperty.VideoProperty.uliVideoHeight;
        return PARSER_SUCCESS;
    } else
        return PARSER_ERR_INVALID_PARAMETER;
}

/**
 * function to tell the scan type of video stream
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum [in] ID of the stream. It must point to a video stream.
 * @param scanType [out] scan type, either VIDEO_SCAN_PROGRESSIVE or VIDEO_SCAN_INTERLACED.
 * @return
 */
int32 Mpeg2GetVideoScanType(FslParserHandle parserHandle, uint32 streamNum, uint32* scanType) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPEGSTREAM_T* stream;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (streamNum > pDemuxer->SystemInfo.uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    stream = &(pDemuxer->SystemInfo.Stream[streamNum]);

    /* only h264 video can return video scan type for now */
    if (stream->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM &&
        (stream->MediaProperty.VideoProperty.enuVideoType == FSL_MPG_DEMUX_H264_VIDEO ||
         stream->MediaProperty.VideoProperty.enuVideoType == FSL_MPG_DEMUX_MPEG2_VIDEO)) {
        *scanType = stream->MediaProperty.VideoProperty.uliScanType;
        return PARSER_SUCCESS;
    } else
        return PARSER_ERR_INVALID_PARAMETER;
}

/**
 * function to tell how many channels in an audio stream.
 *
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum [in] ID of the stream. It must point to an audio steam.
 * @param numchannels [out] Number of the channels. 1 mono, 2 stereo, or more for multiple channels.
 * @return
 */
int32 Mpeg2GetAudioNumChannels(FslParserHandle parserHandle, uint32 streamNum,
                               uint32* numchannels) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM) {
        *numchannels = sysInfo->Stream[streamNum].MediaProperty.AudioProperty.usiAudioChannels;
        return PARSER_SUCCESS;
    } else
        return PARSER_ERR_INVALID_PARAMETER;
}

/**
 * function to tell the audio sample rate (sampling frequency) of an audio stream.
 *
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum [in] ID of the stream. It must point to an audio stream.
 * @param sampleRate [out] Audio integer sample rate (sampling frequency).
 * @return
 */
int32 Mpeg2GetAudioSampleRate(FslParserHandle parserHandle, uint32 streamNum, uint32* sampleRate) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM) {
        *sampleRate = sysInfo->Stream[streamNum].MediaProperty.AudioProperty.uliAudioSampleRate;
        return PARSER_SUCCESS;
    } else
        return PARSER_ERR_INVALID_PARAMETER;
}

int32 Mpeg2GetAudioBitsPerSample(FslParserHandle parserHandle, uint32 streamNum,
                                 uint32* bitsPerSample) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (sysInfo->Stream[streamNum].enuStreamType != FSL_MPG_DEMUX_AUDIO_STREAM)
        return PARSER_ERR_INVALID_PARAMETER;

    if (sysInfo->Stream[streamNum].MediaProperty.AudioProperty.enuAudioType ==
        FSL_MPG_DEMUX_PCM_AUDIO)
        *bitsPerSample =
                sysInfo->Stream[streamNum].MediaProperty.AudioProperty.usiAudioBitsPerSample;
    else
        *bitsPerSample = 0;

    return PARSER_SUCCESS;
}

#if 0
int32 Mpeg2ParserGetAudioBitsPerSample(FslParserHandle parserHandle, uint32 streamNum,uint32 * bitsPerSample)
{

    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr) parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;
    if(parserHandle==NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if(streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    *bitsPerSample = 0;

    if(sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM)
    {
        if(sysInfo->Stream[streamNum].MediaProperty.AudioProperty.enuAudioType == FSL_MPG_DEMUX_PCM_AUDIO)
        {
            * bitsPerSample = sysInfo->Stream[streamNum].MediaProperty.AudioProperty.usiAudioBitsPerSample;
        }
        return PARSER_SUCCESS;
    }
    else
        return PARSER_ERR_INVALID_PARAMETER;

}
#endif

int32 Mpeg2GetAudioPresentationNum(FslParserHandle parserHandle, uint32 streamNum,
                                   int32* presentationNum) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    *presentationNum = 0;

    if (sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM &&
        sysInfo->Stream[streamNum].MediaProperty.AudioProperty.enuAudioType ==
                FSL_MPG_DEMUX_AC4_AUDIO) {
        FSL_MPG_DEMUX_TS_CNXT_T* pTSCnxt = &(pDemuxer->pDemuxContext->TSCnxt);
        FSL_MPG_DEMUX_PMTSECTION_T* pPMTSection = NULL;
        U32 i, j;
        for (i = 0; i < pTSCnxt->PMT[0].Sections; i++) {
            pPMTSection = &(pTSCnxt->PMT[0].PMTSection[i]);
            for (j = 0; j < pPMTSection->Streams; j++) {
                if (pPMTSection->StreamType[j] == 0x15 /* DVB_AC4 */ &&
                    pPMTSection->AudioPresentationNum[j] > 0) {
                    *presentationNum = pPMTSection->AudioPresentationNum[j];
                    return PARSER_SUCCESS;
                }
            }
        }
    }

    return PARSER_ERR_INVALID_PARAMETER;
}

int32 Mpeg2GetAudioPresentationInfo(FslParserHandle parserHandle, uint32 streamNum,
                                    int32 presentationNum, int32* presentationId, char** language,
                                    uint32* masteringIndication, uint32* audioDescriptionAvailable,
                                    uint32* spokenSubtitlesAvailable,
                                    uint32* dialogueEnhancementAvailable) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* sysInfo;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    sysInfo = &(pDemuxer->SystemInfo);
    if (streamNum > sysInfo->uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (sysInfo->Stream[streamNum].enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM &&
        sysInfo->Stream[streamNum].MediaProperty.AudioProperty.enuAudioType ==
                FSL_MPG_DEMUX_AC4_AUDIO) {
        FSL_MPG_DEMUX_TS_CNXT_T* pTSCnxt = &(pDemuxer->pDemuxContext->TSCnxt);
        FSL_MPG_DEMUX_PMTSECTION_T* pPMTSection = NULL;
        U32 i, j;
        for (i = 0; i < pTSCnxt->PMT[0].Sections; i++) {
            pPMTSection = &(pTSCnxt->PMT[0].PMTSection[i]);
            for (j = 0; j < pPMTSection->Streams; j++) {
                if (pPMTSection->StreamType[j] == 0x15 /* DVB_AC4 */ &&
                    pPMTSection->AudioPresentationNum[j] > (U32)presentationNum) {
                    MPG_AUDIO_PRESENTATION_T* pAudioPresentation =
                            &(pPMTSection->AudioPresentation[j][presentationNum]);
                    *presentationId = pAudioPresentation->presentationId;
                    *masteringIndication = pAudioPresentation->masteringIndication;
                    *audioDescriptionAvailable =
                            (pAudioPresentation->audioDescriptionAvailable ? 1 : 0);
                    *spokenSubtitlesAvailable =
                            (pAudioPresentation->spokenSubtitlesAvailable ? 1 : 0);
                    *dialogueEnhancementAvailable =
                            (pAudioPresentation->dialogueEnhancementAvailable ? 1 : 0);
                    *language = pAudioPresentation->language;
                    return PARSER_SUCCESS;
                }
            }
        }
    }

    return PARSER_ERR_INVALID_PARAMETER;
}

/**
 * function to read the next sample from a stream.
 * For A/V streams, the time stamp of samples are continuous. If a sample is output, its start time
 * and end time are also output. But for subtitle text streams, the time stamp & duration are
 * discontinuous and encoded in the sample data.So the parser gives an "estimated" time stamp. The
 * decoder shall decode the accurate time stamp.
 *
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum [in]       ID of the stream to read.
 * @param sampleData [in]   Buffer to hold the sample data.If the buffer is not big enough, only the
 * 1st part of sample is output.
 *
 * @param dataSize [in/out]  Size of the buffer as input, in bytes.
 *                                        As output:
 *                                        If a sample or part of sample is output successfully
 * (return value is PARSER_SUCCESS ), it's the size of the data actually got.
 *
 *                                        If the sample can not be output at all because buffer is
 * too small (the return value is PARSER_INSUFFICIENT_MEMORY), it's the buffer size needed. Only for
 * DRM-protected files.
 *
 * @param usStartTime [out] Start time of the sample in us (timestamp)
 * @param usEndTime [out] End time of the sample in us.
 *
 * @param flag [out] Flags of this sample if a sample is got successfully.
 *
 *                            FLAG_SYNC_SAMPLE
 *                                  Whether this sample is a sync sample (key frame).
 *                                  For non-video media, the wrapper shall take every sample as sync
 * sample.
 *
 *                            FLAG_UNCOMPRESSED_SAMPLE
 *                                  Whethter this sample is a uncompressed one. Uncompressed samples
 * shall bypass the decoder. Warning: Video stream may have both compressed & uncompressed samples.
 *                                                But some MPEG2 clips seem to abuse this flag, sync
 * samples are mark as uncompressed, although they are actually compressed ones.
 *
 *                            FLAG_SAMPLE_ERR_CONCEALED
 *                                  There is error in bitstream but a sample is still got by error
 * concealment.
 *
 *                            FLAG_SAMPLE_SUGGEST_SEEK
 *                                  A seeking on ALL streams is suggested although samples can be
 * got by error concealment. Because there are many corrupts, and A/V sync is likely impacted by
 * simple concealment(scanning bitstream).
 *
 *                            FLAG_SAMPLE_NOT_FINISHED
 *                                  Sample data output is not finished because the buffer is not big
 * enough. Need to get the remaining data by repetitive calling this func. This feature is only for
 * non-protected clips.
 *
 *
 * @return  PARSER_SUCCESS     An entire sample or part of it is got successfully.
 *          PARSER_EOS     No sample is got because of end of the stream.
 *          PARSER_INSUFFICIENT_MEMORY Buffer is not big enough to hold the entire sample.
 *                                  The user can allocate a larger buffer and call this API again.
 * (Only for DRM-protected clips). PARSER_READ_ERROR  File reading error. No need for further error
 * concealment. PARSER_ERR_CONCEAL_FAIL  There is error in bitstream, and no sample can be found by
 * error concealment. A seeking is helpful. Others ...
 */

int32 Mpeg2GetNextSample(FslParserHandle parserHandle, uint32 streamNum, uint8** pSampleData,
                         void** pBufContext, uint32* dataSize, uint64* usPresTime,
                         uint64* usDuration, uint32* flag) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (streamNum >= pDemuxer->SystemInfo.uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if ((NULL == pSampleData) || (NULL == pBufContext) || (NULL == dataSize) ||
        (NULL == usPresTime) || (NULL == usDuration) || (NULL == flag))
        return PARSER_ERR_INVALID_PARAMETER;

    //
    *usDuration = (uint64)PARSER_UNKNOWN_DURATION;
    return Mpeg2ParserProcess(pDemuxer, streamNum, pSampleData, pBufContext, dataSize, usPresTime,
                              flag);
}

int32 Mpeg2GetFileNextSample(FslParserHandle parserHandle, uint32* streamNum, uint8** pSampleData,
                             void** pBufContext, uint32* dataSize, uint64* usPresTime,
                             uint64* usDuration, uint32* flag) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;
    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if ((NULL == streamNum) || (NULL == pSampleData) || (NULL == pBufContext) ||
        (NULL == dataSize) || (NULL == usPresTime) || (NULL == usDuration) || (NULL == flag))
        return PARSER_ERR_INVALID_PARAMETER;

    //
    *usDuration = (uint64)PARSER_UNKNOWN_DURATION;
    err = Mpeg2ParserProcessFile(pDemuxer, streamNum, pSampleData, pBufContext, dataSize,
                                 usPresTime, flag);
    return err;
}

int32 Mpeg2IsSeekable(FslParserHandle parserHandle, bool* seekable) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (pDemuxer->usLongestStreamDuration == 0)
        *seekable = 0;
    else
        *seekable = 1;
    return PARSER_SUCCESS;
}

static void GetTrackRange(MPEG2ObjectPtr pDemuxer, uint32 streamNum, uint32* ProgramFirstTrackIdx,
                          uint32* ProgramEndTrackIdx) {
    uint32 dwProgramIdx = 0;
    uint32 dwFirstTrack = 0;
    uint32 dwEndTrack = 0;

    *ProgramFirstTrackIdx = 0;
    *ProgramEndTrackIdx = pDemuxer->SystemInfo.uliNoStreams;

    if ((pDemuxer->TS_PSI.IsTS == 0) || (pDemuxer->pDemuxContext->TSCnxt.nParsedPMTs == 1) ||
        (pDemuxer->pDemuxContext->TSCnxt.PAT.NonProgramSelected == 1)) {
        return;
    }

    for (dwProgramIdx = 0; dwProgramIdx < pDemuxer->pDemuxContext->TSCnxt.nParsedPMTs;
         dwProgramIdx++) {
        dwEndTrack = dwFirstTrack + pDemuxer->pDemuxContext->TSCnxt.PMT[dwProgramIdx].ValidTrackNum;
        if (streamNum < dwEndTrack) {
            *ProgramFirstTrackIdx = dwFirstTrack;
            *ProgramEndTrackIdx = dwEndTrack;
            return;
        }

        dwFirstTrack = dwEndTrack;
    }

    MPG2_PARSER_LOG("GetTrackRange, unexpect !!!\n");
    return;
}

// these code still need some change
int32 Mpeg2Seek(FslParserHandle parserHandle, uint32 streamNum, uint64* usTime, uint32 flag)

{
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_SYSINFO_T* pSysInfo;
    U32 i, isMpeg2, seekStreamNum = 0;
    U64 fileLength = 0;
    U32 ProgramFirstTrackIdx, ProgramEndTrackIdx;

    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (pDemuxer->playMode == STREAMING_MODE) {
        if (*usTime != 0)
            return PARSER_ERR_NOT_SEEKABLE;
        else
            return PARSER_SUCCESS;
    }

    fileLength = pDemuxer->fileSize;

    if (pDemuxer->playMode == FILE_MODE)
        if (fileLength == 0)
            return PARSER_ERR_INVALID_MEDIA;

    if (*usTime > pDemuxer->usLongestStreamDuration)
        *usTime = pDemuxer->usLongestStreamDuration;

    pSysInfo = &(pDemuxer->SystemInfo);
    isMpeg2 = 0;

    if (streamNum >= pSysInfo->uliNoStreams) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    GetTrackRange(pDemuxer, streamNum, &ProgramFirstTrackIdx, &ProgramEndTrackIdx);

    for (i = ProgramFirstTrackIdx; i < ProgramEndTrackIdx; i++) {
        if (pSysInfo->Stream[i].isEnabled) {
            if (pSysInfo->Stream[i].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
                // Use Mpeg2 Video Stream to seek;
                if (SUPPORT_SEEK(pSysInfo->Stream[i].MediaProperty.VideoProperty.enuVideoType)) {
                    seekStreamNum = i;
                    isMpeg2 = 1;
                }
            } else if (!isMpeg2)
                seekStreamNum = i;

            if ((Err = Mpeg2ResetOuputBuffer(pDemuxer, i)))
                return Err;  //???
            pSysInfo->Stream[i].isBlocked = 0;
            pSysInfo->Stream[i].fileOffset = 0;
        }
    }
    if (streamNum != seekStreamNum) {
        pDemuxer->SystemInfo.Stream[streamNum].lastPTS = PARSER_UNKNOWN_TIME_STAMP;
        return PARSER_SUCCESS;
    }

    if (pSysInfo->Stream[seekStreamNum].usDuration == 0 && (*usTime != 0)) {
        isMpeg2 = 0;
        for (i = ProgramFirstTrackIdx; i < ProgramEndTrackIdx; i++) {
            if (pSysInfo->Stream[i].usDuration != 0) {
                seekStreamNum = i;
                if (*usTime > pSysInfo->Stream[i].usDuration)
                    *usTime = pSysInfo->Stream[i].usDuration;
                break;
            }
        }
    }
    // indexbreak flag set. ??? fix me, how about streamNum != seekStreamNum
    pDemuxer->index[streamNum].indexbreak = TRUE;

    if (pDemuxer->lastFileOffset != (U64)-1 && pDemuxer->lastFileOffset < pDemuxer->fileOffset) {
        if (pDemuxer->index[seekStreamNum].status == 0) {
            mergeIndexRange(&(pDemuxer->pIndexRange), pDemuxer->lastFileOffset,
                            pDemuxer->fileOffset);
            if (isIndexRangeContinuous(pDemuxer->pIndexRange, 0, pDemuxer->fileSize))
                pDemuxer->index[streamNum].status = 1;
        }
    }

    if ((*usTime) == 0) {
        for (i = ProgramFirstTrackIdx; i < ProgramEndTrackIdx; i++) {
            Err = Mpeg2ResetStreamInfo(pDemuxer, i, 0);
            pDemuxer->SystemInfo.Stream[i].cachedPTS = 0;
            pDemuxer->SystemInfo.Stream[i].lastPTS = 0;
        }
        goto bail;
    } else if (*usTime != 0) {
        if (pDemuxer->usLongestStreamDuration == 0) {
            Err = PARSER_ERR_INVALID_PARAMETER;
            goto bail;
        }
        if (*usTime > pDemuxer->usLongestStreamDuration) {
            Err = PARSER_EOS;
            goto bail;
        }

        if ((Err = Mpeg2SeekStream(pDemuxer, seekStreamNum, usTime, flag))) {
            goto bail;
        }

        pDemuxer->lastFileOffset = pDemuxer->fileOffset;

        for (i = ProgramFirstTrackIdx; i < ProgramEndTrackIdx; i++)
            if ((Err = Mpeg2ResetStreamInfo(pDemuxer, i, pDemuxer->fileOffset))) {
                goto bail;
            }

        // fix engr213303
        pDemuxer->SystemInfo.Stream[seekStreamNum].lastPTS = *usTime;
    }

bail:
    pDemuxer->index[streamNum].indexbreak = FALSE;
    pDemuxer->random_access = 0;
    return Err;
}

int32 Mpeg2GetNextSyncSample(FslParserHandle parserHandle, uint32 direction, uint32 streamNum,
                             uint8** sampleData, void** pAppContext, uint32* dataSize,
                             uint64* usPresTime, uint64* usDuration, uint32* flag) {
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;

    MPEG2ObjectPtr pDemuxer;
    uint32 i;
    uint32 enbleStatus[MAX_MPEG2_STREAMS];
    uint32 stackOuputMode;
    FSL_MPEGSTREAM_T* pStream;

    pDemuxer = (MPEG2ObjectPtr)parserHandle;
    stackOuputMode = pDemuxer->outputMode;
    pStream = &(pDemuxer->SystemInfo.Stream[0]);

    if (pDemuxer->playMode == STREAMING_MODE)
        return PARSER_ERR_NOT_SEEKABLE;

    if (NULL == usPresTime || NULL == usDuration || NULL == flag)
        return PARSER_ERR_INVALID_PARAMETER;

    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        enbleStatus[i] = pStream[i].isEnabled ? 1 : 0;
    }

    if (pStream[streamNum].isBlocked) {
        pStream[streamNum].isBlocked = 0;
        pDemuxer->fileOffset = pStream[streamNum].fileOffset;
    }

    *usDuration = (uint64)PARSER_UNKNOWN_DURATION;

    //??? why set to OUTPUT_BYTRACK ?
    pDemuxer->outputMode = OUTPUT_BYTRACK;

    MPG2_PARSER_LOG("stream: %d , isSyncFinished: %d, lastPTS: %lld \r\n", streamNum,
                    pStream[streamNum].isSyncFinished, pStream[streamNum].lastPTS);
    if (direction == FLAG_FORWARD) {
        U64 fileOffset = (U64)INDEX_NOSCANNED, fileOffset2 = 0, fileOffsetPre = 0;
        U64 PTS;
        U32 seekflag = 0;

        if (pStream[streamNum].isSyncFinished) {
#ifndef ALLOW_SEEK_AND_SPEED
            Err = Mpeg2ParserQueryIndex(pDemuxer, streamNum, pStream[streamNum].lastPTS, 1,
                                        &fileOffset);
            if (PARSER_SUCCESS == Err) {
                fileOffsetPre = fileOffset;
            } else if (PARSER_EOS == Err) {
                goto RESET_STREAM;
            }
#endif
            if (fileOffset == (U64)INDEX_NOSCANNED) {
                fileOffset2 = MPEG2FilePos(pDemuxer, streamNum);
                do {
                    fileOffsetPre = fileOffset;
                    fileOffset = fileOffset2;
                    seekflag = 0;

                    Err = Mpeg2ParserScan(pDemuxer, streamNum, &fileOffset, &fileOffset2, &PTS,
                                          &seekflag, 0);
                    if (Err)
                        goto RESET_STREAM;

                } while ((seekflag & FSL_MPG_DEMUX_PTS_VALID) == 0 ||
                         (seekflag & FLAG_SYNC_SAMPLE) == 0 ||
                         fileOffsetPre == (U64)INDEX_NOSCANNED);
            }

            Err = Mpeg2ResetStreamInfo(pDemuxer, streamNum, fileOffsetPre);
            if (Err)
                return Err;
        }

    } else {
        // backward, we have to seek to the previous I frame now.
        U64 fileOffset = (U64)INDEX_NOSCANNED, fileOffset2 = 0, fileOffsetPre = 0;
        U64 PTS;
        U32 seekflag = 0;
        U32 searchStep = 10 * 64 * 1024;
        U32 issynframefound = 0;
        U32 isreachbegin = 0;
        U64 synframeOffset = 0;
        U64 currentOffset = MPEG2FilePos(pDemuxer, streamNum);

        if (pStream[streamNum].isSyncFinished) {
#ifndef ALLOW_SEEK_AND_SPEED
            Err = Mpeg2ParserQueryIndex(pDemuxer, streamNum, pStream[streamNum].lastPTS, 2,
                                        &fileOffset);
            if (PARSER_SUCCESS == Err) {
                fileOffsetPre = fileOffset;
            } else if (PARSER_BOS == Err) {
                goto RESET_STREAM;
            }
#endif
            if (fileOffset == (U64)INDEX_NOSCANNED) {
                if (pStream[streamNum].lastSyncPosition != (U64)-1)
                    currentOffset = pStream[streamNum].lastSyncPosition;
                fileOffset2 = currentOffset;
                if (searchStep <
                    (pStream[streamNum].MediaProperty.VideoProperty.uliVideoBitRate >> 5))
                    searchStep =
                            (pStream[streamNum].MediaProperty.VideoProperty.uliVideoBitRate >> 5);
                if (pDemuxer->fileSize) {
                    if (searchStep > pDemuxer->fileSize >> MIN_SEARCHCOUNT) {
                        searchStep = pDemuxer->fileSize >> MIN_SEARCHCOUNT;
                        if (searchStep < MIN_SEARCHSTEP) {
                            searchStep = MIN_SEARCHSTEP;
                        }
                    }
                }

                do {
                    if (fileOffset2 >= currentOffset) {
                        fileOffset2 = currentOffset;

                        if (fileOffset2 > searchStep) {
                            fileOffset2 -= searchStep;
                        } else if (!isreachbegin) {
                            fileOffset2 = 0;
                            isreachbegin = 1;
                        } else {
                            Err = PARSER_BOS;
                            break;
                        }
                    }

                    pDemuxer->index[streamNum].indexdirection = 1;
                    fileOffset = fileOffset2;

                    do {
                        fileOffsetPre = fileOffset;
                        fileOffset = fileOffset2;
                        seekflag = 0;

                        Err = Mpeg2ParserScan(pDemuxer, streamNum, &fileOffset, &fileOffset2, &PTS,
                                              &seekflag, 0);
                        if (Err != PARSER_EOS && Err != PARSER_SUCCESS) {
                            pDemuxer->index[streamNum].indexdirection = 0;
                            goto RESET_STREAM;
                        } else if (Err == PARSER_EOS)
                            break;

                        if (fileOffset2 >= currentOffset)
                            break;
                    } while ((seekflag & FSL_MPG_DEMUX_PTS_VALID) == 0 ||
                             (seekflag & FLAG_SYNC_SAMPLE) == 0 ||
                             fileOffsetPre == (U64)INDEX_NOSCANNED);

                    pDemuxer->index[streamNum].indexdirection = 0;

                    if (fileOffset < currentOffset) {
                        if ((seekflag & FLAG_SYNC_SAMPLE) != 0 &&
                            (seekflag & FSL_MPG_DEMUX_PTS_VALID) != 0) {
                            issynframefound = 1;
                            synframeOffset = fileOffset;
                        }
                    }
                    if (isreachbegin)
                        break;

                    currentOffset = currentOffset > searchStep ? currentOffset - searchStep : 0;
                } while (!issynframefound);

                if (!issynframefound || synframeOffset == pStream[streamNum].lastSyncPosition) {
                    Err = PARSER_BOS;
                    goto RESET_STREAM;
                }
                fileOffset = synframeOffset;
            }

            MPG2_PARSER_LOG("reset sync offset: %lld(0x%llX) \r\n", fileOffset, fileOffset);
            if ((Err = Mpeg2ResetStreamInfo(pDemuxer, streamNum, fileOffset)))
                goto RESET_STREAM;

            pStream[streamNum].lastSyncPosition = fileOffset;
        }
    }

    // clear the output buffer and disable uncessary streams
    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        if (pStream[i].isSyncFinished)
            Mpeg2ResetOuputBuffer(pDemuxer, i);
        if (i != streamNum)
            pStream[i].isEnabled = 0;
    }

    pStream[streamNum].isSyncFinished = 0;
    Err = Mpeg2ParserProcess(pDemuxer, streamNum, sampleData, pAppContext, dataSize, usPresTime,
                             flag);

    if (*usPresTime == (uint64)PARSER_UNKNOWN_TIME_STAMP) {
        if (direction == FLAG_FORWARD &&
            pDemuxer->index[streamNum].forwardPTS != (uint64)PARSER_UNKNOWN_TIME_STAMP) {
            *usPresTime = pDemuxer->index[streamNum].forwardPTS;
        } else if (direction == FLAG_BACKWARD &&
                   pDemuxer->index[streamNum].rewardPTS != (uint64)PARSER_UNKNOWN_TIME_STAMP) {
            *usPresTime = pDemuxer->index[streamNum].rewardPTS;
        }
    }

    if (*usPresTime != (uint64)PARSER_UNKNOWN_TIME_STAMP)
        pStream[streamNum].lastPTS = *usPresTime;

    if (((*flag) & FLAG_SAMPLE_NOT_FINISHED) == 0) {
        pStream[streamNum].isSyncFinished = 1;
    }

RESET_STREAM:
    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        if (enbleStatus[i]) {
            pStream[i].isEnabled = 1;
            pStream[i].isBlocked = 0;
            pStream[i].fileOffset = pDemuxer->fileOffset;
        }
    }
    pDemuxer->outputMode = stackOuputMode;
    return Err;
}

int32 Mpeg2DeleteParser(FslParserHandle parserHandle) {

    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_CNXT_T* pCnxt;
    FSL_MPEGSTREAM_T* pStream;
    U32 i = 0;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    FreeTempStreamBuffer(pDemuxer);
    pStream = &(pDemuxer->SystemInfo.Stream[0]);
    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        if (pStream[i].frameBuffer)
            LOCALFree(pStream[i].frameBuffer);
        if (pStream[i].outputArray.pLinkMem)
            LOCALFree(pStream[i].outputArray.pLinkMem);
        if (pStream[i].codecSpecInformation)
            LOCALFree(pStream[i].codecSpecInformation);

        if (pStream[i].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
            DeleteParserFunc DeleteParser = NULL;
            if (pStream[i].MediaProperty.VideoProperty.enuVideoType == FSL_MPG_DEMUX_H264_VIDEO &&
                pStream[i].pParser) {
                DeleteParser = &DeleteH264Parser;
            } else if (pStream[i].MediaProperty.VideoProperty.enuVideoType ==
                               FSL_MPG_DEMUX_HEVC_VIDEO &&
                       pStream[i].pParser) {
                DeleteParser = &DeleteHevcParser;
            }
            if (DeleteParser) {
                (void)DeleteParser(pStream[i].pParser);
            }
        }

        if (pStream[i].enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM) {
            if (pStream[i].MediaProperty.AudioProperty.enuAudioType == FSL_MPG_DEMUX_AAC_AUDIO &&
                pStream[i].MediaProperty.AudioProperty.enuAudioSubType == FSL_MPG_DEMUX_AAC_RAW) {
                if (pStream[i].pParser)
                    (void)DeleteAacLatmParser(pStream[i].pParser);
            }
        }

        if (pDemuxer->index[i].pItem) {
            LOCALFree(pDemuxer->index[i].pItem);
            pDemuxer->index[i].pItem = NULL;
        }

        if (pDemuxer->index[i].pts) {
            LOCALFree(pDemuxer->index[i].pts);
            pDemuxer->index[i].pts = NULL;
        }

        Mpeg2ResetOuputBuffer(pDemuxer, i);
    }

    INDEX_RANGE_T* head;
    while (pDemuxer->pIndexRange) {
        head = pDemuxer->pIndexRange;
        pDemuxer->pIndexRange = pDemuxer->pIndexRange->next;
        free(head);
    }

    pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;

    if (pCnxt) {
        FreePMTInfoList(pDemuxer, pCnxt->TSCnxt.pPMTInfoList);

        if (pCnxt->TSCnxt.pbyProInfoMenu)
            LOCALFree(pCnxt->TSCnxt.pbyProInfoMenu);

        if (pCnxt->SeqHdrBuf.pSH)
            LOCALFree(pCnxt->SeqHdrBuf.pSH);
        /*internal buffers   */
        if (pCnxt->TSCnxt.TempBufs.PATSectionBuf.pBuf)
            LOCALFree(pCnxt->TSCnxt.TempBufs.PATSectionBuf.pBuf);

        for (i = 0; i < pCnxt->TSCnxt.nPMTs; i++) {
            if (pCnxt->TSCnxt.TempBufs.PMTSectionBuf[i].pBuf)
                LOCALFree(pCnxt->TSCnxt.TempBufs.PMTSectionBuf[i].pBuf);
        }

        if (pCnxt->TSCnxt.TempBufs.TSTempBuf.pBuf)
            LOCALFree(pCnxt->TSCnxt.TempBufs.TSTempBuf.pBuf);

        for (i = 0; i < MAX_MPEG2_STREAMS; i++) {
            if (pCnxt->TSCnxt.TempBufs.PESStreamBuf[i].pBuf)
                LOCALFree(pCnxt->TSCnxt.TempBufs.PESStreamBuf[i].pBuf);
        }

        LOCALFree(pCnxt);
    }

    if (pDemuxer->inputStream)
        LocalFileClose();
    if (pDemuxer->CacheBuffer.pCacheBuffer)
        LOCALFree(pDemuxer->CacheBuffer.pCacheBuffer);

    if (pDemuxer)
        LOCALFree(pDemuxer);

    return PARSER_SUCCESS;
}

int32 Mpeg2GetFileNextSyncSample(FslParserHandle parserHandle, uint32 direction, uint32* pStreamNum,
                                 uint8** sampleData, void** pAppContext, uint32* dataSize,
                                 uint64* usPresTime, uint64* usDuration, uint32* flag) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    uint32 i;
    int32 ret;

    *sampleData = NULL;
    *pAppContext = NULL;
    *dataSize = 0;

    // Fix me ??? For multi-program, if all enabled, the first program's video track is used.
    // If support all video tracks, may use current file offset to select a nearest track.
    // But in Mpeg2GetNextSyncSample, use pDemuxer->SystemInfo.Stream[streamNum].lastPTS as
    // reference.
    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        if (pDemuxer->SystemInfo.Stream[i].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM &&
            pDemuxer->SystemInfo.Stream[i].isEnabled) {
            if (SUPPORT_SEEK(
                        pDemuxer->SystemInfo.Stream[i].MediaProperty.VideoProperty.enuVideoType))
                break;
        }
    }

    if (i >= pDemuxer->SystemInfo.uliNoStreams)
        return PARSER_ILLEAGAL_OPERATION;

    if ((ret = Mpeg2GetNextSyncSample(parserHandle, direction, i, sampleData, pAppContext, dataSize,
                                      usPresTime, usDuration, flag)))
        return ret;
    *pStreamNum = i;
    MPG2_PARSER_LOG(
            "%s: direction: %d, stream: %d, datasize: %d, time: %lld, duration: %lld, flag: 0x%X "
            "\r\n",
            __FUNCTION__, direction, streamNum, *dataSize, *usPresTime, *usDuration, *flag);

    pDemuxer->outputMode = OUTPUT_BYFILE;

    return PARSER_SUCCESS;
}

static void FreePMTInfoList(MPEG2ObjectPtr pDemuxer, PMTInfoList* pPMTInfoList) {
    uint32 dwProgramIdx = 0;

    if ((pPMTInfoList == NULL) || (pDemuxer == NULL)) {
        return;
    }

    if (pPMTInfoList->m_ptPMTInfo) {
        for (dwProgramIdx = 0; dwProgramIdx < pPMTInfoList->m_dwProgramNum; dwProgramIdx++) {
            PMTInfo* pPMTInfo = &(pPMTInfoList->m_ptPMTInfo[dwProgramIdx]);

            if (pPMTInfo->m_ptTrackInfo) {
                LOCALFree(pPMTInfo->m_ptTrackInfo);
            }
        }

        LOCALFree(pPMTInfoList->m_ptPMTInfo);
    }

    LOCALFree(pPMTInfoList);
}

int32 Mpeg2GetMetaData(FslParserHandle parserHandle, UserDataID userDataId,
                       UserDataFormat* userDataFormat, uint8** userData, uint32* userDataLength) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;
    FSL_MPG_DEMUX_TS_CNXT_T* pTSCnxt = NULL;
    uint32 dwProgramIdx = 0;
    uint32 dwSize = 0;
    ProgramInfo* ptProInfo = NULL;

    if ((NULL == parserHandle) || (userDataFormat == NULL) || (userData == NULL) ||
        (userDataLength == NULL))
        return PARSER_ERR_INVALID_PARAMETER;

    *userData = NULL;
    *userDataLength = 0;

    if (userDataId == USER_DATA_PROGRAMINFO) {
        if ((pDemuxer->pDemuxContext == NULL) || (*userDataFormat != USER_DATA_FORMAT_PROGRAM_INFO))
            return PARSER_ERR_INVALID_PARAMETER;

        pTSCnxt = &(pDemuxer->pDemuxContext->TSCnxt);

        if (pTSCnxt->pbyProInfoMenu == NULL) {
            // alloc max, so if program number increased, no need to realloc
            dwSize = sizeof(ProgramInfoMenu) + NO_PROGRAM_SUPPORT_MAX * sizeof(ProgramInfo);
            pTSCnxt->pbyProInfoMenu = LOCALMalloc(dwSize);

            if (pTSCnxt->pbyProInfoMenu == NULL)
                return PARSER_INSUFFICIENT_MEMORY;

            memset(pTSCnxt->pbyProInfoMenu, 0, dwSize);
        }

        *userData = pTSCnxt->pbyProInfoMenu;

        if (pTSCnxt->nParsedPMTs == 0) {
            *userDataLength = sizeof(ProgramInfoMenu);
            return PARSER_SUCCESS;
        }

        if (pTSCnxt->nParsedPMTs > NO_PROGRAM_SUPPORT_MAX) {
            return PARSER_INSUFFICIENT_MEMORY;
        }

        ((ProgramInfoMenu*)pTSCnxt->pbyProInfoMenu)->m_dwProgramNum = pTSCnxt->nParsedPMTs;
        ptProInfo = (ProgramInfo*)(pTSCnxt->pbyProInfoMenu + sizeof(ProgramInfoMenu));
        for (dwProgramIdx = 0; dwProgramIdx < pTSCnxt->nParsedPMTs; dwProgramIdx++) {
            ptProInfo[dwProgramIdx].m_dwChannel =
                    pTSCnxt->PMT[dwProgramIdx].PMTSection[0].ProgramNum;
            ptProInfo[dwProgramIdx].m_dwPID = pTSCnxt->PMT[dwProgramIdx].PID;
        }

        *userDataLength = sizeof(ProgramInfoMenu) + pTSCnxt->nParsedPMTs * sizeof(ProgramInfo);

        return PARSER_SUCCESS;
    } else if (userDataId == USER_DATA_PMT) {
        PMTInfoList* pPMTInfoList = NULL;
        uint32 dwTrackIdx = 0;
        uint32 dwTrackNum = 0;
        TrackInfo* ptTrackInfo = NULL;

        if ((pDemuxer->pDemuxContext == NULL) || (*userDataFormat != USER_DATA_FORMAT_PMT_INFO))
            return PARSER_ERR_INVALID_PARAMETER;

        pTSCnxt = &(pDemuxer->pDemuxContext->TSCnxt);

        // already got
        if (pTSCnxt->pPMTInfoList) {
            *userData = (uint8*)pTSCnxt->pPMTInfoList;
            *userDataLength = sizeof(PMTInfoList);
            return PARSER_SUCCESS;
        }

        pPMTInfoList = (PMTInfoList*)LOCALCalloc(1, sizeof(PMTInfoList));
        if (pPMTInfoList == NULL)
            return PARSER_INSUFFICIENT_MEMORY;

        pTSCnxt->pPMTInfoList = pPMTInfoList;

        // PS
        if ((pDemuxer->TS_PSI.IsTS == 0) || (pTSCnxt->PAT.NonProgramSelected == 1)) {
            pPMTInfoList->m_dwProgramNum = 1;

            pPMTInfoList->m_ptPMTInfo = (PMTInfo*)LOCALCalloc(1, sizeof(PMTInfo));
            if (pPMTInfoList->m_ptPMTInfo == NULL)
                goto nomemory;

            pPMTInfoList->m_ptPMTInfo[0].m_dwPID = INVALID_PID;
            pPMTInfoList->m_ptPMTInfo[0].m_dwChannel = INVALID_CHANNEL;

            dwTrackNum = pDemuxer->SystemInfo.uliNoStreams;
            pPMTInfoList->m_ptPMTInfo[0].m_dwTrackNum = dwTrackNum;

            ptTrackInfo = (TrackInfo*)LOCALCalloc(dwTrackNum, sizeof(TrackInfo));
            if (ptTrackInfo == NULL)
                goto nomemory;

            pPMTInfoList->m_ptPMTInfo[0].m_ptTrackInfo = ptTrackInfo;

            for (dwTrackIdx = 0; dwTrackIdx < dwTrackNum; dwTrackIdx++) {
                ptTrackInfo[dwTrackIdx].m_dwPID = INVALID_PID;
                ptTrackInfo[dwTrackIdx].m_dwTrackNo = dwTrackIdx;
            }

            *userData = (uint8*)pTSCnxt->pPMTInfoList;
            *userDataLength = sizeof(PMTInfoList);
            pTSCnxt->pPMTInfoList = pPMTInfoList;
            return PARSER_SUCCESS;
        }

        pPMTInfoList->m_dwProgramNum = pTSCnxt->nParsedPMTs;
        pPMTInfoList->m_ptPMTInfo = (PMTInfo*)LOCALCalloc(pTSCnxt->nParsedPMTs, sizeof(PMTInfo));
        if (pPMTInfoList->m_ptPMTInfo == NULL)
            goto nomemory;

        // program
        for (dwProgramIdx = 0; dwProgramIdx < pTSCnxt->nParsedPMTs; dwProgramIdx++) {
            pPMTInfoList->m_ptPMTInfo[dwProgramIdx].m_dwChannel =
                    pTSCnxt->PMT[dwProgramIdx].PMTSection[0].ProgramNum;
            pPMTInfoList->m_ptPMTInfo[dwProgramIdx].m_dwPID = pTSCnxt->PMT[dwProgramIdx].PID;
            pPMTInfoList->m_ptPMTInfo[dwProgramIdx].m_dwTrackNum =
                    pTSCnxt->PMT[dwProgramIdx].ValidTrackNum;

            // track
            dwTrackNum = pTSCnxt->PMT[dwProgramIdx].ValidTrackNum;
            ptTrackInfo = (TrackInfo*)LOCALCalloc(dwTrackNum, sizeof(TrackInfo));
            if (ptTrackInfo == NULL)
                goto nomemory;

            for (dwTrackIdx = 0; dwTrackIdx < dwTrackNum; dwTrackIdx++) {
                ptTrackInfo[dwTrackIdx].m_dwTrackNo =
                        pTSCnxt->PMT[dwProgramIdx].adwValidTrackIdx[dwTrackIdx];
                ptTrackInfo[dwTrackIdx].m_dwPID =
                        pTSCnxt->PMT[dwProgramIdx].adwValidTrackPID[dwTrackIdx];
            }

            pPMTInfoList->m_ptPMTInfo[dwProgramIdx].m_ptTrackInfo = ptTrackInfo;
        }

        *userData = (uint8*)pTSCnxt->pPMTInfoList;
        *userDataLength = sizeof(PMTInfoList);
        pTSCnxt->pPMTInfoList = pPMTInfoList;
        return PARSER_SUCCESS;

    nomemory:
        FreePMTInfoList(pDemuxer, pPMTInfoList);
        pTSCnxt->pPMTInfoList = NULL;

        return PARSER_INSUFFICIENT_MEMORY;
    } else {
        return PARSER_ERR_INVALID_PARAMETER;
    }
}

int32 Mpeg2GetPCR(FslParserHandle parserHandle, uint32 programNum, uint64* PCR) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;

    if (NULL == parserHandle || NULL == PCR ||
        programNum >= pDemuxer->pDemuxContext->TSCnxt.nParsedPMTs)
        return PARSER_ERR_INVALID_PARAMETER;

    *PCR = pDemuxer->pDemuxContext->TSCnxt.PCRInfo[programNum].PCR;

    return PARSER_SUCCESS;
}

int32 Mpeg2FlushTrack(FslParserHandle parserHandle, uint32 numStream) {
    MPEG2ObjectPtr pDemuxer = (MPEG2ObjectPtr)parserHandle;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;
    if (numStream >= pDemuxer->SystemInfo.uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if (PARSER_SUCCESS != Mpeg2ResetOuputBuffer(pDemuxer, numStream))
        return PARSER_ERR_UNKNOWN;

    if (PARSER_SUCCESS != Mpeg2FlushStreamInternal(pDemuxer, numStream))
        return PARSER_ERR_UNKNOWN;

    return PARSER_SUCCESS;
}

// end of file
