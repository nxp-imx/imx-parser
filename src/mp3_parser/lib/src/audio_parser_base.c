/*
***********************************************************************
* Copyright (c) 2012-2014, Freescale Semiconductor, Inc.
* Copyright 2017-2022, 2024, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
*
***********************************************************************
*/

#include "audio_parser_base.h"
#include <string.h>

#define PARSER_TICKS_PER_SECOND (1000000)
#define MAX_VALUE_UINT64 (uint64)(-1LL)

// Extern Functions
extern AUDIO_PARSERRETURNTYPE Mp3ParserFileHeader(AUDIO_FILE_INFO* pFileInfo, uint8* pBuffer,
                                                  uint32 nBufferLen);
extern AUDIO_PARSERRETURNTYPE Mp3ParserFrame(AUDIO_FRAME_INFO* pFrameInfo, uint8* pBuffer,
                                             uint32 nBufferLen);
extern AUDIO_PARSERRETURNTYPE Mp3GetFrameSize(uint32 header, uint32* frame_size,
                                              int* out_sampling_rate, int* out_channels,
                                              int* out_bitrate, int* out_num_samples);

int32 AudioParserBaseCreate(Audio_Parser_Base_t* pParserBase, Parser_Input_Params_t* pParamList) {
    int32 ret = PARSER_SUCCESS;
    uint64 nAudioDuration;
    int32 err;

    if (NULL == pParserBase || NULL == pParamList)
        return PARSER_ERR_INVALID_PARAMETER;

    pParserBase->nBeginPoint = 0;
    pParserBase->nReadPoint = 0;
    pParserBase->nBeginPointOffset = 0;
    pParserBase->bBeginPointFounded = FALSE;

    pParserBase->nSource2CurPos = 0;
    pParserBase->bSegmentStart = TRUE;

    pParserBase->bTOCSeek = FALSE;
    pParserBase->nOneSecondSample = 0;

    pParserBase->nSampleRate = 44100;
    pParserBase->bCBR = FALSE;
    pParserBase->bVBRDurationReady = FALSE;

    pParserBase->hSeekTable = NULL;

    pParserBase->sourceFileHandle = pParamList->sourceFileHandle;
    pParserBase->appContext = pParamList->appContext;
    pParserBase->fileOps = pParamList->fileOps;
    pParserBase->memoryOps = pParamList->memoryOps;

    pParserBase->nFileSize =
            pParserBase->fileOps.Size(pParserBase->sourceFileHandle, pParserBase->appContext);

    // Set function call pointer
    pParserBase->ParserFileHeader = pParamList->ParserFileHeader;
    pParserBase->ParserFrame = pParamList->ParserFrame;
    pParserBase->GetFrameSize = pParamList->GetFrameSize;

    pParserBase->nEndPoint = pParserBase->nFileSize;
    if (pParserBase->nEndPoint == 0) {
        pParserBase->nEndPoint = MAX_VALUE_UINT64;
    }
    if (pParserBase->LiveFlag & FILE_FLAG_NON_SEEKABLE) {
        return ret;
    }

#if 1
    /** Parser ID3 tag */
    pParserBase->hID3 = NULL;
    err = ID3ParserCreate(&(pParserBase->fileOps), &(pParserBase->memoryOps),
                          (pParserBase->sourceFileHandle), (pParserBase->appContext),
                          &(pParserBase->hID3), pParserBase->bEnableConvert);
    if (PARSER_SUCCESS != err) {
        PARSERMSG("Failed to create ID3 Parser\n");
    } else {
        uint32 nID3V2_size = 0;
        err = ID3ParserGetID3V2Size(pParserBase->hID3, &(nID3V2_size));
        if (PARSER_SUCCESS != err) {
            PARSERMSG("Failed to get ID3 V2 size\n");
        }
        pParserBase->nBeginPoint += nID3V2_size;
        PARSERMSG("ID3 V2 size is %d Bytes\n", pParserBase->nBeginPoint);
    }
#endif
    /** Parser file header */
    ret = AudioParserFileHeader(pParserBase);
    if (ret != PARSER_SUCCESS) {
        PARSERMSG("Failed in parser file header !\n");
        return ret;
    } else if (pParserBase->LiveFlag & FILE_FLAG_NON_SEEKABLE) {
        // live stream no need to parse more.
        return ret;
    }

    if (pParserBase->bBeginPointFounded == FALSE)  // && bGetMetadata != TRUE)
    {
        ret = ParserFindBeginPoint(pParserBase);
        if (ret != PARSER_SUCCESS)
            return ret;
    }

    ret = ParserThreeSegmentAudio(pParserBase);
    if (ret != PARSER_SUCCESS) {
        return ret;
    }

    if (TRUE == pParserBase->bBeginPointFounded) {
        pParserBase->nBeginPoint += pParserBase->nBeginPointOffset;
        // set BitRate here
        pParserBase->nSampleRate = pParserBase->FrameInfo.nSamplingRate;
        pParserBase->nChannels = pParserBase->FrameInfo.nChannels;
    }
    PARSERMSG("Begin Point: %lld\n", pParserBase->nBeginPoint);
    pParserBase->nAvrageBitRate = GetAvrageBitRate(pParserBase);
    if (pParserBase->nAvrageBitRate == 0) {
        PARSERMSG("Audio duration is 0.\n");
        return -1;
    }
    nAudioDuration = (uint64)((pParserBase->nEndPoint - pParserBase->nBeginPoint) << 3) *
                     PARSER_TICKS_PER_SECOND / pParserBase->nAvrageBitRate;
    pParserBase->usDuration = nAudioDuration;
    PARSERMSG("Audio Duration: %lld\n", pParserBase->usDuration);

    if (pParserBase->bBeginPointFounded == FALSE)  // && bGetMetadata != TRUE)
    {
        ret = ParserFindBeginPoint(pParserBase);
        if (ret != PARSER_SUCCESS)
            return ret;
    }

    PARSERMSG(
            "Have Xing: %d total frame: %d sample rate: %d sample_per_fr: %d total bytes: %d "
            "TOC(1): %d\n",
            pParserBase->FrameInfo.FrameInfo.xing_exist,
            pParserBase->FrameInfo.FrameInfo.total_frame_num,
            pParserBase->FrameInfo.FrameInfo.sampling_rate,
            pParserBase->FrameInfo.FrameInfo.sample_per_fr,
            pParserBase->FrameInfo.FrameInfo.total_bytes, pParserBase->FrameInfo.FrameInfo.TOC[1]);

    // While it is safe to send the XING/VBRI frame to the decoder, this will
    // result in an extra 1152 samples being output. The real first frame to decode
    // is after the XING/VBRI frame, so skip there.
    if (pParserBase->FrameInfo.FrameInfo.xing_exist ||
        pParserBase->FrameInfo.FrameInfo.vbri_exist) {
        pParserBase->nBeginPoint += pParserBase->FrameInfo.FrameInfo.frm_size;
        if (0 == pParserBase->nFileSize && pParserBase->FrameInfo.FrameInfo.total_bytes > 0) {
            pParserBase->nFileSize = pParserBase->FrameInfo.FrameInfo.total_bytes;
            pParserBase->nEndPoint = pParserBase->nFileSize;
        }
        if (pParserBase->FrameInfo.FrameInfo.total_bytes && pParserBase->FrameInfo.FrameInfo.TOC[1])
            pParserBase->bCBR = FALSE;
    }

    if (pParserBase->FrameInfo.FrameInfo.sampling_rate &&
        pParserBase->FrameInfo.FrameInfo.sample_per_fr &&
        pParserBase->FrameInfo.FrameInfo.total_frame_num) {
        nAudioDuration = (uint64)pParserBase->FrameInfo.FrameInfo.total_frame_num *
                         pParserBase->FrameInfo.FrameInfo.sample_per_fr * PARSER_TICKS_PER_SECOND /
                         pParserBase->FrameInfo.FrameInfo.sampling_rate;
    }
    pParserBase->usDuration = nAudioDuration;
    PARSERMSG("Audio Duration: %lld\n", pParserBase->usDuration);

    if (pParserBase->bCBR == FALSE && pParserBase->FrameInfo.FrameInfo.total_bytes &&
        pParserBase->FrameInfo.FrameInfo.TOC[1]) {
        pParserBase->bTOCSeek = TRUE;
        pParserBase->bVBRDurationReady = TRUE;
        pParserBase->nAvrageBitRate =
                (uint32)((uint64)pParserBase->FrameInfo.FrameInfo.total_bytes * 8 *
                         PARSER_TICKS_PER_SECOND / nAudioDuration);
    }

    pParserBase->fileOps.Seek(pParserBase->sourceFileHandle, pParserBase->nBeginPoint, SEEK_SET,
                              pParserBase->appContext);
    pParserBase->nReadPoint = pParserBase->nBeginPoint;

    return ret;
}

int32 AudioParserBaseDestroy(Audio_Parser_Base_t* pParserBase) {
    int32 ret = PARSER_SUCCESS;

    // Free ID3 parser instance
    if (pParserBase->hID3) {
        ID3ParserDelete(pParserBase->hID3);
    }

    // Free seek table instance.
    if (pParserBase->hSeekTable) {
        AudioIndexTableDestroy(pParserBase->hSeekTable);
        pParserBase->hSeekTable = NULL;
    }

    return ret;
}

int32 ParserThreeSegmentAudio(Audio_Parser_Base_t* pParserBase) {
    int32 ret = 0;
    uint32 nActuralRead, nSegmentCnt = 0;
    uint32 nReadPointTmp = 0, nReadPointTmp2 = 0;
    uint32 nReadLen = AUDIO_PARSER_READ_SIZE;
    uint32 nDataSize = (uint32)(pParserBase->nEndPoint - pParserBase->nBeginPoint);
    uint8* pTmpBuffer;
    uint32 i;
    bool abnormalSize = FALSE;
    FslFileStream fileOps = pParserBase->fileOps;
    FslFileHandle sourceFileHandle = pParserBase->sourceFileHandle;

    int tmp = fileOps.Seek(pParserBase->sourceFileHandle, pParserBase->nBeginPoint, SEEK_SET,
                           pParserBase->appContext);
    if (tmp != 0)
        return PARSER_ERR_UNKNOWN;

    pParserBase->bCBR = TRUE;
    pParserBase->FrameInfo.bIsCBR = TRUE;

    pTmpBuffer = AudioParserGetBuffer(pParserBase, AUDIO_PARSER_READ_SIZE);
    if (pTmpBuffer == NULL) {
        PARSERMSG("Can't get memory.\n");
        return PARSER_INSUFFICIENT_MEMORY;
    }

    for (i = 0;; i++) {
        if (nReadPointTmp + nReadLen > nDataSize) {
            nReadLen = nDataSize - nReadPointTmp;
        }

        ret = fileOps.Read(sourceFileHandle, pTmpBuffer, nReadLen, pParserBase->appContext);
        if (ret < 0)
            nActuralRead = 0;
        else
            nActuralRead = ret;

        nReadPointTmp += nActuralRead;
        nReadPointTmp2 += nActuralRead;

        ParserAudioFrame(pParserBase, pTmpBuffer, nActuralRead, nSegmentCnt);

        if (nActuralRead < AUDIO_PARSER_READ_SIZE || abnormalSize) {
            PARSERMSG("Audio file parser reach end.\n");
            break;
        }

        if (nReadPointTmp2 >= AUDIO_PARSER_SEGMENT_SIZE) {
            int32 nSkip = (nDataSize - 3 * AUDIO_PARSER_SEGMENT_SIZE) / 2;
            if (pParserBase->nEndPoint == MAX_VALUE_UINT64) {
                abnormalSize = TRUE;
                nSkip = 3 * AUDIO_PARSER_SEGMENT_SIZE;
            }
            nReadPointTmp2 = 0;
            nSegmentCnt++;
            if (nSkip < 0) {
                nSkip = 0;
            }

            if (-1 == fileOps.Seek(sourceFileHandle, nSkip, SEEK_CUR, pParserBase->appContext)) {
                PARSERMSG("Audio file seek fail.\n");
                break;
            }
            nReadPointTmp += nSkip;
        }
    }

    if (pParserBase->FrameInfo.bIsCBR == FALSE) {
        pParserBase->bCBR = FALSE;
    }

    AudioParserFreeBuffer(pParserBase, pTmpBuffer);

    return 0;
}

uint32 ParserAudioFrame(Audio_Parser_Base_t* pParserBase, uint8* pBuffer, uint32 nBufferSize,
                        uint32 nSegmentCnt) {
    uint32 ret = PARSER_SUCCESS;
    uint8* pBuffer2 = pBuffer;
    uint32 nBufferSize2;
    uint32 nOverlap = pParserBase->nOverlap;

    pBuffer2 -= nOverlap;
    nBufferSize2 = nBufferSize + nOverlap;

    ParserAudioFrameOverlap(pParserBase, pBuffer2, nBufferSize2, nSegmentCnt);

    if (pParserBase->nOverlap < pParserBase->FileInfo.nFrameHeaderSize) {
        pParserBase->nOverlap = pParserBase->FileInfo.nFrameHeaderSize;
    } else if (pParserBase->nOverlap > AUDIO_PARSER_READ_SIZE >> 1) {
        PARSERMSG("Over lap audio data wrong.\n");
        pParserBase->nOverlap = AUDIO_PARSER_READ_SIZE >> 1;
    }

    memcpy(pBuffer - pParserBase->nOverlap, pBuffer + nBufferSize - pParserBase->nOverlap,
           pParserBase->nOverlap);

    return ret;
}

uint32 ParserAudioFrameOverlap(Audio_Parser_Base_t* pParserBase, uint8* pBuffer, uint32 nBufferSize,
                               uint32 nSegmentCnt) {
    uint32 ret = PARSER_SUCCESS;
    uint32 offset = 0;
    uint32 nFrameLen;
    AUDIO_FRAME_INFO* pFrameInfo = &(pParserBase->FrameInfo);

    while (offset < nBufferSize) {
        pParserBase->ParserFrame(pFrameInfo, pBuffer + offset, nBufferSize - offset);
        if (pFrameInfo->bGotOneFrame == FALSE) {
            pParserBase->nOverlap = nBufferSize - offset;
            break;
        }

        nFrameLen = pFrameInfo->nFrameHeaderConsumed + pFrameInfo->nFrameSize;

        if (pFrameInfo->nFrameCount == 1) {
            if (nSegmentCnt == PARSERAUDIO_HEAD || nSegmentCnt == PARSERAUDIO_BEGINPOINT) {
                pParserBase->nBeginPointOffset = offset +
                                                 pParserBase->FrameInfo.nFrameHeaderConsumed -
                                                 pParserBase->FileInfo.nFrameHeaderSize;
                pParserBase->nBeginPointOffset -= pParserBase->nOverlap;
                pParserBase->bBeginPointFounded = TRUE;
            }
            if (nSegmentCnt == PARSERAUDIO_BEGINPOINT) {
                break;
            }
        }
#if 1
        if (nSegmentCnt == PARSERAUDIO_VBRDURATION) {
            AudioParserBuildSeekTable(pParserBase,
                                      offset - pParserBase->nOverlap +
                                              pFrameInfo->nFrameHeaderConsumed -
                                              pParserBase->FileInfo.nFrameHeaderSize,
                                      pFrameInfo->nSamplesPerFrame, pFrameInfo->nSamplingRate);
        }
#endif
        offset += nFrameLen;

        pParserBase->nTotalBitRate += pFrameInfo->nBitRate * 1000;
    }

    return ret;
}

// Malloc a double-size buffer and return 2nd part to be used
//      1st part is used to store partial data, which left from last round frame boundary
//      recognization
uint8* AudioParserGetBuffer(Audio_Parser_Base_t* pParserBase, uint32 nBufferSize) {
    PARSERMSG("Audio parser malloc tmp buffer size: %d\n", nBufferSize * 2);

    if (NULL != pParserBase->pTmpBufferPtr) {
        pParserBase->memoryOps.Free(pParserBase->pTmpBufferPtr);
        pParserBase->pTmpBufferPtr = NULL;
    }
    pParserBase->pTmpBufferPtr = (uint8*)pParserBase->memoryOps.Malloc(nBufferSize * 2);
    if (pParserBase->pTmpBufferPtr == NULL) {
        PARSERMSG("Can't get memory.\n");
        return NULL;
    }

    pParserBase->nTotalBitRate = 0;
    pParserBase->FrameInfo.nFrameCount = 0;
    pParserBase->nOverlap = 0;

    return pParserBase->pTmpBufferPtr + nBufferSize;
}

uint32 AudioParserFreeBuffer(Audio_Parser_Base_t* pParserBase, uint8* pBuffer) {
    uint32 ret = PARSER_SUCCESS;

    if ((NULL != pParserBase->pTmpBufferPtr) && (pParserBase->pTmpBufferPtr <= pBuffer)) {
        pParserBase->memoryOps.Free(pParserBase->pTmpBufferPtr);
        pParserBase->pTmpBufferPtr = NULL;
    }

    return ret;
}

uint32 GetAvrageBitRate(Audio_Parser_Base_t* pParserBase) {
    if (pParserBase->FrameInfo.nFrameCount != 0) {
        pParserBase->nAvrageBitRate =
                (uint32)(pParserBase->nTotalBitRate / pParserBase->FrameInfo.nFrameCount);
        PARSERMSG("nAvrageBitRate: %d\n", pParserBase->nAvrageBitRate);
    } else {
        pParserBase->nAvrageBitRate = 0;
    }

    return pParserBase->nAvrageBitRate;
}

// Found the file offset of the first valid audio frame
//
int32 ParserFindBeginPoint(Audio_Parser_Base_t* pParserBase) {
    int32 ret = PARSER_SUCCESS;
    uint32 nReadLen = AUDIO_PARSER_READ_SIZE;
    uint32 nSegmentCnt = PARSERAUDIO_BEGINPOINT;
    int32 nActuralRead = 0;
    uint32 nReadPointTmp = 0;
    uint32 nDataSize = (uint32)(pParserBase->nEndPoint - pParserBase->nBeginPoint);
    uint8* pTmpBuffer;
    uint32 i;

    int32 tmp = pParserBase->fileOps.Seek(pParserBase->sourceFileHandle, pParserBase->nBeginPoint,
                                          SEEK_SET, pParserBase->appContext);
    if (tmp != 0) {
        return tmp;
    }

    pTmpBuffer = AudioParserGetBuffer(pParserBase, AUDIO_PARSER_READ_SIZE);
    if (pTmpBuffer == NULL) {
        PARSERMSG("Can't get memory.\n");
        return PARSER_INSUFFICIENT_MEMORY;
    }

    for (i = 0;; i++) {
        if (nReadPointTmp + nReadLen > nDataSize) {
            nReadLen = nDataSize - nReadPointTmp;
        }

        nActuralRead = pParserBase->fileOps.Read(pParserBase->sourceFileHandle, pTmpBuffer,
                                                 nReadLen, pParserBase->appContext);
        if (nActuralRead < 0) {
            ret = PARSER_ERR_UNKNOWN;
            break;
        }

        nReadPointTmp += nActuralRead;

        ParserAudioFrame(pParserBase, pTmpBuffer, nActuralRead, nSegmentCnt);

        if (nActuralRead < AUDIO_PARSER_READ_SIZE) {
            PARSERMSG("Audio file parser reach end.\n");
            break;
        }

        if (pParserBase->bBeginPointFounded == TRUE) {
            PARSERMSG("Audio file begin point found.\n");
            nReadPointTmp -= nActuralRead;
            pParserBase->nBeginPoint += (nReadPointTmp + pParserBase->nBeginPointOffset);
            break;
        }
    }

    AudioParserFreeBuffer(pParserBase, pTmpBuffer);
#if 0
    pParserBase->nBeginPoint += AUDIO_PARSER_SEGMENT_SIZE;
    pParserBase->nBeginPoint += pParserBase->nBeginPointOffset;
#endif
    PARSERMSG("Begin Point: %lld\n", pParserBase->nBeginPoint);

    return ret;
}

int32 AudioParserFileHeader(Audio_Parser_Base_t* pParserBase) {
    int32 ret = PARSER_SUCCESS;
    uint8* pTmpBuffer;
    uint32 nReadLen = AUDIO_PARSER_READ_SIZE;
    int32 nActuralRead;

    /* Parser file header */
    PARSERMSG("Begin Point: %lld\n", pParserBase->nBeginPoint);
    ret = pParserBase->fileOps.Seek(pParserBase->sourceFileHandle, pParserBase->nBeginPoint,
                                    SEEK_SET, pParserBase->appContext);
    if (ret != 0)
        return PARSER_ERR_UNKNOWN;
    pTmpBuffer = (uint8*)pParserBase->memoryOps.Malloc(AUDIO_PARSER_READ_SIZE);
    if (pTmpBuffer == NULL) {
        PARSERMSG("Can't get memory.\n");
        return PARSER_INSUFFICIENT_MEMORY;
    }

    if (nReadLen > pParserBase->nFileSize - pParserBase->nBeginPoint) {
        nReadLen = (uint32)(pParserBase->nFileSize - pParserBase->nBeginPoint);
    }

    nActuralRead = pParserBase->fileOps.Read(pParserBase->sourceFileHandle, pTmpBuffer, nReadLen,
                                             pParserBase->appContext);
    if (nActuralRead <= 0) {
        pParserBase->memoryOps.Free(pTmpBuffer);
        // MA-17070 StreamingMediaPlayerTest#testPlayMp3StreamNoLength
        // for NoLength Mp3 stream, read return -1 is ok for parser,
        // mark as nonseekable and don't return error
        if (0 == pParserBase->nFileSize) {
            pParserBase->nEndPoint = MAX_VALUE_UINT64;
            pParserBase->LiveFlag |= FILE_FLAG_NON_SEEKABLE;
            pParserBase->LiveFlag |= FILE_FLAG_READ_IN_SEQUENCE;
            return PARSER_SUCCESS;
        }
        return PARSER_ERR_UNKNOWN;
    }

    if (pParserBase->ParserFileHeader(&pParserBase->FileInfo, pTmpBuffer, nActuralRead) ==
        AUDIO_PARSERRETURNFAIL)
        return PARSER_ERR_INVALID_MEDIA;

    pParserBase->memoryOps.Free(pTmpBuffer);

    if (pParserBase->FileInfo.bSeekable == FALSE) {
        /** ADIF file will into this part code. */
        PARSERMSG("BitRate: %d\n", pParserBase->FileInfo.nBitRate);
        if (pParserBase->FileInfo.nBitRate != 0) {
            pParserBase->usDuration = (pParserBase->nFileSize << 3) /
                                      pParserBase->FileInfo.nBitRate * PARSER_TICKS_PER_SECOND;
        }
        goto Done;
    }

    if (pParserBase->FileInfo.bGotDuration == TRUE && pParserBase->FileInfo.bIsCBR == TRUE) {
        /** WAVE and FLAC file will into this part code. */
        pParserBase->bCBR = TRUE;
        pParserBase->usDuration = pParserBase->FileInfo.nDuration;
        pParserBase->nBeginPoint += pParserBase->FileInfo.nBeginPointOffset;
        if (pParserBase->FileInfo.ePCMMode != PCM_MODE_UNKNOW)
            pParserBase->nEndPoint = pParserBase->nBeginPoint + pParserBase->FileInfo.nBitStreamLen;
        goto Done;
    }

    if (pParserBase->FileInfo.bGotDuration == TRUE && pParserBase->FileInfo.bIsCBR == FALSE) {
        /** FLAC and MP3 with Xing header file will into this part code. */
        pParserBase->bCBR = FALSE;
        pParserBase->usDuration = pParserBase->FileInfo.nDuration;
        pParserBase->nBeginPoint += pParserBase->FileInfo.nBeginPointOffset;
        goto DoneDuration;
    }

    return ret;

DoneDuration:
    if (pParserBase->bCBR == FALSE) {
        pParserBase->fileOps.Seek(pParserBase->sourceFileHandle, pParserBase->nBeginPoint, SEEK_SET,
                                  pParserBase->appContext);
#if 0
        /** Calculate accurate duration and seek table in background thread */
        if(E_FSL_OSAL_SUCCESS != fsl_osal_thread_create(&pThreadId, NULL, \
            ParserCalculateVBRDurationFunc, this))
        {
            PARSERMSG("Create audio parser calculate duration thread failed.\n");
            return PARSER_INSUFFICIENT_MEMORY;
        }
#endif
    }

Done:
    pParserBase->fileOps.Seek(pParserBase->sourceFileHandle, pParserBase->nBeginPoint, SEEK_SET,
                              pParserBase->appContext);
    pParserBase->nReadPoint = pParserBase->nBeginPoint;

    return ret;
}

// Calculate the duration of the VBR audio content
//  1. Will parser the whole file to get duration
//  2. At the same time, build up the index table for seeking operation
int32 ParserCalculateVBRDuration(Audio_Parser_Base_t* pParserBase) {
    int32 ret = PARSER_SUCCESS;
    FslFileStream fileOps2;
    FslFileHandle sourceFileHandle2;
    ParserMemoryOps memoryOps;
    uint32 nReadLen = AUDIO_PARSER_READ_SIZE;
    uint32 nSegmentCnt = PARSERAUDIO_VBRDURATION;
    int32 nActuralRead = 0;
    uint32 nDataSize = (uint32)(pParserBase->nEndPoint - pParserBase->nBeginPoint);
    uint32 nReadPointTmp = 0;
    uint32 i;

    uint8* pTmpBuffer;

    if (pParserBase->bVBRDurationReady)
        return ret;

    if (pParserBase->nEndPoint == MAX_VALUE_UINT64) {
        pParserBase->usDuration = 0;
        pParserBase->bVBRDurationReady = TRUE;
        return ret;
    }

    pTmpBuffer = AudioParserGetBuffer(pParserBase, AUDIO_PARSER_READ_SIZE);
    if (pTmpBuffer == NULL) {
        PARSERMSG("Can't get memory.\n");
        return PARSER_INSUFFICIENT_MEMORY;
    }

    memcpy(&fileOps2, &pParserBase->fileOps, sizeof(FslFileStream));

    sourceFileHandle2 = pParserBase->sourceFileHandle;

    memoryOps = pParserBase->memoryOps;

    PARSERMSG("Begin point: %d\n", pParserBase->nBeginPoint);
    fileOps2.Seek(sourceFileHandle2, pParserBase->nBeginPoint, SEEK_SET, pParserBase->appContext);

    if (!pParserBase->hSeekTable)
        AudioIndexTableCreate(&(pParserBase->hSeekTable), &memoryOps);
    // Add the first audio frame offset as the 1st item to index table
    AudioIndexTableAddItem(pParserBase->hSeekTable, pParserBase->nBeginPoint);

    pParserBase->secctr = 1;
    pParserBase->minctr = 0;
    pParserBase->hourctr = 0;

    for (i = 0;; i++) {
        if (nReadPointTmp + nReadLen > nDataSize) {
            nReadLen = nDataSize - nReadPointTmp;
        }

        pParserBase->nSource2CurPos = fileOps2.Tell(sourceFileHandle2, pParserBase->appContext);
        nActuralRead =
                fileOps2.Read(sourceFileHandle2, pTmpBuffer, nReadLen, pParserBase->appContext);
        if (nActuralRead <= 0) {
            PARSERMSG("Audio file parser reach end.\n");
            break;
        }
        nReadPointTmp += nActuralRead;

        ParserAudioFrame(pParserBase, pTmpBuffer, nActuralRead, nSegmentCnt);

        if (nActuralRead < AUDIO_PARSER_READ_SIZE) {
            PARSERMSG("Audio file parser reach end.\n");
            break;
        }
    }

    // seek back to begin point
    fileOps2.Seek(sourceFileHandle2, pParserBase->nBeginPoint, SEEK_SET, pParserBase->appContext);
    if (pParserBase->nFileSize == 0) {
        pParserBase->nFileSize = nReadPointTmp;
        pParserBase->nEndPoint = pParserBase->nFileSize;
    }

    AudioParserFreeBuffer(pParserBase, pTmpBuffer);

    pParserBase->usDuration =
            ((pParserBase->hourctr * 60 + pParserBase->minctr) * 60 + (pParserBase->secctr - 1)) *
                    (uint64)PARSER_TICKS_PER_SECOND +
            (uint64)pParserBase->nOneSecondSample * PARSER_TICKS_PER_SECOND /
                    pParserBase->nSampleRate;

    PARSERMSG("Audio Duration: %lld\n", pParserBase->usDuration);
    pParserBase->bVBRDurationReady = TRUE;

    return ret;
}

int32 AudioParserBuildSeekTable(Audio_Parser_Base_t* pParserBase, int32 nOffset, uint32 nSamples,
                                uint32 nSamplingRate) {
    int32 ret = PARSER_SUCCESS;

    pParserBase->nOneSecondSample += nSamples;
    if (nSamplingRate)
        pParserBase->nSampleRate = nSamplingRate;

    if (pParserBase->nOneSecondSample >= nSamplingRate) {
        // Add new item to index table
        if (NULL != pParserBase->hSeekTable) {
            AudioIndexTableAddItem(pParserBase->hSeekTable,
                                   (uint64)(pParserBase->nSource2CurPos + nOffset));
        }
        pParserBase->nOneSecondSample -= nSamplingRate;
        pParserBase->secctr++;
        if (pParserBase->secctr == 60) {
            pParserBase->minctr++;
            pParserBase->secctr = 0;
        }

        if (pParserBase->minctr == 60) {
            pParserBase->hourctr++;
            pParserBase->minctr = 0;
        }

        if (pParserBase->hourctr >= MAXAUDIODURATIONHOUR) {
            PARSERMSG("Audio duration is biger than 1024 hour, can't build seek table.\n");
            return ret;
        }
    }

    return ret;
}

#define AUDIO_PARSER_OUTPUT_SIZE (16 * 1024)
#define MP3_HEADER_LEN (4)

static uint32 GetU32(const uint8* ptr) {
    return ptr[0] << 24 | ptr[1] << 16 | ptr[2] << 8 | ptr[3];
}

static bool resync(Audio_Parser_Base_t* pParserBase) {
    if(pParserBase->appContext == NULL)
        return FALSE;

    uint64 oriPos =
            pParserBase->fileOps.Tell(pParserBase->sourceFileHandle, pParserBase->appContext);
    uint64 syncPos = 0;
    uint32 maxCheckBytes = 128 * 1024;
    uint32 maxReadBytes = 4 * 1024;
    uint8 buf[maxReadBytes];
    uint32 headerWord = 0;
    uint32 totalBytesRead = 0;
    uint32 remainBytes = 0;
    uint32 frame_size, i;
    uint32 matchedFrames = 0, maxMatchedFrames = 3;
    int bitrate, num_samples, sample_rate, ret;
    int result;
    bool sync = FALSE;
    do {
        result = pParserBase->fileOps.Read(pParserBase->sourceFileHandle, buf + remainBytes,
                                           maxReadBytes - remainBytes, pParserBase->appContext);
        if (result <= 0)
            return FALSE;
        totalBytesRead += (uint32)result;
        remainBytes += (uint32)result;
        for (i = 0; i + 3 < remainBytes; i++) {
            headerWord = GetU32(buf + i);
            ret = pParserBase->GetFrameSize(headerWord, &frame_size, &sample_rate, NULL, &bitrate,
                                            &num_samples);
            if (ret == PARSER_SUCCESS) {
                syncPos = oriPos + totalBytesRead - remainBytes + i;
                ++matchedFrames;
                break;
            }
        }
        if (i == remainBytes)
            continue;

        // find a frame, need double check it
        remainBytes -= i;
        uint8* temp = buf + i;
        uint32 remainBytesTemp = remainBytes;
        do {
            if (remainBytesTemp >= frame_size + MP3_HEADER_LEN) {
                temp += frame_size;
                headerWord = GetU32(temp);
                ret = pParserBase->GetFrameSize(headerWord, &frame_size, &sample_rate, NULL,
                                                &bitrate, &num_samples);
                if (ret == PARSER_SUCCESS) {
                    remainBytesTemp -= frame_size;
                    if (++matchedFrames < maxMatchedFrames)
                        continue;
                    else {
                        sync = TRUE;
                        break;  // sync success
                    }
                } else {
                    ++i;
                    --remainBytes;
                }
            }
            memmove(buf, buf + i, remainBytes);
            matchedFrames = 0;
            break;
        } while (TRUE);

    } while (!sync && totalBytesRead <= maxCheckBytes);

    if (sync) {
        pParserBase->fileOps.Seek(pParserBase->sourceFileHandle, syncPos, SEEK_SET,
                                  pParserBase->appContext);
        return TRUE;
    } else
        return FALSE;
}

int32 GetNextSample(Audio_Parser_Base_t* pParserBase, ParserOutputBufferOps* pBufOps,
                    void* appContext, uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                    uint64* usStartTime, uint64* usDuration, uint32* sampleFlags) {
    int32 ret = PARSER_SUCCESS;
    int32 nActuralRead = 0;
    uint8 header[MP3_HEADER_LEN];
    uint32 headerWord;
    uint32 frame_size;
    int bitrate;
    int num_samples;
    int sample_rate;

    *sampleFlags = 0;
    memset(header, 0, sizeof(header) / sizeof(header[0]));

    if (pParserBase->nReadPoint + MP3_HEADER_LEN > pParserBase->nEndPoint) {
        PARSERMSG("Audio parser send EOS");
        return PARSER_EOS;
    }

    for (;;) {
        nActuralRead = pParserBase->fileOps.Read(pParserBase->sourceFileHandle, header,
                                                 MP3_HEADER_LEN, pParserBase->appContext);
        if (nActuralRead < MP3_HEADER_LEN) {
            return PARSER_EOS;
        }

        pParserBase->nReadPoint += nActuralRead;
        headerWord = GetU32(header);

        PARSERMSG("headerWord=0x%x read pos %lld end pos %lld nActuralRead %d", headerWord,
                  (long long)pParserBase->nReadPoint, (long long)pParserBase->nEndPoint,
                  nActuralRead);

        if (AUDIO_PARSERRETURNSUCESS == pParserBase->GetFrameSize(headerWord, &frame_size, &sample_rate, NULL,
                                                        &bitrate, &num_samples)) {
            PARSERMSG("frame_size %d sample_rate %d bitrate %d num_samples %d", frame_size,
                      sample_rate, bitrate, num_samples);
            break;
        }

        PARSERMSG("lost sync, headerWord=0x%x read pos %lld end pos %lld", headerWord,
                  (long long)pParserBase->nReadPoint, (long long)pParserBase->nEndPoint);
        if (!resync(pParserBase)) {
            PARSERMSG("can't resync until EOS");
            return PARSER_EOS;
        }
    }

    *dataSize = frame_size;
    PARSERMSG("Audio parser send length = %d\n", nActuralRead);

    *sampleBuffer = pBufOps->RequestBuffer(0, dataSize, bufferContext, appContext);
    if (!(*sampleBuffer))
        return PARSER_INSUFFICIENT_MEMORY;

    memcpy(*sampleBuffer, header, MP3_HEADER_LEN);
    nActuralRead =
            pParserBase->fileOps.Read(pParserBase->sourceFileHandle, *sampleBuffer + MP3_HEADER_LEN,
                                      frame_size - MP3_HEADER_LEN, pParserBase->appContext);
    if (nActuralRead <= 0) {
        pBufOps->ReleaseBuffer(0, *sampleBuffer, *bufferContext, appContext);
        return PARSER_EOS;
    } else if ((uint32)nActuralRead < frame_size - 4)
        *dataSize = nActuralRead + MP3_HEADER_LEN;

    if (pParserBase->bSegmentStart == TRUE) {
        pParserBase->bSegmentStart = FALSE;
        pParserBase->nSamplesRead = num_samples;
        pParserBase->nCurrentTimeUs = pParserBase->sAudioSeekPos;
        PARSERMSG("Audio parser ts = %lld\n", pParserBase->sAudioSeekPos);
        PARSERMSG("Audio begin data = 0x%08x \n", *((uint32*)sampleBuffer));

        *sampleFlags |= FLAG_SAMPLE_NEWSEG;
    } else {
        pParserBase->nSamplesRead += num_samples;
    }

    *usStartTime = pParserBase->nCurrentTimeUs;
    pParserBase->nReadPoint += nActuralRead;
    pParserBase->nCurrentTimeUs =
            pParserBase->sAudioSeekPos + ((pParserBase->nSamplesRead * 1000000) / sample_rate);
    PARSERMSG("nSamplesRead %d usStartTime %lld", pParserBase->nSamplesRead,
              (long long)*usStartTime);

    *usDuration = 1;
    *sampleFlags |= FLAG_SYNC_SAMPLE;
    return ret;
}

int32 Seek(Audio_Parser_Base_t* pParserBase, uint64* nSeekTime, uint32 flag) {
    int32 ret = PARSER_SUCCESS;
    int64 nSeekPoint;
    int64 sAudioSeekPos;

    sAudioSeekPos = *nSeekTime;

    // Disable seek if file size is 0 when http live.
    if (pParserBase->LiveFlag & FILE_FLAG_NON_SEEKABLE) {
        pParserBase->bSegmentStart = TRUE;
        pParserBase->sAudioSeekPos = sAudioSeekPos;
        return ret;
    }

    PARSERMSG("Seek Position: %lld\t Audio Duration: %lld\n", sAudioSeekPos,
              pParserBase->usDuration);
#if 1
    if (sAudioSeekPos > (int64)(pParserBase->usDuration)) {
        PARSERMSG("Seek time is bigger than audio duration.\n");
        if (SEEK_FLAG_NO_EARLIER == flag)
            return PARSER_EOS;
        else {
            sAudioSeekPos = (pParserBase->usDuration - 10) /
                            PARSER_TICKS_PER_SECOND;  // do not set as pParserBase->usDuration
            sAudioSeekPos *= PARSER_TICKS_PER_SECOND;
        }
    }
#endif
    PARSERMSG("bCBR = %d\t bVBRDurationReady = %d\n", pParserBase->bCBR,
              pParserBase->bVBRDurationReady);
    if (pParserBase->bCBR == TRUE || pParserBase->bVBRDurationReady == FALSE) {
        uint64 nSkip = 0;
        float f_nskip = (float)(pParserBase->nEndPoint - pParserBase->nBeginPoint) *
                        (float)sAudioSeekPos / (float)pParserBase->usDuration;
        nSkip = (uint64)f_nskip;
        nSeekPoint = pParserBase->nBeginPoint + (nSkip);
        PARSERMSG("Stream Len = %lld\n", (pParserBase->nEndPoint - pParserBase->nBeginPoint));
        PARSERMSG("After adjust Seek Position: %lld\t Audio Duration: %lld\n", sAudioSeekPos,
                  pParserBase->usDuration);
    } else if (pParserBase->bTOCSeek == TRUE) {
        float percent = (float)sAudioSeekPos * 100 / pParserBase->usDuration;
        float fx;
        if (percent <= 0.0f) {
            fx = 0.0f;
        } else if (percent >= 100.0f) {
            fx = 256.0f;
        } else {
            int32 a = (int32)percent;
            float fa, fb;
            if (a == 0) {
                fa = 0.0f;
            } else {
                fa = (float)pParserBase->FrameInfo.FrameInfo.TOC[a];
            }
            if (a < 99) {
                fb = (float)pParserBase->FrameInfo.FrameInfo.TOC[a + 1];
            } else {
                fb = 256.0f;
            }
            fx = fa + (fb - fa) * (percent - a);
        }
        nSeekPoint = pParserBase->nBeginPoint +
                     (int32)((1.0f / 256.0f) * fx * pParserBase->FrameInfo.FrameInfo.total_bytes);
        PARSERMSG("Seek Point = %lld\n", nSeekPoint);
    } else {
        uint32 nSec = (uint32)(sAudioSeekPos / PARSER_TICKS_PER_SECOND);
        // Lookup the seek table
        AudioIndexTableGetItem(pParserBase->hSeekTable, nSec, (uint64*)&nSeekPoint);
        PARSERMSG("Seek Point = %lld\n", nSeekPoint);
    }

    pParserBase->fileOps.Seek(pParserBase->sourceFileHandle, nSeekPoint, SEEK_SET,
                              pParserBase->appContext);
    pParserBase->nReadPoint = nSeekPoint;
    pParserBase->bSegmentStart = TRUE;
    pParserBase->sAudioSeekPos = sAudioSeekPos;
    *nSeekTime = sAudioSeekPos;

    return ret;
}

uint32 GetIndexTableSize(Audio_Parser_Base_t* pParserBase) {
    uint32 nSize = 0;

    if (!pParserBase->bCBR && pParserBase->bVBRDurationReady) {
        if (pParserBase->bTOCSeek) {
            // use TOC seek table
            nSize = 100 * sizeof(uint8);
        } else {
            // use SeekTable
            // query the seek table size
            AudioIndexTableExport(pParserBase->hSeekTable, NULL, &nSize);
        }
    }

    return nSize;
}

int32 ExportIndexTable(Audio_Parser_Base_t* pParserBase, uint8* pBuffer) {
    int32 ret = PARSER_SUCCESS;

    uint8* pBuf;
    IndexTableHdr_t* pTabHdr;

    (void)GetIndexTableSize(pParserBase);

    pBuf = pBuffer;
    pTabHdr = (IndexTableHdr_t*)pBuf;
    pTabHdr->nVersion = 0;
    pTabHdr->nIndexItemSize = sizeof(uint64);

    pTabHdr->nIndexItemTimeScale = 1000;
#if 1
    pTabHdr->nIndexCount =
            (60 * pParserBase->hourctr + pParserBase->minctr) * 60 + pParserBase->secctr;
#else
    pTabHdr->nIndexCount = (uint32)(pParserBase->usDuration / pTabHdr->nIndexItemTimeScale / 1000);
#endif
    pTabHdr->qwDuration = pParserBase->usDuration;
    if (pParserBase->bTOCSeek) {
        pTabHdr->nIndexItemSize = sizeof(uint8);
        pTabHdr->nIndexItemTimeScale = 0;  // for TOC
        pTabHdr->nIndexCount = 100;
    }
    pTabHdr++;
    pBuf = (uint8*)pTabHdr;
    // copy index table items here

    if (pParserBase->bTOCSeek) {
        // From TOC table
        memcpy(pBuf, pParserBase->FrameInfo.FrameInfo.TOC, 100 * sizeof(uint8));
    } else {
        // From SeekTable
        // Export from here
        uint32 Size = 0;
        AudioIndexTableExport(pParserBase->hSeekTable, pBuf, &Size);
    }

    return ret;
}

int32 ImportIndexTable(Audio_Parser_Base_t* pParserBase, uint8* pBuffer, uint32 size) {
    int32 ret = PARSER_SUCCESS;

    uint8* pBuf;
    IndexTableHdr_t* pTabHdr;
    uint32 nCopyBytes;

    if (!pBuffer || size <= sizeof(IndexTableHdr_t))
        return PARSER_ERR_INVALID_PARAMETER;

    // Check the Index Table header
    pTabHdr = (IndexTableHdr_t*)pBuffer;
    if (pTabHdr->nVersion != 0) {
        ;
    }

    if ((pTabHdr->nIndexItemSize == sizeof(uint8)) && (pTabHdr->nIndexItemTimeScale == 0)) {
        // TOC table
        nCopyBytes = pTabHdr->nIndexCount;
        if (nCopyBytes > 100) {
            nCopyBytes = 100;
        }
        nCopyBytes *= sizeof(uint8);

        pBuf = (uint8*)(pTabHdr + 1);
        memcpy(pParserBase->FrameInfo.FrameInfo.TOC, pBuf, nCopyBytes);
        pParserBase->bTOCSeek = TRUE;
        pParserBase->bVBRDurationReady = TRUE;
    }

    if ((pTabHdr->nIndexItemSize == sizeof(uint64)) && (pTabHdr->nIndexItemTimeScale == 1000)) {
        // Seek Table
        if (!pParserBase->hSeekTable) {
            AudioIndexTableCreate(&(pParserBase->hSeekTable), &pParserBase->memoryOps);
        }
        if (pParserBase->hSeekTable) {
            pBuf = (uint8*)(pTabHdr + 1);
            AudioIndexTableImport(pParserBase->hSeekTable, pBuf, (size - sizeof(IndexTableHdr_t)));
            pParserBase->bVBRDurationReady = TRUE;
        } else {
            ;
        }
    }

    pParserBase->usDuration = pTabHdr->qwDuration;

    return ret;
}

int32 mp3_parser_open(FslParserHandle* parserHandle, uint32 flags, FslFileStream* p_stream,
                      ParserMemoryOps* pMemOps, ParserOutputBufferOps* pOutputOps,
                      void* appContext) {
    int32 result = PARSER_SUCCESS;
    mp3_parser_t* pParserInstance = NULL;
    const uint8 flag[] = "rb";
    Parser_Input_Params_t inputParam;

    // create a new instance
    pParserInstance = (mp3_parser_t*)pMemOps->Malloc(sizeof(*pParserInstance));
    if (pParserInstance == NULL) {
        result = PARSER_INSUFFICIENT_MEMORY;
        goto bail;
    }

    memset(pParserInstance, 0, sizeof(*pParserInstance));

    memcpy(&(pParserInstance->fileStream), p_stream, sizeof(FslFileStream));
    memcpy(&(pParserInstance->memoryOps), pMemOps, sizeof(ParserMemoryOps));
    memcpy(&(pParserInstance->outputOps), pOutputOps, sizeof(ParserOutputBufferOps));

    if ((flags & FILE_FLAG_NON_SEEKABLE) && (flags & FILE_FLAG_READ_IN_SEQUENCE)) {
        pParserInstance->mp3_parser_core.LiveFlag |= FILE_FLAG_NON_SEEKABLE;
        pParserInstance->mp3_parser_core.LiveFlag |= FILE_FLAG_READ_IN_SEQUENCE;
    }

    if (flags & FLAG_ID3_FORMAT_NON_UTF8)
        pParserInstance->mp3_parser_core.bEnableConvert = FALSE;
    else
        pParserInstance->mp3_parser_core.bEnableConvert = TRUE;

    pParserInstance->fileHandle = p_stream->Open(NULL, flag, appContext); /* Open a file or URL */
    if (pParserInstance->fileHandle == NULL) {
        result = PARSER_FILE_OPEN_ERROR;
        goto bail;
    }

    pParserInstance->appContext = appContext;

    memset(&inputParam, 0, sizeof(Parser_Input_Params_t));
    memcpy(&(inputParam.fileOps), p_stream, sizeof(FslFileStream));
    memcpy(&(inputParam.memoryOps), pMemOps, sizeof(ParserMemoryOps));
    inputParam.sourceFileHandle = pParserInstance->fileHandle;
    inputParam.appContext = pParserInstance->appContext;
    inputParam.ParserFileHeader = Mp3ParserFileHeader;
    inputParam.ParserFrame = Mp3ParserFrame;
    inputParam.GetFrameSize = Mp3GetFrameSize;

    result = AudioParserBaseCreate(&(pParserInstance->mp3_parser_core), &inputParam);
    if (PARSER_SUCCESS != result) {
        pParserInstance->memoryOps.Free(pParserInstance);
        pParserInstance = NULL;
        PARSERMSG("AudioParserBaseInit failed as 0x%08x", result);
        goto bail;
    }

bail:
    *parserHandle = pParserInstance;

    return result;
}

int32 mp3_parser_close(FslParserHandle parserHandle) {
    mp3_parser_t* pParser = (mp3_parser_t*)parserHandle;
    FslFileStream* s = &(pParser->fileStream);

    if ((s->Close != NULL) && (NULL != pParser->fileHandle)) {
        s->Close(pParser->fileHandle, pParser->appContext);
        pParser->fileHandle = NULL;
    }

    // Free other buffers here
    AudioParserBaseDestroy(&(pParser->mp3_parser_core));

    // Clean up
    pParser->memoryOps.Free(pParser);
    pParser = NULL;

    return PARSER_SUCCESS;
}
