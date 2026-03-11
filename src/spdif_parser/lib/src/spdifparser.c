/***********************************************************************
 * Copyright 2021, 2025-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************/

#include "spdifparser.h"
#include "iec937parser.h"
#include "iec958parser.h"

typedef struct {
    SPDIF_PARSER_MODE mode;
    iec937_parser_t iec937_parser;
    iec958_parser_t iec958_parser;
    spdif_audio_info_t spdif_audio_info;
    spdif_parser_memory_ops mem_ops;
} spdif_parser_t;

/* version */
#define SEPARATOR " "

#define BASELINE_SHORT_NAME "SPDIFPARSER_01.00.00"

#ifdef __WINCE
#define OS_NAME "_WINCE"
#else
#define OS_NAME ""
#endif

#ifdef DEMO_VERSION
#define CODEC_RELEASE_TYPE "_DEMO"
#else
#define CODEC_RELEASE_TYPE ""
#endif

/* user define suffix */
#define VERSION_STR_SUFFIX ""

#define CODEC_VERSION_STR                                                                  \
    (BASELINE_SHORT_NAME OS_NAME CODEC_RELEASE_TYPE SEPARATOR VERSION_STR_SUFFIX SEPARATOR \
     "build on" SEPARATOR __DATE__ SEPARATOR __TIME__)

const char* spdif_parser_get_version_info(void) {
    return (const char*)CODEC_VERSION_STR;
}

static SPDIF_RET_TYPE spdif_parser_search_iec937_header_from_iec958_cb(void* param, uint8_t* p_buf,
                                                                       uint32_t len,
                                                                       uint32_t* p_out_pos) {
    iec937_parser_t* p_iec937_parser = (iec937_parser_t*)param;
    return iec937_parser_search_header(p_iec937_parser, p_buf, len, 4, p_out_pos);
}

static void spdif_parser_init(spdif_parser_t* p_parser) {
    iec937_parser_init(&p_parser->iec937_parser);
    iec958_parser_init(&p_parser->iec958_parser, spdif_parser_search_iec937_header_from_iec958_cb);
}

void spdif_parser_set_mode(spdif_parser_handle handle, SPDIF_PARSER_MODE mode) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;
    p_parser->mode = mode;
}

void spdif_parser_set_iec958_type(spdif_parser_handle handle, SPDIF_AUDIO_FORMAT_TYPE audio_type) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;

    if (audio_type == SPDIF_AUDIO_FORMAT_PCM) {
        iec958_parser_set_mode(&(p_parser->iec958_parser), IEC958_PCM_PARSER_MODE);
    } else {
        iec958_parser_set_mode(&(p_parser->iec958_parser), IEC958_COMPRESS_PARSER_MODE);
    }
}

SPDIF_PARSER_MODE
spdif_parser_get_mode(spdif_parser_handle handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;
    return p_parser->mode;
}

static SPDIF_RET_TYPE spdif_parser_match_header(spdif_parser_t* p_parser, uint8_t* p_buf,
                                                uint32_t len, uint32_t* p_out_pos) {
    SPDIF_RET_TYPE ret = SPDIF_ERR_INSUFFICIENT_DATA;
    uint32_t iec937_pos = 0;
    uint32_t iec958_pos = 0;

    ret = iec937_parser_search_header(&p_parser->iec937_parser, p_buf, len, 1, &iec937_pos);
    if (ret == SPDIF_OK) {
        *p_out_pos = iec937_pos;
        p_parser->mode = SPDIF_IEC937_PARSER_MODE;
        SPDIF_LOG_PRINTF(
                "spdif parser: mode change, SPDIF_AUTO_PARSER_MODE -> SPDIF_IEC937_PARSER_MODE\n");
        return ret;
    }

    ret = iec958_parser_search_header(&p_parser->iec958_parser, (void*)(&p_parser->iec937_parser),
                                      p_buf, len, &iec958_pos);
    if (IEC958_PARSER_STATUS_MATCH != iec958_parser_get_status(&p_parser->iec958_parser)) {
        *p_out_pos = iec958_pos;
        p_parser->mode = SPDIF_IEC958_PARSER_MODE;
        SPDIF_LOG_PRINTF(
                "spdif parser: mode change, SPDIF_AUTO_PARSER_MODE -> SPDIF_IEC958_PARSER_MODE\n");
        return ret;
    }

    /* Can't detect packet type, update check position */
    iec937_pos &= ~(0x03);
    *p_out_pos = (iec937_pos < iec958_pos) ? iec937_pos : iec958_pos;
    SPDIF_LOG_PRINTF("spdif_parser_detect_header: err ret= %d, pos = %d\n", ret, *p_out_pos);

    return ret;
}

void spdif_parser_get_audio_info(spdif_parser_handle handle,
                                 spdif_audio_info_t* p_spdif_audio_info) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;
    memcpy(p_spdif_audio_info, &(p_parser->spdif_audio_info), sizeof(spdif_audio_info_t));
}

static void spdif_parser_update_audio_info(spdif_parser_handle handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;
    spdif_audio_info_t* p_audio_info = &p_parser->spdif_audio_info;

    if (p_parser->spdif_audio_info.valid_flag) {
        return;
    }
    p_audio_info->valid_flag = 1;

    if (p_parser->mode == SPDIF_IEC937_PARSER_MODE) {
        p_audio_info->iec937_type =
                (SPDIF_IEC937_FORMAT_TYPE)iec937_parser_get_format_type(&p_parser->iec937_parser);
        p_audio_info->audio_type = SPDIF_AUDIO_FORMAT_COMPRESS;
        p_audio_info->frame_size = spdif_parser_get_compress_audio_frame_size(handle);
        p_audio_info->audio_size = spdif_parser_get_compress_audio_len(handle);
        p_audio_info->sample_per_frame = p_audio_info->frame_size >> 2;
    } else if (p_parser->mode == SPDIF_IEC958_PARSER_MODE) {
        IEC958_FORMAT_TYPE type = iec958_parser_get_frame_type(&p_parser->iec958_parser);
        if (type == IEC958_FORMAT_TYPE_COMPRESS) {
            p_audio_info->iec937_type = (SPDIF_IEC937_FORMAT_TYPE)iec937_parser_get_format_type(
                    &p_parser->iec937_parser);
            p_audio_info->audio_type = SPDIF_AUDIO_FORMAT_COMPRESS;
            p_audio_info->frame_size = spdif_parser_get_compress_audio_frame_size(handle);
            p_audio_info->audio_size = spdif_parser_get_compress_audio_len(handle);
            p_audio_info->sample_per_frame = p_audio_info->frame_size >> 3;
        } else if (type == IEC958_FORMAT_TYPE_PCM) {
            p_audio_info->audio_type = SPDIF_AUDIO_FORMAT_PCM;
        } else {
            p_audio_info->audio_type = SPDIF_AUDIO_FORMAT_UNKNOWN;
            SPDIF_LOG_PRINTF("unknown format\n");
        }
        p_audio_info->sample_rate = p_parser->iec958_parser.audio_info.sample_rate;
        p_audio_info->channel_num = p_parser->iec958_parser.audio_info.channel_num;
        p_audio_info->data_length = p_parser->iec958_parser.audio_info.data_length;
    }

    SPDIF_LOG_PRINTF(
            "SPDIF: mode = %d, audio_type = %d, sample_rate = %d, channel_num = %d, data_length = "
            "%d, frame_size = %d, audio_size = %d\n",
            p_parser->mode, p_audio_info->audio_type, p_audio_info->sample_rate,
            p_audio_info->channel_num, p_audio_info->data_length, p_audio_info->frame_size,
            p_audio_info->audio_size);
}

SPDIF_RET_TYPE
spdif_parser_search_header(spdif_parser_handle handle, uint8_t* p_buf, uint32_t len,
                           uint32_t* p_out_pos) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;
    SPDIF_RET_TYPE ret = SPDIF_ERR_PARAM;

    switch (p_parser->mode) {
        case SPDIF_IEC937_PARSER_MODE: {
            ret = iec937_parser_search_header(&p_parser->iec937_parser, p_buf, len, 1, p_out_pos);
            SPDIF_LOG_PRINTF("spdif parser: mode = SPDIF_IEC937_PARSER_MODE\n");
            break;
        }
        case SPDIF_IEC958_PARSER_MODE: {
            ret = iec958_parser_search_header(&p_parser->iec958_parser,
                                              (void*)(&p_parser->iec937_parser), p_buf, len,
                                              p_out_pos);
            SPDIF_LOG_PRINTF("spdif parser: mode = SPDIF_IEC958_PARSER_MODE\n");
            break;
        }
        case SPDIF_AUTO_PARSER_MODE: {
            ret = spdif_parser_match_header(p_parser, p_buf, len, p_out_pos);
            break;
        }
        default: {
            SPDIF_LOG_PRINTF("spdif parser: unsupported mode = %d\n", p_parser->mode);
            break;
        }
    }

    /* Update audio infomation */
    if (ret == SPDIF_OK) {
        spdif_parser_update_audio_info(p_parser);
    }

    return ret;
}

SPDIF_RET_TYPE
spdif_parser_read(spdif_parser_handle handle, uint8_t* src, uint8_t* dst, uint32_t src_len,
                  uint32_t* p_dst_len) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;
    SPDIF_RET_TYPE ret = SPDIF_OK;
    uint32_t offset = IEC937_SYNCWORD_BYTE_LEN;

    switch (p_parser->mode) {
        case SPDIF_IEC937_PARSER_MODE: {
            ret = iec937_parser_read_audio_data(src + offset, dst, src_len, 1);
            *p_dst_len = src_len;
            break;
        }
        case SPDIF_IEC958_PARSER_MODE: {
            if (iec958_parser_get_frame_type(&p_parser->iec958_parser) == IEC958_FORMAT_TYPE_PCM) {
                offset = 0;
                ret = iec958_parser_read_sample(src, dst, p_parser->spdif_audio_info.data_length,
                                                src_len, p_dst_len);
            } else {
                offset <<= 1;
                ret = iec937_parser_read_audio_data(src + offset, dst, src_len, 4);
                *p_dst_len = src_len >> 1;
            }
            break;
        }
        default: {
            SPDIF_LOG_PRINTF("spdif_parser_read: unsupported mode = %d\n", p_parser->mode);
            ret = SPDIF_ERR_PARAM;
            break;
        }
    }
    return ret;
}

uint32_t spdif_parser_get_compress_audio_len(spdif_parser_handle handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;
    uint32_t bit_pos = 0;

    if (p_parser->mode == SPDIF_IEC958_PARSER_MODE) {
        bit_pos = 1;
        SPDIF_LOG_PRINTF("iec937 packet in iec958 frames\n");
    }

    return (iec937_parser_read_audio_data_len(&p_parser->iec937_parser) << bit_pos);
}

uint32_t spdif_parser_get_compress_audio_frame_size(spdif_parser_handle handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;
    uint32_t bit_pos = 0;

    if (p_parser->mode == SPDIF_IEC958_PARSER_MODE) {
        bit_pos = 1;
        SPDIF_LOG_PRINTF("iec937 packet in iec958 frames\n");
    }

    return (iec937_parser_read_frame_size(&p_parser->iec937_parser) << bit_pos);
}

SPDIF_AUDIO_FORMAT_TYPE
spdif_parser_get_audio_type(spdif_parser_handle handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;

    return p_parser->spdif_audio_info.audio_type;
}

SPDIF_IEC937_FORMAT_TYPE
spdif_parser_get_iec937_type(spdif_parser_handle handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;

    return p_parser->spdif_audio_info.iec937_type;
}

SPDIF_RET_TYPE
spdif_parser_read_with_sync(spdif_parser_handle handle, uint8_t* src, uint8_t* dst,
                            uint32_t src_len, uint32_t* p_dst_len, uint32_t* p_out_src_pos) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;
    SPDIF_RET_TYPE ret = SPDIF_OK;
    uint32_t header_pos = 0;

    *p_out_src_pos = 0;
    *p_dst_len = 0;
    while (src_len) {
        ret = spdif_parser_search_header(handle, src, src_len, &header_pos);
        if (header_pos) {
            src_len -= header_pos;
            src += header_pos;
            *p_out_src_pos += header_pos;
            SPDIF_LOG_PRINTF("header_pos = 0x%x\n", header_pos);
        }

        if ((ret == SPDIF_OK) || (ret == SPDIF_ERR_INSUFFICIENT_DATA) || (ret == SPDIF_ERR_PARAM)) {
            break;
        }
    }

    if (ret != SPDIF_OK) {
        return ret;
    }

    /* Check compress audio frame size */
    if (p_parser->spdif_audio_info.audio_type != SPDIF_AUDIO_FORMAT_PCM) {
        if (src_len < p_parser->spdif_audio_info.frame_size) {
            return SPDIF_ERR_INSUFFICIENT_DATA;
        }
        src_len = p_parser->spdif_audio_info.audio_size;
    }

    ret = spdif_parser_read(handle, src, dst, src_len, p_dst_len);
    if (ret == SPDIF_OK) {
        if (p_parser->spdif_audio_info.audio_type != SPDIF_AUDIO_FORMAT_PCM) {
            *p_out_src_pos += p_parser->spdif_audio_info.frame_size;
        } else {
            *p_out_src_pos += src_len;
        }
    }

    return ret;
}

uint32_t spdif_parser_get_sample_rate(spdif_parser_handle handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;

    return p_parser->spdif_audio_info.sample_rate;
}

uint32_t spdif_parser_get_channel_num(spdif_parser_handle handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;

    return p_parser->spdif_audio_info.channel_num;
}

uint32_t spdif_parser_get_data_length(spdif_parser_handle handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;

    return p_parser->spdif_audio_info.data_length;
}

SPDIF_RET_TYPE
spdif_parser_set_default_word_length(spdif_parser_handle handle, uint32_t word_length) {
    spdif_parser_t* p_parser = (spdif_parser_t*)handle;

    return iec958_parser_set_default_word_length(&p_parser->iec958_parser, word_length);
}

SPDIF_RET_TYPE spdif_parser_open(spdif_parser_handle* p_handle,
                                 spdif_parser_memory_ops* p_mem_ops) {
    spdif_parser_t* p_parser;

    if (!p_handle || !p_mem_ops || !(p_mem_ops->malloc) || (!p_mem_ops->free)) {
        return SPDIF_ERR_PARAM;
    }

    p_parser = (spdif_parser_t*)p_mem_ops->malloc(sizeof(spdif_parser_t));
    spdif_parser_init(p_parser);

    p_parser->mem_ops.malloc = p_mem_ops->malloc;
    p_parser->mem_ops.free = p_mem_ops->free;
    *p_handle = (spdif_parser_handle)p_parser;

    return SPDIF_OK;
}

SPDIF_RET_TYPE spdif_parser_close(spdif_parser_handle* p_handle) {
    spdif_parser_t* p_parser = (spdif_parser_t*)(*p_handle);

    if (!p_handle) {
        return SPDIF_ERR_PARAM;
    }

    p_parser->mem_ops.free(*p_handle);
    *p_handle = 0;

    return SPDIF_OK;
}

SPDIF_RET_TYPE spdif_parser_query_interface(uint32_t id, void** func) {
    if (!func) {
        return SPDIF_ERR_PARAM;
    }

    switch (id) {
        /* parser version information */
        case SPDIF_PARSER_API_GET_VERSION_INFO:
            *func = (void*)spdif_parser_get_version_info;
            break;
        case SPDIF_PARSER_API_OPEN:
            *func = (void*)spdif_parser_open;
            break;
        case SPDIF_PARSER_API_CLOSE:
            *func = (void*)spdif_parser_close;
            break;
        case SPDIF_PARSER_API_SET_MODE:
            *func = (void*)spdif_parser_set_mode;
            break;
        case SPDIF_PARSER_API_SET_IEC958_TYPE:
            *func = (void*)spdif_parser_set_iec958_type;
            break;
        case SPDIF_PARSER_API_GET_MODE:
            *func = (void*)spdif_parser_get_mode;
            break;
        case SPDIF_PARSER_API_SEARCH_HEADER:
            *func = (void*)spdif_parser_search_header;
            break;
        case SPDIF_PARSER_API_GET_COMPRESS_AUDIO_FRAME_SIZE:
            *func = (void*)spdif_parser_get_compress_audio_frame_size;
            break;
        case SPDIF_PARSER_API_GET_COMPRESS_AUDIO_LEN:
            *func = (void*)spdif_parser_get_compress_audio_len;
            break;
        case SPDIF_PARSER_API_READ:
            *func = (void*)spdif_parser_read;
            break;
        case SPDIF_PARSER_API_READ_WITH_SYNC:
            *func = (void*)spdif_parser_read_with_sync;
            break;
        case SPDIF_PARSER_API_GET_AUDIO_INFO:
            *func = (void*)spdif_parser_get_audio_info;
            break;
        case SPDIF_PARSER_API_GET_AUDIO_TYPE:
            *func = (void*)spdif_parser_get_audio_type;
            break;
        case SPDIF_PARSER_API_GET_IEC937_TYPE:
            *func = (void*)spdif_parser_get_iec937_type;
            break;
        case SPDIF_PARSER_API_GET_SAMPLE_RATE:
            *func = (void*)spdif_parser_get_sample_rate;
            break;
        case SPDIF_PARSER_API_GET_CHANNEL_NUM:
            *func = (void*)spdif_parser_get_channel_num;
            break;
        case SPDIF_PARSER_API_GET_DATA_LENGTH:
            *func = (void*)spdif_parser_get_data_length;
            break;
        case SPDIF_PARSER_API_SET_DEFAULT_WORD_LENGTH:
            *func = (void*)spdif_parser_set_default_word_length;
            break;
        default:
            *func = 0;
            break;
    }

    return SPDIF_OK;
}

#if 0
// sample code
typedef struct
{
    spdif_parser_handle handle;
    /* Record all handled data */
    uint32_t offset;
    uint32_t iec_frame_size;
    uint32_t iec_audio_size;
    uint32_t audio_buf_len;
}spdif_parser_app_data_t;

#include "stdlib.h"
int spdif_parser_read_withr_sync_sample_code (int argc, char **agrv)
{
    // test buffer, need to malloc and fill the test data
    uint8_t *p_buf = malloc(SPDIF_PARSER_IEC958_BLOCK_BYTE_LEN);  
    uint32_t len = SPDIF_PARSER_IEC958_BLOCK_BYTE_LEN;
    uint8_t *p_audio_buf = 0;
    spdif_parser_app_data_t parser_data;
    spdif_parser_app_data_t *p_spdif_parser_data = &parser_data;
    uint32_t header_pos = 0;
    SPDIF_RET_TYPE ret = SPDIF_ERR_INSUFFICIENT_DATA;
    
    spdif_parser_memory_ops mem_ops = {malloc, free};
    spdif_parser_open(&p_spdif_parser_data->handle, &mem_ops);

    /* step1: search header and filter noise data */
    while (len >= SPDIF_PARSER_IEC958_BLOCK_BYTE_LEN)
    {
        ret = spdif_parser_search_header(p_spdif_parser_data->handle, p_buf, len, &header_pos);
        if (header_pos)
        {
            len -= header_pos;
            p_buf += header_pos;
            /* Record all handled data for some usage */
            p_spdif_parser_data->offset += header_pos;
        }
        if ((ret == SPDIF_ERR_INSUFFICIENT_DATA) || (ret == SPDIF_ERR_PARAM))
        {
            return ret;
        }
        else if (ret == SPDIF_OK)
        {
            break;
        }
    }

    /* step2: configure audio frame length */
    if (spdif_parser_get_audio_type(p_spdif_parser_data->handle) == SPDIF_AUDIO_FORMAT_PCM)
    {
        p_spdif_parser_data->iec_frame_size = len;
    }
    else
    {
        p_spdif_parser_data->iec_frame_size = spdif_parser_get_compress_audio_frame_size(p_spdif_parser_data->handle);
        if (len < p_spdif_parser_data->iec_frame_size)
        {
            return SPDIF_ERR_INSUFFICIENT_DATA;
        }
    }

    while (len)
    {
        /* step3: extract audio data */
        p_audio_buf = malloc(p_spdif_parser_data->iec_frame_size);
        uint32_t actual_out_len = 0;

        uint32_t src_pos = 0;
        ret = spdif_parser_read_with_sync(p_spdif_parser_data->handle, p_buf, p_audio_buf, len, &actual_out_len, &src_pos);
        p_buf += src_pos;
        len -= src_pos;
        p_spdif_parser_data->offset += src_pos;
        if (ret == SPDIF_OK)
        {
            if (actual_out_len)
            {
                // add application code here
                // send audio buffer, attention: audio buffer length = actual_out_len!
            }
        }
        else
        {
            if ((ret == SPDIF_ERR_INSUFFICIENT_DATA) || (ret == SPDIF_ERR_PARAM))
            {
                break;
            }
        }
    }
    spdif_parser_close(p_spdif_parser_data->handle);

    if (p_buf)
    {
        free(p_buf);
    }
    if (p_audio_buf)
    {
        free(p_audio_buf);
    }

    return ret;
}

int spdif_parser_sample_code (int argc, char **agrv)
{
    // test buffer, need to malloc and fill the test data
    uint8_t *p_buf = malloc(SPDIF_PARSER_IEC958_BLOCK_BYTE_LEN);  
    uint32_t len = SPDIF_PARSER_IEC958_BLOCK_BYTE_LEN;
    uint8_t *p_audio_buf = 0;
    spdif_parser_app_data_t parser_data;
    spdif_parser_app_data_t *p_spdif_parser_data = &parser_data;
    uint32_t header_pos = 0;
    SPDIF_RET_TYPE ret = SPDIF_ERR_INSUFFICIENT_DATA;
    
    spdif_parser_memory_ops mem_ops = {malloc, free};
    spdif_parser_open(&p_spdif_parser_data->handle, &mem_ops);

    while (len >= SPDIF_PARSER_IEC958_BLOCK_BYTE_LEN)
    {
        /* step1: search header and filter noise data */
        while (len >= SPDIF_PARSER_IEC958_BLOCK_BYTE_LEN)
        {
            ret = spdif_parser_search_header(p_spdif_parser_data->handle, p_buf, len, &header_pos);
            if (header_pos)
            {
                len -= header_pos;
                p_buf += header_pos;
                /* Record all handled data for some usage */
                p_spdif_parser_data->offset += header_pos;
            }
            if ((ret == SPDIF_OK) || (ret == SPDIF_ERR_INSUFFICIENT_DATA) || (ret == SPDIF_ERR_PARAM))
            {
                break;
            }
        }

        if (ret != SPDIF_OK)
        {
            break;
        }

        /* step2: check audio frame length */
        if (spdif_parser_get_audio_type(p_spdif_parser_data->handle) == SPDIF_AUDIO_FORMAT_PCM)
        {
            p_spdif_parser_data->iec_frame_size = SPDIF_PARSER_IEC958_BLOCK_BYTE_LEN;
            p_spdif_parser_data->iec_audio_size = SPDIF_PARSER_IEC958_BLOCK_BYTE_LEN;
            p_spdif_parser_data->audio_buf_len = p_spdif_parser_data->iec_audio_size / 2;
        }
        else
        {
            p_spdif_parser_data->iec_frame_size = spdif_parser_get_compress_audio_frame_size(p_spdif_parser_data->handle);
            p_spdif_parser_data->iec_audio_size = spdif_parser_get_compress_audio_len(p_spdif_parser_data->handle);
            p_spdif_parser_data->audio_buf_len = p_spdif_parser_data->iec_audio_size;
            
            if (spdif_parser_get_mode(p_spdif_parser_data->handle) == SPDIF_IEC958_PARSER_MODE)
            {
                p_spdif_parser_data->audio_buf_len = (p_spdif_parser_data->iec_audio_size - (IEC937_SYNCWORD_BYTE_LEN << 1)) << 1;
            }
            else
            {
                p_spdif_parser_data->audio_buf_len = p_spdif_parser_data->iec_audio_size - IEC937_SYNCWORD_BYTE_LEN;
            }
        }

        if (len < p_spdif_parser_data->iec_frame_size)
        {
            ret = SPDIF_ERR_INSUFFICIENT_DATA;
            break;
        }

        /* step3: extract audio data */
        p_audio_buf = malloc(p_spdif_parser_data->audio_buf_len);
        uint32_t actual_out_len = 0;
        spdif_parser_read(p_spdif_parser_data->handle, p_buf, p_audio_buf, p_spdif_parser_data->iec_audio_size, &actual_out_len);
        if (actual_out_len != p_spdif_parser_data->audio_buf_len)
        {
            //error, it should not happend
        }
        else
        {
            // add application code here
            // send audio buffer, audio buffer length = actual_out_len
        }

        p_buf += p_spdif_parser_data->iec_frame_size;
        len -= p_spdif_parser_data->iec_frame_size;
        /* Record all handled data for some usage */
        p_spdif_parser_data->offset += p_spdif_parser_data->iec_frame_size;
    }
    spdif_parser_close(p_spdif_parser_data->handle);

    if (p_buf)
    {
        free(p_buf);
    }
    if (p_audio_buf)
    {
        free(p_audio_buf);
    }

    return ret;
}
#endif
