/*
***********************************************************************
* Copyright (c) 2010-2014, Freescale Semiconductor Inc.
* Copyright 2017, 2023-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*
*/
#define LOG_TAG "fsl_flv_parser"
// #include <utils/Log.h>
#include <float.h>
#include <math.h>

#ifdef _MSC_VER
#include <math.h>
#endif

#include <stdarg.h>
#include <string.h>

#include "flv_parser.h"

#ifdef WIN32
#define UINT64_1 1ui64
#else
#define UINT64_1 1ULL
#endif

#define FLV_FILE_HEADER_SIZE 9
#define FLV_TAG_SIZE 11
#define FLV_TAG_HEADER_SIZE 15

#define FLV_INDEX_COUNT 0x10000

#define AVC_SEQUENCE_HEADER 0
#define AVC_NALU 1
#define AVC_END_OF_SEQUENCE 2

#define AAC_SEQUENCE_HEADER 0
#define AAC_RAW_DATA 1

// check 200 tags to read stream info
#define MAX_SEARCH_TAG_NUM 200

#define FSL_AV_TIME_BASE 1000

#define SEARCH_TAG_RANGE 1024 * 10

#define RECOVER_BUF_SIZE 1024 * 300

#define BUF_PAD_SIZE 8

typedef struct __AVCDecoderConfigurationRecord {
    uint8 configurationVersion;
    uint8 AVCProfileIndication;
    uint8 profile_compatibility;
    uint8 AVCLevelIndication;
    uint8 lengthSizeMinusOne;
    uint8 numOfSequenceParameterSets;
} AVCDecoderConfigurationRecord;

typedef struct mp4AudioSpecificConfig {
    /* Audio Specific Info */
    uint8 objectTypeIndex;
    uint8 samplingFrequencyIndex;
    uint32 samplingFrequency;
    uint8 channelsConfiguration;
    /* GA Specific Info */
    uint8 frameLengthFlag;
    uint8 dependsOnCoreCoder;
    uint16 coreCoderDelay;
    uint8 layerNr;
    uint32 numOfSubFrame;
    uint32 layer_length;
    uint8 extensionFlag;
    uint8 aacSectionDataResilienceFlag;
    uint8 aacScalefactorDataResilienceFlag;
    uint8 aacSpectralDataResilienceFlag;
    uint8 epConfig;
    int8 sbr_present_flag;
    int8 forceUpSampling;
} mp4AudioSpecificConfig;

#ifndef SANITY_CHECK
#define SANITY_CHECK()                                                        \
    do {                                                                      \
        if (p_flv_handle == NULL || p_stream == NULL || p_callback == NULL || \
            p_stream->pf_fopen == NULL || p_stream->pf_fread == NULL ||       \
            p_stream->pf_fseek == NULL || p_stream->pf_ftell == NULL ||       \
            p_stream->pf_fsize == NULL || p_stream->pf_fclose == NULL ||      \
            p_callback->pf_malloc == NULL || p_callback->pf_free == NULL) {   \
            result = PARSER_ERR_INVALID_PARAMETER;                            \
            goto bail;                                                        \
        }                                                                     \
        if (p_stream->b_streaming == 1) {                                     \
            if (p_stream->pf_fcheckavailablebytes == NULL) {                  \
                result = PARSER_ERR_INVALID_PARAMETER;                        \
                goto bail;                                                    \
            }                                                                 \
        }                                                                     \
        if (p_stream->file_handle == NULL && p_stream->file_path == NULL) {   \
            result = PARSER_ERR_INVALID_PARAMETER;                            \
            goto bail;                                                        \
        }                                                                     \
    } while (0)
#endif

#ifndef CLEANUP
#define CLEANUP()                                                          \
    do {                                                                   \
        if (p_flv_parser != NULL && p_flv_parser->memOps.Free != NULL) {   \
            if (p_flv_parser->vids_index.index != NULL) {                  \
                p_flv_parser->memOps.Free(p_flv_parser->vids_index.index); \
            }                                                              \
            p_flv_parser->memOps.Free(p_flv_parser);                       \
            p_flv_parser = NULL;                                           \
        }                                                                  \
    } while (0);
#endif

#define CheckForBail(func)              \
    do {                                \
        result = func;                  \
        if (result != PARSER_SUCCESS) { \
            dump_result_info(result);   \
            goto bail;                  \
        }                               \
    } while (0)

#define GetByte(a, data)     \
    do {                     \
        a = (uint32)data[0]; \
        data += 1;           \
    } while (0)

#define GetBe16(a, data)                                      \
    do {                                                      \
        a = ((((uint32)data[1])) | (((uint32)data[0]) << 8)); \
        data += 2;                                            \
    } while (0)

#define GetLe16(a, data)                                      \
    do {                                                      \
        a = ((((uint32)data[1] << 8)) | (((uint32)data[0]))); \
        data += 2;                                            \
    } while (0)

#define GetBe24(a, data)                                                                \
    do {                                                                                \
        a = (((uint32)data[2]) | (((uint32)data[1]) << 8) | (((uint32)data[0]) << 16)); \
        data += 3;                                                                      \
    } while (0)

#define GetBe32(a, data)                                                                \
    do {                                                                                \
        a = (((uint32)data[3]) | (((uint32)data[2]) << 8) | (((uint32)data[1]) << 16) | \
             (((uint32)data[0]) << 24));                                                \
        data += 4;                                                                      \
    } while (0)

/* swap 16 bits integers */
#define Swap_Uint16(x) ((uint16)((((x) & 0x00FFU) << 8) | (((x) & 0xFF00U) >> 8)))

typedef struct {
    FLVPARSER_ERR_CODE result;
    const char* result_string;
} result_info_t;

typedef struct {
    uint8* rdbfr; /* bit input */
    uint8* rdptr;
    uint32 incnt;
    uint32 bitcnt;
} BitReader;

result_info_t info_table[] = {{PARSER_NEED_MORE_DATA, "Need More data(streaming mode)\n"},
                              {PARSER_EOS, "End of stream\n"},
                              {PARSER_ERR_INVALID_PARAMETER, "Bad parameter\n"},
                              {PARSER_INSUFFICIENT_MEMORY, "Can not allocate memory\n"},
                              {PARSER_NOT_IMPLEMENTED, "This function not be Implemented yet\n"},
                              {PARSER_ERR_INVALID_MEDIA, "Corrupt stream\n"},
                              {PARSER_FILE_OPEN_ERROR, "Open file failed\n"},
                              {PARSER_READ_ERROR, "Read file failed\n"},
                              {PARSER_SEEK_ERROR, "Seek file failed\n"},
                              {PARSER_ERR_UNKNOWN, "Undefined error\n"}};

static FLVPARSER_ERR_CODE flv_parser_read_tag(flv_parser_t* p_flv_parser, FslFileStream* s,
                                              flv_tag_t* p_tag, uint8* buffer);
static uint32 flv_parser_update_index_table(flv_index_table_t* t, uint64 offset, uint32 timestamp);
static int32 amf_parse_object(flv_parser_t* p_flv_parser, const int8* key, uint64 max_pos,
                              int32 depth);

static FLVPARSER_ERR_CODE flv_parser_find_audio_tag_after_offset(flv_parser_t* p_flv_parser,
                                                                 uint64 offset);
static FLVPARSER_ERR_CODE flv_parser_find_video_tag_after_offset(flv_parser_t* p_flv_parser,
                                                                 uint64 offset);

static FLVPARSER_ERR_CODE flv_parser_rbf_init(flv_parser_t* p_flv_parser, uint32 size);
static FLVPARSER_ERR_CODE flv_parser_rbf_deinit(flv_parser_t* p_flv_parser);
static FLVPARSER_ERR_CODE flv_parser_rbf_write_from_stream(flv_parser_t* p_flv_parser, uint32 size);
static FLVPARSER_ERR_CODE flv_parser_rbf_write_from_data(flv_parser_t* p_flv_parser, uint8* pData,
                                                         uint32 size);
static FLVPARSER_ERR_CODE flv_parser_rbf_read(flv_parser_t* p_flv_parser, uint8* pData,
                                              uint32 size);
static FLVPARSER_ERR_CODE flv_parser_rbf_peek(flv_parser_t* p_flv_parser, uint8* pData,
                                              uint32 size);
static FLVPARSER_ERR_CODE flv_parser_rbf_search_tag(flv_parser_t* p_flv_parser, flv_tag_t* p_tag,
                                                    uint8* found);
static FLVPARSER_ERR_CODE flv_parser_rbf_fskip(flv_parser_t* p_flv_parser, uint32 size);

static int flv_parser_is_possible_tag(flv_parser_t* p_flv_parser, uint8* pHeader, flv_tag_t* p_tag);

typedef struct __bit_buffer {
    uint8* start;
    uint32 size;
    uint8* current;
    uint8 read_bits;
} bit_buffer;
__attribute__((unused))
static void msg_print(const char* fmt, ...) {
    va_list params;
    va_start(params, fmt);
    vfprintf(stderr, fmt, params);
    va_end(params);
    fprintf(stderr, "\n");
}

static void initbuffer(uint8* buffer, BitReader* bitRd) {
    bitRd->rdptr = buffer;
    bitRd->rdbfr = buffer;
    bitRd->incnt = 0;
    bitRd->bitcnt = 0;
}

static uint32 showbits(BitReader* bitRd, uint32 n) {
    uint8* p = bitRd->rdptr;
    uint32 a = 0;
    uint32 c = bitRd->incnt;
    /* load in big-Endian order */
    a = (uint32)(p[0] << 24) + (p[1] << 16) + (p[2] << 8) + (p[3]);
    return (a << c) >> (32 - n);
}

static void flushbits(BitReader* bitRd, uint32 n) {
    bitRd->incnt += n;
    bitRd->bitcnt += n;
    bitRd->rdptr += (bitRd->incnt >> 3);
    bitRd->incnt &= 0x07;
}

static uint32 getbits(BitReader* bitRd, uint32 n) {
    uint32 val = showbits(bitRd, n);
    flushbits(bitRd, n);
    return val;
}

static uint32 getbit1(BitReader* bitRd) {
    return getbits(bitRd, 1);
}
__attribute__((unused))
static void byte_align(BitReader* bitRd) {
    if (bitRd->incnt) {
        bitRd->bitcnt += (8 - (bitRd->incnt & 0x07));
        bitRd->incnt = 0;
        bitRd->rdptr += 1;
    }
    return;
}

static uint64 UExpGolombRead(BitReader* bitRd) {
    int32 n = -1;
    char b = 0;
    for (b = 0; !b; n++) {
        b = (char)getbits(bitRd, 1);
    }
    return (getbits(bitRd, n) + ((UINT64_1) << n) - 1);
}
__attribute__((unused))
static int64 SExpGolombRead(BitReader* bitRd) {
    uint64 k = UExpGolombRead(bitRd);
    return ((k & 1) ? 1 : -1) * ((k + 1) >> 1);
}

/**
    bit buffer handling
*/

void skip_bits(bit_buffer* bitbuf, uint32 nbits) {
    bitbuf->current = bitbuf->current + ((nbits + bitbuf->read_bits) / 8);
    bitbuf->read_bits = (uint8)((bitbuf->read_bits + nbits) % 8);
}

uint8 get_bit(bit_buffer* bitbuf) {
    uint8 ret = (*(bitbuf->current) >> (7 - bitbuf->read_bits)) & 0x1;
    if (bitbuf->read_bits == 7) {
        bitbuf->read_bits = 0;
        bitbuf->current++;
    } else {
        bitbuf->read_bits++;
    }
    return ret;
}

uint32 get_bits(bit_buffer* bitbuf, uint32 nbits) {
    uint32 i, ret;
    ret = 0;
    for (i = 0; i < nbits; i++) {
        ret = (ret << 1) + get_bit(bitbuf);
    }
    return ret;
}

uint32 exp_golomb_ue(bit_buffer* bitbuf) {
    uint8 bit, significant_bits;
    significant_bits = 0;
    bit = get_bit(bitbuf);
    while (bit == 0) {
        significant_bits++;
        bit = get_bit(bitbuf);
    }
    return (1 << significant_bits) + get_bits(bitbuf, significant_bits) - 1;
}

int32 exp_golomb_se(bit_buffer* bitbuf) {
    int32 ret = 0;
    ret = exp_golomb_ue(bitbuf);
    if ((ret & 0x1) == 0) {
        return -(ret >> 1);
    } else {
        return (ret + 1) >> 1;
    }
}

/* Returns the sample rate based on the sample rate index */
static uint32 get_sample_rate(const uint32 sr_index) {
    static uint32 sample_rates[] = {96000, 88200, 64000, 48000, 44100, 32000,
                                    24000, 22050, 16000, 12000, 11025, 8000};

    if (sr_index < 12) {
        return sample_rates[sr_index];
    }

    return 0;
}

void get_aac_specific_config(uint8* decoder_info, mp4AudioSpecificConfig* mp4ASC) {
    BitReader bitRd;
    initbuffer(decoder_info, &bitRd);
    mp4ASC->sbr_present_flag = -1;

    /* Get the audio object type */
    mp4ASC->objectTypeIndex = (uint8)getbits(&bitRd, 5);
    if (31 == mp4ASC->objectTypeIndex) {
        mp4ASC->objectTypeIndex = (uint8)(32 + getbits(&bitRd, 6));
    }

    /* Get the sampling frequency index */
    mp4ASC->samplingFrequencyIndex = (uint8)getbits(&bitRd, 4);

    if (0xf == mp4ASC->samplingFrequencyIndex) {
        mp4ASC->samplingFrequency = getbits(&bitRd, 24);
    } else {
        mp4ASC->samplingFrequency = get_sample_rate(mp4ASC->samplingFrequencyIndex);
    }
    /* Get the number of channels */
    mp4ASC->channelsConfiguration = (uint8)getbits(&bitRd, 4);

    if (mp4ASC->objectTypeIndex == 5) {
        mp4ASC->sbr_present_flag = 1;
        mp4ASC->samplingFrequencyIndex = (uint8)getbits(&bitRd, 4);
        if (mp4ASC->samplingFrequencyIndex == 15) {
            mp4ASC->samplingFrequency = getbits(&bitRd, 24);
        } else {
            mp4ASC->samplingFrequency = get_sample_rate(mp4ASC->samplingFrequencyIndex);
        }
        mp4ASC->objectTypeIndex = (uint8)getbits(&bitRd, 5);
    }

    /* Get GASpecificConfig */
    if (mp4ASC->objectTypeIndex == 1 || mp4ASC->objectTypeIndex == 2 ||
        mp4ASC->objectTypeIndex == 3 || mp4ASC->objectTypeIndex == 4 ||
        mp4ASC->objectTypeIndex == 6 || mp4ASC->objectTypeIndex == 7) {
        mp4ASC->frameLengthFlag = (uint8)getbits(&bitRd, 1);
    }
    msg_dbg("object index: %d sample rate: %d channels: %d frameLength %d", mp4ASC->objectTypeIndex,
            mp4ASC->samplingFrequency, mp4ASC->channelsConfiguration,
            ((mp4ASC->frameLengthFlag == 1) ? 1024 : 960));
    return;
}

static void get_h263_dimension(uint8* data, uint32* width, uint32* height) {
    BitReader bitRd;
    uint32 picture_size = 0;
    initbuffer(data, &bitRd);
    *width = 0;
    *height = 0;
    getbits(&bitRd, 17);
    getbits(&bitRd, 13);
    picture_size = getbits(&bitRd, 3);
    switch (picture_size) {
        case 0:
        case 1:
            *width = getbits(&bitRd, (8 * (picture_size + 1)));
            *height = getbits(&bitRd, (8 * (picture_size + 1)));
            break;
        case 2:
            *width = 352;
            *height = 288;
            break;
        case 3:
            *width = 176;
            *height = 144;
            break;
        case 4:
            *width = 128;
            *height = 96;
            break;
        case 5:
            *width = 320;
            *height = 240;
            break;
        case 6:
            *width = 160;
            *height = 120;
            break;
    }
    msg_dbg("H263 width: %d height: %d", *width, *height);
    return;
}
__attribute__((unused))
static void get_vp6_dimension(uint8* data, uint32* width, uint32* height) {
    BitReader bitRd;
    int32 fSeparatedCoeff = 0;
    int32 filterHeader = 0;
    initbuffer(data, &bitRd);
    getbits(&bitRd, 8);
    if (getbit1(&bitRd)) {
        // Delta (inter) frame
        return;
    }
    getbits(&bitRd, 6);
    fSeparatedCoeff = !!getbit1(&bitRd);
    getbits(&bitRd, 5);
    filterHeader = getbits(&bitRd, 2);
    getbit1(&bitRd);
    if (fSeparatedCoeff || !filterHeader) {
        getbits(&bitRd, 16);
    }
    *height = getbits(&bitRd, 8) * 16;
    *width = getbits(&bitRd, 8) * 16;
    return;
}

static void parse_scaling_list(uint32 size, bit_buffer* bb) {
    uint32 last_scale, next_scale, i;
    int32 delta_scale = 0;
    last_scale = 8;
    next_scale = 8;
    for (i = 0; i < size; i++) {
        if (next_scale != 0) {
            delta_scale = exp_golomb_se(bb);
            next_scale = (last_scale + delta_scale + 256) % 256;
        }
        if (next_scale != 0) {
            last_scale = next_scale;
        }
    }
}

static FLVPARSER_ERR_CODE get_AVCDecoderConfigurationRecord(uint8* buffer, uint32 size,
                                                            flv_parser_t* p_flv_parser) {
    uint32 i = 0;
    int32 avc_packet_type = 0;
    uint8* data = buffer;
    uint32 numSPS = 0;
    uint32 numPPS = 0;
    uint32 sps_size = 0;
    uint32 pps_size = 0;
    uint32 AVC_Config_length = 0;
    uint8* buf = NULL;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;

    msg_dbg("get_AVCDecoderConfigurationRecord");

    if (size < 1)
        return result;

    GetByte(avc_packet_type, data);
    size -= 1;
    if (avc_packet_type != AVC_SEQUENCE_HEADER) {
        return result;
    }

    if (size < 7)
        return result;

    data += 3;  // skip composition time
    size -= 3;

    data += 4;
    size -= 4;
    /* skip 4bytes:
       configurationVersion(1 byte)
       AVCProfileIndication(1 byte)
       profile_compatibility(1 byte)
       AVCLevelIndication(1 byte)
     */

    if (size < 1)
        return result;

    p_flv_parser->nal_length_size = (data[0] & 0x3) + 1;
    data += 1;
    size -= 1;

    if (size < 1)
        return result;

    GetByte(numSPS, data);
    size -= 1;

    /* number of SequenceParameterSets */
    numSPS = numSPS & 0x1F;
    if (numSPS == 0) {
        /* no SPS, return */
        return result;
    }
    AVC_Config_length += 6;

    for (i = 0; i < numSPS; i++) {
        GetBe16(sps_size, data);
        data += sps_size;
        AVC_Config_length += (2 + sps_size);
    }
    GetByte(numPPS, data);
    AVC_Config_length++;
    if (numPPS != 0) {
        for (i = 0; i < numPPS; i++) {
            GetBe16(pps_size, data);
            data += pps_size;
            AVC_Config_length += (2 + pps_size);
        }
    }

    buf = p_flv_parser->memOps.Malloc(AVC_Config_length + BUF_PAD_SIZE);
    if (NULL == buf) {
        result = PARSER_INSUFFICIENT_MEMORY;
        return result;
    }
    memcpy(buf, (buffer + 4), AVC_Config_length);
    p_flv_parser->stream_info.video_info.h264_codec_data.size = AVC_Config_length;
    p_flv_parser->stream_info.video_info.h264_codec_data.data = buf;

    return result;
}

static void get_h264_dimension(uint8* data, uint32 size, uint32* width, uint32* height) {
    bit_buffer bitRd;
    uint64 num_ref_frames_in_pic_order_cnt_cycle = 0;
    uint64 pic_width_in_mbs_minus1 = 0;
    uint64 pic_height_in_map_units_minus1 = 0;
    uint64 pic_order_cnt_type = 0;
    uint32 frame_mbs_only_flag = 0;
    uint32 profile = 0;
    uint32 level = 0;
    uint32 i = 0;
    int32 numOfSequenceParameterSets = 0;
    int32 avc_packet_type = 0;
    uint32 tmp = 0;
    uint32 sps_size = 0;
    *width = 0;
    *height = 0;

    msg_dbg("get_h264_dimension");

    if (size < 1)
        return;

    GetByte(avc_packet_type, data);
    size -= 1;

    if (avc_packet_type != AVC_SEQUENCE_HEADER) {
        return;
    }

    if (size < 8)
        return;
    data += 3;  // skip composition time
    size -= 3;

    data += 5;
    size -= 5;
    /* skip 5bytes:
       configurationVersion(1 byte)
       AVCProfileIndication(1 byte)
       profile_compatibility(1 byte)
       AVCLevelIndication(1 byte)
       lengthSizeMinusOne(1 byte)
     */

    if (size < 1)
        return;

    GetByte(numOfSequenceParameterSets, data);
    size -= 1;
    /* number of SequenceParameterSets */
    if ((numOfSequenceParameterSets & 0x1F) == 0) {
        /* no SPS, return */
        return;
    }

    if (size < 2)
        return;

    GetBe16(sps_size, data);
    size -= 2;

    msg_dbg("sps size %d", sps_size);
    data += 1;  // forbidden_zero_bit + nal_ref_idc + nal_unit_type(1 byte)
    bitRd.start = data;
    bitRd.size = sps_size;
    bitRd.current = data;
    bitRd.read_bits = 0;

    // nal length size
    profile = get_bits(&bitRd, 8);
    get_bits(&bitRd, 8);
    level = get_bits(&bitRd, 8);

    msg_dbg("profile %d level %d", profile, level);
    tmp = exp_golomb_ue(&bitRd);  // seq_parameter_set_id
    msg_dbg("seq_parameter_set_id %d", tmp);
    (void)level;
    (void)tmp;
    if (profile >= 100) {
        /* chroma format idx */
        if (exp_golomb_ue(&bitRd) == 3) {
            get_bits(&bitRd, 1);  // residue_transform_flag
        }
        exp_golomb_ue(&bitRd);  // bit_depth_luma_minus8
        exp_golomb_ue(&bitRd);  // bit_depth_chroma_minus8
        get_bit(&bitRd);        // qpprime_y_zero_transform_bypass_flag
        if (get_bit(&bitRd))    // seq_scaling_matrix_present_flag
        {
            parse_scaling_list(16, &bitRd);
        }
    }

    exp_golomb_ue(&bitRd);  // log2_max_frame_num_minus4
    pic_order_cnt_type = exp_golomb_ue(&bitRd);
    if (pic_order_cnt_type == 0) {
        exp_golomb_ue(&bitRd);  // log2_max_pic_order_cnt_lsb_minus4

    } else if (pic_order_cnt_type == 1) {
        get_bit(&bitRd);        // delta_pic_order_always_zero_flag
        exp_golomb_se(&bitRd);  // offset_for_non_ref_pic
        exp_golomb_se(&bitRd);  // offset_for_top_to_bottom_field
        num_ref_frames_in_pic_order_cnt_cycle = exp_golomb_ue(&bitRd);
        for (i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; i++) {
            exp_golomb_se(&bitRd);  // offset_for_ref_frame[i]
        }
    }
    exp_golomb_ue(&bitRd);  // num_ref_frames

    get_bit(&bitRd);  // gaps_in_frame_num_value_allowed_flag
    pic_width_in_mbs_minus1 = exp_golomb_ue(&bitRd);

    pic_height_in_map_units_minus1 = exp_golomb_ue(&bitRd);

    frame_mbs_only_flag = get_bits(&bitRd, 1);

    *width = (uint32)((pic_width_in_mbs_minus1 + 1) * 16);
    *height = (uint32)((2 - frame_mbs_only_flag) * (pic_height_in_map_units_minus1 + 1) * 16);
    return;
}

static void dump_result_info(FLVPARSER_ERR_CODE result) {
    uint32 i = 0;
    uint32 size = sizeof(info_table) / sizeof(result_info_t);
    for (i = 0; i < size; i++) {
        result_info_t* p = &info_table[i];
        if (result == p->result) {
            msg_dbg("%s", info_table[i].result_string);
            return;
        }
    }
    msg_dbg("Undefined error\n");
    return;
}
__attribute__((unused))
static int64 Int2dbl(int64 v) {
    double result = 0.0;

    if ((uint64)(v + v) > ((uint64)0xFFE << 52)) {
#ifdef _MSC_VER
        result = 0 / result;
#else
        result = 0.0 / 0.0;
#endif
    }
    result = (double)(ldexp(((v & (((int64)1 << 52) - 1)) + ((int64)1 << 52)) * (v >> 63 | 1),
                            (v >> 52 & 0x7FF) - 1075));
    return (int64)result;
}

static double Getdbl(int64 v) {
    double result = 0.0;

    if ((uint64)(v + v) > ((uint64)0xFFE << 52)) {
#ifdef _MSC_VER
        result = 0 / result;
#else
        result = 0.0 / 0.0;
#endif
    }
    result = (double)(ldexp(((v & (((int64)1 << 52) - 1)) + ((int64)1 << 52)) * (v >> 63 | 1),
                            (v >> 52 & 0x7FF) - 1075));
    return result;
}

static int flv_strcmp(const int8* s1, const int8* s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        s1++;
        s2++;
    }
    return (*s1 - *s2);
}

#define n_stream_read(s, pRead, iRead) stream_read(s, p_flv_parser, pRead, iRead)
#define n_stream_seek(s, offset, whence) stream_seek(s, p_flv_parser, offset, whence)
#define n_stream_ftell(s) stream_ftell(s, p_flv_parser)
#define n_stream_fskip(s, size) stream_fskip(s, p_flv_parser, size)
#define n_stream_total_size(s) stream_total_size(s, p_flv_parser)
#define n_stream_check_left_bytes(s, size) stream_check_left_bytes(s, p_flv_parser, size)
#define n_stream_read_byte(s) stream_read_byte(s, p_flv_parser)
#define n_stream_read_be16(s) stream_read_be16(s, p_flv_parser)
#define n_stream_read_be24(s) stream_read_be24(s, p_flv_parser)
#define n_stream_read_be32(s) stream_read_be32(s, p_flv_parser)
#define n_stream_read_be64(s) stream_read_be64(s, p_flv_parser)

static FLVPARSER_ERR_CODE stream_read(FslFileStream* s, flv_parser_t* p_flv_parser, void* p_read,
                                      int i_read) {
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    if (p_recovery_buf->count == 0) {
        // normal case when no recovery working

        int32 actual_read = 0;

        actual_read = s->Read(p_flv_parser->fileHandle, p_read, i_read, p_flv_parser->appContext);

        if (actual_read < i_read) {
            msg_dbg("require %d, actual %d bytes", i_read, actual_read);
            return PARSER_READ_ERROR;
        }
        return PARSER_SUCCESS;
    }

    // read data from recovery buf

    if (p_recovery_buf->count < i_read) {
        // anytime there should be enough data for one tag in recovery buf
        msg_dbg("read from recovery buf fail: require %d, actual %d bytes", i_read,
                p_recovery_buf->count);
        return PARSER_READ_ERROR;
    }

    return flv_parser_rbf_read(p_flv_parser, p_read, i_read);
}

static FLVPARSER_ERR_CODE stream_seek(FslFileStream* s, flv_parser_t* p_flv_parser, int64 offset,
                                      int32 whence) {
    /* modified for streaming mode*/
    int32 ret = 0;
    int64 cur_offset = 0;
    int64 cur_stream_offset = 0;
    int64 target_offset = 0;
    int32 to_skip = 0;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    if (p_recovery_buf->count == 0) {
        // normal case when no recovery working, just seek in stream
        ret = s->Seek(p_flv_parser->fileHandle, offset, whence, p_flv_parser->appContext);
        return ret;
    }

    if (whence == SEEK_END) {
        // clear recovery buf and seek in stream
        p_recovery_buf->read = p_recovery_buf->write = p_recovery_buf->count = 0;
        ret = s->Seek(p_flv_parser->fileHandle, offset, whence, p_flv_parser->appContext);
        return ret;
    }

    cur_stream_offset = s->Tell(p_flv_parser->fileHandle, p_flv_parser->appContext);
    cur_offset = cur_stream_offset - p_recovery_buf->count;

    if (whence == SEEK_CUR)
        target_offset = cur_offset + offset;
    else if (whence == SEEK_SET)
        target_offset = offset;

    if (target_offset < cur_offset || target_offset >= cur_stream_offset) {
        // target offset is beyond range of recovery buf, clear recovery buf and seek in stream
        p_recovery_buf->read = p_recovery_buf->write = p_recovery_buf->count = 0;
        ret = s->Seek(p_flv_parser->fileHandle, offset, whence, p_flv_parser->appContext);
        return ret;
    }

    to_skip = (int32)(target_offset - cur_offset);

    // seek in recovery buf
    ret = flv_parser_rbf_fskip(p_flv_parser, to_skip);

    return ret;
}

static uint64 stream_ftell(FslFileStream* s, flv_parser_t* p_flv_parser) {
    int64 cur_offset = 0;
    int64 cur_stream_offset = 0;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    cur_stream_offset = s->Tell(p_flv_parser->fileHandle, p_flv_parser->appContext);

    if (p_recovery_buf->count == 0) {
        // normal case when no recovery working
        return cur_stream_offset;
    }

    cur_offset = cur_stream_offset - p_recovery_buf->count;
    return cur_offset;
}

static FLVPARSER_ERR_CODE stream_fskip(FslFileStream* s, flv_parser_t* p_flv_parser, int32 size) {
    return stream_seek(s, p_flv_parser, size, FSL_SEEK_CUR);
}

static uint64 stream_total_size(FslFileStream* s, flv_parser_t* p_flv_parser) {
    return s->Size(p_flv_parser->fileHandle, p_flv_parser->appContext);
}

static FLVPARSER_ERR_CODE stream_check_left_bytes(FslFileStream* s, flv_parser_t* p_flv_parser,
                                                  int32 left) {
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;
    if (s->CheckAvailableBytes(p_flv_parser->fileHandle, left, p_flv_parser->appContext) +
                p_recovery_buf->count <
        left) {
        return PARSER_NEED_MORE_DATA;
    }
    return PARSER_SUCCESS;
}

static uint32 stream_read_byte(FslFileStream* s, flv_parser_t* p_flv_parser) {
    uint32 c = 0;
    uint8 byte = 0;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    if (p_recovery_buf->count == 0)
        s->Read(p_flv_parser->fileHandle, &c, 1, p_flv_parser->appContext);
    else {
        flv_parser_rbf_read(p_flv_parser, &byte, 1);
        c = byte;
    }
    return c;
}

static uint32 stream_read_be16(FslFileStream* s, flv_parser_t* p_flv_parser) {
    uint32 x = 0;
    uint32 y = 0;
    x = stream_read_byte(s, p_flv_parser);
    y = stream_read_byte(s, p_flv_parser);
    return ((x << 8) | y);
}
__attribute__((unused))
static uint32 stream_read_be24(FslFileStream* s, flv_parser_t* p_flv_parser) {
    uint32 y = 0;
    y = stream_read_byte(s, p_flv_parser);
    y = (y << 8) | stream_read_byte(s, p_flv_parser);
    y = (y << 8) | stream_read_byte(s, p_flv_parser);
    return y;
}

static uint32 stream_read_be32(FslFileStream* s, flv_parser_t* p_flv_parser) {
    uint32 y = 0;
    y = stream_read_byte(s, p_flv_parser);
    y = (y << 8) | stream_read_byte(s, p_flv_parser);
    y = (y << 8) | stream_read_byte(s, p_flv_parser);
    y = (y << 8) | stream_read_byte(s, p_flv_parser);
    return y;
}

static uint64 stream_read_be64(FslFileStream* s, flv_parser_t* p_flv_parser) {
    uint64 val = 0;
    val = (uint64)stream_read_be32(s, p_flv_parser) << 32;
    val |= (uint64)stream_read_be32(s, p_flv_parser);
    return val;
}
__attribute__((unused))
static void flv_tag_dump(flv_tag_t* t) {
    if (t == NULL) {
        return;
    }
    msg_dbg("dataSize = %d, timestamp = %d, type = %d", t->dataSize, t->timestamp, t->type);
}

uint32 flv_parser_convertaudiotype(uint32 audioformat, uint32* pSubType) {
    switch (audioformat) {
        case FLV_CODECID_PCM:
            return AUDIO_PCM;
            break;
        case FLV_CODECID_ADPCM:
            return AUDIO_ADPCM;
            break;
        case FLV_CODECID_MP3:
            return AUDIO_MP3;
            break;
        case FLV_CODECID_PCM_LE:
            return AUDIO_PCM;
            break;
        case FLV_CODECID_NELLYMOSER_8KHZ_MONO:
            break;
        case FLV_CODECID_NELLYMOSER:
            return AUDIO_NELLYMOSER;
            break;
        case FLV_CODECID_AAC:
            *pSubType = AUDIO_AAC_RAW;
            return AUDIO_AAC;
            break;
        case FLV_CODECID_SPEEX:
            return AUIDO_SPEEX;
            break;
        default:
            return AUDIO_TYPE_UNKNOWN;
            break;
    }

    return AUDIO_TYPE_UNKNOWN;
}

uint32 flv_parser_convertvideotype(uint32 videoformat, uint32* pSubType) {
    switch (videoformat) {
        case FLV_CODECID_H263:
            return VIDEO_SORENSON_H263;
            break;
        case FLV_CODECID_SCREEN:
            return VIDEO_FLV_SCREEN;
            break;
        case FLV_CODECID_VP6:
            *pSubType = VIDEO_VP6;
            return VIDEO_ON2_VP;
            break;
        case FLV_CODECID_VP6A:
            *pSubType = VIDEO_VP6A;
            return VIDEO_ON2_VP;
            break;
        case FLV_CODECID_SCREEN2:
            return VIDEO_FLV_SCREEN;
            break;
        case FLV_CODECID_H264:
            return VIDEO_H264;
            break;
        default:
            return VIDEO_TYPE_UNKNOWN;
            break;
    }
}

static void flv_set_video_codec(flv_video_info_t* p, uint32 flv_codec_id) {
    switch (flv_codec_id) {
        case FLV_CODECID_H263:
        case FLV_CODECID_SCREEN:
        case FLV_CODECID_VP6:
        case FLV_CODECID_VP6A:
        case FLV_CODECID_H264:
            break;
        default:
            msg_dbg("############### Unknown video format! ###############");
            break;
    }
    p->codec = flv_codec_id;
    return;
}

static void flv_set_audio_codec(flv_audio_info_t* p_audio_info, uint32 flv_codec_id) {
    switch (flv_codec_id) {
        // no distinction between S16 and S8 PCM codec flags
        case FLV_CODECID_PCM:
            break;
        case FLV_CODECID_PCM_LE:
            break;
        case FLV_CODECID_AAC:
            break;
        case FLV_CODECID_ADPCM:
            break;
        case FLV_CODECID_MP3:
            break;
        case FLV_CODECID_NELLYMOSER_8KHZ_MONO:
            p_audio_info->samplingRate =
                    8000;  // in case metadata does not otherwise declare samplerate
            break;
        case FLV_CODECID_NELLYMOSER:
            break;
        default:
            msg_dbg("############### Unknown audio format:%d ! ###############", flv_codec_id);
            break;
    }
    p_audio_info->format = flv_codec_id;
    return;
}

static FLVPARSER_ERR_CODE flv_parser_read_audio_hdr(flv_parser_t* p_flv_parser) {
    uint32 flags = 0;
    FslFileStream* s = &(p_flv_parser->fileStream);
    flv_audio_info_t* p_audio_info = &p_flv_parser->stream_info.audio_info;
    flags = n_stream_read_byte(s);

    p_audio_info->channel = (flags & FLV_AUDIO_CHANNEL_MASK) == FLV_STEREO ? 2 : 1;

    p_audio_info->sampleSize = (flags & FLV_AUDIO_SAMPLE_SIZE_MASK) ? 16 : 8;

    if ((flags & FLV_AUDIO_CODEC_ID_MASK) >> FLV_AUDIO_CODECID_OFFSET ==
        FLV_CODECID_NELLYMOSER_8KHZ_MONO) {
        p_audio_info->samplingRate = 8000;
    } else {
        p_audio_info->samplingRate =
                (44100 << ((flags & FLV_AUDIO_SAMPLE_RATE_MASK) >> FLV_AUDIO_SAMPLERATE_OFFSET) >>
                 3);
    }
    flv_set_audio_codec(p_audio_info, ((flags >> FLV_AUDIO_CODECID_OFFSET) & 0xf));

    msg_dbg("$$$$ audio format: %d, channel: %d, sampleSize: %d, sampleRate: %d",
            p_audio_info->format, p_audio_info->channel, p_audio_info->sampleSize,
            p_audio_info->samplingRate);

    return PARSER_SUCCESS;
}

static FLVPARSER_ERR_CODE flv_parser_read_video_hdr(flv_parser_t* p_flv_parser) {
    uint32 flags = 0;
    FslFileStream* s = &(p_flv_parser->fileStream);
    flv_video_info_t* p_video_info = &p_flv_parser->stream_info.video_info;
    flags = n_stream_read_byte(s);
    flv_set_video_codec(p_video_info, flags & FLV_VIDEO_CODEC_ID_MASK);
    msg_dbg("$$$$ video codec: %d", p_video_info->codec);
    return PARSER_SUCCESS;
}

static FLVPARSER_ERR_CODE flv_read_video_dimension(flv_parser_t* p_flv_parser, flv_tag_t* p_tag) {
    uint32 codec = p_flv_parser->stream_info.video_info.codec;
    FslFileStream* s = &(p_flv_parser->fileStream);
    flv_video_info_t* p_video_info = &p_flv_parser->stream_info.video_info;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    uint8* buffer = NULL;

    buffer = p_flv_parser->memOps.Malloc(p_tag->dataSize + BUF_PAD_SIZE);
    if (buffer == NULL) {
        result = PARSER_INSUFFICIENT_MEMORY;
        goto bail;
    }

    CheckForBail(n_stream_read(s, buffer,
                               p_tag->dataSize - 1));  // one byte of video header has been read

    switch (codec) {
        case FLV_CODECID_H263:
            get_h263_dimension(buffer, &p_video_info->width, &p_video_info->height);
            break;
        case FLV_CODECID_VP6A:
            // fall through
        case FLV_CODECID_VP6:
            break;
        case FLV_CODECID_H264:
            get_h264_dimension(buffer, p_tag->dataSize - 1, &p_video_info->width,
                               &p_video_info->height);
            if (p_flv_parser->flags & FLAG_H264_NO_CONVERT) {
                result = get_AVCDecoderConfigurationRecord(buffer, p_tag->dataSize - 1,
                                                           p_flv_parser);
            }
            break;
        default:
            break;
    }
    msg_dbg("$$$$ video width: %d, height: %d", p_video_info->width, p_video_info->height);
bail:
    // free allcated buffer
    if (buffer != NULL) {
        p_flv_parser->memOps.Free(buffer);
    }

    return result;
}

uint32 flv_parser_streamnum_from_type(flv_parser_t* p_flv_parser, uint32 streamType) {
    if (p_flv_parser->audio_found) {
        if (streamType == FLV_TAG_TYPE_AUDIO)
            return 0;
        else if ((streamType == FLV_TAG_TYPE_VIDEO) && p_flv_parser->video_found)
            return 1;
        else
            return (uint32)-1;
    } else if (p_flv_parser->video_found) {
        if (streamType == FLV_TAG_TYPE_VIDEO)
            return 0;

    }
    // no stream
    else
        return (uint32)-1;

    return (uint32)-1;
}

#define FLV_MAX_AVC_INFO_SIZE (64 * 1024)

static FLVPARSER_ERR_CODE flv_parser_get_avc_info2(flv_parser_t* p_flv_parser) {
    AVCDecoderConfigurationRecord adcr;
    uint8 buffer[6];
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    FslFileStream* s = &(p_flv_parser->fileStream);

    uint8* p = NULL;
    uint32 i = 0;
    uint32 numSPS = 0;
    uint32 numPPS = 0;
    uint32 local_dst_len = 0;

    CheckForBail(n_stream_read(s, buffer, 6));
    adcr.configurationVersion = buffer[0];
    adcr.AVCProfileIndication = buffer[1];
    adcr.profile_compatibility = buffer[2];
    adcr.AVCLevelIndication = buffer[3];
    adcr.lengthSizeMinusOne = buffer[4];
    adcr.numOfSequenceParameterSets = buffer[5];

    /* number of SequenceParameterSets */
    numSPS = (adcr.numOfSequenceParameterSets & 0x1F);

    p = p_flv_parser->video_infobuffer;
    if (numSPS > 0) {
        uint16 sps_size = 0;
        for (i = 0; i < numSPS; i++) {
            sps_size = (uint16)n_stream_read_be16(s);
            if (sps_size == 0) {
                /* no SPS, return */
                p_flv_parser->video_infolength = 0;
                return result;
            }
            msg_dbg("SPS size is %d", sps_size);
            /* Add start code and read the SPS entirely */
            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
            p[3] = 1;
            p += 4;
            CheckForBail(n_stream_read(s, p, sps_size));
            p += sps_size;
            local_dst_len += (4 + sps_size);
        }
    }

    numPPS = n_stream_read_byte(s);
    if (numPPS > 0) {
        uint16 pps_size = 0;
        for (i = 0; i < numPPS; i++) {
            pps_size = (uint16)n_stream_read_be16(s);
            if (pps_size == 0) {
                /* no PPS, return */
                return result;
            }
            msg_dbg("PPS size is %d", pps_size);
            /* Add start code and read the PPS entirely */
            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
            p[3] = 1;
            p += 4;
            CheckForBail(n_stream_read(s, p, pps_size));
            p += pps_size;
            local_dst_len += (4 + pps_size);
        }
    }

    p_flv_parser->video_infolength = local_dst_len;
    p_flv_parser->nal_length_size = (adcr.lengthSizeMinusOne & 0x3) + 1;

bail:
    return result;
}

static FLVPARSER_ERR_CODE flv_h264_get_codec_data(flv_parser_t* p_flv_parser) {

    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    FslFileStream* s = &(p_flv_parser->fileStream);
    uint32 local_dst_len = p_flv_parser->stream_info.video_info.h264_codec_data.size;

    if (local_dst_len != 0) {
        CheckForBail(n_stream_fskip(s, local_dst_len));
        p_flv_parser->video_infolength = 0;
    } else {
        /* in case that the AVCDecoderConfigurationRecord doesn't got in createParser
           We have to set convert the AVCDecoderConfigurationRecord as before.*/
        p_flv_parser->flags &= (~FLAG_H264_NO_CONVERT);
        result = flv_parser_get_avc_info2(p_flv_parser);
    }

bail:
    return result;
}
__attribute__((unused))
static FLVPARSER_ERR_CODE flv_parser_get_avc_info(flv_parser_t* p_flv_parser, uint8** dst,
                                                  uint32* dst_len, uint32 tSize, void** bufContext,
                                                  uint32* nal_length_size) {
    AVCDecoderConfigurationRecord adcr;
    uint8 buffer[6];
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    FslFileStream* s = &(p_flv_parser->fileStream);
    ParserOutputBufferOps* pRequestBufferOps = &(p_flv_parser->outputOps);

    uint8* p = NULL;
    uint32 i = 0;
    uint32 numSPS = 0;
    uint32 numPPS = 0;
    uint32 local_dst_len = 0;
    uint32 streamNum = flv_parser_streamnum_from_type(p_flv_parser, FLV_TAG_TYPE_VIDEO);
    uint32 bufSize = tSize + 1024;
    bool isUsingHistory = FALSE;

    *dst_len = 0;
    CheckForBail(n_stream_read(s, buffer, 6));
    adcr.configurationVersion = buffer[0];
    adcr.AVCProfileIndication = buffer[1];
    adcr.profile_compatibility = buffer[2];
    adcr.AVCLevelIndication = buffer[3];
    adcr.lengthSizeMinusOne = buffer[4];
    adcr.numOfSequenceParameterSets = buffer[5];

    /* number of SequenceParameterSets */
    numSPS = (adcr.numOfSequenceParameterSets & 0x1F);

    *dst = pRequestBufferOps->RequestBuffer(streamNum, &bufSize, bufContext,
                                            p_flv_parser->appContext);
    if (*dst == NULL) {
        if (NULL == p_flv_parser->p_HistoryBuf)
            LocalFree(p_flv_parser->p_HistoryBuf);
        if (p_flv_parser->p_HistoryBuf)
            p_flv_parser->p_HistoryBuf = LocalMalloc(tSize + 1024);

        if (NULL == p_flv_parser->p_HistoryBuf)
            return PARSER_INSUFFICIENT_MEMORY;

        p = p_flv_parser->p_HistoryBuf;
        isUsingHistory = TRUE;
        result = PARSER_ERR_NO_OUTPUT_BUFFER;
    } else {
        p = *dst;
    }

    if (numSPS > 0) {
        uint16 sps_size = 0;
        for (i = 0; i < numSPS; i++) {
            sps_size = (uint16)n_stream_read_be16(s);
            if (sps_size == 0) {
                /* no SPS, return */
                *dst_len = 0;
                return result;
            }
            msg_dbg("SPS size is %d", sps_size);
            /* Add start code and read the SPS entirely */
            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
            p[3] = 1;
            p += 4;
            CheckForBail(n_stream_read(s, p, sps_size));
            p += sps_size;
            local_dst_len += (4 + sps_size);
        }
    }

    numPPS = n_stream_read_byte(s);
    if (numPPS > 0) {
        uint16 pps_size = 0;
        for (i = 0; i < numPPS; i++) {
            pps_size = (uint16)n_stream_read_be16(s);
            if (pps_size == 0) {
                /* no PPS, return */
                return result;
            }
            msg_dbg("PPS size is %d", pps_size);
            /* Add start code and read the PPS entirely */
            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
            p[3] = 1;
            p += 4;
            CheckForBail(n_stream_read(s, p, pps_size));
            p += pps_size;
            local_dst_len += (4 + pps_size);
        }
    }
    *dst_len = local_dst_len;
    *nal_length_size = (adcr.lengthSizeMinusOne & 0x3) + 1;

    if (isUsingHistory) {
        p_flv_parser->historyBufLen = local_dst_len;
        p_flv_parser->historyBufOffset = 0;
        *dst_len = 0;
    }

    msg_dbg("nal length size is %d avc info length %d", *nal_length_size, local_dst_len);
bail:
    return result;
}

/*
static FLV_PARSER_ERR_CODE flv_parser_validate_tag(flv_parser_t* p_flv_paser, FslFileStream* s, bool
*pIsValidate,)
{

}
*/

FLVPARSER_ERR_CODE flv_parser_search_tag(flv_parser_t* p_flv_parser, FslFileStream* s,
                                                flv_tag_t* p_tag, uint64 lastOffset,
                                                bool* pSyncFound) {
    uint8 buffer[2 * FLV_TAG_SIZE + 4];
    uint8* p = buffer;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    int TimeStampExtended = 0;
    int StreamID = 0;
    int Offset = 0;
    uint64 currentpos;
    uint64 syncpos;
    *pSyncFound = FALSE;

    syncpos = n_stream_ftell(s);
    msg_dbg("search tag start at %llx", syncpos);

    CheckForBail(n_stream_read(s, buffer, 2 * FLV_TAG_SIZE + 4));

    currentpos = n_stream_ftell(s);
    if (currentpos > lastOffset)
        goto bail;

READTYPE:

    GetByte(p_tag->type, p);
    Offset++;
    if (p_tag->type != FLV_TAG_TYPE_AUDIO && p_tag->type != FLV_TAG_TYPE_VIDEO &&
        p_tag->type != FLV_TAG_TYPE_META) {
        // the tag header is not found
        if (Offset == (2 * FLV_TAG_SIZE + 4)) {
            if (n_stream_ftell(s) >= lastOffset) {
                result = PARSER_ERR_CONCEAL_FAIL;
                msg_dbg("reach lastOffset");
                goto bail;
            }
            CheckForBail(n_stream_read(s, buffer, 2 * FLV_TAG_SIZE + 4));
            p = buffer;
            Offset = 0;
        }
        goto READTYPE;
    }

    if (Offset >= FLV_TAG_SIZE) {
        Offset--;  // point to type
        memmove(buffer, buffer + Offset, 2 * FLV_TAG_SIZE + 4 - Offset);
        CheckForBail(n_stream_read(s, buffer + 2 * FLV_TAG_SIZE + 4 - Offset, Offset));
        Offset = 1;
        p = (buffer + 1);
    }

    GetBe24(p_tag->dataSize, p);  // DataSize
    if ((p_tag->dataSize + currentpos) > p_flv_parser->filesize) {
        p = p - 3;
        goto READTYPE;
    }

    GetBe24(p_tag->timestamp, p);   // TimeStamp
    GetByte(TimeStampExtended, p);  // TimeStampExtended
    p_tag->timestamp |= (TimeStampExtended << 24);

    /* some streams have errors in meta data tag, duration will be incorrect and can not use it as a
     * condition */
#if 1
    if ((0 != p_flv_parser->stream_info.duration) &&
        (p_tag->timestamp > (p_flv_parser->stream_info.duration + p_flv_parser->timestamp_base))) {
        // this timestamp is not right
        p = p - 7;
        msg_dbg("discard this tag because timestamp overflow %x, duration %x", p_tag->timestamp,
                p_flv_parser->stream_info.duration);
        goto READTYPE;
    }
#endif

    GetBe24(StreamID, p);
    if (StreamID != 0) {
        // StreamID should be 0
        p = p - 10;
        goto READTYPE;
    }

    {
        uint8 preSizeBuffer[4];
        uint8* pSizeBuffer = (uint8*)preSizeBuffer;
        syncpos = n_stream_ftell(s);

        CheckForBail(
                n_stream_seek(s, Offset - FLV_TAG_SIZE - 5 + (int)(p_tag->dataSize), SEEK_CUR));

        CheckForBail(n_stream_read(s, preSizeBuffer, 4));
        GetBe32(p_tag->preSize, pSizeBuffer);
        n_stream_seek(s, syncpos, SEEK_SET);

        if (p_tag->preSize < (p_tag->dataSize + FLV_TAG_SIZE))  // loose the constraint
        {
            p = p - 10;
            goto READTYPE;
        }
    }

    // Add more check here
    // read next tag for checking

    CheckForBail(n_stream_seek(s, -(2 * FLV_TAG_SIZE - Offset + 4 + 5), SEEK_CUR));

    syncpos = n_stream_ftell(s);

    *pSyncFound = TRUE;

    msg_dbg("tag type: %d, size: %d, timestamp: %d StreamID: %d, syncpos %llx", p_tag->type,
            p_tag->dataSize, p_tag->timestamp, StreamID, syncpos);
    // omit StreamID(always 0)

bail:

    return result;
}

FLVPARSER_ERR_CODE flv_parser_search_tag_file_mode(flv_parser_t* p_flv_parser,
                                                          flv_tag_t* p_tag) {
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    uint8 header[FLV_TAG_SIZE + 4];
    int32 actual_read = 0;
    uint8 found_tag = 0;
    int32 to_read = 0;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    if (p_recovery_buf->count < FLV_TAG_HEADER_SIZE) {
        uint32 to_read = FLV_TAG_HEADER_SIZE;
        uint32 offset = 0;

        if (p_recovery_buf->count > 0) {
            // a few bytes left in recovery buf, move all of them to local header[]
            //  this should seldom occur
            to_read -= p_recovery_buf->count;
            offset = p_recovery_buf->count;
            CheckForBail(flv_parser_rbf_read(p_flv_parser, header, p_recovery_buf->count));
        }

        actual_read = p_flv_parser->fileStream.Read(p_flv_parser->fileHandle, header + offset,
                                                    to_read, p_flv_parser->appContext);
        if (actual_read < (int32)to_read) {
            msg_dbg("read fail: %s line %d, require %d, actual %d bytes", __FUNCTION__, __LINE__,
                    to_read, actual_read);
            return PARSER_READ_ERROR;
        }

        if (flv_parser_is_possible_tag(p_flv_parser, header, p_tag)) {
            // this is the normal case for a correct stream
            return PARSER_SUCCESS;
        }

        msg_dbg("\n============= start error recovery at 0x%llx=============\n\n",
                n_stream_ftell(&p_flv_parser->fileStream) - FLV_TAG_HEADER_SIZE);

        // initialize recovery buffer before first writing
        if (p_recovery_buf->pBuf == 0)
            CheckForBail(flv_parser_rbf_init(p_flv_parser, RECOVER_BUF_SIZE));

        // not a valid tag, copy header to recovery buf to prepare for searching

        CheckForBail(flv_parser_rbf_write_from_data(p_flv_parser, header, FLV_TAG_HEADER_SIZE));

    } else {
        CheckForBail(flv_parser_rbf_peek(p_flv_parser, header, FLV_TAG_HEADER_SIZE));

        if (flv_parser_is_possible_tag(p_flv_parser, header, p_tag)) {
            // found a tag header in recovery buf
            // this is the normal case for a erroneous stream that has been recovered and there are
            // still some tags exist in recovery buf

            if (p_recovery_buf->count < (int32)(p_tag->dataSize + FLV_TAG_HEADER_SIZE)) {
                uint32 to_read = p_tag->dataSize + FLV_TAG_HEADER_SIZE - p_recovery_buf->count;
                msg_dbg("read all data of this tag from stream into recovery buf: %d\n", to_read);

                if ((int32)to_read > p_recovery_buf->len - p_recovery_buf->count) {
                    msg_dbg("buf space is not enough to hold this tag data\n");
                    CheckForBail(
                            flv_parser_rbf_init(p_flv_parser, to_read + p_recovery_buf->count));
                }

                CheckForBail(flv_parser_rbf_write_from_stream(p_flv_parser, to_read));
            }

            msg_dbg("reach next tag in recovery buf, pos %d, remain data %d\n",
                    p_recovery_buf->read, p_recovery_buf->count);

            //  skip forward to tag data
            CheckForBail(flv_parser_rbf_fskip(p_flv_parser, FLV_TAG_HEADER_SIZE));
            return PARSER_SUCCESS;
        }

        // not found a tag header, goes down to start search in recovery buf
    }

    // till now we can not find a valid tag, fill recovery buf with data and begin to search...

    to_read = p_flv_parser->recovery_buf.len - p_flv_parser->recovery_buf.count;

    // if almost reach end of file
    if (n_stream_ftell(&p_flv_parser->fileStream) + to_read + p_recovery_buf->count >
        p_flv_parser->filesize)
        to_read = (int32)(p_flv_parser->filesize - n_stream_ftell(&p_flv_parser->fileStream) -
                          p_flv_parser->recovery_buf.count);

    if (to_read > 0)
        CheckForBail(flv_parser_rbf_write_from_stream(p_flv_parser, to_read));

    flv_parser_rbf_search_tag(p_flv_parser, p_tag, &found_tag);

    if (!found_tag)
        result = PARSER_ERR_CONCEAL_FAIL;
    else {
        // found one tag header, skip forward to tag data
        CheckForBail(flv_parser_rbf_fskip(p_flv_parser, FLV_TAG_HEADER_SIZE));
    }

bail:
    return result;
}

static int flv_parser_is_possible_tag(flv_parser_t* p_flv_parser, uint8* pHeader,
                                      flv_tag_t* p_tag) {
    int TimeStampExtended = 0;
    int StreamID = 0;

    GetBe32(p_tag->preSize, pHeader);
    GetByte(p_tag->type, pHeader);
    GetBe24(p_tag->dataSize, pHeader);
    GetBe24(p_tag->timestamp, pHeader);   // TimeStamp
    GetByte(TimeStampExtended, pHeader);  // TimeStampExtended
    p_tag->timestamp |= (TimeStampExtended << 24);
    GetBe24(StreamID, pHeader);
    (void)p_flv_parser;

    if (p_tag->type != FLV_TAG_TYPE_AUDIO && p_tag->type != FLV_TAG_TYPE_VIDEO &&
        p_tag->type != FLV_TAG_TYPE_META)
        return 0;

    if (StreamID != 0)
        return 0;

    if (p_tag->dataSize == 0)
        return 0;

    return 1;
}

static FLVPARSER_ERR_CODE flv_parser_read_tag(flv_parser_t* p_flv_parser, FslFileStream* s,
                                              flv_tag_t* p_tag, uint8* buffer) {
    uint8* p = buffer;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    int TimeStampExtended = 0;

    CheckForBail(n_stream_read(s, buffer, (FLV_TAG_SIZE + 4)));
    GetBe32(p_tag->preSize, p);     // size of previous packet
    GetByte(p_tag->type, p);        // Type
    GetBe24(p_tag->dataSize, p);    // DataSize
    GetBe24(p_tag->timestamp, p);   // TimeStamp
    GetByte(TimeStampExtended, p);  // TimeStampExtended
    p_tag->timestamp |= (TimeStampExtended << 24);
    p += 3; //GetBe24(StreamID, p);
    // omit StreamID(always 0)

    if (p_tag->type != FLV_TAG_TYPE_META && p_tag->type != FLV_TAG_TYPE_AUDIO &&
        p_tag->type != FLV_TAG_TYPE_VIDEO) {
        msg_dbg("Unknown tag type %d!", p_tag->type);
        result = PARSER_ERR_INVALID_MEDIA;
    }

bail:
    return result;
}

static FLVPARSER_ERR_CODE flv_parser_read_header(flv_parser_t* p_flv_parser) {
    int flags = 0;
    uint8 buffer[FLV_FILE_HEADER_SIZE];
    uint8* p = buffer;
    FslFileStream* s = &(p_flv_parser->fileStream);
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;

    CheckForBail(n_stream_read(s, buffer, FLV_FILE_HEADER_SIZE));

    if (buffer[0] != 'F' || buffer[1] != 'L' || buffer[2] != 'V') {
        msg_dbg("Is not flv header");
        return PARSER_ERR_INVALID_MEDIA;
    }

    flags = buffer[4];  // flags
    p_flv_parser->stream_info.video_present = 0;
    p_flv_parser->stream_info.audio_present = 0;

    /* old flvtool cleared this field */
    if (flags == 0) {
        flags = FLV_HEADER_FLAG_HASVIDEO | FLV_HEADER_FLAG_HASAUDIO;
    }
    if (flags & FLV_HEADER_FLAG_HASVIDEO) {
        p_flv_parser->stream_info.video_present = 1;
        msg_dbg("video exist");
    }
    if (flags & FLV_HEADER_FLAG_HASAUDIO) {
        p_flv_parser->stream_info.audio_present = 1;
        msg_dbg("audio exist");
    }
    msg_dbg("video %d, audio %d", p_flv_parser->stream_info.video_present,
            p_flv_parser->stream_info.audio_present);
    p += 5;

    GetBe32(p_flv_parser->offset, p);
    msg_dbg("Boy starts at %lld", p_flv_parser->offset);
    p_flv_parser->auds_chunk_pos = p_flv_parser->offset;
    p_flv_parser->vids_chunk_pos = p_flv_parser->offset;
    p_flv_parser->body_offset = p_flv_parser->offset;
bail:
    return result;
}

static int32 amf_get_string(flv_parser_t* p_flv_parser, FslFileStream* s, int8* buffer,
                            uint32 buffer_size) {
    uint32 length = 0;
    length = n_stream_read_be16(s);

    if (length > buffer_size) {
        return -1;
    }
    if (n_stream_read(s, buffer, length) != PARSER_SUCCESS)
        return -1;
    buffer[length] = '\0';
    return length;
}

#define AMF_NUM_DB_UNIT 1

static int32 amf_parse_object(flv_parser_t* p_flv_parser, const int8* key, uint64 max_pos,
                              int32 depth) {
    AMF_Data_Type type;
    uint32 key_len = 0;
#ifndef AMF_NUM_DB_UNIT
    uint64 num_val = 0;
#else
    double num_val = 0.0;
#endif
    uint32 end_mark = 0;
    flv_metadata_t* m = &p_flv_parser->metadata;
    int8* buffer = p_flv_parser->metadata.keyname;
    int32 audio_present = p_flv_parser->stream_info.audio_present;
    int32 video_present = p_flv_parser->stream_info.video_present;
    FslFileStream* s = &(p_flv_parser->fileStream);
    flv_audio_info_t* p_audio_info = &p_flv_parser->stream_info.audio_info;
    flv_video_info_t* p_video_info = &p_flv_parser->stream_info.video_info;
    uint32 i = 0;
    type = n_stream_read_byte(s);
    if (key != NULL) {
    }
    switch (type) {
        case AMF_DATA_TYPE_NUMBER:
#ifndef AMF_NUM_DB_UNIT
            num_val = Int2dbl(n_stream_read_be64(s));
#else
            num_val = Getdbl(n_stream_read_be64(s));
#endif
            break;
        case AMF_DATA_TYPE_BOOL:
            num_val = n_stream_read_byte(s);
            break;
        case AMF_DATA_TYPE_STRING:
            if (amf_get_string(p_flv_parser, s, buffer, MAX_KEY_LEN) < 0) {
                msg_dbg("err when parse amf string");
                return -1;
            }
            break;
        case AMF_DATA_TYPE_OBJECT: {
            end_mark = 0;
            while (n_stream_ftell(s) < (max_pos - 1) && (key_len = n_stream_read_be16(s)) > 0) {
                if (n_stream_read(s, buffer, key_len) != PARSER_SUCCESS)
                    return -1;
                buffer[key_len] = '\0';
                msg_dbg("key string is %s", buffer);
                if (amf_parse_object(p_flv_parser, buffer, max_pos, depth + 1) < 0) {
                    msg_dbg("err when parse amf data object");
                    return -1;  // if we couldn't skip, bomb out.
                }
            }
            end_mark = n_stream_read_byte(s);
            if (end_mark != AMF_END_OF_OBJECT) {
                msg_dbg("cannot get end of object(object) %d at %lld", end_mark, n_stream_ftell(s));
                return -1;
            }
            msg_dbg("Get end of object %d at %llu", end_mark, n_stream_ftell(s));
            break;
        }
        case AMF_DATA_TYPE_NULL:
        case AMF_DATA_TYPE_UNDEFINED:
        case AMF_DATA_TYPE_UNSUPPORTED:
            break;  // these take up no additional space
        case AMF_DATA_TYPE_MIXEDARRAY: {
            msg_dbg("parse amf data mixed array");
            end_mark = 0;
            n_stream_fskip(s, 4);  // skip 32-bit max array index
            while (n_stream_ftell(s) < (max_pos - 1) &&
                   amf_get_string(p_flv_parser, s, buffer, MAX_KEY_LEN) > 0) {
                // this is the only case in which we would want a nested parse to not skip over the
                // object
                if (amf_parse_object(p_flv_parser, buffer, max_pos, depth + 1) < 0) {
                    msg_dbg("err when parse amf data mixed array");
                    return -1;
                }
            }
            if (n_stream_ftell(s) < max_pos) {
                end_mark = n_stream_read_byte(s);
                if (end_mark != AMF_END_OF_OBJECT) {
                    msg_dbg("cannot get end of object(mixed array) %d", end_mark);
                    return -1;
                }
            }

            break;
        }
        case AMF_DATA_TYPE_ARRAY: {
            uint32 array_len = 0;
            array_len = n_stream_read_be32(s);
            msg_dbg("array size is %d", array_len);
            if (key && !flv_strcmp(key, "filepositions")) {
                if (!p_flv_parser->vids_index.indexadjusted)
                    p_flv_parser->vids_index.entries_in_use = 0;
            } else if (key && !flv_strcmp(key, "times")) {
                if (!p_flv_parser->vids_index.indexadjusted)
                    p_flv_parser->vids_index.entries_in_use = 0;
            }

            for (i = 0; i < array_len && n_stream_ftell(s) < (max_pos - 1); i++) {
                if (amf_parse_object(p_flv_parser, buffer, max_pos, depth + 1) <
                    0)  // NULL --> buffer
                {
                    msg_dbg("err when parse amf data array");
                    return -1;  // if we couldn't skip, bomb out.
                }
            }
            break;
        }

        case AMF_DATA_TYPE_DATE:
            n_stream_fskip(s, 8 + 2);  // timestamp (double) and UTC offset (int16)
            break;
        default:  // unsupported type, we couldn't skip
            msg_dbg("unsupported amf type %d", type);
            return -1;
    }

    if (key != NULL) {
        // only look for metadata values when we are not nested and key != NULL
        if (type == AMF_DATA_TYPE_BOOL) {
            if (!flv_strcmp(key, "stereo") && audio_present) {
                p_audio_info->channel = num_val > 0 ? 2 : 1;
                msg_dbg("channal is %u", p_audio_info->channel);
            }
            if (!flv_strcmp(key, "hasKeyframes")) {
                msg_dbg("has key frames %u", (uint32)num_val);
            }
            if (!flv_strcmp(key, "hasVideo")) {
                msg_dbg("has Video %u", (uint32)num_val);
            }
            if (!flv_strcmp(key, "hasAudio")) {
                msg_dbg("has Audio %u", (uint32)num_val);
            }
            if (!flv_strcmp(key, "hasMetadata")) {
                msg_dbg("has Metadata %d", (uint32)num_val);
            }
            if (!flv_strcmp(key, "canSeekToEnd")) {
                m->canSeekToEnd = (uint32)num_val;
            }
        } else if (type == AMF_DATA_TYPE_NUMBER) {
            if (!flv_strcmp(key, "duration")) {
                // use this num_val only when there is no valid duration.
                // if duration was got from seekDuration(), it should be more reliable than this
                // num_val
                if (p_flv_parser->stream_info.duration == 0)
                    p_flv_parser->stream_info.duration = (uint32)(num_val * FSL_AV_TIME_BASE);
                msg_dbg("%s %d duration is %d\n", __FUNCTION__, __LINE__,
                        p_flv_parser->stream_info.duration);
            } else if (!flv_strcmp(key, "width") && video_present && num_val > 0) {
                p_video_info->width = (uint32)num_val;
                msg_dbg("width is %d", p_video_info->width);
            } else if (!flv_strcmp(key, "height") && video_present && num_val > 0) {
                p_video_info->height = (uint32)num_val;
                msg_dbg("height is %d", p_video_info->height);
            } else if (!flv_strcmp(key, "audiocodecid") && audio_present) {
                flv_set_audio_codec(p_audio_info, (int32)num_val);
            } else if (!flv_strcmp(key, "videocodecid") && video_present) {
                flv_set_video_codec(p_video_info, (int32)num_val);
            } else if (!flv_strcmp(key, "audiosamplesize") && audio_present) {
                p_audio_info->sampleSize = (uint32)num_val;
            } else if (!flv_strcmp(key, "audiosamplerate") && audio_present) {
                // some tools, like FLVTool2, write consistently approximate metadata sample rates
                switch ((int32)num_val) {
                    case 44000: {
                        p_audio_info->samplingRate = 44100;
                        break;
                    }
                    case 22000: {
                        p_audio_info->samplingRate = 22050;
                        break;
                    }
                    case 11000: {
                        p_audio_info->samplingRate = 11025;
                        break;
                    }
                    case 5000: {
                        p_audio_info->samplingRate = 5512;
                        break;
                    }
                    default: {
                        p_audio_info->samplingRate = (uint32)num_val;
                        break;
                    }
                }
                msg_dbg("samplingRate is %d", p_audio_info->samplingRate);
            } else if (!flv_strcmp(key, "filesize")) {
                m->filesize = (uint32)num_val;
                msg_dbg("set filesize to %u in amf_parse_object", m->filesize);
            } else if (!flv_strcmp(key, "audiodelay")) {
                // do nothing
            } else if (!flv_strcmp(key, "keyframes")) {
                msg_dbg("keyframes is %d", (uint32)num_val);
                m->keyframes = (uint32)num_val;
            } else if (!flv_strcmp(key, "audiodelay")) {
                msg_dbg("audio delay is %d", (uint32)num_val);
            } else if (!flv_strcmp(key, "videosize")) {
                msg_dbg("video size is %d", (uint32)num_val);
            } else if (!flv_strcmp(key, "framerate")) {
                msg_dbg("frame rate is %d", (uint32)num_val);
                m->framerate = (uint32)num_val;
            } else if (!flv_strcmp(key, "videodatarate")) {
                msg_dbg("video data rate is %d", (uint32)num_val);
                m->videodatarate = (uint32)num_val;
            } else if (!flv_strcmp(key, "audiosize")) {
                msg_dbg("audio size is %d", (uint32)num_val);
            } else if (!flv_strcmp(key, "lastkeyframelocation")) {
                m->lastkeyframelocation = (uint32)num_val;
                msg_dbg("last keyframe location is %d", m->lastkeyframelocation);
                msg_dbg("current %lld", n_stream_ftell(s));
            } else if (!flv_strcmp(key, "lastkeyframetimestamp")) {
                m->lastkeyframelocation = (uint32)(num_val * FSL_AV_TIME_BASE);
                msg_dbg("last keyframe timestamp is %d", m->lastkeyframelocation);
            } else if (!flv_strcmp(key, "lasttimestamp")) {
                m->lasttimestamp = (uint32)(num_val * FSL_AV_TIME_BASE);

            } else if (!flv_strcmp(key, "audiodatarate")) {
                msg_dbg("audio data rate is %d", (uint32)num_val);
                m->audiodatarate = (uint32)num_val;
            } else if (!flv_strcmp(key, "filepositions")) {
                if (!p_flv_parser->vids_index.indexadjusted)
                    if (p_flv_parser->vids_index.entries_in_use <
                        p_flv_parser->vids_index.entries_of_table) {
                        p_flv_parser->stream_info.seekable = 1;
                        p_flv_parser->vids_index.index[p_flv_parser->vids_index.entries_in_use]
                                .offset = (uint32)(num_val - 4);
                        p_flv_parser->vids_index.entries_in_use++;
                    }
            } else if (!flv_strcmp(key, "times")) {
                if (!p_flv_parser->vids_index.indexadjusted)
                    if (p_flv_parser->vids_index.entries_in_use <
                        p_flv_parser->vids_index.entries_of_table) {
                        p_flv_parser->vids_index.index[p_flv_parser->vids_index.entries_in_use]
                                .timestamp = (uint32)(num_val * FSL_AV_TIME_BASE);
                        p_flv_parser->vids_index.range_time = (uint32)(num_val * FSL_AV_TIME_BASE);
                        p_flv_parser->vids_index.entries_in_use++;
                    }
            }
        }
    }
    return 0;
}

/**
 *
 * @param p_flv_parser
 * @param next_pos
 *
 * @return FLVPARSER_ERR_CODE
 */
static FLVPARSER_ERR_CODE flv_parser_read_meta_tag(flv_parser_t* p_flv_parser, int64 next_pos) {
    AMF_Data_Type type;
    int8* buffer = p_flv_parser->metadata.keyname;
    FslFileStream* s = &(p_flv_parser->fileStream);

    type = n_stream_read_byte(s);

    if (type != AMF_DATA_TYPE_STRING) {
        msg_dbg("type is not AMF_DATA_TYPE_STRING");
        return PARSER_ERR_INVALID_MEDIA;
    }
    if (amf_get_string(p_flv_parser, s, buffer, MAX_KEY_LEN) < 0) {
        msg_dbg("can not get amf string");
        return PARSER_ERR_INVALID_MEDIA;
    }

    if (flv_strcmp(buffer, "onMetaData") && flv_strcmp(buffer, "onLastSecond")) {
        msg_dbg("Invalid string");
        //?removed by linzhongsong on Dec 13rd
    }

    msg_dbg("Get meta data end at %lld", next_pos);
    // parse the second object (we want a mixed array)
    if (amf_parse_object(p_flv_parser, buffer, next_pos, 0) < 0) {
        msg_dbg("parse amf object failed");
        n_stream_seek(s, next_pos, FSL_SEEK_SET);
    }
    msg_dbg("meta tag finished at %llu", n_stream_ftell(s));
    return PARSER_SUCCESS;
}

__attribute__((unused))
static FLVPARSER_ERR_CODE flv_get_extradata(flv_parser_t* p_flv_parser) {
    (void)p_flv_parser;
    return PARSER_SUCCESS;
}

__attribute__((unused))
static int32 flv_is_aac_hdr_tag(flv_parser_t* p_flv_parser, flv_tag_t* p_tag) {
    if (p_flv_parser->stream_info.audio_info.format == FLV_CODECID_AAC && p_tag->dataSize == 1) {
        return 1;
    }
    return 0;
}

__attribute__((unused))
static void flv_parser_update_time_range(flv_index_table_t* t, uint32 timestamp) {
    if (t->range_time <= timestamp) {
        t->range_time = timestamp;
    }
    return;
}

static FLVPARSER_ERR_CODE flv_parser_read_stream_info(flv_parser_t* p_flv_parser) {
    uint64 next_pos = p_flv_parser->offset;
    FslFileStream* s = &(p_flv_parser->fileStream);
    int audio_found = p_flv_parser->audio_found;
    int video_found = p_flv_parser->video_found;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    flv_tag_t tag;

    do {
        CheckForBail(n_stream_seek(s, next_pos, FSL_SEEK_SET));  // Jump to next tag
        CheckForBail(n_stream_check_left_bytes(
                s, FLV_TAG_HEADER_SIZE));  // notice 4(previous packet size) + 11(tag head)
        CheckForBail(flv_parser_search_tag_file_mode(p_flv_parser, &tag));

        // tag start position is changed when erroneous data is skipped in searching
        // so re-get it from current position
        next_pos = n_stream_ftell(s) - FLV_TAG_HEADER_SIZE;

        msg_dbg("===== read_stream_info tag type : 0x%x, dataSize %d, tag_cnt %d, pos is %lld, "
                "filesize %lld",
                tag.type, tag.dataSize, p_flv_parser->tag_cnt, next_pos, p_flv_parser->filesize);

        if (tag.dataSize == 0) {
            next_pos += (tag.dataSize + FLV_TAG_HEADER_SIZE);
            continue;
        }

        CheckForBail(n_stream_check_left_bytes(s, tag.dataSize));
        if (tag.type == FLV_TAG_TYPE_AUDIO && audio_found == 0) {
            flv_parser_read_audio_hdr(p_flv_parser);
            audio_found = 1;
            p_flv_parser->audio_found = 1;
            p_flv_parser->auds_timestamp_base = tag.timestamp;
            p_flv_parser->timestamp_base = p_flv_parser->vids_timestamp_base;
            msg_dbg("First audio offset %llu timstamp %d", next_pos, tag.timestamp);

            // set BodyOffset , when find first audio/video tag
            if (0 == video_found) {
                p_flv_parser->body_offset = next_pos;
                p_flv_parser->auds_chunk_pos = next_pos;
                p_flv_parser->vids_chunk_pos = next_pos;
            }

            msg_dbg("audio found!");
            // If audio format is aac, must find aac sequence header
            if (p_flv_parser->stream_info.audio_info.format == FLV_CODECID_AAC) {
                uint8 AACPacketType = (uint8)n_stream_read_byte(s);
                if (AACPacketType == AAC_SEQUENCE_HEADER) {
                    mp4AudioSpecificConfig mp4ASC;
                    uint8* decoder_info = NULL;
                    uint32 buffer_size = tag.dataSize - 2;
                    decoder_info = p_flv_parser->memOps.Malloc(buffer_size + BUF_PAD_SIZE);
                    if (decoder_info == NULL) {
                        return PARSER_INSUFFICIENT_MEMORY;
                    }
                    msg_dbg("aac specific info size is %d", buffer_size);
                    if (n_stream_read(s, decoder_info, buffer_size) == PARSER_SUCCESS) {
                        get_aac_specific_config(decoder_info, &mp4ASC);
                        p_flv_parser->stream_info.audio_info.channel = mp4ASC.channelsConfiguration;
                        p_flv_parser->stream_info.audio_info.samplingRate =
                                mp4ASC.samplingFrequency;
                        p_flv_parser->stream_info.audio_info.aac_specific_config.size = buffer_size;
                        if (p_flv_parser->stream_info.audio_info.aac_specific_config.data != NULL) {
                            p_flv_parser->memOps.Free(
                                    p_flv_parser->stream_info.audio_info.aac_specific_config.data);
                        }
                        p_flv_parser->stream_info.audio_info.aac_specific_config.data =
                                p_flv_parser->memOps.Malloc(buffer_size);
                        if (p_flv_parser->stream_info.audio_info.aac_specific_config.data == NULL) {
                            return PARSER_INSUFFICIENT_MEMORY;
                        }
                        memcpy(p_flv_parser->stream_info.audio_info.aac_specific_config.data,
                               decoder_info, buffer_size);
                    }
                    if (decoder_info != NULL) {
                        p_flv_parser->memOps.Free(decoder_info);
                    }

                } else {
                    audio_found = 0;
                    p_flv_parser->audio_found = 0;
                }
            }

        } else if (tag.type == FLV_TAG_TYPE_VIDEO && video_found == 0) {
            flv_parser_read_video_hdr(p_flv_parser);
            CheckForBail(flv_read_video_dimension(p_flv_parser, &tag));
            video_found = 1;
            p_flv_parser->video_found = 1;
            msg_dbg("video found!");
            p_flv_parser->vids_timestamp_base = tag.timestamp;
            p_flv_parser->timestamp_base = p_flv_parser->vids_timestamp_base;
            msg_dbg("First video offset %llu timstamp %d", next_pos, tag.timestamp);
            // set BodyOffset , when find first audio/video tag
            if (0 == audio_found) {
                p_flv_parser->body_offset = next_pos;
                p_flv_parser->auds_chunk_pos = next_pos;
                p_flv_parser->vids_chunk_pos = next_pos;
            }
        } else if (tag.type == FLV_TAG_TYPE_META) {
            CheckForBail(
                    flv_parser_read_meta_tag(p_flv_parser, (n_stream_ftell(s) + tag.dataSize)));
        }
        p_flv_parser->tag_cnt++;
        next_pos += (tag.dataSize + FLV_TAG_HEADER_SIZE);

        /*in erroneous stream, file header denotes no audio or no video, but in fact it has
           audio/video. So do not care value of audio_present and video_present, just search
           audio&video until MAX_SEARCH_TAG_NUM*/
        if ((audio_found == 1) && (video_found == 1)) {
            p_flv_parser->timestamp_base = p_flv_parser->vids_timestamp_base;
            if (p_flv_parser->auds_timestamp_base < p_flv_parser->vids_timestamp_base)
                p_flv_parser->timestamp_base = p_flv_parser->auds_timestamp_base;
            break;  // No need to continue;
        }

    } while (p_flv_parser->tag_cnt < MAX_SEARCH_TAG_NUM &&
             (next_pos + 4) < p_flv_parser->filesize);  // Finish searching

    if (p_flv_parser->isLive) {
        p_flv_parser->body_offset = next_pos;
    }

bail:
    p_flv_parser->offset = next_pos;  // Restore the position
    if (result == 0)
        n_stream_seek(s, next_pos, FSL_SEEK_SET);
    return result;
}

/**
 *
 * @param p_flv_handle
 * @param p_stream
 * @param p_callback
 *
 * @return
 */
// FLVPARSER_ERR_CODE flv_parser_open(flv_parser_t *p_flv_parser, file_stream_t *p_stream,
// ParserMemoryOps* p_callback)
FLVPARSER_ERR_CODE flv_parser_open(FslParserHandle* parserHandle, uint32 flags,
                                   FslFileStream* p_stream, ParserMemoryOps* pMemOps,
                                   ParserOutputBufferOps* pOutputOps, void* appContext) {
    flv_parser_t* p_flv_parser = NULL;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    const uint8 flag[] = "rb";
    bool isLive = FALSE;

    // new handle
    p_flv_parser = (flv_parser_t*)pMemOps->Malloc(sizeof(*p_flv_parser));
    if (p_flv_parser == NULL) {
        result = PARSER_INSUFFICIENT_MEMORY;
        goto bail;
    }

    memset(p_flv_parser, 0, sizeof(*p_flv_parser));

    if ((flags & FILE_FLAG_NON_SEEKABLE) && (flags & FILE_FLAG_READ_IN_SEQUENCE)) {
        isLive = TRUE;
    }

    p_flv_parser->flags = flags;
    p_flv_parser->isLive = isLive;
    p_flv_parser->fileHandle = p_stream->Open(NULL, flag, appContext); /* Open a file or URL */
    if (p_flv_parser->fileHandle == NULL) {
        result = PARSER_FILE_OPEN_ERROR;
        goto bail;
    }

    memcpy(&(p_flv_parser->fileStream), p_stream, sizeof(FslFileStream));
    memcpy(&(p_flv_parser->memOps), pMemOps, sizeof(ParserMemoryOps));
    memcpy(&(p_flv_parser->outputOps), pOutputOps, sizeof(ParserOutputBufferOps));

    p_flv_parser->appContext = appContext;

    p_flv_parser->historyBufLen = 0;
    p_flv_parser->p_HistoryBuf = NULL;

    p_flv_parser->tag_cnt = 0;
    p_flv_parser->audio_found = 0;
    p_flv_parser->video_found = 0;
    p_flv_parser->vids_timestamp = 0;
    p_flv_parser->auds_timestamp = 0;
    p_flv_parser->vids_timestamp_base = 0;
    p_flv_parser->auds_timestamp_base = 0;
    p_flv_parser->vids_first_sample = 0;
    p_flv_parser->auds_first_sample = 0;
    p_flv_parser->metadata.canSeekToEnd = 0;
    p_flv_parser->metadata.framerate = 0;
    p_flv_parser->metadata.videodatarate = 0;
    p_flv_parser->metadata.audiodatarate = 0;
    p_flv_parser->metadata.lasttimestamp = 0;
    p_flv_parser->metadata.lastkeyframetimestamp = 0;
    p_flv_parser->metadata.keyframes = 0;
    p_flv_parser->metadata.filesize = 0;

    p_flv_parser->stream_info.seekable = 0;
    p_flv_parser->stream_info.duration = 0;
    p_flv_parser->stream_info.audio_info.channel = 0;
    p_flv_parser->stream_info.audio_info.format = 0;
    p_flv_parser->stream_info.audio_info.sampleSize = 0;
    p_flv_parser->stream_info.audio_info.samplingRate = 0;
    p_flv_parser->stream_info.audio_info.aac_specific_config.size = 0;
    p_flv_parser->stream_info.audio_info.aac_specific_config.data = NULL;
    p_flv_parser->stream_info.video_info.codec = 0;
    p_flv_parser->stream_info.video_info.width = 0;
    p_flv_parser->stream_info.video_info.height = 0;
    p_flv_parser->stream_info.video_info.h264_codec_data.size = 0;
    p_flv_parser->stream_info.video_info.h264_codec_data.data = NULL;

    p_flv_parser->stream_corrupt = 0;
    p_flv_parser->vids_index.index = NULL;
    CheckForBail(flv_parser_read_header(p_flv_parser));

    p_flv_parser->vids_index.range_time = 0;
    p_flv_parser->vids_index.entries_in_use = 0;
    p_flv_parser->vids_index.entries_of_table = 0;
    // Create index table for playback

    p_flv_parser->vids_index.index =
            (flv_index_entry_t*)pMemOps->Malloc(FLV_INDEX_COUNT * sizeof(flv_index_entry_t));
    if (p_flv_parser->vids_index.index == NULL) {
        goto bail;
    }

    p_flv_parser->filesize = p_stream->Size(p_flv_parser->fileHandle, appContext);

    if (0 == p_flv_parser->filesize) {
        p_flv_parser->filesize = 0x100000000ULL;
        p_flv_parser->isLive = TRUE;
    }
    p_flv_parser->vids_index.entries_of_table = FLV_INDEX_COUNT;

    p_flv_parser->p_HistoryBuf = NULL;
    p_flv_parser->video_infolength = 0;
    p_flv_parser->video_info_sent = FALSE;

    p_flv_parser->isLastSyncFinished = TRUE;

    p_flv_parser->nal_length_size = 4;

    memset(&p_flv_parser->recovery_buf, 0, sizeof(recovery_buf_t));

    *parserHandle = p_flv_parser;

    result = flv_parser_read_stream_info(p_flv_parser);
    if (result)
        goto bail;

    if (p_flv_parser->stream_info.duration == 0) {
        if (p_flv_parser->vids_index.entries_in_use != 0) {
            p_flv_parser->stream_info.duration = p_flv_parser->vids_index.range_time;
        } else if (p_flv_parser->isLive == FALSE) {
            result = flv_parser_seekduration(p_flv_parser, &(p_flv_parser->stream_info.duration));
            if (result)
                goto bail;
        }
    }

    result = flv_parser_seek(p_flv_parser, 0, 0);
    if (result)
        goto bail;
    else {
        if (p_flv_parser->stream_info.duration > p_flv_parser->timestamp_base)
            p_flv_parser->stream_info.duration -= p_flv_parser->timestamp_base;
    }

    return result;
bail:
    *parserHandle = p_flv_parser;
    return result;
}

/**
 *
 * @param flv_handle
 */
FLVPARSER_ERR_CODE flv_parser_close(flv_parser_t* p_flv_parser) {
#if 1
    FslFileStream* s = &(p_flv_parser->fileStream);

    if (s->Close != NULL) {
        s->Close(p_flv_parser->fileHandle, p_flv_parser->appContext);
        p_flv_parser->fileHandle = NULL;
    }
    if (p_flv_parser->stream_info.audio_info.aac_specific_config.size > 0 &&
        p_flv_parser->stream_info.audio_info.aac_specific_config.data) {
        p_flv_parser->memOps.Free(p_flv_parser->stream_info.audio_info.aac_specific_config.data);
    }
    if (p_flv_parser->stream_info.video_info.h264_codec_data.size > 0 &&
        p_flv_parser->stream_info.video_info.h264_codec_data.data) {
        p_flv_parser->memOps.Free(p_flv_parser->stream_info.video_info.h264_codec_data.data);
    }
    // Close the file handler for each track if available
    if (p_flv_parser->p_HistoryBuf && p_flv_parser->memOps.Free != NULL)
        p_flv_parser->memOps.Free(p_flv_parser->p_HistoryBuf);
    p_flv_parser->p_HistoryBuf = NULL;

    flv_parser_rbf_deinit(p_flv_parser);

    CLEANUP();
#endif

    return PARSER_SUCCESS;
}

/**
 *
 * @param flv_handle
 *                   flv parser instance
 * @param p_stream_info
 *                   flv stream info
 *
 * @return
 */
FLVPARSER_ERR_CODE flv_parser_get_stream_info(flv_parser_t* p_flv_parser,
                                              flv_stream_info_t* p_stream_info) {
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;

    if (p_flv_parser == NULL || p_stream_info == NULL) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    CheckForBail(flv_parser_read_stream_info(p_flv_parser));

    p_stream_info->streamingable = 1;
    if (p_flv_parser->metadata.filesize == 0) {
        msg_dbg("FLV meta data do not contain file size info!");
        p_stream_info->streamingable = 0;
    }

    p_stream_info->duration = p_flv_parser->stream_info.duration;
    p_stream_info->audio_present = p_flv_parser->stream_info.audio_present;
    p_stream_info->video_present = p_flv_parser->stream_info.video_present;
    p_stream_info->seekable = p_flv_parser->stream_info.seekable;
    // Copy video info
    p_stream_info->video_info.codec = p_flv_parser->stream_info.video_info.codec;
    p_stream_info->video_info.width = p_flv_parser->stream_info.video_info.width;
    p_stream_info->video_info.height = p_flv_parser->stream_info.video_info.height;
    // Copy audio info
    p_stream_info->audio_info.channel = p_flv_parser->stream_info.audio_info.channel;
    p_stream_info->audio_info.samplingRate = p_flv_parser->stream_info.audio_info.samplingRate;
    p_stream_info->audio_info.sampleSize = p_flv_parser->stream_info.audio_info.sampleSize;
    p_stream_info->audio_info.format = p_flv_parser->stream_info.audio_info.format;

    // Get the file handler for each track
    /*
    FslFileStream* s = &(p_flv_parser->fileStream);
    if (p_flv_parser->stream_info.audio_present == TRUE)
    {
        file_stream_t *p_temp_stream = NULL;
        p_flv_parser->p_flv_audio_stream = (file_stream_t *)LocalMalloc(sizeof(file_stream_t));
        if (p_flv_parser->p_flv_audio_stream == NULL)
        {
            result = FLV_PARSER_ErrorInsufficientBuffer;
            msg_dbg("new p_flv_audio_stream failed");
            goto bail;
        }
        duplicate_file_stream(p_flv_parser->p_flv_stream, p_flv_parser->p_flv_audio_stream);

        p_temp_stream = p_flv_parser->p_flv_audio_stream;

        p_temp_stream->file_handle = (void *)NULL;

        if (p_temp_stream->pf_fopen(p_temp_stream, "rb", NULL) != 0)
        {
            result = PARSER_FILE_OPEN_ERROR;
            goto bail;
        }
    }

    if (p_flv_parser->stream_info.video_present == TRUE)
    {
        file_stream_t *p_temp_stream = NULL;
        p_flv_parser->p_flv_video_stream = (file_stream_t *)LocalMalloc(sizeof(file_stream_t));
        if (p_flv_parser->p_flv_video_stream == NULL)
        {
            result = FLV_PARSER_ErrorInsufficientBuffer;
            msg_dbg("new p_flv_video_stream failed");
            goto bail;
        }
        duplicate_file_stream(p_flv_parser->p_flv_stream, p_flv_parser->p_flv_video_stream);
        p_temp_stream = p_flv_parser->p_flv_video_stream;
        p_temp_stream->file_handle = (void *)NULL;

        if (p_temp_stream->pf_fopen(p_temp_stream, "rb", NULL) != 0)
        {
            result = PARSER_FILE_OPEN_ERROR;
            goto bail;
        }
    }
    */
    /*
    if (s != NULL)
    {
        s->pf_fclose(s, NULL);
        p_flv_parser->p_flv_stream = NULL;
    }
    */

bail:
    return result;
}

#define MAX_FLV_VIDEO_SIZE (512 * 1024)

FLVPARSER_ERR_CODE flv_parser_output_history_buffer(flv_parser_t* p_flv_parser, int* stream_type,
                                                    uint8** p_data, void** p_BufContext,
                                                    uint32* p_size, uint64* p_timestamp,
                                                    uint32* p_sync_flag) {
    if (p_flv_parser->historyBufLen > p_flv_parser->historyBufOffset) {
        ParserOutputBufferOps* pRequestBufferOps = &(p_flv_parser->outputOps);
        uint32 bufSize = p_flv_parser->historyBufLen - p_flv_parser->historyBufOffset;

        msg_dbg("history buffer output!%u\n", p_flv_parser->historyBufLen);

        *p_data = pRequestBufferOps->RequestBuffer(p_flv_parser->historyBufType, &bufSize,
                                                   p_BufContext, p_flv_parser->appContext);

        if (*p_data == NULL) {
            *p_BufContext = NULL;
            *p_size = 0;
            return PARSER_ERR_NO_OUTPUT_BUFFER;
        } else {
            if (bufSize > p_flv_parser->historyBufLen - p_flv_parser->historyBufOffset)
                bufSize = (p_flv_parser->historyBufLen - p_flv_parser->historyBufOffset);

            memcpy(*p_data, (p_flv_parser->p_HistoryBuf + p_flv_parser->historyBufOffset), bufSize);
            *p_size = bufSize;
            p_flv_parser->historyBufOffset += bufSize;
            *p_timestamp = p_flv_parser->historyTime;
            *p_sync_flag = p_flv_parser->historyFlag;
            *stream_type = p_flv_parser->historyBufType;

            if (p_flv_parser->historyBufLen == p_flv_parser->historyBufOffset) {
                p_flv_parser->historyBufLen = 0;
                p_flv_parser->historyBufOffset = 0;
                *p_sync_flag &= (~FLAG_SAMPLE_NOT_FINISHED);

            } else
                *p_sync_flag |= FLAG_SAMPLE_NOT_FINISHED;
        }
    }

    return PARSER_SUCCESS;
}

FLVPARSER_ERR_CODE flv_parser_output_NAL_unit(flv_parser_t* p_flv_parser, int stream_type,
                                              uint8** p_data, void** p_BufContext, uint32* p_size,
                                              uint32 tSize, uint32* p_sync_flag)

{
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    FslFileStream* s = &(p_flv_parser->fileStream);
    ParserOutputBufferOps* pRequestBufferOps = &(p_flv_parser->outputOps);
    uint8* p = NULL;
    uint32 offset = 0;
    uint32 out_length = 0;
    uint32 bufSize = tSize + 1024;
    bool isUsingHistory = FALSE;
    uint32 nal_length_size = (p_flv_parser->nal_length_size == 2) ? 2 : 4;

    *p_data = pRequestBufferOps->RequestBuffer(stream_type, &bufSize, p_BufContext,
                                               p_flv_parser->appContext);

    if (*p_data == NULL ||
        (bufSize < p_flv_parser->video_infolength && p_flv_parser->video_info_sent == FALSE)) {
        bufSize = tSize + 1024;
        if (p_flv_parser->p_HistoryBuf != NULL)
            LocalFree(p_flv_parser->p_HistoryBuf);
        p_flv_parser->p_HistoryBuf = LocalMalloc(bufSize);
        p_flv_parser->historyBufLen = 0;

        if (p_flv_parser->p_HistoryBuf == NULL) {
            result = PARSER_INSUFFICIENT_MEMORY;
            return result;
        }
        p = p_flv_parser->p_HistoryBuf;
        isUsingHistory = TRUE;
    } else
        p = *p_data;

    // will apply
    if (p_flv_parser->video_infolength > 0 && p_flv_parser->video_info_sent == FALSE) {
        memcpy(p, p_flv_parser->video_infobuffer, p_flv_parser->video_infolength);
        p += p_flv_parser->video_infolength;
        out_length = p_flv_parser->video_infolength;
        p_flv_parser->video_info_sent = TRUE;
    }

    while (offset < tSize) {
        uint32 nal_len = 0;
        uint32 start_length;  // if NO_CONVERTH, it's the tag length else it is the startcode length
        if (p_flv_parser->video_infolength ==
            0)  // if no codec data, do not read nal_len, just output whole video tag data
        {
            nal_len = tSize;
            nal_length_size = 0;
        } else {
            nal_len = (nal_length_size == 2) ? n_stream_read_be16(s) : n_stream_read_be32(s);
            msg_dbg("output_NAL_unit offset %d, tSize %d, nal_len %d", offset, tSize, nal_len);

            if (nal_length_size + nal_len + offset > tSize) {
                msg_dbg("nal_len invalid, read whole video tag");
                nal_len = tSize - offset - nal_length_size;
            }
        }

        if (p_flv_parser->flags & FLAG_H264_NO_CONVERT) {
            start_length = nal_length_size;
        } else {
            start_length = 4;
        }

        if (out_length + start_length + nal_len > bufSize) {
            if (isUsingHistory) {
                p_flv_parser->p_HistoryBuf =
                        LocalReAlloc(p_flv_parser->p_HistoryBuf, (bufSize + MAX_FLV_VIDEO_SIZE));
                if (p_flv_parser->p_HistoryBuf == NULL)
                    return PARSER_INSUFFICIENT_MEMORY;
                p = (p_flv_parser->p_HistoryBuf + out_length);

            } else {
                if (p_flv_parser->p_HistoryBuf != NULL)
                    LocalFree(p_flv_parser->p_HistoryBuf);
                p_flv_parser->p_HistoryBuf = LocalMalloc(tSize + 1024);
                if (p_flv_parser->p_HistoryBuf == NULL)
                    return PARSER_INSUFFICIENT_MEMORY;
                p = p_flv_parser->p_HistoryBuf;
                *p_size = out_length;
                isUsingHistory = TRUE;
                out_length = 0;
                bufSize = MAX_FLV_VIDEO_SIZE;
            }
        }

        if (!(p_flv_parser->flags & FLAG_H264_NO_CONVERT)) {
            p[0] = 0;
            p[1] = 0;
            p[2] = 0;
            p[3] = 1;
            p += 4;
        } else {
            if (nal_length_size == 2) {
                p[0] = (nal_len & 0xff00) >> 8;
                p[1] = nal_len & 0xff;
                p += 2;
            } else if (nal_length_size == 4) {
                p[0] = (nal_len & 0xff000000) >> 24;
                p[1] = (nal_len & 0xff0000) >> 16;
                p[2] = (nal_len & 0xff00) >> 8;
                p[3] = nal_len & 0xff;
                p += 4;
            }
        }

        CheckForBail(n_stream_read(s, p, nal_len));
        p += nal_len;
        offset += (nal_len + nal_length_size);
        if (p_flv_parser->flags & FLAG_H264_NO_CONVERT) {
            out_length += (nal_length_size + nal_len);
        } else {
            out_length += (4 + nal_len);
        }
    }

    if (!isUsingHistory)
        *p_size = out_length;
    else {
        *p_sync_flag |= FLAG_SAMPLE_NOT_FINISHED;
        p_flv_parser->historyBufLen = out_length;
        p_flv_parser->historyBufOffset = 0;
    }

    return PARSER_SUCCESS;
bail:

    if (result < 0 && *p_BufContext) {
        pRequestBufferOps->ReleaseBuffer(stream_type, *p_data, *p_BufContext,
                                         p_flv_parser->appContext);
        *p_data = *p_BufContext = NULL;
    }
    return result;
}

FLVPARSER_ERR_CODE flv_parser_get_file_next_sample(flv_parser_t* p_flv_parser, int* stream_type,
                                                   uint8** p_data, void** p_BufContext,
                                                   uint32* p_size, uint64* p_timestamp,
                                                   uint32* p_sync_flag) {
    FslFileStream* s = &(p_flv_parser->fileStream);
    flv_tag_t t;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    int32 found = 0;
    int32 eos = 0;
    int32 flags = 0;
    uint64 offset = 0;
    uint64 tag_start_pos = 0;

    if (p_flv_parser == NULL || p_data == NULL || p_size == NULL || p_sync_flag == NULL ||
        p_BufContext == NULL || p_timestamp == NULL) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *p_sync_flag = 0;
    *p_size = 0;
    *p_data = NULL;

    if (p_flv_parser->historyBufLen > 0)
        return flv_parser_output_history_buffer(p_flv_parser, stream_type, p_data, p_BufContext,
                                                p_size, p_timestamp, p_sync_flag);

    if (p_flv_parser->stream_corrupt == 1) {
        result = PARSER_ERR_INVALID_MEDIA;
        goto bail;
    }

    offset = n_stream_ftell(s);
    if ((offset + 4) == n_stream_total_size(s))  // 4 byte(last packet size)
    {
        eos = 1;
    }

START_SAMPLE:
    while (found == 0 && eos == 0) {
        uint32 data_size = 0;

        CheckForBail(flv_parser_search_tag_file_mode(p_flv_parser, &t));

        tag_start_pos = n_stream_ftell(s) - FLV_TAG_HEADER_SIZE;

        msg_dbg("===== get_file_next_sample tag pos %lld, type %d, dataSize %d", tag_start_pos,
                t.type, t.dataSize);

        if (t.dataSize == 0) {
            continue;
        }
        data_size = t.dataSize;
        CheckForBail(n_stream_check_left_bytes(s, t.dataSize));

        if (t.type != FLV_TAG_TYPE_META) {
            uint32 b_readed = 0;

            *stream_type = t.type;
            *stream_type = flv_parser_streamnum_from_type(p_flv_parser, *stream_type);
            //*stream_type = flv_                t.type
            // stream_type should be converted to streamnum

            flags = n_stream_read_byte(s);  // 1 byte of flags
            data_size -= 1;
            if (t.type == FLV_TAG_TYPE_VIDEO) {
                if (!p_flv_parser->video_enabled) {
                    CheckForBail(n_stream_fskip(s, t.dataSize - 1));
                    goto START_SAMPLE;
                }

                if ((flags & 0xf0) == 0x50) /* video info / command frame */
                {
                    msg_dbg("video info / command frame");
                }
                if ((flags & FLV_VIDEO_FRAME_TYPE_MASK) == FLV_FRAME_KEY) {
                    *p_sync_flag = FLAG_SYNC_SAMPLE;
#if 0
                    // bulid sync table on the fly
                    flv_parser_update_index_table(&p_flv_parser->vids_index, tag_start_pos, t.timestamp);
                    flv_parser_update_time_range(&p_flv_parser->vids_index, t.timestamp);
#endif
                }
                if ((flags & 0xf) == FLV_CODECID_H264) {
                    uint32 tmp =
                            n_stream_read_be32(s);  // AVCPacketType(UI8) + CompositionTime(UI24)
                    data_size -= 4;
                    switch ((tmp & 0xff000000) >> 24) {
                        case AVC_SEQUENCE_HEADER: {
                            if (data_size <= 0)
                                goto START_SAMPLE;
                            if (!(p_flv_parser->flags & FLAG_H264_NO_CONVERT)) {
                                result = flv_parser_get_avc_info2(p_flv_parser);
                            } else {
                                result = flv_h264_get_codec_data(p_flv_parser);
                            }
                            b_readed = 1;
                            goto START_SAMPLE;
                            break;
                        }

                        case AVC_NALU: {
                            result = flv_parser_output_NAL_unit(p_flv_parser, *stream_type, p_data,
                                                                p_BufContext, p_size, data_size,
                                                                p_sync_flag);

                            b_readed = 1;
                            break;
                        }

                        case AVC_END_OF_SEQUENCE:
                            msg_dbg("AVC end of sequence");
                            break;
                        default:
                            msg_dbg("invalid avc packet");
                            result = PARSER_ERR_INVALID_MEDIA;
                            goto bail;
                            break;
                    }
                } else if ((flags & 0xf) == FLV_CODECID_VP6 || (flags & 0xf) == FLV_CODECID_VP6A) {
                    // skip HorizontalAdjustment and VerticalAdjustment field
                    (void)n_stream_read_byte(s);
                    data_size -= 1;
                }
                p_flv_parser->vids_timestamp = t.timestamp - p_flv_parser->timestamp_base;
                t.timestamp = p_flv_parser->vids_timestamp;
            } else {
                *p_sync_flag = 0;
                // If audio format is aac, must find aac sequence header
                if (!p_flv_parser->audio_enabled) {
                    CheckForBail(n_stream_fskip(s, t.dataSize - 1));
                    goto START_SAMPLE;
                }
                if (((flags >> FLV_AUDIO_CODECID_OFFSET) & 0xf) == FLV_CODECID_AAC) {
                    uint8 AACPacketType = (uint8)n_stream_read_byte(s);
                    data_size -= 1;

                    if (AACPacketType == AAC_RAW_DATA) {
                        // do nothing
                    } else {
                        // This is a ADIF header, we shoudl ignore it.
                        CheckForBail(n_stream_fskip(s, data_size));
                        goto START_SAMPLE;
                    }
                }
                /*
                if (p_flv_parser->auds_first_sample == 0)
                {
                    p_flv_parser->auds_first_sample = 1;
                    p_flv_parser->auds_timestamp_base = t.timestamp;
                    msg_dbg("auds_timestamp_base is %d", p_flv_parser->auds_timestamp_base);
                }
                */
                p_flv_parser->auds_timestamp = t.timestamp - p_flv_parser->timestamp_base;
                t.timestamp = p_flv_parser->auds_timestamp;
            }

            if (b_readed == 0 && data_size > 0) {
                ParserOutputBufferOps* pRequestBufferOps = &(p_flv_parser->outputOps);
                uint32 bufSize;
                uint32 outDataSize;
                // Read raw data
                outDataSize = (data_size);  // flag(1 byte)
                bufSize = outDataSize;

                *p_data = pRequestBufferOps->RequestBuffer(*stream_type, &bufSize, p_BufContext,
                                                           p_flv_parser->appContext);

                // Only use necessary buffer
                if (bufSize > outDataSize)
                    bufSize = outDataSize;

                if (*p_data == NULL) {
                    if (p_flv_parser->p_HistoryBuf != NULL)
                        LocalFree(p_flv_parser->p_HistoryBuf);
                    bufSize = outDataSize;
                    p_flv_parser->p_HistoryBuf = LocalMalloc(bufSize);
                    if (p_flv_parser->p_HistoryBuf == NULL)
                        return PARSER_INSUFFICIENT_MEMORY;

                    CheckForBail(n_stream_read(s, p_flv_parser->p_HistoryBuf, bufSize));
                    *p_size = 0;
                    result = PARSER_ERR_NO_OUTPUT_BUFFER;
                    goto bail;

                } else {
                    CheckForBail(n_stream_read(s, *p_data, bufSize));
                    *p_size = bufSize;

                    if (bufSize < outDataSize) {
                        bufSize = (outDataSize - bufSize);
                        *p_sync_flag |= FLAG_SAMPLE_NOT_FINISHED;

                        if (p_flv_parser->p_HistoryBuf != NULL)
                            LocalFree(p_flv_parser->p_HistoryBuf);

                        p_flv_parser->p_HistoryBuf = LocalMalloc(bufSize);
                        if (p_flv_parser->p_HistoryBuf == NULL)
                            return PARSER_INSUFFICIENT_MEMORY;
                        CheckForBail(n_stream_read(s, p_flv_parser->p_HistoryBuf, bufSize));

                        p_flv_parser->historyBufLen = bufSize;
                        p_flv_parser->historyBufOffset = 0;
                    }
                }
            }

            found = 1;
            *p_timestamp = (uint64)t.timestamp * FSL_AV_TIME_BASE;

            if (p_flv_parser->historyBufLen > 0) {
                p_flv_parser->historyBufType = *stream_type;
                p_flv_parser->historyTime = *p_timestamp;
                *p_sync_flag |= FLAG_SAMPLE_NOT_FINISHED;
            } else
                *p_sync_flag &= (~FLAG_SAMPLE_NOT_FINISHED);
        }

        else if (t.type == FLV_TAG_TYPE_META) {
            CheckForBail(flv_parser_read_meta_tag(p_flv_parser, (n_stream_ftell(s) + t.dataSize)));
            offset = (tag_start_pos + t.dataSize + 4 + FLV_TAG_SIZE);
            CheckForBail(n_stream_seek(s, offset, FSL_SEEK_SET));
        } else {
            // skip chunk of other stream
            CheckForBail(n_stream_fskip(s, t.dataSize));
        }

        // check eos
        if (n_stream_total_size(s) == (n_stream_ftell(s) + 4)) {
            eos = 1;
        }
    }

    if (eos == 1 && *p_size == 0) {
        result = PARSER_EOS;
    }

    if (*p_timestamp > ((uint64)p_flv_parser->stream_info.duration * 1000)) {
        p_flv_parser->stream_info.duration = (uint32)(*p_timestamp / 1000);
    }

bail:

    /*
   if (result < 0)
   {
      *p_size = 0;
       p_flv_parser->stream_corrupt = 1;
       result = PARSER_EOS;
   }
   */
    if ((result == PARSER_NEED_MORE_DATA || result == PARSER_READ_ERROR ||
         result == PARSER_ERR_CONCEAL_FAIL) &&
        p_flv_parser->filesize > 0 &&
        (n_stream_ftell(s) + FLV_TAG_HEADER_SIZE) >= p_flv_parser->filesize) {
        msg_dbg("PARSER_EOS");
        result = PARSER_EOS;
    }

    return result;
}

// get last Random Access Table entry
static uint32 flv_parser_get_last_entry_ts(flv_index_table_t* t) {
    return t->range_time;
}

__attribute__((unused)) static uint32 flv_parser_update_index_table(flv_index_table_t* t, uint64 offset, uint32 timestamp) {
    msg_dbg("Get a key frame");
    /* Do we have space left in the table? */
    if (t->entries_in_use < t->entries_of_table) {
        if (timestamp >= flv_parser_get_last_entry_ts(t)) {
            /* Make the new entry */
            t->index[t->entries_in_use].timestamp = timestamp;
            t->index[t->entries_in_use].offset = offset;
            /* Increment the number of entries */
            t->entries_in_use++;
            msg_dbg("Key frame offset %llu timestamp %d", offset, timestamp);
        }
    }
    return 0;
}

static FLVPARSER_ERR_CODE flv_parser_find_first_video_tag_after_timestamp(
        flv_parser_t* p_flv_parser, uint32 timestamp) {
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;

    // do nothing now
    (void)p_flv_parser;
    (void)timestamp;

    return result;
}

static FLVPARSER_ERR_CODE flv_parser_search_index_table(flv_parser_t* p_flv_parser,
                                                        flv_index_table_t* t, uint32 timestamp,
                                                        uint32 flag, uint32* found_timestamp,
                                                        uint64* found_offset) {
    uint32 idx = 0;
    FLVPARSER_ERR_CODE result;

    while ((idx + 1) < t->entries_in_use) {
        if (t->index[idx + 1].timestamp > timestamp) {
            break;
        } else if (t->index[idx + 1].timestamp == timestamp) {
            idx++;
            break;
        }
        idx++;
    }
    if (SEEK_FLAG_NEAREST == flag) {
        if ((idx + 1) < t->entries_in_use)
            if ((timestamp - t->index[idx].timestamp) > (t->index[idx + 1].timestamp - timestamp))
                idx = idx + 1;
    } else if (SEEK_FLAG_NO_EARLIER == flag) {
        idx = idx + 1;
    }

    if (idx < t->entries_in_use) {
        *found_timestamp = t->index[idx].timestamp;
        *found_offset = t->index[idx].offset;

        if (p_flv_parser->video_found) {
        FIND_VIDEO_TAG:
            result = flv_parser_find_video_tag_after_offset(p_flv_parser, t->index[idx].offset);
            if (result)
                return result;

            if (SEEK_FLAG_NO_EARLIER == flag) {
                if ((p_flv_parser->vids_timestamp + p_flv_parser->timestamp_base) < timestamp) {
                    if ((idx + 1) < t->entries_in_use) {
                        idx += 1;
                        goto FIND_VIDEO_TAG;
                    } else
                        return PARSER_EOS;
                }
            } else if (SEEK_FLAG_NO_LATER == flag) {
                if ((p_flv_parser->vids_timestamp + p_flv_parser->timestamp_base) > timestamp) {
                    if (idx >= 1) {
                        idx -= 1;
                        goto FIND_VIDEO_TAG;
                    } else
                        return PARSER_BOS;
                }
            }

            *found_timestamp = p_flv_parser->vids_timestamp;
            *found_offset = p_flv_parser->vids_chunk_pos;
        }
    }
    // we have reached the end of the file
    else {
        return PARSER_EOS;
    }

    msg_dbg("target %d actual %u offset %llu\n", timestamp, *found_timestamp, *found_offset);
    msg_dbg("table use %d table index is %d\n", t->entries_in_use, idx);
    return 0;
}

__attribute__((unused)) static FLVPARSER_ERR_CODE flv_parser_find_audio_tag_before_timestamp(flv_parser_t* p_flv_parser,
                                                                     uint32 timestamp) {
    FslFileStream* s = &(p_flv_parser->fileStream);
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    int32 found = 0;
    uint32 tag_start_pos = 0;
    uint64 offset = p_flv_parser->vids_chunk_pos;  // find video neighbour chunk
    flv_tag_t t;
    uint8 tagBuffer[FLV_TAG_HEADER_SIZE];

    // find audio chunk backward
    while (found == 0) {
        CheckForBail(n_stream_seek(s, offset, FSL_SEEK_SET));
        tag_start_pos = (uint32)offset;
        CheckForBail(n_stream_check_left_bytes(s, (4 + FLV_TAG_SIZE)));
        CheckForBail(flv_parser_read_tag(p_flv_parser, s, &t, tagBuffer));  // 11btyes(tag header)
        if (t.type == FLV_TAG_TYPE_AUDIO) {
            if (t.timestamp <= timestamp) {
                found = 1;
            }
            p_flv_parser->auds_chunk_pos = tag_start_pos;
            p_flv_parser->auds_timestamp = t.timestamp;
        }
        msg_dbg("prev size %d", t.preSize);
        offset -= (t.preSize + 4);
    }
bail:
    return result;
}

FLVPARSER_ERR_CODE flv_parser_adjust_index(flv_parser_t* p_flv_parser, uint64 offset,
                                                  uint32 timestamp) {
    int64 distance;
    int64 fileoffset;
    uint32 idx, idx1;
    flv_tag_t t;
    uint8 tagBuffer[FLV_TAG_HEADER_SIZE];
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;

    flv_index_table_t* pTable = &(p_flv_parser->vids_index);

    idx = 0;
    while (idx < pTable->entries_in_use) {
        if (pTable->index[idx].timestamp / 1000 == timestamp / 1000) {
            if (pTable->index[idx].offset != offset) {
                distance = (int64)offset - (int64)pTable->index[idx].offset;

                for (idx1 = 0; idx1 < pTable->entries_in_use; idx1++) {
                    fileoffset = (int64)pTable->index[idx1].offset + distance;
                    if (fileoffset < (int64)offset)
                        fileoffset = (int64)offset;

                    CheckForBail(n_stream_seek(&(p_flv_parser->fileStream), fileoffset, SEEK_SET));
                    CheckForBail(n_stream_check_left_bytes(&p_flv_parser->fileStream,
                                                           (4 + FLV_TAG_SIZE)));
                    if (PARSER_SUCCESS == flv_parser_read_tag(p_flv_parser,
                                                              &p_flv_parser->fileStream, &t,
                                                              tagBuffer)) {
                        if (t.type == FLV_TAG_TYPE_VIDEO)
                            pTable->index[idx1].offset = (uint64)fileoffset;
                    }
                }
            }
            break;
        } else if (pTable->index[idx].timestamp > timestamp)
            break;
        idx++;
    }
    pTable->indexadjusted = 1;

bail:

    return result;
}

static FLVPARSER_ERR_CODE flv_parser_find_video_tag_after_offset(flv_parser_t* p_flv_parser,
                                                                 uint64 offset) {
    FslFileStream* s = &(p_flv_parser->fileStream);
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    int32 found = 0;
    int32 flags = 0;
    uint32 tag_start_pos = 0;
    flv_tag_t t;
    uint8 tagBuffer[FLV_TAG_HEADER_SIZE];

    // find video chunk forward
    while (found == 0) {
        bool bIsSyncPtr;
        CheckForBail(n_stream_seek(s, offset, FSL_SEEK_SET));
        CheckForBail(
                flv_parser_search_tag(p_flv_parser, s, &t, p_flv_parser->filesize, &bIsSyncPtr));
        offset = n_stream_ftell(s);
        tag_start_pos = (uint32)offset;
        CheckForBail(flv_parser_read_tag(p_flv_parser, s, &t, tagBuffer));
        flags = n_stream_read_byte(s);

        if (t.type == FLV_TAG_TYPE_VIDEO) {
            p_flv_parser->vids_chunk_pos = tag_start_pos;
            if (p_flv_parser->vids_first_sample == 0) {
                p_flv_parser->vids_timestamp_base = t.timestamp;
                p_flv_parser->vids_first_sample = 1;
            }
            if (p_flv_parser->vids_first_syncsample == 0 &&
                (flags & FLV_VIDEO_FRAME_TYPE_MASK) == FLV_FRAME_KEY &&
                p_flv_parser->stream_info.seekable) {
                flv_parser_adjust_index(p_flv_parser, offset, t.timestamp);
                p_flv_parser->vids_first_syncsample = 1;
            }

            p_flv_parser->vids_timestamp = t.timestamp - p_flv_parser->timestamp_base;
            found = 1;
            msg_dbg("auds offset %d timstamp %d", tag_start_pos, t.timestamp);
        }
        offset += (t.dataSize + 4 + FLV_TAG_SIZE);
    }
bail:
    return result;
}

static FLVPARSER_ERR_CODE flv_parser_find_audio_tag_after_offset(flv_parser_t* p_flv_parser,
                                                                 uint64 offset) {
    FslFileStream* s = &(p_flv_parser->fileStream);
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    int32 found = 0;
    uint32 tag_start_pos = 0;
    flv_tag_t t;
    uint8 tagBuffer[FLV_TAG_HEADER_SIZE];

    // find audio chunk forward
    while (found == 0) {
        CheckForBail(n_stream_seek(s, offset, FSL_SEEK_SET));
        tag_start_pos = (uint32)offset;
        CheckForBail(n_stream_check_left_bytes(s, (4 + FLV_TAG_SIZE)));
        CheckForBail(flv_parser_read_tag(p_flv_parser, s, &t, tagBuffer));  // 11btyes(tag header)
        if (t.type == FLV_TAG_TYPE_AUDIO) {
            p_flv_parser->auds_chunk_pos = tag_start_pos;
            if (p_flv_parser->auds_first_sample == 0) {
                p_flv_parser->auds_timestamp_base = t.timestamp;
                p_flv_parser->auds_first_sample = 1;
            }

            p_flv_parser->auds_timestamp = t.timestamp - p_flv_parser->timestamp_base;
            found = 1;
            msg_dbg("auds offset %d timstamp %d", tag_start_pos, t.timestamp);
        }
        offset += (t.dataSize + 4 + FLV_TAG_SIZE);
    }
bail:
    return result;
}

FLVPARSER_ERR_CODE flv_parser_get_current_position(flv_parser_t* p_flv_parser, int stream_type,
                                                   uint64* p_timestamp) {
    if (p_flv_parser == NULL) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if (stream_type == FLV_AUDIO_SAMPLE) {
        *p_timestamp = (uint64)p_flv_parser->auds_timestamp * FSL_AV_TIME_BASE;
    } else if (stream_type == FLV_VIDEO_SAMPLE) {
        *p_timestamp = (uint64)p_flv_parser->vids_timestamp * FSL_AV_TIME_BASE;
    } else {
        return PARSER_ERR_INVALID_PARAMETER;
    }
    return PARSER_SUCCESS;
}

__attribute__((unused)) static void flv_vuds_table_dump(flv_index_table_t* t) {
    uint32 i = 0;
    for (i = 0; i < t->entries_in_use; i++) {
        msg_dbg("timestamp = %d, offset = %llu", t->index[i].timestamp, t->index[i].offset);
    }
    return;
}

FLVPARSER_ERR_CODE flv_parser_seek(flv_parser_t* p_flv_parser, uint32 timestamp, uint32 flag) {
    uint64 offset = 0;
    uint32 actual_target_time = 0;
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;

    if (p_flv_parser == NULL) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    msg_dbg("flv_parser_seek() seek to %d duration is %d\n", timestamp,
            p_flv_parser->stream_info.duration);
    // seek to the end
    if (timestamp > p_flv_parser->stream_info.duration + p_flv_parser->timestamp_base)
        return PARSER_EOS;

    if (timestamp == 0) {
        p_flv_parser->vids_chunk_pos = p_flv_parser->body_offset;
        p_flv_parser->auds_chunk_pos = p_flv_parser->body_offset;
        p_flv_parser->auds_timestamp = 0;
        p_flv_parser->vids_timestamp = 0;
        // we should set time base here...
        // in this case, stream is seekable...
        if (p_flv_parser->vids_first_sample == 0 && (p_flv_parser->video_found)) {
            result =
                    flv_parser_find_video_tag_after_offset(p_flv_parser, p_flv_parser->body_offset);
            if (result)
                return result;
            else
                p_flv_parser->timestamp_base = p_flv_parser->vids_timestamp_base;
        }
        if (p_flv_parser->auds_first_sample == 0 && (p_flv_parser->audio_found)) {
            result =
                    flv_parser_find_audio_tag_after_offset(p_flv_parser, p_flv_parser->body_offset);
            if (result)
                return result;
            else {
                if (p_flv_parser->vids_first_sample == 1 && (p_flv_parser->video_found))
                    p_flv_parser->timestamp_base =
                            p_flv_parser->auds_timestamp_base < p_flv_parser->vids_timestamp_base
                                    ? p_flv_parser->auds_timestamp_base
                                    : p_flv_parser->vids_timestamp_base;
                else
                    p_flv_parser->timestamp_base = p_flv_parser->auds_timestamp_base;
            }
        }
        /*	 p_flv_parser->timestamp_base = p_flv_parser->auds_timestamp_base <
           p_flv_parser->vids_timestamp_base?p_flv_parser->auds_timestamp_base:p_flv_parser->vids_timestamp_base;
             if (p_flv_parser->stream_info.duration > p_flv_parser->timestamp_base)
                p_flv_parser->stream_info.duration -= p_flv_parser->timestamp_base;*/
        CheckForBail(n_stream_seek(&(p_flv_parser->fileStream), p_flv_parser->body_offset,
                                   FSL_SEEK_SET));
        return PARSER_SUCCESS;
    }

    if (p_flv_parser->vids_index.entries_in_use == 0) {
        uint64 syncoffset;
        uint32 synctime;
        bool syncfound;
        uint32 MINI_OFFSET = 2 * FLV_TAG_SIZE + 4;

        printf("No index table!");

        if (p_flv_parser->stream_info.duration == 0)
            return PARSER_ERR_NOT_SEEKABLE;
        if (p_flv_parser->filesize == 0)
            return PARSER_ERR_NOT_SEEKABLE;
        if (timestamp == p_flv_parser->auds_timestamp || timestamp == p_flv_parser->vids_timestamp)
            return PARSER_SUCCESS;

        syncoffset = p_flv_parser->filesize * (timestamp - p_flv_parser->timestamp_base) /
                     p_flv_parser->stream_info.duration;

    SYNCPOSITION:
        CheckForBail(n_stream_seek(&(p_flv_parser->fileStream), syncoffset, SEEK_SET));
        if (p_flv_parser->video_enabled &&
            p_flv_parser->stream_info.video_info.codec == FLV_CODECID_H263)
            result = flv_seek_video_key_frame(p_flv_parser, &synctime,
                                              (p_flv_parser->filesize - MINI_OFFSET), &syncfound);
        else
            result = flv_seek_sync_point(p_flv_parser, &synctime,
                                         (p_flv_parser->filesize - MINI_OFFSET), &syncfound);
        if (result != PARSER_SUCCESS) {
            msg_dbg("flv_seek_sync_point return error code 0x%08x \r\n", result);
            p_flv_parser->auds_chunk_pos = p_flv_parser->vids_chunk_pos =
                    n_stream_ftell(&(p_flv_parser->fileStream));
            p_flv_parser->vids_timestamp = p_flv_parser->stream_info.duration;
            p_flv_parser->auds_timestamp = p_flv_parser->stream_info.duration;
            n_stream_seek(&(p_flv_parser->fileStream), 0, SEEK_END);
            return PARSER_EOS;
        }
        if (!syncfound) {
            goto SYNCPOSITION;
        }
        return result;
    }

    if (flv_parser_find_first_video_tag_after_timestamp(p_flv_parser, timestamp) ==
        PARSER_SUCCESS) {
        result = flv_parser_search_index_table(p_flv_parser, &p_flv_parser->vids_index, timestamp,
                                               flag, &actual_target_time, &offset);
        if (PARSER_EOS == result) {
            p_flv_parser->auds_chunk_pos = p_flv_parser->vids_chunk_pos =
                    n_stream_ftell(&(p_flv_parser->fileStream));
            p_flv_parser->vids_timestamp = p_flv_parser->stream_info.duration;
            p_flv_parser->auds_timestamp = p_flv_parser->stream_info.duration;
            n_stream_seek(&(p_flv_parser->fileStream), 0, SEEK_END);
            return PARSER_EOS;
        } else if (PARSER_BOS == result) {
            p_flv_parser->auds_chunk_pos = p_flv_parser->vids_chunk_pos = p_flv_parser->offset;
            p_flv_parser->vids_timestamp = p_flv_parser->auds_timestamp = 0;
            n_stream_seek(&(p_flv_parser->fileStream), p_flv_parser->offset, SEEK_SET);
            return PARSER_BOS;

        } else if (PARSER_SUCCESS != result)
            return result;

        p_flv_parser->auds_timestamp = p_flv_parser->vids_timestamp;
    }

    CheckForBail(n_stream_seek(&(p_flv_parser->fileStream), offset, FSL_SEEK_SET));

    msg_dbg("after seek vids pos %llu auds pos %llu", p_flv_parser->vids_chunk_pos,
            p_flv_parser->auds_chunk_pos);
    msg_dbg("after seek vids ts %d auds ts %d", p_flv_parser->vids_timestamp,
            p_flv_parser->auds_timestamp);
bail:

    return result;
}

FLVPARSER_ERR_CODE flv_seek_sync_point(flv_parser_t* p_flv_parser, uint32* pTimeStamp,
                                              uint64 lastOffset, bool* pIsSyncFound) {
    // find the sync point, the beginning of a tag
    // and fetch the timestamp
    FslFileStream* s = &(p_flv_parser->fileStream);
    flv_tag_t tag;
    FLVPARSER_ERR_CODE result;

    msg_dbg("\nflv_seek_sync_point, lastOffset %llx", lastOffset);

    result = flv_parser_search_tag(p_flv_parser, s, &tag, lastOffset, pIsSyncFound);
    if (result)
        goto bail;

    if (*pIsSyncFound) {
        if (tag.timestamp < p_flv_parser->timestamp_base) {
            *pTimeStamp = 0;
            *pIsSyncFound = FALSE;
            goto bail;
        }
        tag.timestamp = tag.timestamp - p_flv_parser->timestamp_base;
        *pTimeStamp = tag.timestamp;
        //? is this right?
        p_flv_parser->auds_timestamp = tag.timestamp;
        p_flv_parser->vids_timestamp = tag.timestamp;
    } else
        *pTimeStamp = 0;

bail:

    msg_dbg("flv_seek_sync_point, end, result %d, pIsSyncFound %d", result, *pIsSyncFound);

    return result;
}

FLVPARSER_ERR_CODE flv_seek_video_key_frame(flv_parser_t* p_flv_parser, uint32* pTimeStamp,
                                                   uint64 lastOffset, bool* pIsSyncFound) {
    // find the sync point, the beginning of a tag
    // and fetch the timestamp
    FslFileStream* s = &(p_flv_parser->fileStream);
    flv_tag_t tag;
    FLVPARSER_ERR_CODE result;
    uint8 tagBuffer[FLV_TAG_HEADER_SIZE];
    int32 flags;

    msg_dbg("\nflv_seek_video_key_frame, lastOffset %llx\n", lastOffset);

    result = flv_parser_search_tag(p_flv_parser, s, &tag, lastOffset, pIsSyncFound);
    if (result)
        goto bail;

    bool found = FALSE;
    do {
        CheckForBail(flv_parser_read_tag(p_flv_parser, s, &tag, tagBuffer));
        if (tag.type == FLV_TAG_TYPE_VIDEO) {
            flags = n_stream_read_byte(s);
            msg_dbg("video tag found ts %d data %d flags 0x%x\n", tag.timestamp, tag.dataSize,
                    flags);
            if ((flags & FLV_VIDEO_FRAME_TYPE_MASK) == FLV_FRAME_KEY) {
                msg_dbg("found a key frame\n");
                found = TRUE;
            } else {
                msg_dbg("not a key frame\n");
            }
            CheckForBail(n_stream_seek(&(p_flv_parser->fileStream), -1, FSL_SEEK_CUR));
        }
        if (!found)
            CheckForBail(n_stream_seek(&(p_flv_parser->fileStream), tag.dataSize, FSL_SEEK_CUR));
        else
            CheckForBail(n_stream_seek(&(p_flv_parser->fileStream), -(FLV_TAG_HEADER_SIZE),
                                       FSL_SEEK_CUR));
    } while (found == FALSE);

    if (*pIsSyncFound) {
        if (tag.timestamp < p_flv_parser->timestamp_base) {
            *pTimeStamp = 0;
            *pIsSyncFound = FALSE;
            goto bail;
        }
        tag.timestamp = tag.timestamp - p_flv_parser->timestamp_base;
        *pTimeStamp = tag.timestamp;
        p_flv_parser->auds_timestamp = tag.timestamp;
        p_flv_parser->vids_timestamp = tag.timestamp;
    } else
        *pTimeStamp = 0;

bail:

    msg_dbg("flv_seek_video_key_frame, end, result %d, pIsSyncFound %d", result, *pIsSyncFound);

    return result;
}

/*
 * search last duration in stream
 *
 * search between file_end - 64K and file_end
 *         search last tag between this range
 * if not found, go to file_end - 64K*2 and file_end - 64K, search again
 */
#define SEEKDURATION_SEARCH_LOOP_MAX 100

FLVPARSER_ERR_CODE flv_parser_seekduration(flv_parser_t* p_flv_parser, uint32* pDuration) {
    uint32 step = 64000;          // 64Kbytes;
    const uint32 minigap = 2000;  // 2K

    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    uint32 duration = 0;
    uint32 lastfoundduration = 0;
    bool durationfound = FALSE;
    uint8 buf[FLV_TAG_SIZE + 4];
    uint8* pbuf = buf + 5;
    uint32 size;
    uint64 lastOffset = p_flv_parser->filesize;
    uint64 curOffset;
    uint32 syncfound = 0;
    uint32 search_cnt = 0;

    *pDuration = 0;
    msg_dbg("flv_parser_seekduration\n");

    if (p_flv_parser->filesize == 0)
        return result;

    while ((FALSE == durationfound) && (SEEKDURATION_SEARCH_LOOP_MAX >= search_cnt)) {
        search_cnt++;
        while (step > lastOffset) step = (step >> 1);

        curOffset = lastOffset - step;
        result = n_stream_seek(&(p_flv_parser->fileStream), curOffset, FSL_SEEK_SET);
        if (result != PARSER_SUCCESS)
            goto bail;

        syncfound = 0;

        if (curOffset <= minigap)
            break;

        while (curOffset < lastOffset) {
            result = flv_seek_sync_point(p_flv_parser, &duration, lastOffset, (int*)(&syncfound));
            if (result != PARSER_SUCCESS) {
                if (durationfound == TRUE)
                    result = 0;  // reset return value: ENGR160231
                break;
            }
            if (syncfound && lastfoundduration < duration) {
                lastfoundduration = duration;
                durationfound = TRUE;
            }

            if (!syncfound)
                break;

            curOffset = n_stream_ftell(&(p_flv_parser->fileStream));
            if (curOffset + minigap > lastOffset)
                break;

            n_stream_read(&(p_flv_parser->fileStream), buf, FLV_TAG_SIZE + 4);
            pbuf = buf + 5;  // reset pbuf as GetBe24 will update pbuf value
            GetBe24(size, pbuf);
            n_stream_seek(&(p_flv_parser->fileStream), curOffset + FLV_TAG_SIZE + 4 + size,
                          FSL_SEEK_SET);
        }

        lastOffset -= step;

        // if lastOffset too small, just breakout
        if (lastOffset < minigap)
            break;
    }

    if (durationfound)
        *pDuration = lastfoundduration;
    else {
        *pDuration = 0;
        if (SEEKDURATION_SEARCH_LOOP_MAX < search_cnt)
            result = 0;
    }

bail:
    return result;
}

static FLVPARSER_ERR_CODE flv_parser_rbf_init(flv_parser_t* p_flv_parser, uint32 size) {
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    msg_dbg("%s, size %d\n", __FUNCTION__, size);

    if (p_recovery_buf->pBuf != 0 && (int32)size <= p_recovery_buf->len) {
        msg_dbg("error: %s line %d, re-init recovery buffer to size %d!!!\n", __FUNCTION__,
                __LINE__, size);
        return PARSER_ERR_UNKNOWN;
    }

    if (p_recovery_buf->pBuf != 0) {
        // realloc buffer to bigger space and copy data
        // can not use ReAlloc() because existing data may not be continuous

        uint8* pNewBuf = (uint8*)p_flv_parser->memOps.Malloc(size);
        uint32 data_to_move;

        if (pNewBuf == 0) {
            return PARSER_INSUFFICIENT_MEMORY;
        }

        memset(pNewBuf, 0, size);
        data_to_move = p_recovery_buf->count;
        flv_parser_rbf_read(p_flv_parser, pNewBuf, data_to_move);
        p_flv_parser->memOps.Free(p_recovery_buf->pBuf);
        p_recovery_buf->pBuf = pNewBuf;

        p_recovery_buf->count = data_to_move;
        p_recovery_buf->read = 0;
        p_recovery_buf->write = data_to_move;
        p_recovery_buf->len = size;

    } else {
        p_recovery_buf->pBuf = (uint8*)p_flv_parser->memOps.Malloc(size);
        if (p_recovery_buf->pBuf == 0) {
            return PARSER_INSUFFICIENT_MEMORY;
        }

        memset(p_recovery_buf->pBuf, 0, size);

        p_recovery_buf->len = size;
        p_recovery_buf->count = 0;
        p_recovery_buf->read = 0;
        p_recovery_buf->write = 0;
    }
    return PARSER_SUCCESS;
}

static FLVPARSER_ERR_CODE flv_parser_rbf_deinit(flv_parser_t* p_flv_parser) {
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    msg_dbg("%s\n", __FUNCTION__);

    if (p_recovery_buf->pBuf != 0)
        p_flv_parser->memOps.Free(p_recovery_buf->pBuf);

    memset((void*)p_recovery_buf, 0, sizeof(recovery_buf_t));

    return PARSER_SUCCESS;
}

static FLVPARSER_ERR_CODE flv_parser_rbf_write_from_data(flv_parser_t* p_flv_parser, uint8* pData,
                                                         uint32 size) {
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    if ((int32)size > p_recovery_buf->len - p_recovery_buf->count) {
        msg_dbg("error: %s line %d, size %d overflow buffer space!!!\n", __FUNCTION__, __LINE__,
                size);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if ((int32)size <= p_recovery_buf->len - p_recovery_buf->write) {
        memcpy(p_recovery_buf->pBuf + p_recovery_buf->write, pData, size);
    } else {  // need wrap around
        uint32 first_write = p_recovery_buf->len - p_recovery_buf->write;
        memcpy(p_recovery_buf->pBuf + p_recovery_buf->write, pData, first_write);
        memcpy(p_recovery_buf->pBuf, pData + first_write, size - first_write);
    }

    p_recovery_buf->write += size;
    p_recovery_buf->write %= p_recovery_buf->len;
    p_recovery_buf->count += size;

    return result;
}

static FLVPARSER_ERR_CODE flv_parser_rbf_write_from_stream(flv_parser_t* p_flv_parser,
                                                           uint32 size) {
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    int32 actual_write = 0;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    if ((int32)size > p_recovery_buf->len - p_recovery_buf->count) {
        msg_dbg("error: %s line %d, size %d overflow buffer space!!!\n", __FUNCTION__, __LINE__,
                size);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    msg_dbg("%s, size %d\n", __FUNCTION__, size);

    if ((int32)size <= p_recovery_buf->len - p_recovery_buf->write) {
        actual_write = p_flv_parser->fileStream.Read(p_flv_parser->fileHandle,
                                                     p_recovery_buf->pBuf + p_recovery_buf->write,
                                                     size, p_flv_parser->appContext);
        if (actual_write < (int32)size) {
            msg_dbg("file read error: require %d, actual %d bytes", size, actual_write);
            return PARSER_READ_ERROR;
        }
    } else {  // need wrap around
        uint32 first_write = p_recovery_buf->len - p_recovery_buf->write;
        actual_write = p_flv_parser->fileStream.Read(p_flv_parser->fileHandle,
                                                     p_recovery_buf->pBuf + p_recovery_buf->write,
                                                     first_write, p_flv_parser->appContext);
        if (actual_write < (int32)first_write) {
            msg_dbg("file read error: require %d, actual %d bytes", first_write, actual_write);
            return PARSER_READ_ERROR;
        }

        actual_write = p_flv_parser->fileStream.Read(p_flv_parser->fileHandle, p_recovery_buf->pBuf,
                                                     size - first_write, p_flv_parser->appContext);
        if (actual_write < (int32)(size - first_write)) {
            msg_dbg("file read error: require %d, actual %d bytes", size - first_write,
                    actual_write);
            return PARSER_READ_ERROR;
        }
    }

    p_recovery_buf->write += size;
    p_recovery_buf->write %= p_recovery_buf->len;
    p_recovery_buf->count += size;

    return result;
}

static FLVPARSER_ERR_CODE flv_parser_rbf_read(flv_parser_t* p_flv_parser, uint8* pData,
                                              uint32 size) {
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    if (pData == 0 || size == 0)
        return PARSER_ERR_INVALID_PARAMETER;

    if ((int32)size > p_recovery_buf->count) {
        msg_dbg("error: %s line %d, size %d overflow buffer count!!!\n", __FUNCTION__, __LINE__,
                size);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if ((int32)size <= p_recovery_buf->len - p_recovery_buf->read) {
        memcpy(pData, p_recovery_buf->pBuf + p_recovery_buf->read, size);
    } else {  // need wrap around
        uint32 first_read = p_recovery_buf->len - p_recovery_buf->read;
        memcpy(pData, p_recovery_buf->pBuf + p_recovery_buf->read, first_read);
        memcpy(pData + first_read, p_recovery_buf->pBuf, size - first_read);
    }

    p_recovery_buf->read += size;
    p_recovery_buf->read %= p_recovery_buf->len;
    p_recovery_buf->count -= size;

    return result;
}

static FLVPARSER_ERR_CODE flv_parser_rbf_fskip(flv_parser_t* p_flv_parser, uint32 size) {
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    if (size == 0)
        return result;

    if ((int32)size > p_recovery_buf->count) {
        msg_dbg("error: %s line %d, size %d overflow buffer count!!!\n", __FUNCTION__, __LINE__,
                size);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    p_recovery_buf->read += size;
    p_recovery_buf->read %= p_recovery_buf->len;
    p_recovery_buf->count -= size;

    return result;
}

static FLVPARSER_ERR_CODE flv_parser_rbf_peek(flv_parser_t* p_flv_parser, uint8* pData,
                                              uint32 size) {
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;

    if (pData == 0 || size == 0)
        return PARSER_ERR_INVALID_PARAMETER;

    if ((int32)size > p_recovery_buf->count) {
        msg_dbg("error: %s line %d, size %d overflow buffer count!!!\n", __FUNCTION__, __LINE__,
                size);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if ((int32)size <= p_recovery_buf->len - p_recovery_buf->read) {
        memcpy(pData, p_recovery_buf->pBuf + p_recovery_buf->read, size);
    } else {  // need wrap around
        uint32 first_read = p_recovery_buf->len - p_recovery_buf->read;
        memcpy(pData, p_recovery_buf->pBuf + p_recovery_buf->read, first_read);
        memcpy(pData + first_read, p_recovery_buf->pBuf, size - first_read);
    }

    return result;
}

/*
 * search valide tag header in recovery buf until data count less than length of tag header
 * p_tag: output
 *            tag_t if valid tag header found
 */

static FLVPARSER_ERR_CODE flv_parser_rbf_search_tag(flv_parser_t* p_flv_parser, flv_tag_t* p_tag,
                                                    uint8* found) {
    FLVPARSER_ERR_CODE result = PARSER_SUCCESS;
    recovery_buf_t* p_recovery_buf = &p_flv_parser->recovery_buf;
    uint8* pHeader;
    uint8* pNextHeader;
    uint8 header[FLV_TAG_HEADER_SIZE];
    uint8* pTmp;
    flv_tag_t tag, nextTag;
    uint8 byte;
    uint8* pBufEnd = p_recovery_buf->pBuf + p_recovery_buf->len;

    *found = 0;

    msg_dbg("---------- start search tag ---------------\n");
    msg_dbg("recovery_buf: len %d, r %d, w %d, count %d\n", p_recovery_buf->len,
            p_recovery_buf->read, p_recovery_buf->write, p_recovery_buf->count);

    while (p_recovery_buf->count >= FLV_TAG_HEADER_SIZE) {
        pHeader = p_recovery_buf->pBuf + p_recovery_buf->read;

        if (pHeader + FLV_TAG_HEADER_SIZE > pBufEnd) {
            // a tag header is divided into two parts, collect them into header[];
            int first_read = (int)(pBufEnd - pHeader);
            memcpy(&header[0], pHeader, first_read);
            memcpy(&header[first_read], p_recovery_buf->pBuf, FLV_TAG_HEADER_SIZE - first_read);
            pTmp = header;
        } else
            pTmp = pHeader;

        if (!flv_parser_is_possible_tag(p_flv_parser, pTmp, &tag)) {
            flv_parser_rbf_read(p_flv_parser, &byte, 1);
            continue;
        }

        if ((int32)(tag.dataSize + FLV_TAG_HEADER_SIZE * 2) > p_recovery_buf->count) {
            // next tag header does not exist in recovery buf

            if ((int32)(tag.dataSize + FLV_TAG_HEADER_SIZE * 2) > p_recovery_buf->len) {
                // i don't believe this is a valid tag because data size is too big
                flv_parser_rbf_read(p_flv_parser, &byte, 1);
                continue;
            }

            // fill data from stream , until header of next tag
            flv_parser_rbf_write_from_stream(
                    p_flv_parser, tag.dataSize + FLV_TAG_HEADER_SIZE * 2 - p_recovery_buf->count);
        }

        // this seems like a tag, check next one

        pNextHeader = pHeader + FLV_TAG_HEADER_SIZE + tag.dataSize;
        if (pNextHeader >= pBufEnd)
            pNextHeader -= p_recovery_buf->len;

        if (pNextHeader + FLV_TAG_HEADER_SIZE > pBufEnd) {
            // a tag header is divided into two parts, collect them into header[];
            uint32 first_read = (uint32)(pBufEnd - pNextHeader);
            memcpy(&header[0], pNextHeader, first_read);
            memcpy(&header[first_read], p_recovery_buf->pBuf, FLV_TAG_HEADER_SIZE - first_read);
            pTmp = header;
        } else
            pTmp = pNextHeader;

        if (!flv_parser_is_possible_tag(p_flv_parser, pTmp, &nextTag)) {
            flv_parser_rbf_read(p_flv_parser, &byte, 1);
            continue;
        }

        if (nextTag.preSize != tag.dataSize + FLV_TAG_SIZE) {
            flv_parser_rbf_read(p_flv_parser, &byte, 1);
            continue;
        }

        *found = 1;
        memcpy(p_tag, &tag, sizeof(flv_tag_t));
        msg_dbg("\n!!! find a tag at buf pos %d, stream pos 0x%llx !!!\n\n", p_recovery_buf->read,
                n_stream_ftell(&p_flv_parser->fileStream));
        break;
    }

    if (*found == 0) {
        msg_dbg("!!! can not find a tag !!!\n");
    }

    return result;
}
