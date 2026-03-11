/*
***********************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2025-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include <string.h>

#include "fsl_types.h"
#include "fsl_parser.h"

#include "AudioIndexTable.h"
#include "AudioIndexTableInner.h"

#define INVALID_OFFSET (uint64)(-1LL)
#define HOUR_TABLE_SIZE (uint32)(sizeof(uint64*) * 60)
#define MIN_TABLE_SIZE (uint32)(sizeof(uint64) * 60)

//#define INDEXMSG printf
#define INDEXMSG(...)

#define SAFE_FREE(p)                 \
    do {                             \
        if (p) {                     \
            self->m_tMemOps.Free(p); \
            p = NULL;                \
        }                            \
    } while (0)
#define TESTMALLOC(p)                          \
    do {                                       \
        if (NULL == p) {                       \
            return PARSER_INSUFFICIENT_MEMORY; \
        }                                      \
    } while (0)

#define ALLOC_MIN_IN_HOUR                                                                       \
    do {                                                                                        \
        self->m_aSeekTable[self->m_dwHour] = (uint64**)self->m_tMemOps.Malloc(HOUR_TABLE_SIZE); \
        TESTMALLOC(self->m_aSeekTable[self->m_dwHour]);                                         \
        memset(self->m_aSeekTable[self->m_dwHour], 0, HOUR_TABLE_SIZE);                         \
    } while (0)

#define ALLOC_SEC_IN_MIN                                                                 \
    do {                                                                                 \
        self->m_aSeekTable[self->m_dwHour][self->m_dwMin] =                              \
                (uint64*)self->m_tMemOps.Malloc(MIN_TABLE_SIZE);                         \
        TESTMALLOC(self->m_aSeekTable[self->m_dwHour][self->m_dwMin]);                   \
        memset(self->m_aSeekTable[self->m_dwHour][self->m_dwMin], 0xff, MIN_TABLE_SIZE); \
    } while (0)

int32 AudioIndexTableCreate(AITHandle* phIndex, ParserMemoryOps* pMemOps) {
    TAudioIndexTable* self = NULL;

    if ((phIndex == NULL) || (pMemOps == NULL) || (pMemOps->Malloc == NULL) ||
        (pMemOps->Free == NULL)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    self = (TAudioIndexTable*)pMemOps->Malloc(sizeof(TAudioIndexTable));
    TESTMALLOC(self);
    memset(self, 0, sizeof(TAudioIndexTable));
    self->m_tMemOps = *pMemOps;

    *phIndex = (AITHandle)self;

    return PARSER_SUCCESS;
}

int32 AudioIndexTableDestroy(AITHandle hIndex) {
    uint32 dwHour = 0;
    uint32 dwMin = 0;
    TAudioIndexTable* self = (TAudioIndexTable*)hIndex;
    uint32 dwMinEnd = 59;

    if (hIndex == NULL) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    // free seek table
    for (dwHour = 0; dwHour <= self->m_dwHour; dwHour++) {
        if (dwHour == self->m_dwHour) {
            dwMinEnd = self->m_dwMin;
        }

        if (self->m_aSeekTable[dwHour]) {
            for (dwMin = 0; dwMin <= dwMinEnd; dwMin++) {
                SAFE_FREE(self->m_aSeekTable[dwHour][dwMin]);
            }
        }

        SAFE_FREE(self->m_aSeekTable[dwHour]);
    }

    SAFE_FREE(self);

    return PARSER_SUCCESS;
}

// must increased second by second
int32 AudioIndexTableAddItem(AITHandle hIndex, uint64 qwOffset) {
    TAudioIndexTable* self = (TAudioIndexTable*)hIndex;

    if (hIndex == NULL) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if (self->m_dwHour >= MAX_DURATIO_HOUR) {
        INDEXMSG("AudioIndexTableAddItem: beyond %d hours\n", MAX_DURATIO_HOUR);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    // the first time add item, just keep h:m:s as 0:0:0.
    // after this, will add second by second
    if (self->m_bHasItem) {
        // point to current second
        self->m_dwSec = (self->m_dwSec + 1) % 60;
        if (self->m_dwSec == 0) {
            self->m_dwMin = (self->m_dwMin + 1) % 60;
            if (self->m_dwMin == 0) {
                self->m_dwHour++;
            }
        }
    }

    // need alloc new table
    if (self->m_dwSec == 0) {
        if (self->m_dwMin == 0) {
            if (self->m_aSeekTable[self->m_dwHour]) {
                INDEXMSG("AudioIndexTableAddItem: expect self->m_aSeekTable[%d] be null\n",
                         (int)self->m_dwHour);
                return PARSER_ERR_INVALID_PARAMETER;
            }

            ALLOC_MIN_IN_HOUR;
        }

        if (self->m_aSeekTable[self->m_dwHour][self->m_dwMin]) {
            INDEXMSG("AudioIndexTableAddItem: expect self->m_aSeekTable[%d][%d] be null\n",
                     (int)self->m_dwHour, (int)self->m_dwMin);
            return PARSER_ERR_INVALID_PARAMETER;
        }

        ALLOC_SEC_IN_MIN;
    }

    self->m_aSeekTable[self->m_dwHour][self->m_dwMin][self->m_dwSec] = qwOffset;

    self->m_bHasItem = TRUE;

    return PARSER_SUCCESS;
}

int32 AudioIndexTableGetItem(AITHandle hIndex, uint32 dwSeconds, uint64* pqwOffset) {
    uint32 dwHour = 0;
    uint32 dwMin = 0;
    uint32 dwSec = 0;
    uint32 dwRes = 0;
    uint64 qwOffset = 0;
    TAudioIndexTable* self = (TAudioIndexTable*)hIndex;

    if ((hIndex == NULL) || (pqwOffset == NULL) || (FALSE == self->m_bHasItem)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *pqwOffset = INVALID_OFFSET;

    // beyond duration
    if ((dwSeconds > 60 * (self->m_dwHour * 60 + self->m_dwMin) + self->m_dwSec)) {
        return PARSER_SUCCESS;
    }

    dwHour = dwSeconds / 3600;
    dwRes = dwSeconds % 3600;

    dwMin = dwRes / 60;
    dwSec = dwRes % 60;

    if (dwHour >= MAX_DURATIO_HOUR) {
        INDEXMSG("AudioIndexTableGetItem: beyond %d hours\n", MAX_DURATIO_HOUR);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if ((self->m_aSeekTable[dwHour] == NULL) || (self->m_aSeekTable[dwHour][dwMin] == NULL)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    qwOffset = self->m_aSeekTable[dwHour][dwMin][dwSec];

    if (qwOffset == INVALID_OFFSET) {
        INDEXMSG("AudioIndexTableGetItem: get a invalid offset for %d seconds, %d:%d:%d\n",
                 (int)dwSeconds, (int)dwHour, (int)dwMin, (int)dwSec);
    }

    *pqwOffset = qwOffset;

    return PARSER_SUCCESS;
}

// if pbyBuf is NULL, return the actual size needed
int32 AudioIndexTableExport(AITHandle hIndex, uint8* pbyBuf, uint32* pdwSize) {
    uint32 dwSize = 0;
    uint32 dwHour = 0;
    uint32 dwMin = 0;
    uint8* pbyBufIn = pbyBuf;
    TAudioIndexHead* pIndexHead = (TAudioIndexHead*)pbyBuf;
    uint32 dwTotalMinIdx = 0;
    uint32 dwTotalMin = 0;
    TAudioIndexTable* self = (TAudioIndexTable*)hIndex;

    if ((hIndex == NULL) || (pdwSize == NULL)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if (self->m_bHasItem == FALSE) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    dwTotalMin = self->m_dwHour * 60 + self->m_dwMin + 1;

    dwSize = MIN_TABLE_SIZE * dwTotalMin + INDEX_HEAD_SIZE;
    *pdwSize = dwSize;

    if (pbyBuf == NULL) {
        return PARSER_SUCCESS;
    }

    if (*pdwSize < dwSize) {
        INDEXMSG("AudioIndexTableExport: buff size %d < need size %d\n", (int)*pdwSize,
                 (int)dwSize);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    memset(pIndexHead, 0, INDEX_HEAD_SIZE);
    pIndexHead->m_dwHour = self->m_dwHour;
    pIndexHead->m_dwMin = self->m_dwMin;
    pIndexHead->m_dwSec = self->m_dwSec;

    pbyBufIn += INDEX_HEAD_SIZE;
    for (dwHour = 0; dwHour <= self->m_dwHour; dwHour++) {
        for (dwMin = 0; dwMin < 60 && dwTotalMinIdx < dwTotalMin; dwMin++, dwTotalMinIdx++) {
            if (self->m_aSeekTable[dwHour] && self->m_aSeekTable[dwHour][dwMin]) {
                memcpy(pbyBufIn, self->m_aSeekTable[dwHour][dwMin], MIN_TABLE_SIZE);
                pbyBufIn += MIN_TABLE_SIZE;
            }
        }
    }

    return PARSER_SUCCESS;
}

int32 AudioIndexTableImport(AITHandle hIndex, uint8* pbyBuf, uint32 dwSize) {
    uint32 dwTotalMinIdx = 0;
    uint32 dwTotalMin = 0;
    uint32 dwTotalHour = 0;
    uint8* pbyBufIn = pbyBuf;
    TAudioIndexHead* pIndexHead = (TAudioIndexHead*)pbyBuf;
    uint32 dwRecordHour = 0;
    uint32 dwRecordMin = 0;
    uint32 dwRecordSec = 0;
    uint32 dwRawSize = 0;

    TAudioIndexTable* self = (TAudioIndexTable*)hIndex;

    if ((hIndex == NULL) || (pbyBuf == NULL) || (dwSize < INDEX_HEAD_SIZE)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    dwRecordHour = pIndexHead->m_dwHour;
    dwRecordMin = pIndexHead->m_dwMin;
    dwRecordSec = pIndexHead->m_dwSec;

    dwRawSize = dwSize - INDEX_HEAD_SIZE;
    if (dwRawSize % MIN_TABLE_SIZE) {
        INDEXMSG("AudioIndexTableImport: dwRawSize(%d) mod MIN_TABLE_SIZE(%d) is not zero\n",
                 (int)dwRawSize, (int)MIN_TABLE_SIZE);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    dwTotalMin = dwRawSize / MIN_TABLE_SIZE;
    if (dwTotalMin > MAX_DURATIO_HOUR * 60) {
        INDEXMSG("AudioIndexTableImport: dwTotalMin(%d) > MAX_DURATIO_HOUR(%d) * 60\n",
                 (int)dwTotalMin, MAX_DURATIO_HOUR);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    dwTotalHour = dwTotalMin / 60;

    // verify
    if (dwTotalMin != dwRecordHour * 60 + dwRecordMin + 1) {
        INDEXMSG(
                "AudioIndexTableImport: dwTotalMin(%d) != dwRecordHour(%d) * 60 + dwRecordMin(%d) "
                "+ 1\n",
                (int)dwTotalMin, (int)dwRecordHour, (int)dwRecordMin);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    // ensure called just after AudioIndexTableCreate
    if ((self->m_dwMin > 0) || (self->m_dwHour > 0) || (self->m_dwSec > 0)) {
        INDEXMSG(
                "AudioIndexTableImport: err call sequence, must called just after "
                "AudioIndexTableCreate\n");
        return PARSER_ERR_INVALID_PARAMETER;
    }

    pbyBufIn += INDEX_HEAD_SIZE;
    for (self->m_dwHour = 0; self->m_dwHour <= dwTotalHour; self->m_dwHour++) {
        if (self->m_aSeekTable[self->m_dwHour]) {
            INDEXMSG(
                    "AudioIndexTableImport: err call sequence, expect self->m_aSeekTable[%d] be "
                    "null\n",
                    (int)self->m_dwHour);
            return PARSER_ERR_INVALID_PARAMETER;
        }

        ALLOC_MIN_IN_HOUR;

        for (self->m_dwMin = 0; self->m_dwMin < 60 && dwTotalMinIdx < dwTotalMin;
             self->m_dwMin++, dwTotalMinIdx++) {
            if (self->m_aSeekTable[self->m_dwHour][self->m_dwMin]) {
                INDEXMSG(
                        "AudioIndexTableImport: err call sequence, expect "
                        "self->m_aSeekTable[%d][%d] be null\n",
                        (int)self->m_dwHour, (int)self->m_dwMin);
                return PARSER_ERR_INVALID_PARAMETER;
            }

            ALLOC_SEC_IN_MIN;

            memcpy(self->m_aSeekTable[self->m_dwHour][self->m_dwMin], pbyBufIn, MIN_TABLE_SIZE);
            pbyBufIn += MIN_TABLE_SIZE;
        }
    }

    self->m_dwHour = dwRecordHour;
    self->m_dwMin = dwRecordMin;
    self->m_dwSec = dwRecordSec;

    self->m_bHasItem = TRUE;

    // check
    if (pbyBufIn != pbyBuf + dwSize) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    return PARSER_SUCCESS;
}
