/*
 ***********************************************************************
 * Copyright (c) 2010-2013, Freescale Semiconductor, Inc.
 * Copyright 2017-2018, 2026 NXP
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

/* never includ <string.h> and check macro BIG_ENDIAN in src code. It can defined with -O2 flag but
 * not with -O0 flag */

void PrintTagSize(uint32 tag, uint32 size) {
    AVIMSG("tag=%c%c%c%c size=0x%x\n", (tag) & 0xff, ((tag) >> 8) & 0xff, ((tag) >> 16) & 0xff,
           ((tag) >> 24) & 0xff, size);
    (void)tag;
    (void)size;
}

void PrintTag(uint32 tag) {
    AVIMSG("%c%c%c%c \n", (tag) & 0xff, ((tag) >> 8) & 0xff, ((tag) >> 16) & 0xff,
           ((tag) >> 24) & 0xff);
    (void)tag;
}

int32 readData(AviInputStream* s, void* buffer, uint32 size, void* context) {
    int32 sizeRead;

    sizeRead = LocalFileRead(s, buffer, size, context);
    if ((int32)size != sizeRead)
        return PARSER_READ_ERROR;

    return PARSER_SUCCESS;
}

int32 read16(AviInputStream* s, uint16* outVal, void* context) {
    uint16 val;
    int32 sizeRead;

    sizeRead = LocalFileRead(s, &val, 2, context);
    if (2 != sizeRead)
        return PARSER_READ_ERROR;

#ifdef FSL_CPU_BIG_ENDIAN
    *outVal = ((val & 0X00FF) << 8) | ((val & 0XFF00) >> 8);
#else
    *outVal = val;
#endif

    return PARSER_SUCCESS;
}

int32 read32(AviInputStream* s, uint32* outVal, void* context) {
    uint32 val;
    int32 sizeRead;

    sizeRead = LocalFileRead(s, &val, 4, context);
    if (4 != sizeRead)
        return PARSER_READ_ERROR;

#ifdef FSL_CPU_BIG_ENDIAN
    *outVal = (val & 0X000000FF) << 24 | (val & 0X0000FF00) << 8 | (val & 0X00FF0000) >> 8 |
              (val & 0XFF000000) >> 24;

#else
    *outVal = val;
#endif

    return PARSER_SUCCESS;
}

int32 read64(AviInputStream* s, uint64* outVal, void* context) {
    uint64 val;
    int32 sizeRead;

    sizeRead = LocalFileRead(s, &val, 8, context);
    if (8 != sizeRead)
        return PARSER_READ_ERROR;

#ifdef FSL_CPU_BIG_ENDIAN
    *outVal = (val & 0X000000FF) << 56 | (val & 0X0000FF00) << 40 | (val & 0X00FF0000) << 24 |
              (val & 0XFF000000) << 8 | (val & 0X000000FF00000000ULL) >> 8 |
              (val & 0X0000FF0000000000ULL) >> 16 | (val & 0X00FF000000000000ULL) >> 40 |
              (val & 0XFF00000000000000ULL) >> 56;

#else
    *outVal = val;
#endif

    return PARSER_SUCCESS;
}

uint16 read16At(uint8* pBytePtr) {
    uint16 val;
    uint16 tmp;
    uint8* des = (uint8*)&tmp;

    des[0] = pBytePtr[0];
    des[1] = pBytePtr[1];

    val = tmp;

#ifdef FSL_CPU_BIG_ENDIAN
    val = ((tmp & 0X00FF) << 8) | ((tmp & 0XFF00) >> 8);
#endif

    return val;
}

uint32 read32At(uint8* pBytePtr) {
    uint32 val;
    uint32 tmp;
    uint8* des = (uint8*)&tmp;

    des[0] = pBytePtr[0];
    des[1] = pBytePtr[1];
    des[2] = pBytePtr[2];
    des[3] = pBytePtr[3];
    val = tmp;

#ifdef FSL_CPU_BIG_ENDIAN
    val = (tmp & 0X000000FF) << 24 | (tmp & 0X0000FF00) << 8 | (tmp & 0X00FF0000) >> 8 |
          (tmp & 0XFF000000) >> 24;
#endif

    return val;
}

// disable the function as nobody use it.
#if 0
uint64 read64At(uint8 * pBytePtr)
{
    uint64 val;
    uint64 tmp;
    uint8 *des = (uint8 *)&tmp;

    des[0] = pBytePtr[0];
    des[1] = pBytePtr[1];
    des[2] = pBytePtr[2];
    des[3] = pBytePtr[3];
    des[4] = pBytePtr[4];
    des[5] = pBytePtr[5];
    des[6] = pBytePtr[6];
    des[7] = pBytePtr[7];
    val = tmp;

#ifdef FSL_CPU_BIG_ENDIAN
    val = (tmp & 0X000000FF)<<56
                    | (tmp & 0X0000FF00)<<40
                    | (tmp & 0X00FF0000)<<24
                    | (tmp & 0XFF000000)<<8
                    | (tmp & 0X000000FF00000000ULL)>>8
                    | (tmp & 0X0000FF0000000000ULL)>>16
                    | (tmp & 0X00FF000000000000ULL)>>40
                    | (tmp & 0XFF00000000000000ULL)>>56;

#endif

    return val;
}
#endif

/* Directly using the "fccCompression" is better.
Divx3: the fccHandler is div4 (?) but fccCompression is DIV3
Divx4: both fccHandler & fccCompression are divx
Divx5, fccHandler is divx, fccCompression is DX50*/
void getVideoCodecType(uint32 fccHandler, uint32 fccCompression, uint32* decoderType,
                       uint32* decoderSubtype) {
    *decoderType = UNKNOWN_CODEC_TYPE;
    *decoderSubtype = UNKNOWN_CODEC_SUBTYPE;
    (void)fccHandler;

    switch (fccCompression) {
        case fourcc('d', 'x', '5', '0'):
        case fourcc('D', 'X', '5', '0'):
            AVIMSG("DIVX video version 5/6\n");
            *decoderType = VIDEO_DIVX;
            *decoderSubtype = VIDEO_DIVX5_6;
            break;

        case fourcc('d', 'i', 'v', '3'):
        case fourcc('D', 'I', 'V', '3'):
        case fourcc('D', 'V', 'X', '3'):
        case fourcc('d', 'v', 'x', '3'):
        case fourcc('d', 'i', 'v', '4'):
        case fourcc('D', 'I', 'V', '4'):
        case fourcc('D', 'I', 'V', '5'):
        case fourcc('d', 'i', 'v', '5'):
        case fourcc('D', 'I', 'V', '6'):
        case fourcc('d', 'i', 'v', '6'):
        case fourcc('M', 'P', 'G', '3'):
        case fourcc('m', 'p', 'g', '3'):
        case fourcc('c', 'o', 'l', '0'):
        case fourcc('C', 'O', 'L', '0'):
        case fourcc('c', 'o', 'l', '1'):
        case fourcc('C', 'O', 'L', '1'):
        case fourcc('A', 'P', '4', '1'):
            AVIMSG("DIVX video version 3\n");
            *decoderType = VIDEO_DIVX;
            *decoderSubtype = VIDEO_DIVX3;
            break;

        case fourcc('d', 'i', 'v', 'x'):
        case fourcc('D', 'I', 'V', 'X'):
            AVIMSG("DIVX video version 4\n");
            *decoderType = VIDEO_DIVX;
            *decoderSubtype = VIDEO_DIVX4;
            break;

        case fourcc('m', 'p', '4', 'v'):
        case fourcc('M', 'P', '4', 'V'):
        case fourcc('m', 'p', 'g', '4'):
        case fourcc('M', 'P', 'G', '4'):
        case fourcc('F', 'M', 'P', '4'):
        case fourcc('f', 'm', 'p', '4'):
            AVIMSG("MPEG-4 video\n");
            *decoderType = VIDEO_MPEG4;
            break;

        case fourcc('R', 'M', 'P', '4'):
            AVIMSG("MPEG-4 AS Profile video\n");
            *decoderType = VIDEO_MPEG4;
            *decoderSubtype = MPEG4_VIDEO_AS_PROFILE;
            break;

        case fourcc('M', 'P', '4', '2'): /* MPEG-4 video version 2 */
        case fourcc('m', 'p', '4', '2'):
            AVIMSG("MS MPEG-4 video version 2\n");
            *decoderType = VIDEO_MS_MPEG4;
            *decoderSubtype = VIDEO_MS_MPEG4_V2;
            break;

        case fourcc('M', 'P', '4', '3'): /* MPEG-4 video version 3 */
        case fourcc('m', 'p', '4', '3'):
            AVIMSG("MS MPEG-4 video version 3\n");
            *decoderType = VIDEO_MS_MPEG4;
            *decoderSubtype = VIDEO_MS_MPEG4_V3;
            break;

        case fourcc('M', 'P', 'E', 'G'):
        case fourcc('M', 'P', 'G', 'I'):
        case fourcc('m', 'p', 'g', '1'):
        case fourcc('M', 'P', 'G', '1'):
        case fourcc('P', 'I', 'M', '1'):
        case fourcc(0x01, 0x00, 0x00, 0x10):
            AVIMSG("MPEG-1/2 video");
            *decoderType = VIDEO_MPEG2;
            break;

        case fourcc('M', 'P', 'G', '2'):
        case fourcc('m', 'p', 'g', '2'):
        case fourcc('P', 'I', 'M', '2'):
        case fourcc('D', 'V', 'R', ' '):
        case fourcc(0x02, 0x00, 0x00, 0x10):
            AVIMSG("MPEG-2 video");
            *decoderType = VIDEO_MPEG2;
            break;

        case fourcc('h', '2', '6', '3'):
        case fourcc('H', '2', '6', '3'):
        case fourcc('i', '2', '6', '3'):
        case fourcc('U', '2', '6', '3'):
        case fourcc('v', 'i', 'v', '1'):
        case fourcc('T', '2', '6', '3'):
        case fourcc('s', '2', '6', '3'):
            AVIMSG("H.263 video\n");
            *decoderType = VIDEO_H263;
            break;

        case fourcc('h', '2', '6', '4'):
        case fourcc('H', '2', '6', '4'):
        case fourcc('a', 'v', 'c', '1'):
        case fourcc('A', 'V', 'C', '1'):
        case fourcc('x', '2', '6', '4'):
        case fourcc('X', '2', '6', '4'):
            AVIMSG("H.264 video\n");
            *decoderType = VIDEO_H264;
            break;

        case fourcc('H', 'M', '1', '0'):
        case fourcc('h', 'm', '1', '0'):
        case fourcc('h', 'e', 'v', 'c'):
        case fourcc('H', 'E', 'V', 'C'):
        case fourcc('h', 'e', 'v', '1'):
        case fourcc('H', 'E', 'V', '1'):
            AVIMSG("HEVC video\n");
            *decoderType = VIDEO_HEVC;
            break;

        case fourcc('m', 'j', 'p', 'g'):
        case fourcc('M', 'J', 'P', 'G'):
        case fourcc('A', 'V', 'R', 'n'):
        case fourcc('I', 'J', 'P', 'G'):
        case fourcc('i', 'j', 'p', 'g'):
        case fourcc('d', 'm', 'b', '1'):
        case fourcc('A', 'C', 'D', 'V'):
        case fourcc('Q', 'I', 'V', 'G'):
            AVIMSG("Motion JPEG video\n");
            *decoderType = VIDEO_MJPG;
            break;

        case fourcc('x', 'v', 'i', 'd'):
        case fourcc('X', 'V', 'I', 'D'):
            AVIMSG("XVID video\n");
            *decoderType = VIDEO_XVID;
            break;

        case fourcc('w', 'm', 'v', '1'):
        case fourcc('W', 'M', 'V', '1'):
            AVIMSG("WMV7 video\n");
            *decoderType = VIDEO_WMV;
            *decoderSubtype = VIDEO_WMV7;
            break;

        case fourcc('w', 'm', 'v', '2'):
        case fourcc('W', 'M', 'V', '2'):
            AVIMSG("WMV8 video\n");
            *decoderType = VIDEO_WMV;
            *decoderSubtype = VIDEO_WMV8;
            break;

        case fourcc('w', 'm', 'v', '3'):
        case fourcc('W', 'M', 'V', '3'):
            AVIMSG("WMV9 video\n");
            *decoderType = VIDEO_WMV;
            *decoderSubtype = VIDEO_WMV9;
            break;

        case fourcc('w', 'v', 'c', '1'):
        case fourcc('W', 'V', 'C', '1'):
            AVIMSG("WMV9A (VC-1) video\n");
            *decoderType = VIDEO_WMV;
            *decoderSubtype = VIDEO_WVC1;
            break;

        case fourcc('V', 'P', '8', '0'):
        case fourcc('v', 'p', '8', '0'):
            AVIMSG("VP8 video\n");
            *decoderType = VIDEO_ON2_VP;
            *decoderSubtype = VIDEO_VP8;
            break;

        case fourcc('V', 'P', '9', '0'):
        case fourcc('v', 'p', '9', '0'):
            AVIMSG("VP9 video\n");
            *decoderType = VIDEO_ON2_VP;
            *decoderSubtype = VIDEO_VP9;
            break;

        default:
            AVIMSG("Warning:!Unknown video compression fcc: ");
            PrintTag(fccCompression);
    }

    return;
}

void getAudioCodecType(uint32 fccHandler, uint16 formatTag, uint32 bitPerSample,
                       uint32* decoderType, uint32* decoderSubtype) {
    *decoderType = UNKNOWN_CODEC_TYPE;
    *decoderSubtype = UNKNOWN_CODEC_SUBTYPE;
    (void)fccHandler;

    /* audio codec type, strh->fccHandler is usually EMPTY */
    switch (formatTag) {
        case WAVE_TAG_MP3:
            AVIMSG("MPEG Layer-3 audio\n");
            *decoderType = AUDIO_MP3;
            break;

        case WAVE_TAG_MP2:
            AVIMSG("MPEG Layer-2 audio\n");
            *decoderType = AUDIO_MP3;
            break;

        case WAVE_TAG_AC3:
            AVIMSG("AC3 audio\n");
            *decoderType = AUDIO_AC3;
            break;

        case WAVE_TAG_AAC:
        case WAVE_TAG_FAAD_AAC:
            AVIMSG("AAC audio\n");
            *decoderType = AUDIO_AAC;
            break;

        case WAVE_TAG_WMA1:
            AVIMSG("WMA1 audio\n");
            *decoderType = AUDIO_WMA;
            *decoderSubtype = AUDIO_WMA1;
            break;

        case WAVE_TAG_WMA2:
            AVIMSG("WMA2 audio\n");
            *decoderType = AUDIO_WMA;
            *decoderSubtype = AUDIO_WMA2;
            break;

        case WAVE_TAG_WMA3:
            AVIMSG("WMA3 audio\n");
            *decoderType = AUDIO_WMA;
            *decoderSubtype = AUDIO_WMA3;
            break;

        case WAVE_TAG_WMALL:
            AVIMSG("WMALL audio\n");
            *decoderType = AUDIO_WMA;
            *decoderSubtype = AUDIO_WMALL;
            break;

        case WAVE_TAG_ADPCM_MS:
            AVIMSG("MS ADPCM audio\n");
            *decoderType = AUDIO_ADPCM;
            *decoderSubtype = AUDIO_ADPCM_MS;
            break;

        case WAVE_TAG_ADPCM_IMA_WAV:
            AVIMSG("IMA ADPCM audio\n");
            *decoderType = AUDIO_ADPCM;
            *decoderSubtype = AUDIO_IMA_ADPCM;
            break;

        case WAVE_TAG_PCM_MULAW:
            AVIMSG("PCM Mu-Law audio \n");
            *decoderType = AUDIO_PCM_MULAW;
            break;

        case WAVE_TAG_PCM_ALAW:
            AVIMSG("PCM A-Law audio \n");
            *decoderType = AUDIO_PCM_ALAW;
            break;

        case WAVE_TAG_VORBIS:
            AVIMSG("Vorbis audio\n");
            *decoderType = AUDIO_VORBIS;
            break;

        case WAVE_TAG_PCM:
            AVIMSG("PCM audio\n");
            *decoderType = AUDIO_PCM;
            switch (bitPerSample) {
                case 8:
                    *decoderSubtype = AUDIO_PCM_U8;
                    break;

                case 16:
                    *decoderSubtype = AUDIO_PCM_S16LE;
                    break;

                case 24:
                    *decoderSubtype = AUDIO_PCM_S24LE;
                    break;

                case 32:
                    *decoderSubtype = AUDIO_PCM_S32LE;
                    break;

                default:
                    AVIMSG("Invalid PCM bits per sample: %d\n", bitPerSample);
            }
            break;

        default:
            AVIMSG("Warning:!Unknown audio format tag: 0x%x\n", formatTag);
    }

    return;
}

/* NOT used now the divx language code is only used in CSET chunk.
Track's language is defined in strn */
void getLanguage(uint32 lanCode, uint8* threeCharCode) {
    threeCharCode[0] = 'u';
    threeCharCode[1] = 'n';
    threeCharCode[2] = 'd';
    switch (lanCode) /* maybe shall use fccCompression */
    {
        case NONE_LANGUAGE:
            AVIMSG("Lanuage is ignored\n");
            threeCharCode[0] = 'u';
            threeCharCode[1] = 'n';
            threeCharCode[2] = 'd';
            break;

        case ARABIC:
            AVIMSG("ARABIC\n");
            break;

        case BULGARIAN:
            AVIMSG("BULGARIAN\n");
            break;

        case CATALAN:
            AVIMSG("CATALAN\n");
            break;

        case CHINESE:
            AVIMSG("CHINESE\n");
            break;

        case CZECH:
            AVIMSG("CZECH\n");
            break;

        case DANISH:
            AVIMSG("DANISH\n");
            break;

        case GERMAN:
            AVIMSG("GERMAN\n");
            break;

        case GREEK:
            AVIMSG("GREEK\n");
            break;

        case ENGLISH:
            AVIMSG("ENGLISH\n");
            break;

        case SPANISH:
            AVIMSG("SPANISH\n");
            break;

        case FINNISH:
            AVIMSG("FINNISH\n");
            break;

        case FRENCH:
            AVIMSG("FRENCH\n");
            break;

        case HEBREW:
            AVIMSG("HEBREW\n");
            break;

        case HUNGARIAN:
            AVIMSG("HUNGARIAN\n");
            break;

        case ICELANDIC:
            AVIMSG("ICELANDIC\n");
            break;

        case ITALIAN:
            AVIMSG("ITALIAN\n");
            break;

        case JAPANESE:
            AVIMSG("JAPANESE\n");
            break;

        case KOREAN:
            AVIMSG("KOREAN\n");
            break;

        case DUTCH:
            AVIMSG("DUTCH\n");
            break;

        case NORWEGIAN:
            AVIMSG("NORWEGIAN\n");
            break;

        case POLISH:
            AVIMSG("POLISH\n");
            break;

        case PORTUGUESE:
            AVIMSG("PORTUGUESE\n");
            break;

        case RHAETO_ROMANIC:
            AVIMSG("RHAETO_ROMANIC\n");
            break;

        case ROMANIAN:
            AVIMSG("ROMANIAN\n");
            break;

        case RUSSIAN:
            AVIMSG("RUSSIAN\n");
            break;

        case SERBO_CROATIAN:
            AVIMSG("SERBO_CROATIAN\n");
            break;

        case SLOVAK:
            AVIMSG("SOLVAK\n");
            break;

        case ALBANIAN:
            AVIMSG("ALBANIAN\n");
            break;

        case SWEDISH:
            AVIMSG("SWEDISH\n");
            break;

        case THAI:
            AVIMSG("THAI\n");
            break;

        case TURKISH:
            AVIMSG("TURKISH\n");
            break;

        case URDU:
            AVIMSG("URDU\n");
            break;

        case BAHASA:
            AVIMSG("BAHASA\n");
            break;

        default:
            AVIMSG("Unknown lanuage\n ");
    }
    return;
}

/* align_size has to be a power of two !!
To gurantee the alignment >= 8 bytes
structure: <original ptr> , [possible padding bytes] , <4bytes: distance between aligned ptr &
original pointr>, <aligned ptr> */
void* alignedMalloc(uint32 size, uint32 alignSize) {
    char *ptr, *ptr2, *aligned_ptr;
    /* use unsigned long to make it work for both 32bit and 64bit architecture */
    unsigned long align_mask = (unsigned long)(alignSize - 1);

    ptr = (char*)LOCALMalloc(size + alignSize + sizeof(uint32));
    if (NULL == ptr)
        return (NULL);

    ptr2 = ptr + sizeof(uint32);
    aligned_ptr = (char*)(((unsigned long)(ptr2 + align_mask)) & (~align_mask));

    ptr2 = aligned_ptr - sizeof(uint32);
    *((uint32*)ptr2) = (uint32)(aligned_ptr - ptr);

    return (aligned_ptr);
}

void alignedFree(void* ptr) {
    uint32* ptr2 = (uint32*)ptr - 1; /* distance between aligned ptr and original ptr */
    char* originalPtr = (char*)ptr;
    originalPtr -= *ptr2;
    LOCALFree(originalPtr);
}

#ifdef OPEN_FILE_ONCE_PER_TRACK
AviInputStream* duplicateFileHandler(AviInputStream* src) {
    AviInputStream* des = NULL;

    des = (AviInputStream*)LOCALCalloc(1, sizeof(AviInputStream));
    if (des) {
        memcpy(des, src, sizeof(AviInputStream));
        des->fileHandle = NULL;
    }
    return des;
}
#endif

void disposeFileHandler(AviInputStream* inputStream, void* appContext) {
    if (!inputStream)
        return;

    if (inputStream->fileHandle)
        LocalFileClose(inputStream->fileHandle, appContext);

    LOCALFree(inputStream);
}
