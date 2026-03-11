
/*
 ***********************************************************************
 * Copyright (c) 2009-2013, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

/**
 * @file Fsl_parser_api.h
 * @Fsl file parser header files
 */

#ifndef FSL_PARSER_API_HEADER_INCLUDED
#define FSL_PARSER_API_HEADER_INCLUDED

/* common files of mmalyer */
#include "common/file_stream.h"
#include "common/fsl_media_types.h"
#include "common/fsl_parser.h"
#include "common/fsl_types.h"

/*
 * Fsl parser memory callback funtion pointer table.An "context" is used for the wrapper usage.
 */
typedef struct {
    void* (*Calloc)(uint32 numElements, uint32 size);
    void* (*Malloc)(uint32 size);
    void (*Free)(void* ptr);
    void* (*ReAlloc)(void* ptr, uint32 size); /* necessary for index scanning!*/

} FslMemoryOps; /* callback operation callback table */

typedef struct {
    int32 (*WriteDrmFragment)(uint32 fragmentNum, uint8* data, uint32 dataLength);
    int32 (*ReadDrmFragment)(uint32 fragmentNum, uint8* data, uint32 dataLength);
} FslDrmOps;

/* DRM callback funtion, to read/write local drm information. Application decide where to hide the
 * information.*/

/* return value of the DRM callback function, consist with DRM lib */
#define Fsl_DRM_LOCAL_SUCCESS 0
#define Fsl_DRM_ERROR_READING_MEMORY 12
#define Fsl_DRM_ERROR_WRITING_MEMORY 13

/***************************************************************************************
 *                  API Funtions
 * For calling sequence, please refer to the end of this file.
 ***************************************************************************************/
typedef const char* (*tFslParserVersionInfo)();

typedef int32 (*tFslCreateParser)(file_stream_t* stream, FslMemoryOps* memOps, FslDrmOps* drmOps,
                                  void* context, FslParserHandle* parserHandle);

typedef int32 (*tFslDeleteParser)(FslParserHandle parserHandle);

typedef int32 (*tFslParseHeader)(FslParserHandle parserHandle);

typedef int32 (*tFslIsProtected)(FslParserHandle parserHandle, bool* isProtected);

typedef int32 (*tFslQueryContentUsage)(FslParserHandle parserHandle, bool* isRental,
                                       uint32* viewLimit, uint32* viewCount);

typedef int32 (*tFslQueryOutputProtectionFlag)(FslParserHandle parserHandle, uint8* cgmsaSignal,
                                               uint8* acptbSignal, uint8* digitalProtectionSignal);

typedef int32 (*tFslCommitPlayback)(FslParserHandle parserHandle);

typedef int32 (*tFslFinalizePlayback)(FslParserHandle parserHandle);

typedef int32 (*tFslInitializeIndex)(FslParserHandle parserHandle);

typedef int32 (*tFslImportIndex)(FslParserHandle parserHandle, uint32 trackNum, uint8* buffer,
                                 uint32 size);

typedef int32 (*tFslExportIndex)(FslParserHandle parserHandle, uint32 trackNum, uint8* buffer,
                                 uint32* size);

typedef int32 (*tFslIsSeekable)(FslParserHandle parserHandle, bool* seekable);

typedef int32 (*tFslGetNumTracks)(FslParserHandle parserHandle, uint32* numTracks);

typedef int32 (*tFslGetNumIndexTable)(FslParserHandle parserHandle, uint32* num_index_tables);

typedef int32 (*tFslGetUserData)(FslParserHandle parserHandle, uint32 id, uint8** buffer,
                                 uint32* size);

typedef int32 (*tFslGetMovieDuration)(FslParserHandle parserHandle, uint64* usDuration);

typedef int32 (*tFslGetTrackDuration)(FslParserHandle parserHandle, uint32 trackNum,
                                      uint64* usDuration);

typedef int32 (*tFslGetTrackType)(FslParserHandle parserHandle, uint32 trackNum, uint32* mediaType,
                                  uint32* decoderType, uint32* decoderSubtype);

typedef int32 (*tFslGetCodecSpecificInfo)(FslParserHandle parserHandle, uint32 trackNum,
                                          uint8** data, uint32* size);

typedef int32 (*tFslGetMaxSampleSize)(FslParserHandle parserHandle, uint32 trackNum, uint32* size);

typedef int32 (*tFslGetLanguage)(FslParserHandle parserHandle, uint32 trackNum,
                                 uint8* threeCharCode);

typedef int32 (*tFslGetScale)(FslParserHandle parserHandle, uint32 trackNum, uint32* scale);

typedef int32 (*tFslGetRate)(FslParserHandle parserHandle, uint32 trackNum, uint32* rate);

typedef int32 (*tFslGetStartDelay)(FslParserHandle parserHandle, uint32 trackNum,
                                   uint32* usStartDelay);

typedef int32 (*tFslGetBitRate)(FslParserHandle parserHandle, uint32 trackNum, uint32* bitrate);

typedef int32 (*tFslGetSampleDuration)(FslParserHandle parserHandle, uint32 trackNum,
                                       uint64* usDuration);

typedef int32 (*tFslGetVideoFrameWidth)(FslParserHandle parserHandle, uint32 trackNum,
                                        uint32* width);

typedef int32 (*tFslGetVideoFrameHeight)(FslParserHandle parserHandle, uint32 trackNum,
                                         uint32* height);
typedef int32 (*tFslGetVideoFrameRotation)(FslParserHandle parserHandle, uint32 trackNum,
                                           uint32* rotation);

typedef int32 (*tFslGetAudioNumChannels)(FslParserHandle parserHandle, uint32 trackNum,
                                         uint32* numchannels);

typedef int32 (*tFslGetAudioBitsPerFrame)(FslParserHandle parserHandle, uint32 trackNum,
                                          uint32* bits_per_frame);

typedef int32 (*tFslGetAudioSampleRate)(FslParserHandle parserHandle, uint32 trackNum,
                                        uint32* sampleRate);

typedef int32 (*tFslGetAudioBitsPerSample)(FslParserHandle parserHandle, uint32 trackNum,
                                           uint32* bitsPerSample);

typedef int32 (*tFslGetTextTrackWidth)(FslParserHandle parserHandle, uint32 trackNum,
                                       uint32* width);

typedef int32 (*tFslGetTextTrackHeight)(FslParserHandle parserHandle, uint32 trackNum,
                                        uint32* height);

typedef int32 (*tFslGetNextSampleSize)(FslParserHandle parserHandle, uint32 trackNum,
                                       uint32* sampleSize);

typedef int32 (*tFslGetNextSample)(FslParserHandle parserHandle, uint32 trackNum, uint8* sampleData,
                                   uint32* dataSize, uint64* usStartTime, uint64* usEndTime,
                                   uint32* flag);

typedef int32 (*tFslSeek)(FslParserHandle parserHandle, uint32 trackNum, uint64* usTime,
                          uint32 flag);

typedef int32 (*tFslGetNextSyncSampleSize)(FslParserHandle parserHandle, uint32 trackNum,
                                           uint32 direction, uint32* sampleSize);

typedef int32 (*tFslGetNextSyncSample)(FslParserHandle parserHandle, uint32 trackNum,
                                       uint32 direction, uint8* sampleData, uint32* dataSize,
                                       uint64* usStartTime, uint64* usEndTime, uint32* flag);

typedef struct FslParser {
    tFslParserVersionInfo FslParserVersionInfo;
    tFslCreateParser FslCreateParser;
    tFslDeleteParser FslDeleteParser;
    tFslParseHeader FslParseHeader;
    tFslIsProtected FslIsProtected;
    tFslQueryContentUsage FslQueryContentUsage;
    tFslQueryOutputProtectionFlag FslQueryOutputProtectionFlag;
    tFslCommitPlayback FslCommitPlayback;
    tFslFinalizePlayback FslFinalizePlayback;
    tFslInitializeIndex FslInitializeIndex;
    tFslImportIndex FslImportIndex;
    tFslExportIndex FslExportIndex;
    tFslIsSeekable FslIsSeekable;
    tFslGetNumTracks FslGetNumTracks;
    tFslGetNumIndexTable FslGetNumIndexTable;
    tFslGetUserData FslGetUserData;
    tFslGetMovieDuration FslGetMovieDuration;
    tFslGetTrackDuration FslGetTrackDuration;
    tFslGetTrackType FslGetTrackType;
    tFslGetCodecSpecificInfo FslGetCodecSpecificInfo;
    tFslGetMaxSampleSize FslGetMaxSampleSize;
    tFslGetLanguage FslGetLanguage;
    tFslGetScale FslGetScale;
    tFslGetRate FslGetRate;
    tFslGetStartDelay FslGetStartDelay;
    tFslGetBitRate FslGetBitRate;
    tFslGetSampleDuration FslGetSampleDuration;
    tFslGetVideoFrameWidth FslGetVideoFrameWidth;
    tFslGetVideoFrameHeight FslGetVideoFrameHeight;
    tFslGetVideoFrameRotation FslGetVideoFrameRotation;
    tFslGetAudioNumChannels FslGetAudioNumChannels;
    tFslGetAudioBitsPerFrame FslGetAudioBitsPerFrame;
    tFslGetAudioSampleRate FslGetAudioSampleRate;
    tFslGetAudioBitsPerSample FslGetAudioBitsPerSample;
    tFslGetTextTrackWidth FslGetTextTrackWidth;
    tFslGetTextTrackHeight FslGetTextTrackHeight;
    tFslGetNextSampleSize FslGetNextSampleSize;
    tFslGetNextSample FslGetNextSample;
    tFslSeek FslSeek;
    tFslGetNextSyncSampleSize FslGetNextSyncSampleSize;
    tFslGetNextSyncSample FslGetNextSyncSample;
    FslParserHandle parserHandle;
} FslParser;

#endif  // FSL_PARSER_API_HEADER_INCLUDED
