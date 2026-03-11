/*
***********************************************************************
* Copyright (c) 2015, Freescale Semiconductor, Inc.
*
* Copyright 2024, 2026 NXP
*
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include "latmParser.h"
#include "latmParserInternal.h"

int LatmGetValue(GetBitContext* gb) {
    int len;

    len = get_nbits(gb, 2);

    return get_nbits_long(gb, 8 * (len + 1));
}

int GetAudioObjectType(GetBitContext* gb) {
    int type = get_nbits(gb, 5);
    if (31 == type)
        type = get_nbits(gb, 6) + 32;
    return type;
}

int GetSampleRate(GetBitContext* gb, int* pIndex) {
    *pIndex = get_nbits(gb, 4);
    return *pIndex == 0x0f ? (int)get_nbits(gb, 24) : (int)AAC_LATM_SAMPLE_RATES[*pIndex];
}

void ParseChannelMap(AAC_LATM_PARSER* h, uint8 layoutMap[][3], int num, int type) {
    GetBitContext* gb = &h->gb;
    int i = 0;

    for (i = 0; i < num; i++) {
        if (CHANNEL_FRONT == type || CHANNEL_BACK == type || CHANNEL_BACK == type) {
            layoutMap[0][0] = get_bits1(gb);
        } else if (CHANNEL_CC == type) {
            skip_nbits(gb, 1);
            layoutMap[0][0] = BLOCK_CCE;
        } else if (CHANNEL_LFE == type) {
            layoutMap[0][0] = BLOCK_LFE;
        }
        layoutMap[0][1] = get_nbits(gb, 4);
        layoutMap[0][2] = type;
        layoutMap++;
    }
}

int ParseProgramConfigElement(AAC_LATM_PARSER* h, uint8 (*layoutMap)[3]) {
    GetBitContext* gb = &h->gb;
    int numFront, numSide, numBack, numLfe, numAssocData, numValidCc;
    int commentLen, tag;

    skip_nbits(gb, 4);  // elementInstanceTag
    skip_nbits(gb, 2);  // objectType
    skip_nbits(gb, 4);  // samplingIndex

    numFront = get_nbits(gb, 4);
    numSide = get_nbits(gb, 4);
    numBack = get_nbits(gb, 4);
    numLfe = get_nbits(gb, 2);
    numAssocData = get_nbits(gb, 3);
    numValidCc = get_nbits(gb, 4);

    if (get_nbits(gb, 1))       // monoMixdownPresent
        skip_nbits(gb, 4);      // monoMixdownElementNumber
    if (get_nbits(gb, 1))       // stereoMixdownPresent
        skip_nbits(gb, 4);      // stereoMixdownElementNumber
    if (get_nbits(gb, 1))       // matrixMixdownidxPresent
        skip_nbits(gb, 2 + 1);  // matrixMixdownidx + pseudoSurroundEnable

    ParseChannelMap(h, layoutMap, numFront, CHANNEL_FRONT);
    tag = numFront;
    ParseChannelMap(h, layoutMap + tag, numSide, CHANNEL_SIDE);
    tag += numSide;
    ParseChannelMap(h, layoutMap + tag, numBack, CHANNEL_BACK);
    tag += numBack;
    ParseChannelMap(h, layoutMap + tag, numLfe, CHANNEL_LFE);
    tag += numLfe;

    skip_bits_long(gb, numAssocData * 4);

    ParseChannelMap(h, layoutMap + tag, numValidCc, CHANNEL_CC);
    tag += numValidCc;

    align_get_bits(gb);

    commentLen = get_nbits(gb, 8) << 3;
    if (get_bits_left(gb) < commentLen)
        return -1;

    skip_bits_long(gb, commentLen);

    return tag;
}

int ParseGASpecificConfig(AAC_LATM_PARSER* h, int channelConfig) {
    GetBitContext* gb = &h->gb;
    uint8 layoutMap[64][3];
    int extensionFlag;
    int ret = 0;

    skip_nbits(gb, 1);  // frameLengthFlag

    if (get_bits1(gb))       // dependsOnCoreCoder
        skip_nbits(gb, 14);  // coreCoderDelay

    extensionFlag = get_bits1(gb);
    if (6 == h->sConfig.objectType || 7 == h->sConfig.objectType)
        skip_nbits(gb, 3);  // layerNr

    if (0 == channelConfig) {
        ret = ParseProgramConfigElement(h, layoutMap);
        if (ret < 0)
            return ret;
    }

    if (extensionFlag) {
        switch (h->sConfig.objectType) {
            case 22:
                skip_nbits(gb, 5 + 11);  // numOfSubFrame & layerLength
                break;
            case 17:
            case 19:
            case 20:
            case 23:
                skip_nbits(gb, 1);  // aacSectionDataResilienceFlag
                skip_nbits(gb, 1);  // aacScaleFactorDataResilienceFlag
                skip_nbits(gb, 1);  // aacSpectralDataResilienceFlag
                break;
            default:
                break;
        }
        skip_nbits(gb, 1);  // extensionFlag3
    }

    switch (h->sConfig.objectType) {
        case 17:
        case 19:
        case 20:
        case 23:
            if (get_nbits(gb, 2))
                return -1;
            break;
        default:
            break;
    }

    return 0;
}

int ParseLatmAudioSpecificConfig(AAC_LATM_PARSER* h, int ascLen) {
    int configStartBit, syncExtension, bitsConsumed;
    int configBitIndex;
    AAC_LATM_SPEC_CONFIG sConfig;
    GetBitContext* gb = &h->gb;
    int ret = 0;

    configStartBit = get_bits_count(gb);
    syncExtension = 0;

    if (ascLen) {
        syncExtension = 1;
        ascLen = AAC_LATM_MIN(ascLen, get_bits_left(gb));
    } else
        ascLen = get_bits_left(gb);

    if ((configStartBit % 8) || ascLen <= 0)
        return -1;

    memset(&sConfig, 0, sizeof(AAC_LATM_SPEC_CONFIG));
    sConfig.objectType = GetAudioObjectType(gb);
    sConfig.sampleRate = GetSampleRate(gb, &sConfig.samplingIndex);
    sConfig.channelConfig = get_nbits(gb, 4);

    if (sConfig.channelConfig < (int)AAC_LATM_ARRAY_ELEMS(AAC_LATM_CHANNELS))
        sConfig.channels = AAC_LATM_CHANNELS[sConfig.channelConfig];

    if ((sConfig.objectType == 29 && !(show_nbits(gb, 3) & 0x03 && !(show_nbits(gb, 9) & 0x3F))) ||
        sConfig.objectType == 5) {
        sConfig.extObjectType = 5;
        sConfig.extSampleRate = GetSampleRate(gb, &sConfig.extSamplingIndex);
        sConfig.objectType = GetAudioObjectType(gb);
        if (22 == sConfig.objectType)
            sConfig.extChannelConfig = get_nbits(gb, 4);
    } else {
        sConfig.extObjectType = 0;
        sConfig.extSampleRate = 0;
    }

    if (sConfig.objectType == 36) {
        skip_nbits(gb, 5);
        if (show_nbits_long(gb, 24) != AAC_LATM_MKBETAG('\0', 'A', 'L', 'S'))
            skip_bits_long(gb, 24);
    }

    if (sConfig.extObjectType != 5 && syncExtension) {
        while (get_bits_left(gb) > 15) {
            if (show_nbits(gb, 11) == AAC_LATM_SYNC_WORD) {  // sync extension
                skip_nbits(gb, 11);
                sConfig.extObjectType = GetAudioObjectType(gb);
                if (sConfig.extObjectType == 5 && (get_nbits(gb, 1)) == 1)
                    sConfig.extSampleRate = GetSampleRate(gb, &sConfig.extSamplingIndex);
                if (get_bits_left(gb) > 11 && get_nbits(gb, 11) == 0x548)
                    skip_nbits(gb, 1);
                break;
            } else
                skip_nbits(gb, 1);
        }
    }

    switch (sConfig.objectType) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 6:
        case 7:
        case 17:
        case 19:
        case 20:
        case 21:
        case 22:
        case 23:
            ret = ParseGASpecificConfig(h, sConfig.channelConfig);
            if (ret < 0)
                return ret;
            break;
        default:
            break;
    }

    memcpy(&h->sConfig, &sConfig, sizeof(AAC_LATM_SPEC_CONFIG));

    configBitIndex = get_bits_count(gb);
    bitsConsumed = configBitIndex - configStartBit;

    if (h->pCodecData) {
        memcpy(h->pCodecData, gb->buffer + configStartBit / 8, (bitsConsumed + 7) / 8);
        h->nCodecDataSize = (bitsConsumed + 7) / 8;
    }

    if (36 == sConfig.objectType) {
        skip_nbits(gb, 5);
        if (show_nbits_long(gb, 24) != AAC_LATM_MKBETAG('\0', 'A', 'L', 'S'))
            skip_bits_long(gb, 24);

        configBitIndex = get_bits_count(gb);
    }
    return bitsConsumed;
}

int ParseAudioMuxConfig(AAC_LATM_PARSER* h) {
    GetBitContext* gb = &(h->gb);
    int audioMuxVersion, audioMuxVersionA;
    int otherData;
    int ret = 0;

    audioMuxVersion = get_nbits(gb, 1);
    if (1 == audioMuxVersion)
        audioMuxVersionA = get_nbits(gb, 1);
    else
        audioMuxVersionA = 0;

    h->muxVersionA = audioMuxVersionA;

    if (!audioMuxVersionA) {
        if (1 == audioMuxVersion)
            LatmGetValue(gb);  // taraFullness

        skip_nbits(gb, 1);     // allStreamSameTimeFraming
        skip_nbits(gb, 6);     // numSubFrames
        if (get_nbits(gb, 4))  // numPrograms
            return -1;
        if (get_nbits(gb, 3))  // numLayer
            return -1;

        if (!audioMuxVersion) {
            ret = ParseLatmAudioSpecificConfig(h, 0);
            if (ret < 0)
                return -1;
        } else {
            int ascLen = LatmGetValue(gb);
            ret = ParseLatmAudioSpecificConfig(h, ascLen);
            if (ret < 0)
                return -1;
            ascLen -= ret;
            skip_bits_long(gb, ascLen);  // fill bits
        }

        h->frameLenType = get_nbits(gb, 3);
        switch (h->frameLenType) {
            case 0:
                skip_nbits(gb, 8);  // latmBufferFullness
                break;
            case 1:
                h->frameLen = get_nbits(gb, 9);
                break;
            case 3:
            case 4:
            case 5:
                skip_nbits(gb, 6);
                break;
            case 6:
            case 7:
                skip_nbits(gb, 1);
                break;
            default:
                break;
        }

        otherData = get_nbits(gb, 1);  // otherDataPresent
        if (otherData) {
            if (audioMuxVersion)
                LatmGetValue(gb);
            else {
                int esc;  // otherDataLenEsc
                do {
                    esc = get_nbits(gb, 1);
                    skip_nbits(gb, 8);  // otherDataLenTmp
                } while (esc);
            }
        }

        if (get_nbits(gb, 1))   // crcCheckPresent
            skip_nbits(gb, 8);  // crcCheckSum

        h->bGotConfig = 1;
    }
    return 0;
}

int ParsePayloadLengthInfo(AAC_LATM_PARSER* h) {
    GetBitContext* gb = &(h->gb);
    int tmp;
    int muxSlotLen = 0;

    if (0 == h->frameLenType) {
        do {
            tmp = get_nbits(gb, 8);
            muxSlotLen += tmp;
        } while (255 == tmp);
        return muxSlotLen;
    } else if (5 == h->frameLenType || 7 == h->frameLenType || 3 == h->frameLenType)
        skip_nbits(gb, 2);  // muxSlotLengthCoded
    else if (1 == h->frameLenType)
        return h->frameLen;

    return 0;
}

int ParseAudioMuxElement(AAC_LATM_PARSER* h) {
    int useSameMux;
    int ret = 0;
    GetBitContext* gb = &(h->gb);
    int muxSlotLen;

    if (NULL == gb)
        return -1;

    useSameMux = get_nbits(gb, 1);
    if (!useSameMux) {
        ret = ParseAudioMuxConfig(h);
        if (ret)
            return ret;
    } else if (0 == h->bGotConfig)
        return -1;

    if (0 == h->muxVersionA) {
        muxSlotLen = ParsePayloadLengthInfo(h);
        if ((muxSlotLen << 3) > get_bits_left(gb))
            return -1;
        h->payloadStartBitOffset = get_bits_count(gb);
        h->payloadStartOffset = (get_bits_count(gb) + 7) / 8;
        h->payloadLen = muxSlotLen;
    }

    return 0;
}

AacLatmParserRetCode ParseAacLatmAudioInfo(uint8* pData, uint32 size, AAC_LATM_AUDIO_INFO* pInfo)

{
    AAC_LATM_PARSER parser;
    AAC_LATM_PARSER* h = NULL;
    GetBitContext* gb;
    uint8* pBuf = pData;
    int bufSize = size;
    int muxLen;
    int ret = 0;

    memset(&parser, 0, sizeof(AAC_LATM_PARSER));
    h = &parser;

    h->pCodecData = pInfo->pCodecData;

    gb = &h->gb;
    if (init_get_bits(gb, pBuf, 8 * bufSize))
        return LATMPARSER_ERROR;
    ;

    // find sync word 0x2b7
    while (get_bits_count(gb) + 11 < 8 * bufSize) {
        if (show_nbits(gb, 11) == AAC_LATM_SYNC_WORD)
            break;
        else
            skip_nbits(gb, 8);
    }

    while (bufSize > AAC_LATM_SYNC_WORD_BYTES) {
        if (get_nbits(gb, 11) != AAC_LATM_SYNC_WORD)
            return LATMPARSER_ERROR;

        muxLen = get_nbits(gb, 13) + 3;
        if (muxLen > bufSize)
            return LATMPARSER_NEED_MORE_DATA;

        ret = ParseAudioMuxElement(h);
        if (ret) {
            pBuf += muxLen;
            bufSize -= muxLen;
            if (init_get_bits(gb, pBuf, 8 * bufSize))
                return LATMPARSER_ERROR;
            ;
        } else {
            pInfo->nSampleRate = h->sConfig.sampleRate;
            pInfo->nChannles = h->sConfig.channels;
            pInfo->nCodecDataSize = h->nCodecDataSize;
            return LATMPARSER_SUCCESS;
        }
    }
    return LATMPARSER_NEED_MORE_DATA;
}

AacLatmParserRetCode ParseAacLatmData(AacLatmParserHandle handle, uint8* pData, uint32 size,
                                      uint32* consumed) {
    AAC_LATM_PARSER* h = (AAC_LATM_PARSER*)handle;
    GetBitContext* gb = NULL;
    uint8* pBuf = pData;
    int bufSize = size;
    AacLatmParserRetCode err = LATMPARSER_SUCCESS;
    int ret = 0, syncWordOffset, muxLen;
    uint32 i = 0;

    if (NULL == h)
        return LATMPARSER_ERROR;

    if (NULL == h->pBuf)
        return LATMPARSER_NEED_BUFFER;

    gb = &h->gb;
    if (init_get_bits(gb, pBuf, 8 * bufSize))
        return LATMPARSER_ERROR;
    ;

    // find sync word 0x2b7
    while (get_bits_count(gb) + 11 < 8 * bufSize) {
        if (show_nbits(gb, 11) == AAC_LATM_SYNC_WORD)
            break;
        else
            skip_nbits(gb, 8);
    }

    if (get_bits_count(gb) + 11 >= 8 * bufSize) {
        *consumed = size;
        return err;
    }

    syncWordOffset = get_bits_count(gb) / 8;
    pBuf += syncWordOffset;
    bufSize -= syncWordOffset;
    *consumed = syncWordOffset;

    while (bufSize > AAC_LATM_SYNC_WORD_BYTES) {
        if (init_get_bits(&h->gb, pBuf, 8 * bufSize))
            return LATMPARSER_ERROR;
        ;
        if (get_nbits(gb, 11) != AAC_LATM_SYNC_WORD)
            return LATMPARSER_ERROR;

        muxLen = get_nbits(gb, 13) + 3;
        if (muxLen > bufSize) {
            // corrupted data
            *consumed += bufSize;
            return LATMPARSER_ERROR;
        }

        ret = ParseAudioMuxElement(h);
        *consumed += muxLen;

        if (0 == ret) {
            // copy raw data
            if (h->payloadStartBitOffset % 8) {
                for (i = 0; i < h->payloadLen; i++) h->pBuf[i + h->bufOffset] = get_nbits(gb, 8);
                h->bufOffset += h->payloadLen;
            } else
                memcpy(h->pBuf + h->bufOffset, pBuf + h->payloadStartOffset, h->payloadLen);

            h->payloadLen = 0;
            h->payloadStartOffset = 0;
            h->payloadStartBitOffset = 0;
            err = LATMPARSER_HAS_OUTPUT;
            return err;
        }
        pBuf += muxLen;
        bufSize -= muxLen;
    }

    return err;
}

AacLatmParserRetCode SetAacLatmBuffer(AacLatmParserHandle handle, FrameInfo* pFrame) {
    AAC_LATM_PARSER* h = (AAC_LATM_PARSER*)handle;

    if (NULL == h)
        return LATMPARSER_ERROR;

    h->pBuf = pFrame->buffer;
    h->bufSize = pFrame->alloc_size;
    h->bufOffset = pFrame->data_size;

    return LATMPARSER_SUCCESS;
}

AacLatmParserRetCode GetAacLatmBuffer(AacLatmParserHandle handle, FrameInfo* pFrame) {
    AAC_LATM_PARSER* h = (AAC_LATM_PARSER*)handle;

    if (NULL == h)
        return LATMPARSER_ERROR;

    if (h->pBuf && h->bufOffset > 0) {
        pFrame->buffer = h->pBuf;
        pFrame->data_size = h->bufOffset;
        pFrame->alloc_size = h->bufSize;

        h->pBuf = NULL;
        h->bufSize = 0;
        h->bufOffset = 0;
    }

    return LATMPARSER_SUCCESS;
}

AacLatmParserRetCode CreateAacLatmParser(AacLatmParserHandle* pHandle, ParserMemoryOps* pMemOps) {
    AAC_LATM_PARSER* parser = (AAC_LATM_PARSER*)pMemOps->Malloc(sizeof(AAC_LATM_PARSER));
    if (NULL == parser)
        return -1;
    memset(parser, 0, sizeof(AAC_LATM_PARSER));
    parser->memOps = pMemOps;
    *pHandle = (AacLatmParserHandle)parser;
    return 0;
}

AacLatmParserRetCode DeleteAacLatmParser(AacLatmParserHandle handle) {
    AAC_LATM_PARSER* parser = (AAC_LATM_PARSER*)handle;
    ParserMemoryOps* memOps;

    if (parser == NULL)
        return -1;

    memOps = parser->memOps;
    memOps->Free(parser);
    return 0;
}
