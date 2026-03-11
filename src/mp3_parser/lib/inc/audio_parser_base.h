/*
***********************************************************************
* Copyright (c) 2013, Freescale Semiconductor, Inc.
*
* Copyright 2020, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef __AUDIO_PARSER_BASE_H__
#define __AUDIO_PARSER_BASE_H__

#include "fsl_types.h"
#include "fsl_parser.h"
#include "AudioCoreParser.h"
#include "AudioIndexTable.h"
#include "ID3Parser.h"

#define MAXAUDIODURATIONHOUR (1024)
#define AUDIO_PARSER_READ_SIZE (16 * 1024)
#define AUDIO_PARSER_SEGMENT_SIZE (32 * 1024)

#define PARSERAUDIO_HEAD 0
#define PARSERAUDIO_MIDDLE 1
#define PARSERAUDIO_TAIL 2
#define PARSERAUDIO_BEGINPOINT 3
#define PARSERAUDIO_VBRDURATION 4

#define MP3_DURATION_SCAN_THRESHOLD (6 * 1024 * 1024)

typedef struct {
    uint32 nVersion;             // Index Table format version
    uint32 nIndexItemSize;       // size in byte
    uint32 nIndexItemTimeScale;  // unit in ms
    uint32 nIndexCount;          // total number of item
    uint64 qwDuration;           // unit in us
    uint8 sReserved[8];          // reserved area
    uint8 IndexItemList[];
} IndexTableHdr_t;

typedef struct {
    FslFileStream fileOps;
    FslFileHandle sourceFileHandle;
    ParserMemoryOps memoryOps;
    void* appContext;

    AUDIO_PARSERRETURNTYPE (*ParserFileHeader)(AUDIO_FILE_INFO* pFileInfo, uint8* pBuffer,
                                               uint32 nBufferLen);
    AUDIO_PARSERRETURNTYPE (*ParserFrame)(AUDIO_FRAME_INFO* pFrameInfo, uint8* pBuffer,
                                          uint32 nBufferLen);
    AUDIO_PARSERRETURNTYPE (*GetFrameSize)(uint32 header, uint32* frame_size,
                                           int* out_sampling_rate, int* out_channels,
                                           int* out_bitrate, int* out_num_samples);

} Parser_Input_Params_t;

typedef struct {
    uint64 nBeginPoint;
    uint64 nBeginPointOffset;
    uint64 nReadPoint;
    uint64 nEndPoint;
    uint64 nFileSize;

    ID3Parser hID3;

    bool bCBR;
    bool bVBRDurationReady;
    bool bBeginPointFounded;
    uint32 nAvrageBitRate;
    uint8* pTmpBufferPtr;
    uint32 nOverlap;

    uint64 usDuration;
    uint64 nTotalBitRate;
    uint32 nFrameCount;

    AITHandle hSeekTable;
    int64 nSource2CurPos;

    int64 sAudioSeekPos;

    uint32 secctr;
    uint32 minctr;
    uint32 hourctr;
    uint32 nOneSecondSample;
    uint32 nSampleRate;
    uint32 nChannels;
    bool bNeedSendCodecConfig;

    uint64 nSamplesRead;
    uint64 nCurrentTimeUs;

    AUDIO_FILE_INFO FileInfo;
    AUDIO_FRAME_INFO FrameInfo;
    bool bSegmentStart;
    bool bTOCSeek;

    FslFileStream fileOps;
    FslFileHandle sourceFileHandle;
    ParserMemoryOps memoryOps;
    void* appContext;

    uint32 LiveFlag;
    bool bEnableConvert;

    AUDIO_PARSERRETURNTYPE (*ParserFileHeader)(AUDIO_FILE_INFO* pFileInfo, uint8* pBuffer,
                                               uint32 nBufferLen);
    AUDIO_PARSERRETURNTYPE (*ParserFrame)(AUDIO_FRAME_INFO* pFrameInfo, uint8* pBuffer,
                                          uint32 nBufferLen);
    AUDIO_PARSERRETURNTYPE (*GetFrameSize)(uint32 header, uint32* frame_size,
                                           int* out_sampling_rate, int* out_channels,
                                           int* out_bitrate, int* out_num_samples);
} Audio_Parser_Base_t;

int32 AudioParserBaseCreate(Audio_Parser_Base_t* pParserBase, Parser_Input_Params_t* pParamList);
int32 AudioParserBaseDestroy(Audio_Parser_Base_t* pParserBase);

int32 AudioParserFileHeader(Audio_Parser_Base_t* pParserBase);
int32 ParserThreeSegmentAudio(Audio_Parser_Base_t* pParserBase);

uint32 ParserAudioFrame(Audio_Parser_Base_t* pParserBase, uint8* pBuffer, uint32 nBufferSize,
                        uint32 nSegmentCnt);
uint32 ParserAudioFrameOverlap(Audio_Parser_Base_t* pParserBase, uint8* pBuffer, uint32 nBufferSize,
                               uint32 nSegmentCnt);

uint8* AudioParserGetBuffer(Audio_Parser_Base_t* pParserBase, uint32 nBufferSize);
uint32 AudioParserFreeBuffer(Audio_Parser_Base_t* pParserBase, uint8* pBuffer);

int32 ParserCalculateVBRDuration(Audio_Parser_Base_t* pParserBase);
int32 AudioParserBuildSeekTable(Audio_Parser_Base_t* pParserBase, int32 nOffset, uint32 nSamples,
                                uint32 nSamplingRate);

int32 ParserFindBeginPoint(Audio_Parser_Base_t* pParserBase);

uint32 GetAvrageBitRate(Audio_Parser_Base_t* pParserBase);

int32 GetNextSample(Audio_Parser_Base_t* pParserBase, ParserOutputBufferOps* pBufOps,
                    void* appContext, uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                    uint64* usStartTime, uint64* usDuration, uint32* sampleFlags);

int32 Seek(Audio_Parser_Base_t* pParserBase, uint64* nSeekTime, uint32 flag);

uint32 GetIndexTableSize(Audio_Parser_Base_t* pParserBase);
int32 ExportIndexTable(Audio_Parser_Base_t* pParserBase, uint8* pBuffer);
int32 ImportIndexTable(Audio_Parser_Base_t* pParserBase, uint8* pBuffer, uint32 size);

typedef struct {
    uint16 wCodecID;
    uint16 wChannels;
    uint32 nSampleRate;
    uint32 nAvgBytesPerSec;
    uint16 nBlockAlign;    /* Block Align					*/
    uint16 wBitsPerSample; /* bits per sample				*/

    uint32 nCodecSpecInfoSize;
    void* pCodecSpecInfo;

    uint32 nDuration;
    uint32 nTotalSamples;

} audio_track_info_t;

typedef struct {
    uint32 nTableType;
    uint32 nTotalItem;
    uint64* pSeekTable;
} audio_seek_table_t;

typedef struct {
    bool bIsCBR;
    bool bSeekable;
    bool bGotDuration;
    uint32 nReserved1;

    int64 nDuration;
    uint64 nBeginPointOffset;
    uint64 nStreamLen;

    audio_track_info_t* pTrack_Info;
    audio_seek_table_t stSeekTable;
} audio_strean_info_t;

typedef struct {
    void* appContext;

    audio_strean_info_t stStreamInfo;

    FslFileHandle fileHandle;
    FslFileStream fileStream;
    uint64 readOffset;
    uint64 fileSize;

    ParserMemoryOps memoryOps;
    ParserOutputBufferOps outputOps;

    Audio_Parser_Base_t mp3_parser_core;
} mp3_parser_t;

int32 mp3_parser_open(FslParserHandle* parserHandle, uint32 flags, FslFileStream* p_stream,
                      ParserMemoryOps* pMemOps, ParserOutputBufferOps* pOutputOps,
                      void* appContext);

int32 mp3_parser_close(FslParserHandle parserHandle);

// private functions

#endif  //__AUDIO_PARSER_BASE_H__
