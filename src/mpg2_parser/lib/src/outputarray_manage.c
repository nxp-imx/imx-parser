/*
***********************************************************************
* Copyright (c) 2011-2012, Freescale Semiconductor, Inc.
* Copyright 2022, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include "outputarray_manage.h"
#include "string.h"

MPEG2_PARSER_ERROR_CODE InitOuputBufArray(ParserMemoryOps* pMemOps,
                                          OutputBufArray* pOutputBufArray) {
    uint32 i;
    OutputBufLink* pHead;
    pOutputBufArray->pLinkMem = (void*)pMemOps->Malloc(DEFAULT_UNIT_COUNTS * sizeof(OutputBufLink));
    if (pOutputBufArray->pLinkMem == NULL)
        return PARSER_INSUFFICIENT_MEMORY;

    memset(pOutputBufArray->pLinkMem, 0, DEFAULT_UNIT_COUNTS * sizeof(OutputBufLink));
    pOutputBufArray->pHead = (OutputBufLink*)pOutputBufArray->pLinkMem;
    pOutputBufArray->pTail = pOutputBufArray->pHead + (DEFAULT_UNIT_COUNTS - 1);
    pOutputBufArray->totalCount = DEFAULT_UNIT_COUNTS;
    pOutputBufArray->validCount = 0;
    pOutputBufArray->pValidTail = NULL;

    pHead = pOutputBufArray->pHead;
    for (i = 0; i < DEFAULT_UNIT_COUNTS - 1; i++) {
        pHead->pNextUnit = &(pHead[1]);
        pHead++;
    }

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE ReallocUnits(ParserMemoryOps* pMemOps, OutputBufArray* pOutputBufArray,
                                     uint32 moreCounts) {
    uint32 i;
    OutputBufLink* pHead;
    uint32 count = pOutputBufArray->totalCount;
    void* pOldLinkMem = pOutputBufArray->pLinkMem;
    (void)moreCounts;

    pOutputBufArray->pLinkMem = (void*)pMemOps->ReAlloc(
            pOutputBufArray->pLinkMem, (count + MORE_UNIT_COUNTS) * sizeof(OutputBufLink));
    if (pOutputBufArray->pLinkMem == NULL)
        return PARSER_INSUFFICIENT_MEMORY;

    if (pOldLinkMem != pOutputBufArray->pLinkMem) {
        unsigned long diff = (unsigned long)pOutputBufArray->pLinkMem - (unsigned long)pOldLinkMem;
        pOutputBufArray->pHead = (OutputBufLink*)((unsigned long)pOutputBufArray->pHead + diff);
        pOutputBufArray->pTail = (OutputBufLink*)((unsigned long)pOutputBufArray->pTail + diff);
        if (pOutputBufArray->validCount != 0)
            pOutputBufArray->pValidTail =
                    (OutputBufLink*)((unsigned long)pOutputBufArray->pValidTail + diff);
        for (i = 0; i < count; i++) {
            pHead = &((OutputBufLink*)pOutputBufArray->pLinkMem)[i];
            if (pHead->pNextUnit != NULL)
                pHead->pNextUnit = (OutputBufLink*)((unsigned long)pHead->pNextUnit + diff);
        }

        /*
        uint32 headIndex = pOutputBufArray->pHead - (OutputBufLink*)pOldLinkMem;
        pHead = (OutputBufLink*)pOutputBufArray->pLinkMem +headIndex;
        pOutputBufArray->pHead= pHead;
        for(i=headIndex;i<count-1;i++)
        {
        pHead->pNextUnit=&pHead[1];
        pHead++;
        }
        pHead->pNextUnit = (OutputBufLink*)pOutputBufArray->pLinkMem;
        pHead = (OutputBufLink*)pOutputBufArray->pLinkMem;
        for(i=0;i<headIndex-1;i++)
        {
        pHead->pNextUnit=&pHead[1];
        pHead++;
        }
        pOutputBufArray->pTail = pHead;
        pHead->pNextUnit=NULL;
        if(pOutputBufArray->validCount!=0)
        {
        if( headIndex+(pOutputBufArray->validCount-1) < count)
        pOutputBufArray->pValidTail = pOutputBufArray->pHead + pOutputBufArray->validCount - 1;
        else
        pOutputBufArray->pValidTail = (OutputBufLink*)pOutputBufArray->pLinkMem
        +(pOutputBufArray->validCount-1+headIndex-count);
        }
        */
    }

    pHead = (OutputBufLink*)pOutputBufArray->pLinkMem;
    pHead += count;
    memset(pHead, 0, MORE_UNIT_COUNTS * sizeof(OutputBufLink));

    pOutputBufArray->pTail->pNextUnit = pHead;
    for (i = 0; i < MORE_UNIT_COUNTS - 1; i++) {
        pHead->pNextUnit = &pHead[1];
        pHead++;
    }

    pOutputBufArray->totalCount += MORE_UNIT_COUNTS;
    pOutputBufArray->pTail = pHead;

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE InputOneUnitToArray(ParserMemoryOps* pMemOps,
                                            OutputBufArray* pOutputBufArray, uint8* pBuffer,
                                            uint32 bufLen, uint32 filledLen, uint64 usPresTime,
                                            uint32 flag, void* pAppContext) {
    OutputBufLink* pLink;
    if (pOutputBufArray->totalCount == 0)
        return PARSER_INSUFFICIENT_MEMORY;

    if (pOutputBufArray->pValidTail == NULL)
        pLink = pOutputBufArray->pHead;
    else
        pLink = pOutputBufArray->pValidTail->pNextUnit;

    if (pLink == NULL) {
        MPEG2_PARSER_ERROR_CODE err;
        if ((err = ReallocUnits(pMemOps, pOutputBufArray, MORE_UNIT_COUNTS)) != PARSER_SUCCESS)
            return err;
        if (pOutputBufArray->pValidTail == NULL)
            pLink = pOutputBufArray->pHead;
        else
            pLink = pOutputBufArray->pValidTail->pNextUnit;
    }

    pLink->pBuffer = pBuffer;
    pLink->bufLen = bufLen;
    pLink->filledLen = filledLen;
    pLink->pAppContext = pAppContext;
    pLink->usPresTime = usPresTime;
    pLink->flag = flag;

    pOutputBufArray->validCount++;
    pOutputBufArray->pValidTail = pLink;

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE OutputOneUnitFromArray(OutputBufArray* pOutputBufArray, uint8** pBuffer,
                                               uint32* pDataSize, uint64* pusPresTime,
                                               uint32* pFlag, void** pContext) {
    OutputBufLink* pHead;
    if (pOutputBufArray->validCount == 0)
        return MPEG2_ERR_NO_MORE_ARRAY_PACKETS;
    pHead = pOutputBufArray->pHead;

    *pBuffer = pHead->pBuffer;
    *pDataSize = pHead->filledLen;
    *pContext = pHead->pAppContext;
    *pusPresTime = pHead->usPresTime;
    *pFlag = pHead->flag;

    pOutputBufArray->validCount--;
    pOutputBufArray->pHead = pHead->pNextUnit;

    pHead->bufLen = 0;
    pHead->pBuffer = NULL;
    pHead->pAppContext = NULL;
    pHead->pNextUnit = NULL;
    pHead->filledLen = 0;

    pOutputBufArray->pTail->pNextUnit = pHead;
    pOutputBufArray->pTail = pHead;
    if (pOutputBufArray->validCount == 0)
        pOutputBufArray->pValidTail = NULL;

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE ReleaseArrayUnits(OutputBufArray* pOutputBufArray, uint32 streamNum,
                                          ParserOutputBufferOps* pRequestBufferOps,
                                          void* appContext) {
    uint32 i;
    OutputBufLink* pLink;

    pLink = pOutputBufArray->pHead;
    for (i = 0; i < pOutputBufArray->validCount; i++) {
        pRequestBufferOps->ReleaseBuffer(streamNum, pLink->pBuffer, pLink->pAppContext, appContext);
        pLink = pLink->pNextUnit;
    }
    pOutputBufArray->validCount = 0;
    pOutputBufArray->pValidTail = NULL;
    return PARSER_SUCCESS;
}
