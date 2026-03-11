/***********************************************************************
 * Copyright 2021, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************/

#ifndef _SPDIF_PARSER_TYPES_H
#define _SPDIF_PARSER_TYPES_H
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// #include "stdio.h"
/* data type definition */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef int int32_t;

typedef enum {
    SPDIF_OK = 0,
    SPDIF_ERR_PARAM = -1,
    SPDIF_ERR_INSUFFICIENT_DATA = -2,
    SPDIF_ERR_HEADER = -3,
    SPDIF_ERR_IEC937_PA = -4,
    SPDIF_ERR_READ_LEN = -5,
    SPDIF_ERR_IEC958_PREAMBLE = -6,
    SPDIF_ERR_UNREGISTER_FUN = -7,
    SPDIF_EOS = -8,
    SPDIF_ERR_INCOMPLETE = -9,
    SPDIF_ERR_UNKNOWN = -10,
} SPDIF_RET_TYPE;

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif
