/*
***********************************************************************
* Copyright (c) 2010-2014, Freescale Semiconductor Inc.
* Copyright 2017, 2025-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
#include "flv_parser_api.h"

// #define DEBUG

#include "flv_parser.h"

extern const char* flvParserVersionInfo();

/*
Every core parser shall implement this function and tell a specific API function pointer.
If the queried API is not implemented, the parser shall set funtion pointer to NULL and return
PARSER_SUCCESS. */

int32 FslParserQueryInterface(uint32 id, void** func) {
    if (NULL == func)
        return PARSER_ERR_INVALID_PARAMETER;

    switch (id) {
        case PARSER_API_GET_VERSION_INFO:
            *func = (void*)FLVParserVersionInfo;
            break;
        case PARSER_API_CREATE_PARSER:
            *func = (void*)FLVCreateParser;
            break;
        case PARSER_API_CREATE_PARSER2:
            *func = (void*)FLVCreateParser2;
            break;
        case PARSER_API_DELETE_PARSER:
            *func = (void*)FLVDeleteParser;
            break;

        /* movie properties */
        case PARSER_API_IS_MOVIE_SEEKABLE:
            *func = (void*)FLVIsSeekable;
            break;
        case PARSER_API_GET_MOVIE_DURATION:
            *func = (void*)FLVGetDuration;
            break;

        case PARSER_API_GET_NUM_TRACKS:
            *func = (void*)FLVGetNumTracks;
            break;

        /* generic track properties */
        case PARSER_API_GET_TRACK_TYPE:
            *func = (void*)FLVGetTrackType;
            break;
        case PARSER_API_GET_TRACK_DURATION:
            *func = (void*)FLVGetTrackDuration;
            break;
        case PARSER_API_GET_BITRATE:
            *func = (void*)FLVGetBitRate;
            break;
        case PARSER_API_GET_DECODER_SPECIFIC_INFO:
            *func = FLVGetDecoderSpecificInfo;
            break;

        /* video properties */
        case PARSER_API_GET_VIDEO_FRAME_WIDTH:
            *func = (void*)FLVGetVideoFrameWidth;
            break;
        case PARSER_API_GET_VIDEO_FRAME_HEIGHT:
            *func = (void*)FLVGetVideoFrameHeight;
            break;
        case PARSER_API_GET_VIDEO_FRAME_RATE:
            *func = (void*)FLVGetVideoFrameRate;
            break;

        /* audio properties */
        case PARSER_API_GET_AUDIO_NUM_CHANNELS:
            *func = (void*)FLVGetAudioNumChannels;
            break;
        case PARSER_API_GET_AUDIO_SAMPLE_RATE:
            *func = (void*)FLVGetAudioSampleRate;
            break;

        /* sample reading, seek & trick mode */
        case PARSER_API_GET_READ_MODE:
            *func = (void*)FLVGetReadMode;
            break;
        case PARSER_API_SET_READ_MODE:
            *func = (void*)FLVSetReadMode;
            break;
        case PARSER_API_ENABLE_TRACK:
            *func = (void*)FLVEnableTrack;
            break;
        case PARSER_API_GET_NEXT_SAMPLE:
            *func = NULL;
            break;
        case PARSER_API_GET_NEXT_SYNC_SAMPLE:
            *func = NULL;
            break;
        case PARSER_API_GET_FILE_NEXT_SAMPLE:
            *func = (void*)FLVGetFileNextSample;
            break;
        case PARSER_API_GET_FILE_NEXT_SYNC_SAMPLE:
            *func = (void*)FLVGetFileNextSyncSample;
            break;
        case PARSER_API_SEEK:
            *func = (void*)FLVSeek;
            break;
        default:
            *func = NULL;
            break;
    }

    return PARSER_SUCCESS;
}

int32 FLVCreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                      ParserOutputBufferOps* outputBufferOps, void* context,
                      FslParserHandle* parserHandle) {
    uint32 flags = 0;
    if (isLive) {
        flags |= FILE_FLAG_NON_SEEKABLE;
        flags |= FILE_FLAG_READ_IN_SEQUENCE;
    }
    return FLVCreateParser2(flags, streamOps, memOps, outputBufferOps, context, parserHandle);
}

int32 FLVCreateParser2(uint32 flags, FslFileStream* streamOps, ParserMemoryOps* memOps,
                       ParserOutputBufferOps* outputBufferOps, void* context,
                       FslParserHandle* parserHandle) {
    if (NULL == streamOps || NULL == memOps || NULL == outputBufferOps || NULL == context ||
        NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    return flv_parser_open(parserHandle, flags, streamOps, memOps, outputBufferOps, context);
    //*parserHandle =
}

int32 FLVSetReadMode(FslParserHandle parserHandle, uint32 readMode) {
    (void)parserHandle;
    if (readMode != PARSER_READ_MODE_FILE_BASED)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

int32 FLVGetReadMode(FslParserHandle parserHandle, uint32* readMode) {
    (void)parserHandle;
    *readMode = PARSER_READ_MODE_FILE_BASED;

    return PARSER_SUCCESS;
}

/**
 * function to tell how many streams in the stream in the program with ID progId
 * If it's PS or MPEG1SS just set progId=0
 * @param parserHandle [in] Handle of the flv core parser.
 * @param progId [in]  Prog Id of program.
 * @param numstreams[out] Number of Stream
 * @return
 */

int32 FLVGetNumTracks(FslParserHandle parserHandle, uint32* numStreams) {
    flv_parser_t* p_flv_parser = parserHandle;
    uint32 numofStreams = 0;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->audio_found)
        numofStreams++;
    if (p_flv_parser->video_found)
        numofStreams++;

    *numStreams = numofStreams;
    return PARSER_SUCCESS;
}

int32 FLVEnableTrack(FslParserHandle parserHandle, uint32 numStream, bool enable) {
    flv_parser_t* p_flv_parser = parserHandle;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->audio_found) {
        if (numStream == 0)
            p_flv_parser->audio_enabled = enable;
        else if ((1 == numStream) && p_flv_parser->video_found)
            p_flv_parser->video_enabled = enable;
        else
            return PARSER_ERR_INVALID_PARAMETER;
    } else {
        if ((numStream == 0) && p_flv_parser->video_found)
            p_flv_parser->video_enabled = enable;
        else
            return PARSER_ERR_INVALID_PARAMETER;
    }

    return PARSER_SUCCESS;
}

int32 FLVGetDuration(FslParserHandle parserHandle, uint64* usDuration) {
    flv_parser_t* p_flv_parser = parserHandle;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    *usDuration = (uint64)p_flv_parser->stream_info.duration * 1000;

    /*
     if(*usDuration==0)
     {
        if(p_flv_parser->vids_index.entries_in_use!=0)
        {
             p_flv_parser->stream_info.duration = p_flv_parser->vids_index.range_time;
             *usDuration = (uint64)p_flv_parser->stream_info.duration*1000;
        }

       }
   */
    return PARSER_SUCCESS;
}

/**
 * function to tell the movie duration.
 *
 * @param parserHandle [in] Handle of the flv core parser.
 * @param streamNum[in]        the streamNum of the stream
 * @param usDuration [out]  Duration in us.
 * @return
 */
int32 FLVGetTrackDuration(FslParserHandle parserHandle, uint32 streamNum, uint64* usDuration) {
    flv_parser_t* p_flv_parser = parserHandle;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->audio_found) {
        if (streamNum == 0)
            *usDuration = (uint64)p_flv_parser->stream_info.duration * 1000;
        else if ((1 == streamNum) && p_flv_parser->video_found)
            *usDuration = (uint64)p_flv_parser->stream_info.duration * 1000;
        else
            return PARSER_ERR_INVALID_PARAMETER;
    } else {
        if ((streamNum == 0) && p_flv_parser->video_found)
            *usDuration = (uint64)p_flv_parser->stream_info.duration * 1000;
        else
            return PARSER_ERR_INVALID_PARAMETER;
    }

    return PARSER_SUCCESS;
}

/* stream-level info */
/**
 * function to tell the media type of a stream (video, audio, subtitle...)
 *
 * @param parserHandle [in] Handle of the flv core parser.
 * @param streamNum [in] Number of the stream, 0-based.
 * @param type [out] Media type of the stream.
 * @return
 */

int32 FLVGetTrackType(FslParserHandle parserHandle, uint32 streamNum, uint32* mediaType,
                      uint32* decoderType, uint32* decoderSubtype) {
    flv_parser_t* p_flv_parser = parserHandle;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->audio_found) {
        if (streamNum == 0) {
            *mediaType = MEDIA_AUDIO;
            *decoderType = flv_parser_convertaudiotype(p_flv_parser->stream_info.audio_info.format,
                                                       decoderSubtype);
        } else if ((1 == streamNum) && p_flv_parser->video_found) {
            *mediaType = MEDIA_VIDEO;
            *decoderType = flv_parser_convertvideotype(p_flv_parser->stream_info.video_info.codec,
                                                       decoderSubtype);
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    } else {
        if ((streamNum == 0) && p_flv_parser->video_found) {
            *mediaType = MEDIA_VIDEO;
            *decoderType = flv_parser_convertvideotype(p_flv_parser->stream_info.video_info.codec,
                                                       decoderSubtype);
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    }
    return PARSER_SUCCESS;
}

/**
 * function to tell the bitrate of a stream.
 * For CBR stream, the real bitrate is given.
 * For VBR stream, 0 is given since the bitrate varies during the playback and flv parser does not
 * calculate the peak or average bit rate.
 *
 * @param parserHandle [in] Handle of the flv core parser.
 * @param streamNum [in] ID of the stream. [in] Number of the stream, 0-based.
 * @param bitrate [out] Bitrate. For CBR stream, this is the real bitrate.
 *                                            For VBR stream, the bitrate is 0 since max bitrate is
 * usually not available.
 * @return
 */
int32 FLVGetBitRate(FslParserHandle parserHandle, uint32 streamNum, uint32* bitrate) {
    flv_parser_t* p_flv_parser = parserHandle;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->audio_found) {
        if (streamNum == 0) {
            *bitrate = 0;
        } else if ((1 == streamNum) && p_flv_parser->video_found) {
            *bitrate = 0;
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    } else {
        if ((streamNum == 0) && p_flv_parser->video_found) {
            *bitrate = 0;
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    }

    return PARSER_SUCCESS;
}

/**
 * function to tell the sample duration in us of a stream.
 * If the sample duration is not a constant (eg. some audio, subtilte), 0 is given.
 *
 * @param parserHandle [in] Handle of the flv core parser.
 * @param streamNum[in] ID of the stream.
 * @param usDuration [out] Sample duration in us. If sample duration is not a constant, this value
 * is 0.
 * @return
 */

int32 FLVGetVideoFrameRate(FslParserHandle parserHandle, uint32 streamNum, uint32* rate,
                           uint32* scale) {
    flv_parser_t* p_flv_parser = parserHandle;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->video_found) {
        if ((1 == streamNum) && p_flv_parser->audio_found) {
            *rate = p_flv_parser->metadata.framerate;
            *scale = 1;
        } else if (0 == streamNum) {
            *rate = p_flv_parser->metadata.framerate;
            *scale = 1;
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    } else
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

/**
 * function to tell the width in pixels of a video stream.
 * @param parserHandle [in] Handle of the flv core parser.
 * @param streamNum [in] ID of the stream. It must point to a video stream.
 * @param width [out] Width in pixels.
 * @return
 */
int32 FLVGetVideoFrameWidth(FslParserHandle parserHandle, uint32 streamNum, uint32* width) {
    flv_parser_t* p_flv_parser = parserHandle;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->audio_found) {
        if ((1 == streamNum) && p_flv_parser->video_found) {
            *width = p_flv_parser->stream_info.video_info.width;
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    } else {
        if ((streamNum == 0) && p_flv_parser->video_found) {
            *width = p_flv_parser->stream_info.video_info.width;
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    }

    return PARSER_SUCCESS;
}

/**
 * function to tell the height in pixels of a video stream.
 * @param parserHandle [in] Handle of the flv core parser.
 * @param streamNum [in] ID of the stream. It must point to a video stream.
 * @param height [out] Height in pixels.
 * @return
 */
int32 FLVGetVideoFrameHeight(FslParserHandle parserHandle, uint32 streamNum, uint32* height) {
    flv_parser_t* p_flv_parser = parserHandle;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->audio_found) {
        if ((1 == streamNum) && p_flv_parser->video_found) {
            *height = p_flv_parser->stream_info.video_info.height;

        } else
            return PARSER_ERR_INVALID_PARAMETER;
    } else {
        if ((streamNum == 0) && p_flv_parser->video_found) {
            *height = p_flv_parser->stream_info.video_info.height;
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    }

    return PARSER_SUCCESS;
}

/**
 * function to tell how many channels in an audio stream.
 *
 * @param parserHandle [in] Handle of the flv core parser.
 * @param streamNum [in] ID of the stream. It must point to an audio steam.
 * @param numchannels [out] Number of the channels. 1 mono, 2 stereo, or more for multiple channels.
 * @return
 */
int32 FLVGetAudioNumChannels(FslParserHandle parserHandle, uint32 streamNum, uint32* numchannels) {
    flv_parser_t* p_flv_parser = parserHandle;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->audio_found) {
        if (0 == streamNum) {
            *numchannels = p_flv_parser->stream_info.audio_info.channel;
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    } else
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

/**
 * function to tell the audio sample rate (sampling frequency) of an audio stream.
 *
 * @param parserHandle [in] Handle of the flv core parser.
 * @param streamNum [in] ID of the stream. It must point to an audio stream.
 * @param sampleRate [out] Audio integer sample rate (sampling frequency).
 * @return
 */
int32 FLVGetAudioSampleRate(FslParserHandle parserHandle, uint32 streamNum, uint32* sampleRate) {
    flv_parser_t* p_flv_parser = parserHandle;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->audio_found) {
        if (0 == streamNum) {
            *sampleRate = p_flv_parser->stream_info.audio_info.samplingRate;
        } else
            return PARSER_ERR_INVALID_PARAMETER;
    } else
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

int32 FLVGetFileNextSample(FslParserHandle parserHandle, uint32* streamNum, uint8** pSampleData,
                           void** pBufContext, uint32* dataSize, uint64* usPresTime,
                           uint64* usDuration, uint32* flag) {
    FLVPARSER_ERR_CODE err = PARSER_SUCCESS;
    flv_parser_t* p_flv_parser = parserHandle;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;
    if (NULL == streamNum || NULL == pSampleData || NULL == pBufContext || NULL == dataSize ||
        NULL == usPresTime || NULL == usDuration || NULL == flag)
        return PARSER_ERR_INVALID_PARAMETER;

    *usDuration = (uint64)PARSER_UNKNOWN_DURATION;
    err = flv_parser_get_file_next_sample(p_flv_parser, (int*)streamNum, pSampleData, pBufContext,
                                          dataSize, usPresTime, flag);

    return err;
}

int32 FLVIsSeekable(FslParserHandle parserHandle, bool* seekable) {
    flv_parser_t* p_flv_parser = parserHandle;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (p_flv_parser->isLive == TRUE)
        *seekable = FALSE;
    else
        *seekable = TRUE;  // p_flv_parser->stream_info.seekable;

    return PARSER_SUCCESS;
}

// these code still need some change
int32 FLVSeek(FslParserHandle parserHandle, uint32 streamNum, uint64* usTime, uint32 flag)

{
    flv_parser_t* p_flv_parser = parserHandle;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    uint32 msTime = (uint32)(*usTime / 1000);
    (void)streamNum;
    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;
    if (p_flv_parser->isLive && msTime != 0)
        return PARSER_ERR_NOT_SEEKABLE;

    if (*usTime == (uint64)-1) {
        // according to the API, it is the last time stamp actually wanted
        result = flv_parser_seekduration(p_flv_parser, &msTime);
        if (result == PARSER_SUCCESS) {
            *usTime = msTime * (uint64)1000;
            return PARSER_EOS;
        }
    }

    if (msTime != 0) {
        msTime += p_flv_parser->timestamp_base;
    }

    p_flv_parser->isLastSyncFinished = TRUE;
    if (p_flv_parser->historyBufLen != 0) {
        LocalFree(p_flv_parser->p_HistoryBuf);
        p_flv_parser->historyBufLen = 0;
        p_flv_parser->p_HistoryBuf = NULL;
    }

    result = flv_parser_seek(p_flv_parser, msTime, flag);
    if (result != PARSER_SUCCESS)
        return result;

    if (p_flv_parser->audio_found && p_flv_parser->video_found) {
        uint64 audiotime, videotime;

        result = flv_parser_get_current_position(p_flv_parser, FLV_AUDIO_SAMPLE, &audiotime);
        if (result != PARSER_SUCCESS)
            return result;

        result = flv_parser_get_current_position(p_flv_parser, FLV_VIDEO_SAMPLE, &videotime);
        if (result != PARSER_SUCCESS)
            return result;

        *usTime = audiotime < videotime ? audiotime : videotime;
    } else if (p_flv_parser->audio_found) {
        uint64 curtime;

        result = flv_parser_get_current_position(p_flv_parser, FLV_AUDIO_SAMPLE, &curtime);
        if (result != PARSER_SUCCESS)
            return result;
        *usTime = curtime;
    } else if (p_flv_parser->video_found) {
        uint64 curtime;
        result = flv_parser_get_current_position(p_flv_parser, FLV_VIDEO_SAMPLE, &curtime);
        if (result != PARSER_SUCCESS)
            return result;
        *usTime = curtime;
    }

    return PARSER_SUCCESS;
}

int32 FLVGetFileNextSyncSample(FslParserHandle parserHandle, uint32 direction, uint32* trackNum,
                               uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                               uint64* usStartTime, uint64* usDuration, uint32* flags) {
    flv_parser_t* p_flv_parser = parserHandle;
    bool stackAudioStatus;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    uint64 curTimeStamp;
    uint32 streamNum, flag;
    uint64 seekTime;

    if (parserHandle == NULL)
        return PARSER_ERR_INVALID_PARAMETER;

    if (!p_flv_parser->stream_info.seekable)
        return PARSER_ERR_NOT_SEEKABLE;

    streamNum = 0;
    if (!p_flv_parser->video_found)
        return PARSER_ERR_INVALID_PARAMETER;
    else if (p_flv_parser->audio_found)
        streamNum++;

    *usDuration = (uint64)PARSER_UNKNOWN_DURATION;
    if (!p_flv_parser->isLastSyncFinished) {
        result = flv_parser_get_file_next_sample(p_flv_parser, (int*)trackNum, sampleBuffer,
                                                 bufferContext, dataSize, usStartTime, flags);
        if ((result & FLAG_SAMPLE_NOT_FINISHED) == 0)
            p_flv_parser->isLastSyncFinished = TRUE;
        return result;
    }

    stackAudioStatus = p_flv_parser->audio_enabled;
    p_flv_parser->audio_enabled = FALSE;

    result = flv_parser_get_current_position(p_flv_parser, FLV_VIDEO_SAMPLE, &curTimeStamp);
    if (result)
        goto BAIL;

    if (direction == FLAG_FORWARD) {
        curTimeStamp = curTimeStamp + 1000;

        flag = SEEK_FLAG_NO_EARLIER;
        seekTime = (uint64)curTimeStamp;
        //? how can we return EOS here.
        result = FLVSeek(parserHandle, streamNum, &seekTime, flag);
        if (result)
            goto BAIL;
    }

    else {
        if (curTimeStamp < 1000) {
            result = PARSER_BOS;
            goto BAIL;
        } else
            curTimeStamp = curTimeStamp - 1000;
        //?how we can return a BOS here.
        flag = SEEK_FLAG_NO_LATER;
        seekTime = (uint64)curTimeStamp;

        result = FLVSeek(parserHandle, streamNum, &seekTime, flag);
        if (result) {
            msg_dbg("seek to %lld fail", curTimeStamp);
            goto BAIL;
        }
    }

    if (seekTime != 0) {
        result = flv_parser_get_file_next_sample(p_flv_parser, (int*)trackNum, sampleBuffer,
                                                 bufferContext, dataSize, usStartTime, flags);
        if (*flags & FLAG_SAMPLE_NOT_FINISHED)
            p_flv_parser->isLastSyncFinished = FALSE;
    } else {
        result = PARSER_BOS;
    }

BAIL:
    p_flv_parser->audio_enabled = stackAudioStatus;
    return result;
}

int32 FLVDeleteParser(FslParserHandle parserHandle) {
    flv_parser_t* p_flv_parser = parserHandle;

    if (NULL == parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    return flv_parser_close(p_flv_parser);
}

int32 FLVGetDecoderSpecificInfo(FslParserHandle parserHandle, uint32 trackNum, uint8** data,
                                uint32* size) {
    int32 result = PARSER_SUCCESS;
    flv_parser_t* p_flv_parser = NULL;

    do {
        if (parserHandle == NULL) {
            result = PARSER_ERR_INVALID_PARAMETER;
            break;
        }
        p_flv_parser = parserHandle;

        *size = 0;
        *data = NULL;

        if (p_flv_parser->audio_found) {
            if (trackNum == 0) {  //*mediaType = MEDIA_AUDIO;
                if (p_flv_parser->stream_info.audio_info.format == FLV_CODECID_AAC) {
                    *data = p_flv_parser->stream_info.audio_info.aac_specific_config.data;
                    *size = p_flv_parser->stream_info.audio_info.aac_specific_config.size;
                }
            } else if ((1 == trackNum) && p_flv_parser->video_found) {  //*mediaType = MEDIA_VIDEO;
                if (p_flv_parser->stream_info.video_info.codec == FLV_CODECID_H264) {
                    *size = p_flv_parser->stream_info.video_info.h264_codec_data.size;
                    *data = p_flv_parser->stream_info.video_info.h264_codec_data.data;
                }
            }
        } else {
            if ((trackNum == 0) && p_flv_parser->video_found) {  //*mediaType = MEDIA_VIDEO;
                if (p_flv_parser->stream_info.video_info.codec == FLV_CODECID_H264) {
                    *size = p_flv_parser->stream_info.video_info.h264_codec_data.size;
                    *data = p_flv_parser->stream_info.video_info.h264_codec_data.data;
                }
            }
        }

        result = PARSER_SUCCESS;
    } while (0);
    return result;
}
