/*
***********************************************************************
* Copyright (c) 2005-2016, Freescale Semiconductor, Inc.
*
* Copyright 2017-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
/*****************************************************************************
 * demux_ps_version.c
 *
 * Description:
 * Version information for MPEG2 demuxer.
 *
 *
 ****************************************************************************/
#include "mpeg2_parser_api.h"

#define SEPARATOR " "

#define BASELINE_SHORT_NAME "BLN_MAD-MMLAYER_MPG2PARSER_07.00.00"

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

const char* Mpeg2ParserVersionInfo() {
    return (const char*)CODEC_VERSION_STR;
}
