/*
***********************************************************************
* Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include "err_logs.h"
#include "fsl_types.h"

LOG_CONTROL logcontrol;

#define PARSER_ERROR_ON 1
#define PARSER_WARNNING_ON 0
#define PARSER_DEBUG_ON 0
#define PARSER_INFO_ON 1

#define PARSER_INFO_SEEK_ON 0
#define PARSER_INFO_PTS_ON 0
#define PARSER_INFO_DATASIZE_ON 0
#define PARSER_INFO_FRAMESIZE_ON 0
#define PARSER_INFO_BUFFER_ON 0
#define PARSER_INFO_STREAM_ON 1
#define PARSER_INFO_ERRORCONSEAL_ON 0
#define PARSER_INFO_FILE_ON 0
#define PARSER_INFO_PERF_ON 0

void parser_initlogstatus() {
    logcontrol.level = logcontrol.type = 0;

    if (PARSER_ERROR_ON)
        logcontrol.level |= PARSER_LEVEL_ERROR;
    if (PARSER_WARNNING_ON)
        logcontrol.level |= PARSER_LEVEL_WARNNING;
    if (PARSER_DEBUG_ON)
        logcontrol.level |= PARSER_LEVEL_DEBUG;
    if (PARSER_INFO_ON)
        logcontrol.level |= PARSER_LEVEL_INFO;

    if (PARSER_INFO_SEEK_ON)
        logcontrol.type |= PARSER_INFO_SEEK;
    if (PARSER_INFO_PTS_ON)
        logcontrol.type |= PARSER_INFO_PTS;
    if (PARSER_INFO_DATASIZE_ON)
        logcontrol.type |= PARSER_INFO_DATASIZE;
    if (PARSER_INFO_PERF_ON)
        logcontrol.type |= PARSER_INFO_PERF;
    if (PARSER_INFO_FRAMESIZE_ON)
        logcontrol.type |= PARSER_INFO_FRAMESIZE;
    if (PARSER_INFO_BUFFER_ON)
        logcontrol.type |= PARSER_INFO_BUFFER;
    if (PARSER_INFO_STREAM_ON)
        logcontrol.type |= PARSER_INFO_STREAM;
    if (PARSER_INFO_ERRORCONSEAL_ON)
        logcontrol.type |= PARSER_INFO_ERRORCONSEAL;
    if (PARSER_INFO_FILE_ON)
        logcontrol.type |= PARSER_INFO_FILE;
}

#if !(defined(__WINCE) || defined(WIN32))
void parser_printf(uint32 level, uint32 type, const char* fmt, ...)
#else
void __cdecl parser_printf(uint32 level, uint32 type, const char* fmt, ...)
#endif
{
    if (level & logcontrol.level) {
        if (level != PARSER_LEVEL_INFO) {
            va_list params;
            va_start(params, fmt);
            vfprintf(stderr, fmt, params);
            va_end(params);
            fprintf(stderr, "\n");
        } else if (type & logcontrol.type) {
            va_list params;
            va_start(params, fmt);
            vfprintf(stderr, fmt, params);
            va_end(params);
            fprintf(stderr, "\n");
        }
    }
}
