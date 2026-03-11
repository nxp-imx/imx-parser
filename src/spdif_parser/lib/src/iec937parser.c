/***********************************************************************
 * Copyright 2021, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************/

#include "iec937parser.h"

void iec937_parser_init(iec937_parser_t* p_parser) {
    memset(p_parser, 0, sizeof(iec937_parser_t));
}

static uint16_t iec937_parser_convert_data(uint32_t* p_buf) {
    uint32_t data = 0;

    data = *p_buf;
    data >>= 12;
    data &= 0xFFFF;
    return ((uint16_t)data);
}

static uint16_t iec937_parser_extract_data(uint16_t* p_buf, uint8_t step) {
    if (step == 1) {
        return (*p_buf);
    } else if (step == 4) {
        return iec937_parser_convert_data((uint32_t*)p_buf);
    } else {
        return 0;
    }
}

static int32_t iec937_parser_search(uint8_t* p_buf, uint32_t len, uint16_t data, uint8_t step) {
    uint32_t i = 0;
    uint32_t loop = len / step;

    if (!p_buf || !len || !loop) {
        return -1;
    }

    if (step == 1) {
        for (i = 0; i < (loop - 1); i++) {
            if (*((uint16_t*)(p_buf + i)) == data) {
                return i;
            }
        }

        /* avoid miss lower byte of PA */
        if ((*(p_buf + loop - 1) == IEC937_PA_LOW_BYTE) && (*(p_buf + loop - 2) == 0) &&
            (*(p_buf + loop - 3) == 0) && (*(p_buf + loop - 4) == 0) &&
            (*(p_buf + loop - 5) == 0)) {
            return loop - 1;
        }
    } else if (step == 4) {
        for (i = 0; i < loop; i++) {
            if (iec937_parser_convert_data((uint32_t*)p_buf + i) == data) {
                return (i * step);
            }
        }
    } else {
        return -1;
    }

    return -1;
}

static void iec937_parser_update_format_type(iec937_parser_t* p_parser, uint8_t* p_buf,
                                             uint8_t step) {
    /* Pc: bit 13-15 - stream number (0)
     *     bit 11-12 - reserved (0)
     *     bit  8-10 - bsmod from AC3 frame */
    /* Pc: bit    7  - error bit (0)
     *     bit  5-6  - subdata type (0)
     *     bit  0-4  - data type */
    p_parser->format_type = iec937_parser_extract_data((uint16_t*)p_buf, step) & 0x1F;
    // TODO: add some format handle code
}

IEC937_FORMAT_TYPE
iec937_parser_get_format_type(iec937_parser_t* p_parser) {
    return (IEC937_FORMAT_TYPE)(p_parser->format_type);
}

static void iec937_parser_update_audio_len(iec937_parser_t* p_parser, uint8_t* p_buf,
                                           uint8_t step) {
    uint16_t data = iec937_parser_extract_data((uint16_t*)p_buf, step);
    IEC937_FORMAT_TYPE type = p_parser->format_type;

    /* Pd: bit 15-0  - frame size in bits */
    /* EAC3 is frame size in bytes */
    if (type == IEC937_FORMAT_TYPE_EAC3) {
        p_parser->audio_len = data;
    } else {
        p_parser->audio_len = data >> 3;
    }
    // TODO: add some format handle code
}

/**
 * Description: search iec937 header
 * @p_parser: iec937 parser data structure
 * @p_buf: a buffer pointer
 * @len: buffer length
 * @step: check step and the value can be 1 or 4.
 *  1: iec937 stream and check by byte
 *  4: iec958 stream and check by word
 * @p_out_pos[out]: iec937 header position in current buffer
 * Returns: search status.
 */
SPDIF_RET_TYPE
iec937_parser_search_header(iec937_parser_t* p_parser, uint8_t* p_buf, uint32_t len, uint8_t step,
                            uint32_t* p_out_pos) {
    int32_t pos = -1;
    uint32_t offset = 0;
    uint32_t check_len = 0;
    uint32_t shift_width = (step == 1) ? 2 : 4;

    if ((step != 1) && (step != 4)) {
        *p_out_pos = 0;
        return SPDIF_ERR_PARAM;
    }

    check_len = IEC937_SYNCWORD_BYTE_LEN * ((step == 4) ? 2 : 1);
    if (!p_buf || len < check_len) {
        *p_out_pos = 0;
        return SPDIF_ERR_PARAM;
    }

parser_start:
    /* Search first keyword */
    pos = iec937_parser_search(p_buf, len, IEC937_PA, step);
    if (pos < 0) {
        *p_out_pos = len;
        IEC937_LOG_PRINTF("ret = IEC937_ERROR_PA, pos = 0x%x\n", *p_out_pos);
        return SPDIF_ERR_IEC937_PA;
    }

    *p_out_pos = pos;
    /* Check remain length */
    if (len - pos < check_len) {
        IEC937_LOG_PRINTF("ret = IEC937_INSUFFICIENT_DATA\n");
        return SPDIF_ERR_INSUFFICIENT_DATA;
    }

    /* If position is not zero, it should have zero frame */
    if (pos) {
        uint32_t frame_num = pos / step;
        uint8_t* p_pre_frame = p_buf + pos;

        frame_num = (frame_num < 4) ? frame_num : 4;
        while (frame_num--) {
            p_pre_frame -= shift_width;
            if (iec937_parser_extract_data((uint16_t*)p_pre_frame, step)) {
                *p_out_pos = pos + shift_width;
                IEC937_LOG_PRINTF("check zero frame before PA error, pos = 0x%x, frame_num = %d\n",
                                  pos, pos / step);
                return SPDIF_ERR_IEC937_PA;
            }
        }
    }

    /* Check PB */
    pos += shift_width;
    if (iec937_parser_extract_data((uint16_t*)(p_buf + pos), step) != IEC937_PB) {
        /* Search again */
        offset = pos + step;
        len -= offset;
        p_buf += offset;
        goto parser_start;
    }

    /* Pc */
    pos += shift_width;
    iec937_parser_update_format_type(p_parser, p_buf + pos, step);

    /* Pd */
    pos += shift_width;
    iec937_parser_update_audio_len(p_parser, p_buf + pos, step);

    IEC937_LOG_PRINTF("pos = 0x%x, format_type = 0x%x, audio_len = 0x%x, shift_width = 0x%x\n", pos,
                      p_parser->format_type, p_parser->audio_len, shift_width);

    return SPDIF_OK;
}

uint32_t iec937_parser_read_frame_size(iec937_parser_t* p_parser) {
    IEC937_FORMAT_TYPE type = p_parser->format_type;

    switch (type) {
        case IEC937_FORMAT_TYPE_AC3:
            return IEC937_PAYLOAD_SIZE_AC3;
        case IEC937_FORMAT_TYPE_EAC3:
            return IEC937_PAYLOAD_SIZE_EAC3;
        case IEC937_FORMAT_TYPE_MPEG1L1:
            return IEC937_PAYLOAD_SIZE_MPEG1L1;
        case IEC937_FORMAT_TYPE_MPEG1L23:
            return IEC937_PAYLOAD_SIZE_MPEG1L23;
        case IEC937_FORMAT_TYPE_MPEG2_4_AAC:
            return IEC937_PAYLOAD_SIZE_AAC;
        // TODO: add more case
        default:
            return 0;
    }
}

uint32_t iec937_parser_read_audio_data_len(iec937_parser_t* p_parser) {
    return p_parser->audio_len;
}

SPDIF_RET_TYPE
iec937_parser_read_audio_data(uint8_t* src, uint8_t* dst, uint32_t len, uint8_t step) {
    uint32_t i = 0;
    uint32_t data = 0;
    uint16_t swap_data = 0;
    uint16_t* dst16 = (uint16_t*)dst;
    uint32_t shift_width = (step == 1) ? 2 : 4;

    if (!src || !dst || (len < shift_width)) {
        return SPDIF_ERR_PARAM;
    }

    if (step == 1) {
        uint16_t* src16 = (uint16_t*)src;
        while (i <= len) {
            data = *src16++;
            swap_data = (data >> 8) & 0xFF;
            swap_data += (data & 0xFF) << 8;
            *dst16++ = swap_data;
            i += 2;
        }
    } else if (step == 4) {
        uint32_t* src32 = (uint32_t*)src;
        while (i < len) {
            data = *src32++;
            data >>= 12;
            data &= 0xFFFF;
            swap_data = (data >> 8) & 0xFF;
            swap_data += (data & 0xFF) << 8;
            *dst16++ = swap_data;
            i += 4;
        }
    } else {
        return SPDIF_ERR_PARAM;
    }

    return SPDIF_OK;
}
