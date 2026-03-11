/*
 ***********************************************************************
 * Copyright (c) 2011-2016, Freescale Semiconductor, Inc.
 * Copyright 2017, 2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#ifndef MPEG2_PARSER_INTERNAL_H
#define MPEG2_PARSER_INTERNAL_H

#if defined __WINCE || defined WIN32
#include <windows.h>
#endif

#include "h264parser.h"
#include "hevcparser.h"
#include "latmParser.h"
#include "mpeg2_parser_api.h"
#include "mpg_demuxer_api.h"
#include "parse_ps_context.h"

#include <stdlib.h>
#include <string.h>

#define MPG2_PARSER_SUPPORT_H264_FIELD_MERGE  // H.264: merge two fields into one frame

#if defined __WINCE
#define MPEG2MSG(fmt, ...) DEBUGMSG(1, (_T(fmt), __VA_ARGS__))
// #define MPEG2MSG printf

#else /* linux platfom */
#ifdef ANDROID_BUILD
#define printf LogOutput
#endif
#ifdef DEBUG
#define MPEG2MSG printf
#else
#define MPEG2MSG(fmt...)
#endif
#endif

#define TESTMALLOC(ptr)                   \
    if ((ptr) == 0) {                     \
        err = PARSER_INSUFFICIENT_MEMORY; \
        goto bail;                        \
    }

#define BAILWITHERROR(v) \
    {                    \
        err = (v);       \
        goto bail;       \
    }

#define SUPPORT_LARGE_MPEG2_FILE

#ifdef SUPPORT_LARGE_MPEG2_FILE
typedef uint64 TIME_STAMP;
typedef uint64 OFFSET;
#else
typedef uint32 TIME_STAMP;
typedef uint32 OFFSET;
#endif

#define MPG2_UNKNOWN_FRAMETYPE 0
#define MPG2_IFRAME_TYPE 1
#define MPG2_PFRAME_TYPE 2
#define MPG2_BFRAME_TYPE 3

#define MAX_PACKET_FRAMES 32
#define NUMCACHEBLOCKS 2
#define CACHEBLOCKBITS (16)

// #define MAXCACHEBLOCK          64

#define MPEG2_MAX_PIC_HEADER_SIZE 0x10
#define MPEG2_MAX_ERR_TIMES 100

typedef struct {
    U32 cacheFilled;      // The valid size of the cache
    U64 cacheFileOffset;  // The read location of the cache,which is the actually the file location
    U8* pCacheBuffer;     // The base address the cache
    U8* pLowAddress;      // The low address of the buffer
    U8* pHighAddress;     // The high address of the buffer
    U8* pReadAddress;  // Where the read starts,which is actually the beginning of the cache buffer
} CACHEBUFFER;

typedef enum {
    STREAMING_MODE,
    FILE_MODE
} MPEG2_PLAYMODE;

typedef enum {
    OUTPUT_BYTRACK,
    OUTPUT_BYFILE
} MPEG2_OUTPUTMODE;

#define INDEX_NOSCANNED (-1)
#define INDEX_NOFOUND (-2)

#define MPEG2_INDEXHEAD_SIZE (36)
#define MIN_SEARCHSTEP (128 * 1024)
#define MIN_SEARCHCOUNT (3)

// keep MPEG2_Index_Head same as the begin of MPEG2_Indexd
typedef struct {
    uint32 version;
    uint32 status;
    uint32 offsetbytes;
    uint32 period;  // period in ms
    uint32 dwTrackIdx;
    uint8 reserved[12];
    uint32 itemcount;
} MPEG2_Index_Head;

typedef struct {
    uint32 version;
    uint32 status;
    uint32 offsetbytes;
    uint32 period;  // period in ms
    uint32 dwTrackIdx;
    uint8 reserved[12];
    uint32 itemcount;
    void* pItem;
    uint64* pts;
    int32 lastitem;  // avoid dead loop
    int32 lastdir;   // avoid dead loop
    int32 lastQueriedRWItem;
    int32 lastQueriedFWItem;
    uint64 rewardPTS;   // PTS record for reward
    uint64 forwardPTS;  // PTS record for forward
    bool indexbreak;
    bool indexdirection;
} MPEG2_Index;

typedef struct INDEX_RANGE_T {
    U64 left;
    U64 right;
    struct INDEX_RANGE_T* next;
} INDEX_RANGE_T;

typedef struct {
    void* appContext;

    FslFileStream sIputStream;
    ParserOutputBufferOps sRequestBufferOps;
    ParserMemoryOps sMemOps;

    FslFileStream* inputStream;
    ParserOutputBufferOps* pRequestBufferOps;
    ParserMemoryOps* memOps;

    FSL_MPG_DEMUX_PSI_T TS_PSI;
    FSL_MPG_DEMUX_SYSINFO_T SystemInfo;
    FSL_MPG_DEMUX_CNXT_T* pDemuxContext;
    CACHEBUFFER CacheBuffer;

    // Streaming or Local File Play
    MPEG2_PLAYMODE playMode;
    MPEG2_OUTPUTMODE outputMode;

    // the file handle
    FslFileHandle fileHandle;
    /*The file size */
    U64 fileSize;
    /*The current fileoffset of parsing the file*/
    U64 fileOffset;

    // uint64 usDuration;/* duration from main avi header 'avih' */
    uint64 usLongestStreamDuration;

    /*Is stream already enabled? or still in the state of parsing header?*/
    bool streamEnabled;
    // bool indexLoaded; /* whether index is loaded from file or imported from outside database */

    S64 PTSDeta;

    MPEG2_Index index[MAX_MPEG2_STREAMS];
    INDEX_RANGE_T* pIndexRange;

    uint64 lastsyncoffset;
    uint32 random_access;
    bool bNeedH264Convert;
    bool bNeedH264SeiPosData;
    bool bForceGetAacAdtsCsd;
    U64 lastFileOffset;
} MPEG2Object, *MPEG2ObjectPtr;

#define MIN_MPEG2_FILE_SIZE 2 * 188

// fix CT40909633
#define SCAN_DURATION_SIZE (8 << 20)  //(1024*1024)

#define MAX_AUDIO_FRAME_SIZE 0x10000
#define MAX_VIDEO_FRAME_SIZE 0x10000
#define MAX_PES_PACKET_SIZE 0x10000
#define MAX_PES_SYNC_SIZE 0x20000
#define PES_SEQ_SYNC_SIZE 0x40
#define MAX_PARSE_HEAD_SIZE \
    0x800000  // 0x200000. some clips(kgan_20120327_1min.ts) sequence start code is at large file
              // offset

// #define MPG2_PROBE_STREAMING_ACCELERATE  //reduce delay as possible for parrot
#define MAX_PARSE_HEAD_SIZE_STREAMMODE (uint32)(5120 << 10)  // for parrot: iMX6
// #define MAX_PARSE_HEAD_SIZE_STREAMMODE    (uint32)(128<<10)  //for parrot: iMX28

#define MPG2_UNKNOEN_STRUCTURE 0x00
#define MPG2_TOP_FIELD 0x01
#define MPG2_BOTTOM_FIELD 0x02
#define MPG2_FRAME_PICTURE 0x03

#define MPG2_EXTENSION_START_CODE 0x000001B5

#define MPG2_PTS_TO_TIMESTAMP(pts) (pts * 100 / 9)

#define MPG2_MAX_SEARCH_FRAME_SIZE 0x6400000  // 100MB

// #define NO_FRAMEBOUNDRY

#ifndef MPEG2_MEM_DEBUG
#define LOCALCalloc(number, size) pDemuxer->memOps->Calloc(number, size)
#define LOCALMalloc(size) pDemuxer->memOps->Malloc(size)
#define LOCALFree(MemoryBlock) pDemuxer->memOps->Free(MemoryBlock)
#define LOCALReAlloc(MemoryBlock, size) pDemuxer->memOps->ReAlloc(MemoryBlock, size)
#endif

#define LocalFileOpen(mode) \
    pDemuxer->inputStream->Open(pDemuxer->fileHanle, mode, pDemuxer->appContext)
#define LocalFileRead(buffer, size) \
    pDemuxer->inputStream->Read(pDemuxer->fileHandle, buffer, size, pDemuxer->appContext)
#define LocalFileSeek(offset, whence) \
    pDemuxer->inputStream->Seek(pDemuxer->fileHandle, offset, whence, pDemuxer->appContext)
#define LocalFileSize() pDemuxer->inputStream->Size(pDemuxer->fileHandle, pDemuxer->appContext)
#define LocalFileClose() pDemuxer->inputStream->Close(pDemuxer->fileHandle, pDemuxer->appContext)

U32 NextNBufferBytes(U8* pInput, int n, U32* Offset);

MPEG2_PARSER_ERROR_CODE Mpeg2ParserMakeHistoryBuffer(MPEG2ObjectPtr pDemuxer,
                                                     FSL_MPEGSTREAM_T* pStreamInfo, uint32 size);

MPEG2_PARSER_ERROR_CODE Mpeg2OutputAC3Frame(U8* sampleData, U32* dataSize, U32* flag, U8* pNewSeg,
                                            U32 newSegSize, FSL_MPEGSTREAM_T* pStreamInfo);

MPEG2_PARSER_ERROR_CODE Mpeg2ScanVideoFrame(MPEG2ObjectPtr pDemuxer, U32* flag, U8* pNewSeg,
                                            U32 newSegSize, FSL_MPEGSTREAM_T* pStreamInfo);

MPEG2_PARSER_ERROR_CODE Mpeg2OutputMpeg2VideoBufferFrame(U8* sampleData, U32* dataSize, U32* flag,
                                                         FSL_MPEGSTREAM_T* pStreamInfo);

MPEG2_PARSER_ERROR_CODE Mpeg2OutputMpeg2VideoFrame(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32 flag,
                                                   U8* pNewSeg, U32 newSegSize);

MPEG2_PARSER_ERROR_CODE Mpeg2OutputNomalData(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32 flag,
                                             U8* pNewSeg, U32 newSegSize);

MPEG2_PARSER_ERROR_CODE Mpeg2OutputAacLatmData(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32 flag,
                                               U8* pNewSeg, U32 newSegSize);

MPEG2_PARSER_ERROR_CODE MPEG2_ParsePES_Process(MPEG2ObjectPtr pDemuxer, U32 streamNum,
                                               U32 fourBytes, U8* pPESbuf, U32 pesBufLen, U32 isTS,
                                               U32 PID);

U32 MPEG2_ParsePES_Scan(MPEG2ObjectPtr pDemuxer, FSL_MPEGSTREAM_T* pStreamInfo, U8* pPESbuf,
                        U32 pesBufLen, U32 isTS, U64* pPTS, U32* flag, U32 streamNum);

MPEG2_PARSER_ERROR_CODE Mpeg2ParserInitialIndex(MPEG2ObjectPtr pDemuxer, uint32 dwTrackIdx);

MPEG2_PARSER_ERROR_CODE Mpeg2ParserUpdateIndex(MPEG2ObjectPtr pDemuxer, uint32 dwTrackIdx,
                                               uint64 offset, uint64 PTS);

MPEG2_PARSER_ERROR_CODE Mpeg2ParserQueryIndex(MPEG2ObjectPtr pDemuxer, uint32 dwTrackIdx,
                                              uint64 PTS, uint32 direction, uint64* pOffset);

MPEG2_PARSER_ERROR_CODE Mpeg2ParserQueryIndexRange(MPEG2ObjectPtr pDemuxer, uint32 dwTrackIdx,
                                                   uint64 PTS, int32* pItems, uint64* pOffsets);

MPEG2_PARSER_ERROR_CODE FoundPESSycnWord(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32* pFourBytes);

MPEG2_PARSER_ERROR_CODE FoundPESSycnWordInBuffer(U8* pBuffer, U32 bufferSize, U32* pOffset,
                                                 U32* pFourBytes);

int MPEG2FindFrameStructure(U8* pStartPos, U32 segSize, bool* pIsSizeEnough);
MPEG2_PARSER_ERROR_CODE MPEG2FindMPEG2Frames(MPEG2ObjectPtr pDemuxer, U8* pNewSeg, U32* pNewSegSize,
                                             FSL_MPEGSTREAM_T* pStreamInfo, U32* pFrameOffsets,
                                             U32* pCount, U32* frameTypes, U8** ppSplitLoc,
                                             S32* pStartFill);
int MPEG2FastFindMPEG2Frames(MPEG2ObjectPtr pDemuxer, U8* pNewSeg, U32 newSegSize,
                             U32 lastFourBytes, FSL_MPEGSTREAM_T* pStreamInfo);

MPEG2_PARSER_ERROR_CODE Mpeg2CreateParserInternal(FslFileStream* stream, ParserMemoryOps* memOps,
                                                  ParserOutputBufferOps* requestBufferOps,
                                                  void* context, uint32 flags,
                                                  FslParserHandle* parserHandle);

MPEG2_PARSER_ERROR_CODE Mpeg2ParseHeaderInternal(MPEG2ObjectPtr parserHandle);

MPEG2_PARSER_ERROR_CODE Mpeg2ParserProcess(MPEG2ObjectPtr pDemuxer, uint32 streamNum,
                                           uint8** sampleData, void** pBufContext, uint32* dataSize,
                                           uint64* usPresTime, uint32* flag);

MPEG2_PARSER_ERROR_CODE Mpeg2ParserProcessFile(MPEG2ObjectPtr pDemuxer, uint32* streamNum,
                                               uint8** sampleData, void** pBufContext,
                                               uint32* dataSize, uint64* usPresTime, uint32* flag);
MPEG2_PARSER_ERROR_CODE Mpeg2ParserScan(MPEG2ObjectPtr pDemuxer, uint32 streamNum,
                                        uint64* fileOffset, uint64* fileOffsetSeeked, uint64* PTS,
                                        uint32* flag, int32 strict);

MPEG2_PARSER_ERROR_CODE Mpeg2ParserScanStreamDuration(MPEG2ObjectPtr pDemuxer, uint32 streamNum);
MPEG2_PARSER_ERROR_CODE Mpeg2ResetStreamInfo(MPEG2ObjectPtr pDemuxer, U32 streamNum,
                                             U64 fileOffset);
MPEG2_PARSER_ERROR_CODE Mpeg2ResetOuputBuffer(MPEG2ObjectPtr pDemuxer, U32 streamNum);
MPEG2_PARSER_ERROR_CODE Mpeg2FlushStreamInternal(MPEG2ObjectPtr pDemuxer, U32 streamNum);
MPEG2_PARSER_ERROR_CODE Mpeg2SeekStream(MPEG2ObjectPtr pDemuxer, uint32 streamNum, uint64* usTime,
                                        uint32 flag);
MPEG2_PARSER_ERROR_CODE MPEG2ParserReadBuffer(MPEG2ObjectPtr pDemuxer, uint32 streamNum, U8** pBuf,
                                              unsigned int size);
MPEG2_PARSER_ERROR_CODE MPEG2ParserNextNBytes(MPEG2ObjectPtr parserHandle, uint32 streamNum,
                                              uint32 n, uint32* pRet);
MPEG2_PARSER_ERROR_CODE MPEG2ParserRewindNBytes(MPEG2ObjectPtr parserHandle, uint32 streamNum,
                                                uint32 n);
MPEG2_PARSER_ERROR_CODE MPEG2ParserForwardNBytes(MPEG2ObjectPtr parserHandle, uint32 streamNum,
                                                 uint32 n);
MPEG2_PARSER_ERROR_CODE MPEG2FileSeek(MPEG2ObjectPtr pDemuxer, uint32 streamNum, int64 offset,
                                      int32 whence);
uint64 MPEG2FilePos(MPEG2ObjectPtr pDemuxer, uint32 streamNum);
uint32 MPEG2FileRead(MPEG2ObjectPtr pDemuxer, uint32 streamNum, U8** pData, uint32 dataSize);

uint64 Mpeg2CalcFileOffset(uint64 usTime, uint64 fileLength, uint64 duration);

MPEG2_PARSER_ERROR_CODE Mpeg2Parser_FillOutputBuf(MPEG2ObjectPtr pDemuxer, uint32 streamNum,
                                                  uint8* pStreamData, uint32* pDataSize,
                                                  uint32 flag, uint64 usPresTime, int32 start_fill,
                                                  uint32 fourbytes);

MPEG2_PARSER_ERROR_CODE Mpeg2Parser_Request_History_OutBuffer(MPEG2ObjectPtr pDemuxer,
                                                              uint32 streamNum);

MPEG2_PARSER_ERROR_CODE Mpeg2Paser_OuputSample_FromArray(OutputBufArray* pOutputArray,
                                                         uint8** sampleData, void** pAppContext,
                                                         uint32* dataSize, uint64* usPresTime,
                                                         uint32* flag, uint32 dump_id);

MPEG2_PARSER_ERROR_CODE Mpeg2OutputMpeg2VideoBufferFrame(U8* sampleData, U32* dataSize, U32* flag,
                                                         FSL_MPEGSTREAM_T* pStreamInfo);

U32 streamNumFromStreamId(MPEG2ObjectPtr pDemuxer, U32 streamId, U32 isTs);
U32 streamNumFromPID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 PID);

typedef int (*ParseStreamFunc)(void* handle, U8* in_data, U32 in_size, U32 flags,
                               U32* consumed_size);
typedef int (*SetFrameBufferFunc)(void* handle, FrameInfo* frame);
typedef int (*GetFrameBufferFunc)(void* handle, FrameInfo* frame);
typedef int (*CreateParserFunc)(H264ParserHandle* pHandle, ParserMemoryOps* pMemOps);
typedef int (*DeleteParserFunc)(H264ParserHandle pHandle);

bool intersectIndexRange(INDEX_RANGE_T* a, INDEX_RANGE_T* b);
bool isIndexRangeContinuous(INDEX_RANGE_T* index, U64 left, U64 right);
void mergeIndexRange(INDEX_RANGE_T** index, U64 left, U64 right);

#endif
