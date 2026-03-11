/***********************************************************************
 * Copyright 2021, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************/

#ifndef __IEC937PARSER_H__
#define __IEC937PARSER_H__
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "spdifparser_types.h"
#include "string.h"

/* log print configuration */
//#define IEC937_LOG_PRINTF printf
#define IEC937_LOG_PRINTF(...)

/* iec937 protocol definition */
#define IEC937_PA_LOW_BYTE (0x72)
#define IEC937_PA (0xF872)
#define IEC937_PA_BYTE_LEN (0x02)
#define IEC937_PB (0x4E1F)
#define IEC937_PB_BYTE_LEN (0x02)
#define IEC937_PC_BYTE_IDX (0x04)
#define IEC937_PD_BYTE_IDX (0x06)
#define IEC937_SYNCWORD_BYTE_LEN (0x08)
#define IEC937_PAYLOAD_SIZE_AC3 (1536 * 4)
#define IEC937_PAYLOAD_SIZE_EAC3 (6144 * 4)
#define IEC937_PAYLOAD_SIZE_MPEG1L1 (384 * 4)
#define IEC937_PAYLOAD_SIZE_MPEG1L23 (1152 * 4)
#define IEC937_PAYLOAD_SIZE_AAC (1024 * 4)

typedef enum {
    IEC937_FORMAT_TYPE_NULL = 0x00,
    IEC937_FORMAT_TYPE_AC3 = 0x01,
    IEC937_FORMAT_TYPE_EAC3 = 0x15,
    IEC937_FORMAT_TYPE_MPEG1L1 = 0x4,
    IEC937_FORMAT_TYPE_MPEG1L23 = 0x5,
    IEC937_FORMAT_TYPE_MPEG2 = 0x6,
    IEC937_FORMAT_TYPE_MPEG2L1 = 0x8,
    IEC937_FORMAT_TYPE_MPEG2L2 = 0x9,
    IEC937_FORMAT_TYPE_MPEG2L3 = 0xA,
    IEC937_FORMAT_TYPE_MPEG2_4_AAC = 0x7,
    IEC937_FORMAT_TYPE_MPEG2_4_AAC_2 = 0x13,
    IEC937_FORMAT_TYPE_MPEG2_4_AAC_3 = 0x33
} IEC937_FORMAT_TYPE;

typedef struct {
    uint8_t format_type;
    uint32_t audio_len;
} iec937_parser_t;

void iec937_parser_init(iec937_parser_t* p_parser);
SPDIF_RET_TYPE iec937_parser_search_header(iec937_parser_t* p_parser, uint8_t* p_buf, uint32_t len,
                                           uint8_t step, uint32_t* p_out_pos);
uint32_t iec937_parser_read_frame_size(iec937_parser_t* p_parser);
uint32_t iec937_parser_read_audio_data_len(iec937_parser_t* p_parser);
SPDIF_RET_TYPE iec937_parser_read_audio_data(uint8_t* src, uint8_t* dst, uint32_t len,
                                             uint8_t step);
IEC937_FORMAT_TYPE iec937_parser_get_format_type(iec937_parser_t* p_parser);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
