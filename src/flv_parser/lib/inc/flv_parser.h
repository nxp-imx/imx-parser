/*
***********************************************************************
* Copyright (c) 2010-2014, Freescale Semiconductor Inc.,
* Copyright 2023, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef _FSL_FLV_PARSER_INCLUDE_
#define _FSL_FLV_PARSER_INCLUDE_

#include "flv_parser_api.h"
#include "fsl_parser.h"
#include "fsl_types.h"

#define FLV_AUDIO_CHANNEL_MASK 0x01
#define FLV_AUDIO_SAMPLE_SIZE_MASK 0x02
#define FLV_AUDIO_SAMPLE_RATE_MASK 0x0C
#define FLV_AUDIO_CODEC_ID_MASK 0xF0
#define FLV_VIDEO_FRAME_TYPE_MASK 0xF0
#define FLV_VIDEO_CODEC_ID_MASK 0x0F

#define FLV_AUDIO_SAMPLES_SIZE_OFFSET 1
#define FLV_AUDIO_SAMPLERATE_OFFSET 2
#define FLV_VIDEO_FRAME_TYPE_OFFSET 4
#define FLV_AUDIO_CODECID_OFFSET 4

#define AMF_END_OF_OBJECT 0x09

enum {
    FLV_FRAME_KEY = 1 << FLV_VIDEO_FRAME_TYPE_OFFSET,
    FLV_FRAME_INTER = 2 << FLV_VIDEO_FRAME_TYPE_OFFSET,
    FLV_FRAME_DISP_INTER = 3 << FLV_VIDEO_FRAME_TYPE_OFFSET,
};

enum {
    FLV_TAG_TYPE_AUDIO = 0x08, /* audio */
    FLV_TAG_TYPE_VIDEO = 0x09, /* video */
    FLV_TAG_TYPE_META = 0x12,  /* meta */
};

enum {
    FLV_SAMPLESSIZE_8BIT = 0,                                   /* sample size of 8 bit */
    FLV_SAMPLESSIZE_16BIT = 1 << FLV_AUDIO_SAMPLES_SIZE_OFFSET, /* sample size of 16 bit */
};

enum {
    FLV_MONO = 0,   /* mono */
    FLV_STEREO = 1, /* sterio */
};

enum {
    FLV_SAMPLERATE_SPECIAL = 0, /* signify 5512Hz and 8000Hz in case of NELLYMOSER */
    FLV_SAMPLERATE_11025HZ = 1 << FLV_AUDIO_SAMPLERATE_OFFSET, /* 11025HZ */
    FLV_SAMPLERATE_22050HZ = 2 << FLV_AUDIO_SAMPLERATE_OFFSET, /* 22050HZ */
    FLV_SAMPLERATE_44100HZ = 3 << FLV_AUDIO_SAMPLERATE_OFFSET, /* 44100HZ */
};

enum {
    FLV_HEADER_FLAG_HASVIDEO = 1, /* has video */
    FLV_HEADER_FLAG_HASAUDIO = 4, /* has audio */
};

#define MAX_KEY_LEN 256

typedef enum {
    AMF_DATA_TYPE_NUMBER = 0x00,
    AMF_DATA_TYPE_BOOL = 0x01,
    AMF_DATA_TYPE_STRING = 0x02,
    AMF_DATA_TYPE_OBJECT = 0x03,
    AMF_DATA_TYPE_NULL = 0x05,
    AMF_DATA_TYPE_UNDEFINED = 0x06,
    AMF_DATA_TYPE_REFERENCE = 0x07,
    AMF_DATA_TYPE_MIXEDARRAY = 0x08,
    AMF_DATA_TYPE_ENDOFOBJECT = 0x09,
    AMF_DATA_TYPE_ARRAY = 0x0a,
    AMF_DATA_TYPE_DATE = 0x0b,
    AMF_DATA_TYPE_UNSUPPORTED = 0x0d,
} AMF_Data_Type;

typedef struct {
    uint32 preSize;
    uint32 type;       //!< Tag type 8:audio 9:vido 18:script
    uint32 dataSize;   //!< Size of FLV data for this tag
    uint32 timestamp;  //!< Time stamp in mili second when this tag should be decoded
} flv_tag_t;

typedef struct {
    uint64 offset;
    uint32 timestamp;
} flv_index_entry_t;

typedef struct {
    uint32 entries_in_use;
    uint32 entries_of_table;
    uint32 range_time;
    flv_index_entry_t* index;
    uint32 indexadjusted;
} flv_index_table_t;

typedef struct {
    uint32 hasVideo;
    uint32 hasAudio;
    uint32 hasMetadata;
    uint32 hasCuePoints;
    uint32 hasKeyframes;
    uint32 canSeekToEnd;

    uint32 audiocodecid;
    uint32 audiosamplerate;
    uint32 audiodatarate;
    uint32 audiosamplesize;
    uint32 audiodelay;
    uint32 stereo;

    uint32 videocodecid;
    uint32 framerate;
    uint32 videodatarate;
    uint32 height;
    uint32 width;

    uint32 filesize;
    uint32 datasize;
    uint32 videosize;
    uint32 audiosize;

    uint32 lasttimestamp;
    uint32 lastvideoframetimestamp;
    uint32 lastkeyframetimestamp;
    uint32 lastkeyframelocation;
    uint32 keyframes;
    uint32 duration;

    uint32 hasLastSecond;
    uint32 lastsecondTagCount;
    uint32 onlastkeyframelength;
    uint32 lastkeyframesize;
    uint32 hasLastKeyframe;
    int8 keyname[MAX_KEY_LEN];
} flv_metadata_t;

typedef enum {
    FLV_AUDIO_SAMPLE = 0,
    FLV_VIDEO_SAMPLE = 1
} FLV_SAMPLE_TYPE;

typedef void* flv_parser_handle_t;

typedef struct {
    uint32 size;
    uint8* data;
} h264_specific_config;

typedef struct {
    uint32 codec;
    uint32 width;
    uint32 height;
    h264_specific_config h264_codec_data;
} flv_video_info_t;

typedef struct {
    uint32 size;
    uint8* data;
} aac_specific_config;

typedef struct {
    uint32 format;
    uint32 samplingRate;
    uint32 sampleSize;
    uint32 channel;
    aac_specific_config aac_specific_config;
} flv_audio_info_t;

typedef struct {
    uint32 duration;              //!< in mili second
    uint32 seekable;              //!< seekable
    uint32 streamingable;         //!< streamingable
    int32 audio_present;          //!< true if audio tags are present
    int32 video_present;          //!< true if video tags are present
    flv_video_info_t video_info;  //!< video information
    flv_audio_info_t audio_info;  //!< auido information
} flv_stream_info_t;

#define MAX_VIDEO_INFO_LENGTH 256

typedef struct {
    uint8* pBuf;
    int32 len;
    int32 count;
    int32 read;
    int32 write;
} recovery_buf_t;

typedef struct {
    flv_metadata_t metadata;
    flv_stream_info_t stream_info;
    bool isLive;
    uint32 flags;

    FslFileHandle fileHandle;
    void* appContext;
    FslFileStream fileStream;

    // file_stream_t* p_flv_stream;        //!< Input file stream
    // file_stream_t* p_flv_audio_stream;  //!< audio track stream
    // file_stream_t* p_flv_video_stream;  //!< video track stream

    ParserMemoryOps memOps;
    ParserOutputBufferOps outputOps;

    int64 offset;
    uint64 filesize;
    int64 body_offset;             //!< Absolute offset of this tag from begining of the file.
    flv_index_table_t vids_index;  //!

    int32 audio_found;  //!
    int32 video_found;  //!
    bool audio_enabled;
    bool video_enabled;

    uint32 tag_cnt;              //!
    uint64 vids_chunk_pos;       //!< The end of last video sample
    uint32 vids_timestamp;       //!
    uint32 auds_timestamp;       //!
    uint32 vids_timestamp_base;  //!
    uint32 auds_timestamp_base;  //!
    uint32 timestamp_base;
    uint32 vids_first_sample;  //!
    uint32 vids_first_syncsample;
    uint32 auds_first_sample;  //!
    uint64 auds_chunk_pos;     //!< The end of last audio sample
    uint32 nal_length_size;
    uint32 stream_corrupt;

    uint8* p_HistoryBuf;
    uint32 historyBufLen;
    uint32 historyFlag;
    uint32 historyBufOffset;
    uint32 historyBufType;
    uint64 historyTime;

    uint8 video_infobuffer[MAX_VIDEO_INFO_LENGTH];
    uint32 video_infolength;
    bool video_info_sent;

    bool isLastSyncFinished;

    recovery_buf_t recovery_buf;

} flv_parser_t;

#define LocalCalloc(number, size) p_flv_parser->memOps.Calloc(number, size)
#define LocalMalloc(size) p_flv_parser->memOps.Malloc(size)
#define LocalFree(MemoryBlock) p_flv_parser->memOps.Free(MemoryBlock)
#define LocalReAlloc(MemoryBlock, size) p_flv_parser->memOps.ReAlloc(MemoryBlock, size)

#define MIN_BYTES_FOR_FLV_PARSER 4096

/*
typedef enum
{
} flv_parser_result_t;
*/

enum {
    FLV_CODECID_H263 = 2,
    FLV_CODECID_SCREEN = 3,
    FLV_CODECID_VP6 = 4,
    FLV_CODECID_VP6A = 5,
    FLV_CODECID_SCREEN2 = 6,
    FLV_CODECID_H264 = 7,
};

enum {
    FLV_CODECID_PCM = 0,
    FLV_CODECID_ADPCM = 1,
    FLV_CODECID_MP3 = 2,
    FLV_CODECID_PCM_LE = 3,
    FLV_CODECID_NELLYMOSER_8KHZ_MONO = 5,
    FLV_CODECID_NELLYMOSER = 6,
    FLV_CODECID_AAC = 10,
    FLV_CODECID_SPEEX = 11
};

// #define DEBUG
#ifdef ANDROID
#include "android/log.h"
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "FLV PARSER", __VA_ARGS__)
#ifdef DEBUG
#define msg_dbg printf
#else
#define msg_dbg(...)
#endif

#else
#ifdef DEBUG
#define msg_dbg msg_print
#else
#define msg_dbg(...)
#endif
#endif

/**
 *
 * @param p_handle
 * @param stream
 * @param p_callback
 *
 * @return
 */

uint32 flv_parser_convertaudiotype(uint32 audioformat, uint32* pSubType);
uint32 flv_parser_convertvideotype(uint32 videoformat, uint32* pSubType);

FLVPARSER_ERR_CODE flv_parser_open(FslParserHandle* parserHandle, uint32 flags,
                                   FslFileStream* p_stream, ParserMemoryOps* pMemOps,
                                   ParserOutputBufferOps* pOutputOps, void* appContext);

/**
 *
 * @param flv_handle
 */
FLVPARSER_ERR_CODE flv_parser_close(flv_parser_t* p_flv_parser);

/**
 *
 * @param flv_handle
 * @param p_stream_info
 *
 * @return
 */
FLVPARSER_ERR_CODE flv_parser_get_stream_info(flv_parser_t* p_flv_parser,
                                              flv_stream_info_t* p_stream_info);

FLVPARSER_ERR_CODE flv_parser_get_next_sample(flv_parser_t* p_flv_parser, int stream_type,
                                              uint8* p_data, uint32 capacity, uint32* p_size,
                                              uint32* p_timestamp, uint32* p_sync_flag);

FLVPARSER_ERR_CODE flv_parser_get_current_position(flv_parser_t* p_flv_parser, int stream_type,
                                                   uint64* p_timestamp);

FLVPARSER_ERR_CODE flv_parser_get_file_next_sample(flv_parser_t* p_flv_parser, int* stream_type,
                                                   uint8** p_data, void** p_BufContext,
                                                   uint32* p_size, uint64* p_timestamp,
                                                   uint32* p_sync_flag);
/**
 *
 * @param flv_handle
 *
 * @return
 */
FLVPARSER_ERR_CODE flv_parser_seek(flv_parser_t* p_flv_parser, uint32 timestamp, uint32 flag);

FLVPARSER_ERR_CODE flv_parser_adjust_index(flv_parser_t* p_flv_parser, uint64 offset,
                                                  uint32 timestamp);

FLVPARSER_ERR_CODE flv_seek_sync_point(flv_parser_t* p_flv_parser, uint32* pTimeStamp,
                                              uint64 lastOffset, bool* pIsSyncFound);

FLVPARSER_ERR_CODE flv_parser_search_tag(flv_parser_t* p_flv_parser, FslFileStream* s,
                                                flv_tag_t* p_tag, uint64 lastOffset,
                                                bool* pSyncFound);

FLVPARSER_ERR_CODE flv_parser_search_tag_file_mode(flv_parser_t* p_flv_parser,
                                                          flv_tag_t* p_tag);

FLVPARSER_ERR_CODE flv_parser_seekduration(flv_parser_t* p_flv_parser, uint32* pDuration);

FLVPARSER_ERR_CODE flv_seek_video_key_frame(flv_parser_t* p_flv_parser, uint32* pTimeStamp,
                                                   uint64 lastOffset, bool* pIsSyncFound);

#endif /* _FSL_FLV_PARSER_INCLUDE_ */
