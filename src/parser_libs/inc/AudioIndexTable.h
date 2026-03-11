/*
***********************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef AUDIO_INDEX_TABLE_H
#define AUDIO_INDEX_TABLE_H

// only 2 call sequence
// a: AudioIndexTableCreate - AudioIndexTableAddItem - AudioIndexTableExport -
// AudioIndexTableDestroy b: AudioIndexTableCreate - AudioIndexTableImport - AudioIndexTableGetItem
// - AudioIndexTableDestroy

typedef void* AITHandle;

int32 AudioIndexTableCreate(AITHandle* phIndex, ParserMemoryOps* pMemOps);
int32 AudioIndexTableDestroy(AITHandle hIndex);

// must called second by second
int32 AudioIndexTableAddItem(AITHandle hIndex, uint64 qwOffset);

// if beyond, return success, set *pqwOffset to -1;
int32 AudioIndexTableGetItem(AITHandle hIndex, uint32 dwSecond, uint64* pqwOffset);

// if pbyBuf is NULL, return the actual size needed
int32 AudioIndexTableExport(AITHandle hIndex, uint8* pbyBuf, uint32* pdwSize);
int32 AudioIndexTableImport(AITHandle hIndex, uint8* pbyBuf, uint32 dwSize);

#endif  // AUDIO_INDEX_TABLE_H
