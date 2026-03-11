
/*
 ***********************************************************************
 * Copyright 2015 by Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

/**
 * @file ape_parser_api.h
 * @ape file parser header files
 */

#ifndef __APE_PARSER_API_H__
#define __APE_PARSER_API_H__

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

EXTERN char* ApeParserVersionInfo();

/******************************************************************************
 *
 *                Creation & Deletion
 *
 ******************************************************************************/
EXTERN int32 ApeCreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                             ParserOutputBufferOps* outputBufferOps, void* context,
                             FslParserHandle* parserHandle);

EXTERN int32 ApeDeleteParser(FslParserHandle parserHandle);

/******************************************************************************
 *
 *                 Index Table Loading, Export & Import
 *
 ******************************************************************************/
/* optional */
EXTERN int32 ApeParserInitializeIndex(FslParserHandle parserHandle);

EXTERN int32 ApeParserImportIndex(FslParserHandle parserHandle, uint8* buffer, uint32 size);

EXTERN int32 ApeParserExportIndex(FslParserHandle parserHandle, uint8* buffer, uint32* size);

/******************************************************************************
 *
 *               Movie Properties
 *
 ******************************************************************************/
EXTERN int32 ApeParserIsSeekable(FslParserHandle parserHandle, bool* seekable);

EXTERN int32 ApeParserGetNumTracks(FslParserHandle parserHandle, uint32* numTracks);

EXTERN int32 ApeParserGetMetaData(FslParserHandle parserHandle, UserDataID userDataId,
                                  UserDataFormat* userDataFormat, uint8** userData,
                                  uint32* userDataLength);

EXTERN int32 ApeParserGetMovieDuration(FslParserHandle parserHandle, uint64* usDuration);

/******************************************************************************
 *
 *              General Track Properties
 *
 ******************************************************************************/
EXTERN int32 ApeParserGetTrackType(FslParserHandle parserHandle, uint32 trackNum, uint32* mediaType,
                                   uint32* decoderType, uint32* decoderSubtype);

EXTERN int32 ApeParserGetTrackDuration(FslParserHandle parserHandle, uint32 trackNum,
                                       uint64* usDuration);

EXTERN int32 ApeParserGetCodecSpecificInfo(FslParserHandle parserHandle, uint32 trackNum,
                                           uint8** data, uint32* size);
/* optional */
EXTERN int32 ApeParserGetBitRate(FslParserHandle parserHandle, uint32 trackNum, uint32* bitrate);

/******************************************************************************
 *
 *               Audio Properties
 *
 ******************************************************************************/
EXTERN int32 ApeParserGetAudioNumChannels(FslParserHandle parserHandle, uint32 trackNum,
                                          uint32* numchannels);

EXTERN int32 ApeParserGetAudioSampleRate(FslParserHandle parserHandle, uint32 trackNum,
                                         uint32* sampleRate);

EXTERN int32 ApeParserGetAudioBitsPerSample(FslParserHandle parserHandle, uint32 trackNum,
                                            uint32* bitsPerSample);
/* optional */
EXTERN int32 ApeParserGetAudioBlockAlign(FslParserHandle parserHandle, uint32 trackNum,
                                         uint32* blockAlign);

/******************************************************************************
 *
 *               Sample Reading, Seek & Trick Mode
 *
 ******************************************************************************/
EXTERN int32 ApeParserGetReadMode(FslParserHandle parser_handle, uint32* readMode);

EXTERN int32 ApeParserSetReadMode(FslParserHandle parser_handle, uint32 readMode);

EXTERN int32 ApeParserEnableTrack(FslParserHandle parserHandle, uint32 trackNum, bool enable);

EXTERN int32 ApeParserGetFileNextSample(FslParserHandle parserHandle, uint32* trackNum,
                                        uint8** sampleBuffer, void** bufferContext,
                                        uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                                        uint32* sampleFlags);

EXTERN int32 ApeParserSeek(FslParserHandle parserHandle, uint32 trackNum, uint64* usTime,
                           uint32 flag);

#endif
