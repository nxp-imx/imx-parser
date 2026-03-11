/*
***********************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef AUDIO_INDEX_TABLE_INNER_H
#define AUDIO_INDEX_TABLE_INNER_H

#define MAX_DURATIO_HOUR 1024

typedef struct {
    ParserMemoryOps m_tMemOps;
    uint32 m_dwHour;
    uint32 m_dwMin;
    uint32 m_dwSec;
    uint64** m_aSeekTable[MAX_DURATIO_HOUR];
    bool m_bHasItem;
} TAudioIndexTable;

typedef struct {
    uint32 m_dwHour;
    uint32 m_dwMin;
    uint32 m_dwSec;
    uint32 m_dwReserved[6];
} TAudioIndexHead;

#define INDEX_HEAD_SIZE sizeof(TAudioIndexHead)

#endif  // AUDIO_INDEX_TABLE_INNER_H
