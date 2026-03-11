/*
***********************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef ID3_PARSER_INNER_H
#define ID3_PARSER_INNER_H

typedef struct {
    uint32 iDataLen;
    UserDataFormat eDataFormat;
    uint8* pData;
} MetaDataItem;

typedef struct {
    MetaDataItem Album;
    MetaDataItem Artist;
    MetaDataItem CopyRight;
    MetaDataItem Band;
    MetaDataItem Composer;
    MetaDataItem Genre;
    MetaDataItem Title;
    MetaDataItem Year;
    MetaDataItem TrackNumber;
    MetaDataItem DiscNumber;
    MetaDataItem ArtWork;
    MetaDataItem AlbumArtist;
    MetaDataItem EncoderDelay;
    MetaDataItem EncoderPadding;
} MetaDataArray;

typedef struct {
    ID3 m_id3;
    uint32 m_dwID3V2Size;
    FslFileStream m_tStreamOps;
    ParserMemoryOps m_tMemOps;
    void* m_context;
    FslFileHandle m_fileHandle;
    uint64 m_qwFileSize;
    MetaDataArray m_tMetaDataList;
    int32 m_dwEncoderDelay;
    int32 m_dwEncoderPadding;
} ID3Obj, *ID3ObjPtr;

#endif  // ID3_PARSER_INNER_H
