/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef output_array_h
#define output_array_h

// #include "fsl_datatypes.h"
#include "fsl_parser.h"
#include "mpeg2_parser_api.h"

#define DEFAULT_UNIT_COUNTS 100
#define MORE_UNIT_COUNTS 50

typedef struct _OutputBuf_Array OutputBufArray;

typedef struct OutputBufLinkStruct {
    // points to the actual address;
    uint8* pBuffer;
    // the bufferlen of the buffer
    uint32 bufLen;
    // the actuall filled length
    uint32 filledLen;
    // the application data attached to this buffer
    void* pAppContext;
    // presentation time stamp
    uint64 usPresTime;
    // the flag which is attached to this buffer
    // PTS valid, sample completed..
    uint32 flag;
    struct OutputBufLinkStruct* pNextUnit;
} OutputBufLink;

struct _OutputBuf_Array {
    OutputBufLink* pHead;       // Point to the first unit
    OutputBufLink* pTail;       // Point to the last unit
    OutputBufLink* pValidTail;  // Point to the last unit which actually contains the unit
    uint32 totalCount;          // total units
    uint32 validCount;          // Count of unit which attacully contains the payload
    void* pLinkMem;             // the memory which contains the units
};

MPEG2_PARSER_ERROR_CODE InitOuputBufArray(ParserMemoryOps* pMemOps,
                                          OutputBufArray* pOutputBufArray);

MPEG2_PARSER_ERROR_CODE ReallocUnits(ParserMemoryOps* pMemOps, OutputBufArray* pOutputBufArray,
                                     uint32 moreCounts);

MPEG2_PARSER_ERROR_CODE InputOneUnitToArray(ParserMemoryOps* pMemOps,
                                            OutputBufArray* pOutputBufArray, uint8* pBuffer,
                                            uint32 bufLen, uint32 filledLen, uint64 usPresTime,
                                            uint32 flag, void* pAppContext);

MPEG2_PARSER_ERROR_CODE OutputOneUnitFromArray(OutputBufArray* pOutputBufArray, uint8** pBuffer,
                                               uint32* pDataSize, uint64* usPresTime, uint32* flag,
                                               void** pContext);

MPEG2_PARSER_ERROR_CODE ReleaseArrayUnits(OutputBufArray* pOutputBufArray, uint32 streamNum,
                                          ParserOutputBufferOps* pRequestBufferOps,
                                          void* appContext);

#endif
