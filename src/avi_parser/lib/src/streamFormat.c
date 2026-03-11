
/*
 ***********************************************************************
 * Copyright (c) 2005-2015, Freescale Semiconductor, Inc.
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

/*
 * Sampling Frequency look up table
 * The look up index is found in the
 * header of an ADTS packet
 */
static const int32 AACSampleFreqTable[16] = {
        96000, /* 96000 Hz */
        88200, /* 88200 Hz */
        64000, /* 64000 Hz */
        48000, /* 48000 Hz */
        44100, /* 44100 Hz */
        32000, /* 32000 Hz */
        24000, /* 24000 Hz */
        22050, /* 22050 Hz */
        16000, /* 16000 Hz */
        12000, /* 12000 Hz */
        11025, /* 11025 Hz */
        8000,  /*  8000 Hz */
        7350,  /*  7350 Hz */
        -1,    /* future use */
        -1,    /* future use */
        -1     /* escape value */
};

static int32 parseH264DecoderSpecificInfo(uint8* decoderSpecificInfo,
                                          uint32* decoderSpecificInfoSize,
                                          uint32* NALLengthFieldSize, uint32 au_flag);

static void destroy(BaseAtomPtr s) {
    StreamFormatPtr self = (StreamFormatPtr)s;

    if (self->decoderSpecificInfo) {
        LOCALFree(self->decoderSpecificInfo);
        self->decoderSpecificInfo = NULL;
    }

    destroyBaseAtom(s);
}

int32 parseStreamFormat(StreamFormatPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                        void* appContext) {
    int32 err = PARSER_SUCCESS;
    StreamFormatPtr self = NULL;
    BitmapInfo* bitmapInfo;
    WaveFormatEx* waveFomatEx;
    uint32 fccType;
    uint32 bytesLeft;
    uint8* pBytePtr;
    uint32 au_flag;

    uint32 NALLengthFieldSize;

    self = LOCALCalloc(1, sizeof(StreamFormat));
    TESTMALLOC(self)
    COPY_ATOM(self, proto)
    PRINT_INHERITANCE
    self->destroy = destroy;
    self->waveformat_extensible = FALSE;

    au_flag = self->au_flag;

    /* read the whole 'strf' data as decoder specific info */
    self->decoderSpecificInfoSize = self->size - self->bytesRead;
    if (0 >= (int32)self->decoderSpecificInfoSize)
        BAILWITHERROR(AVI_ERR_UNKNOWN_STREAM_FORMAT)

    self->decoderSpecificInfo = LOCALMalloc(self->decoderSpecificInfoSize);
    TESTMALLOC(self->decoderSpecificInfo)
    GETBYTES(self->decoderSpecificInfo, self->decoderSpecificInfoSize);

    fccType = ((StreamHeaderPtr)(((StreamHeaderListPtr)(self->parent))->strh))
                      ->fccType; /* get tag from its peer 'strh' */
    pBytePtr = self->decoderSpecificInfo;
    if (VideoTag == fccType) /* video stream */
    {
        bitmapInfo = &self->bitmapInfo;

        bitmapInfo->size = read32At(pBytePtr);
        pBytePtr += 4;
        bitmapInfo->width = read32At(pBytePtr);
        pBytePtr += 4;
        bitmapInfo->height = read32At(pBytePtr);
        pBytePtr += 4;
        bitmapInfo->planes = read16At(pBytePtr);
        pBytePtr += 2;
        bitmapInfo->bitCount = read16At(pBytePtr);
        pBytePtr += 2;
        bitmapInfo->compression = read32At(pBytePtr);
        pBytePtr += 4;
        bitmapInfo->sizeImage = read32At(pBytePtr);
        pBytePtr += 4;
        bitmapInfo->xPelsPerMeter = read32At(pBytePtr);
        pBytePtr += 4;
        bitmapInfo->yPelsPerMeter = read32At(pBytePtr);
        pBytePtr += 4;
        bitmapInfo->clrUsed = read32At(pBytePtr);
        pBytePtr += 4;
        bitmapInfo->clrImportant = read32At(pBytePtr);
        pBytePtr += 4;

#if 1
        // Convert H.264 video's decoderSpecificInfo
        switch (bitmapInfo->compression) {
            case fourcc('h', '2', '6', '4'):
            case fourcc('H', '2', '6', '4'):
            case fourcc('a', 'v', 'c', '1'):
            case fourcc('A', 'V', 'C', '1'):
            case fourcc('x', '2', '6', '4'):
            case fourcc('X', '2', '6', '4'):
                AVIMSG("H.264 video\n");
                NALLengthFieldSize = self->h264NAL_HeaderSize = 0;
                parseH264DecoderSpecificInfo(self->decoderSpecificInfo,
                                             &self->decoderSpecificInfoSize, &NALLengthFieldSize,
                                             au_flag);
                self->h264NAL_HeaderSize = NALLengthFieldSize;
                AVIMSG("H264 Video NAL header size is %d \r", NALLengthFieldSize);
                break;
            default:
                AVIMSG("Non - H.264 video\n");
                break;
        }
#endif
#ifdef DEBUG_SHOW_ATOM_CONTENT
        AVIMSG("Bitmap info: size %d\n", bitmapInfo->size);
        AVIMSG("width: %d\n", bitmapInfo->width);
        AVIMSG("height: %d\n", bitmapInfo->height);
        AVIMSG("number of planes: %d\n", bitmapInfo->planes);
        AVIMSG("number of bits per pixel: %d\n", bitmapInfo->bitCount);
        AVIMSG("compression: ");
        PrintTag(bitmapInfo->compression);
        AVIMSG("image size: %d\n", bitmapInfo->sizeImage);
        AVIMSG("xPelsPerMeter: %d\n", bitmapInfo->xPelsPerMeter);
        AVIMSG("yPelsPerMeter: %d\n", bitmapInfo->yPelsPerMeter);
        AVIMSG("color used: %d\n", bitmapInfo->clrUsed);
        AVIMSG("important color: %d\n", bitmapInfo->clrImportant);
        AVIMSG("____________________________\n");
#endif

    }

    else if (AudioTag == fccType) /* audio stream */
    {
        waveFomatEx = &self->waveFomatEx;

        waveFomatEx->formatTag = read16At(pBytePtr);
        AVIMSG("WAVE format tag: 0x%x\n", waveFomatEx->formatTag);
        pBytePtr += 2;
        waveFomatEx->channels = read16At(pBytePtr);
        pBytePtr += 2;
        waveFomatEx->samplesPerSec = read32At(pBytePtr);
        pBytePtr += 4;
        waveFomatEx->avgBytesPerSec = read32At(pBytePtr);
        pBytePtr += 4;
        waveFomatEx->blockAlgn = read16At(pBytePtr);
        pBytePtr += 2;
        waveFomatEx->bitsPerSample = read16At(pBytePtr);
        pBytePtr += 2;

        if (18 <= self->size) /* for PCM audio, strf is only 16 bytes and no such a field ! */
        {
            waveFomatEx->extraSize = read16At(pBytePtr);
            pBytePtr += 2;
        }

        if (0 < self->waveFomatEx.extraSize) /* for non-mp3 audio types, it could be ZERO */
        {
            uint8 SampRateIdx;
            uint32 subformat_data1; /* the data1 of subformat GUID.*/
            /* typedef struct _GUID
            {
                uint32_t Data1;
                uint16_t Data2;
                uint16_t Data3;
                uint8_t  Data4[8];
            } GUID  // GUID_DEFINED */

            AVIMSG("Wave exta data size %d\n", self->waveFomatEx.extraSize);
            waveFomatEx->extraData = pBytePtr;
#if 1  // correct the AAC audio parameters
            switch (waveFomatEx->formatTag) {
                case WAVE_TAG_AAC:
                case WAVE_TAG_FAAD_AAC:
                    AVIMSG("AAC audio\n");
                    SampRateIdx = (pBytePtr[1] >> 7) & 1;
                    SampRateIdx = (((pBytePtr[0] << 5) | (SampRateIdx << 4)) >> 4) & 15;
                    if (12 > SampRateIdx)
                        waveFomatEx->samplesPerSec = AACSampleFreqTable[SampRateIdx];
                    break;
                case WAVE_FORMAT_EXTENSIBLE:
                    if (waveFomatEx->extraSize > 22) { /* At least 22 bytes 2byte bit/sample 4byte
                                                          ch_mask + 16byte subformat GUID */
                        self->waveformat_extensible = TRUE;
                        pBytePtr += 6; /* get the subformat */
                        subformat_data1 = read32At(pBytePtr);
                        switch (subformat_data1) {
                            case WAVE_TAG_AAC:
                                /* set the formatTag to AAC, since subfomat not pass into
                                 * getAudioCoecType() */
                                waveFomatEx->formatTag = WAVE_TAG_AAC;
                                break;
                            case WAVE_TAG_WMA1:
                                waveFomatEx->formatTag = WAVE_TAG_WMA1;
                                break;
                            case WAVE_TAG_WMA2:
                                waveFomatEx->formatTag = WAVE_TAG_WMA2;
                                break;
                            case WAVE_TAG_WMA3:
                                waveFomatEx->formatTag = WAVE_TAG_WMA3;
                                break;
                            case WAVE_TAG_WMALL:
                                waveFomatEx->formatTag = WAVE_TAG_WMALL;
                                break;
                            default:
                                AVIMSG("Unknown audio format\n");
                                break;
                        }
                        break;
                    }
                default:
                    AVIMSG("Unknown audio format\n");
                    break;
            }
#endif
        }

#if 0 /* no longer care the layer, & even for MP2, the extra data may not exist */
        if(WAVE_TAG_MP2 == waveFomatEx->formatTag && waveFomatEx->extraData)
        {
            uint16 layer;
#ifdef FSL_CPU_BIG_ENDIAN
            layer = waveFomatEx->extraData[0];
            layer = (layer << 8)| waveFomatEx->extraData[1];
#else
            layer = waveFomatEx->extraData[1];
            layer = (layer << 8)| waveFomatEx->extraData[0];
#endif
            waveFomatEx->layer = layer;
        }
        else
            waveFomatEx->layer = MPEG_LAYER3;
#endif

#ifdef DEBUG_SHOW_ATOM_CONTENT
        AVIMSG("WAVEFORMATEX (Wave form audio header):\n");
        AVIMSG("format tag: 0x%x (0x%x for MPEG Layer-2 & 0x%x for MPEG Layer-3)\n",
               waveFomatEx->formatTag, WAVE_TAG_MP2, WAVE_TAG_MP3);
        AVIMSG("Number of channels: %d\n", waveFomatEx->channels);
        AVIMSG("samples per second: %d\n", waveFomatEx->samplesPerSec);
        AVIMSG("average byte rate: %d:\n", waveFomatEx->avgBytesPerSec);
        AVIMSG("block alignment in bytes: %d\n", waveFomatEx->blockAlgn);
        AVIMSG("bits per sample: %d\n", waveFomatEx->bitsPerSample);
        AVIMSG("extra format data size: %d\n", waveFomatEx->extraSize);
        AVIMSG("layer: %d\n", waveFomatEx->layer);
        AVIMSG("____________________________\n");
#endif

    }

    else if (TextTag == fccType) /* Text stream*/
    {
        AVIMSG("fccType TextTag\n");
    } else /* others */
    {
        err = AVI_ERR_UNKNOWN_STREAM_FORMAT;
    }

    /* Warning: skip the left bytes, there can be padding bytes behind,
    sometimes we are step backward, 'strf' can be less than it shall be!*/
    bytesLeft = self->size - self->bytesRead;
    if (bytesLeft) {
        AVIMSG("skip %d bytes in 'strf' ******************\n", (int32)bytesLeft);
        SKIPBYTES_FORWARD(bytesLeft);
    }

bail:
    if (PARSER_SUCCESS == err)
        *outAtom = self;
    else
        self->destroy((BaseAtomPtr)self);
    return err;
}

/* parse the NAL length field size, SPS and PPS info,
wrap SPS &PPS in NALs and rewrite the data buffer.*/
#define NAL_START_CODE_SIZE 4
#define CODEC_SPECINFO_OFFSET BITMAPINFO_SIZE

/* if flag NO_CONVERT is set, just check validation and no need to add start code in the stream */
static int32 parseH264DecoderSpecificInfo(uint8* decoderSpecificInfo,
                                          uint32* decoderSpecificInfoSize,
                                          uint32* NALLengthFieldSize, uint32 au_flag) {
    int32 err = PARSER_SUCCESS;
    uint8* inputData = decoderSpecificInfo + BITMAPINFO_SIZE;
    uint32 inputDataSize = *decoderSpecificInfoSize - BITMAPINFO_SIZE;
    /* H264 video, wrap decoder info in NAL units. The parameter NAL length field size
    is always 2 bytes long, different from that of data NAL units (1, 2 or 4 bytes)*/
    uint32 i, j, k;
    uint32 info_size = 0; /* size of SPS&PPS in NALs */
    char* data = NULL;    /* temp buffer */
    uint8 lengthSizeMinusOne;
    uint8 numOfSequenceParameterSets;
    uint8 numOfPictureParameterSets;
    uint16 NALLength;

    /* start code to check for the beginning of the H264 object header.
     Might have to change for other streams if required

    aligned(8) class AVCDecoderConfigurationRecord {
    unsigned int(8) configurationVersion = 1;
    unsigned int(8) AVCProfileIndication;
    unsigned int(8) profile_compatibility;
    unsigned int(8) AVCLevelIndication;
    bit(6) reserved = '111111'b;
    unsigned int(2) lengthSizeMinusOne;
    bit(3) reserved = '111'b;
    unsigned int(5) numOfSequenceParameterSets;
    for (i=0; i< numOfSequenceParameterSets;  i++) {
    unsigned int(16) sequenceParameterSetLength ;
    bit(8*sequenceParameterSetLength) sequenceParameterSetNALUnit;
    }
    unsigned int(8) numOfPictureParameterSets;
    for (i=0; i< numOfPictureParameterSets;  i++) {
    unsigned int(16) pictureParameterSetLength;
    bit(8*pictureParameterSetLength) pictureParameterSetNALUnit;
    }
    }
    TODO: test more files to change the following code*/
    uint32 StartCode;

    if ((6 > inputDataSize) || (0x01 != inputData[0]) ||
        ((0xFC != (inputData[4] & 0xFC)) && (0x00 != (inputData[4] & 0xFC)))) {
        *decoderSpecificInfoSize = 0;
        *NALLengthFieldSize = 0;
        AVIMSG("Zero Length of decoderSpecificInfo \r");
        return 0;
    }

    /* only when NO_CONVERT flag is not set, need add start code in the stream */
    if (!(au_flag & FLAG_H264_NO_CONVERT)) {
        StartCode =
                (inputData[0] << 24) | (inputData[1] << 16) | (inputData[2] << 8) | inputData[3];
        if (0x00000001 == StartCode) {
            // H264 SPS/PPS is in desired format
            memmove(decoderSpecificInfo, inputData, inputDataSize);
            *decoderSpecificInfoSize = inputDataSize;
            return 0;
        }
    }
    lengthSizeMinusOne = inputData[4];
    lengthSizeMinusOne &= 0x03;

    if (!(au_flag & FLAG_H264_NO_CONVERT)) {
        *NALLengthFieldSize = NALLength =
                (uint32)lengthSizeMinusOne + 1; /* lengthSizeMinusOne = 0x11, 0b1111 1111 */
    }
    numOfSequenceParameterSets = inputData[5] & 0x1f;

    k = 6;
    for (i = 0; i < numOfSequenceParameterSets; i++) {
        if (k >= inputDataSize) {
            AVIMSG("Invalid Sequence parameter NAL length: %d\n", NALLength);
            err = PARSER_ERR_INVALID_MEDIA;
            *decoderSpecificInfoSize = 0;
            *NALLengthFieldSize = 0;
            goto bail;
        }
        NALLength = inputData[k];
        NALLength = (NALLength << 8) + inputData[k + 1];
        k += (NALLength + 2);
        info_size += (NALLength + NAL_START_CODE_SIZE);
    }
    numOfPictureParameterSets = inputData[k];
    k++;

    for (i = 0; i < numOfPictureParameterSets; i++) {
        if (k >= inputDataSize) {
            AVIMSG("Invalid picture parameter NAL length: %d\n", NALLength);
            err = PARSER_ERR_INVALID_MEDIA;
            *decoderSpecificInfoSize = 0;
            *NALLengthFieldSize = 0;
            goto bail;
        }
        NALLength = inputData[k];
        NALLength = (NALLength << 8) + inputData[k + 1];
        k += (NALLength + 2);
        info_size += (NALLength + NAL_START_CODE_SIZE);
    }

    /* if au_flag is set as NO_CONVERT, no need to add start code, just return*/
    if (au_flag & FLAG_H264_NO_CONVERT)
        return 0;

    /* wrap SPS + PPS into the temp buffer "data" */
    data = (char*)LOCALMalloc(info_size);
    if (NULL == data) {
        err = PARSER_INSUFFICIENT_MEMORY;
        *decoderSpecificInfoSize = 0;
        *NALLengthFieldSize = 0;
        goto bail;
    }

    k = 6;
    j = 0;
    for (i = 0; i < numOfSequenceParameterSets; i++) {
        NALLength = inputData[k];
        NALLength = (NALLength << 8) + inputData[k + 1];
        *(data + j) = 0;
        *(data + j + 1) = 0;
        *(data + j + 2) = 0;
        *(data + j + 3) = 1;
        j += 4;

        memcpy(data + j, inputData + k + 2, NALLength);
        k += (NALLength + 2);
        j += NALLength;
    }
    k++; /* number of picture parameter sets */
    for (i = 0; i < numOfPictureParameterSets; i++) {
        NALLength = inputData[k];
        NALLength = (NALLength << 8) + inputData[k + 1];
        *(data + j) = 0;
        *(data + j + 1) = 0;
        *(data + j + 2) = 0;
        *(data + j + 3) = 1;
        j += 4;

        memcpy(data + j, inputData + k + 2, NALLength);
        k += (NALLength + 2);
        j += NALLength;
    }

    /* write back to the original buffer */
    memcpy(decoderSpecificInfo, data, info_size);
    *decoderSpecificInfoSize = info_size;

bail:
    if (data)
        LOCALFree(data);
    return err;
}
