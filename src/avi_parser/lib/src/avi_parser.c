/*
 ***********************************************************************
 * Copyright (c) 2010-2016, Freescale Semiconductor, Inc.
 * Copyright 2017-2020, 2023-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

/**
 * @file avi_parser.c
 * @AVI file parser
 */

#include <string.h>
#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "fsl_parser_drm.h"
#include "avi_parser_api.h"
#include "avi_utils.h"
#include "avi.h"
#include "mp3.h"

#ifdef SUPPORT_AVI_DRM
#include "DrmApi.h"
#include "portab.h"
// #include "DrmErrors.h"
// #include "DrmAdpApi.h"
#include <errno.h>
#include <time.h> /* clock */
#include "avi_drm.h"
#endif

/***************************************************************************************
 *                  Global variables
 ***************************************************************************************/
FslFileStream g_streamOps;
ParserMemoryOps g_memOps;
ParserOutputBufferOps g_outputBufferOps;

/***************************************************************************************
 *                  API Functions
 ***************************************************************************************/

/*--------------------------------- Version Information --------------------------------*/
#define SEPARATOR " "

#define BASELINE_SHORT_NAME "AVI_PARSER_04.00.00"

#ifdef __WINCE
#define OS_NAME "_WINCE"
#else
#define OS_NAME ""
#endif

#ifdef DEMO_VERSION
#define CODEC_RELEASE_TYPE "_DEMO"
#else
#define CODEC_RELEASE_TYPE ""
#endif

/* user define suffix */
#define VERSION_STR_SUFFIX ""

#define CODEC_VERSION_STR                                                                  \
    (BASELINE_SHORT_NAME OS_NAME CODEC_RELEASE_TYPE SEPARATOR VERSION_STR_SUFFIX SEPARATOR \
     "build on" SEPARATOR __DATE__ SEPARATOR __TIME__)

static void verifyTrackDuation(FslFileHandle parserHandle);
static bool isAudioCBR(uint32 decoderType, uint32 decoderSubtype, StreamHeaderPtr strh);

static int32 verifyMP3AudioFrameSize(FslFileHandle parserHandle);

static void getAudioFrameSize(AVStreamPtr stream);

static int32 parseHeader(FslFileHandle parserHandle);

/***************************************************************************************
 *                 DLL entry point
 ***************************************************************************************/
EXTERN int32 FslParserQueryInterface(uint32 id, void** func) {
    int32 err = PARSER_SUCCESS;

    if (!func)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    *func = NULL;

    switch (id) {
        /* creation & deletion */
        case PARSER_API_GET_VERSION_INFO:
            *func = AviParserVersionInfo;
            break;

        case PARSER_API_CREATE_PARSER:
            *func = AviCreateParser;
            break;

        case PARSER_API_CREATE_PARSER2:
            *func = AviCreateParser2;
            break;

        case PARSER_API_DELETE_PARSER:
            *func = AviDeleteParser;
            break;

        /* index loading */
        case PARSER_API_INITIALIZE_INDEX:
            *func = AviInitializeIndex;
            break;

        case PARSER_API_IMPORT_INDEX:
            *func = AviImportIndex;
            break;

        case PARSER_API_EXPORT_INDEX:
            *func = AviExportIndex;
            break;

        /* movie properties */
        case PARSER_API_IS_MOVIE_SEEKABLE:
            *func = AviIsSeekable;
            break;

        case PARSER_API_GET_MOVIE_DURATION:
            *func = AviGetMovieDuration;
            break;

        case PARSER_API_GET_USER_DATA:
            *func = AviGetUserData;
            break;

        case PARSER_API_GET_META_DATA:
            *func = AviGetMetaData;
            break;

        case PARSER_API_GET_NUM_TRACKS:
            *func = AviGetNumTracks;
            break;

        /* generic track properties */
        case PARSER_API_GET_TRACK_TYPE:
            *func = AviGetTrackType;
            break;

        case PARSER_API_GET_DECODER_SPECIFIC_INFO:
            *func = AviGetCodecSpecificInfo;
            break;

        case PARSER_API_GET_LANGUAGE:
            *func = AviGetLanguage;
            break;

        case PARSER_API_GET_TRACK_DURATION:
            *func = AviGetTrackDuration;
            break;

        case PARSER_API_GET_BITRATE:
            *func = AviGetBitRate;
            break;

        /* video properties */
        case PARSER_API_GET_VIDEO_FRAME_WIDTH:
            *func = AviGetVideoFrameWidth;
            break;

        case PARSER_API_GET_VIDEO_FRAME_HEIGHT:
            *func = AviGetVideoFrameHeight;
            break;

        case PARSER_API_GET_VIDEO_FRAME_RATE:
            *func = AviGetVideoFrameRate;
            break;

        /* audio properties */
        case PARSER_API_GET_AUDIO_NUM_CHANNELS:
            *func = AviGetAudioNumChannels;
            break;

        case PARSER_API_GET_AUDIO_SAMPLE_RATE:
            *func = AviGetAudioSampleRate;
            break;

        case PARSER_API_GET_AUDIO_BITS_PER_SAMPLE:
            *func = AviGetAudioBitsPerSample;
            break;

        case PARSER_API_GET_AUDIO_BLOCK_ALIGN:
            *func = AviGetAudioBlockAlign;
            break;

        /* text/subtitle properties */
        case PARSER_API_GET_TEXT_TRACK_WIDTH:
            *func = AviGetTextTrackWidth;
            break;

        case PARSER_API_GET_TEXT_TRACK_HEIGHT:
            *func = AviGetTextTrackHeight;
            break;

        /* sample reading, seek & trick mode */
        case PARSER_API_GET_READ_MODE:
            *func = AviGetReadMode;
            break;

        case PARSER_API_SET_READ_MODE:
            *func = AviSetReadMode;
            break;

        case PARSER_API_ENABLE_TRACK:
            *func = AviEnableTrack;
            break;

        case PARSER_API_GET_NEXT_SAMPLE:
            *func = AviGetNextSample;
            break;

        case PARSER_API_GET_NEXT_SYNC_SAMPLE:
            *func = AviGetNextSyncSample;
            break;

        case PARSER_API_GET_FILE_NEXT_SAMPLE:
            *func = AviGetFileNextSample;
            break;

        case PARSER_API_GET_FILE_NEXT_SYNC_SAMPLE:
            *func = AviGetFileNextSyncSample;
            break;

        case PARSER_API_SEEK:
            *func = AviSeek;
            break;

#ifdef SUPPORT_AVI_DRM
        case PARSER_API_IS_DRM_PROTECTED:
            *func = AviIsProtected;
            break;

        case PARSER_API_QUERY_CONTENT_USAGE:
            *func = AviQueryContentUsage;
            break;
        case PARSER_API_QUERY_OUTPUT_PROTECTION_FLAG:
            *func = AviQueryOutputProtectionFlag;
            break;
        case PARSER_API_COMMIT_PLAYBACK:
            *func = AviCommitPlayback;
            break;
        case PARSER_API_FINAL_PLAYBACK:
            *func = AviFinalizePlayback;
            break;
#endif
        default:
            break; /* no support for other API */
    }

bail:
    return err;
}

/**
 * function to get the AVI core parser version.
 *
 * @return Version string.
 */
const char* AviParserVersionInfo() {
    return (const char*)CODEC_VERSION_STR;
}

/**
 * function to create the AVI core parser.
 *
 * @param stream [in]   Source stream of the AVI file.
 *                      It implements functions to open, close, tell, read and seek a file.
 *
 * @param memOps [in]   Memory operation callback table.
 *                      It implements the functions to malloc, calloc, realloc and free memory.
 *
 * @param drmOps [in]   DRM callback function table.
 *                      It implements the functions to read and write local DRM memory.
 *
 * @param context [in]  Wrapper context for the callback functions. Avi parser never modify it.
 * @param parserHandle [out] Handle of the AVI core parser if succeeds. NULL for failure.
 * @return
 */

int32 AviCreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                      ParserOutputBufferOps* outputBufferOps, void* context,
                      FslParserHandle* parserHandle) {
    uint32 flags = 0;
    if (isLive) {
        flags |= FILE_FLAG_NON_SEEKABLE;
        flags |= FILE_FLAG_READ_IN_SEQUENCE;
    }
    return AviCreateParser2(flags, streamOps, memOps, outputBufferOps, context, parserHandle);
}

/**
 * function to create the AVI core parser.
 * @param flag [in]  	flags to be set when create parser.
 *                      3 flags can be set now:
 *                      a.	FILE_FLAG_NON_SEEKABLE (0x01)
 *                      b.	FILE_FLAG_READ_IN_SEQUENCE(0x02)
 *                      c.	FLAGS_H264_NO_CONVERT(0x04)
 *
 * @param stream [in]   Source stream of the AVI file.
 *                      It implements functions to open, close, tell, read and seek a file.
 *
 * @param memOps [in]   Memory operation callback table.
 *                      It implements the functions to malloc, calloc, realloc and free memory.
 *
 * @param drmOps [in]   DRM callback function table.
 *                      It implements the functions to read and write local DRM memory.
 *
 * @param context [in]  Wrapper context for the callback functions. Avi parser never modify it.
 * @param parserHandle [out] Handle of the AVI core parser if succeeds. NULL for failure.
 * @return
 */

int32 AviCreateParser2(uint32 flags, FslFileStream* streamOps, ParserMemoryOps* memOps,
                       ParserOutputBufferOps* outputBufferOps, void* context,
                       FslParserHandle* parserHandle) {
    int32 err = PARSER_SUCCESS;
    AviObject* self = NULL;
    AviInputStream* inputStream = NULL;
    int64 fileSize = -1;
    const uint8 openflag[] = "rb";

    if ((NULL == streamOps) || (NULL == memOps) || (NULL == parserHandle)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }
    /* NOTE: drmOps may be NULL if not to support DRM */

    *parserHandle = NULL;

    g_streamOps.Open = streamOps->Open;
    g_streamOps.Read = streamOps->Read;
    g_streamOps.Seek = streamOps->Seek;
    g_streamOps.Tell = streamOps->Tell;
    g_streamOps.Size = streamOps->Size;
    g_streamOps.Close = streamOps->Close;
    g_streamOps.CheckAvailableBytes = streamOps->CheckAvailableBytes;

    g_memOps.Calloc = memOps->Calloc;
    g_memOps.Malloc = memOps->Malloc;
    g_memOps.Free = memOps->Free;
    g_memOps.ReAlloc = memOps->ReAlloc;

    g_outputBufferOps.RequestBuffer = outputBufferOps->RequestBuffer;
    g_outputBufferOps.ReleaseBuffer = outputBufferOps->ReleaseBuffer;

#ifdef AVI_MEM_DEBUG_SELF
    mm_mm_init();
#endif

    self = LOCALCalloc(1, sizeof(AviObject));
    TESTMALLOC(self)

    self->primaryStreamNum = PARSER_INVALID_TRACK_NUMBER;

    inputStream = LOCALCalloc(1, sizeof(AviInputStream));
    if (!inputStream)
        BAILWITHERROR(PARSER_INSUFFICIENT_MEMORY)

    /* try to open the source stream */
    inputStream->fileHandle = LocalFileOpen(NULL, openflag, context);
    if (!inputStream->fileHandle) {
        AVIMSG("AviCreateParser: error: can not open source stream\n");
        BAILWITHERROR(PARSER_FILE_OPEN_ERROR)
    }

    self->isLive = FALSE;
    if ((flags & FILE_FLAG_NON_SEEKABLE) && (flags & FILE_FLAG_READ_IN_SEQUENCE)) {
        self->isLive = TRUE;
    }
    AVIMSG("Input stream is a live source ? %d\n", self->isLive);

    self->au_flag = flags;

    fileSize = LocalFileSize(inputStream, context);
    if (fileSize < 0) {
        AVIMSG("warning: file size <= 0 (%llu), set it to max\n", self->fileSize);
        self->fileSize = (uint32)(-1);
    } else {
        self->fileSize = (uint64)fileSize;
    }

#ifdef SUPPORT_LARGE_AVI_FILE
    if (MIN_AVI_FILE_SIZE >= (int64)self->fileSize && !self->isLive)
#else
    if (MIN_AVI_FILE_SIZE >= (int32)self->fileSize && !self->isLive)
#endif
    {
        AVIMSG("error: file size %lld is bad or exceeds parser's capacity!\n", self->fileSize);
        BAILWITHERROR(PARSER_ILLEAGAL_FILE_SIZE)
    }

    self->inputStream = inputStream;

    self->appContext = context;

    err = parseHeader((FslParserHandle)self);
    if (PARSER_SUCCESS != err) {
        AVIMSG("error: fail to parser file header, err %d!\n", err);
        goto bail;
    }
    self->fileHeaderParsed = TRUE;

    self->readMode =
            PARSER_READ_MODE_FILE_BASED; /* prefer file-based mode for performance consideration */

    if (self->isLive) {
        self->seekable = FALSE;
    }

    self->bCorruptedIdx = FALSE;
#ifdef SUPPORT_AVI_DRM
    // Initialize the DRM API function pointers here
    if (self->protected) {
        self->bHasDrmLib = FALSE;
        self->hDRMLib = NULL;
        memset((void*)(&self->sDrmAPI), 0, sizeof(drmAPI_s));
        self->bHasDrmLib = LoadDrmLibrary(self);
        if (self->bHasDrmLib) {
            AVIMSG("Load DRM Library OK! \n");
        } else {
            AVIMSG("Failed to load DRM Library! \n");
        }
    }

#endif

bail:
    if (PARSER_SUCCESS != err) {
        if (inputStream) {
            disposeFileHandler(inputStream, context);
        }

        if (self) {
            LOCALFree(self);
            self = NULL;
        }

#ifdef AVI_MEM_DEBUG_SELF
        mm_mm_exit();
#endif
    } else {
        *parserHandle = (FslFileHandle*)self;
        AVIMSG("AviCreateParser:parser created successfully\n");
    }

    return err;
}

/**
 * function to delete the AVI core parser.
 *
 * @param parserHandle Handle of the AVI core parser.
 * @return
 */
int32 AviDeleteParser(FslFileHandle parserHandle) {
    int32 err = PARSER_SUCCESS;
    AviObject* self = (AviObject*)parserHandle;
    int i;
    AVStreamPtr stream;

    if (NULL == self) {
        AVIMSG("AviDeleteParser: error: invalid parser handle\n");
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if (self->inputStream) {
        disposeFileHandler(self->inputStream, self->appContext);
    }

    for (i = 0; i < MAX_AVI_TRACKS; i++) {
        stream = self->streams[i];
        if (stream) {
            if (stream->indexTab) {
                LOCALFree(stream->indexTab);
                stream->indexTab = NULL;
            }

            if (stream->cache) {
                alignedFree(stream->cache);
                stream->cache = NULL;
            }

#ifdef OPEN_FILE_ONCE_PER_TRACK
            if (stream->inputStream) {
                disposeFileHandler(stream->inputStream, self->appContext);
                stream->inputStream = NULL;
                AVIMSG("trk %d, dispose file hander\n", i);
            }
#endif

            LOCALFree(stream);
            self->streams[i] = NULL;
        }
    }

    /* TODO: free other memory blks */
    DESTROY_ATOM(riff)

    if (self->drmContext) {
        LOCALFree(self->drmContext);
    }
#ifdef SUPPORT_AVI_DRM
    if ((self->protected) && (self->bHasDrmLib)) {
        UnloadDrmLibrary(self);
    }
#endif

    LOCALFree(self);

#ifdef AVI_MEM_DEBUG_SELF
    mm_mm_exit();
#endif
    AVIMSG("AviCreateParser:parser deleted\n");
    return err;
}

#ifdef SUPPORT_AVI_DRM
/**
 * DRM interface.function to see whether file is protected by DRM.
 * The wrapper shall call the DRM interface right after the file header is parsed for a quick
 * decision. before doing the time-consuming task such as initialize index table.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param isProtected [out]True for protected file.
 */
int32 AviIsProtected(FslFileHandle parserHandle, bool* isProtected) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    *isProtected = self->protected;
    return err;
}

/**
 * DRM interface.function to see whether file is a rental or purchased movie.
 * This API shall be called once before playing a protected clip.
 *
 * @param parserHandle[in] Handle of the AVI core parser.
 * @param isRental[out] True for a rental file and False for a puchase file. Reatanl file has a view
 * limit.
 * @param viewLimit[out] View limit if a rental file.
 * @param viewCount [out]Count of views played already.
 * @return
 */
int32 AviQueryContentUsage(FslFileHandle parserHandle, bool* isRental, uint32* viewLimit,
                           uint32* viewCount) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    *isRental = FALSE;
    *viewLimit = 0;
    *viewCount = 0;

    if (FALSE == self->protected) /* shall not call this API */
        BAILWITHERROR(AVI_ERR_DRM_NOT_PROTECTED)

    if (self->bHasDrmLib) { /* init DRM playback */
        drmErrorCodes_t result;
        uint32 drmContextLength = 0;
        uint8* drmContext;

        uint8_t rentalMessageFlag = FALSE;
        uint8_t useLimit;
        uint8_t useCount;
        int i;
        int interval_ms = 80;

        if (NULL != self->drmContext)
            BAILWITHERROR(AVI_ERR_DRM_PREV_PLAY_NOT_CLEAERED)

        result = self->sDrmAPI.drmInitSystem(NULL, &drmContextLength);
        if (DRM_SUCCESS != result) {
            DRMMSG("fail to init DRM system\n");
            goto bail;
        }

        drmContext = (uint8_t*)LOCALCalloc(1, drmContextLength);
        TESTMALLOC(drmContext)
        self->drmContext = drmContext;

        result = self->sDrmAPI.drmInitSystem(self->drmContext, &drmContextLength);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 0\n",
                   self->sDrmAPI.drmGetLastError(drmContext));
            goto bail;
        }

        for (i = 0; i < 3; i++) {
#if !(defined(__WINCE) || defined(WIN32))
            struct timespec ts;

            ts.tv_sec = (interval_ms) / 1000;
            ts.tv_nsec = ((interval_ms)-1000 * ts.tv_sec) * 1000000;
            while (nanosleep(&ts, &ts) && errno == EINTR) {
                ;
            } /* continue sleeping when interrupted by signal */
#else
            Sleep(interval_ms);
#endif
            {
                self->sDrmAPI.drmSetRandomSample(drmContext);
            }
        }

        result = self->sDrmAPI.drmInitPlayback(drmContext, self->drmHeader);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 1\n",
                   self->sDrmAPI.drmGetLastError(drmContext));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }

        /*rental status */
        result = self->sDrmAPI.drmQueryRentalStatus(drmContext, &rentalMessageFlag, &useLimit,
                                                    &useCount);
        DRMMSG("drmQueryRentalStatus return code %d, use limit %d, use count %d\n", result,
               useLimit, useCount);

        *isRental = rentalMessageFlag;
        *viewLimit = useLimit;
        *viewCount = useCount;

        if (DRM_RENTAL_EXPIRED == result) {
            DRMMSG("Screen 3 (rental expired), code %d - Step 2\n",
                   self->sDrmAPI.drmGetLastError(drmContext));
            *isRental = TRUE; /* When expired, rentalMessageFlag is ZERO */
            BAILWITHERROR(DRM_ERR_RENTAL_EXPIRED)
        }

        else if (DRM_NOT_AUTHORIZED == result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 2\n",
                   self->sDrmAPI.drmGetLastError(drmContext));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }

        else if (DRM_SUCCESS != result) {
            DRMMSG("drm:4 DRM Message 1 (generic), code %d - Screen 2\n", result);
            BAILWITHERROR(AVI_ERR_DRM_OTHERS)
        }
    } else {
        BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
    }

bail:
    return err;
}

/**
 * DRM interface. function to check the video output protection flag.
 *
 * @param parserHandle[in] - Handle of the AVI core parser.
 * @param cgmsaSignal[out] - 0, 1, 2, or 3 based on standard CGMSA signaling.
 * @param acptbSignal[out] - 0, 1, 2, or 3 based on standard trigger bit signaling.
 *                                      acptb values:
 *                                      0 = Off.
 *                                      1 = Auto gain control / pseudo sync pulse.
 *                                      2 = Two line color burst.
 *                                      3 = Four line color burst.
 * @param digitalProtectionSignal[out]  - 0=off, 1=on.
 * @return PARSER_SUCCESS - success. Others - failure.
 */
int32 AviQueryOutputProtectionFlag(FslFileHandle parserHandle, uint8* cgmsaSignal,
                                   uint8* acptbSignal, uint8* digitalProtectionSignal,
                                   uint8* ictSignal) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    *cgmsaSignal = 0;
    *acptbSignal = 0;
    *digitalProtectionSignal = 0;
    *ictSignal = 0;

    if (FALSE == self->protected) /* shall not call this API */
        BAILWITHERROR(AVI_ERR_DRM_NOT_PROTECTED)

    if (self->bHasDrmLib) {
        drmErrorCodes_t result;
        uint8_t* context = self->drmContext;
        int i;
        int interval_ms = 80;

        if (NULL == context)
            BAILWITHERROR(AVI_ERR_DRM_INVALID_CONTEXT)

        for (i = 0; i < 3; i++) {
#if !(defined(__WINCE) || defined(WIN32))
            struct timespec ts;

            ts.tv_sec = (interval_ms) / 1000;
            ts.tv_nsec = ((interval_ms)-1000 * ts.tv_sec) * 1000000;
            while (nanosleep(&ts, &ts) &&
                   errno == EINTR); /* continue sleeping when interrupted by signal */
#else
            Sleep(interval_ms);
#endif
            self->sDrmAPI.drmSetRandomSample(context);
        }

        result = self->sDrmAPI.drmQueryCgmsa(context, cgmsaSignal);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 4\n",
                   self->sDrmAPI.drmGetLastError(context));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }
        DRMMSG("%d cgmsa signal\n", *cgmsaSignal);

        result = self->sDrmAPI.drmQueryAcptb(context, acptbSignal);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 5\n",
                   self->sDrmAPI.drmGetLastError(context));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }
        DRMMSG("%d acptb signal\n", *acptbSignal);

        result = self->sDrmAPI.drmQueryDigitalProtection(context, digitalProtectionSignal);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 6\n",
                   self->sDrmAPI.drmGetLastError(context));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }
        DRMMSG("%d digital protection signal\n", *digitalProtectionSignal);

        result = self->sDrmAPI.drmQueryIct(context, ictSignal);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 7\n",
                   self->sDrmAPI.drmGetLastError(context));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }
    } else {
        BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
    }

bail:

    return err;
}

/**
 * DRM interface.function to commit playing the protected file.The wrapper shall call it before
 * playback is started.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @return
 */
int32 AviCommitPlayback(FslFileHandle parserHandle) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    if (FALSE == self->protected) /* shall not call this API */
        BAILWITHERROR(AVI_ERR_DRM_NOT_PROTECTED)

    if (self->bHasDrmLib) {
        drmErrorCodes_t result;
        uint8_t* context = self->drmContext;

        if (NULL == context)
            BAILWITHERROR(AVI_ERR_DRM_INVALID_CONTEXT)

        result = self->sDrmAPI.drmCommitPlayback(context);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 7\n",
                   self->sDrmAPI.drmGetLastError(context));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }
    } else {
        BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
    }

bail:
    return err;
}

/**
 * DRM interface.function to end playing the protected file.
 * The wrapper shall call it after playback is stopped.
 * Otherwise error "AVI_ERR_DRM_PREV_PLAY_NOT_CLEAERED" on next playback.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @return
 */
int32 AviFinalizePlayback(FslFileHandle parserHandle) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    if (FALSE == self->protected) /* shall not call this API */
        BAILWITHERROR(AVI_ERR_DRM_NOT_PROTECTED)

    if (self->bHasDrmLib) {
        uint8_t* context = self->drmContext;

        if (NULL == context)
            BAILWITHERROR(AVI_ERR_DRM_INVALID_CONTEXT)

        self->sDrmAPI.drmFinalizePlayback(context);

        LOCALFree(context);
        self->drmContext = NULL;
    } else {
        BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
    }

bail:
    return err;
}

#endif

/**
 * Function to initialize the index table by scanning the index in the file.
 * For a movie played for the 1st time, the index table has to be loaded from the file.
 * The index table increases with the movie duration. So the longer the movie is,
 * the more time it takes to load the index.
 *
 * Seeking and trick mode can be performed on a movie only if it has a index.
 * But normal playback does not depend on the index. So even if this function fails,
 * normal playback can still work as long as movie data are right.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @return
 */
int32 AviInitializeIndex(FslFileHandle parserHandle) {
    int32 err = AVI_ERR_NO_INDEX; /* assume no index at first, not affecting normal playback */
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    RiffTitlePtr riff = (RiffTitlePtr)self->riff;
    AVStreamPtr stream = NULL;
    AVStreamPtr baseStream = NULL;

    uint32 i;
    bool avi2IndexPresent = FALSE; /*AVI 2.0 present? */

    HeaderListPtr hdrl;
    StreamHeaderListPtr strl;
    BaseIndexPtr indx;

    if (self->isLive) {
        BAILWITHERROR(PARSER_SUCCESS)
    }

    if (self->indexLoaded)
        BAILWITHERROR(AVI_ERR_INDEX_ALREADY_LOADED)

    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];
        /* init max sample size as suggested buffer size,
        to avoid frequently file reading to verify index entry size. */
        stream->maxSampleSize = stream->suggestedBufferSize;
        if (MEDIA_AUDIO == stream->mediaType) {
            getScaledTime(stream, DEFAULT_AUDIO_INDEX_INTERVAL * 1000 * 1000,
                          &stream->defaultAudioIndexInterval);
        }
    }

    /* load index from the file, may fail if index table is corrupted.
    Try to load AVI2.0 at first, if not available then trying AVI 1.0 index.*/

    avi2IndexPresent = FALSE;
    hdrl = (HeaderListPtr)riff->hdrl;

    if (INVALID_TRACK_NUM == (int32)self->primaryStreamNum)
        BAILWITHERROR(AVI_ERR_NO_PRIMARY_TRACK)

    baseStream = self->streams[self->primaryStreamNum];
    if (NULL == baseStream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    /* 1st scan: index primary(base) stream */
    AVIMSG("index primary trak %d...\n", self->primaryStreamNum);
    strl = (StreamHeaderListPtr)hdrl->strl[self->primaryStreamNum];
    indx = (BaseIndexPtr)strl->indx;
    if (indx) {
        avi2IndexPresent = TRUE;
        if (AVI_INDEX_OF_INDEXES == indx->indexType)
            err = loadSuperIndex(self, self->primaryStreamNum, (SuperIndexPtr)indx, NULL);

        else if (AVI_INDEX_OF_CHUNKS == indx->indexType)
            err = loadStandardIndex(self, self->primaryStreamNum, (StdIndexPtr)indx, NULL);

        if ((NULL != baseStream->indexTab) && (PARSER_SUCCESS != err) && (NULL != stream)) {
            LOCALFree(baseStream->indexTab);
            baseStream->indexTab = NULL;
            baseStream->numSamples = 0;
            baseStream->numIndexEntries = 0;
        }
    }

    if (avi2IndexPresent) {
        /* 2nd scan: index secondary streams (video, audio, text) */
        for (i = 0; i < self->numStreams; i++) {
            stream = self->streams[i];
            if (stream == baseStream)
                continue;

            if ((MEDIA_VIDEO == stream->mediaType) || (MEDIA_AUDIO == stream->mediaType) ||
                (MEDIA_TEXT == stream->mediaType)) {
                AVIMSG("index secondary trak %d...\n", i);
                strl = (StreamHeaderListPtr)hdrl->strl[i];
                indx = (BaseIndexPtr)strl->indx;
                if (indx) {
                    if (AVI_INDEX_OF_INDEXES == indx->indexType)
                        err = loadSuperIndex(self, i, (SuperIndexPtr)indx, baseStream);

                    else if (AVI_INDEX_OF_CHUNKS == indx->indexType)
                        err = loadStandardIndex(self, i, (StdIndexPtr)indx, baseStream);

                    if ((NULL != stream->indexTab) && (PARSER_SUCCESS != err)) {
                        LOCALFree(stream->indexTab);
                        stream->indexTab = NULL;
                        stream->numSamples = 0;
                        stream->numIndexEntries = 0;
                    }
                }
            }
        }
    }

    /* if avi 2.0 super index is not present or loading fails, try avi 1.0 index */
    if ((FALSE == avi2IndexPresent) || (PARSER_SUCCESS != err)) {
        if (riff->idx1) /*AVI 1.0 */
        {
            err = loadIdx1(self);
        } else {
            AVIMSG("No idx1 is present\n");
        }
    }

    if (PARSER_SUCCESS == err) {
        checkInterleavingDepth(self);
    }

bail:
    /* stream's max sample size is first get from stream header ,
    even if index table not available, need to calculate the movie's max sample size for future
    error check!*/
    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];

        AVIMSG("\ntrk %d, index %lld samples\n", i, stream->numIndexEntries);

        stream->maxSampleSize =
                (stream->maxSampleSize + 1) & (~1); /* round the max sample size to EVEN */

        if ((0 == stream->maxSampleSize) ||
            (PARSER_SUCCESS != err)) {         /* index table can not be loaded */
            if (stream->suggestedBufferSize) { /* use suggested buffer size */
                stream->maxSampleSize = stream->suggestedBufferSize;
                AVIMSG("Track %d, max sample size %d, use suggested buffer size %d\n", i,
                       stream->maxSampleSize, stream->suggestedBufferSize);
            } else {
                /* Neither suggested buffer size given in stream header nor index table exists */
                stream->maxSampleSize =
                        INVALID_MEDIA_SAMPLE_SIZE; /* set as maximum. NOTE: it will be exported! */
            }
        } else if ((stream->suggestedBufferSize) &&
                   (stream->maxSampleSize > stream->suggestedBufferSize)) {
            AVIMSG("Track %d, max sample size %d, bigger than suggested buffer size %d\n", i,
                   stream->maxSampleSize, stream->suggestedBufferSize);
        }
        AVIMSG("Track %d, max sample size %d\n", i, stream->maxSampleSize);
        AVIMSG("Track %d, totally %lld samples, %lld scaled units\n", i, stream->numSamples,
               stream->cumLength);
    }

    /* set text track duration equal to 1st video's duration. Even if movie has no index, video can
    got duration from stream header. Otherwise text duration is 0, and will get "seek to end" err.*/
    if (PARSER_SUCCESS == err)
        verifyTrackDuation(parserHandle);

    if (PARSER_SUCCESS == err) {
        self->indexLoaded = TRUE;
        if (baseStream &&
            baseStream->numIndexEntries) { /* ENGR119088, if index table indicates no key frames,
                                           still not seekable. If non primary tracks have no index,
                                           will seek to BOS or EOS.*/
            self->seekable = TRUE;
        } else {
            AVIMSG("No sync sample is actually indexed although index table is present for the "
                   "primary track\n");
            self->seekable = FALSE; /* export can still speed up next loading */
        }

    } else /* index table is corrupted, modify the seekable flag */
    {
        AVIMSG("Warning: Failed to load index. The file is NOT seekable!\n");
        self->seekable = FALSE;
    }

    return err;
}

/**
 * function to import the index table of a track from outside, instead of scanning the file.
 * This can reduce time to open a 2nd-play movie if its index table has been exported on 1st play.
 * To save the memory used, import/export a track's index per time.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param buffer [in] Buffer containing the index data.
 * @param size [in] Size of the index data in the buffer, in bytes.
 * @return
 */
int32 AviImportIndex(FslFileHandle parserHandle, uint8* buffer, uint32 size) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    if (self->isLive) {
        err = PARSER_SUCCESS;
    } else {
        err = importIndex(self, buffer, size);
    }

    return err;
}

/**
 * function to export the index table of track from outside, after the index table is loaded by
 * scanning the file at 1st play. This function is usually used on a 1st play movie. This can reduce
 * time to open a 2nd-play movie if its index table has been exported on 1st play. To save the
 * memory used, import/export a track's index per time.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param buffer [in] Buffer to export the index data.
 *                             If this parameter is NULL, just return the size of buffer needed
 * without exporting the index data.
 *
 * @param size [in/out] Size of the the buffer as input, in bytes.
 *                                 Size of the index data in the buffer as output, in bytes.
 * @return
 */
int32 AviExportIndex(FslParserHandle parserHandle, /* Import index from outside */
                     uint8* buffer, uint32* size) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    if (self->isLive) {
        err = PARSER_SUCCESS;
    } else {
        err = exportIndex(self, buffer, size);
    }

    return err;
}

/**
 * function to tell whether the movie is seekable. A seekable AVI movie must have the index table.
 * If the file's index table is loaded from file or imported successfully, it's seekable.
 * Seeking and trick mode can be performed on a seekable file.
 * If the index table is corrupted, the file is NOT seekable. This function will fail and return
 * value can tell the error type.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param seekable [out] true for seekable and false for non-seekable
 * @return
 */
int32 AviIsSeekable(FslFileHandle parserHandle, bool* seekable) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    *seekable = self->seekable;

    return err;
}

/**
 * function to tell how many tracks in the movie.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param numTracks [out] Number of tracks.
 * @return
 */
int32 AviGetNumTracks(FslFileHandle parserHandle, uint32* numTracks) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    *numTracks = self->numStreams;

    return err;
}

/**
 * function to tell the user data information (title, artist, genre etc) of the movie. User data
 * API.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param id [in] User data ID, type of the user data.
 * @param buffer [out] Buffer containing the information. The core parser manages this buffer and
 * the user shall NOT free it. If no such info is availabe, this value will be set to NULL.
 * @param size [out] Length of the information in bytes. The informaiton is usually a
 * null-terminated ASCII string. If no such info is available, this value will be set to 0.
 * @return
 */
int32 AviGetUserData(FslFileHandle parserHandle, uint32 id, uint16** unicodeString,
                     uint32* stringLength) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    RiffTitlePtr riff = (RiffTitlePtr)self->riff;
    InfoListPtr info = (InfoListPtr)riff->info;
    UserDataAtomPtr atom = NULL;

    *unicodeString = NULL;
    *stringLength = 0;

    if (NULL == info)
        goto bail;

    switch (id) {
        case USER_DATA_TITLE:
            atom = (UserDataAtomPtr)info->inam;
            break;

        case USER_DATA_LANGUAGE:
            atom = (UserDataAtomPtr)info->ilng;
            break;

        case USER_DATA_GENRE:
            atom = (UserDataAtomPtr)info->ignr;
            break;

        case USER_DATA_ARTIST:
            atom = (UserDataAtomPtr)info->iart;
            break;

        case USER_DATA_COPYRIGHT:
            atom = (UserDataAtomPtr)info->icop;
            break;

        case USER_DATA_COMMENTS:
            atom = (UserDataAtomPtr)info->icmt;
            break;

        case USER_DATA_CREATION_DATE:
            atom = (UserDataAtomPtr)info->icrd;
            break;

        case USER_DATA_RATING:
            atom = (UserDataAtomPtr)info->irtd;
            break;

        case USER_DATA_FORMATVERSION:
            atom = (UserDataAtomPtr)info->idfv;
            break;

        case USER_DATA_PROFILENAME:
            atom = (UserDataAtomPtr)info->idpn;
            break;

        case USER_DATA_TOOL:
            atom = (UserDataAtomPtr)info->isft;
            break;

        case USER_DATA_KEYWORDS:
            atom = (UserDataAtomPtr)info->ikey;
            break;

        case USER_DATA_LOCATION:
            atom = (UserDataAtomPtr)info->iarl;
            break;

        default:
            atom = NULL;
    }

    if (atom && atom->data && atom->size) {
        *unicodeString = atom->unicodeString;
        *stringLength = atom->stringLength;
    }

bail:
    return err;
}

/**
 * function to tell the meta data information (title, artist, genre etc) of the movie. User data
 * API.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param userDataId [in] User data ID. Type of the user data. Value can be from enum
 * FSL_PARSER_USER_DATA_TYPE
 * @param userDataFormat [in/out] format of user data. Value can be from enum
 * FSL_PARSER_USER_DATA_FORMAT
 * @param userData [out] Buffer containing the information. The core parser manages this buffer and
 * the user shall NOT free it. If no such info is availabe, this value will be set to NULL.
 * @param userDataLength [out] Length of the information in bytes. The informaiton is usually a
 * null-terminated ASCII string. If no such info is available, this value will be set to 0.
 * @return
 */
int32 AviGetMetaData(FslFileHandle parserHandle, UserDataID userDataId,
                     UserDataFormat* userDataFormat, uint8** userData, uint32* userDataLength) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    RiffTitlePtr riff = (RiffTitlePtr)self->riff;
    InfoListPtr info = (InfoListPtr)riff->info;
    UserDataAtomPtr atom = NULL;

    *userData = NULL;
    *userDataLength = 0;

    if (NULL == info)
        goto bail;

    if (*userDataFormat != USER_DATA_FORMAT_UTF8)
        goto bail;

    switch (userDataId) {
        case USER_DATA_TITLE:
            atom = (UserDataAtomPtr)info->inam;
            break;

        case USER_DATA_LANGUAGE:
            atom = (UserDataAtomPtr)info->ilng;
            break;

        case USER_DATA_GENRE:
            atom = (UserDataAtomPtr)info->ignr;
            break;

        case USER_DATA_ARTIST:
            atom = (UserDataAtomPtr)info->iart;
            break;

        case USER_DATA_COPYRIGHT:
            atom = (UserDataAtomPtr)info->icop;
            break;

        case USER_DATA_COMMENTS:
            atom = (UserDataAtomPtr)info->icmt;
            break;

        case USER_DATA_CREATION_DATE:
            atom = (UserDataAtomPtr)info->icrd;
            break;

        case USER_DATA_RATING:
            atom = (UserDataAtomPtr)info->irtd;
            break;

        case USER_DATA_FORMATVERSION:
            atom = (UserDataAtomPtr)info->idfv;
            break;

        case USER_DATA_PROFILENAME:
            atom = (UserDataAtomPtr)info->idpn;
            break;

        case USER_DATA_TOOL:
            atom = (UserDataAtomPtr)info->isft;
            break;

        case USER_DATA_KEYWORDS:
            atom = (UserDataAtomPtr)info->ikey;
            break;

        case USER_DATA_LOCATION:
            atom = (UserDataAtomPtr)info->iarl;
            break;

        default:
            atom = NULL;
    }

    if (atom && atom->data && atom->size) {
        *userData = atom->data;
        *userDataLength = atom->size;
    }

bail:
    return err;
}

/**
 * function to tell the movie duration.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param usDuration [out] Duration in us.
 * @return
 */
int32 AviGetMovieDuration(
        FslFileHandle parserHandle,
        uint64* usDuration) /* return file duration in us, duration of the longest track */
{
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    uint32 i;
    AVStreamPtr stream;

    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];
        if (self->usLongestTrackDuration < stream->usDuration)
            self->usLongestTrackDuration = stream->usDuration;
    }

    *usDuration = self->usLongestTrackDuration;
    return err;
}

/**
 * function to tell a track's duration.
 * The tracks may have different durations.
 * And the movie's duration is usually the video track's duration (maybe not the longest one).
 * Warning: For some media track such as subtitle, the duration is ZERO.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param usDuration [out] Duration in us.
 * @return
 */
int32 AviGetTrackDuration(FslFileHandle parserHandle, uint32 trackNum, uint64* usDuration) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    *usDuration = stream->usDuration;

bail:
    return err;
}

/**
 * function to tell the type of a track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 *
 * @param mediaType [out] Media type of the track. (video, audio, subtitle...)
 *                        "MEDIA_TYPE_UNKNOWN" means the media type is unknown.
 *
 * @param decoderType [out] Decoder type of the track if available. (eg. MPEG-4, H264, AAC, MP3, AMR
 * ...) "UNKNOWN_CODEC_TYPE" means the decoder type is unknown.
 *
 * @param decoderSubtype [out] Decoder Subtype type of the track if available. (eg. AMR-NB, AMR-WB
 * ...) "UNKNOWN_CODEC_SUBTYPE" means the decoder subtype is unknown.
 * @return
 */
int32 AviGetTrackType(FslFileHandle parserHandle, uint32 trackNum, uint32* mediaType,
                      uint32* decoderType, uint32* decoderSubtype) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if ((MEDIA_VIDEO != stream->mediaType) && (MEDIA_AUDIO != stream->mediaType) &&
        (MEDIA_TEXT != stream->mediaType)) {
        if ((UNKNOWN_CODEC_TYPE != stream->decoderType) ||
            (UNKNOWN_CODEC_SUBTYPE != stream->decoderType))
            BAILWITHERROR(PARSER_ERR_UNKNOWN)
    }

    *mediaType = (uint32)stream->mediaType;
    *decoderType = stream->decoderType;
    *decoderSubtype = stream->decoderSubtype;

bail:
    return err;
}

/**
 * function to tell the codec specific information of a track.
 * It's the data of stream format atom (strf).
 * It's a Windows bitmap header for video track (at least 40 bytes)
 * and a Windows Waveform audio header for audio (at least 16 or 18 bytes).
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param data [out] Buffer holding the codec specific information. The user shall never free this
 * buffer.
 * @param size [out] Size of the codec specific information, in bytes.
 * @return
 */
int32 AviGetCodecSpecificInfo(FslFileHandle parserHandle, uint32 trackNum, uint8** data,
                              uint32* size) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    RiffTitlePtr riff;
    HeaderListPtr hdrl;
    StreamHeaderListPtr strl;
    StreamFormatPtr strf;
    uint32 au_flag;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    *data = NULL;
    *size = 0;

    au_flag = self->au_flag;

    riff = (RiffTitlePtr)self->riff;
    hdrl = (HeaderListPtr)riff->hdrl;
    strl = (StreamHeaderListPtr)hdrl->strl[trackNum];
    strf = (StreamFormatPtr)strl->strf;

    if (strf && strf->decoderSpecificInfo) {
        if ((MEDIA_VIDEO == stream->mediaType) &&
            ((VIDEO_WMV == stream->decoderType) || (VIDEO_MPEG4 == stream->decoderType))) {
            if (strf->decoderSpecificInfoSize > BITMAPINFO_SIZE) {
                *data = strf->decoderSpecificInfo + BITMAPINFO_SIZE;
                *size = strf->decoderSpecificInfoSize - BITMAPINFO_SIZE;
            }
        }
        // else if( (MEDIA_VIDEO == stream->mediaType) && (VIDEO_H264 == stream->decoderType) )
        else if ((MEDIA_VIDEO == stream->mediaType) &&
                 ((VIDEO_H264 == stream->decoderType) || (VIDEO_HEVC == stream->decoderType))) {
            if (au_flag & FLAG_H264_NO_CONVERT) {
                if (strf->decoderSpecificInfoSize > BITMAPINFO_SIZE) {
                    *data = strf->decoderSpecificInfo + BITMAPINFO_SIZE;
                    *size = strf->decoderSpecificInfoSize - BITMAPINFO_SIZE;
                }
            } else {
                *data = strf->decoderSpecificInfo;
                *size = strf->decoderSpecificInfoSize;
            }
            /* this fix is for some avi with fake hvcc codec data,
                 and is byte-stream actually  MA-13897 */
            if (VIDEO_HEVC == stream->decoderType) {
                if (*data) {
                    if ((*data)[0] != 1) {
                        *data = NULL;
                        *size = 0;
                    }
                }
            }
        } else if ((MEDIA_AUDIO == stream->mediaType) &&
                   ((AUDIO_WMA == stream->decoderType) || (AUDIO_ADPCM == stream->decoderType) ||
                    (AUDIO_AAC == stream->decoderType) || (AUDIO_VORBIS == stream->decoderType))) {
            if (strf->decoderSpecificInfoSize > WAVEFORMATEX_SIZE) {
                *data = strf->decoderSpecificInfo + WAVEFORMATEX_SIZE;
                *size = strf->decoderSpecificInfoSize - WAVEFORMATEX_SIZE;
                if (TRUE == strf->waveformat_extensible) {
                    *data += WAVEFORMATEXTENSIBLE_SIZE;
                    *size -= WAVEFORMATEXTENSIBLE_SIZE;
                }
            }
        }
    }

bail:
    return err;
}

// disable the function as we does not use it.
#if 0
/**
 * function to tell the maximum sample size of a track.
 * Avi parser read A/V tracks sample by sample. The max sample size can help the user to prepare a big enough buffer.
 * Warning!The "max sample size" can be zero if the file header information is not complete or index table is not available.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param size [out] Max sample size of the track. Warning!It can be zero if index table does not exist.
 * @return
 */
int32 AviGetMaxSampleSize(FslFileHandle parserHandle, uint32 trackNum, uint32 * size)
{
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    if(trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if(NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if(INVALID_MEDIA_SAMPLE_SIZE != (int32)stream->maxSampleSize)
        *size = stream->maxSampleSize;
    else
         *size = 0;


  bail:
    return err;

}
#endif

/**
 * function to tell the language of a track used.
 * This is helpful to select an video/audio/subtitle track or menu pages.
 *
 * @param parserHandle [in] Handle of the MP4 core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param threeCharCode [out] Three or two character language code.
 *                  See ISO 639-2/T for the set of three character codes.Eg. 'eng' for English.
 *                  Four special case:
 *                  mis- "uncoded languages"
 *                  mul- "multiple languages"
 *                  und- "undetermined language"
 *                  zxx- "no linguistic content"
 *
 *                  See ISO 639 for the set of two character codes. Eg. 'en' for English and'zh'for
 * chinese. If ISO 639 is used, the 3rd character will be '\0'.
 * @return
 */
EXTERN int32 AviGetLanguage(FslFileHandle parserHandle, uint32 trackNum, uint8* threeCharCode) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    RiffTitlePtr riff;
    HeaderListPtr hdrl;
    StreamHeaderListPtr strl;

    StreamNamePtr strn;
    uint8 audioDescriptionPrefix[] = "Audio - ";
    uint8 subtitleDescriptionPrefix[] = "Subtitle - ";

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    riff = (RiffTitlePtr)self->riff;
    hdrl = (HeaderListPtr)riff->hdrl;
    strl = (StreamHeaderListPtr)hdrl->strl[trackNum];
    strn = (StreamNamePtr)strl->strn;

    threeCharCode[0] = 'u';
    threeCharCode[1] = 'n';
    threeCharCode[2] = 'd';

    if (strn && strn->nameString) {
        strn->nameString[strn->nameSize - 1] = 0; /* for display name */

        /* Try to pick up language from audio/subtitle description.
        Video usually does not indicate language */

        if ((MEDIA_AUDIO == stream->mediaType) &&
            (strn->nameSize > (STREAM_NAME_AUDIO_PREFIX_SIZE + 3))) {
            if (0 ==
                memcmp(audioDescriptionPrefix, strn->nameString, STREAM_NAME_AUDIO_PREFIX_SIZE)) {
                threeCharCode[0] = strn->nameString[STREAM_NAME_AUDIO_PREFIX_SIZE];
                threeCharCode[1] = strn->nameString[STREAM_NAME_AUDIO_PREFIX_SIZE + 1];
                threeCharCode[2] = strn->nameString[STREAM_NAME_AUDIO_PREFIX_SIZE + 2];
            }

        } else if ((MEDIA_TEXT == stream->mediaType) &&
                   (TXT_DIVX_FEATURE_SUBTITLE == stream->decoderType) &&
                   (strn->nameSize > (STREAM_NAME_SUBTITLE_PREFIX_SIZE + 3))) {
            if (0 == memcmp(subtitleDescriptionPrefix, strn->nameString,
                            STREAM_NAME_SUBTITLE_PREFIX_SIZE)) {
                threeCharCode[0] = strn->nameString[STREAM_NAME_SUBTITLE_PREFIX_SIZE];
                threeCharCode[1] = strn->nameString[STREAM_NAME_SUBTITLE_PREFIX_SIZE + 1];
                threeCharCode[2] = strn->nameString[STREAM_NAME_SUBTITLE_PREFIX_SIZE + 2];
            }
        }

        if ('-' == threeCharCode[2])
            threeCharCode[2] = 0;
    }

bail:
    return err;
}

/**
 * function to tell the bitrate of a track.
 * For CBR stream, the real bitrate is given.
 * For VBR stream, 0 is given since the bitrate varies during the playback and AVI parser does not
 * calculate the peak or average bit rate.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param bitrate [out] Bitrate. For CBR stream, this is the real bitrate.
 *                                            For VBR stream, the bitrate is 0 since max bitrate is
 * usually not available.
 * @return
 */
int32 AviGetBitRate(FslFileHandle parserHandle, uint32 trackNum, uint32* bitrate) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    *bitrate = (stream->bytesPerSec) << 3;

bail:
    return err;
}

// disable the function as we does not use it.
#if 0
/**
 * function to tell the sample duration in us of a track.
 * If the sample duration is not a constant (eg. some audio, subtilte), 0 is given.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param usDuration [out] Sample duration in us. If sample duration is not a constant, this value is 0.
 * @return
 */
int32 AviGetSampleDuration(FslFileHandle parserHandle, uint32 trackNum, uint64 *usDuration)
{
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

     if(trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if(NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

   *usDuration = stream->usFixedSampleDuration;

bail:
    return err;
}

#endif

/**
 * function to tell the width in pixels of a video track.
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a video track.
 * @param width [out] Width in pixels.
 * @return
 */
int32 AviGetVideoFrameWidth(FslFileHandle parserHandle, uint32 trackNum, uint32* width) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    RiffTitlePtr riff;
    HeaderListPtr hdrl;
    StreamHeaderListPtr strl;
    StreamFormatPtr strf;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (MEDIA_VIDEO != stream->mediaType)
        BAILWITHERROR(AVI_ERR_WRONG_MEDIA_TYPE)

    riff = (RiffTitlePtr)self->riff;
    hdrl = (HeaderListPtr)riff->hdrl;
    strl = (StreamHeaderListPtr)hdrl->strl[trackNum];
    strf = (StreamFormatPtr)strl->strf;

    *width = strf->bitmapInfo.width;
bail:
    return err;
}

/**
 * function to tell the height in pixels of a video track.
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a video track.
 * @param height [out] Height in pixels.
 * @return
 */
int32 AviGetVideoFrameHeight(FslFileHandle parserHandle, uint32 trackNum, uint32* height) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    RiffTitlePtr riff;
    HeaderListPtr hdrl;
    StreamHeaderListPtr strl;
    StreamFormatPtr strf;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (MEDIA_VIDEO != stream->mediaType)
        BAILWITHERROR(AVI_ERR_WRONG_MEDIA_TYPE)

    riff = (RiffTitlePtr)self->riff;
    hdrl = (HeaderListPtr)riff->hdrl;
    strl = (StreamHeaderListPtr)hdrl->strl[trackNum];
    strf = (StreamFormatPtr)strl->strf;

    *height = strf->bitmapInfo.height;
bail:
    return err;
}

EXTERN int32 AviGetVideoFrameRate(FslParserHandle parserHandle, uint32 trackNum, uint32* rate,
                                  uint32* scale) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    if (NULL == self)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];
    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (MEDIA_VIDEO != stream->mediaType)
        BAILWITHERROR(PARSER_ERR_INVALID_MEDIA)

    *rate = stream->rate;
    *scale = stream->scale;

bail:
    return err;
}

/**
 * function to tell how many channels in an audio track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to an audio track.
 * @param numchannels [out] Number of the channels. 1 mono, 2 stereo, or more for multiple channels.
 * @return
 */
int32 AviGetAudioNumChannels(FslFileHandle parserHandle, uint32 trackNum, uint32* numchannels) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    RiffTitlePtr riff;
    HeaderListPtr hdrl;
    StreamHeaderListPtr strl;
    StreamFormatPtr strf;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (MEDIA_AUDIO != stream->mediaType)
        BAILWITHERROR(AVI_ERR_WRONG_MEDIA_TYPE)

    riff = (RiffTitlePtr)self->riff;
    hdrl = (HeaderListPtr)riff->hdrl;
    strl = (StreamHeaderListPtr)hdrl->strl[trackNum];
    strf = (StreamFormatPtr)strl->strf;

    *numchannels = (uint32)strf->waveFomatEx.channels;

bail:
    return err;
}

/**
 * function to tell the audio sample rate (sampling frequency) of an audio track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to an audio track.
 * @param sampleRate [out] Audio integer sample rate (sampling frequency).
 * @return
 */
int32 AviGetAudioSampleRate(FslFileHandle parserHandle, uint32 trackNum, uint32* sampleRate) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    RiffTitlePtr riff;
    HeaderListPtr hdrl;
    StreamHeaderListPtr strl;
    StreamFormatPtr strf;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (MEDIA_AUDIO != stream->mediaType)
        BAILWITHERROR(AVI_ERR_WRONG_MEDIA_TYPE)

    riff = (RiffTitlePtr)self->riff;
    hdrl = (HeaderListPtr)riff->hdrl;
    strl = (StreamHeaderListPtr)hdrl->strl[trackNum];
    strf = (StreamFormatPtr)strl->strf;

    *sampleRate = (uint32)strf->waveFomatEx.samplesPerSec;

bail:
    return err;
}

/**
 * function to tell the bits per sample for a PCM audio track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a PCM audio track.
 * @param bitsPerSample [out] Bits per PCM sample. For non-PCM audio, it will be ZERO.
 * @return
 */
EXTERN int32 AviGetAudioBitsPerSample(FslFileHandle parserHandle, uint32 trackNum,
                                      uint32* bitsPerSample) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    RiffTitlePtr riff;
    HeaderListPtr hdrl;
    StreamHeaderListPtr strl;
    StreamFormatPtr strf;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (MEDIA_AUDIO != stream->mediaType)
        BAILWITHERROR(AVI_ERR_WRONG_MEDIA_TYPE)

    riff = (RiffTitlePtr)self->riff;
    hdrl = (HeaderListPtr)riff->hdrl;
    strl = (StreamHeaderListPtr)hdrl->strl[trackNum];
    strf = (StreamFormatPtr)strl->strf;

    *bitsPerSample = (uint32)strf->waveFomatEx.bitsPerSample;

bail:
    return err;
}

/**
 * function to tell the block alignment for an audio track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a audio track.
 * @param blockAlign [out] Block alignment in bytes.
 * @return
 */
EXTERN int32 AviGetAudioBlockAlign(FslParserHandle parserHandle, uint32 trackNum,
                                   uint32* blockAlign) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    RiffTitlePtr riff;
    HeaderListPtr hdrl;
    StreamHeaderListPtr strl;
    StreamFormatPtr strf;

    if ((trackNum >= self->numStreams) || (NULL == blockAlign))
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (MEDIA_AUDIO != stream->mediaType)
        BAILWITHERROR(AVI_ERR_WRONG_MEDIA_TYPE)

    riff = (RiffTitlePtr)self->riff;
    hdrl = (HeaderListPtr)riff->hdrl;
    strl = (StreamHeaderListPtr)hdrl->strl[trackNum];
    strf = (StreamFormatPtr)strl->strf;

    *blockAlign = (uint32)strf->waveFomatEx.blockAlgn;

bail:
    return err;
}

/**
 * Function to tell the width of a text track.
 * The text track defines a window to display the subtitles.
 * This window shall be positioned in the middle of the screen.
 * And the sample is displayed in the window.How to position the sample within the window is defined
 * by the sample data. The origin of window is always (0, 0).
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param width [out] Width of the text track, in pixels.
 * @return
 */
int32 AviGetTextTrackWidth(FslFileHandle parserHandle, uint32 trackNum, uint32* width) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (MEDIA_TEXT != stream->mediaType)
        BAILWITHERROR(AVI_ERR_WRONG_MEDIA_TYPE)

    *width = 640;

bail:
    return err;
}

/**
 * Function to tell the height of a text track.
 * The text track defines a window to display the subtitles.
 * This window shall be positioned in the middle of the screen.
 * And the sample is displayed in the window.How to position the sample within the window is defined
 * by the sample data. The origin of window is always (0, 0).
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param height [out] Height of the window, in pixels.
 * @return
 */
int32 AviGetTextTrackHeight(FslFileHandle parserHandle, uint32 trackNum, uint32* height) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (MEDIA_TEXT != stream->mediaType)
        BAILWITHERROR(AVI_ERR_WRONG_MEDIA_TYPE)

    *height = 480;

bail:
    return err;
}

/**
 * Function to set the mode to read media samples, file-based or track-based. *
 * Warning:
 *  - The parser may support only one reading mode.Setting a not-supported reading mode will fail.
 *  - Once selected, the reading mode can no longer change.
 *
 * @param parserHandle [in] Handle of the MP4 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param readMode [in] Sample reading mode.
 *
 *                      READ_MODE_FILE_BASED
 *                      Default mode.Linear sample reading. The reading order is same as that of
 *                      track interleaving in the file.
 *
 *                      READ_MODE_TRACK_BASED
 *                      Track-based sample reading. Each track can be read independently from each
 * other.
 * @return
 * PARSER_SUCCESS   The reading mode is set successfully.
 * PARSER_NOT_IMPLEMENTED   The reading mode is not supported.
 *
 */
EXTERN int32 AviSetReadMode(FslParserHandle parserHandle, uint32 readMode) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    uint32 i;

    if (NULL == self)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    if (readMode == self->readMode)
        return err;

#if 0
    if((PARSER_READ_MODE_TRACK_BASED == readMode) && self->isLive)
    {
        AVIMSG("ERR!Live source can not support track-based sample reading!\n");
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)
    }
#endif /* if the streaming source clips is totally cached, track-based mode is also OK! */

    if (self->isDeepInterleaving && (PARSER_READ_MODE_FILE_BASED == readMode)) {
        AVIMSG("ERR! CAN NOT support file-based reading mode on a deep interleaving clip!\n");
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)
    }

    self->readMode = readMode;
    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];
        if (NULL == stream)
            BAILWITHERROR(PARSER_ERR_UNKNOWN)
        stream->readMode = readMode;
    }
    AVIMSG("Change read mode to %d\n", readMode);

bail:
    return err;
}

/**
 * Function to get the mode to read media samples, file-based or track-based. *
 * And the parser has a default read mode.
 * For streaming application, file-based reading is mainly for streaming application.
 *
 * @param parserHandle [in] Handle of the MP4 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param readMode [out] Current Sample reading mode.
 * @return
 */
EXTERN int32 AviGetReadMode(FslParserHandle parserHandle, uint32* readMode) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;

    if (NULL == self)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    *readMode = self->readMode;

bail:
    return err;
}

/**
 * Function to enable or disable track.
 * The parser can only output samples from enabled tracks.
 * To avoid unexpected memory cost or data output from a track, the application can disable the
 * track.
 *
 * @param parserHandle [in] Handle of the MP4 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param enable [in] TRUE to enable the track and FALSE to disable it.
 * @return
 */
EXTERN int32 AviEnableTrack(FslParserHandle parserHandle, uint32 trackNum, bool enable) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    if (NULL == self)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];
    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (enable == stream->enabled) {
        AVIMSG("No need to turn on/off trk %d (current state %d)\n", trackNum, stream->enabled);
        return err; /* no need to change. */
    }

    stream->enabled = enable;
    if (!enable) {
        AVIMSG("disable trk %d\n", trackNum);
    } else {
        AVIMSG("enable trk %d\n", trackNum);
    }

    /* reset reading status of this track or all BSAC tracks */
    if (!enable) {
        resetTrackReadingStatus(stream);
    }

bail:
    return err;
}

/**
 * function to read the next sample from a track.
 *
 * For clips not protected by DRM, it supports partial output of large samples:
 * If the entire sample can not be output by calling this function once, its remaining data
 * can be got by repetitive calling the same function.
 *
 * For DRM-protected clips, can only output entire samples for decryption need.
 *
 * For A/V tracks, the time stamp of samples are continuous. If a sample is output, its start time
 * and end time are also output. But for subtitle text tracks, the time stamp & duration are
 * discontinuous and encoded in the sample data.So the parser gives an "estimated" time stamp. The
 * decoder shall decode the accurate time stamp.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track to read, 0-based.
 * @param sampleData [in]   Buffer to hold the sample data.
 *                          For DRM-protected clips, it must be large enough to hold the entire
 * sample for decryption need. Otherwise this function will fail. For non-protected clips, if the
 * buffer is not big enough, only the 1st part of sample is output.
 *
 * @param dataSize [in/out]  Size of the buffer as input, in bytes.
 *                             As output:
 *
 *                              If a sample or part of sample is output successfully (return value
 * is PARSER_SUCCESS ), it's the size of the data actually got.
 *
 *                              If the sample can not be output at all because buffer is too small
 *                              (the return value is PARSER_INSUFFICIENT_MEMORY), it's the buffer
 * size needed. Only for DRM-protected files.
 *
 * @param usStartTime [out] Start time of the sample in us (timestamp)
 * @param usEndTime [out] End time of the sample in us.
 *
 * @param flag [out] Flags of this sample if a sample is got successfully.
 *
 *                            FLAG_SYNC_SAMPLE
 *                                  Whether this sample is a sync sample (key frame).
 *                                  For non-video media, the wrapper shall take every sample as sync
 * sample.
 *
 *                            FLAG_UNCOMPRESSED_SAMPLE
 *                                  Whethter this sample is a uncompressed one. Uncompressed samples
 * shall bypass the decoder. Warning: Video track may have both compressed & uncompressed samples.
 *                                                But some AVI clips seem to abuse this flag, sync
 * samples are mark as uncompressed, although they are actually compressed ones.
 *
 *                            FLAG_SAMPLE_ERR_CONCEALED
 *                                  There is error in bitstream but a sample is still got by error
 * concealment.
 *
 *                            FLAG_SAMPLE_SUGGEST_SEEK
 *                                  A seeking on ALL tracks is suggested although samples can be got
 * by error concealment. Because there are many corrupts, and A/V sync is likely impacted by simple
 * concealment(scanning bitstream).
 *
 *                            FLAG_SAMPLE_NOT_FINISHED
 *                                  Sample data output is not finished because the buffer is not big
 * enough. Need to get the remaining data by repetitive calling this func. This feature is only for
 * non-protected clips.
 *
 *
 * @return  PARSER_SUCCESS     An entire sample or part of it is got successfully.
 *          PARSER_EOS     No sample is got because of end of the track.
 *          PARSER_INSUFFICIENT_MEMORY Buffer is not big enough to hold the entire sample.
 *                                  The user can allocate a larger buffer and call this API again.
 * (Only for DRM-protected clips). PARSER_READ_ERROR  File reading error. No need for further error
 * concealment. PARSER_ERR_CONCEAL_FAIL  There is error in bitstream, and no sample can be found by
 * error concealment. A seeking is helpful. Others ...
 */
EXTERN int32 AviGetNextSample(FslParserHandle parserHandle, uint32 trackNum, uint8** sampleBuffer,
                              void** bufferContext, uint32* dataSize, uint64* usStartTime,
                              uint64* usDuration, uint32* sampleFlags) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream = self->streams[trackNum];

    if (PARSER_READ_MODE_TRACK_BASED != self->readMode)
        return PARSER_ERR_INVALID_READ_MODE;

    if (!stream->enabled)
        return PARSER_ERR_TRACK_DISABLED;

    if (!stream->sampleBytesLeft) {
        /* output the next media sample */
        err = getNextSample(parserHandle, trackNum, sampleBuffer, bufferContext, dataSize,
                            usStartTime, usDuration, sampleFlags);
    } else { /* current sample output is not finished yet */
        bool finished;
        err = getSampleRemainingBytes(parserHandle, trackNum, sampleBuffer, bufferContext, dataSize,
                                      &finished);

        if (PARSER_SUCCESS == err) {
            *usStartTime = stream->usSampleStartTime;
            *usDuration = stream->usSampleDuration;

            if (finished) /* clear the "NotFinished" flag */
                *sampleFlags = stream->sampleFlag & (~FLAG_SAMPLE_NOT_FINISHED);
            else
                *sampleFlags = stream->sampleFlag;
        }
    }

    return err;
}

EXTERN int32 AviGetFileNextSample(FslParserHandle parserHandle, uint32* trackNum,
                                  uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                                  uint64* usStartTime, uint64* usDuration, uint32* sampleFlags) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    uint32 streamIndex;

    if (PARSER_READ_MODE_FILE_BASED != self->readMode)
        return PARSER_ERR_INVALID_READ_MODE;

    if (NULL == self->nextStream) {
        if (self->isNewSegment) {
            findMinFileOffset(self);
            self->isNewSegment = FALSE;
        }

        err = getFileNextChunkHead(parserHandle, trackNum);
        if (PARSER_SUCCESS != err)
            return err;

        streamIndex = *trackNum;
        stream = self->streams[streamIndex];

        if (stream->sampleBytesLeft)
            BAILWITHERROR(PARSER_ERR_UNKNOWN)

        if (!stream->enabled)
            BAILWITHERROR(PARSER_ERR_TRACK_DISABLED)

        /* output the next media sample */
        stream->fileOffset = self->fileOffset;
        err = getNextSample(parserHandle, streamIndex, sampleBuffer, bufferContext, dataSize,
                            usStartTime, usDuration, sampleFlags);

        self->fileOffset = stream->fileOffset;

        if (stream->sampleBytesLeft)
            self->nextStream = stream;

    }

    else {
        bool finished;

        stream = self->nextStream;

        if (!stream->sampleBytesLeft)
            BAILWITHERROR(PARSER_ERR_UNKNOWN)

        streamIndex = stream->streamIndex;
        *trackNum = streamIndex;

        stream->fileOffset = self->fileOffset;
        err = getSampleRemainingBytes(parserHandle, streamIndex, sampleBuffer, bufferContext,
                                      dataSize, &finished);
        self->fileOffset = stream->fileOffset;

        if (PARSER_SUCCESS == err) {
            *usStartTime = stream->usSampleStartTime;
            *usDuration = stream->usSampleDuration;

            if (finished) /* clear the "NotFinished" flag */
                *sampleFlags = stream->sampleFlag & (~FLAG_SAMPLE_NOT_FINISHED);
            else
                *sampleFlags = stream->sampleFlag;

            if (finished)
                self->nextStream = NULL;
        }
    }

bail:
    if ((PARSER_SUCCESS != err) && (PARSER_ERR_NO_OUTPUT_BUFFER != err))
        self->nextStream = NULL;

    return err;
}

/**
 * function to get the next or previous sync sample (key frame) from current reading position of a
 * track. For trick mode FF/RW. Also support partial output of large samples for clips not protected
 * by DRM. If not the entire sample is got, its remaining data can be got by repetitive calling the
 * same function.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track to read, 0-based.
 * @param direction [in]  Direction to get the sync sample.
 *                           FLAG_FORWARD   Read the next sync sample from current reading position.
 *                           FLAG_BACKWARD  Read the previous sync sample from current reading
 * position.
 *
 * @param sampleData [in]   Buffer to hold the sample data.
 *                          For DRM-protected clips, it must be large enough to hold the entire
 * sample for decryption need. Otherwise this function will fail. For non-protected clips, if the
 * buffer is not big enough, only the 1st part of sample is output.
 *
 * @param dataSize [in/out]  Size of the buffer as input, in bytes.
 *                             As output:
 *
 *                              If a sample or part of sample is output successfully (return value
 * is PARSER_SUCCESS ), it's the size of the data actually got.
 *
 *                              If the sample can not be output at all because buffer is too small
 *                              (the return value is PARSER_INSUFFICIENT_MEMORY), it's the buffer
 * size needed. Only for DRM-protected files.
 *
 * @param usStartTime [out] Start time of the sample in us (timestamp)
 * @param usEndTime [out] End time of the sample in us.
 * @param flag [out] Flags of this sample.
 *                            FLAG_SYNC_SAMPLE
 *                                  Whether this sample is a sync sample (key frame).
 *                                  For non-video media, the wrapper shall take every sample as sync
 * sample. This flag shall always be SET for this API.
 *
 *                            FLAG_UNCOMPRESSED_SAMPLE
 *                                  Whethter this sample is a uncompressed one. Uncompressed samples
 * shall bypass the decoder. Warning: Video track may have both compressed & uncompressed samples.
 *                                                But some AVI clips seem to abuse this flag, sync
 * samples are mark as uncompressed, although they are actually compressed ones.
 *
 *                            FLAG_SAMPLE_ERR_CONCEALED
 *                                  There is error in bitstream but a sample is still got by error
 * concealment.
 *
 *                            FLAG_SAMPLE_SUGGEST_SEEK
 *                                  A seeking on ALL tracks is suggested although samples can be got
 * by error concealment. Because there are many corrupts, and A/V sync is likely impacted by simple
 * concealment(scanning bitstream).
 *
 *                            FLAG_SAMPLE_NOT_FINISHED
 *                                  Sample data output is not finished because the buffer is not big
 * enough. Need to get the remaining data by repetitive calling this func. This feature is only for
 * non-protected clips.
 *
 * @return  PARSER_SUCCESS     An entire sync sample or part of it is got successfully.
 *          PARSER_ERR_NOT_SEEKABLE    No sync sample is got  because the movie is not seekable
 * (index not available) * PARSER_EOS      Reaching the end of the track, no sync sample is got.
 *          PARSER_BOS      Reaching the beginning of the track, no sync sample is got.
 *          PARSER_INSUFFICIENT_MEMORY Buffer is too small to hold the sample.
 *                                  The user can allocate a larger buffer and call this API again
 * (Only for DRM-protected clips). PARSER_READ_ERROR  File reading error. No need for further error
 * concealment. PARSER_ERR_CONCEAL_FAIL  There is error in bitstream, and no sample can be found by
 * error concealment. A seeking is helpful. Others ... Reading fails for other reason.
 */
int32 AviGetNextSyncSample(FslFileHandle parserHandle, uint32 direction, uint32 trackNum,
                           uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                           uint64* usStartTime, uint64* usDuration, uint32* flag) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    uint32 sampleFlag;

    if (PARSER_READ_MODE_TRACK_BASED != self->readMode)
        return PARSER_ERR_INVALID_READ_MODE;

    if (!self->seekable)
        BAILWITHERROR(PARSER_ERR_NOT_SEEKABLE)

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if (!stream->enabled) {
        BAILWITHERROR(PARSER_ERR_TRACK_DISABLED)
    }

    if (!stream->sampleBytesLeft) /* read a new sample */
    {
        if (!stream->chunkHeaderRead) { /* the next sync sample size has not been pre-gotten yet.
                                        otherwise, seeking is already done.*/
            err = seek2SyncSample(self, stream, direction);
            if (PARSER_SUCCESS != err)
                goto bail;
        }

        err = getNextSample(parserHandle, trackNum, sampleBuffer, bufferContext, dataSize,
                            usStartTime, usDuration, &sampleFlag);

        if (!(sampleFlag & FLAG_SYNC_SAMPLE) && (PARSER_SUCCESS == err))
            BAILWITHERROR(PARSER_ERR_UNKNOWN)

        *flag = sampleFlag;
    } else {
        /* current sample output is not finished yet */
        bool finished;
        err = getSampleRemainingBytes(parserHandle, trackNum, sampleBuffer, bufferContext, dataSize,
                                      &finished);

        if (PARSER_SUCCESS == err) {
            *usStartTime = stream->usSampleStartTime;
            *usDuration = stream->usSampleDuration;

            if (finished) /* clear the "NotFinished" flag */
                *flag = stream->sampleFlag & (~FLAG_SAMPLE_NOT_FINISHED);
            else
                *flag = stream->sampleFlag;
        }
    }

bail:

    return err;
}

EXTERN int32 AviGetFileNextSyncSample(FslParserHandle parserHandle, uint32 direction,
                                      uint32* trackNum, uint8** sampleBuffer, void** bufferContext,
                                      uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                                      uint32* sampleFlags) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    uint32 i;
    AVStreamPtr stream;
    uint32 streamIndex;
    bool eos = TRUE; /* end of movie */

    if (PARSER_READ_MODE_FILE_BASED != self->readMode)
        return PARSER_ERR_INVALID_READ_MODE;

    if (NULL == self->nextStream) {
        for (i = 0; i < self->numStreams; i++) {
            stream = self->streams[i];
            if (!stream->enabled)
                continue;

            err = seek2SyncSample(
                    self, stream,
                    direction); /* if a sync sample is not read, shall repeat this sample! */

            if (PARSER_SUCCESS == err)
                eos = FALSE; /* as long as one track can find a sync sample */

            else if ((PARSER_EOS != err) && (PARSER_BOS != err))
                goto bail;
        }

        if (eos) { /* all enabled tracks reach EOS */
            if (FLAG_FORWARD == direction)
                return PARSER_EOS;
            else
                return PARSER_BOS;
        }

        if (FLAG_FORWARD == direction)
            findMinFileOffset(self);
        else
            findMaxFileOffset(self);

        err = getFileNextChunkHead(parserHandle, trackNum);
        if (PARSER_SUCCESS != err)
            return err;

        streamIndex = *trackNum;
        stream = self->streams[streamIndex];

        if (stream->sampleBytesLeft)
            BAILWITHERROR(PARSER_ERR_UNKNOWN)

        if (!stream->enabled)
            BAILWITHERROR(PARSER_ERR_TRACK_DISABLED)

        /* output the next media sample */
        stream->fileOffset = self->fileOffset;
        err = getNextSample(parserHandle, streamIndex, sampleBuffer, bufferContext, dataSize,
                            usStartTime, usDuration, sampleFlags);

        self->fileOffset = stream->fileOffset;

        if (stream->sampleBytesLeft)
            self->nextStream = stream;

    }

    else {
        bool finished;

        stream = self->nextStream;
        if (!stream->sampleBytesLeft)
            BAILWITHERROR(PARSER_ERR_UNKNOWN)

        streamIndex = stream->streamIndex;

        stream->fileOffset = self->fileOffset;
        err = getSampleRemainingBytes(parserHandle, streamIndex, sampleBuffer, bufferContext,
                                      dataSize, &finished);
        self->fileOffset = stream->fileOffset;

        if (PARSER_SUCCESS == err) {
            *usStartTime = stream->usSampleStartTime;
            *usDuration = stream->usSampleDuration;

            if (finished) /* clear the "NotFinished" flag */
                *sampleFlags = stream->sampleFlag & (~FLAG_SAMPLE_NOT_FINISHED);
            else
                *sampleFlags = stream->sampleFlag;

            if (finished)
                self->nextStream = NULL;
        }
    }

bail:
    if (PARSER_SUCCESS != err)
        self->nextStream = NULL;

    return err;
}

/**
 * Function to seek a track to a target time. It will seek to a sync sample of the time stamp
 * matching the target time. Due to the scarcity of the video sync samples (key frames),
 * there can be a gap between the target time and the timestamp of the matched sync sample.
 * So this time stamp will be output to as the accurate start time of the following playback
 * segment. NOTE: Seeking to the beginning of the movie (target time is 0 us) does not require the
 * index table.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track to seek, 0-based.
 * @param usTime [in/out]  Target time to seek as input, in us.
 *                         Actual seeking time, timestamp of the matched sync sample, as output.
 *
 * @param flag [in]  Control flags to seek.
 *
 *                      SEEK_FLAG_NEAREST
 *                      Default flag.The matched time stamp shall be the nearest one to the target
 * time (may be later or earlier).
 *
 *                      SEEK_FLAG_NO_LATER
 *                      The matched time stamp shall be no later than the target time.
 *
 *                      SEEK_FLAG_NO_EARLIER
 *                      The matched time stamp shall be no earlier than the target time.
 *
 * @return  PARSER_SUCCESS    Seeking succeeds.
 *          PARSER_ERR_NOT_SEEKABLE    Seeking fails because he movie is not seekable (index not
 * available) Others      Seeking fails for other reason.
 */
int32 AviSeek(FslFileHandle parserHandle, uint32 trackNum, uint64* usTime, uint32 flag) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    if ((!self->seekable) &&
        (0 != *usTime)) /* permit seeking to the beginning even without index table */
        BAILWITHERROR(PARSER_ERR_NOT_SEEKABLE)

    if (trackNum >= self->numStreams)
        BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)

    stream = self->streams[trackNum];

    if (NULL == stream)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)

    if ((SEEK_FLAG_NO_LATER != flag) && (SEEK_FLAG_NO_EARLIER != flag))
        flag = SEEK_FLAG_NEAREST;

    AVIMSG("\nTrk %d, seek (flag %d)  to target time %lld us...\n", trackNum, flag, *usTime);

    err = seekTrack(self, stream, usTime, flag);

    if ((PARSER_SUCCESS == err) || (PARSER_EOS == err))
        self->isNewSegment = TRUE;

    self->nextStream = NULL;

bail:
    return err;
}

#ifndef AVI_MEM_DEBUG

void* LOCALCalloc(uint32 number, uint32 size) {
    return g_memOps.Calloc(number, size);
}

void* LOCALMalloc(uint32 size) {
    return g_memOps.Malloc(size);
}

void LOCALFree(void* MemoryBlock) {
    g_memOps.Free(MemoryBlock);
}

void* LOCALReAlloc(void* MemoryBlock, uint32 size) {
    return g_memOps.ReAlloc(MemoryBlock, size);
}
#endif

/**
 * function to parse the AVI file header. It shall be called after AVI parser is created
 * and it will probe whether the movie can be handled by this AVI parser.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @return
 */
static int32 parseHeader(FslFileHandle parserHandle) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AviInputStream* inputStream = self->inputStream;
    void* appContext = self->appContext;
    RiffTitlePtr riff;
    HeaderListPtr hdrl;

    StreamHeaderListPtr strl;
    StreamHeaderPtr strh;
    StreamFormatPtr strf;
    StreamHeaderDataPtr strd;

    MovieListPtr movi;

    uint32 i;
    bool FirstVideoGot = FALSE;
    uint32 tag = 0;
    AVStreamPtr stream;
    AVStreamPtr videoStream = NULL; /* first video stream */

    /* Is it a RIFF file? Check 1st 4 bytes */
    if (LocalFileSeek(inputStream, 0, SEEK_SET, appContext))
        BAILWITHERROR(PARSER_SEEK_ERROR)

    err = read32(inputStream, &tag, appContext);
    if (err)
        goto bail;
    if (RIFFTag != tag) {
        AVIMSG("No RIFF found\n");
        BAILWITHERROR(PARSER_ERR_INVALID_MEDIA)
    }

    if (LocalFileSeek(inputStream, 0, SEEK_SET, appContext))
        BAILWITHERROR(PARSER_SEEK_ERROR)

    /* "file" atom is the root atom, but its size info may be wrong because file size may exceeds
    2GB. But we don't use this size later.*/
    INIT_ATOM(FileTag, (uint32)self->fileSize, (uint32)self->fileSize, 0, NULL, self->isLive,
              (uint32)self->au_flag)

    /* create RIFF title atom by scanning the file header */
    GET_ATOM(riff)

    if (self->fileSize < self->riff->size + 8) {
        AVIMSG("... Warning: riff size %d, proper file size shall be %d, but actual file size %d\n",
               (uint32)self->riff->size, (uint32)self->riff->size + 8, (uint32)self->fileSize);
        /* try to tolerate this */
    }
    riff = (RiffTitlePtr)self->riff;

    /* How many streams & stream properties*/
    hdrl = (HeaderListPtr)riff->hdrl;
    self->numStreams = hdrl->numStreams;

    for (i = 0; i < self->numStreams; i++) {
        self->streams[i] = LOCALCalloc(1, sizeof(AVStream));
        TESTMALLOC(self->streams[i])

        stream = self->streams[i];

        strl = (StreamHeaderListPtr)(hdrl->strl[i]);
        strh = (StreamHeaderPtr)(strl->strh);
        strf = (StreamFormatPtr)strl->strf;

        stream->streamIndex = i;

#ifdef OPEN_FILE_ONCE_PER_TRACK
        /* open its own file handler */
        stream->inputStream = duplicateFileHandler(self->inputStream);
        if (NULL == stream->inputStream)
            BAILWITHERROR(PARSER_FILE_OPEN_ERROR)

        stream->inputStream->fileHandle = LocalFileOpen(NULL, "rb", appContext);
        if (!stream->inputStream->fileHandle) {
            AVIMSG("trk %d, can not open file handler\n", i);
            BAILWITHERROR(PARSER_FILE_OPEN_ERROR)
        }
        AVIMSG("trk %d, file handler opened: %p\n", i, stream->inputStream->fileHandle);
#else
        /* share one file handler */
        stream->inputStream = self->inputStream;
#endif

        stream->scale = strh->scale;
        stream->rate = strh->rate;
        stream->startDelay = strh->start;
        stream->suggestedBufferSize = (strh->suggestedBufferSize + 1) & (~1);

        // initialize the first /last sample's File Offset
        stream->firstSampleFileOffset = 0;
        stream->lastSampleFileOffset = self->fileSize;

        if (VideoTag == strh->fccType) {
            if ((fourcc('d', 'i', 'v', 'x') == strh->fccHandler) &&
                ((fourcc('D', 'X', 'S', 'B') == strf->bitmapInfo.compression) || /* Xsub */
                 (fourcc('D', 'X', 'S', 'A') == strf->bitmapInfo.compression)))  /* Xsub Plus */
            { /*subtitle stream */
                stream->mediaType = MEDIA_TEXT;
                stream->decoderType = TXT_DIVX_FEATURE_SUBTITLE;
                stream->tag = fourcc(i / 10 + '0', i % 10 + '0', 's', 'b');
                stream->isCbr = FALSE;

            } else /*video stream */
            {
                videoStream = stream;
                stream->mediaType = MEDIA_VIDEO;
                stream->tag = fourcc(i / 10 + '0', i % 10 + '0', 'd', 'c'); /* compressed video*/
                stream->uncompressedVideoTag =
                        fourcc(i / 10 + '0', i % 10 + '0', 'd',
                               'b'); /* raw video, a stream may put some non-compressed video ahead
                                        of compressed sequence. */

                getVideoCodecType(strh->fccHandler, strf->bitmapInfo.compression,
                                  &stream->decoderType, &stream->decoderSubtype);
                stream->isCbr = FALSE;
                if (strh->rate)
                    stream->usFixedSampleDuration = (uint64)strh->scale * 1000 * 1000 / strh->rate;

                if (!FirstVideoGot) {
                    FirstVideoGot = TRUE;
                    self->videoTag = stream->tag;
                    self->drmTag = fourcc(i / 10 + '0', i % 10 + '0', 'd', 'd');
                    self->primaryStreamNum = i;
                    AVIMSG("video trk %d is the primary track\n", self->primaryStreamNum);

                    /* DRM */
                    strd = (StreamHeaderDataPtr)(strl->strd);
                    if ((VIDEO_DIVX == stream->decoderType) && (strd && strd->drmInfoSize)) {
                        self->protected = TRUE;
                        self->drmHeadeSize = strd->drmInfoSize;
                        self->drmHeader = strd->drmInfo;
                    }

                    /* AVI 2.0 ? */
                    if (strl->indx)
                        self->isAvi2 = TRUE;
                }
            }
        }

        else if (AudioTag == strh->fccType) /*  audio stream */
        {
            stream->mediaType = MEDIA_AUDIO;
            stream->tag = fourcc(i / 10 + '0', i % 10 + '0', 'w', 'b');

            getAudioCodecType(strh->fccHandler, strf->waveFomatEx.formatTag,
                              strf->waveFomatEx.bitsPerSample, &stream->decoderType,
                              &stream->decoderSubtype);
            if (strf->waveFomatEx.samplesPerSec)
                stream->sampleRate = strf->waveFomatEx.samplesPerSec;
            else
                stream->sampleRate = 44100;  // avoid div zero exception
            stream->blockAlign = strf->waveFomatEx.blockAlgn;

            getAudioFrameSize(stream);

            stream->isCbr = isAudioCBR(stream->decoderType, stream->decoderSubtype,
                                       strh); /* cbr or vbr ? */
            if (!stream->isCbr) {
                if (stream->sampleRate)
                    stream->usFixedSampleDuration =
                            (uint64)stream->audioFrameSize * 1000 * 1000 / stream->sampleRate;
                AVIMSG("VBR audio\n"); /* TODO: other type of audio */
            } else {
                if (strf->waveFomatEx.avgBytesPerSec)
                    stream->bytesPerSec = strf->waveFomatEx.avgBytesPerSec;
                else
                    stream->bytesPerSec = 1;
                AVIMSG("CBR audio, average byte rate: %d\n", stream->bytesPerSec);
                if (strh->rate)
                    stream->usFixedSampleDuration = (strh->scale * 1000 / strh->rate * 1000);
            }
        }

        if (stream->rate) {
            stream->usDuration = ((uint64)strh->length * strh->scale * 1000 / strh->rate) * 1000;
            AVIMSG("track %d, duration %lld\n", i, stream->usDuration);
        }
#if 0  // if the rate is 0, the pts output could be -1
        else if ((MEDIA_AUDIO == stream->mediaType) || (MEDIA_VIDEO == stream->mediaType))
        {
            AVIMSG("ERR: track %d, A/V track has ZERO rate\n", i); /* subtitle stream can have ZERO rate */
            BAILWITHERROR(AVI_ERR_ZERO_STREAM_RATE)
        }
#endif
    }

    /* let text track share 1st video track's scale & rate, for seeking.
    (Text track's original scale & rate are 0).
    Not handle duration here, because video own duration may need correction after index table
    loaded.*/
    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];
        if ((MEDIA_TEXT == stream->mediaType) && videoStream) {
            stream->scale = videoStream->scale;
            stream->rate = videoStream->rate;
        }

        if ((INVALID_TRACK_NUM == (int32)self->primaryStreamNum) &&
            (MEDIA_AUDIO == stream->mediaType)) {
            self->primaryStreamNum = i;
            AVIMSG("audio trk %d is the primary track\n", self->primaryStreamNum);
        }
    }

    movi = (MovieListPtr)riff->movi;
    self->moviList = movi->moviList;
    AVIMSG("Movie list start %lld\n", self->moviList);

    if (self->isAvi2) {
        AVIMSG("\nAttention: AVI2.0 format, may have multiple titles!\n\n");
        self->moviEnd = self->fileSize; /* For AVI2.0, movie data may exist in many 'movi' lists,
                                           the extended ones are in AVIX chunk. */
    } else {
        self->moviEnd = movi->moviEnd;
    }
    AVIMSG("Movie list end %lld\n", self->moviEnd);

    self->moviSize = self->moviEnd - self->moviList - 4;
    AVIMSG("Movie data size %lld\n", self->moviSize);

    err = verifyMP3AudioFrameSize(parserHandle);
    if (err)
        goto bail;

bail:
    /* AviParserDelete() shall be called to free the resources */
    return err;
}

static void verifyTrackDuation(FslFileHandle parserHandle) {
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;
    AVStreamPtr primaryStream; /* primary track */
    uint32 primaryStreamNum = self->primaryStreamNum;
    uint64 usTrackDuation; /* duration from index scanning */
    uint32 i;

    if (self->bCorruptedIdx) {
        return;
    }
    /* primary stream first, we believe the duration from header as long as it's not zero */
    primaryStream = stream = self->streams[primaryStreamNum];
    getSampleTime(stream, stream->cumLength, &usTrackDuation);
    AVIMSG("primary trk %d, stream duration from index table in us: %lld\n", primaryStreamNum,
           usTrackDuation); /* display may be error for overflow on wince */

    if ((0 == primaryStream->usDuration) ||
        ((usTrackDuation != primaryStream->usDuration) && (0 != usTrackDuation))) {
        /* if duration from header is different with duration form index table,
         * we trust duration from index table */
        primaryStream->usDuration = usTrackDuation;
        AVIMSG("trk %d, correct stream duration to %lld (1)\n", primaryStream->streamIndex,
               primaryStream->usDuration);
    }

    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];
        if (MEDIA_TEXT == stream->mediaType) {
            stream->usDuration = primaryStream->usDuration;
            AVIMSG("trk %d, set text track's duration to primary track's duration %lld us\n", i,
                   stream->usDuration);
        } else if (i != primaryStreamNum) /* audio or second video */
        {
            getSampleTime(stream, stream->cumLength, &usTrackDuation);
            AVIMSG("sec trk %d, stream duration from index table in us: %lld\n", i,
                   usTrackDuation); /* display may be error for overflow on wince */

            if (0 == stream->usDuration) {
                stream->usDuration = usTrackDuation;
                AVIMSG("trk %d, correct stream duration to %lld (1)\n", i, stream->usDuration);
            } else if (stream->usDuration != usTrackDuation) {
                int64 usTrackDuraitonGap;

                usTrackDuraitonGap = (int64)(usTrackDuation - stream->usDuration);
                AVIMSG("trk %d, duration difference: %lld\n", i, usTrackDuraitonGap);
                if ((usTrackDuraitonGap > (0 - DURATION_ERROR_SCOPE)) &&
                    (usTrackDuraitonGap < DURATION_ERROR_SCOPE))
                    AVIMSG("trk %d, duration difference is reasonible\n", i);
                else {
                    usTrackDuraitonGap = (int64)(stream->usDuration - primaryStream->usDuration);
                    AVIMSG("trk %d, duration difference from primary trk: %lld\n", i,
                           usTrackDuraitonGap);
                    if ((usTrackDuraitonGap > (0 - DURATION_ERROR_SCOPE)) &&
                        (usTrackDuraitonGap < DURATION_ERROR_SCOPE)) {
                        usTrackDuation = stream->usDuration;
                        AVIMSG("trk %d, correct stream duration to %lld (2)\n", i,
                               primaryStream->usDuration);
                    }
                }

                stream->usDuration = usTrackDuation;
            }
        }
    }
}

#define CHECKING_COUNT_MAX 100

static int32 verifyMP3AudioFrameSize(FslFileHandle parserHandle) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    AVStreamPtr stream;

    uint32 i;
    uint64 usSeekTime;
    uint8* tempBuffer = NULL;
    uint32 tempBufferSize = 8192;  // 4096; /* special cases: no meaningful data for several seconds
                                   // in the beginning */
    uint32 mp3SampleDataSize;
    uint64 usStartTime, usDuration;
    uint32 sampleFlags;
    uint32 maxSampleSizeBackup;

    mp3_audio_context mp3Config;

    void* bufferContext;
    uint32 nLoopCnt;

    memset(&mp3Config, 0, sizeof(mp3_audio_context));

    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];
        if (stream->isCbr)
            continue;

        if ((MEDIA_AUDIO == stream->mediaType) && (AUDIO_MP3 == stream->decoderType)) {
            usSeekTime = 0;
            err = AviSeek(parserHandle, i, &usSeekTime, SEEK_FLAG_NO_LATER);
            if (err)
                goto bail;

            if (NULL == tempBuffer) {
                tempBuffer = LOCALMalloc(tempBufferSize);
                TESTMALLOC(tempBuffer)
            }

            nLoopCnt = 0;
        NEXT_SAMPLE_LOOP:
            mp3SampleDataSize = tempBufferSize;

            maxSampleSizeBackup = stream->maxSampleSize;
            if (0 == maxSampleSizeBackup)
                stream->maxSampleSize = -1; /* otherwise, sample size check failure */

            err = getNextSample(parserHandle, i, &tempBuffer, &bufferContext, &mp3SampleDataSize,
                                &usStartTime, &usDuration, &sampleFlags);
            stream->maxSampleSize = maxSampleSizeBackup;
            if (err)
                goto bail;

            if ((mp3SampleDataSize <= 4) && (nLoopCnt < CHECKING_COUNT_MAX)) {
                // too small sample size for mp3 frame
                AVIMSG("verifyMP3AudioFrameSize: %d -- SampleSize %d \n", nLoopCnt,
                       mp3SampleDataSize);
                nLoopCnt++;
                goto NEXT_SAMPLE_LOOP;
            }

            err = mpa_parse_frame_header((char*)tempBuffer, mp3SampleDataSize, FALSE, &mp3Config);
            if (err && (AVI_ERR_NO_MP3_FRAME_FOUND != err))
                goto bail;

            if (PARSER_SUCCESS == err) {
                if (((int32)stream->audioFrameSize != mp3Config.nb_samples_per_frame) &&
                    ((int32)stream->sampleRate == mp3Config.sampling_frequency)) {
                    /* All properties match except frame size */
                    stream->audioFrameSize = mp3Config.nb_samples_per_frame;
                    AVIMSG("MP3 audio frame size SHALL be %d\n", stream->audioFrameSize);
                }
            } else {
                AVIMSG("No valid MP3 frame found in 1st sample. Accept default frame size %d\n",
                       stream->audioFrameSize);
            }

            if (mp3Config.toc)  // avoid memory leak
            {
                LOCALFree(mp3Config.toc);
                mp3Config.toc = NULL;
            }

            usSeekTime = 0;
            err = AviSeek(parserHandle, i, &usSeekTime, SEEK_FLAG_NO_LATER);
            if (err)
                goto bail;
        }
    }

bail:
    if (mp3Config.toc) {
        LOCALFree(mp3Config.toc);
        mp3Config.toc = NULL;
    }

    if (tempBuffer)
        LOCALFree(tempBuffer);

    return err;
}

static bool isAudioCBR(uint32 decoderType, uint32 decoderSubtype, StreamHeaderPtr strh) {
    bool isCbr = TRUE;
    (void)decoderType;
    (void)decoderSubtype;

    AVIMSG("Audio sample size %u, scale %u\n", strh->sampleSize, strh->scale);
    if (((0 == strh->sampleSize) /*||(1 < strh->sampleSize)*/) &&
        (1 < strh->scale)) /* cbr or vbr ? */
        isCbr = FALSE;
#if 0
    else if((AUDIO_ADPCM == decoderType)
            && (AUDIO_ADPCM_MS == decoderSubtype))
        isCbr = FALSE;
#endif
    return isCbr;
}

static void getAudioFrameSize(AVStreamPtr stream) {
    if (AUDIO_AC3 == stream->decoderType)
        stream->audioFrameSize = AC3_FRAME_SIZE;

    else if (AUDIO_AAC == stream->decoderType)
        stream->audioFrameSize = AAC_FRAME_SIZE;
#if 0
    else if((AUDIO_ADPCM == stream->decoderType)
            && (AUDIO_ADPCM_MS == stream->decoderSubtype))
        stream->audioFrameSize = MS_ADPCM_FRAME_SIZE;
#endif
    else
        stream->audioFrameSize = MP3_FRAME_SIZE; /* default value */
    AVIMSG("Audio frame size: %u\n", stream->audioFrameSize);
}
