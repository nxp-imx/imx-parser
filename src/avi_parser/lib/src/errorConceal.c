/*
 ***********************************************************************
 * Copyright (c) 2005-2011,2016 by Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "avi_parser_api.h"
#include "avi_utils.h"
#include "avi.h"

/* search a valid sample of a stream in the specified scope, from stream's current read point.
1. Search the stream's tag. For video, search its xxdb  & xxdd
2. verify the chunk size by checking whether the next chunk tag is valid  */

// #define DEBUG_ERR_CONCEALMENT
#ifdef DEBUG_ERR_CONCEALMENT
#define AVIERRMSG AVIMSG
#define PrintErrTag PrintTag
#else
#if defined(__WINCE)
#define AVIERRMSG(fmt, ...) DEBUGMSG(0, (_T(fmt), __VA_ARGS__))
#elif defined(WIN32)
#define AVIERRMSG(fmt, ...) DEBUGMSG(0, (fmt, __VA_ARGS__))
#else
#define AVIERRMSG(fmt...)
#endif

#define PrintErrTag(a)
#endif

/* try to find a valid sample by searching the bitstream & seeking */
int32 AviSearchValidSample(AviObjectPtr aviObj, AVStreamPtr stream, bool* suggestSeek) {
    int32 err = PARSER_ERR_CONCEAL_FAIL; /* assume no sample can be found at first */

    /* searching across a large scope is likely to impact A/V sync
    If search count reaches the threshold, PARSER_ERR_CONCEAL_FAIL is returned, upper layer shall
    suggest seek*/
    if (RIFF_HEADER_SIZE > stream->fileOffset)
        BAILWITHERROR(PARSER_ERR_UNKNOWN)
    stream->fileOffset -= RIFF_HEADER_SIZE; /* step backward the chunk ID/size field */

    *suggestSeek = FALSE; /* seeking can not assure more */

    err = AviSearchNextSample(aviObj, stream, TRACK_SEARCH_SCOPE);
    if (PARSER_SUCCESS != err) {
        AVIMSG("err concealed failed with err %d, bytes scanned %d\n", err,
               (uint32)stream->errBytesScanned);
    }

bail:
    return err;
}

/* try to find a valid sample by searching the bitstream in a scope */
int32 AviSearchNextSample(AviObjectPtr aviObj, AVStreamPtr stream, uint32 scope) {
    int32 err = PARSER_ERR_CONCEAL_FAIL; /* assume no sample can be found at first */
    void* appContext = aviObj->appContext;
    AviInputStream* inputStream = stream->inputStream;

    AVStreamPtr baseStream = aviObj->streams[aviObj->primaryStreamNum];

    uint32 baseTag = baseStream->tag;
    uint32 tag = stream->tag;
    uint32 uncompressedVideoTag =
            baseStream->uncompressedVideoTag; /* for video, we need to check xxdb & xxdd */
    uint32 drmTag = aviObj->drmTag;
    uint32 maxSampleSize = baseStream->maxSampleSize;
    bool baseStreamIsVideo = (MEDIA_VIDEO == baseStream->mediaType) ? TRUE : FALSE;

    uint64 movieSize = aviObj->moviSize;
    uint8* cache;

    int64 bytesScanned = 0; /* bytes scanned from current read point */

    uint32 bytesLeft;
    uint32 bytesToRead;
    int32 bytesGot;

    uint32 chunkTag = 0;
    uint32 chunkSize = 0;
    uint32 bytesGotForChunkSize = 0;
    uint32 chunkSizeEven;

    int32 i;
    bool tagMatched = FALSE;
    bool sampleFound = FALSE;
    uint8 val8;
    int64 oriTrackFileOffset = stream->fileOffset;

    /* for fast search, read 1k data one time */
    if (NULL == stream->cache) {
        stream->cache = alignedMalloc(TRACK_CACHE_SIZE, 4);
        TESTMALLOC(stream->cache)
    }
    cache = stream->cache;
    AVIERRMSG(
            "Target tag 0x%08x, search scope %d, start from file offset %d (0x%x), base stream is "
            "video? %d\n",
            stream->tag, scope, (uint32)stream->fileOffset, (uint32)stream->fileOffset,
            baseStreamIsVideo);
    stream->errBytesScanned = 0;

    if (stream->fileOffset >= aviObj->moviEnd)
        BAILWITHERROR(PARSER_EOS)

    if (LocalFileSeek(inputStream, stream->fileOffset, SEEK_SET,
                      appContext)) /* for successive searching */
        BAILWITHERROR(PARSER_SEEK_ERROR)

    bytesLeft = scope;
    while ((0 < bytesLeft) && !sampleFound) {
        bytesToRead = (TRACK_CACHE_SIZE < bytesLeft) ? TRACK_CACHE_SIZE : bytesLeft;
        bytesGot = LocalFileRead(inputStream, cache, bytesToRead, appContext);
        bytesLeft -= bytesGot;

#if 0
        if(0 < bytesGot)
        {
            stream->fileOffset += bytesGot; /* may exceeds the end of movie (moviEnd) */
        }
#endif

        if (RIFF_HEADER_SIZE > bytesGot) {
            AVIERRMSG(" No valid sample found till the end of the movie\n");
            BAILWITHERROR(PARSER_READ_ERROR)
        }

        for (i = 0; i < bytesGot; i++) {
            val8 = *(cache + i);
            bytesScanned++;

            if (!tagMatched) /* search the valid tag */
            {
                chunkTag = chunkTag >> 8;
                chunkTag |= ((uint32)val8) << 24;

                if (3 < bytesScanned) {
                    if (chunkTag == tag || chunkTag == baseTag) {
                        AVIERRMSG("\nFound tag ");
                        PrintErrTag(chunkTag);
                        AVIERRMSG("\tfile offset %d (0x%x), bytes scan %d\n",
                                  (uint32)(stream->fileOffset + bytesScanned),
                                  (uint32)(stream->fileOffset + bytesScanned),
                                  (uint32)bytesScanned);
                        tagMatched = TRUE;
                    }

                    else if (baseStreamIsVideo && ((chunkTag == uncompressedVideoTag) ||
                                                   (chunkTag && (chunkTag == drmTag)))) {
                        /* if the movie is protected, drmTag is not ZERO */
                        AVIERRMSG("\nFound video tag ");
                        PrintErrTag(chunkTag);
                        AVIERRMSG("\tfile offset %d (0x%x), bytes scan %d\n",
                                  (uint32)(stream->fileOffset + bytesScanned),
                                  (uint32)(stream->fileOffset + bytesScanned),
                                  (uint32)bytesScanned);
                        tagMatched = TRUE;
                    }
                }

            } else /* verify the chunk size, if the size is invalid,  discard the matched tag*/
            {
                /* LSB in file */
                chunkSize = chunkSize >> 8;
                chunkSize |= ((uint32)val8) << 24;

                bytesGotForChunkSize++;
                if (4 == bytesGotForChunkSize) {
                    chunkSizeEven = (chunkSize + 1) & (~1);

                    AVIERRMSG("\tverify chunk size %d (0x%x)\n", chunkSize, chunkSize);
                    AVIERRMSG("\tfile offset %d (0x%x), target offset %d (0x%x)\n",
                              (uint32)(stream->fileOffset + bytesScanned),
                              (uint32)(stream->fileOffset + bytesScanned),
                              (uint32)(stream->fileOffset + bytesScanned + chunkSizeEven),
                              (uint32)(stream->fileOffset + bytesScanned + chunkSizeEven));

                    if ((stream->fileOffset + bytesScanned + chunkSizeEven) >= aviObj->moviEnd) {
                        BAILWITHERROR(PARSER_EOS)
                    }

                    if (((uint64)chunkSizeEven > movieSize) ||
                        (chunkSizeEven >
                         ((chunkTag == baseTag ? maxSampleSize : stream->maxSampleSize)))) {
                        AVIERRMSG("Invalid chunk size %ld (0x%x)\n", chunkSize, chunkSize);
                        ;  /* invalid size */
                    } else /* whether followed by a valid chunk (assume this is not the last chunk.
                              If it's is, discard unfortunately) */
                    {
                        int64 curFilePos;
                        int64 nextChunkOffset;
                        uint32 nextTag;
                        uint32 size;
                        curFilePos = LocalFileTell(inputStream, appContext);
                        nextChunkOffset =
                                (int64)(stream->fileOffset + bytesScanned + chunkSizeEven);
                        if (LocalFileSeek(inputStream, nextChunkOffset, SEEK_SET, appContext))
                            BAILWITHERROR(PARSER_SEEK_ERROR)
                        size = LocalFileRead(inputStream, &nextTag, 4, appContext);

                        if ((4 == size) && isValidTag(nextTag)) {
                            /* current sample is valid, seek to the start of the chunk */
                            AVIERRMSG(
                                    "\tSample is valid. At file offset %d(0x%x) found Next chunk "
                                    "tag ",
                                    (int32)nextChunkOffset, (int32)nextChunkOffset);
                            PrintErrTag(nextTag);
                            sampleFound = TRUE;
                            stream->fileOffset += (bytesScanned - 8);
                            AVIERRMSG("\t\tmodify file offset %d (0x%x)\n",
                                      (uint32)stream->fileOffset, (uint32)stream->fileOffset);
                            if (LocalFileSeek(inputStream, stream->fileOffset, SEEK_SET,
                                              appContext))
                                BAILWITHERROR(PARSER_SEEK_ERROR)
                            break;
                        } else /* just restore file position for next search, MUST NOT return
                                  PARSER_READ_ERROR */
                        {
                            if (LocalFileSeek(inputStream, curFilePos, SEEK_SET, appContext)) {
                                AVIERRMSG("Fail to restore file offset to %lld(0x%x)", curFilePos,
                                          curFilePos);
                                BAILWITHERROR(PARSER_SEEK_ERROR)
                            }
                        }
                    }

                    /* the size is invalid, search again for the tag.
                    No longer scan the 4 bytes of chunk size, Since the possibility to be a valid
                    tag is too low! */
                    tagMatched = FALSE;
                    chunkTag = 0;
                    chunkSize = 0;
                    bytesGotForChunkSize = 0;
                }
            }
        }
        if (sampleFound)
            break;
    }

bail:

    if (sampleFound) {
        stream->errBytesScanned = (stream->fileOffset - oriTrackFileOffset);
        AVIERRMSG("Next sample got, bytes scanned %lld\n", stream->errBytesScanned);
        err = PARSER_SUCCESS;
    } else if (0 < bytesScanned) {
        AVIERRMSG("NO valid sample found in the scope %d\n", scope);

        /* for next searching, modify track's file offset */
        if (8 < bytesScanned)
            stream->fileOffset +=
                    (bytesScanned - 8); /* to avoid missing chunk tag on the boundary */
        else
            stream->fileOffset = aviObj->moviEnd; /* file end, no meaning for next searching */

        if (stream->fileOffset >= aviObj->moviEnd) {
            err = PARSER_EOS; /* NO need for next searching. Only seeking can save the playback */
        }
    }

    return err;
}

bool isValidTag(uint32 tag) {
    bool ret = TRUE;
    uint8* id = (uint8*)&tag;
    int i;

    for (i = 0; i < 4; i++) {
        if ((('A' <= id[i]) && ('Z' >= id[i])) || (('a' <= id[i]) && ('z' >= id[i])) ||
            (('0' <= id[i]) && ('9' >= id[i])) || (0x20 == id[i])) {
            ;
        } else
            ret = FALSE;
    }

    return ret;
}
