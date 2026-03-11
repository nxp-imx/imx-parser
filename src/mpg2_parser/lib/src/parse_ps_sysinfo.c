/*
***********************************************************************
* Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
*
* Copyright 2017-2020, 2023-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
/*****************************************************************************
 * demux_ps_sysinfo.c
 *
 * Use of Freescale code is governed by terms and conditions
 * stated in the accompanying licensing statement.
 *
 * Description:
 * Parse Video/Audio header to get system information.
 *
 ****************************************************************************/

#include "parse_cfg.h"
#include "parse_ps_context.h"
#include "parse_ps_sc.h"

#ifdef DEMUX_DEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#endif

// #define MPG2_PARSER_SYSINFO_LOG	printf
#define MPG2_PARSER_SYSINFO_LOG(...)

#include "h264mp4info.h"

#define CONVERT_MPEGAUDIO_TYPE(x)                   \
    ((x) == 1 ? FSL_MPG_DEMUX_MP3_AUDIO             \
              : ((x) == 2 ? FSL_MPG_DEMUX_MP2_AUDIO \
                          : ((x) == 3 ? FSL_MPG_DEMUX_MP1_AUDIO : FSL_MPG_DEMUX_AAC_AUDIO)))

typedef struct {
    U8* data;
    U32 size;
    U32 reservoir;
    U32 bitsLeft;
    bool eos;
} BIT_READER;

BIT_READER* createBitReader(U8* data, U32 size) {
    BIT_READER* bitReader = (BIT_READER*)malloc(sizeof(BIT_READER));
    if (bitReader) {
        memset(bitReader, 0, sizeof(BIT_READER));
        bitReader->data = data;
        bitReader->size = size;
    }
    return bitReader;
}

void deleteBitReader(BIT_READER* pBitsReader) {
    if (pBitsReader)
        free(pBitsReader);
}

bool fillReservoir(BIT_READER* pBitsReader) {
    if (pBitsReader->size == 0) {
        pBitsReader->eos = TRUE;
        return FALSE;
    }

    pBitsReader->reservoir = 0;
    U32 i;
    for (i = 0; pBitsReader->size > 0 && i < 4; ++i) {
        pBitsReader->reservoir = (pBitsReader->reservoir << 8) | pBitsReader->data[0];

        ++(pBitsReader->data);
        --(pBitsReader->size);
    }

    pBitsReader->bitsLeft = 8 * i;
    pBitsReader->reservoir <<= 32 - pBitsReader->bitsLeft;
    return TRUE;
}

U32 readBits(BIT_READER* pBitsReader, U32 n) {
    U32 value = 0, m = 0;

    if (n > 32)
        return 0;
    while (n > 0) {
        if (pBitsReader->bitsLeft == 0) {
            if (!fillReservoir(pBitsReader)) {
                return 0;
            }
        }

        m = n;
        if (m > pBitsReader->bitsLeft) {
            m = pBitsReader->bitsLeft;
        }

        // Don't move forward if n is equal to 32, it violates rules.
        if (m == 32) {
            value = pBitsReader->reservoir;
            pBitsReader->reservoir = 0;
            pBitsReader->bitsLeft = 0;
        } else {
            value = (value << m) | (pBitsReader->reservoir >> (32 - m));
            pBitsReader->reservoir <<= m;
            pBitsReader->bitsLeft -= m;
        }

        n -= m;
    }

    return value;
}

void skipBits(BIT_READER* pBitsReader, U32 n) {
    while (n > 32) {
        readBits(pBitsReader, 32);
        n -= 32;
    }
    readBits(pBitsReader, n);
}

#define RETURN_ERR_IF_DATA_NOT_ENOUGH(bitsReader, n)            \
    do {                                                        \
        if (bitsReader->size * 8 + bitsReader->bitsLeft <= n) { \
            deleteBitReader(bitsReader);                        \
            return 1;                                           \
        }                                                       \
    } while (0)

/*******************Constans*********************  */

/*Video Frame Rate   */
static const U32 MPEG_FrameRate[8][2] = {{24000, 1001}, {24, 1}, {25, 1},       {30000, 1001},
                                         {30, 1},       {50, 1}, {60000, 1001}, {60, 1}};

U32 ParseMp4VideoInfo(FSL_MPG_DEMUX_CNXT_T* pCnxt, FSL_MPG_DEMUX_VSHeader_T* pVHeader, U8* pBuf,
                      U32 MaxLen) {
    U8 *pTemp, *pMaxTemp;
    U8* pBufTemp;
    U32 FourBytes = 0xFFFFFFFF;
    U32 SCFound = 0;
    U32 Size = 0;
    U32 VOS_offset = 0;
    U8 uiStartCode;
    MPG4_OBJECT MPG4Obj;
    MPG4_OBJECT* pMPG4Obj = &MPG4Obj;
    BUF_CONTEXT_T bufContext;

    MPG4_VOS_HEADER VosHdr;
    MPG4_VSO_HEADER VsoHdr;
    MPG4_VDO_HEADER VdoHdr;
    MPG4_VOL_HEADER VolHdr;
    MPG4_GOV_HEADER GovHdr;
    MPG4_VOP_HEADER VopHdr;

    memset(pMPG4Obj, 0, sizeof(MPG4_OBJECT));
    memset(&VosHdr, 0, sizeof(MPG4_VOS_HEADER));
    memset(&VsoHdr, 0, sizeof(MPG4_VSO_HEADER));
    memset(&VdoHdr, 0, sizeof(MPG4_VDO_HEADER));
    memset(&VolHdr, 0, sizeof(MPG4_VOL_HEADER));
    memset(&GovHdr, 0, sizeof(MPG4_GOV_HEADER));
    memset(&VopHdr, 0, sizeof(MPG4_VOP_HEADER));

    pMPG4Obj->psVosHdr = &VosHdr;
    pMPG4Obj->psVsoHdr = &VsoHdr;
    pMPG4Obj->psVdoHdr = &VdoHdr;
    pMPG4Obj->psVolHdr = &VolHdr;
    pMPG4Obj->psGovHdr = &GovHdr;
    pMPG4Obj->psVopHdr = &VopHdr;

    pTemp = pBuf;
    pMaxTemp = pBuf + MaxLen;

    while ((!SCFound) && (pTemp < pMaxTemp)) {
        FourBytes = (FourBytes << 8) | (*pTemp);
        if (IS_MP4_SH(FourBytes)) {
            SCFound = 1;
        }
        pTemp++;
        VOS_offset++;
    }
    if (pTemp >= pMaxTemp)
        return 1;

    // now we have found a H264 SPS NAL header
    Size = pTemp - pBuf;
    if (Size < 4)
        Size = 4;
    Size = Size - 4;

#if 1  // we need to remove vop header from vos info
    {
        // search vop start code for VOS boundary
        U32 SCVopFound = 0;
        U8* pCur = pTemp;
        FourBytes = 0xFFFFFFFF;
        while ((!SCVopFound) && (pCur < pMaxTemp)) {
            FourBytes = (FourBytes << 8) | (*pCur);
            if (IS_MP4_VOP(FourBytes)) {
                SCVopFound = 1;
            }
            pCur++;
        }
        pTemp = pTemp - 4;
        if (1 == SCVopFound) {
            // found vop start code, need to remove vop header from VOS info
            Size = (pCur - 4) - pTemp;
            MPG2_PARSER_SYSINFO_LOG("VOS size(removed vop): %d \r\n", Size);
        } else {
            // no vop start code
            Size = MaxLen - Size;
            MPG2_PARSER_SYSINFO_LOG("VOS size: %d \r\n", Size);
        }
    }
#else
    pTemp = pTemp - 4;
    Size = MaxLen - Size;
#endif

    if (Size > MAX_SEQHDRBUF_SIZE)
        Size = MAX_SEQHDRBUF_SIZE;

    pBufTemp = pCnxt->SeqHdrBuf.pSH;
    memcpy(pBufTemp, pTemp, Size);
    pCnxt->SeqHdrBuf.Size = Size;

    // look for VOP start code
    pTemp = pBuf + VOS_offset;
    uiStartCode = *(pTemp - 1);

    pMPG4Obj->pStreamReader = &bufContext;
    Buf_initContext(pMPG4Obj->pStreamReader, pTemp, (int)(pMaxTemp - pTemp));

    do {
        if (MPG4VP_fnParseVideoHeader(pMPG4Obj, uiStartCode))
            break;

        if (bufContext.bytesOffset >= bufContext.bufLen)
            return 1;

        if (pMPG4Obj->vopParsed)
            break;

        SCFound = 0;
        FourBytes = 0xFFFFFFFF;
        pTemp = (bufContext.pBuffer + bufContext.bytesOffset);
        if (bufContext.bitsOffset)
            pTemp++;

        while (pTemp < pMaxTemp && (SCFound != 1)) {
            FourBytes = (FourBytes << 8) | (*pTemp++);
            if (IS_MP4_PREFIX(FourBytes)) {
                SCFound = 1;
            }
            if (pTemp >= pMaxTemp)
                return 1;
        }
        uiStartCode = *pTemp++;
        Buf_initContext(pMPG4Obj->pStreamReader, pTemp, (int)(pMaxTemp - pTemp));
    } while ((pTemp <= pMaxTemp));

    if (pTemp >= pMaxTemp)
        return 1;

    if (pMPG4Obj->psVopHdr->iVopWidth > 0)
        pVHeader->HSize = pMPG4Obj->psVopHdr->iVopWidth;
    else
        pVHeader->HSize = 1920;

    if (pMPG4Obj->psVopHdr->iVopHeight > 0)
        pVHeader->VSize = pMPG4Obj->psVopHdr->iVopHeight;
    else
        pVHeader->VSize = 1080;

    pVHeader->VBVSize = 0;
    pVHeader->BitRate = 0;
    pVHeader->AR = 0;

    pVHeader->FRNumerator = 0;
    pVHeader->FRDenominator = 1;
    pVHeader->enuVideoType = FSL_MPG_DEMUX_MP4_VIDEO;

    return 0;
}

/*
Parser H.264 sequence header
*/
U32 ParseH264VideoInfo(ParserMemoryOps* memOps, FSL_MPG_DEMUX_VSHeader_T* pVHeader, U8* pBuf,
                       U32 MaxLen) {
    U32 err = 1;
    H264HeaderInfo h264Header;
    H264ParserHandle h264ParserHandle = NULL;

    if (CreateH264Parser(&h264ParserHandle, memOps, 0) != 0) {
        return err;
    }
    memset(&h264Header, 0, sizeof(H264HeaderInfo));
    if (ParseH264CodecDataFrame(h264ParserHandle, pBuf, MaxLen, &h264Header) == 0) {
        pVHeader->enuVideoType = FSL_MPG_DEMUX_H264_VIDEO;
        pVHeader->HSize = h264Header.width;
        pVHeader->VSize = h264Header.height;
        pVHeader->FRNumerator = h264Header.frameNumerator;
        pVHeader->FRDenominator = h264Header.frameDenominator;
        pVHeader->ScanType = h264Header.scanType;
        pVHeader->VBVSize = 0;
        pVHeader->BitRate = 0;
        pVHeader->AR = 0;
        err = 0;
    }

    DeleteH264Parser(h264ParserHandle);
    return err;
}

/*
Parse part of the MPEG sequence header to get system information
Input buffer context is not changed.
Assume all needed bits are available.
Return 0 if success.
----------------------------------------------------------------
          MPEG2 VIDEO Sequence Header Syntax (iec13818-2)
    syntax                              bit
sequence_header_code                    32
horizontal_size_value                   12
vertical_size_value                     12
aspect_ratio_information                4
frame_rate_code                         4
bit_rate_value                          18
market_bit                              1
vbv_buffer_size_value                   10
constrained_parameters_flag             1
load_intra_quantiser_matrix             1
if (load_intra_quantiser_matrix)
    intra_quantiser_matrix[64]          8*64
load_non_intra_quantiser_matrix         1
if (load_non_intra_quantiser_matrix)
    non_intra_quantiser_matrix[64]      8*64
----------------------------------------------------------------
*/
/*
Parse MPEG2(MPEG1) sequence header in a temp buffer
Return 0 if success
Return 1 if complete sequence header not found
*/
U32 ParseMPEG2VideoInfo(FSL_MPG_DEMUX_CNXT_T* pCnxt, FSL_MPG_DEMUX_VSHeader_T* pVHeader, U8* pBuf,
                        U32 MaxLen) {
    U8 FrameRateCode;
    U8* pTemp;
    U8* pSH;
    U8* pBufTemp;
    U32 Temp;
    U32 FourBytes = 0xFFFFFFFF;
    U32 SCFound = 0;
    U32 Len = 0;
    U32 LoadIntraMatrix = 0;
    U32 LoadInterMatrix = 0;

    pTemp = pBuf;
    while ((!SCFound) && (Len <= MaxLen)) {
        Len++;
        FourBytes = (FourBytes << 8) | (*pTemp);
        if ((IS_SC(FourBytes)) && (MPEG_SH == PS_ID(FourBytes))) {
            SCFound = 1;
        }
        pTemp++;
    }

    if ((Len + 8) > MaxLen)
        return 1;

    pSH = pTemp;

    FourBytes = (*(pTemp + 1)) | ((*pTemp) << 8);
    pVHeader->HSize = FourBytes >> 4;

    pTemp += 2;
    pVHeader->VSize = ((FourBytes & 0xF) << 8) | (*pTemp);
    pTemp++;

    pVHeader->AR = (U8)((*pTemp) >> 4);
    FrameRateCode = (U8)((*pTemp) & 0xF);
    pTemp++;

#ifdef DEMUX_DEBUG
    assert(FrameRateCode <= 16);
#endif

    /*set to default first   */
    pVHeader->FRNumerator = 30;
    pVHeader->FRDenominator = 1;

    if (FrameRateCode <= 8) {
        pVHeader->FRNumerator = MPEG_FrameRate[FrameRateCode - 1][0];
        pVHeader->FRDenominator = MPEG_FrameRate[FrameRateCode - 1][1];
    }

    FourBytes = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) | (*(pTemp + 3));
    pVHeader->BitRate = 400 * (FourBytes >> 14);
    pVHeader->VBVSize = (FourBytes >> 3) & 0x3FF;
    pVHeader->ConstrainedFlag = (FourBytes >> 2) & 0x1;
    pVHeader->enuVideoType = FSL_MPG_DEMUX_MPEG2_VIDEO;

    LoadIntraMatrix = (FourBytes >> 1) & 0x1;
    pTemp += 4;

    if (LoadIntraMatrix) {
        if ((Len + 8 + 64) > MaxLen)
            return 1;
        pTemp += 63;
        FourBytes = *pTemp;
        pTemp++;
    }

    LoadInterMatrix = FourBytes & 0x1;

    if (LoadInterMatrix) {
        if (LoadIntraMatrix) {
            if ((Len + 8 + 64 + 64) > MaxLen)
                return 1;
        } else {
            if ((Len + 8 + 64) > MaxLen)
                return 1;
        }
    }

    if ((0 == LoadIntraMatrix) && (0 == LoadInterMatrix))
        Temp = 8;
    else if ((1 == LoadIntraMatrix) && (1 == LoadInterMatrix))
        Temp = 8 + 64 + 64;
    else
        Temp = 8 + 64;

    /* check next start code */
    pTemp = pSH + Temp;
    FourBytes = (pTemp[0] << 24) | (pTemp[1] << 16) | (pTemp[2] << 8) | pTemp[3];
    if (!IS_SC(FourBytes))
        return 1;

    if (MPEG2_SEQUENCE_EXTENSION_HEADER == FourBytes) {
        int progressive_sequence = pTemp[5] & 0x8;
        if (!progressive_sequence)
            pVHeader->ScanType = VIDEO_SCAN_INTERLACED;
    }

    /*copy sequence header   */
    pBufTemp = pCnxt->SeqHdrBuf.pSH;
    *pBufTemp = 0x00;
    pBufTemp++;
    *pBufTemp = 0x00;
    pBufTemp++;
    *pBufTemp = 0x01;
    pBufTemp++;
    *pBufTemp = 0xB3;
    pBufTemp++;
    memcpy(pBufTemp, pSH, Temp);
    pCnxt->SeqHdrBuf.Size = 4 + Temp;
    // fill sequence extention to identify mpeg1 and mpeg2
    if (Len + Temp + 10 <= MaxLen) {
        // FIXME:
        //(1)don't consider stuff bytes between sequence header and sequence_extension header
        //(2)suppose buffer is enough to cover all header info
        if (FourBytes == MPEG2_SEQUENCE_EXTENSION_HEADER) {
            MPG2_PARSER_SYSINFO_LOG("fill sequence extension header for ts \r\n");
            memcpy(pCnxt->SeqHdrBuf.pSH + pCnxt->SeqHdrBuf.Size, pTemp, 10);
            pCnxt->SeqHdrBuf.Size += 10;
        }
    }

    return 0;
}

/*Some table for MPEG audio header    */

/* Bitrate table*/
static const U16 MPEG_AudioBitRate[16][6] = {
        /* bitrate in kbps
         * MPEG-1 layer 1, 2, 3; MPEG-2&2.5 layer 1,2,3
         */
        {0, 0, 0, 0, 0, 0},
        {32, 32, 32, 32, 8, 8},
        {64, 48, 40, 48, 16, 16},
        {96, 56, 48, 56, 24, 24},
        {128, 64, 56, 64, 32, 32},
        {160, 80, 64, 80, 40, 40},
        {192, 96, 80, 96, 48, 48},
        {224, 112, 96, 112, 56, 56},
        {256, 128, 112, 128, 64, 64},
        {288, 160, 128, 144, 80, 80},
        {320, 192, 160, 160, 96, 96},
        {352, 224, 192, 176, 112, 112},
        {384, 256, 224, 192, 128, 128},
        {416, 320, 256, 224, 144, 144},
        {448, 384, 320, 256, 160, 160},
        {999, 999, 999, 999, 999, 999} /* reserved */
};

/* sampling rate table */
static const U32 MPEG_AudioSampleRate[4][3] = {
        {44100, 22050, 11025}, {48000, 24000, 12000}, {32000, 16000, 8000}, {99999, 99999, 99999}};

/* sample per frame table */
static U32 MPEG_AudioSamplePerFrame[3][3] = {{384, 384, 84}, {1152, 1152, 1152}, {1152, 576, 576}};

static const U32 AAC_AudioSampleRate[16] = {96000, 88200, 64000, 48000, 44100, 32000, 24000,
                                            22050, 16000, 12000, 11025, 8000,  7350};

static const U8 AAC_ChannelsTable[8] = {0, 1, 2, 3, 4, 5, 6, 8};

#define MPEG_AHEADER_MASK 0xFFE00000
#define MPEG_AHEADER_CODE 0xFFE00000
#define AAC_SYNC_CODE 0xFFF
#define MP3_SYNC_CODE 0xFFE

#define IS_AHEADER(x) ((((x) & MPEG_AHEADER_MASK) == MPEG_AHEADER_CODE) ? 1 : 0)

static U32 ParseAacHeader(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, U32 len) {
    U32 profile;
    U32 sampling_frequency_index;
    U32 channel_index;
    U32 blocks_in_frame;
    U32 frame_len;
    U32 aac_byte;
    U32 temp;

    profile = (pBuf[2] >> 6);
    sampling_frequency_index = ((pBuf[2] & 0x3C) >> 2);
    channel_index = ((pBuf[2] & 0x01) << 2) | ((pBuf[3] & 0xC0) >> 6);
    temp = (pBuf[4] << 16) | (pBuf[5] << 8) | pBuf[6];

    blocks_in_frame = temp & 0x03;
    frame_len = ((pBuf[3] & 0x03) << 11) | ((temp >> 13) & 0x07FF);
    if (frame_len > len)
        return 1;  // exceed the buffer length
    aac_byte = ((pBuf[frame_len] & 0x00FF) << 4) + ((pBuf[frame_len + 1] & 0x00F0) >> 4);

    /* check the next aac frame sync byte*/
    if (0 == frame_len || aac_byte != AAC_SYNC_CODE)
        return 1;

    pAHeader->SampleRate = AAC_AudioSampleRate[sampling_frequency_index];
    pAHeader->Channels = AAC_ChannelsTable[channel_index];
    pAHeader->Layer = 0;
    pAHeader->ChannelMode = 0;
    pAHeader->BitRate = frame_len * 8 * pAHeader->SampleRate / (1024 * (blocks_in_frame + 1));

    if (pAHeader->pCodecData) {
        // AudioSpecificInfo follows

        // oooo offf fccc c000
        // o - audioObjectType
        // f - samplingFreqIndex
        // c - channelConfig

        pAHeader->pCodecData[0] = ((profile + 1) << 3) | (sampling_frequency_index >> 1);

        pAHeader->pCodecData[1] = ((sampling_frequency_index << 7) & 0x80) | (channel_index << 3);

        pAHeader->nCodecDataSize = 2;
    }

    return 0;
}

static U32 ParseMp3Header(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, U32 len) {
    U32 padding;
    U32 sample_per_frame;
    U32 mp3_byte;
    U32 frame_len;
    U32 temp, index;
    U32 version, layer;

    version = 3 - pAHeader->AudioVersion;
    if (version > 2)
        version = 2;

    layer = 4 - pAHeader->Layer;
    temp = (pBuf[2] & 0xF0) >> 4;
    if (0 == version)
        index = layer - 1;
    else
        index = layer + 3 - 1;

    pAHeader->BitRate = (U32)MPEG_AudioBitRate[temp][index];
    temp = (pBuf[2] & 0x0C) >> 2;
    pAHeader->SampleRate = MPEG_AudioSampleRate[temp][version];
    sample_per_frame = MPEG_AudioSamplePerFrame[layer - 1][version];
    padding = (pBuf[2] & 0x02) >> 1;

    if (pAHeader->SampleRate > 48000)
        return 1;

    if (1 == layer)
        frame_len = ((12 * pAHeader->BitRate * 1000) / pAHeader->SampleRate + padding) * 4;
    else
        frame_len = ((sample_per_frame / 8 * pAHeader->BitRate * 1000) / pAHeader->SampleRate) +
                    padding;

    if (frame_len > len)
        return 1;  // exceed the buffer length

    /* check the next mp3 frame sync byte*/
    mp3_byte = ((pBuf[frame_len] & 0x00FF) << 4) + ((pBuf[frame_len + 1] & 0x00E0) >> 4);
    if (0 == frame_len || mp3_byte != MP3_SYNC_CODE) {
        return 1;
    }

    temp = (pBuf[3] & 0xC0) >> 6;
    pAHeader->ChannelMode = (U8)temp;
    pAHeader->Channels = 2;
    if (FSL_MPEG_A_CH_MODE_SINGLECHANNEL == pAHeader->ChannelMode)
        pAHeader->Channels = 1;

    return 0;
}

/*
Parse part of the MPEG audio header to get system information
Input buffer context is not changed.
Assume all needed bits are available.
Return 0 if success.
*/
U32 ParseMPEGAudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, int MaxLen) {
    U8* pTemp;
    U32 FourBytes = 0x00000000;
    U32 SCFound = 0;
    int Len = 0;
    U32 frame_offset = 0;
    U32 ret = 0;

    pTemp = pBuf;

ReSync:
    while ((!SCFound) && (Len <= MaxLen)) {
        Len++;
        FourBytes = (FourBytes << 8) | (*pTemp);
        if (IS_AHEADER(FourBytes)) {
            SCFound = 1;
        }
        pTemp++;
    }

    if ((Len) > MaxLen) {
        return 1;
    }
    pAHeader->AudioVersion = (U8)((FourBytes >> 19) & 0x3);
    pAHeader->Layer = (U8)((FourBytes >> 17) & 0x3);
    frame_offset = Len - 4;

    if (0 == pAHeader->Layer)
        ret = ParseAacHeader(pAHeader, pBuf + frame_offset, MaxLen - frame_offset);
    else
        ret = ParseMp3Header(pAHeader, pBuf + frame_offset, MaxLen - frame_offset);

    if (ret > 0) {
        SCFound = 0;
        goto ReSync;
    }

    pAHeader->enuAudioType = CONVERT_MPEGAUDIO_TYPE(pAHeader->Layer);
    return 0;
}

/*
Parse system header
Return 0 if success.
*/
U32 ParseSystemHeader(FSL_MPG_DEMUX_SYSHDR_T* pSysHdr, U8* pInput, U32 Offset, int MaxLen) {
    U8* pLocalInput;
    U32 LocalOffset;
    int Len = 0;
    U32 Temp;
    U32 i = 0;

    pLocalInput = pInput;
    LocalOffset = Offset;

    if (MaxLen < 6)
        return 1;

    Temp = NextNBufferBytes(pLocalInput, 3, &LocalOffset);
    pSysHdr->RateBound = (Temp >> 1) & 0x3FFFFF;

    Temp = NextNBufferBytes(pLocalInput, 1, &LocalOffset);
    pSysHdr->AudioBound = Temp >> 2;

    Temp = NextNBufferBytes(pLocalInput, 1, &LocalOffset);
    pSysHdr->VideoBound = Temp & 0x1F;

    Temp = NextNBufferBytes(pLocalInput, 1, &LocalOffset);
    Len += (3 + 1 + 1 + 1);

    /*Do not trust the header if video_bound or audio_bound is zero    */
    // fix me?
    /*
    if (0==pSysHdr->AudioBound)
    pSysHdr->AudioBound=1;
    if (0==pSysHdr->VideoBound)
    pSysHdr->VideoBound=1;
    */
    i = 0;
    Temp = NextNBufferBytes(pLocalInput, 1, &LocalOffset);
    while (1 == (Temp >> 7) && Len < MaxLen) {
        pSysHdr->StreamID[i++] = Temp;
        Temp = NextNBufferBytes(pLocalInput, 2, &LocalOffset);
        Temp = NextNBufferBytes(pLocalInput, 1, &LocalOffset);
        Len += 3;
    }

    if (Len <= MaxLen) {
        pSysHdr->Found = 1;
        return 0;
    } else {
        return 1;
    }
}

#define AC3_SYNCWORD 0x0B77
#define AC3_HEADER_LENGTH 7
/*AC3 sampling rate table   */
static const U32 AC3_SamplingRate[4] = {48000, 44100, 32000, 99999};

static const U16 AC3_BitRate[] = {32,  40,  48,  56,  64,  80,  96,  112, 128, 160,
                                  192, 224, 256, 320, 384, 448, 512, 576, 640, 999};
/*AC3 number of channels  */
static const U8 AC3_Chans[8] = {2, 1, 2, 3, 3, 4, 4, 5};

/*AC3 frame size table  */
#define AC3_FRAME_SIZE_ENTRIES_NUM 38
#define AC3_SAMPLE_RATE_ENTRIES_NUM 3
static const U16 AC3_FrameSize[AC3_FRAME_SIZE_ENTRIES_NUM][AC3_SAMPLE_RATE_ENTRIES_NUM] = {
        {64, 69, 96},       {64, 70, 96},       {80, 87, 120},      {80, 88, 120},
        {96, 104, 144},     {96, 105, 144},     {112, 121, 168},    {112, 122, 168},
        {128, 139, 192},    {128, 140, 192},    {160, 174, 240},    {160, 175, 240},
        {192, 208, 288},    {192, 209, 288},    {224, 243, 336},    {224, 244, 336},
        {256, 278, 384},    {256, 279, 384},    {320, 348, 480},    {320, 349, 480},
        {384, 417, 576},    {384, 418, 576},    {448, 487, 672},    {448, 488, 672},
        {512, 557, 768},    {512, 558, 768},    {640, 696, 960},    {640, 697, 960},
        {768, 835, 1152},   {768, 836, 1152},   {896, 975, 1344},   {896, 976, 1344},
        {1024, 1114, 1536}, {1024, 1115, 1536}, {1152, 1253, 1728}, {1152, 1254, 1728},
        {1280, 1393, 1920}, {1280, 1394, 1920}};

/* E-AC3 blocks table */
static const U16 EAC3_Blocks[4] = {1, 2, 3, 6};

/*
Parse part of the AC3 audio header to get system information
Input buffer context is not changed.
Assume all needed bits are available.
Return 0 if success.
*/
U32 ParseAC3AudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, int MaxLen) {
    U8* pTemp;
    U32 Temp = 0;
    U32 fscod = 0;
    U32 frmsicod = 0;
    U32 bsid = 0;
    U32 channelmode = 0;
    U32 nfchans = 0;
    U32 syncLen = 0;
    U32 framesizebytes = 0;
    U8 lfeon = 0;  // low frequency effects channel

    if (MaxLen < 7)
        return 1;
    pTemp = pBuf;

    while (syncLen < (U32)MaxLen - 7) {
        Temp = ((*pTemp) << 8) | (*(pTemp + 1));
        if (Temp == AC3_SYNCWORD)
            break;

        // fix ENGR00223393
        pTemp++;
        syncLen++;
    }
    if (AC3_SYNCWORD != Temp)
        return 1;

    bsid = pTemp[5] >> 3;
    /* read ahead to bsid to distinguish between AC-3 and E-AC-3 */
    if ((bsid > 10) && (bsid <= 16))
        pAHeader->enuAudioType = FSL_MPG_DEMUX_EAC3_AUDIO;
    else if (bsid <= 8)
        pAHeader->enuAudioType = FSL_MPG_DEMUX_AC3_AUDIO;
    else {
        // invalid AC3 bsid
        return 1;
    }

    fscod = pTemp[4] >> 6;

    if (FSL_MPG_DEMUX_EAC3_AUDIO == pAHeader->enuAudioType) {
        // E-AC3
        U32 frametype = 0;
        U32 frmsize = 0;
        U32 numblocks = 6;

        frametype = pTemp[2] >> 6;
        if (frametype >= 3)
            return 1;

        // substreamid = (pTemp[2] >> 3) & 0x7;
        frmsize = ((pTemp[2] & 0x7) << 8) + pTemp[3];
        framesizebytes = (frmsize + 1) * sizeof(U16);

        if (framesizebytes < AC3_HEADER_LENGTH)
            return 1;

        if (3 == fscod) {
            U32 fscod2 = (pTemp[4] >> 4) & 0x3;
            if (fscod2 >= 3)
                return 1;
            pAHeader->SampleRate = AC3_SamplingRate[fscod2] / 2;
        } else {
            numblocks = EAC3_Blocks[(pTemp[4] >> 4) & 0x3];
            pAHeader->SampleRate = AC3_SamplingRate[fscod];
        }

        pAHeader->BitRate = 8 * framesizebytes * pAHeader->SampleRate / (numblocks * 256);

        channelmode = (pTemp[4] >> 1) & 0x7;
        lfeon = pTemp[4] & 0x1;
    } else {
        // AC3
        frmsicod = pTemp[4] & 0x3F;

        if (fscod >= AC3_SAMPLE_RATE_ENTRIES_NUM || frmsicod >= AC3_FRAME_SIZE_ENTRIES_NUM)
            return 1;  // invalid fscod or frmsicod

        pAHeader->SampleRate = AC3_SamplingRate[fscod];
        pAHeader->BitRate = AC3_BitRate[frmsicod >> 1] * 1000;
        framesizebytes = AC3_FrameSize[frmsicod][fscod] * sizeof(U16);

        channelmode = pTemp[6] >> 5;

        // ref Table 5.2 bsi Syntax and Word Size of a_52a.pdf
        if ((0 == channelmode) || (1 == channelmode)) {
            lfeon = (pTemp[6] >> 4) & 0x1;
        } else if (7 == channelmode) {
            lfeon = pTemp[6] & 0x1;
        } else {
            lfeon = (pTemp[6] >> 2) & 0x1;
        }
    }

    // check next sync header
    if (syncLen + framesizebytes != (U32)MaxLen) {
        if (syncLen + framesizebytes + 2 > (U32)MaxLen)
            return 1;
        else {
            U8* pNextSyncHeader = pTemp + framesizebytes;
            if (AC3_SYNCWORD != (pNextSyncHeader[0] << 8 | pNextSyncHeader[1]))
                return 1;
        }
    }

    nfchans = AC3_Chans[channelmode];
    pAHeader->Channels = nfchans + lfeon;

    /*simply set channel mode to stereo, right?  */
    pAHeader->ChannelMode = 0;
    return 0;
}

static U32 getBits(U8* pData, U32 bitsOffset, U32 len, U32 readBits) {
    U32 offset = bitsOffset / 8;
    U32 value;
    (void)len;

    bitsOffset %= 8;
    pData += offset;

    if (bitsOffset + readBits <= 8) {
        value = ((pData[0] & ((1 << (8 - bitsOffset)) - 1)) >> (8 - bitsOffset - readBits));
    } else {
        U32 value1, value2;
        U32 readBits1, readBits2;
        readBits1 = 8 - bitsOffset;
        readBits2 = readBits - readBits1;
        value1 = (pData[0] & ((1 << (8 - bitsOffset)) - 1));
        value2 = (pData[1] >> (8 - readBits2));
        value = value1 + value2;
    }

    return value;
}

/*
Parse part of the AC4 audio header to get system information
Assume all needed bits are available.
Return 0 if success.
*/
U32 ParseAC4AudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, int MaxLen) {
    // ETSI TS 103 190-2 V1.1.1 (2015-09), Annex C
    // The sync_word can be either 0xAC40 or 0xAC41.
    const int kSyncByteAC40 = 0xAC40;
    const int kSyncByteAC41 = 0xAC41;

    U32 syncByte;
    U32 frameSize;
    U32 version, fsIndex, samplingRate, waitFrames;
    U32 offset = 0, bitOffset = 0;
    U8* pTemp = pBuf;

    if (MaxLen < 4)
        return 1;

    syncByte = ((pTemp[0] << 8) | pTemp[1]);
    if (syncByte != kSyncByteAC40 && syncByte != kSyncByteAC41) {
        return 1;  // not sync
    }

    pTemp += 2;
    offset += 2;

    frameSize = ((pTemp[0] << 8) | pTemp[1]);
    pTemp += 2;
    offset += 2;

    if (0xffff == frameSize) {
        frameSize = ((pTemp[0] << 16) | (pTemp[1] << 8) | pTemp[2]);
        pTemp += 3;
        offset += 3;
    } else if (0 == frameSize) {
        return 1;  // invalid frame size
    }

    if (syncByte == kSyncByteAC41) {
        frameSize += 2;  // crc word
    }

    version = getBits(pTemp, bitOffset, MaxLen - offset, 2);
    bitOffset += 2;
    if (3 == version) {
        // readVariableBits
        int value = 0, moreBits = 1;
        while (moreBits) {
            value += getBits(pTemp, bitOffset, MaxLen - offset, 2);
            moreBits = getBits(pTemp, bitOffset + 2, MaxLen - offset, 1);
            bitOffset += 3;
            if (!moreBits)
                break;
            value++;
            value <<= 2;
        }
    }

    // skip sequence counter
    getBits(pTemp, bitOffset, MaxLen - offset, 10);
    bitOffset += 10;

    waitFrames = getBits(pTemp, bitOffset, MaxLen - offset, 1);
    bitOffset += 1;
    if (waitFrames) {
        if (getBits(pTemp, bitOffset, MaxLen - offset, 3) > 0) {
            getBits(pTemp, bitOffset, MaxLen - offset, 2);  // skip br_code
            bitOffset += 2;
        }
        bitOffset += 3;
    }

    fsIndex = getBits(pTemp, bitOffset, MaxLen - offset, 1);
    bitOffset += 1;
    samplingRate = fsIndex ? 48000 : 44100;

    memset(pAHeader, 0, sizeof(FSL_MPG_DEMUX_AHeader_T));
    pAHeader->SampleRate = samplingRate;
    pAHeader->Channels = 2;
    pAHeader->enuAudioType = FSL_MPG_DEMUX_AC4_AUDIO;
    return 0;
}

/*
number of frames: 8 bits
first access unit pointer: 16 bits
audio emphasis on-off: 1 bits
audio mute on-off: 1 bits
reserved: 1 bits
audio frame number: 5 bits
quantization word length: 2 bits
audio sampling frequency (48khz = 0, 96khz = 1): 2 bits
reserved: 1 bits
number of audio channels - 1 (e.g. stereo = 1): 3 bits
dynamic range control (0x80 if off): 8 bits
(total 6 bytes)

Parse part of the LPCM audio header to get system information
Input buffer context is not changed.
Assume all needed bits are available.
Return 0 if success.
*/
U32 ParseLPCMAudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pInput, U32 Offset, int MaxLen) {
    U8* pLocalInput;
    U32 LocalOffset;
    U32 Temp;
    int Len = 0;
    U32 SRCod = 0;
    U32 QCod = 0;
    U32 nfchans = 0;

    pLocalInput = pInput;
    LocalOffset = Offset;

    if (MaxLen < 3)
        return 1;
    /*skip frames count  */
    Temp = NextNBufferBytes(pLocalInput, 1, &LocalOffset);
    Len++;
    /*offset to first access unit in this PES  */
    Temp = NextNBufferBytes(pLocalInput, 2, &LocalOffset);
    Len += 2;
    if (Temp < 4)
        return 1;
    if ((Len + 3) > MaxLen)
        return 1;
    /*frame number,emphasis and mute  */
    Temp = NextNBufferBytes(pLocalInput, 1, &LocalOffset);
    /*channels, sampling rate and quantizaiton   */
    Temp = NextNBufferBytes(pLocalInput, 1, &LocalOffset);
    ;
    nfchans = (Temp & 0x7) + 1;
    SRCod = (Temp >> 4) & 0x3;
    QCod = (Temp >> 6) & 0x3;
    pAHeader->SampleRate = (SRCod == 0) ? 48000 : 96000;
    QCod = 4 * (4 + QCod);
    pAHeader->Channels = nfchans;
    pAHeader->BitRate = pAHeader->SampleRate * nfchans * QCod;
    pAHeader->ChannelMode = 0;
    pAHeader->BitsPerSample = QCod;
    pAHeader->enuAudioType = FSL_MPG_DEMUX_PCM_AUDIO;

    return 0;
}

#define DTS_SYNCWORD_CORE 0x7FFE8001
#define DTS_SYNCWORD_SUBSTREAM 0x64582025
#define DTSUHD_SYNC_CORE 0x40411BF2
#define DTSUHD_NONSYNC_CORE 0x71C442E8

static const U8 DTS_ChannelsTable[16] = {1, 2, 2, 2, 2, 3, 3, 4, 4, 5, 6, 6, 6, 7, 8, 8};

static const U32 DTS_SampleRateTableCore[16] = {
        0, 8000, 16000, 32000, 0, 0, 11025, 22050, 44100, 0, 0, 12000, 24000, 48000, 0, 0};

static const U32 DTS_SampleRateTableExt[16] = {8000,  16000, 32000,  64000,  128000, 22050,
                                               44100, 88200, 176400, 352800, 12000,  24000,
                                               48000, 96000, 192000, 384000};

static const U32 DTS_BitrateTable[32] = {
        32,  56,   64,   96,   112,  128,  192,  224,  256,  320,  384,  448,  512,  576, 640, 768,
        960, 1024, 1152, 1280, 1344, 1408, 1411, 1472, 1536, 1920, 2048, 3072, 3840, 0,   0,   0};

/*
Parse part of the DTS audio header to get system information
Input buffer context is not changed.
Assume all needed bits are available.
Return 0 if success.
*/
/*
format of DTS frame header:
Sync word ----32 bits
ftype----1 bit
deficit sample count----5 bits
CRC present flag----1 bit
PCM sample blocks----7 bits
fsize----14 bits
amode----6 bits
sfreq----4 bits
bit rate----5 bits
......

*/

/*
 * refer to ETSI TS 102 114 V1.6.1 (2019-08)
 * Parse part of the DTS audio header to get system information
 * Return 0 if success.
 */
U32 ParseDTSAudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, int MaxLen) {
    U32 syncword;
    U32 sampleRate = 0, channels = 0, bitrate = 0;
    FSL_MPG_DEMUX_AUDIO_TYPE_T audioType;

    BIT_READER* pBitReader = createBitReader(pBuf, MaxLen);
    if (NULL == pBitReader)
        return 1;

    // syncword 32
    RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 32);
    syncword = readBits(pBitReader, 32);

    // Table 5-1: Bit-stream header
    if (syncword == DTS_SYNCWORD_CORE) {
        U32 amode, sfreq, rate, lfe;

        // FTYPE 1, SHORT 5, CPF 1, NBLKS 7, FSIZE 14, AMODE 6, SFREQ 4, RATE 5, FixedBit 1,
        // DYBF 1, TIMEF 1, AUXF 1, HDCD 1, EXT_AUDIO_ID 3, EXT_AUDIO 1, ASPF 1, LFE 2
        // total size: 1+5+1+7+14+6+4+5+1+1+1+1+1+3+1+1+2 = 55
        RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 55);

        // skip FTYPE 1, SHORT 5, CPF 1, NBLKS 7, FSIZE 14
        skipBits(pBitReader, 1 + 5 + 1 + 7 + 14);

        amode = readBits(pBitReader, 6);
        sfreq = readBits(pBitReader, 4);
        rate = readBits(pBitReader, 5);

        // FixedBit 1,DYBF 1, TIMEF 1, AUXF 1, HDCD 1, EXT_AUDIO_ID 3, EXT_AUDIO 1, ASPF 1
        skipBits(pBitReader, 1 + 1 + 1 + 1 + 1 + 3 + 1 + 1);

        lfe = readBits(pBitReader, 2);
        channels = (amode <= 15) ? DTS_ChannelsTable[amode] : 0;
        channels += ((lfe == 1) || (lfe == 2)) ? 1 : 0;
        sampleRate = (sfreq <= 15) ? DTS_SampleRateTableCore[sfreq] : 0;
        bitrate = (rate <= 31) ? DTS_BitrateTable[rate] : 0;
        audioType = FSL_MPG_DEMUX_DTS_AUDIO;
    } else if (syncword == DTS_SYNCWORD_SUBSTREAM) {
        // Table 7-2: Extension Substream Header Structure
        U32 extSSIndex, headerSizeType;
        U32 nAuPr, nSS, nAst;
        U32 staticFieldsPresent;
        U32 activeExSSMask[8];
        U32 numAudioPresnt = 1, numAssets = 1;

        // UserDefaultBits 8, ExtSSIndex 2, HeaderSizeType 1
        RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 11);

        skipBits(pBitReader, 8);
        extSSIndex = readBits(pBitReader, 2);
        headerSizeType = readBits(pBitReader, 1);

        if (0 == headerSizeType) {
            // exHeadersize 8, exSSFsize 16
            RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 8 + 16);
            skipBits(pBitReader, 8 + 16);
        } else {
            // // exHeadersize 12, exSSFsize 20
            RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 12 + 20);
            skipBits(pBitReader, 12 + 20);
        }

        // StaticFieldPresent 1
        RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 1);
        staticFieldsPresent = readBits(pBitReader, 1);
        if (staticFieldsPresent) {
            // RefClockCode 2, ExSSFrameDurationCode 3, TimeStampFlag 1
            RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 2 + 3 + 1);

            skipBits(pBitReader, 2 + 3);
            if (readBits(pBitReader, 1)) {
                // TimeStamp 32, LSB 4
                skipBits(pBitReader, 32 + 4);
            }

            // NumAudioPresnt 3, NumAssets 3
            RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 3 + 3);
            skipBits(pBitReader, 3 + 3);
            for (nAuPr = 0; nAuPr < numAudioPresnt; nAuPr++) {
                RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, extSSIndex + 1);
                activeExSSMask[nAuPr] = readBits(pBitReader, extSSIndex + 1);
            }
            for (nAuPr = 0; nAuPr < numAudioPresnt; nAuPr++) {
                for (nSS = 0; nSS < extSSIndex + 1; nSS++) {
                    if (((activeExSSMask[nAuPr] >> nSS) & 0x1) == 1) {
                        RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 8);
                        // ActiveAssetMask
                        skipBits(pBitReader, 8);
                    }
                }
            }

            // MixMetadataEnbl 1
            RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 1);
            if (readBits(pBitReader, 1)) {
                U32 Bits4MixOutMask, NumMixOutConfigs, ns;

                // MixMetadataAdjLevel 2, Bits4MixOutMask 2, NumMixOutConfigs 2
                RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 2 + 2 + 2);

                skipBits(pBitReader, 2);
                Bits4MixOutMask = (readBits(pBitReader, 2) + 1) << 2;
                NumMixOutConfigs = readBits(pBitReader, 2) + 1;
                for (ns = 0; ns < NumMixOutConfigs; ns++) {
                    RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, Bits4MixOutMask);
                    // nuMixOutChMask
                    skipBits(pBitReader, Bits4MixOutMask);
                }
            }
        }

        for (nAst = 0; nAst < numAssets; nAst++) {
            U32 bits4ExSSFsize = (headerSizeType == 0) ? 16 : 20;
            RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, bits4ExSSFsize);
            // nuAssetFsize
            skipBits(pBitReader, bits4ExSSFsize);
        }

        // Table 7-5: Audio Asset Descriptor Syntax
        for (nAst = 0; nAst < numAssets; nAst++) {
            // nuAssetDescriptFsize 9, nuAssetIndex 3
            RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 9 + 3);
            skipBits(pBitReader, 9 + 3);
            if (staticFieldsPresent) {
                // bAssetTypeDescrPresent 1
                RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 1);
                if (readBits(pBitReader, 1)) {
                    // nuAssetTypeDescriptor 4
                    RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 4);
                    skipBits(pBitReader, 4);
                }
                // bLanguageDescrPresent 1
                RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 1);
                if (readBits(pBitReader, 1)) {
                    // LanguageDescriptor 24
                    RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 24);
                    skipBits(pBitReader, 24);
                }
                // bInfoTextPresent 1
                RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 1);
                if (readBits(pBitReader, 1)) {
                    U32 infoTextByteSize;
                    // bInfoTextPresent 10
                    RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 10);
                    infoTextByteSize = readBits(pBitReader, 10) + 1;
                    // InfoTextString nuInfoTextByteSize * 8
                    RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, infoTextByteSize * 8);
                    skipBits(pBitReader, infoTextByteSize * 8);
                }

                // nuBitResolution 5, nuMaxSampleRate 4, nuTotalNumChs 8
                RETURN_ERR_IF_DATA_NOT_ENOUGH(pBitReader, 5 + 4 + 8);
                skipBits(pBitReader, 5);
                sampleRate = DTS_SampleRateTableExt[readBits(pBitReader, 4)];
                channels = readBits(pBitReader, 8) + 1;
            }
        }
        audioType = FSL_MPG_DEMUX_DTS_HD_AUDIO;
    } else {
        if (syncword == DTSUHD_SYNC_CORE || syncword == DTSUHD_NONSYNC_CORE) {
            sampleRate = 48000;
            channels = 2;
            audioType = FSL_MPG_DEMUX_DTS_UHD_AUDIO;
        } else {
            deleteBitReader(pBitReader);
            return 1;
        }
    }

    deleteBitReader(pBitReader);

    pAHeader->SampleRate = sampleRate;
    pAHeader->Channels = channels;
    pAHeader->BitRate = bitrate;
    pAHeader->enuAudioType = audioType;
    return 0;
}

#define AAC_SYNCWORD 0xFFF

/*
Parse part of the AAC audio header to get system information
Assume all needed bits are available.
Return 0 if success.
*/
U32 ParseAACAudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, U32 MaxLen) {
    U8* pTemp;
    U32 Temp = 0;
    U32 profile = 0;
    U32 sample_frequency_index = 0;
    U32 channel_configuration = 0;
    U32 aac_frame_length = 0;
    U32 number_of_raw_data_blocks_in_frame = 0;
    U32 samples = 0;
    U32 syncLen = 0;

    if (MaxLen < 7)
        return 1;
    pTemp = pBuf;

    while (syncLen <= (U32)MaxLen - 7) {
        Temp = ((*pTemp) << 8) | (*(pTemp + 1));
        pTemp += 2;
        syncLen += 2;
        if (((Temp >> 4) & 0xFFF) == AAC_SYNCWORD)
            break;
        // reduce the possible to error checking, so we suppose:  the first byte should be the sync
        // location clips:  ardehd.ts / zdfhd.ts
        break;
    }
    if (AAC_SYNCWORD != ((Temp >> 4) & 0xFFF))
        return 1;

    Temp = (pTemp[0] << 24) | (pTemp[1] << 16) | (pTemp[2] << 8) | pTemp[3];
    pTemp += 4;
    profile = (Temp >> 30) & 0x3;                         // 2 bits
    sample_frequency_index = (Temp >> 26) & 0xF;          // 4 bits
    channel_configuration = (Temp >> 22) & 0x7;           // 3 bits
    aac_frame_length = (Temp >> 5) & 0x1FFF;              // 13 bits
    number_of_raw_data_blocks_in_frame = (*pTemp) & 0x3;  // 2 bits
    pTemp++;

    pAHeader->SampleRate = AAC_AudioSampleRate[sample_frequency_index];
    samples = (number_of_raw_data_blocks_in_frame + 1) * 1024;
    pAHeader->BitRate = aac_frame_length * 8 * pAHeader->SampleRate / samples;  // bps ??
    pAHeader->Channels = AAC_ChannelsTable[channel_configuration];

    MPG2_PARSER_SYSINFO_LOG(
            "aac audio: sample rate: %d, bitrate: %d, channel: %d, frame length: %d \r\n",
            pAHeader->SampleRate, pAHeader->BitRate, pAHeader->Channels, aac_frame_length);

    /*simply set channel mode to stereo, right?  */
    pAHeader->ChannelMode = 0;
    pAHeader->enuAudioType = FSL_MPG_DEMUX_AAC_AUDIO;

    if (pAHeader->pCodecData) {
        pAHeader->pCodecData[0] = ((profile + 1) << 3) | (sample_frequency_index >> 1);

        pAHeader->pCodecData[1] =
                ((sample_frequency_index << 7) & 0x80) | (channel_configuration << 3);
        pAHeader->nCodecDataSize = 2;
    }

    return 0;
}
