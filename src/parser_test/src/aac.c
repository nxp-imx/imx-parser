/*
***********************************************************************
* Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include <string.h>
#include "fsl_types.h"
#include "err_logs.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "fsl_parser_drm.h"
#include "aac.h"

/* AAC frequency index table */
static const uint32 SampFreqIndexTable[12] = {96000, 88200, 64000, 48000, 44100, 32000,
                                              24000, 22050, 16000, 12000, 11025, 8000};

static uint32 bword, bbit;

/*
NOTE: BSAC sample do not need ADTS header!

For definition of AudioSpecificConfig:
        ES_Descriptor--->DecoderConfigDescriptor-->DecoderSpecificInfo-->AudioSpecificConfig
        Refer to 14496-1 p42
        8.6.6 DecoderConfigDescriptor
        8.6.6.1 Syntax

        For AAC audio track,
        14496-3 section 1.6 Interface to 14496-1, AudioSpecificConfig
        1.6.2 Syntax
        1.6.2.1 AudioSpecificConfig
*/
int32 setAACConfig(uint8* decoder_info, uint32 audioDecoderType, sAACConfigPtr aacConfig) {

    uint8 val;

    memset(aacConfig, 0, sizeof(sAACConfig));

    /* Fill the values for the FIXED ADTS header variable. */
    aacConfig->syncword = 0xfff; /* Frame sync (all bits must be set) */
    aacConfig->id = 0;           /* 0: MPEG-4, 1: MPEG-2              */
    aacConfig->layer = 0;        /* Layer is Always 0, 0 layer.       */

    /* 0- Protected by CRC (16bit CRC follows header) 1 - Not protected. */
    aacConfig->protection_abs = 1; /* `   .   */
    aacConfig->profile = 1;        /* Low Complexity (LC) AAC LC  */
    aacConfig->sampl_freq_idx = 4; /* Media Time Scale.           */

    /* Only informative Bits. */
    aacConfig->private_bit = 0;
    aacConfig->original_copy = 0;
    aacConfig->home = 0;
    aacConfig->copyR_id_bit = 0;
    aacConfig->copyR_id_start = 0;
    aacConfig->buff_fullness = 0x7FF;
    aacConfig->channel_config = 2; /* Default is 2 channels. Update after audio decoder info (NOT
                                      'stsd) is parsed. Amanda */

    /* No CRC Check is performed. */
    aacConfig->crc_check = 0;

    //Audio_ObjectType = (decoder_info[0] >> 3) & 0x1f; /* 22 is BSAC audio */

    /* ENGR114907: Is MPEG-2 AAC? Then revise Mpeg Identifier for ADTS header (default is MPEG-4
    AAC). Non-AAC audio types does not care this flag. */
    if (AUDIO_AAC == audioDecoderType)
        aacConfig->id = 0;
    else if (AUDIO_MPEG2_AAC == audioDecoderType)
        aacConfig->id = 1;
    else
        return PARSER_ERR_INVALID_PARAMETER;

    val = (decoder_info[1] >> 7) & 1;
    /* Get the sampling frequency index */
    aacConfig->sampl_freq_idx = (((decoder_info[0] << 5) | (val << 4)) >> 4) & 15;
    if (12 > aacConfig->sampl_freq_idx) {
        PARSER_INFO(PARSER_INFO_STREAM,
                    "\t << AAC sample rate from decoder specific info is %ld HZ >>\n",
                    SampFreqIndexTable[aacConfig->sampl_freq_idx]);
    } else {
        PARSER_INFO(PARSER_INFO_STREAM,
                    "\t << AAC sample rate from decoder specific info is invalid! >>\n");
    }

    /* Get the number of channels,
    TODO: if the number of channels in decoder info is zero,  ADTS header use default channel number
    2 */
    aacConfig->channel_config = (decoder_info[1] & 120) >> 3;

    return PARSER_SUCCESS;
}

/******************************************************************************
* Function:    MP4PutBits
*
* Description: Function to fill the number of bits required for the each
variable.
*
* Argumnets:   source
*              destination
*
* Return:      The error code.
*
* Notes:
*****************************************************************************/
static int32 PutBits(uint8** PositionDest, uint32 Data, uint32 NumberOfBits) {
    uint32 dbit;
    uint32 word;
    uint32 bit;
    uint8* dummyDest;

    /* Hold the Address of the passed Destination source address. */
    if (PositionDest == NULL) {
        dummyDest = NULL;
    } else {
        dummyDest = *PositionDest;
    }

    dbit = 1 << (NumberOfBits - 1);
    word = bword;
    bit = bbit;
    while (dbit != 0) {
        if (Data & dbit) {
            word |= bit;
        }

        dbit >>= 1;
        bit >>= 1;

        if (bit == 0) {
            if (dummyDest) {
                *dummyDest++ = (uint8)word;
            }

            word = 0;
            bit = 0x80;
        }
    }

    bword = word;
    bbit = bit;

    if (dummyDest) {
        *PositionDest = dummyDest;
        return PARSER_SUCCESS;
    } else {
        return PARSER_INSUFFICIENT_MEMORY;
    }
}

/******************************************************************************
 * Function:    MP4PutAdtsHeader
 *
 * Description: Function to fill the required bits for the ADTS header.
 *
 * Argumnets:   source
 *              destination
 *
 * Return:      The error code.
 *
 * Notes:
 *****************************************************************************/
int32 PutAdtsHeader(sAACConfigPtr Source, uint8* Destination) {
    int32 err = 0;          /* Error Variable.                         */
    uint8* StartPos = NULL; /* Store the start postion of destination. */
    bbit = 0x80;            /* Byte align for bit stream.              */
    bword = 0;

    if (NULL == Source || NULL == Destination) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    StartPos = Destination;
    /* Fill the number of bits required fixed and varibale. */
    err = PutBits(&Destination, Source->syncword, BITS_FOR_SYNCWORD);
    if (err)
        return err;

    err = PutBits(&Destination, Source->id, BITS_FOR_MPEG_ID);
    if (err)
        return err;

    err = PutBits(&Destination, Source->layer, BITS_FOR_MPEG_LAYER);
    if (err)
        return err;

    err = PutBits(&Destination, Source->protection_abs, BITS_FOR_PROTECTION);
    if (err)
        return err;

    err = PutBits(&Destination, Source->profile, BITS_FOR_PROFILE);
    if (err)
        return err;

    err = PutBits(&Destination, Source->sampl_freq_idx, BITS_FOR_SAMPLING_FREQ);
    if (err)
        return err;

    err = PutBits(&Destination, Source->private_bit, BITS_FOR_PRIVATE_BIT);
    if (err)
        return err;

    err = PutBits(&Destination, Source->channel_config, BITS_FOR_CHANNEL_TYPE);
    if (err)
        return err;

    err = PutBits(&Destination, Source->original_copy, BITS_FOR_ORIGINAL_COPY);
    if (err)
        return err;

    err = PutBits(&Destination, Source->home, BITS_FOR_HOME);
    if (err)
        return err;

    err = PutBits(&Destination, Source->copyR_id_bit, BITS_FOR_COPYRIGHT);
    if (err)
        return err;

    err = PutBits(&Destination, Source->copyR_id_start, BITS_FOR_COPYRIGHT_START);
    if (err)
        return err;

    err = PutBits(&Destination, Source->frame_length, BITS_FOR_FRAME_LENGTH);
    if (err)
        return err;

    err = PutBits(&Destination, Source->buff_fullness, BITS_FOR_BUFFER_FULLNESS);
    if (err)
        return err;

    /* Byte Align. */
    while (bbit != 0x80) {
        PutBits(&Destination, 0, 1);
    }

    /* Return the start of the destination. */
    return (Destination - StartPos);
}
