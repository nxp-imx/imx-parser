/*
 ***********************************************************************
 * Copyright (c) 2010-2016, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "fsl_parser_drm.h"
#include "avi_parser_api.h"
#include "avi_utils.h"
#include "avi.h"

#ifdef SUPPORT_AVI_DRM
#include "DrmApi.h"
#include "portab.h"
#endif

static uint32 findTrackNum(AviObjectPtr self, uint32 fourcc);
static void backupSampleStatus(AVStreamPtr stream, uint32 sampleFlags, uint32 sampleSize,
                               uint64 usSampleTimeStamp, uint64 usSampleDuration);

void resetTrackReadingStatus(AVStreamPtr stream) {
    stream->prevSampleSize = 0;
    stream->chunkHeaderRead = FALSE;
    stream->sampleBytesLeft = 0;

    /* error counter */
    stream->errConcealedCount = 0;
    stream->errBytesScanned = 0;

    /* file-based rewinding flags */
    stream->isOneSampleGotAfterSkip = FALSE;
    stream->bos = FALSE;
}

/* if a valid chunk header can be found, the stream's sample size & flag will be updated */
int32 getNextChunkHead(FslFileHandle parserHandle, uint32 trackNum)

{
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    void* appContext = self->appContext;
    AVStreamPtr stream = self->streams[trackNum];
    AviInputStream* inputStream = stream->inputStream;
    uint32 chunkHead[2];
    uint32 size;
    uint32 chunkSize;     /* sample size */
    uint32 chunkSizeEven; /* even-rounded chunk size -> file offset update */
    bool eos = FALSE;
    bool tagMatched = FALSE; /* Whether the chunk tag matches the stream's tag (including the
                                uncompressed video), need further check the size */
    bool sampleGot = FALSE;  /* whether a valid sample header is got */

    uint32 type;
    uint32 sampleFlag = 0;
    bool SuggestSeekOnErr; /* error concealment module suggest to do a seek to assure A/V sync */

    stream->sampleFlag = 0;

    if (stream->fileOffset >= self->moviEnd) {
        /*Don't use if(stream->sampleOffset >= stream->cumLength)
        index table may be not available, so "cumLength" can be ZERO */
        return PARSER_EOS;
    }

    if (LocalFileSeek(inputStream, stream->fileOffset, SEEK_SET, appContext))
        return PARSER_SEEK_ERROR;

    while (!eos && !sampleGot) {
        /* seek from this file offset to find the next sample of the track */
        size = LocalFileRead(inputStream, (uint8*)chunkHead, RIFF_HEADER_SIZE, appContext);
        if (RIFF_HEADER_SIZE != size)
            return PARSER_READ_ERROR;

        stream->fileOffset += RIFF_HEADER_SIZE;

        chunkSize = chunkHead[1];
        chunkSizeEven = (chunkSize + 1) & (~1);

#if 0
        uint32 streamIndex;
        if ((ListTag != chunkHead[0]) &&
            (JunkTag != chunkHead[0]) &&
            (FALSE == isAvi2IndexTag(chunkHead[0])))
        {
            streamIndex = findTrackNum(self, chunkHead[0]);
            if((PARSER_INVALID_TRACK_NUMBER == (int32)streamIndex)
                ||(chunkSize >= self->moviSize)
                ||((0 == chunkSize) && !isValidTag(chunkHead[0])))
            {
                AVIMSG("\nTrk %d, Invalid chunk size %d (0x%08x), tag 0x%08x:", trackNum, chunkSize, chunkSize, chunkHead[0]); PrintTag( chunkHead[0]);
                err = AviSearchValidSample(self, stream, &SuggestSeekOnErr);

                if(PARSER_SUCCESS == err)
                {
                    sampleFlag |= FLAG_SAMPLE_ERR_CONCEALED;
                    if(SuggestSeekOnErr)
                        sampleFlag |= FLAG_SAMPLE_SUGGEST_SEEK;
                    continue;
                }
                else
                    return err; /* no valid sample can be found by error concealment */
            }
        }
#endif
        if (stream->tag == chunkHead[0]) /* chunk ID matched, shall read sample */
            tagMatched = TRUE;

        else /* further check, to skip? */
        {
#ifdef SUPPORT_AVI_DRM
            if (self->protected && (chunkHead[0] == self->drmTag) &&
                (MEDIA_VIDEO == stream->mediaType)) { /* frame key is found for video stream*/
                if (chunkSize != DRM_FRAME_KEY_SIZE)
                    return AVI_ERR_WRONG_CHUNK_SIZE;

                size = LocalFileRead(inputStream, self->frameKey, chunkSizeEven, appContext);
                if (chunkSizeEven != size)
                    return PARSER_READ_ERROR;
                stream->fileOffset += chunkSizeEven;
                /* go ahead to find the data chunk */
            } else
#endif
            {
                /* if not support DRM, just skip this chunk/list, but pay attention it MUST not a
                'rec' list! NOTE:
                1. The data chunks can reside directly in the 'movi' list, or they might be grouped
                within 'rec ' lists.
                2. For AVI2.0, there may be mutltiple RIFF(AVIX) with their own movi list */

                if (RIFFTag == chunkHead[0]) /* extented RIFF titles or menu */
                {
                    /* check RIFF type */
                    err = read32(inputStream, &type, appContext);
                    if (PARSER_SUCCESS != err)
                        return err;
                    AVIMSG("\nWarning!RIFF found, size %d, type ", chunkSize);
                    PrintTag(type);
                    if (AVIExtensionTag == type) {
                        stream->fileOffset += 4; /* only across the  type field */
                    } else {                     /* skip the remaining part of this RIFF */
                        LocalFileSeek(inputStream, (int64)chunkSizeEven - 4, SEEK_CUR, appContext);
                        stream->fileOffset += chunkSizeEven;
                    }
                } else if (ListTag == chunkHead[0]) /* list */
                {
                    /* check list type */
                    err = read32(inputStream, &type, appContext);
                    if (PARSER_SUCCESS != err)
                        return err;
                    if ((RecordListTag == type) || (MovieListTag == type)) {
                        stream->fileOffset += 4; /* only across the list type field */
                    } else {                     /* skip the remaining part of this list */
                        LocalFileSeek(inputStream, (int64)chunkSizeEven - 4, SEEK_CUR, appContext);
                        stream->fileOffset += chunkSizeEven;
                    }
                } else if ((MEDIA_VIDEO == stream->mediaType) &&
                           (stream->uncompressedVideoTag == chunkHead[0])) {
                    tagMatched = TRUE; /* uncompressed video, shall read the sample */
                    sampleFlag |= FLAG_UNCOMPRESSED_SAMPLE;
                } else if (isValidTag(chunkHead[0]) &&
                           ((chunkSize + stream->fileOffset) <= self->moviEnd)) {
                    if (chunkSizeEven) /* skip this chunk */
                        LocalFileSeek(inputStream, (int64)chunkSizeEven, SEEK_CUR, appContext);
                    stream->fileOffset += chunkSizeEven;
                } else /* corrupted data */
                {
                    AVIMSG("\nInvalid chunk size %d (0x%08x), tag 0x%08x:", chunkSize, chunkSize,
                           chunkHead[0]);
                    PrintTag(chunkHead[0]);

                    err = AviSearchValidSample(self, stream, &SuggestSeekOnErr);
                    if (PARSER_SUCCESS != err) {
                        return err; /* no valid sample can be found by error concealment */
                    }

                    sampleFlag |= FLAG_SAMPLE_ERR_CONCEALED;
                    if (SuggestSeekOnErr)
                        sampleFlag |= FLAG_SAMPLE_SUGGEST_SEEK;
                }
            }
        }

        if (tagMatched) /* further check the sample size */
        {
            if (chunkSize > stream->maxSampleSize) /* only check size for matched ID */
            {
                if ((chunkSize + stream->fileOffset) > self->moviEnd) /* corrupted data */
                {
                    AVIMSG("\nInvalid chunk size %d (0x%08x), tag 0x%08x:", chunkSize, chunkSize,
                           chunkHead[0]);
                    PrintTag(chunkHead[0]);
                    return PARSER_READ_ERROR;
                }
                if (self->indexLoaded) {
                    AVIMSG("trk %d, wrong chunk size %d, max sample size %d\n", trackNum, chunkSize,
                           stream->maxSampleSize);
                    err = AviSearchValidSample(self, stream, &SuggestSeekOnErr);
                    if (PARSER_SUCCESS == err) {
                        sampleFlag |= FLAG_SAMPLE_ERR_CONCEALED;
                        if (SuggestSeekOnErr)
                            sampleFlag |= FLAG_SAMPLE_SUGGEST_SEEK;
                        continue;
                    } else
                        return err; /* no valid sample can be found by error concealment */
                } else {            /* index table does not give a valid max sample size,
                                    and max sample size in file header may be invalid (eg. 0)! */
                    stream->maxSampleSize = (chunkSize + 1) & (~1);
                    AVIMSG("trk %u, update max sample size to %u\n", trackNum,
                           stream->maxSampleSize);
                }
            }

            stream->sampleSize = chunkSize;
            stream->sampleFlag = sampleFlag;
            stream->chunkHeaderRead = TRUE;

            sampleGot = TRUE;
        }

        if (stream->fileOffset >= self->moviEnd) /* check file offset to avoid infinite loop */
        {
            eos = TRUE;
            err = PARSER_EOS;
        }
    }

    return err;
}

/* read a new sample from a track */
int32 getNextSample(FslFileHandle parserHandle, uint32 trackNum, uint8** sampleBuffer,
                    void** bufferContext, uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                    uint32* flag)

{
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    void* appContext = self->appContext;
    AVStreamPtr stream = self->streams[trackNum];
    AviInputStream* inputStream = stream->inputStream;
    uint32 size;
    uint32 chunkSize; /* sample size */
    uint32 chunkSizeEven /* rounded chunk size -> bytes to read */;

    uint64 pts;
    uint64 usSampleDuration;

    uint8* sampleData = NULL;
    uint32 bufferSize = 0;
    uint32 sampleFlag;
    uint32 au_flag;

    au_flag = self->au_flag;

    stream->isOneSampleGotAfterSkip = TRUE;

    if (!stream->chunkHeaderRead) {
        err = getNextChunkHead(parserHandle, trackNum);
        if (PARSER_SUCCESS != err)
            return err;
    }
    sampleFlag = stream->sampleFlag;
    chunkSize = stream->sampleSize;
    chunkSizeEven = (chunkSize + 1) & (~1);

    /* key frame ? for non-video medias, all samples are sync */
    if (MEDIA_VIDEO == stream->mediaType) {
        if (stream->indexTab && (stream->indexOffset < stream->numIndexEntries)) {
            if (stream->sampleOffset == stream->indexTab[stream->indexOffset].pts) {
                sampleFlag |= FLAG_SYNC_SAMPLE;
                stream->indexOffset++;
            }
        }
    } else
        sampleFlag |= FLAG_SYNC_SAMPLE;

    /* time stamp */
    getTimestamp(stream, chunkSize, &pts, &usSampleDuration);
    *usStartTime = pts;
    *usDuration = usSampleDuration;

    stream->chunkHeaderRead = FALSE;

    if (stream->fileOffset >= self->moviEnd) {
        /*Don't use if(stream->sampleOffset >= stream->cumLength)
        index table may be not available, so "cumLength" can be ZERO */
        BAILWITHERROR(PARSER_EOS)
    }

    LocalFileSeek(inputStream, stream->fileOffset, SEEK_SET, appContext);

    if (chunkSize) {
        if (self->fileHeaderParsed) {
            bufferSize = chunkSizeEven;
            sampleData = g_outputBufferOps.RequestBuffer(trackNum, &bufferSize, bufferContext,
                                                         appContext);
            if (!sampleData) {
                AVIMSG("trak %u, fail to get buffer! sample offset %lld, size %u, pts %lld, "
                       "duration %lld, file offset %lld bytes\n",
                       trackNum, stream->sampleOffset, chunkSize, pts, usSampleDuration,
                       stream->fileOffset);
                sampleFlag |= FLAG_SAMPLE_NOT_FINISHED;
                backupSampleStatus(stream, sampleFlag, chunkSize, pts, usSampleDuration);
                stream->sampleBytesLeft = chunkSizeEven;
                self->nextStream = stream;
                BAILWITHERROR(PARSER_ERR_NO_OUTPUT_BUFFER)
            }
            *sampleBuffer = sampleData;
        } else {
            sampleData = *sampleBuffer; /* can not request buffer yet, just verify MP3 properties */
            bufferSize = *dataSize;
        }

        if (chunkSizeEven > bufferSize) { /* buffer too small! */
            if (self->protected) {        /* for DRM clips, need a larger buffer */
                LocalFileSeek(inputStream, 0 - RIFF_HEADER_SIZE, SEEK_CUR, appContext);
                stream->fileOffset -= RIFF_HEADER_SIZE;
                *dataSize = chunkSizeEven; /* tell the buffer size needed, even-rounded size */
                BAILWITHERROR(PARSER_INSUFFICIENT_MEMORY)
            } else { /* for non-DRM clips, only output 1st part this time */
                stream->sampleBytesLeft = chunkSizeEven - bufferSize;
                chunkSizeEven = bufferSize;
            }
        }

        /* begin read the sample data */
        size = LocalFileRead(inputStream, sampleData, chunkSizeEven, appContext);
        if (chunkSizeEven != size)
            BAILWITHERROR(PARSER_READ_ERROR)
        stream->fileOffset += chunkSizeEven;

        if (MEDIA_VIDEO == stream->mediaType) /* video stream */
        {
#ifdef SUPPORT_AVI_DRM
            if ((self->protected) && (self->bHasDrmLib)) {
                drmErrorCodes_t decrypt_ret;
                decrypt_ret = self->sDrmAPI.drmDecryptVideo(self->drmContext, sampleData, chunkSize,
                                                            self->frameKey);
                if (DRM_SUCCESS != decrypt_ret)
                    BAILWITHERROR(AVI_ERR_DRM_OTHERS)
            }
#endif

        }

        else if (MEDIA_AUDIO == stream->mediaType) /* audio stream */
        {
#ifdef SUPPORT_AVI_DRM
            if ((self->protected) && (self->bHasDrmLib)) {
                drmErrorCodes_t decrypt_ret;
                decrypt_ret =
                        self->sDrmAPI.drmDecryptAudio(self->drmContext, sampleData, chunkSize);
                if (DRM_SUCCESS != decrypt_ret)
                    BAILWITHERROR(AVI_ERR_DRM_OTHERS);
            }
#endif
        }
    } else /* empty sample */
    {
        AVIMSG("trak %d, empty sample\n", trackNum);
        *dataSize = 0;
        if (self->fileHeaderParsed)
            *sampleBuffer = NULL;
    }

    /* data size & sample flag */
    if (stream->sampleBytesLeft) { /* only 1st part of this sample is output, backup sample size,
                                      flag & time */
        *dataSize = bufferSize;
        sampleFlag |= FLAG_SAMPLE_NOT_FINISHED;
        backupSampleStatus(stream, sampleFlag, chunkSize, pts, usSampleDuration);
    } else { /* entire sample is output */
        if (stream->isCbr)
            stream->sampleOffset += chunkSize;
        else {
            /* take subtitle as a normal VBR, it's PTS is mono but not accurate. Keep things simple!
            After seeking, its 1st pts is same as the base stream, and then change very slowly */
            stream->sampleOffset++;
        }

        *dataSize = chunkSize;
        stream->prevSampleSize = chunkSize;

        if (!(au_flag & FLAG_H264_NO_CONVERT)) {
            if ((MEDIA_VIDEO == stream->mediaType) && (VIDEO_H264 == stream->decoderType)) {
#if 1
                uint32 nal_size = 0x00000001;
                uint32 rem_bytes;
                uint8* ptr = sampleData;
                uint8* pBufEnd;
                StreamFormatPtr strf =
                        (StreamFormatPtr)(((StreamHeaderListPtr)(((HeaderListPtr)(((RiffTitlePtr)(self->riff))
                                                                                          ->hdrl))
                                                                         ->strl[trackNum]))
                                                  ->strf);

                if (chunkSize > 4) {
                    if (4 == strf->h264NAL_HeaderSize)
                        nal_size = (sampleData[0] << 24) | (sampleData[1] << 16) |
                                   (sampleData[2] << 8) | sampleData[3];
                    else if (3 == strf->h264NAL_HeaderSize)
                        nal_size = (sampleData[0] << 16) | (sampleData[1] << 8) | sampleData[2];
                    else
                        goto loop_exit;
                }

                if (nal_size == 0x00000001)
                    goto loop_exit;

                rem_bytes = chunkSize;
                pBufEnd = sampleData + chunkSize;

                while ((rem_bytes > 0) && ((ptr + 4) <= pBufEnd)) {
                    nal_size = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
                    ptr[0] = ptr[1] = ptr[2] = 0;
                    ptr[3] = 0x1;

                    if ((nal_size > chunkSize) ||
                        (nal_size > rem_bytes))  // avoid to write output of the buffer range
                        nal_size = rem_bytes - 4;

                    ptr += (4 + nal_size);
                    rem_bytes -= (4 + nal_size);
                }
            loop_exit:;
#else
                if ((sampleData[0] == 0x00) && (sampleData[1] == 0x00) && (sampleData[2] == 0x00) &&
                    (sampleData[3] == 0x01)) {
                    ;
                } else {
                    sampleData[0] = 0x00;
                    sampleData[1] = 0x00;
                    sampleData[2] = 0x00;
                    sampleData[3] = 0x01;
                }
#endif
            }
        }
    }

    *flag = sampleFlag;

bail:
    if (err) {
        if (self->fileHeaderParsed && sampleData)
            g_outputBufferOps.ReleaseBuffer(stream->streamIndex, sampleData, *bufferContext,
                                            appContext);
        *dataSize = 0;
        *sampleBuffer = NULL;
        *usStartTime = PARSER_UNKNOWN_TIME_STAMP;
        *usDuration = PARSER_UNKNOWN_DURATION;
    }
    return err;
}

int32 getSampleRemainingBytes(FslFileHandle parserHandle, uint32 trackNum, uint8** sampleBuffer,
                              void** bufferContext, uint32* dataSize, bool* finished) {
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    void* appContext = self->appContext;
    AVStreamPtr stream = self->streams[trackNum];

    AviInputStream* inputStream = stream->inputStream;
    uint32 bufferSize;
    uint32 entireSampleSize = stream->sampleSize;
    uint32 bytesToRead;
    uint32 bytesGot;
    uint8* sampleData = NULL;

    if (!self->fileHeaderParsed)
        return PARSER_ERR_UNKNOWN;

    bufferSize = stream->sampleBytesLeft;
    sampleData = g_outputBufferOps.RequestBuffer(trackNum, &bufferSize, bufferContext, appContext);
    if (!sampleData)
        BAILWITHERROR(PARSER_ERR_NO_OUTPUT_BUFFER)
    *sampleBuffer = sampleData;

    LocalFileSeek(inputStream, stream->fileOffset, SEEK_SET,
                  appContext); /* seek is necessary for thread race consideration */

    bytesToRead = (stream->sampleBytesLeft <= bufferSize) ? stream->sampleBytesLeft : bufferSize;

    bytesGot = LocalFileRead(inputStream, sampleData, bytesToRead, appContext);
    if (bytesToRead != bytesGot)
        BAILWITHERROR(PARSER_READ_ERROR)

    *dataSize = bytesGot;

    stream->sampleBytesLeft -= bytesGot;
    stream->fileOffset += bytesGot;

    if (stream->sampleBytesLeft)
        *finished = FALSE;
    else { /* current sample is finished in this output */
        *finished = TRUE;
        stream->prevSampleSize = entireSampleSize;

        if (stream->isCbr)
            stream->sampleOffset += entireSampleSize;
        else
            stream->sampleOffset++;

        /* index offset is updated in the 1st output */

        if (entireSampleSize & 0x1)
            bytesGot--; /* has one padding byte at the end of the sample */
    }

    *dataSize = bytesGot;

bail:
    if ((PARSER_SUCCESS != err) && sampleData)
        g_outputBufferOps.ReleaseBuffer(trackNum, sampleData, *bufferContext, appContext);

    return err;
}

/* if a valid chunk header can be found, the stream's sample size & flag will be updated,
maintain the movie's file offset

If the movie offset less than the track's start fileoffset

*/
int32 getFileNextChunkHead(FslFileHandle parserHandle, uint32* trackNum)

{
    int32 err = PARSER_SUCCESS;
    AviObjectPtr self = (AviObjectPtr)parserHandle;
    void* appContext = self->appContext;
    AviInputStream* inputStream = self->inputStream;

    AVStreamPtr stream;
    uint32 streamIndex;

    uint32 chunkHead[2];
    uint32 size;
    uint32 chunkSize;     /* sample size */
    uint32 chunkSizeEven; /* even-rounded chunk size -> file offset update */
    bool eos = FALSE;
    bool sampleGot = FALSE;  /* whether a valid sample header is got */

    uint32 type;
    uint32 sampleFlag = 0;
    bool SuggestSeekOnErr; /* error concealment module suggest to do a seek to assure A/V sync */

    *trackNum = PARSER_INVALID_TRACK_NUMBER;

    err = LocalFileSeek(inputStream, self->fileOffset, SEEK_SET, appContext);
    if (err)
        return PARSER_SEEK_ERROR;

    while (!eos && !sampleGot) {
        if (self->fileOffset >= self->moviEnd) {
            AVIMSG("file offset %lld >= movie end  %lld, EOS\n", self->fileOffset, self->moviEnd);
            return PARSER_EOS;
        }

        /* find the next sample of the file */
        size = LocalFileRead(inputStream, (uint8*)chunkHead, RIFF_HEADER_SIZE, appContext);
        if (RIFF_HEADER_SIZE != size)
            return PARSER_READ_ERROR;

        self->fileOffset += RIFF_HEADER_SIZE;

        chunkSize = chunkHead[1];
        chunkSizeEven = (chunkSize + 1) & (~1);

        streamIndex = findTrackNum(self, chunkHead[0]);
        if ((PARSER_INVALID_TRACK_NUMBER == (int32)streamIndex) ||
            (chunkSize >=
             self->moviSize)) { /* corrupt data or specific tags 'LIST' 'REC ' 'RIFF' */

            if (RIFFTag == chunkHead[0]) /* extented RIFF titles or menu */
            {
                /* check RIFF type */
                err = read32(inputStream, &type, appContext);
                if (PARSER_SUCCESS != err)
                    return err;
                AVIMSG("\nWarning!RIFF found, size %d, type ", chunkSize);
                PrintTag(type);
                if (AVIExtensionTag == type) {
                    self->fileOffset += 4; /* only across the  type field */
                } else {                   /* skip the remaining part of this RIFF */
                    LocalFileSeek(inputStream, (int64)chunkSizeEven - 4, SEEK_CUR, appContext);
                    self->fileOffset += chunkSizeEven;
                }
            } else if (ListTag == chunkHead[0]) /* list */
            {
                /* check list type */
                err = read32(inputStream, &type, appContext);
                if (PARSER_SUCCESS != err)
                    return err;
                if ((RecordListTag == type) || (MovieListTag == type)) {
                    self->fileOffset += 4; /* only across the list type field */
                } else {                   /* skip the remaining part of this list */
                    LocalFileSeek(inputStream, (int64)chunkSizeEven - 4, SEEK_CUR, appContext);
                    self->fileOffset += chunkSizeEven;
                }
            } else if (isValidTag(chunkHead[0]) && (chunkSize < self->moviSize)) {
                if (chunkSizeEven) /* skip this chunk */
                    LocalFileSeek(inputStream, (int64)chunkSizeEven, SEEK_CUR, appContext);
                self->fileOffset += chunkSizeEven;
            } else /* corrupted data */
            {
                AVIMSG("\nInvalid chunk size %d (0x%08x), tag 0x%08x:", chunkSize, chunkSize,
                       chunkHead[0]);
                PrintTag(chunkHead[0]);

                /* find a valid sample of primary stream */
                stream = self->streams[self->primaryStreamNum];
                stream->fileOffset = self->fileOffset;

                err = AviSearchValidSample(self, stream, &SuggestSeekOnErr);
                self->fileOffset = stream->fileOffset;
                if (PARSER_SUCCESS != err) {
                    return err; /* no valid sample can be found by error concealment */
                }

                sampleFlag |= FLAG_SAMPLE_ERR_CONCEALED;
                if (SuggestSeekOnErr)
                    sampleFlag |= FLAG_SAMPLE_SUGGEST_SEEK;
            }

            continue;
        }

        stream = self->streams[streamIndex];

        /* track disabled or not reach start point ? */
        if (!stream->enabled || (self->fileOffset < stream->fileOffset)) {
            if (!stream->enabled) {
                AVIMSG("trk %d, disabled, skip it\n", streamIndex);
            } else {
                AVIMSG("trk %d, not reach start offset %lld, current file offset %lld\n",
                       streamIndex, stream->fileOffset, self->fileOffset);
            }
            if (chunkSizeEven) /* skip this chunk */
                LocalFileSeek(inputStream, (int64)chunkSizeEven, SEEK_CUR, appContext);
            self->fileOffset += chunkSizeEven;
            continue;
        }

#ifdef SUPPORT_AVI_DRM
        if (self->protected && (chunkHead[0] == self->drmTag) &&
            (MEDIA_VIDEO == stream->mediaType)) { /* frame key is found for video stream*/
            if (chunkSize != DRM_FRAME_KEY_SIZE)
                return AVI_ERR_WRONG_CHUNK_SIZE;

            size = LocalFileRead(inputStream, self->frameKey, chunkSizeEven, appContext);
            if (chunkSizeEven != size)
                return PARSER_READ_ERROR;
            self->fileOffset += chunkSizeEven;
            continue; /* go ahead to find the data chunk */
        }
#endif

        if (chunkSize > stream->maxSampleSize) /* only check size for matched ID */
        {
            if (self->indexLoaded) {
                AVIMSG("trk %d, wrong chunk size %d, max sample size %d\n", *trackNum, chunkSize,
                       stream->maxSampleSize);
                stream->fileOffset = self->fileOffset;
                err = AviSearchValidSample(self, stream, &SuggestSeekOnErr);
                if (PARSER_SUCCESS == err) {
                    sampleFlag |= FLAG_SAMPLE_ERR_CONCEALED;
                    if (SuggestSeekOnErr)
                        sampleFlag |= FLAG_SAMPLE_SUGGEST_SEEK;
                    continue;
                } else
                    return err; /* no valid sample can be found by error concealment */
            } else {            /* index table does not give a valid max sample size,
                                and max sample size in file header may be invalid (eg. 0)! */
                stream->maxSampleSize = (chunkSize + 1) & (~1);
                AVIMSG("trk %u, update max sample size to %u\n", streamIndex,
                       stream->maxSampleSize);
            }
        }

        if ((MEDIA_VIDEO == stream->mediaType) && (stream->uncompressedVideoTag == chunkHead[0]))
            sampleFlag |= FLAG_UNCOMPRESSED_SAMPLE;

        stream->sampleSize = chunkSize;
        stream->sampleFlag = sampleFlag;
        stream->chunkHeaderRead = TRUE;

        stream->fileOffset = self->fileOffset;

        *trackNum = streamIndex;
        sampleGot = TRUE;
    }

    // bail:
    return err;
}

static uint32 findTrackNum(AviObjectPtr self, uint32 fourcc) {
    uint32 trackNum = PARSER_INVALID_TRACK_NUMBER;
    AVStreamPtr stream;
    uint32 i;

    if (!fourcc)
        return PARSER_INVALID_TRACK_NUMBER;

    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];

        if ((fourcc == stream->tag) || (fourcc == stream->uncompressedVideoTag)) {
            trackNum = i;
            break;
        } else if (fourcc == self->drmTag) {
            trackNum = self->primaryStreamNum;
            break;
        }
    }

    return trackNum;
}

/* find the smallest file offset of all enabled tracks.
Disabled tracks may not be seeked.*/
void findMinFileOffset(AviObjectPtr self) {
    AVStreamPtr stream;
    uint32 i;
    uint64 minFileOffset = self->moviEnd;

    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];
        if (!stream->enabled)
            continue;

        if (minFileOffset > stream->fileOffset)
            minFileOffset = stream->fileOffset;
    }

    self->fileOffset = minFileOffset;

    return;
}

/* find the smallest file offset of all enabled tracks.
Disabled tracks may not be seeked.*/
void findMaxFileOffset(AviObjectPtr self) {
    AVStreamPtr stream;
    uint32 i;
    uint64 maxFileOffset = self->moviList + 4;

    for (i = 0; i < self->numStreams; i++) {
        stream = self->streams[i];
        if (!stream->enabled || stream->bos)
            continue; /* no need to read an ended stream, which may has a bigger file offset */

        if (maxFileOffset < stream->fileOffset) {
            maxFileOffset = stream->fileOffset;
        }
    }

    self->fileOffset = maxFileOffset;

    return;
}

static void backupSampleStatus(AVStreamPtr stream, uint32 sampleFlags, uint32 sampleSize,
                               uint64 usSampleTimeStamp, uint64 usSampleDuration) {
    stream->sampleFlag = sampleFlags;
    stream->sampleSize = sampleSize;
    stream->usSampleStartTime = usSampleTimeStamp;
    stream->usSampleDuration = usSampleDuration;
}
