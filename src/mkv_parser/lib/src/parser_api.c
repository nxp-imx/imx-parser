/***********************************************************************
 * Copyright 2009-2016, Freescale Semiconductor, Inc.
 * Copyright 2017-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "matroska.h"
#include "mkv_parser_api.h"
#include "streambuf.h"

#ifdef SUPPORT_MKV_DRM
#include "mkv_drm.h"
#if (defined(__WINCE) || defined(WIN32))
#include <windows.h>
#else
#include <errno.h>
#include <time.h> /* clock */
#endif
#endif

// #define MKV_DEBUG
#ifdef MKV_DEBUG
#ifdef ANDROID
#include <android/log.h>
#define LOG_BUF_SIZE 1024

void LogOutput(char* fmt, ...) {
    va_list ap;
    char buf[LOG_BUF_SIZE];

    va_start(ap, fmt);
    vsnprintf(buf, LOG_BUF_SIZE, fmt, ap);
    va_end(ap);

    __android_log_write(ANDROID_LOG_DEBUG, "MKV_PARSER", buf);

    return;
}

#define LOG_PRINTF LogOutput
#else
#define LOG_PRINTF printf
#endif
#else
#define LOG_PRINTF(...)
#endif

#ifdef __WINCE
#define MKV_API_ERROR_LOG(...)
#define MKV_API_LOG(...)
#else
#define MKV_API_ERROR_LOG LOG_PRINTF
#define MKV_API_LOG LOG_PRINTF
#endif

#define SEPARATOR " "

#define BASELINE_SHORT_NAME "MKVPARSER_02.00.00"

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

#define PARSER_VERSION_STR                                                                 \
    (BASELINE_SHORT_NAME OS_NAME CODEC_RELEASE_TYPE SEPARATOR VERSION_STR_SUFFIX SEPARATOR \
     "build on" SEPARATOR __DATE__ SEPARATOR __TIME__)

typedef void mkv_parser;

typedef struct {
    MKVReaderContext mkvctx;
    io_deps iodeps;
    ParserOutputBufferOps output_buffer_ops;
} mkv_internal_parser;
#if 0
int mkv_parser_is_matroska(uint8* ptr, int size)
{
    return is_matroska_file_type(ptr, size);
}
#endif
mkv_parser* mkv_parser_create(io_deps* pio_deps) {
    mkv_internal_parser* parser = NULL;

    if (!pio_deps->malloc_ptr)
        return NULL;

    parser = (mkv_internal_parser*)pio_deps->malloc_ptr(sizeof(mkv_internal_parser));

    if (!parser)
        return NULL;

    memset(parser, 0, sizeof(mkv_internal_parser));

    memcpy(&parser->iodeps, pio_deps, sizeof(io_deps));

    init_stream_buffer(&parser->mkvctx.bs, pio_deps, INIT_READ_HEADER_LEN, MIN_SEEK_SPACE);
    parser->mkvctx.direction = FLAG_FORWARD;

    return (mkv_parser*)parser;
}

int mkv_parser_read_headers(mkv_parser* parser) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return read_matroska_file_header(&iparser->mkvctx);
}

int mkv_parser_initialize_index(mkv_parser* parser) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_initialize_index(&iparser->mkvctx);
}

int mkv_parser_import_index(mkv_parser* parser, uint8* buffer, uint32 size) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;
    int32 i, offset = 0, track_index_size;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;
    MKVReaderContext* pctx = &iparser->mkvctx;

    if (!iparser || !buffer || size < 4 || !pctx)
        return -1;

    if (pctx->isLive)
        return -1;

    for (i = 1; i < pctx->stream_count + 1; i++) {
        track = matroska_find_track_by_num(pctx, i);
        if (!track)
            return -1;
        stream = (mkvStream*)track->stream;
        if (!stream)
            return -1;
        if (stream->index_list)
            free_stream_buffer(&pctx->bs, (char*)stream->index_list, NON_STREAM);

        stream->index_count = *(uint32*)(buffer + offset);
        offset += 4;
        track_index_size = stream->index_count * sizeof(CueIndex);
        if (offset + track_index_size > (int32)size)
            return -1;

        matroska_import_index(pctx, i, buffer + offset, track_index_size);

        offset += track_index_size;
    }

    return 0;
}

int mkv_parser_export_index(mkv_parser* parser, uint8* buffer, uint32* size) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;
    int32 i, offset = 0, track_index_size;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;
    MKVReaderContext* pctx = &iparser->mkvctx;

    if (!iparser || !size || !pctx)
        return -1;

    if (pctx->isLive)
        return -1;

    if (!buffer) {
        for (i = 1; i < pctx->stream_count + 1; i++) {
            offset += 4;
            track = matroska_find_track_by_num(pctx, i);
            if (!track)
                return -1;
            stream = (mkvStream*)track->stream;
            if (!stream)
                return -1;
            track_index_size = stream->index_count * sizeof(CueIndex);

            offset += track_index_size;
        }
        *size = offset;
        return 0;
    }

    for (i = 1; i < pctx->stream_count + 1; i++) {
        track = matroska_find_track_by_num(pctx, i);
        if (!track)
            return -1;
        stream = (mkvStream*)track->stream;
        if (!stream)
            return -1;

        *(uint32*)(buffer + offset) = stream->index_count;
        offset += 4;
        track_index_size = stream->index_count * sizeof(CueIndex);

        if (offset + track_index_size > (int32)(*size))
            return -1;

        if (matroska_export_index(&iparser->mkvctx, i, buffer + offset,
                                  (uint32*)(&track_index_size)) < 0)
            return -1;

        offset += track_index_size;
    }

    return 0;
}

int mkv_parser_is_seekable(mkv_parser* parser) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_is_seekable(&iparser->mkvctx);
}

int mkv_parser_get_trackcount(mkv_parser* parser) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_trackcount(&iparser->mkvctx);
}

int mkv_parser_get_userdata(mkv_parser* parser, uint32 userDataId, uint8** unicodeString,
                            uint32* stringLength) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_userdata(&iparser->mkvctx, userDataId, unicodeString, stringLength);
}

int mkv_parser_get_artwork(mkv_parser* parser, UserDataFormat* userDataFormat,
                           uint8** unicodeString, uint32* stringLength) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_artwork(&iparser->mkvctx, userDataFormat, unicodeString, stringLength);
}

int mkv_parser_get_movie_duration(mkv_parser* parser, uint64* duration) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_movie_duration(&iparser->mkvctx, duration);
}

int mkv_parser_get_track_duration(mkv_parser* parser, uint32 tracknum, uint64* duration) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_track_duration(&iparser->mkvctx, tracknum, duration);
}

int mkv_parser_get_track_position(mkv_parser* parser, uint32 tracknum, uint64* timestamp) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_track_position(&iparser->mkvctx, tracknum, timestamp);
}

int mkv_parser_get_track_type(mkv_parser* parser, uint32 tracknum, uint32* mediaType,
                              uint32* decoderType, uint32* decoderSubtype) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_track_type(&iparser->mkvctx, tracknum, mediaType, decoderType,
                                   decoderSubtype);
}

int mkv_parser_get_language(mkv_parser* parser, uint32 tracknum, char* langcode) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_language(&iparser->mkvctx, tracknum, langcode);
}

int mkv_parser_get_max_samplesize(mkv_parser* parser, uint32 tracknum, uint32* size) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_max_samplesize(&iparser->mkvctx, tracknum, size);
}

int mkv_parser_get_bitrate(mkv_parser* parser, uint32 tracknum, uint32* bitrate) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_bitrate(&iparser->mkvctx, tracknum, bitrate);
}

int mkv_parser_get_ext_tag_list(mkv_parser* parser, uint32 tracknum, TrackExtTagList** pList) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_track_ext_taglist(&iparser->mkvctx, tracknum, pList);
}

int mkv_parser_get_sample_duration(mkv_parser* parser, uint32 tracknum, uint64* duration) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_sample_duration(&iparser->mkvctx, tracknum, duration);
}

int mkv_parser_get_video_frame_width(mkv_parser* parser, uint32 tracknum, uint32* width) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_video_frame_width(&iparser->mkvctx, tracknum, width);
}

int mkv_parser_get_video_frame_height(mkv_parser* parser, uint32 tracknum, uint32* height) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_video_frame_height(&iparser->mkvctx, tracknum, height);
}

int mkv_parser_get_video_pixelbits(mkv_parser* parser, uint32 tracknum, uint32* bitcount) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_video_pixelbits(&iparser->mkvctx, tracknum, bitcount);
}

int mkv_parser_get_audio_channels(mkv_parser* parser, uint32 tracknum, uint32* channels) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_audio_channels(&iparser->mkvctx, tracknum, channels);
}

int mkv_parser_get_audio_samplerate(mkv_parser* parser, uint32 tracknum, uint32* samplerate) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_audio_samplerate(&iparser->mkvctx, tracknum, samplerate);
}

int mkv_parser_get_audio_samplebits(mkv_parser* parser, uint32 tracknum, uint32* samplebits) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_audio_samplebits(&iparser->mkvctx, tracknum, samplebits);
}

int mkv_parser_get_audio_bits_per_frame(mkv_parser* parser, uint32 tracknum,
                                        uint32* bits_per_frame) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;

    if (!iparser)
        return -1;

    track = matroska_find_track_by_num(&iparser->mkvctx, tracknum);
    if (!track)
        return -1;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    if (!stream->has_ra_info)
        return 0;

    *bits_per_frame = stream->rainfo.sub_packet_size * 8;

    return 0;
}

int mkv_parser_get_text_width(mkv_parser* parser, uint32 tracknum, uint32* width) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_text_width(&iparser->mkvctx, tracknum, width);
}

int mkv_parser_get_text_height(mkv_parser* parser, uint32 tracknum, uint32* height) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_text_height(&iparser->mkvctx, tracknum, height);
}

int mkv_parser_track_seek(mkv_parser* parser, uint32 tracknum, uint64 utime, uint32 flags) {
    int i;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;
    TrackEntry* track = NULL;
    MKVReaderContext* pctx = NULL;
    mkvStream* stream = NULL;

    if (!iparser)
        return -1;

    pctx = &iparser->mkvctx;

    track = matroska_find_track_by_num(pctx, tracknum);
    if (!track)
        return -1;
    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    /*processing seek to zero before start playback,
     * should always return OK regardless the file is seekable,
     * due to our parser API's defination.*/
    if (!utime && !stream->position && !track->is_not_new_segment && !pctx->switchTrack) {
        return PARSER_SUCCESS;
    }

    track->is_not_new_segment = 0;

    for (i = 0; i < pctx->stream_count; i++) {
        pctx->stream_list[i].track_enable = pctx->stream_list[i].force_enable;
    }

    return matroska_file_seek(pctx, utime, flags, track->track_type);
}

int mkv_parser_get_extra_data(mkv_parser* parser, uint32 tracknum, uint8** data, uint32* size) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_extra_data(&iparser->mkvctx, tracknum, data, size);
}

int mkv_parser_get_wave_format(mkv_parser* parser, uint32 tracknum, WaveFormatEx** waveinfo) {
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    return matroska_get_wave_format(&iparser->mkvctx, tracknum, waveinfo);
}

int mkv_parser_next_sample(mkv_parser* parser, uint32 trackNum, uint8** sampleData,
                           void** bufferContext, uint32* sampleSize, uint64* usStartTime,
                           uint64* usDuration, uint32* flag) {
    int i, retval;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;
    TrackEntry* track = NULL;
    MKVReaderContext* pctx;
    uint32 data_size, buf_size;
    uint32 external_track_num = trackNum - 1;
    void* output_buf_context;

    if (!iparser || !sampleSize)
        return -1;

    pctx = &iparser->mkvctx;

    if (PARSER_READ_MODE_TRACK_BASED != pctx->readMode)
        return PARSER_ERR_INVALID_READ_MODE;

    for (i = 0; i < pctx->stream_count; i++) {
        pctx->stream_list[i].track_enable = pctx->stream_list[i].force_enable;
    }

    track = matroska_find_track_by_num(pctx, trackNum);
    if (!track)
        return -1;

    if (!track->is_sample_data_remained) {
        track->data_offset = 0;
        do {
            retval = matroska_get_packet(&iparser->mkvctx, &track->packet, trackNum);

            if (retval != 0)
                return retval;

            /*drop video/caption track samples prior to current file position in normal play mode.
             * because the track seek actually done by file seek,seek audio track when switch audio
             * track will cause the core parser send samples prior to current position */
            if (track->packet.stream_index == (int)trackNum) {
                if (track->track_type == MATROSKA_TRACK_TYPE_AUDIO) {
                    break;
                } else if (track->packet.pos > track->curr_pos || !track->is_not_new_segment) {
                    break;
                }
            }

            matroska_free_packet(&iparser->mkvctx, &track->packet);
        } while (1);
    }
    data_size = buf_size = track->packet.size - track->data_offset;

    *sampleData = iparser->output_buffer_ops.RequestBuffer(
            external_track_num, &buf_size, &output_buf_context, iparser->iodeps.context);

    if (!(*sampleData))
        return PARSER_INSUFFICIENT_MEMORY;

    if (buf_size < data_size)
        data_size = buf_size;

    memcpy(*sampleData, track->packet.data + track->data_offset, data_size);

    *sampleSize = data_size;
    if (pctx->sctx.info.time_code_scale > 0)
        *usStartTime = track->packet.pts * pctx->sctx.info.time_code_scale / 1000;
    else
        *usStartTime = track->packet.pts * 1000;
    *usDuration = track->packet.duration * 1000;
    *flag = track->packet.flags;
    *bufferContext = output_buf_context;

    track->data_offset += data_size;

    track->curr_pos = track->packet.pos;
    track->is_not_new_segment = TRUE;
    MKV_API_LOG("mkv_parser_next_sample out track=%d,ts=%lld", trackNum, *usStartTime);

    if (track->packet.flags & PKT_FLAG_CODEC_DATA)
        *flag |= FLAG_SAMPLE_CODEC_DATA;

    if (track->data_offset < (unsigned int)track->packet.size) {
        *flag |= FLAG_SAMPLE_NOT_FINISHED;
        track->is_sample_data_remained = TRUE;
    } else {
        matroska_free_packet(&iparser->mkvctx, &track->packet);
        track->is_sample_data_remained = FALSE;
        track->last_ts = *usStartTime;
    }

    return 0;
}
#if 0
int mkv_parser_get_file_next_sample(mkv_parser * parser,
                             uint32 * trackNum,
                             uint8 ** sampleData,  
                             void  ** bufferContext,
                             uint32 * sampleSize, 
                             uint64 * usStartTime, 
                             uint64 * usDuration,
                             uint32 * flag)
{
    int retval = 0;
    mkv_internal_parser    *iparser = (mkv_internal_parser *)parser;
    TrackEntry *track = NULL;
    MKVReaderContext *pctx;
    uint32 data_size,buf_size;
    uint32 external_track_num = *trackNum - 1;
    void  * output_buf_context;
    int i, curTrackIdx;

    if(!iparser || !sampleSize) return -1;

    pctx = &iparser->mkvctx;

    if(PARSER_READ_MODE_FILE_BASED != pctx->readMode)
        return PARSER_ERR_INVALID_READ_MODE;

    for(i=0; i<pctx->stream_count; i++)
    {
        pctx->stream_list[i].track_enable = pctx->stream_list[i].force_enable;
    }

    do
    {
        retval = matroska_get_next_packet_from_cluster(&iparser->mkvctx, trackNum);

        if(retval != 0) return retval;

        curTrackIdx = *trackNum;

        track = matroska_find_track_by_num(pctx, curTrackIdx);
        if(!track) return -1;

        /*drop video/caption track samples prior to current file position in normal play mode.
        * because the track seek actually done by file seek,seek audio track when switch audio track will
        * cause the core parser send samples prior to current position */
        {
            if (track->track_type == MATROSKA_TRACK_TYPE_AUDIO)
            {
                break;
            }
            else if (track->packet.pos > track->curr_pos || !track->is_not_new_segment)
            {
                break;
            }
        }

        matroska_free_packet(&iparser->mkvctx, &track->packet);
    }
    while(1);

    data_size = buf_size = track->packet.size - track->data_offset;

    *sampleData = iparser->output_buffer_ops.RequestBuffer(curTrackIdx-1, 
        &buf_size, 
        &output_buf_context, 
        iparser->iodeps.context);

    if(!(*sampleData))
        return PARSER_INSUFFICIENT_MEMORY;

    if (buf_size < data_size)
        data_size = buf_size;

    memcpy(*sampleData, track->packet.data + track->data_offset, data_size);

    *sampleSize = data_size;
    *usStartTime = track->packet.pts * 1000;
    *usDuration = track->packet.duration * 1000;
    *flag = track->packet.flags;
    *bufferContext = output_buf_context;

    track->data_offset += data_size;

    track->curr_pos = track->packet.pos;
    track->is_not_new_segment = TRUE;

    if (track->data_offset  < (unsigned int)track->packet.size)
    {
        *flag |= FLAG_SAMPLE_NOT_FINISHED;
        track->is_sample_data_remained = TRUE;
    }
    else
    {
        matroska_free_packet(&iparser->mkvctx, &track->packet);
        track->is_sample_data_remained = FALSE;
        track->last_ts = *usStartTime;
    }

    return 0;

}
#else
int mkv_parser_file_next_sample(mkv_parser* parser, uint32* trackNum, uint8** sampleData,
                                void** bufferContext, uint32* sampleSize, uint64* usStartTime,
                                uint64* usDuration, uint32* flag) {
    int retval = 0;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;
    TrackEntry* track = NULL;
    MKVReaderContext* pctx;
    uint32 data_size, buf_size;
    void* output_buf_context;

    if (!iparser || !sampleSize)
        return -1;

    pctx = &iparser->mkvctx;

    if (!pctx->remaining_sample) {
        do {
            retval = matroska_get_next_packet_from_cluster(&iparser->mkvctx, trackNum);

            if (retval != 0)
                return retval;

            track = matroska_find_track_by_num(pctx, *trackNum);
            if (!track)
                return -1;

            /*drop video/caption track samples prior to current file position in normal play mode.
             * because the track seek actually done by file seek,seek audio track when switch audio
             * track will cause the core parser send samples prior to current position */
            if (track->track_type == MATROSKA_TRACK_TYPE_AUDIO) {
                break;
            } else if (track->packet.pos > track->curr_pos || !track->is_not_new_segment) {
                break;
            }

            matroska_free_packet(&iparser->mkvctx, &track->packet);
        } while (1);

        data_size = buf_size = track->packet.size;
        MKV_API_LOG("mkv_parser_file_next_sample track=%d end \n", *trackNum);

    } else {
        int track_count = pctx->sctx.track_count;
        TrackEntry* tracks = pctx->sctx.track_list;
        int i;

        for (i = 0; i < track_count; i++) {
            if (tracks[i].is_sample_data_remained) {
                track = &tracks[i];
                *trackNum = track->track_num;
                break;
            }
        }
        MKV_API_LOG("mkv_parser_file_next_sample track=%d", i);

        if (track == NULL)
            return PARSER_ERR_UNKNOWN;

        data_size = buf_size = track->packet.size - track->data_offset;

        MKV_API_LOG("data_offset=%d,data_size=%d", track->data_offset, data_size);
    }

    *sampleData = iparser->output_buffer_ops.RequestBuffer(
            (*trackNum) - 1, &buf_size, &output_buf_context, iparser->iodeps.context);

    if (!(*sampleData))
        return PARSER_INSUFFICIENT_MEMORY;

    if (buf_size < data_size)
        data_size = buf_size;

    memcpy(*sampleData, track->packet.data + track->data_offset, data_size);

    *sampleSize = data_size;
    if (pctx->sctx.info.time_code_scale > 0)
        *usStartTime = track->packet.pts * pctx->sctx.info.time_code_scale / 1000;
    else
        *usStartTime = track->packet.pts * 1000;
    *usDuration = track->packet.duration * 1000;
    *flag = track->packet.flags;
    *bufferContext = output_buf_context;

    track->data_offset += data_size;

    track->curr_pos = track->packet.pos;
    track->is_not_new_segment = TRUE;

    if (track->packet.flags & PKT_FLAG_CODEC_DATA)
        *flag |= FLAG_SAMPLE_CODEC_DATA;

    if (track->data_offset < (unsigned int)track->packet.size) {
        *flag |= FLAG_SAMPLE_NOT_FINISHED;
        track->is_sample_data_remained = TRUE;
        pctx->remaining_sample = TRUE;
    } else {
        matroska_free_packet(&iparser->mkvctx, &track->packet);
        track->is_sample_data_remained = FALSE;
        track->last_ts = *usStartTime;
        track->data_offset = 0;
        pctx->remaining_sample = FALSE;
    }

    return 0;
}
#endif

int mkv_parser_get_sync_sample(mkv_parser* parser, uint32 direction, uint32 trackNum,
                               uint8** sampleData, void** bufferContext, uint32* sampleSize,
                               uint64* usStartTime, uint64* usDuration, uint32* flag) {
#define MAX_SYNC_LOOPCNT 1000  // max video frame numbers(gop)
    int i, retval;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;
    TrackEntry* track = NULL;
    MKVReaderContext* pctx;
    uint32 data_size, buf_size;
    uint32 external_track_num = trackNum - 1;
    void* output_buf_context;
    mkvStream* stream = NULL;
    uint32 seek_flag;
    int maxcnt = 0;  // for big clips (with block group), may no reference flag. So we need to add
                     // max cnt to avoid hangup
#if 1
    /*since we change the strategy of flags(SEEK_FLAG_NO_EARLIER/SEEK_FLAG_NO_LATER) to fix
    ENGR142866, we need to add below variables to avoid dead loop in trick mode */
    int det_step = 0;
    int det_scale = 1;
#endif

    pctx = &iparser->mkvctx;

    if (PARSER_READ_MODE_TRACK_BASED != pctx->readMode)
        return PARSER_ERR_INVALID_READ_MODE;

    track = matroska_find_track_by_num(pctx, trackNum);
    if (!track)
        return -1;
    stream = (mkvStream*)track->stream;
    if (!stream)
        return -1;

    if (stream->track_type != MATROSKA_TRACK_TYPE_VIDEO)
        return -1;

    if (!stream->index_list)
        return -1;

    if (pctx->sctx.info.time_code_scale) {
#if 0
        det_scale = pctx->sctx.info.time_code_scale/1000 ;
        det_step = 1000000 / pctx->sctx.info.time_code_scale;
#else
        det_scale = 1000;
#endif
    }

    for (i = 0; i < pctx->stream_count; i++) {
        if (i == (int)external_track_num)
            continue;
        pctx->stream_list[i].track_enable = 0;
        ;
    }

    if (!track->is_sample_data_remained) {
        track->data_offset = 0;

        /*seek to next/prev sync sample*/
        if (direction == FLAG_FORWARD) {
            if (track->seek_to_EOS) {
                MKV_API_LOG("seek to EOS \r\n");
                return PARSER_EOS;
            }
            seek_flag = SEEK_FLAG_NO_EARLIER;
            det_step = 1;
            iparser->mkvctx.direction = FLAG_FORWARD;
        } else {
            if (track->seek_to_BOS) {
                MKV_API_LOG("seek to BOS \r\n");
                return PARSER_BOS;
            }
            seek_flag = SEEK_FLAG_NO_LATER;
            det_step = -1;
            iparser->mkvctx.direction = FLAG_BACKWARD;
        }

        MKV_API_LOG("file seek: last ts: %lld \r\n", track->last_ts);
        iparser->mkvctx.is_in_trick_mode = TRUE;
        retval = matroska_file_seek(&iparser->mkvctx, track->last_ts, seek_flag,
                                    MATROSKA_TRACK_TYPE_VIDEO);
        iparser->mkvctx.is_in_trick_mode = FALSE;

        if (retval != 0)
            return retval;

        do {
            retval = matroska_get_packet(&iparser->mkvctx, &track->packet, trackNum);
            if (retval != 0) {
                MKV_API_LOG("failur to get sync sample \r\n");
                return retval;
            }

            if (!(track->packet.flags & FLAG_SYNC_SAMPLE)) {
                MKV_API_LOG("%s,%d.\n", __FUNCTION__, __LINE__);
            }

            if (track->packet.stream_index == (int)trackNum &&
                track->packet.flags & FLAG_SYNC_SAMPLE) {
                MKV_API_LOG("find sync sampe: track: %d  \r\n", trackNum);
                break;
            }
            maxcnt++;
            if ((maxcnt >= MAX_SYNC_LOOPCNT) && (track->packet.stream_index == (int)trackNum)) {
                // 003.Attack.The.Gas.Station.2.DVDRip.x264-CBSS.MKV: e 0 1 => c -16
                // FIXME: application may hangup, so we return failure
                matroska_free_packet(&iparser->mkvctx, &track->packet);
                return PARSER_NOT_IMPLEMENTED;
            }

            matroska_free_packet(&iparser->mkvctx, &track->packet);
        } while (1);
    } else {
        if (direction == FLAG_FORWARD)
            det_step = 1;
        else
            det_step = -1;
    }
    iparser->mkvctx.direction = FLAG_FORWARD;

    data_size = buf_size = track->packet.size - track->data_offset;

    *sampleData = iparser->output_buffer_ops.RequestBuffer(
            external_track_num, &buf_size, &output_buf_context, iparser->iodeps.context);

    if (!(*sampleData))
        return PARSER_INSUFFICIENT_MEMORY;

    if (buf_size < data_size)
        data_size = buf_size;

    memcpy(*sampleData, track->packet.data + track->data_offset, data_size);

    *sampleSize = data_size;
    if (pctx->sctx.info.time_code_scale > 0)
        *usStartTime = track->packet.pts * pctx->sctx.info.time_code_scale / 1000;
    else
        *usStartTime = track->packet.pts * 1000;
    *usDuration = track->packet.duration * 1000;
    *flag = track->packet.flags;
    *bufferContext = output_buf_context;

    track->data_offset += data_size;
    if (track->data_offset < (unsigned int)track->packet.size) {
        *flag |= FLAG_SAMPLE_NOT_FINISHED;
        track->is_sample_data_remained = TRUE;
    } else {
        matroska_free_packet(&iparser->mkvctx, &track->packet);
        track->is_sample_data_remained = FALSE;
        track->last_ts = *usStartTime;
        if (((int64)track->last_ts) + det_step * det_scale > 0)
            track->last_ts = (uint64)(((int64)track->last_ts) + det_step * det_scale);
    }

    return 0;
}

int mkv_parser_get_file_next_sync_sample(mkv_parser* parser, uint32 direction, uint32* trackNum,
                                         uint8** sampleData, void** bufferContext,
                                         uint32* sampleSize, uint64* usStartTime,
                                         uint64* usDuration, uint32* flag) {
    MKVReaderContext* pctx = NULL;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;
    uint32 data_size, buf_size;
    void* output_buf_context;
    uint32 seek_flag;
    int maxcnt = 0;  // for big clips (with block group), may no reference flag. So we need to add
                     // max cnt to avoid hangup
#if 1
    /*since we change the strategy of flags(SEEK_FLAG_NO_EARLIER/SEEK_FLAG_NO_LATER) to fix
    ENGR142866, we need to add below variables to avoid dead loop in trick mode */
    int det_step = 0;
    int det_scale = 1;
#endif

    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    int curTrackNum, retval;

    pctx = &iparser->mkvctx;
    if (!pctx)
        return -1;

    if (PARSER_READ_MODE_FILE_BASED != pctx->readMode)
        return PARSER_ERR_INVALID_READ_MODE;

    if (pctx->sctx.info.time_code_scale) {
        det_scale = 1000;
    }

    for (curTrackNum = 0; curTrackNum < pctx->stream_count; curTrackNum++) {
        track = matroska_find_track_by_num(pctx, curTrackNum + 1);
        if (!track)
            return -1;

        stream = (mkvStream*)track->stream;
        if (!stream)
            return -1;

        if (stream->track_type != MATROSKA_TRACK_TYPE_VIDEO)
            continue;  // return -1;

        if (!stream->index_list)
            return -1;

        if (pctx->stream_list[curTrackNum].track_enable == 0)
            continue;

        if (!track->is_sample_data_remained) {
            track->data_offset = 0;

            /*seek to next/prev sync sample*/
            if (direction == FLAG_FORWARD) {
                if (track->seek_to_EOS) {
                    MKV_API_LOG("seek to EOS \r\n");
                    return PARSER_EOS;
                }
                seek_flag = SEEK_FLAG_NO_EARLIER;
                det_step = 1;
                iparser->mkvctx.direction = FLAG_FORWARD;
            } else {
                if (track->seek_to_BOS) {
                    MKV_API_LOG("seek to BOS \r\n");
                    return PARSER_BOS;
                }
                seek_flag = SEEK_FLAG_NO_LATER;
                det_step = -1;
                iparser->mkvctx.direction = FLAG_BACKWARD;
            }

            MKV_API_LOG("file seek: last ts: %lld \r\n", track->last_ts);
            iparser->mkvctx.is_in_trick_mode = TRUE;
            retval = matroska_file_seek(&iparser->mkvctx, track->last_ts, seek_flag,
                                        MATROSKA_TRACK_TYPE_VIDEO);
            iparser->mkvctx.is_in_trick_mode = FALSE;

            if (retval != 0)
                return retval;

            do {
                retval = matroska_get_packet(&iparser->mkvctx, &track->packet, curTrackNum + 1);
                if (retval != 0) {
                    MKV_API_LOG("failur to get sync sample \r\n");
                    return retval;
                }
                if (!(track->packet.flags & FLAG_SYNC_SAMPLE)) {
                    MKV_API_LOG("%s,%d.\n", __FUNCTION__, __LINE__);
                }

                if (track->packet.stream_index == (int)(curTrackNum + 1) &&
                    track->packet.flags & FLAG_SYNC_SAMPLE) {
                    MKV_API_LOG("find sync sampe: track: %d  \r\n", *trackNum);
                    break;
                }
                maxcnt++;
                if ((maxcnt >= MAX_SYNC_LOOPCNT) &&
                    (track->packet.stream_index == (int)(curTrackNum + 1))) {
                    // 003.Attack.The.Gas.Station.2.DVDRip.x264-CBSS.MKV: e 0 1 => c -16
                    // FIXME: application may hangup, so we return failure
                    matroska_free_packet(&iparser->mkvctx, &track->packet);
                    return PARSER_NOT_IMPLEMENTED;
                }

                matroska_free_packet(&iparser->mkvctx, &track->packet);
            } while (1);
            break;
        } else
            break;
    }
    iparser->mkvctx.direction = FLAG_FORWARD;

    data_size = buf_size = track->packet.size - track->data_offset;

    *sampleData = iparser->output_buffer_ops.RequestBuffer(
            curTrackNum, &buf_size, &output_buf_context, iparser->iodeps.context);

    if (!(*sampleData))
        return PARSER_INSUFFICIENT_MEMORY;

    if (buf_size < data_size)
        data_size = buf_size;

    memcpy(*sampleData, track->packet.data + track->data_offset, data_size);

    *sampleSize = data_size;
    if (pctx->sctx.info.time_code_scale > 0)
        *usStartTime = track->packet.pts * pctx->sctx.info.time_code_scale / 1000;
    else
        *usStartTime = track->packet.pts * 1000;
    *usDuration = track->packet.duration * 1000;
    *flag = track->packet.flags;
    *bufferContext = output_buf_context;

    track->data_offset += data_size;
    if (track->data_offset < (unsigned int)track->packet.size) {
        *flag |= FLAG_SAMPLE_NOT_FINISHED;
        track->is_sample_data_remained = TRUE;
    } else {
        matroska_free_packet(&iparser->mkvctx, &track->packet);
        track->is_sample_data_remained = FALSE;
        track->last_ts = *usStartTime;
        if (0 < (((int64)track->last_ts) + det_step * det_scale))
            track->last_ts = (uint64)(((int64)track->last_ts) + det_step * det_scale);
    }

    *trackNum = curTrackNum + 1;

    return 0;
}

int mkv_parser_destory(mkv_parser* parser) {
    free_func_ptr fpfree = NULL;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser;

    if (!iparser)
        return -1;

    fpfree = iparser->mkvctx.bs.fpfree;

    if (!fpfree)
        return -1;

    close_matroska_file_header(&iparser->mkvctx);

    deinit_stream_buffer(&iparser->mkvctx.bs);

    fpfree(parser);

    return 0;
}

int mkv_parser_clear_track(mkv_parser* parser, uint32 tracknum) {
    TrackEntry* track = NULL;
    MKVReaderContext* pctx = NULL;
    mkv_internal_parser* iparser = NULL;

    if (!parser)
        return -1;

    iparser = (mkv_internal_parser*)parser;

    pctx = &iparser->mkvctx;

    track = matroska_find_track_by_num(pctx, tracknum);

    if (!track)
        return -1;

    if (!pctx->isLive)
        return 0;

    matroska_parser_flush_track(pctx);

    matroska_clear_queue(pctx, tracknum);

    return 0;
}

EXTERN const char* MkvParserVersionInfo() {
    return PARSER_VERSION_STR;
}

EXTERN int32 MkvCreateParser(bool isLive, FslFileStream* stream, ParserMemoryOps* memOps,
                             ParserOutputBufferOps* outputBufferOps, void* context,
                             FslParserHandle* parserHandle) {
    uint32 flags = 0;
    if (isLive) {
        flags |= FILE_FLAG_NON_SEEKABLE;
        flags |= FILE_FLAG_READ_IN_SEQUENCE;
    }
    return MkvCreateParser2(flags, stream, memOps, outputBufferOps, context, parserHandle);
}

EXTERN int32 MkvCreateParser2(uint32 flags, FslFileStream* stream, ParserMemoryOps* memOps,
                              ParserOutputBufferOps* outputBufferOps, void* context,
                              FslParserHandle* parserHandle) {
    int retval = 0;
    io_deps iodeps;
    mkv_parser* parser = NULL;
    mkv_internal_parser* iparser = NULL;

    const uint8 flag[] = "rb";

    if (!stream || !memOps || !parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    iodeps.filehandle = stream->Open(NULL, flag, context);
    if (!iodeps.filehandle)
        return PARSER_FILE_OPEN_ERROR;

    iodeps.context = context;
    iodeps.filesize = stream->Size(iodeps.filehandle, context);
    iodeps.fread_ptr = stream->Read;
    iodeps.fseek_ptr = stream->Seek;
    iodeps.fclose_ptr = stream->Close;
    iodeps.malloc_ptr = memOps->Malloc;
    iodeps.free_ptr = memOps->Free;

    parser = mkv_parser_create(&iodeps);
    if (!parser) {
        stream->Close(iodeps.filehandle, context);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    iparser = (mkv_internal_parser*)parser;
    iparser->output_buffer_ops.RequestBuffer = outputBufferOps->RequestBuffer;
    iparser->output_buffer_ops.ReleaseBuffer = outputBufferOps->ReleaseBuffer;

    iparser->mkvctx.isLive = FALSE;  // Set IsLiving Flag
    iparser->mkvctx.flags = flags;
    if ((flags & FILE_FLAG_NON_SEEKABLE) && (flags & FILE_FLAG_READ_IN_SEQUENCE)) {
        iparser->mkvctx.isLive = TRUE;
    }
    if (TRUE == iparser->mkvctx.isLive)
        iparser->mkvctx.readMode = PARSER_READ_MODE_FILE_BASED;
    else
        iparser->mkvctx.readMode = PARSER_READ_MODE_TRACK_BASED;  // default read mode

    PARSERMSG("MkvCreateParser2 flags=0x%x,isLive=%d, size=%lld\n", flags, iparser->mkvctx.isLive,
              iodeps.filesize);
    *parserHandle = (FslParserHandle*)parser;
    retval = mkv_parser_read_headers(parser);
    if (retval == -1)
        return MKV_ERR_NOT_MKV_FILE;

#ifdef SUPPORT_MKV_DRM
    // Initialize the DRM API function pointers here
    if (iparser->mkvctx.sctx.bHasDRMHdr) {
        iparser->mkvctx.bHasDrmLib = FALSE;
        iparser->mkvctx.hDRMLib = NULL;
        memset((void*)(&iparser->mkvctx.sDrmAPI), 0, sizeof(drmAPI_s));
        iparser->mkvctx.bHasDrmLib = LoadDrmLibrary(&(iparser->mkvctx));
        if (iparser->mkvctx.bHasDrmLib) {
            PARSERMSG("Load DRM Library OK! \n");
        } else {
            PARSERMSG("Failed to load DRM Library! \n");
        }
    }
#endif

    return PARSER_SUCCESS;
}

EXTERN int32 MkvDeleteParser(FslParserHandle parserHandle) {
    mkv_internal_parser* iparser = NULL;
    io_deps* iodeps = NULL;

    if (!parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    iparser = (mkv_internal_parser*)parserHandle;
    iodeps = &iparser->iodeps;

    if (iodeps->filehandle && iodeps->fclose_ptr)
        iodeps->fclose_ptr(iodeps->filehandle, iodeps->context);

#ifdef SUPPORT_MKV_DRM
    if ((iparser->mkvctx.sctx.bHasDRMHdr) && (iparser->mkvctx.bHasDrmLib)) {
        UnloadDrmLibrary(&(iparser->mkvctx));
    }
#endif

    mkv_parser_destory((mkv_parser*)parserHandle);

    return PARSER_SUCCESS;
}

EXTERN int32 MkvInitializeIndex(FslParserHandle parserHandle) {
    int retval = 0;

    if (!parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_initialize_index((mkv_parser*)parserHandle);
    if (retval == -1)
        return MKV_ERR_CORRUPTED_INDEX;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvImportIndex(FslParserHandle parserHandle, uint8* buffer, uint32 size) {
    int retval = 0;

    if (!parserHandle || !buffer || !size)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_import_index((mkv_parser*)parserHandle, buffer, size);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvExportIndex(FslParserHandle parserHandle, uint8* buffer, uint32* size) {
    int retval = 0;

    if (!parserHandle || !size)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_export_index((mkv_parser*)parserHandle, buffer, size);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvIsSeekable(FslParserHandle parserHandle, bool* seekable) {
    int retval = 0;

    if (!parserHandle || !seekable)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_is_seekable((mkv_parser*)parserHandle);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    if (!retval)
        *seekable = 0;
    else
        *seekable = 1;

    MKV_API_LOG("is seekable: %d \r\n", *seekable);
    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetNumTracks(FslParserHandle parserHandle, uint32* numTracks) {
    int retval = 0;

    if (!parserHandle || !numTracks)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_trackcount((mkv_parser*)parserHandle);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    *numTracks = retval;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetMetaData(FslParserHandle parserHandle, UserDataID userDataId,
                            UserDataFormat* userDataFormat, uint8** userData,
                            uint32* userDataLength) {
    int retval = 0;

    if (!parserHandle || !userDataFormat || !userData || !userDataLength)
        return PARSER_ERR_INVALID_PARAMETER;

    *userData = NULL;
    *userDataLength = 0;

    switch (userDataId) {
        case USER_DATA_TITLE:
        case USER_DATA_GENRE:
        case USER_DATA_TOOL:
        case USER_DATA_ARTIST:
        case USER_DATA_COPYRIGHT:
        case USER_DATA_COMMENTS:
        case USER_DATA_CREATION_DATE:
        case USER_DATA_ALBUM:
        case USER_DATA_COMPOSER:
        case USER_DATA_DIRECTOR:
        case USER_DATA_CREATOR:
        case USER_DATA_PRODUCER:
        case USER_DATA_PERFORMER:
        case USER_DATA_MOVIEWRITER:
        case USER_DATA_DESCRIPTION:
        case USER_DATA_TRACKNUMBER:
        case USER_DATA_TOTALTRACKNUMBER:
        case USER_DATA_DISCNUMBER:
        case USER_DATA_AUTHOR:
        case USER_DATA_KEYWORDS:
        case USER_DATA_ALBUMARTIST:
            *userDataFormat = USER_DATA_FORMAT_UTF8;
            retval = mkv_parser_get_userdata((mkv_parser*)parserHandle, userDataId, userData,
                                             userDataLength);
            break;
        case USER_DATA_ARTWORK:
            retval = mkv_parser_get_artwork((mkv_parser*)parserHandle, userDataFormat, userData,
                                            userDataLength);
            break;
        case USER_DATA_RATING:

            *userDataFormat = USER_DATA_FORMAT_UTF8;
            retval = mkv_parser_get_userdata((mkv_parser*)parserHandle, userDataId, userData,
                                             userDataLength);
            if (*userDataLength > 0) {
                *userDataLength = strlen((char*)userData);
                *userDataFormat = USER_DATA_FORMAT_UTF8;
            }
            break;
        case USER_DATA_CHAPTER_MENU:
            retval = matroska_get_chapter_menu(&((mkv_internal_parser*)parserHandle)->mkvctx,
                                               userData, userDataLength);
            if (0 == retval) {
                MKV_API_LOG("MKV file contains Chapter Menu \n");
            }
            break;
        default:
            userData = NULL;
            userDataLength = 0;
            break;
    }
    if (-1 == retval)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetMovieDuration(FslParserHandle parserHandle, uint64* usDuration) {
    int retval = 0;

    if (!parserHandle || !usDuration)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_movie_duration((mkv_parser*)parserHandle, usDuration);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetTrackDuration(FslParserHandle parserHandle, uint32 trackNum,
                                 uint64* usDuration) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !usDuration)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_track_duration((mkv_parser*)parserHandle, internal_track_num,
                                           usDuration);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetTrackType(FslParserHandle parserHandle, uint32 trackNum, uint32* mediaType,
                             uint32* decoderType, uint32* decoderSubtype) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !mediaType || !decoderType || !decoderSubtype)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_track_type((mkv_parser*)parserHandle, internal_track_num, mediaType,
                                       decoderType, decoderSubtype);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetLanguage(FslParserHandle parserHandle, uint32 trackNum, uint8* threeCharCode) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !threeCharCode)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_language((mkv_parser*)parserHandle, internal_track_num,
                                     (char*)threeCharCode);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetBitRate(FslParserHandle parserHandle, uint32 trackNum, uint32* bitrate) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;
    uint32 mediaType, decoderType, decoderSubtype;
    WaveFormatEx* WaveInfo = NULL;

    if (!parserHandle || !bitrate)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_bitrate((mkv_parser*)parserHandle, internal_track_num, bitrate);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_track_type((mkv_parser*)parserHandle, internal_track_num, &mediaType,
                                       &decoderType, &decoderSubtype);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    if (mediaType != MEDIA_VIDEO && mediaType != MEDIA_AUDIO) {
        *bitrate = 0;
        return PARSER_SUCCESS;
    }

    if (decoderType == AUDIO_WMA) {
        retval = mkv_parser_get_wave_format((mkv_parser*)parserHandle, internal_track_num,
                                            &WaveInfo);
        if (retval == -1)
            return PARSER_ERR_INVALID_PARAMETER;

        if (WaveInfo)
            *bitrate = WaveInfo->nAvgBytesPerSec << 3;
    }

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetTrackExtTag(FslParserHandle parserHandle, uint32 trackNum,
                               TrackExtTagList** pList) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !pList)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_ext_tag_list((mkv_parser*)parserHandle, internal_track_num, pList);

    if (retval == -1)
        return PARSER_ERR_INVALID_MEDIA;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetSampleDuration(FslParserHandle parserHandle, uint32 trackNum,
                                  uint64* usDuration) {
    int retval = 0;

    uint32 internal_track_num = trackNum + 1;
    if (!parserHandle || !usDuration)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_sample_duration((mkv_parser*)parserHandle, internal_track_num,
                                            usDuration);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetVideoFrameWidth(FslParserHandle parserHandle, uint32 trackNum, uint32* width) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !width)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_video_frame_width((mkv_parser*)parserHandle, internal_track_num, width);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetVideoFrameHeight(FslParserHandle parserHandle, uint32 trackNum, uint32* height) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !height)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_video_frame_height((mkv_parser*)parserHandle, internal_track_num,
                                               height);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

#if 0
EXTERN int32 MkvGetMaxSampleSize(FslParserHandle parserHandle, uint32 trackNum, uint32 * size)
{
    int retval=0;
    uint32 internal_track_num = trackNum + 1;

    if(!parserHandle || !size) return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_max_samplesize((mkv_parser *)parserHandle, internal_track_num, size);
    if(retval == -1) return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetVideoBitsPerPixel(FslParserHandle parserHandle, uint32 trackNum, uint32 *bitCount)
{
    int retval=0;
    uint32 internal_track_num = trackNum + 1;

    if(!parserHandle || !bitCount) return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_video_pixelbits((mkv_parser *)parserHandle, internal_track_num, bitCount);
    if(retval == -1) return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}
#endif

EXTERN int32 MkvGetAudioNumChannels(FslParserHandle parserHandle, uint32 trackNum,
                                    uint32* numchannels) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !numchannels)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_audio_channels((mkv_parser*)parserHandle, internal_track_num,
                                           numchannels);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetAudioSampleRate(FslParserHandle parserHandle, uint32 trackNum,
                                   uint32* sampleRate) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !sampleRate)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_audio_samplerate((mkv_parser*)parserHandle, internal_track_num,
                                             sampleRate);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvParserGetAudioBlockAlign(FslParserHandle parserHandle, uint32 trackNum,
                                         uint32* blockAlign) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;
    uint32 mediaType, decoderType, decoderSubtype;
    WaveFormatEx* WaveInfo = NULL;

    if (!parserHandle || !blockAlign)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_track_type((mkv_parser*)parserHandle, internal_track_num, &mediaType,
                                       &decoderType, &decoderSubtype);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    if (decoderType != AUDIO_WMA) {
        *blockAlign = 0;
        return PARSER_SUCCESS;
    }

    retval = mkv_parser_get_wave_format((mkv_parser*)parserHandle, internal_track_num, &WaveInfo);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    *blockAlign = WaveInfo->nBlockAlign;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetAudioBitsPerSample(FslParserHandle parserHandle, uint32 trackNum,
                                      uint32* bitsPerSample) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !bitsPerSample)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_audio_samplebits((mkv_parser*)parserHandle, internal_track_num,
                                             bitsPerSample);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetAudioBitsPerFrame(FslParserHandle parserHandle, uint32 trackNum,
                                     uint32* bitsPerFrame) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !bitsPerFrame)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_audio_bits_per_frame((mkv_parser*)parserHandle, internal_track_num,
                                                 bitsPerFrame);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetTextTrackWidth(FslParserHandle parserHandle, uint32 trackNum, uint32* width) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !width)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_text_width((mkv_parser*)parserHandle, internal_track_num, width);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetTextTrackHeight(FslParserHandle parserHandle, uint32 trackNum, uint32* height) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !height)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_text_height((mkv_parser*)parserHandle, internal_track_num, height);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvSeek(FslParserHandle parserHandle, uint32 trackNum, uint64* usTime, uint32 flag) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;
    if (!parserHandle || !usTime)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_track_seek((mkv_parser*)parserHandle, internal_track_num, *usTime, flag);
    if (retval == -1)
        return PARSER_SEEK_ERROR;

    retval = mkv_parser_get_track_position((mkv_parser*)parserHandle, internal_track_num, usTime);
    if (retval == -1)
        return PARSER_SEEK_ERROR;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetCodecSpecificInfo(FslParserHandle parserHandle, uint32 trackNum, uint8** data,
                                     uint32* size) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !data || !size)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_extra_data((mkv_parser*)parserHandle, internal_track_num, data, size);
    if (retval == -1)
        return PARSER_SEEK_ERROR;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetWaveFormatExInfo(FslParserHandle parserHandle, uint32 trackNum,
                                    WaveFormatEx** WaveInfo) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !WaveInfo)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_wave_format((mkv_parser*)parserHandle, internal_track_num, WaveInfo);
    if (retval == -1)
        return PARSER_SEEK_ERROR;

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetNextSample(FslParserHandle parserHandle, uint32 trackNum, uint8** sampleData,
                              void** bufferContext, uint32* sampleSize, uint64* usStartTime,
                              uint64* usDuration, uint32* flag) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !sampleData || !sampleSize || !usStartTime || !usDuration || !flag)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_next_sample((mkv_parser*)parserHandle, internal_track_num, sampleData,
                                    bufferContext, sampleSize, usStartTime, usDuration, flag);

    if (retval == PARSER_NOT_READY)
        return PARSER_NOT_READY;

    if (retval == PARSER_ERR_UNKNOWN)
        return PARSER_ERR_INVALID_PARAMETER;

    if (retval == PARSER_EOS) {
        if (sampleSize)
            *sampleSize = 0;
        return PARSER_EOS;
    }

    return PARSER_SUCCESS;
}

EXTERN int32 MkvGetSyncSample(FslParserHandle parserHandle, uint32 direction, uint32 trackNum,
                              uint8** sampleData, void** bufferContext, uint32* sampleSize,
                              uint64* usStartTime, uint64* usDuration, uint32* flag) {
    int retval = 0;
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle || !sampleData)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_sync_sample((mkv_parser*)parserHandle, direction, internal_track_num,
                                        sampleData, bufferContext, sampleSize, usStartTime,
                                        usDuration, flag);
    if (retval == PARSER_EOS || retval == PARSER_BOS) {
        if (sampleSize)
            *sampleSize = 0;
    }

    return retval;
}

EXTERN int32 MkvGetFileNextSample(FslParserHandle parserHandle, uint32* trackNum,
                                  uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                                  uint64* usStartTime, uint64* usDuration, uint32* sampleFlags) {
    int retval = 0;
    if (!parserHandle || !trackNum || !sampleBuffer || !dataSize || !usStartTime || !usDuration ||
        !sampleFlags)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_file_next_sample((mkv_parser*)parserHandle, trackNum, sampleBuffer,
                                         bufferContext, dataSize, usStartTime, usDuration,
                                         sampleFlags);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    *trackNum = *trackNum - 1;
    if (retval == PARSER_EOS) {
        if (dataSize)
            *dataSize = 0;
        return PARSER_EOS;
    }

    return retval;
}

EXTERN int32 MkvGetFileNextSyncSample(FslParserHandle parserHandle, uint32 direction,
                                      uint32* trackNum, uint8** sampleBuffer, void** bufferContext,
                                      uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                                      uint32* sampleFlags) {
    int32 retval = 0;
    if (!parserHandle || !trackNum || !sampleBuffer || !dataSize || !usStartTime || !usDuration ||
        !sampleFlags)
        return PARSER_ERR_INVALID_PARAMETER;

    retval = mkv_parser_get_file_next_sync_sample((mkv_parser*)parserHandle, direction, trackNum,
                                                  sampleBuffer, bufferContext, dataSize,
                                                  usStartTime, usDuration, sampleFlags);
    if (retval == -1)
        return PARSER_ERR_INVALID_PARAMETER;

    *trackNum = *trackNum - 1;
    if (retval == PARSER_EOS || retval == PARSER_BOS) {
        if (dataSize)
            *dataSize = 0;
    }

    return retval;
}

#define FILE_MODE_ONLY

EXTERN int32 MkvParserGetReadMode(FslParserHandle parser_handle, uint32* readMode) {
    int32 err = PARSER_SUCCESS;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser_handle;

    if (NULL == parser_handle || !readMode) {
        MKV_API_ERROR_LOG("%s: error: invalid parser handle\n", __FUNCTION__);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *readMode = iparser->mkvctx.readMode;
    if (*readMode == PARSER_READ_MODE_FILE_BASED) {
        MKV_API_LOG("MKV Parser's Read Mode is File-based \n");
    } else {
        MKV_API_LOG("MKV Parser's Read Mode is Track-based \n");
    }

    return err;
}

EXTERN int32 MkvParserSetReadMode(FslParserHandle parser_handle, uint32 readMode) {
    int32 err = PARSER_SUCCESS;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser_handle;

    if (NULL == parser_handle) {
        MKV_API_ERROR_LOG("%s: error: invalid parser handle\n", __FUNCTION__);
        return PARSER_ERR_INVALID_PARAMETER;
    }
    MKV_API_LOG("SetReadMode=%s\n",
                readMode == PARSER_READ_MODE_TRACK_BASED ? "TrackMode" : "FileMode");

    if ((readMode == PARSER_READ_MODE_TRACK_BASED) && (iparser->mkvctx.isLive))
        return PARSER_ERR_INVALID_READ_MODE;
    if (readMode == iparser->mkvctx.readMode) {
        return err;
    }

    iparser->mkvctx.readMode = readMode;

    if (readMode == PARSER_READ_MODE_FILE_BASED) {
        MKV_API_LOG("Change MKV Parser's Read Mode as File-based \n");
    } else {
        MKV_API_LOG("Change MKV Parser's Read Mode as Track-based \n");
    }

    return err;
}

EXTERN int32 MkvEnableTrack(FslParserHandle parser_handle, uint32 track_number, bool enable) {
    int32 err = PARSER_SUCCESS;
    uint32 internal_track_num = track_number + 1;
    TrackEntry* track = NULL;
    mkvStream* stream = NULL;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser_handle;
    MKVReaderContext* pctx = &iparser->mkvctx;

    track = matroska_find_track_by_num(pctx, internal_track_num);
    if (!track)
        return PARSER_ERR_INVALID_PARAMETER;

    stream = (mkvStream*)track->stream;
    if (!stream)
        return PARSER_ERR_INVALID_PARAMETER;

    stream->force_enable = enable ? 1 : 0;
    stream->track_enable = enable ? 1 : 0;

    if (!enable)
        pctx->switchTrack = TRUE;
    return err;
}

EXTERN int32 MkvGetVideoFrameRate(FslParserHandle parser_handle, uint32 track_number, uint32* rate,
                                  uint32* scale) {
    int32 err = PARSER_SUCCESS;
    uint32 internal_track_num = track_number + 1;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parser_handle;
    TrackEntry* track = NULL;
    MKVReaderContext* pctx = &iparser->mkvctx;

    if (!iparser || !rate || !scale)
        return -1;

    track = matroska_find_track_by_num(pctx, internal_track_num);
    if (!track)
        return -1;

    if (track->track_type != MATROSKA_TRACK_TYPE_VIDEO)
        return -1;

    if (!track->vinfo.frame_rate && track->default_duration)
        track->vinfo.frame_rate = 1000000000.0 / track->default_duration;
    *rate = (uint32)(track->vinfo.frame_rate * 0x400);
    *scale = 0x400;
    return err;
}

EXTERN int32 MkvGetVideoColorInfo(FslParserHandle parserHandle, uint32 trackNum, int32* primaries,
                                  int32* transfer, int32* coeff, int32* fullRange) {
    int32 err = PARSER_SUCCESS;
    uint32 internal_track_num = trackNum + 1;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parserHandle;
    MKVReaderContext* pctx = &iparser->mkvctx;

    if (!iparser || !primaries || !transfer || !coeff || !fullRange)
        return PARSER_ERR_INVALID_MEDIA;

    err = matroska_get_video_color_info(pctx, internal_track_num, primaries, transfer, coeff,
                                        fullRange);

    if (err)
        return PARSER_ERR_INVALID_MEDIA;

    return err;
}

EXTERN int32 MkvGetVideoHDRColorInfo(FslParserHandle parserHandle, uint32 trackNum,
                                     VideoHDRColorInfo* pInfo) {
    int32 err = PARSER_SUCCESS;
    uint32 internal_track_num = trackNum + 1;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parserHandle;
    MKVReaderContext* pctx = &iparser->mkvctx;

    if (!iparser || !pInfo)
        return PARSER_ERR_INVALID_MEDIA;

    err = matroska_get_video_hdr_color_info(pctx, internal_track_num, pInfo);

    if (err)
        return PARSER_ERR_INVALID_MEDIA;

    return err;
}

#ifdef SUPPORT_MKV_DRM

#define BAILWITHERROR(v) \
    {                    \
        err = (v);       \
        goto bail;       \
    }

#define DRMMSG PARSERMSG

/**
 * DRM interface.function to see whether file is protected by DRM.
 * The wrapper shall call the DRM interface right after the file header is parsed for a quick
 * decision. before doing the time-consuming task such as initialize index table.
 *
 * @param parserHandle [in] Handle of the core parser.
 * @param isProtected [out]True for protected file.
 */
int32 MkvIsProtected(FslFileHandle parserHandle, bool* isProtected) {
    int32 err = PARSER_SUCCESS;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parserHandle;
    MKVReaderContext* pctx = &iparser->mkvctx;

    if (NULL == isProtected)
        return PARSER_ERR_INVALID_PARAMETER;

    *isProtected = pctx->sctx.bHasDRMHdr;
    return err;
}

/**
 * DRM interface.function to see whether file is a rental or purchased movie.
 * This API shall be called once before playing a protected clip.
 *
 * @param parserHandle[in] Handle of the core parser.
 * @param isRental[out] True for a rental file and False for a puchase file. Reatanl file has a view
 * limit.
 * @param viewLimit[out] View limit if a rental file.
 * @param viewCount [out]Count of views played already.
 * @return
 */
int32 MkvQueryContentUsage(FslFileHandle parserHandle, bool* isRental, uint32* viewLimit,
                           uint32* viewCount) {
    int32 err = PARSER_SUCCESS;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parserHandle;
    MKVReaderContext* pctx = &iparser->mkvctx;

    *isRental = FALSE;
    *viewLimit = 0;
    *viewCount = 0;

    if (FALSE == pctx->sctx.bHasDRMHdr) /* shall not call this API */
        BAILWITHERROR(MKV_ERR_DRM_NOT_PROTECTED)

    if (pctx->bHasDrmLib) { /* init DRM playback */
        drmErrorCodes_t result;
        uint32_t drmContextLength = 0;
        uint8* drmContext;

        uint8_t rentalMessageFlag = FALSE;
        uint8_t useLimit;
        uint8_t useCount;
        int i;
        int interval_ms = 80;
        drmAPI_s* pDRMAPI = &(pctx->sDrmAPI);

        if (NULL != pctx->drmContext)
            BAILWITHERROR(MKV_ERR_DRM_PREV_PLAY_NOT_CLEAERED)

        result = pDRMAPI->drmInitSystem(NULL, &drmContextLength);
        if (DRM_SUCCESS != result) {
            DRMMSG("fail to init DRM system\n");
            goto bail;
        }

        drmContext = (uint8*)alloc_stream_buffer(
                &(pctx->bs), drmContextLength,
                NON_STREAM);  //(uint8_t *)LOCALCalloc( 1, drmContextLength );
        if (NULL == drmContext)
            return PARSER_INSUFFICIENT_MEMORY;

        pctx->drmContext = drmContext;

        result = pDRMAPI->drmInitSystem(pctx->drmContext, &drmContextLength);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 0\n",
                   pDRMAPI->drmGetLastError(drmContext));
            goto bail;
        }

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
            {
                pDRMAPI->drmSetRandomSample(drmContext);
            }
        }

        result = pDRMAPI->drmInitPlayback(drmContext, pctx->sctx.stDRM_Hdr.pdrmHdr);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 1\n",
                   pDRMAPI->drmGetLastError(drmContext));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }

        /*rental status */
        result =
                pDRMAPI->drmQueryRentalStatus(drmContext, &rentalMessageFlag, &useLimit, &useCount);
        DRMMSG("drmQueryRentalStatus return code %d, use limit %d, use count %d\n", result,
               useLimit, useCount);

        *isRental = rentalMessageFlag;
        *viewLimit = useLimit;
        *viewCount = useCount;

        if (DRM_RENTAL_EXPIRED == result) {
            DRMMSG("Screen 3 (rental expired), code %d - Step 2\n",
                   pDRMAPI->drmGetLastError(drmContext));
            *isRental = TRUE; /* When expired, rentalMessageFlag is ZERO */
            BAILWITHERROR(DRM_ERR_RENTAL_EXPIRED)
        }

        else if (DRM_NOT_AUTHORIZED == result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 2\n",
                   pDRMAPI->drmGetLastError(drmContext));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }

        else if (DRM_SUCCESS != result) {
            DRMMSG("drm:4 DRM Message 1 (generic), code %d - Screen 2\n", result);
            BAILWITHERROR(MKV_ERR_DRM_OTHERS)
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
 * @param parserHandle[in] - Handle of the core parser.
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
int32 MkvQueryOutputProtectionFlag(FslFileHandle parserHandle, uint8* cgmsaSignal,
                                   uint8* acptbSignal, uint8* digitalProtectionSignal,
                                   uint8* ictSignal) {
    int32 err = PARSER_SUCCESS;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parserHandle;
    MKVReaderContext* pctx = &iparser->mkvctx;

    *cgmsaSignal = 0;
    *acptbSignal = 0;
    *digitalProtectionSignal = 0;
    *ictSignal = 0;

    if (FALSE == pctx->sctx.bHasDRMHdr) /* shall not call this API */
        BAILWITHERROR(MKV_ERR_DRM_NOT_PROTECTED)

    if (pctx->bHasDrmLib) {
        drmErrorCodes_t result;
        uint8_t* context = pctx->drmContext;
        int i;
        int interval_ms = 80;
        drmAPI_s* pDRMAPI = &(pctx->sDrmAPI);

        if (NULL == context)
            BAILWITHERROR(MKV_ERR_DRM_INVALID_CONTEXT)

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
            pDRMAPI->drmSetRandomSample(context);
        }

        result = pDRMAPI->drmQueryCgmsa(context, cgmsaSignal);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 4\n",
                   pDRMAPI->drmGetLastError(context));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }
        DRMMSG("%d cgmsa signal\n", *cgmsaSignal);

        result = pDRMAPI->drmQueryAcptb(context, acptbSignal);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 5\n",
                   pDRMAPI->drmGetLastError(context));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }
        DRMMSG("%d acptb signal\n", *acptbSignal);

        result = pDRMAPI->drmQueryDigitalProtection(context, digitalProtectionSignal);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 6\n",
                   pDRMAPI->drmGetLastError(context));
            BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
        }
        DRMMSG("%d digital protection signal\n", *digitalProtectionSignal);

        result = pDRMAPI->drmQueryIct(context, ictSignal);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 7\n",
                   pDRMAPI->drmGetLastError(context));
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
 * @param parserHandle [in] Handle of the core parser.
 * @return
 */
int32 MkvCommitPlayback(FslFileHandle parserHandle) {
    int32 err = PARSER_SUCCESS;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parserHandle;
    MKVReaderContext* pctx = &iparser->mkvctx;

    if (FALSE == pctx->sctx.bHasDRMHdr) /* shall not call this API */
        BAILWITHERROR(MKV_ERR_DRM_NOT_PROTECTED)

    if (pctx->bHasDrmLib) {
        drmErrorCodes_t result;
        uint8_t* context = pctx->drmContext;
        drmAPI_s* pDRMAPI = &(pctx->sDrmAPI);

        if (NULL == context)
            BAILWITHERROR(MKV_ERR_DRM_INVALID_CONTEXT)

        result = pDRMAPI->drmCommitPlayback(context);
        if (DRM_SUCCESS != result) {
            DRMMSG("Screen 2 (not authorized), code %d - Step 7\n",
                   pDRMAPI->drmGetLastError(context));
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
 * Otherwise error "MKV_ERR_DRM_PREV_PLAY_NOT_CLEAERED" on next playback.
 *
 * @param parserHandle [in] Handle of the core parser.
 * @return
 */
int32 MkvFinalizePlayback(FslFileHandle parserHandle) {
    int32 err = PARSER_SUCCESS;
    mkv_internal_parser* iparser = (mkv_internal_parser*)parserHandle;
    MKVReaderContext* pctx = &iparser->mkvctx;

    if (FALSE == pctx->sctx.bHasDRMHdr) /* shall not call this API */
        BAILWITHERROR(MKV_ERR_DRM_NOT_PROTECTED)

    if (pctx->bHasDrmLib) {
        uint8_t* context = pctx->drmContext;
        drmAPI_s* pDRMAPI = &(pctx->sDrmAPI);

        if (NULL == context)
            BAILWITHERROR(MKV_ERR_DRM_INVALID_CONTEXT)

        pDRMAPI->drmFinalizePlayback(context);

        free_stream_buffer(&(pctx->bs), (char*)context, NON_STREAM);
        pctx->drmContext = NULL;
    } else {
        BAILWITHERROR(DRM_ERR_NOT_AUTHORIZED_USER)
    }

bail:
    return err;
}
#endif  // SUPPORT_MKV_DRM

int32 MkvFlushTrack(FslParserHandle parserHandle, uint32 trackNum) {
    uint32 internal_track_num = trackNum + 1;

    if (!parserHandle)
        return PARSER_ERR_INVALID_PARAMETER;

    if (0 != mkv_parser_clear_track(parserHandle, internal_track_num))
        return PARSER_ERR_UNKNOWN;

    MKV_API_LOG("MkvFlushTrack success\n");

    return PARSER_SUCCESS;
}

/***************************************************************************************
 *                 DLL entry point
 ***************************************************************************************/
EXTERN int32 FslParserQueryInterface(uint32 id, void** func) {
    int32 err = PARSER_SUCCESS;

    if (!func)
        return PARSER_ERR_INVALID_PARAMETER;

    *func = NULL;

    switch (id) {
        /* creation & deletion */
        case PARSER_API_GET_VERSION_INFO:
            *func = MkvParserVersionInfo;
            break;

        case PARSER_API_CREATE_PARSER:
            *func = MkvCreateParser;
            break;
        case PARSER_API_CREATE_PARSER2:
            *func = MkvCreateParser2;
            break;
        case PARSER_API_DELETE_PARSER:
            *func = MkvDeleteParser;
            break;

        /* movie properties */
        case PARSER_API_IS_MOVIE_SEEKABLE:
            *func = MkvIsSeekable;
            break;

        case PARSER_API_GET_MOVIE_DURATION:
            *func = MkvGetMovieDuration;
            break;

        case PARSER_API_GET_META_DATA:
            *func = MkvGetMetaData;
            break;

        case PARSER_API_GET_NUM_TRACKS:
            *func = MkvGetNumTracks;
            break;

        /* generic track properties */
        case PARSER_API_GET_TRACK_TYPE:
            *func = MkvGetTrackType;
            break;

        case PARSER_API_GET_DECODER_SPECIFIC_INFO:
            *func = MkvGetCodecSpecificInfo;
            break;

        case PARSER_API_GET_LANGUAGE:
            *func = MkvGetLanguage;
            break;

        case PARSER_API_GET_TRACK_DURATION:
            *func = MkvGetTrackDuration;
            break;

        case PARSER_API_GET_BITRATE:
            *func = MkvGetBitRate;
            break;

        case PARSER_API_GET_TRACK_EXT_TAG:
            *func = MkvGetTrackExtTag;
            break;

        /* video properties */
        case PARSER_API_GET_VIDEO_FRAME_WIDTH:
            *func = MkvGetVideoFrameWidth;
            break;

        case PARSER_API_GET_VIDEO_FRAME_HEIGHT:
            *func = MkvGetVideoFrameHeight;
            break;

        case PARSER_API_GET_VIDEO_FRAME_RATE:
            *func = MkvGetVideoFrameRate;
            break;

        case PARSER_API_GET_VIDEO_COLOR_INFO:
            *func = MkvGetVideoColorInfo;
            break;

        case PARSER_API_GET_VIDEO_HDR_COLOR_INFO:
            *func = MkvGetVideoHDRColorInfo;
            break;

        /* audio properties */
        case PARSER_API_GET_AUDIO_NUM_CHANNELS:
            *func = MkvGetAudioNumChannels;
            break;

        case PARSER_API_GET_AUDIO_SAMPLE_RATE:
            *func = MkvGetAudioSampleRate;
            break;

        case PARSER_API_GET_AUDIO_BLOCK_ALIGN:
            *func = MkvParserGetAudioBlockAlign;
            break;

        case PARSER_API_GET_AUDIO_BITS_PER_SAMPLE:
            *func = MkvGetAudioBitsPerSample;
            break;

        case PARSER_API_GET_AUDIO_BITS_PER_FRAME:
            *func = MkvGetAudioBitsPerFrame;
            break;
        /* text/subtitle properties */
        case PARSER_API_GET_TEXT_TRACK_WIDTH:
            *func = MkvGetTextTrackWidth;
            break;

        case PARSER_API_GET_TEXT_TRACK_HEIGHT:
            *func = MkvGetTextTrackHeight;
            break;

        /* sample reading, seek & trick mode */
        case PARSER_API_GET_READ_MODE:
            *func = MkvParserGetReadMode;
            break;

        case PARSER_API_SET_READ_MODE:
            *func = MkvParserSetReadMode;
            break;

        case PARSER_API_ENABLE_TRACK:
            *func = MkvEnableTrack;
            break;

        case PARSER_API_GET_NEXT_SAMPLE:
            *func = MkvGetNextSample;
            break;

        case PARSER_API_GET_FILE_NEXT_SAMPLE:
            *func = MkvGetFileNextSample;
            break;

        case PARSER_API_GET_NEXT_SYNC_SAMPLE:
            *func = MkvGetSyncSample;
            break;

        case PARSER_API_GET_FILE_NEXT_SYNC_SAMPLE:
            *func = MkvGetFileNextSyncSample;
            break;

        case PARSER_API_SEEK:
            *func = MkvSeek;
            break;
#ifdef SUPPORT_INDEX_API
        case PARSER_API_INITIALIZE_INDEX:
            *func = MkvInitializeIndex;
            break;
        case PARSER_API_IMPORT_INDEX:
            *func = MkvImportIndex;
            break;
        case PARSER_API_EXPORT_INDEX:
            *func = MkvExportIndex;
            break;
#endif

#ifdef SUPPORT_MKV_DRM
        case PARSER_API_IS_DRM_PROTECTED:
            *func = MkvIsProtected;
            break;
        case PARSER_API_QUERY_CONTENT_USAGE:
            *func = MkvQueryContentUsage;
            break;
        case PARSER_API_QUERY_OUTPUT_PROTECTION_FLAG:
            *func = MkvQueryOutputProtectionFlag;
            break;
        case PARSER_API_COMMIT_PLAYBACK:
            *func = MkvCommitPlayback;
            break;
        case PARSER_API_FINAL_PLAYBACK:
            *func = MkvFinalizePlayback;
            break;
#endif  // SUPPORT_MKV_DRM
        case PARSER_API_FLUSH_TRACK:
            *func = MkvFlushTrack;
        default:
            break; /* no support for other API */
    }

    return err;
}
