/***********************************************************************
 * Copyright 2021, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************/

#ifndef __IEC958PARSER_H__
#define __IEC958PARSER_H__
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "spdifparser_types.h"
#include "string.h"

/* Configure log print function */
//#define IEC958_LOG_PRINTF printf
#define IEC958_LOG_PRINTF(...)


/* iec958 protocol definition */
#define IEC958_SUBFRAME_SIZE (0x04)
#define IEC958_CHECK_BYTE_LEN (0x10)
#define IEC958_CHECK_STEP_SIZE (0x0C)
#define IEC958_BLOCK_SUBFRAME_NUM (192 << 1)
#define IEC958_BLOCK_SUBFRAME_LEN (192 << 3)
#define IEC958_CHANNEL_STATUS_MSK (1 << 30)
#define IEC958_VALID_FLAG_MSK (1 << 28)
#define IEC958_PREAMBLE_MSK (0x0f)
#define IEC958_PREAMBLE_BITS_Z (0x00)
#define IEC958_PREAMBLE_BITS_X (0x01)
#define IEC958_PREAMBLE_BITS_Y (0x03)

/* Configurable parameters */
#define IEC958_PCM_FORMAT_MATCH_THR (IEC958_BLOCK_SUBFRAME_NUM >> 1)

/* Audio format match */
#define IEC958_FEATURE_MATCH_FORMAT_BY_DATA
/* IEC937_PAYLOAD_SIZE_EAC3 * 2 */
#define IEC958_PARSER_MATCH_FORMAT_DATA_THR (6144 * 4 * 2)

typedef enum {
    IEC958_VALID_FLAG_NO_SET = 0,
    IEC958_VALID_FLAG_0 = 1,
    IEC958_VALID_FLAG_1 = 2,
} IEC958_VALID_TYPE;

typedef enum {
    IEC958_FORMAT_TYPE_NONE = 0x00,
    IEC958_FORMAT_TYPE_PCM = 0x01,
    IEC958_FORMAT_TYPE_COMPRESS = 0x02,
    IEC958_FORMAT_TYPE_UNKNOWN = 0xFF
} IEC958_FORMAT_TYPE;

typedef enum {
    IEC958_PARSER_STATUS_MATCH = 0x00,
    IEC958_PARSER_STATUS_CHECK_CHANNEL_STATUS = 0x01,
    IEC958_PARSER_STATUS_CHECK_VALID = 0x02,
    IEC958_PARSER_STATUS_CHECK_PCM = 0x03,
    IEC958_PARSER_STATUS_PARSING_IEC937 = 0x04,
    IEC958_PARSER_STATUS_DETECT_FORMAT = 0x05,
} IEC958_PARSER_STATUS;

typedef struct {
    IEC958_VALID_TYPE valid_flag;
    /* use for discard data for pcm type */
    int32_t valid_flag_pos;
    uint32_t channel0_cnt;
    uint32_t channel1_cnt;
} iec958_valid_info_t;

typedef struct {
    uint32_t frame_id;
    uint32_t channel_num;
    uint32_t sample_rate;
    uint32_t data_length;
    uint32_t default_word_length;
} iec958_audio_info_t;

typedef enum {
    IEC958_AUTO_PARSER_MODE = 0,
    IEC958_COMPRESS_PARSER_MODE = 1,
    IEC958_PCM_PARSER_MODE = 2,
} IEC958_PARSER_MODE;

typedef SPDIF_RET_TYPE (*iec958_parser_search_iec937_header_cb)(void* param, uint8_t* p_buf,
                                                                uint32_t len, uint32_t* p_out_pos);

typedef struct {
    uint32_t pos;
    IEC958_PARSER_MODE mode;
    IEC958_PARSER_STATUS status;
    IEC958_FORMAT_TYPE iec958_type;
    iec958_valid_info_t valid_info;
    uint8_t channel_status[24];
    iec958_audio_info_t audio_info;
    iec958_parser_search_iec937_header_cb iec958_parser_search_iec937_header;
} iec958_parser_t;

void iec958_parser_init(iec958_parser_t* p_parser, iec958_parser_search_iec937_header_cb p_fun);
void iec958_parser_set_mode(iec958_parser_t* p_parser, IEC958_PARSER_MODE mode);
IEC958_PARSER_STATUS iec958_parser_get_status(iec958_parser_t* p_parser);
SPDIF_RET_TYPE iec958_parser_search_header(iec958_parser_t* p_parser, void* param, uint8_t* p_buf,
                                           uint32_t len, uint32_t* p_out_pos);
IEC958_FORMAT_TYPE iec958_parser_get_frame_type(iec958_parser_t* p_parser);
SPDIF_RET_TYPE iec958_parser_set_frame_type(iec958_parser_t* p_parser, IEC958_FORMAT_TYPE format);
SPDIF_RET_TYPE iec958_parser_read_sample(uint8_t* src, uint8_t* dst, uint32_t sample_width,
                                         uint32_t src_len, uint32_t* p_dst_len);
void iec958_parser_get_audio_info(iec958_parser_t* p_parser);
SPDIF_RET_TYPE iec958_parser_update_channel_status(iec958_parser_t* p_parser,
                                                   uint8_t is_check_preamble, uint8_t* p_buf,
                                                   uint32_t len);
SPDIF_RET_TYPE iec958_parser_set_default_word_length(iec958_parser_t* p_parser,
                                                     uint32_t word_length);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
