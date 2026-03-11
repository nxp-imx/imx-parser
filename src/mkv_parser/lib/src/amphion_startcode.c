/**
 *  Copyright 2023, 2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "amphion_startcode.h"
#include <string.h>

#define SCODE_NEW_SEQUENCE 0x31
#define SCODE_NEW_PICTURE 0x32
#define SCODE_NEW_SLICE 0x33
#define IMX_CODEC_VERSION_ID 0x1
#define IMX_CODEC_ID_ARV8 0x28
#define IMX_CODEC_ID_ARV9 0x29

static int set_payload_hdr(uint8* dst, uint32 size, uint32 scd_type, RV_INFO* rv_info) {
    unsigned int payload_size;
    /* payload_size = buffer_size + itself_size(16) - start_code(4) */
    payload_size = size + 12;

    dst[0] = 0x00;
    dst[1] = 0x00;
    dst[2] = 0x01;
    dst[3] = scd_type;

    /* length */
    dst[4] = ((payload_size >> 16) & 0xff);
    dst[5] = ((payload_size >> 8) & 0xff);
    dst[6] = 0x4e;
    dst[7] = ((payload_size >> 0) & 0xff);

    /* Codec ID and Version */
    dst[8] = rv_info->codec_id;
    dst[9] = IMX_CODEC_VERSION_ID;

    /* width */
    dst[10] = ((rv_info->width >> 8) & 0xff);
    dst[11] = ((rv_info->width >> 0) & 0xff);
    dst[12] = 0x58;

    /* height */
    dst[13] = ((rv_info->height >> 8) & 0xff);
    dst[14] = ((rv_info->height >> 0) & 0xff);
    dst[15] = 0x50;

    return 0;
}

int get_rv_info(uint8* src, uint32 size, RV_INFO* rv_info) {
    if (rv_info == NULL || src == NULL || size < 12)
        return -1;

    if (strncmp((const char*)(src + 8), "RV30", 4) == 0) {
        rv_info->type = ARV_8;
        rv_info->codec_id = IMX_CODEC_ID_ARV8;
    } else {
        rv_info->type = ARV_9;
        rv_info->codec_id = IMX_CODEC_ID_ARV9;
    }

    return 0;
}

int set_rv_seq(void* dst, uint32 size, RV_INFO* rv_info) {
    if (dst == NULL || rv_info == NULL)
        return -1;

    set_payload_hdr(dst, size, SCODE_NEW_SEQUENCE, rv_info);
    return 0;
}

int set_rv_pic(void* dst, uint32 size, RV_INFO* rv_info) {
    if (dst == NULL || rv_info == NULL)
        return -1;

    set_payload_hdr(dst, size, SCODE_NEW_PICTURE, rv_info);
    return 0;
}

int set_rv_slice(void* dst, uint32 size, RV_INFO* rv_info) {
    if (dst == NULL || rv_info == NULL)
        return -1;

    set_payload_hdr(dst, size, SCODE_NEW_SLICE, rv_info);
    return 0;
}
