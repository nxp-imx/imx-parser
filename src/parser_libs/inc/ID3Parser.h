/*
***********************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2018, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef _ID3_PARSER_H
#define _ID3_PARSER_H

typedef void* ID3Parser;

int32 ID3ParserCreate(FslFileStream* streamOps, ParserMemoryOps* memOps, FslFileHandle fileHandle,
                      void* context, ID3Parser* phParser, bool isConvert);

int32 ID3ParserCreate2(FslFileStream* streamOps, ParserMemoryOps* memOps, FslFileHandle fileHandle,
                       void* context, ID3Parser* phParser, bool isConvert, uint64 startOffset);

int32 ID3ParserDelete(ID3Parser hParser);

int32 ID3ParserGetMetaData(ID3Parser hParser, UserDataID userDataId, UserDataFormat* userDataFormat,
                           uint8** userData, uint32* userDataLength);

int32 ID3ParserGetID3V2Size(ID3Parser hParser, uint32* pdwSize);

#endif  //_ID3_PARSER_H
