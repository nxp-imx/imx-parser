/***********************************************************************
 * Copyright 2021, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************/

#include "iec958parser.h"

void iec958_parser_init(iec958_parser_t* p_parser, iec958_parser_search_iec937_header_cb p_fun) {
    memset(p_parser, 0, sizeof(iec958_parser_t));
    p_parser->iec958_parser_search_iec937_header = p_fun;
}

IEC958_FORMAT_TYPE
iec958_parser_get_frame_type(iec958_parser_t* p_parser) {
    return p_parser->iec958_type;
}

SPDIF_RET_TYPE
iec958_parser_set_frame_type(iec958_parser_t* p_parser, IEC958_FORMAT_TYPE format) {
    if ((format != IEC958_FORMAT_TYPE_PCM) && (format != IEC958_FORMAT_TYPE_COMPRESS)) {
        return SPDIF_ERR_PARAM;
    }

    /* Update frame type by current data stream */
    p_parser->iec958_type = format;
    IEC958_LOG_PRINTF("modify frame type: current type = %d, new type = %d\n",
                      p_parser->iec958_type, format);

    return SPDIF_OK;
}

static uint32_t iec958_parser_get_preamble_tab_index(uint32_t sub_frame) {
    if ((sub_frame % IEC958_BLOCK_SUBFRAME_NUM) == 0) {
        return 0;
    }

    return ((sub_frame & 0x01) + 1);
}

static uint32_t iec958_parser_check_frame_preamble(uint8_t* p_buf, uint32_t len, uint8_t step,
                                                   uint32_t* p_out_pos) {
    uint32_t pos = 0;
    uint32_t* p_data = 0;
    uint32_t index = 0;
    uint32_t frame = 0;
    uint8_t preamble[3] = {IEC958_PREAMBLE_BITS_Z, IEC958_PREAMBLE_BITS_X, IEC958_PREAMBLE_BITS_Y};
    uint32_t check_len = len;
    uint32_t move_step = IEC958_SUBFRAME_SIZE;

    *p_out_pos = 0;
    while (check_len >= IEC958_SUBFRAME_SIZE) {
        // IEC958_LOG_PRINTF("iec958_parser_check_frame_preamble: pos = %d, check_len = %d, frame =
        // %d\n", pos, check_len, frame);
        index = iec958_parser_get_preamble_tab_index(frame);
        p_data = (uint32_t*)(p_buf + pos);

        if ((*p_data & IEC958_PREAMBLE_MSK) == preamble[index]) {
            frame++;
            if (frame >= IEC958_BLOCK_SUBFRAME_NUM) {
                break;
            }
            pos += move_step;
            check_len -= move_step;
        } else {
            frame = 0;
            /* Update check buffer address by step */
            pos += step;
            // IEC958_LOG_PRINTF("iec958_parser_check_frame_preamble: Mismatch frame, pos = %d,
            // check_len = %d, len = %d\n", pos, check_len, len);
            /* Reload check length and check again */
            if (len > pos) {
                /* Record check failure number */
                *p_out_pos = pos;
                check_len = len - pos;
            } else {
                *p_out_pos = len;
                break;
            }
        }
    }

    return frame;
}

static SPDIF_RET_TYPE iec958_parser_check_valid_flag(iec958_parser_t* p_parser, uint8_t* p_buf,
                                                     uint32_t len, uint32_t* p_out_pos) {
    uint32_t i = 0;
    uint32_t data = 0;
    uint32_t* p_sub_frame = (uint32_t*)(p_buf);
    uint8_t record_first_pcm_flag = 1;
    uint32_t sub_frame_len = len / IEC958_SUBFRAME_SIZE;

    /* Check parameters */
    *p_out_pos = 0;
    if (!p_parser || !p_buf) {
        return SPDIF_ERR_PARAM;
    }

    if (sub_frame_len < IEC958_BLOCK_SUBFRAME_NUM) {
        IEC958_LOG_PRINTF("SPDIF_ERR_INSUFFICIENT_DATA\n");
        return SPDIF_ERR_INSUFFICIENT_DATA;
    }

    /* Initialize related parameters */
    p_parser->valid_info.valid_flag_pos = -1;
    p_parser->valid_info.valid_flag = IEC958_VALID_FLAG_NO_SET;
    p_parser->valid_info.channel0_cnt = 0;
    p_parser->valid_info.channel1_cnt = 0;

    while (i < sub_frame_len) {
        data = *(p_sub_frame + i);
        if ((data & IEC958_VALID_FLAG_MSK) != IEC958_VALID_FLAG_MSK) {
            if (record_first_pcm_flag) {
                IEC958_LOG_PRINTF("first data = 0x%x, pos = 0x%x\n", data, i << 2);
                p_parser->valid_info.valid_flag_pos = i << 2;
                record_first_pcm_flag = 0;
            }

            if (i & 0x01) {
                p_parser->valid_info.channel1_cnt++;
            } else {
                p_parser->valid_info.channel0_cnt++;
            }
        }
        i++;

        /* update iec958 frame type */
        if ((i % IEC958_BLOCK_SUBFRAME_NUM) == 0) {
            *p_out_pos = i << 2;

            // fix me
            if ((p_parser->valid_info.channel0_cnt + p_parser->valid_info.channel1_cnt) >=
                IEC958_PCM_FORMAT_MATCH_THR) {
                /* record channel num by frame for PCM format */
                if (p_parser->valid_info.channel0_cnt >= IEC958_PCM_FORMAT_MATCH_THR) {
                    p_parser->audio_info.channel_num++;
                }
                if (p_parser->valid_info.channel1_cnt >= IEC958_PCM_FORMAT_MATCH_THR) {
                    p_parser->audio_info.channel_num++;
                }

                p_parser->valid_info.valid_flag = IEC958_VALID_FLAG_0;
#ifndef IEC958_FEATURE_MATCH_FORMAT_BY_DATA
                iec958_parser_set_frame_type(p_parser, IEC958_FORMAT_TYPE_PCM);
#endif
                IEC958_LOG_PRINTF(
                        "pcm: last valid_flag = 0x%x, valid_flag_pos = %d, channel0_cnt = %d, "
                        "channel1_cnt = %d, channel_num = %d, sub_frame_len = 0x%x, p_out_pos = "
                        "0x%x \n",
                        p_parser->valid_info.valid_flag, p_parser->valid_info.valid_flag_pos,
                        p_parser->valid_info.channel0_cnt, p_parser->valid_info.channel1_cnt,
                        p_parser->audio_info.channel_num, sub_frame_len, *p_out_pos);
                break;
            }

            /* Clear status and try to record again */
            record_first_pcm_flag = 1;
            p_parser->valid_info.valid_flag_pos = -1;
            p_parser->valid_info.channel0_cnt = 0;
            p_parser->valid_info.channel1_cnt = 0;
            IEC958_LOG_PRINTF("not pcm: sub_frame_len = 0x%x, i = 0x%x, p_out_pos = 0x%x \n",
                              sub_frame_len, i, *p_out_pos);

            if ((sub_frame_len - i) < IEC958_BLOCK_SUBFRAME_NUM) {
                IEC958_LOG_PRINTF("iec958 frame: remain data number is not enough\n");
                break;
            }
        }
    }

    return SPDIF_OK;
}

uint8_t iec958_parser_check_pcm_info(iec958_parser_t* p_parser) {
    iec958_audio_info_t* p_audio_info = &(p_parser->audio_info);

    if (p_parser->iec958_type == IEC958_FORMAT_TYPE_PCM) {
        if (p_audio_info->channel_num && p_audio_info->sample_rate && p_audio_info->data_length) {
            IEC958_LOG_PRINTF("check iec958 PCM frame success\n");
            return 1;
        } else {
            IEC958_LOG_PRINTF("check iec958 PCM frame failure\n");
            return 0;
        }
    }

    return 1;
}

static SPDIF_RET_TYPE iec958_parser_match_header(iec958_parser_t* p_parser, uint8_t* p_buf,
                                                 uint32_t len, uint32_t* p_out_pos) {
    uint32_t sub_frame = 0;

    /* Check parameters */
    if (!p_parser || !p_buf) {
        *p_out_pos = 0;
        IEC958_LOG_PRINTF("match iec958 header: param error\n");
        return SPDIF_ERR_PARAM;
    }

    if (len < IEC958_BLOCK_SUBFRAME_LEN) {
        *p_out_pos = 0;
        IEC958_LOG_PRINTF("match iec958 header: insufficient data\n");
        return SPDIF_ERR_INSUFFICIENT_DATA;
    }

    /* Search first subframe position and check all frame preamble */
    sub_frame = iec958_parser_check_frame_preamble(p_buf, len, 4, p_out_pos);
    if (sub_frame < IEC958_BLOCK_SUBFRAME_NUM) {
        // IEC958_LOG_PRINTF("Not iec958 frame, pos = 0x%x, len = 0x%x\n", *p_out_pos, len);
        return SPDIF_ERR_INSUFFICIENT_DATA;
    } else {
        IEC958_LOG_PRINTF("iec958 frame match ok, pos = 0x%x\n", *p_out_pos);
    }

    return SPDIF_OK;
}

IEC958_PARSER_STATUS iec958_parser_get_status(iec958_parser_t* p_parser) {
    return p_parser->status;
}

#if 0
// reserved for future
// Add feature for switching pcm to iec937 frame automatically
void 
iec958_parser_set_status(iec958_parser_t *p_parser, IEC958_PARSER_STATUS status)
{
    p_parser->status = status;
}
#endif

void iec958_parser_set_mode(iec958_parser_t* p_parser, IEC958_PARSER_MODE mode) {
    p_parser->mode = mode;
}

SPDIF_RET_TYPE
iec958_parser_search_header(iec958_parser_t* p_parser, void* param, uint8_t* p_buf, uint32_t len,
                            uint32_t* p_out_pos) {
    uint32_t frame_pos = 0;
    uint32_t valid_frame_pos = 0;
    SPDIF_RET_TYPE ret = SPDIF_ERR_INSUFFICIENT_DATA;
    IEC958_PARSER_STATUS status = p_parser->status;
    uint8_t check_preamble = 1;

    *p_out_pos = 0;
    IEC958_LOG_PRINTF("parser current status = %d\n", status);
    switch (status) {
        case IEC958_PARSER_STATUS_MATCH: {
            ret = iec958_parser_match_header(p_parser, p_buf, len, &frame_pos);
            *p_out_pos = frame_pos;
            if (ret != SPDIF_OK) {
                IEC958_LOG_PRINTF("IEC958_PARSER_STATUS_MATCH: err ret= %d\n", ret);
                return ret;
            }
            ret = SPDIF_ERR_INCOMPLETE;

            p_parser->status = IEC958_PARSER_STATUS_CHECK_CHANNEL_STATUS;
            check_preamble = 0;
        }
        __attribute__((fallthrough));
        case IEC958_PARSER_STATUS_CHECK_CHANNEL_STATUS: {
            *p_out_pos = frame_pos;
            /* get channel status data, and then check validity flag */
            ret = iec958_parser_update_channel_status(p_parser, check_preamble, p_buf + frame_pos,
                                                      len - frame_pos);
            if (ret != SPDIF_OK) {
                IEC958_LOG_PRINTF("get channel status data: err ret = %d\n", ret);
                return ret;
            }
            ret = SPDIF_ERR_INCOMPLETE;

            if (p_parser->mode == IEC958_COMPRESS_PARSER_MODE) {
                /* switch to iec937 format parsing status */
                p_parser->status = IEC958_PARSER_STATUS_PARSING_IEC937;
                iec958_parser_set_frame_type(p_parser, IEC958_FORMAT_TYPE_COMPRESS);
            } else if (p_parser->mode == IEC958_PCM_PARSER_MODE) {
                /* switch to check channel status parsing status */
                p_parser->status = IEC958_PARSER_STATUS_CHECK_VALID;
                iec958_parser_set_frame_type(p_parser, IEC958_FORMAT_TYPE_PCM);
            } else {
                /* Auto mode */
                if (p_parser->audio_info.frame_id == IEC958_FORMAT_TYPE_COMPRESS) {
                    p_parser->status = IEC958_PARSER_STATUS_PARSING_IEC937;
                    iec958_parser_set_frame_type(p_parser, IEC958_FORMAT_TYPE_COMPRESS);
                    IEC958_LOG_PRINTF("channel status update, compress data\n");
                } else {
                    p_parser->status = IEC958_PARSER_STATUS_CHECK_VALID;
                }
            }
            IEC958_LOG_PRINTF("parser status change: %d -> %d, pos = 0x%x, len = 0x%x\n", status,
                              p_parser->status, frame_pos, len);
        } break;
        case IEC958_PARSER_STATUS_CHECK_VALID: {
            *p_out_pos = frame_pos;
            /* Check frame validity flag and analyze audio type: PCM, iec937 or unused data */
            ret = iec958_parser_check_valid_flag(p_parser, p_buf + frame_pos, len - frame_pos,
                                                 &valid_frame_pos);

            /* Update actual length for compress data check */
            // len = valid_frame_pos + frame_pos;

            if (ret != SPDIF_OK) {
                IEC958_LOG_PRINTF(
                        "iec958_parser_check_valid_flag, err ret = %d, valid_frame_pos = %d\n", ret,
                        valid_frame_pos);
                return ret;
            }
            ret = SPDIF_ERR_INCOMPLETE;

            /* switch to PCM format parsing mode */
            if (iec958_parser_get_frame_type(p_parser) == IEC958_FORMAT_TYPE_PCM) {
                /* Check PCM audio information */
                if (iec958_parser_check_pcm_info(p_parser)) {
                    p_parser->status = IEC958_PARSER_STATUS_CHECK_PCM;
                } else {
                    p_parser->status = IEC958_PARSER_STATUS_CHECK_CHANNEL_STATUS;
                    *p_out_pos = frame_pos + IEC958_BLOCK_SUBFRAME_LEN;
                    IEC958_LOG_PRINTF("parser status = %d, retry to get pcm information\n", status);
                    break;
                }

                /* record audio position */
                if (p_parser->valid_info.valid_flag_pos > 0) {
                    *p_out_pos += p_parser->valid_info.valid_flag_pos;
                }
                IEC958_LOG_PRINTF("parser status change: %d -> %d, pos = 0x%x\n", status,
                                  p_parser->status, *p_out_pos);
                ret = SPDIF_OK;
                break;
            } else {
                /* switch to detect format status */
                p_parser->status = IEC958_PARSER_STATUS_DETECT_FORMAT;
                p_parser->pos = 0;
            }
        }
        __attribute__((fallthrough));
        case IEC958_PARSER_STATUS_DETECT_FORMAT: {
            if (p_parser->pos >= IEC958_PARSER_MATCH_FORMAT_DATA_THR) {
                IEC958_LOG_PRINTF("The detected data exceeds the threshold, pos = 0x%x\n",
                                  p_parser->pos);
                iec958_parser_set_frame_type(p_parser, IEC958_FORMAT_TYPE_PCM);

                /* Check PCM audio information */
                if (iec958_parser_check_pcm_info(p_parser)) {
                    p_parser->status = IEC958_PARSER_STATUS_CHECK_PCM;
                    ret = SPDIF_OK;
                } else {
                    IEC958_LOG_PRINTF("parser status = %d, retry to get pcm information\n", status);
                    p_parser->status = IEC958_PARSER_STATUS_CHECK_CHANNEL_STATUS;
                    ret = SPDIF_ERR_INCOMPLETE;
                }
                break;
            }
        }
        __attribute__((fallthrough));
        case IEC958_PARSER_STATUS_PARSING_IEC937: {
            /* Check parameters */
            if (!p_parser->iec958_parser_search_iec937_header) {
                IEC958_LOG_PRINTF("unregister iec937 header search function\n");
                ret = SPDIF_ERR_UNREGISTER_FUN;
                break;
            }

            /* Check iec937 frame */
            ret = p_parser->iec958_parser_search_iec937_header(param, p_buf + frame_pos,
                                                               len - frame_pos, p_out_pos);
            *p_out_pos += frame_pos;
            // IEC958_LOG_PRINTF("IEC958_PARSER_STATUS_PARSING_IEC937, frame_pos = 0x%x, len =
            // 0x%x\n", frame_pos, len - frame_pos);

            /* record check length */
            p_parser->pos += *p_out_pos;

            if (ret != SPDIF_OK) {
                IEC958_LOG_PRINTF("iec958_parser_search_iec937_header: ret= %d, pos = %u\n", ret,
                                  *p_out_pos);
                break;
            } else {
                /* Update frame type */
                iec958_parser_set_frame_type(p_parser, IEC958_FORMAT_TYPE_COMPRESS);

                /* compress data format */
                p_parser->status = IEC958_PARSER_STATUS_PARSING_IEC937;
                if (status != p_parser->status) {
                    IEC958_LOG_PRINTF("parser status change: %d -> %d, pos = 0x%x\n", status,
                                      p_parser->status, *p_out_pos);
                } else {
                    IEC958_LOG_PRINTF("parser status: %d\n", p_parser->status);
                }
            }
        } break;
        case IEC958_PARSER_STATUS_CHECK_PCM: {
            // TODO: Add feature for switching pcm to iec937 frame automatically
            *p_out_pos = 0;
            ret = SPDIF_OK;
            IEC958_LOG_PRINTF("parser status: IEC958_PARSER_STATUS_CHECK_PCM \n");
        } break;
        default:
            break;
    }
    return ret;
}

SPDIF_RET_TYPE
iec958_parser_read_sample(uint8_t* src, uint8_t* dst, uint32_t sample_width, uint32_t src_len,
                          uint32_t* p_dst_len) {
    uint32_t* src1 = (uint32_t*)src;
    uint32_t data = 0;
    int32_t sample = 0;
    uint32_t i = 0;

    if (!src || !dst || !src_len || (src_len & 0x03)) {
        IEC958_LOG_PRINTF("SPDIF_ERR_PARAM\n");
        return SPDIF_ERR_PARAM;
    }

    if (sample_width == 16) {
        uint16_t* dst1 = (uint16_t*)dst;
        while (i < src_len) {
            data = *src1++;
            data &= ~0xf;
            data <<= 4;
            sample = (int32_t)data;
            *dst1++ = sample >> 16;
            i += 4;
        }
        *p_dst_len = src_len >> 1;
    } else if (sample_width == 24) {
        // uint32_t *dst1= (uint32_t *)dst;
        while (i < src_len) {
            data = *src1++;
            data &= ~0xf;
            data <<= 4;
            sample = (int32_t)data;
            //*dst1++ = (sample >> 8) & 0xFFFFFF;
            *dst++ = (sample >> 8) & 0xFF;
            *dst++ = (sample >> 16) & 0xFF;
            *dst++ = (sample >> 24) & 0xFF;
            i += 4;
        }
        *p_dst_len = (src_len >> 2) * 3;
    } else if (sample_width == 8) {
        while (i < src_len) {
            data = *src1++;
            data &= ~0xf;
            data <<= 4;
            sample = (int32_t)data;
            *dst++ = (sample >> 24) & 0xFF;
            i += 4;
        }
        *p_dst_len = src_len >> 2;
    } else {
        return SPDIF_ERR_PARAM;
    }

    return SPDIF_OK;
}

SPDIF_RET_TYPE
iec958_parser_update_channel_status(iec958_parser_t* p_parser, uint8_t is_check_preamble,
                                    uint8_t* p_buf, uint32_t len) {
    /* Check preamble */
    uint32_t out_pos = 0;
    uint32_t sub_frame = 0;
    /* update channel status */
    uint32_t* p_data = 0;
    uint32_t bit_value = 0;
    uint32_t byte_pos = 0;
    uint32_t bit_pos = 0;
    uint32_t pos = 0;

    /* Search first subframe position and check all frame preamble */
    if (is_check_preamble) {
        sub_frame = iec958_parser_check_frame_preamble(p_buf, len, 4, &out_pos);
        if (sub_frame < IEC958_BLOCK_SUBFRAME_NUM) {
            IEC958_LOG_PRINTF("No sufficient sub frame = %d, frame pos = 0x%x\n", sub_frame,
                              out_pos);
            return SPDIF_ERR_INSUFFICIENT_DATA;
        }
    }

    p_data = (uint32_t*)(p_buf + out_pos);
    for (pos = 0; pos < IEC958_BLOCK_SUBFRAME_NUM; pos += 2) {
        bit_value = *(p_data + pos) & IEC958_CHANNEL_STATUS_MSK;
        byte_pos = pos >> (1 + 3);
        bit_pos = (pos >> 1) & 0x07;

        if (bit_value) {
            p_parser->channel_status[byte_pos] |= 1 << bit_pos;
        } else {
            p_parser->channel_status[byte_pos] &= ~(1 << bit_pos);
        }
    }

    for (uint8_t j = 0; j < 24; j++) {
        IEC958_LOG_PRINTF("channel status: data[%d]= 0x%x \n", j, p_parser->channel_status[j]);
    }

    iec958_parser_get_audio_info(p_parser);
    return SPDIF_OK;
}

static uint32_t get_param_bit_mask(uint32_t width) {
    uint32_t bit_mask = 0;

    while (width--) {
        bit_mask <<= 1;
        bit_mask |= 1;
    }
    return bit_mask;
}

static uint32_t swap_bit(uint32_t bit_value, uint32_t bit_width) {
    uint32_t data = 0;
    uint8_t i = 0;

    while (i < bit_width) {
        data <<= 1;
        data |= (bit_value >> i) & 0x01;
        i++;
    }
    return data;
}

void iec958_parser_get_audio_info(iec958_parser_t* p_parser) {
    uint8_t data = 0;
    uint8_t bit_value = 0;
    uint32_t bit_mask = 0;
    uint32_t param = 0;
    iec958_audio_info_t* p_audio_info = &p_parser->audio_info;
    uint32_t sample_rate_tab[16] = {44100, 88200,  22050, 176400, 48000, 96000, 24000, 192000,
                                    0,     768000, 0,     0,      32000, 0,     0,     0};
    uint32_t data_len_tab[16] = {0, 19, 18, 17, 16, 20, 0, 0, 0, 23, 22, 21, 20, 24, 0, 0};

/* channel parameters definition */
#define FRAME_TYPE_INDEX (1 << 16 | 0x01)
#define CHANNEL_INFO_INDEX (3 << 16 | 0x03)
#define SOURCE_NUMBER_INDEX (16 << 16 | 0x04)
#define SAMPLE_RATE_INDEX (24 << 16 | 0x04)
#define DATA_LENGTH_INDEX (32 << 16 | 0x04)

/* parameters extract definition */
#define GET_PARAM_BYTE_POS(x) (x >> (16 + 3))
#define GET_PARAM_BIT_POS(x) ((x >> 16) & 0x07)
#define GET_PARAM_WIDTH(x) (x & 0xFF)

    /* frame identification */
    param = FRAME_TYPE_INDEX;
    data = p_parser->channel_status[GET_PARAM_BYTE_POS(param)] >> GET_PARAM_BIT_POS(param);
    bit_mask = get_param_bit_mask(GET_PARAM_WIDTH(param));
    bit_value = data & bit_mask;
    if (bit_value) {
        p_audio_info->frame_id = IEC958_FORMAT_TYPE_COMPRESS;
    } else {
        p_audio_info->frame_id = IEC958_FORMAT_TYPE_PCM;
    }

    /* Channel and pre-emphasis */
    // param = CHANNEL_INFO_INDEX;
    // data = p_parser->channel_status[GET_PARAM_BYTE_POS(param)] >> GET_PARAM_BIT_POS(param);
    // bit_mask = get_param_bit_mask(GET_PARAM_WIDTH(param));
    // bit_value = data & bit_mask;

    /* sample rate */
    param = SAMPLE_RATE_INDEX;
    data = p_parser->channel_status[GET_PARAM_BYTE_POS(param)] >> GET_PARAM_BIT_POS(param);
    bit_mask = get_param_bit_mask(GET_PARAM_WIDTH(param));
    bit_value = swap_bit(data & bit_mask, GET_PARAM_WIDTH(param));
    p_audio_info->sample_rate = sample_rate_tab[bit_value];

    /* data length */
    param = DATA_LENGTH_INDEX;
    data = p_parser->channel_status[GET_PARAM_BYTE_POS(param)] >> GET_PARAM_BIT_POS(param);
    bit_mask = get_param_bit_mask(GET_PARAM_WIDTH(param));
    bit_value = swap_bit(data & bit_mask, GET_PARAM_WIDTH(param));
    p_audio_info->data_length = data_len_tab[bit_value];
    /* Configure the default data length if it is not indicated */
    if (!(bit_value & 0x07)) {
        p_audio_info->data_length = p_parser->audio_info.default_word_length;
    }

    IEC958_LOG_PRINTF("frame_id = %d, channel_num = %d, sample_rate = %d, data length = %d \n",
                      p_audio_info->frame_id, p_audio_info->channel_num, p_audio_info->sample_rate,
                      p_audio_info->data_length);
}

SPDIF_RET_TYPE
iec958_parser_set_default_word_length(iec958_parser_t* p_parser, uint32_t word_length) {
    uint32_t i;
    uint32_t default_word_length_tab[] = {8, 16, 24};
    uint32_t len = sizeof(default_word_length_tab) / sizeof(default_word_length_tab[0]);

    for (i = 0; i < len; i++) {
        if (word_length == default_word_length_tab[i]) {
            break;
        }
    }

    if (i >= len) {
        IEC958_LOG_PRINTF("Default word length: %d is not supported", word_length);
        return SPDIF_ERR_PARAM;
    } else {
        p_parser->audio_info.default_word_length = word_length;
        IEC958_LOG_PRINTF("Configure iec958 pcm default word length: %d", word_length);
        return SPDIF_OK;
    }
}
