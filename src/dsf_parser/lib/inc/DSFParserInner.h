/*
***********************************************************************
* Copyright 2018, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
/*****************************************************************************
* DsfParserInner.h
*
* Description:
* Internal information for DSF parser.
*
****************************************************************************/

#ifndef DSF_PARSER_INNER_H
#define DSF_PARSER_INNER_H

#include "ID3Parser.h"

#define MAX_TRACK_NUM 1

typedef struct
{
    bool    m_bEnable;
    uint32  m_dwChnNum;
    uint32  m_dwBitRate;
    uint32  m_dwSampleRate;
    uint32  m_dwSampleBits;
    uint64  m_qwDuration;
    uint64  m_qwCurStamp;
    uint32  m_dwCodecType;
    uint32  m_dwCodecSubType;
}DSFTrack, *DSFTrackPrt;

typedef struct
{
    FslFileStream m_tStreamOps;
    ParserMemoryOps m_tMemOps;
    ParserOutputBufferOps m_tBufferOps;
    void *m_context;
    FslFileHandle m_fileHandle;
    uint64 m_qwFileSize;

    uint64 m_qwDataBeginOffset;
    uint64 m_qwDataEndOffset;

    uint64 m_qwMetaDataBeginOffset;
    uint64 m_qwMetaDataEndOffset;

    bool m_bBeyondDataChunk;
    DSFTrack m_DSFTrack;
    uint8 * m_pBlockBuffer;
    ID3Parser hID3;
    bool bEnableConvert;
}DSFObj, *DSFObjPtr;

#define TESTMALLOC(ptr) \
    if ( (ptr) == 0 ) \
{ \
    err = PARSER_INSUFFICIENT_MEMORY; \
    goto bail; \
}

#define BAILWITHERROR(v) \
{ \
    err = (v); \
    goto bail; \
}

#ifdef ANDROID
    #include "android/log.h"
    #ifdef DEBUG
        #define DSFMSG(...) __android_log_print(ANDROID_LOG_INFO, "FLV PARSER", __VA_ARGS__)
    #else
        #define DSFMSG(...)
    #endif

#else
    #ifdef DEBUG
        #define DSFMSG(format,...) printf(format, ##__VA_ARGS__)
    #else
        #define DSFMSG(...)
    #endif
#endif

#define SAFE_DELETE(p) do{if(p){self->m_tMemOps.Free(p); p = NULL;}} while(0)

#endif //WAV_PARSER_INNER_H
