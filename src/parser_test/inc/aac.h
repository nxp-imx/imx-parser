/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef AAC_TEST_HEADER_INCLUDED
#define AAC_TEST_HEADER_INCLUDED

#define ADTS_HEADER_SIZE 7

/* Macros for the ADTS Header bit Allocation. */
#define BITS_FOR_SYNCWORD 12
#define BITS_FOR_MPEG_ID 1
#define BITS_FOR_MPEG_LAYER 2
#define BITS_FOR_PROTECTION 1
#define BITS_FOR_PROFILE 2
#define BITS_FOR_SAMPLING_FREQ 4
#define BITS_FOR_PRIVATE_BIT 1
#define BITS_FOR_CHANNEL_TYPE 3
#define BITS_FOR_ORIGINAL_COPY 1
#define BITS_FOR_HOME 1
#define BITS_FOR_COPYRIGHT 1
#define BITS_FOR_COPYRIGHT_START 1
#define BITS_FOR_FRAME_LENGTH 13
#define BITS_FOR_BUFFER_FULLNESS 11

typedef struct {
    uint32 syncword;
    uint32 id; /* MPEG identifier, 1 for MPEG-2 AAC, 0 for MPEG-4 AAC */
    uint32 layer;
    uint32 protection_abs;
    uint32 profile;
    uint32 sampl_freq_idx;
    uint32 private_bit;
    uint32 channel_config;
    uint32 original_copy;
    uint32 home;
    uint32 copyR_id_bit;
    uint32 copyR_id_start;
    uint32 frame_length;
    uint32 buff_fullness;
    uint32 num_of_rdb;
    uint32 crc_check;
    uint32 rdb_position;
    uint32 crc_check_rdb;

} sAACConfig, *sAACConfigPtr;

int32 setAACConfig(uint8* decoder_info, uint32 audioDecoderType, sAACConfigPtr aacConfig);
int32 PutAdtsHeader(sAACConfigPtr Source, uint8* Destination);

#endif
