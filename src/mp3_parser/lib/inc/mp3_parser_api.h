/*
*******************************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
*******************************************************************************
*/

/**
 * @ file mp3_parser_api.h
 * @ MP3 file parser header files
 */

#ifndef __MP3_PARSER_API_H__
#define __MP3_PARSER_API_H__

#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"

/******************************************************************************
 *                  API Functions
 * For calling sequence, please refer to the end of this file.
 ******************************************************************************/
#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN
#endif

EXTERN char* MP3ParserVersionInfo();

/******************************************************************************
 *
 *                Creation & Deletion
 *
 ******************************************************************************/
EXTERN int32 MP3CreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                             ParserOutputBufferOps* outputBufferOps, void* context,
                             FslParserHandle* parserHandle);

EXTERN int32 MP3CreateParser2(uint32 flags, FslFileStream* streamOps, ParserMemoryOps* memOps,
                              ParserOutputBufferOps* outputBufferOps, void* context,
                              FslParserHandle* parserHandle);

EXTERN int32 MP3DeleteParser(FslParserHandle parserHandle);

/******************************************************************************
 *
 *                 Index Table Loading, Export & Import
 *
 ******************************************************************************/
/* optional */
EXTERN int32 MP3ParserInitializeIndex(FslParserHandle parserHandle);

EXTERN int32 MP3ParserImportIndex(FslParserHandle parserHandle, uint8* buffer, uint32 size);

EXTERN int32 MP3ParserExportIndex(FslParserHandle parserHandle, uint8* buffer, uint32* size);

/******************************************************************************
 *
 *               Movie Properties
 *
 ******************************************************************************/
EXTERN int32 MP3ParserIsSeekable(FslParserHandle parserHandle, bool* seekable);

EXTERN int32 MP3ParserGetNumTracks(FslParserHandle parserHandle, uint32* numTracks);

EXTERN int32 MP3ParserGetMetaData(FslParserHandle parserHandle, UserDataID userDataId,
                                  UserDataFormat* userDataFormat, uint8** userData,
                                  uint32* userDataLength);

EXTERN int32 MP3ParserGetMovieDuration(FslParserHandle parserHandle, uint64* usDuration);

/******************************************************************************
 *
 *              General Track Properties
 *
 ******************************************************************************/
EXTERN int32 MP3ParserGetTrackType(FslParserHandle parserHandle, uint32 trackNum, uint32* mediaType,
                                   uint32* decoderType, uint32* decoderSubtype);

EXTERN int32 MP3ParserGetTrackDuration(FslParserHandle parserHandle, uint32 trackNum,
                                       uint64* usDuration);

EXTERN int32 MP3ParserGetCodecSpecificInfo(FslParserHandle parserHandle, uint32 trackNum,
                                           uint8** data, uint32* size);
/* optional */
EXTERN int32 MP3ParserGetBitRate(FslParserHandle parserHandle, uint32 trackNum, uint32* bitrate);

/******************************************************************************
 *
 *               Audio Properties
 *
 ******************************************************************************/
EXTERN int32 MP3ParserGetAudioNumChannels(FslParserHandle parserHandle, uint32 trackNum,
                                          uint32* numchannels);

EXTERN int32 MP3ParserGetAudioSampleRate(FslParserHandle parserHandle, uint32 trackNum,
                                         uint32* sampleRate);

EXTERN int32 MP3ParserGetAudioBitsPerSample(FslParserHandle parserHandle, uint32 trackNum,
                                            uint32* bitsPerSample);
/* optional */
EXTERN int32 MP3ParserGetAudioBlockAlign(FslParserHandle parserHandle, uint32 trackNum,
                                         uint32* blockAlign);

/******************************************************************************
 *
 *               Sample Reading, Seek & Trick Mode
 *
 ******************************************************************************/
EXTERN int32 MP3ParserGetReadMode(FslParserHandle parser_handle, uint32* readMode);

EXTERN int32 MP3ParserSetReadMode(FslParserHandle parser_handle, uint32 readMode);

EXTERN int32 MP3ParserEnableTrack(FslParserHandle parserHandle, uint32 trackNum, bool enable);

EXTERN int32 MP3ParserGetFileNextSample(FslParserHandle parserHandle, uint32* trackNum,
                                        uint8** sampleBuffer, void** bufferContext,
                                        uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                                        uint32* sampleFlags);

EXTERN int32 MP3ParserSeek(FslParserHandle parserHandle, uint32 trackNum, uint64* usTime,
                           uint32 flag);

#endif  // __MP3_PARSER_API_H__
