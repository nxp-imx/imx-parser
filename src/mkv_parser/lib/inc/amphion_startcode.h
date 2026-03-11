/**
 *  Copyright 2023, 2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef AMPHION_STARTCODE_H
#define AMPHION_STARTCODE_H
#include "fsl_types.h"

enum ARV_FRAME_TYPE {
    ARV_8 = 0,
    ARV_9,
    ARV_10,
};

typedef struct amphion_rv_info {
    bool insert_header;
    enum ARV_FRAME_TYPE type;
    uint32 codec_id;
    uint32 width;
    uint32 height;
} RV_INFO;

#define MALONE_PAYLOAD_HEADER_SIZE 16

int get_rv_info(uint8* data, uint32 size, RV_INFO* rv_info);

int set_rv_seq(void* data, uint32 size, RV_INFO* rv_info);
int set_rv_pic(void* data, uint32 size, RV_INFO* rv_info);
int set_rv_slice(void* data, uint32 size, RV_INFO* rv_info);
#endif
