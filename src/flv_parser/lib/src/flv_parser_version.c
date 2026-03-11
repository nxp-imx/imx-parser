/*****************************************************************************
 * Copyright (c) 2010-2014, Freescale Semiconductor Inc.
 * Copyright 2017-2018, 2020, 2023-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 *
 ****************************************************************************/
#include "flv_parser_api.h"

#define SEPARATOR " "

#define BASELINE_SHORT_NAME "BLN_MAD-MMLAYER_FLVPARSER_02.00.00"

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

const char* FLVParserVersionInfo() {
    return (const char*)CODEC_VERSION_STR;
}
