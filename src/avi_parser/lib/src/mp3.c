/*
***********************************************************************
* Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include <string.h>
#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "avi_parser_api.h"
#include "avi_utils.h"
#include "avi.h"
#include "mp3.h"

#define MAKE_TAG(ch0, ch1, ch2, ch3)                                                     \
    ((uint32)(uint8)(ch0) | ((uint32)(uint8)(ch1) << 8) | ((uint32)(uint8)(ch2) << 16) | \
     ((uint32)(uint8)(ch3) << 24))

typedef enum _MMF_MPEG_CHANNEL_MODE {
    MMF_STEREO = 0,
    MMF_JOINTSTEREO,
    MMF_DUALCHANNEL,   /*2, dual mono */
    MMF_SINGLE_CHANNEL /*3, mono */
} MMF_MPEG_CHANNEL_MODE;

/* Bitrate table*/
static short mp3_bit_rate[16][6] = {
        /* bitrate in kbps
         * MPEG-1 layer 1, 2, 3; MPEG-2&2.5 layer 1,2,3
         */
        {0, 0, 0, 0, 0, 0},
        {32, 32, 32, 32, 8, 8},
        {64, 48, 40, 48, 16, 16},
        {96, 56, 48, 56, 24, 24},
        {128, 64, 56, 64, 32, 32},
        {160, 80, 64, 80, 40, 40},
        {192, 96, 80, 96, 48, 48},
        {224, 112, 96, 112, 56, 56},
        {256, 128, 112, 128, 64, 64},
        {288, 160, 128, 144, 80, 80},
        {320, 192, 160, 160, 96, 96},
        {352, 224, 192, 176, 112, 112},
        {384, 256, 224, 192, 128, 128},
        {416, 320, 256, 224, 144, 144},
        {448, 384, 320, 256, 160, 160},
        {999, 999, 999, 999, 999, 999} /* reserved */

};

/* sampling rate table */
static unsigned int sampling_rate[4][3] = {
        {44100, 22050, 11025}, {48000, 24000, 12000}, {32000, 16000, 8000}, {99999, 99999, 99999}};

/* sample per frame table, MPEG-1, MPEG-2, MPEG-2.5 */
static unsigned int sample_per_frame[3][3] = {
        {384, 384, 384}, {1152, 1152, 1152}, {1152, 576, 576}};

/* size, in bytes, for layer 3 size information, MPEG-1, MPEG-2, MPEG-2.5 */
static unsigned int lay3_side_info_size[2][3] = {
        {32, 17, 17}, /* two channels */
        {17, 9, 9}    /* mono */
};

/*
0x0001 - Frames field is present
0x0002 - Bytes field is present
0x0004 - TOC field is present
0x0008 - Quality indicator field is present
*/
#define XING_FRAMES_PRESENT 0X0001
#define XING_BYTES_PRESENT 0X0002
#define XING_TOC_PRESENT 0X0004

#define XING_TOC_SIZE 100 /* in bytes */

static int32 mp3_dmx_read_xing_header(char* frame_buffer, int buf_size,
                                      mp3_audio_context* mpa_context);
static uint16 CalcCRC16(uint8* pBuffer, uint32 dwBitSize);

int32 mpa_check_next_frame_header(char* frame_buffer, int32 ref_mpeg_version, int32 ref_layer,
                                  int32 ref_bit_rate, int32 ref_sampling_frequency,
                                  int32 ref_nb_samples_per_frame, int32 ref_channel_mode);

/************************************************************************
mpa_parse_frame_header:
Search the 1st valid MP3 frame header in the given buffer.
Extract audio properties from the frame header and possible index info from VBR header (Xing header)

A MP3 audio frame header is valid when one of the following conditions is met:
(a) MP3 frame header has CRC and can pass CRC.
(b) Either 'Xing' or 'Info' header is found following the frame header.
(c) If neither of above conditions is met and "check_two_frames" is TRUE " ,
    the 2nd frame header with the same audio properites can be found with an offset same to the
frame size to the 1st frame header.

Arguments:
frame_buffer            [IN] input stream buffer
buf_size                   [IN] count of bytes of the input buffer. If not enough bytes are given,
searching may fail. check_two_frames    [IN] Whether to check the 2nd frame header if neither of
condition (a) (b) is net. At least two frame header shall be in the input buffer. It's important for
MP3 file parsing. Other parser can choose according to actual data size. mpa_context [OUT] audio
properties and index table got from frame header.
************************************************************************/
int32 mpa_parse_frame_header(char* frame_buffer, int buf_size, bool check_two_frames,
                             mp3_audio_context* mpa_context) {
    int32 err = AVI_ERR_NO_MP3_FRAME_FOUND; /* return value */

    int sync_mp3;
    int index_tmp = 0;
    int32 prev_frame_offset = 0; /* offset , in bytes, of the previously found frame header */
    int32 next_frame_offset = 0; /* offset , in bytes, of the next  frame header */
    uint8 header[4];
    int mpeg_version; /* 0 for MPEG-1, 1 for MPEG-2, 2 for MPEG-2.5 */
    int layer;        /* 1 for layer 1, 2 for layer 2, 3 for layer 3 */
    bool has_crc;
    int bit_rate_index;
    int bit_rate;  // bitrate in kbps
    int sampling_frequency;
    int sampling_frequency_index;
    int padding;
    int channel_mode;
    int nb_samples_per_frame = 0;
    int frame_size = 0;
    int channel_num;
    int buf_size2; /* original buffer size -3, frame header is 4 bytes long */

    int m_wBound = 32;      // only valid for intensity stereo (joint stereo)
    uint8 m_ModeExt;        // only valid for intensity stereo (joint stereo)
    uint32 dwProtectedBits; /* for crc */
    uint32 dwProtectBytes;
    uint16 check_sum16;
    uint16 check_sum_calculated;
    bool vbr_head_got = FALSE; /*whether Xing or Info header is found */
    uint32 vbr_head_tag = 0;
    uint32 xing_tag1 = MAKE_TAG('X', 'i', 'n', 'g');
    uint32 info_tag = MAKE_TAG('I', 'n', 'f', 'o'); /* usually for CBR audio */

    PARSERMSG("mpa: parse audio frame header ... data size %u \n", buf_size);
    memset(mpa_context, 0, sizeof(mp3_audio_context));
    mpa_context->cbr = TRUE; /* assume cbr */

    if (4 > buf_size)
        return PARSER_INSUFFICIENT_DATA;

    buf_size2 = buf_size - 3; /* don't exceed memory range, header is 4 bytes */
    while (index_tmp < buf_size2) {
        sync_mp3 = ((int)(frame_buffer[index_tmp] & 0x00FF) << 4) +
                   (int)((frame_buffer[index_tmp + 1] & 0x00E0) >> 4);

        if (MP3_SYNC == sync_mp3) {
            memcpy(header, &frame_buffer[index_tmp], 4);
            PARSERMSG("\nmpa: cur offset %d bytes\n", index_tmp);

            prev_frame_offset = index_tmp;
            vbr_head_got = FALSE; /* assume no vbr header */

            /* version,  then convert version code to index */
            mpeg_version = ((int)header[1] & 0x18) >> 3;
            if (3 == mpeg_version) {
                mpeg_version = 0;  // mpeg ver 1
                PARSERMSG("mpa: version MPEG-1\n");
            } else if (2 == mpeg_version) {
                mpeg_version = 1;  // mpeg ver 2
                PARSERMSG("mpa: version MPEG-2\n");
            } else if (0 == mpeg_version) {
                mpeg_version = 2;  // mpeg ver 2.5
                PARSERMSG("mpa: version MPEG-2.5\n");
            } else {
                PARSERMSG("mpa: Warnig!reseved version code\n");
                index_tmp++;
                continue;
            }

            /* layer */
            layer = 4 - (((int)header[1] & 0x06) >> 1);
            if (4 == layer) {
                PARSERMSG("mpa: Warnig!reseved layer code 0x%02x.\n", 4 - layer);
                index_tmp++;
                continue;
            } else if (1 == layer) {
                PARSERMSG("\t layer 1\n");
            } else if (2 == layer) {
                PARSERMSG("\t layer 2\n");
            } else if (3 == layer) {
                PARSERMSG("\t layer 3\n");
            }

            /* has crc ? */
            has_crc = !(((unsigned char)header[1]) & 0x01);
            PARSERMSG("\t has CRC ? %d\n", has_crc);
#if 0
            if (has_crc)
            {
                index_tmp++;
                continue;
            }
#endif

            /* bit rate, in kbps */
            bit_rate_index = ((int)header[2] & 0xF0) >> 4;
            if (mpeg_version == 0) /* mpeg-1 */
                bit_rate = mp3_bit_rate[bit_rate_index][mpeg_version + layer - 1];
            else /* mpeg-2/2.5 */
                bit_rate = mp3_bit_rate[bit_rate_index][3 + layer - 1];
            PARSERMSG("\t bitrate %d kbps\n", bit_rate);
            if (0 == bit_rate) {
                PARSERMSG("mpa: Warning!free bitrate (0), not support\n");
                index_tmp++;
                continue;

            } else if (bit_rate > 448) {
                PARSERMSG("mpa: exceed max bitrate 448 kbps\n");
                index_tmp++;
                continue;
            }

            /* sample rate */
            sampling_frequency_index = ((int)header[2] & 0x0C) >> 2;
            sampling_frequency = sampling_rate[sampling_frequency_index][mpeg_version];
            PARSERMSG("\t sample rate %d hz\n", sampling_frequency);
            if (sampling_frequency > 48000) {
                PARSERMSG("mpa: exceed max sample rate, not support\n");
                index_tmp++;
                continue;
            }

            /* samples per frame */
            nb_samples_per_frame = sample_per_frame[layer - 1][mpeg_version];
            PARSERMSG("\t %d samples/frame\n", nb_samples_per_frame);

            padding = ((int)header[2] & 0x02) >> 1;  // in Slots (always 1)

            //private_bit = ((int)header[2] & 0x01);

            channel_mode = ((int)header[3] & 0xC0) >> 6;

            frame_size = 0;
#ifdef MMF_ONLY_SUPPORT_MPEG1_LAYER3 /* old mpa only support mpeg-1 layer 3 */
            frame_size = (int)(144000 * bit_rate / sampling_frequency) + padding;
#else
            /* bit_rate never be 0 -> segmentation fault */
            frame_size =
                    (int)((uint64)nb_samples_per_frame * bit_rate / 8 * 1000 / sampling_frequency) +
                    padding;

            PARSERMSG("\t frame size %d\n", frame_size);
#endif

            if (MMF_SINGLE_CHANNEL == channel_mode) {
                channel_num = 1;
                PARSERMSG("\t 1 channel\n");
            } else {
                channel_num = 2;
                PARSERMSG("\t 2 channels\n");
            }

            // determine the bound for intensity stereo
            if (MMF_JOINTSTEREO == channel_mode) {
                // mode extension [bit 26,27], for jointstereo
                m_ModeExt = (uint8)((header[3] >> 4) & 0x03);
                m_wBound = 4 + m_ModeExt * 4;
            } else
                m_wBound = 32;

            /* check crc, difficult to decide the protect range for layer II */
            if ((has_crc) && (2 != layer)) {
                if (1 == layer) {
                    dwProtectedBits = 4 * (channel_num * m_wBound + (32 - m_wBound));
                } else { /*layer 3 */
                    dwProtectedBits = lay3_side_info_size[2 - channel_num][mpeg_version] * 8;
                }

                dwProtectedBits += 48;  // add  frame header(4 bytes) +CRC sum (2 bytes)

                dwProtectBytes = dwProtectedBits >> 3;
                if (dwProtectedBits & 0x7)
                    dwProtectBytes += 1;
                PARSERMSG("\t protect bits %u -> %u bytes\n", dwProtectedBits, dwProtectBytes);
                if (((index_tmp + 6) > buf_size) ||
                    ((index_tmp + (int)dwProtectBytes) > buf_size)) {
                    PARSERMSG("mpa: data not enough to check CRC\n");
                    return -1;
                }
                check_sum16 = ((uint16)(frame_buffer[index_tmp + 4])) << 8;
                check_sum16 += frame_buffer[index_tmp + 5];
                check_sum_calculated =
                        CalcCRC16((uint8*)(frame_buffer + index_tmp), dwProtectedBits);
                if (check_sum16 != check_sum_calculated) {
                    PARSERMSG("mpa: crc error. sum in stream 0x%04x, calculated 0x%04x\n",
                              check_sum16, check_sum_calculated);
                    index_tmp++;
                    continue;
                }
            }

            /* good mpeg audio frame header found */
            mpa_context->first_frame_offset = index_tmp;

            /* find VBR header (Xing header) for layer 3 audio */
            if (3 == layer) {
                index_tmp += 4; /* jump over frame header */
                if (has_crc)
                    index_tmp += 2; /* jump over check sum */
                index_tmp += lay3_side_info_size[2 - channel_num][mpeg_version];
                PARSERMSG("\t side info size %d bytes\n",
                          lay3_side_info_size[2 - channel_num][mpeg_version]);
                if ((index_tmp + 12) > buf_size) {
                    PARSERMSG("\t not enough data for xing header\n");
                } else {
                    vbr_head_tag =
                            MAKE_TAG(frame_buffer[index_tmp], frame_buffer[index_tmp + 1],
                                     frame_buffer[index_tmp + 2], frame_buffer[index_tmp + 3]);
                    if (vbr_head_tag == xing_tag1) /*tag 'Info' is used for CBR */
                    {
                        PARSERMSG("\t Xing head found!-> VBR\n");
                        mpa_context->cbr = FALSE;
                        vbr_head_got = TRUE;
                        mp3_dmx_read_xing_header(frame_buffer + index_tmp, buf_size - index_tmp,
                                                 mpa_context);
                    } else if (vbr_head_tag == info_tag) {
                        PARSERMSG("\t Info head found!-> CBR\n");
                        /*Already assume cbr in the beginning */
                        vbr_head_got = TRUE;
                    }
                }
            }

            if ((!has_crc) && (!vbr_head_got) &&
                check_two_frames) { /* for CBR audio, check next frame header */
                next_frame_offset = prev_frame_offset + frame_size;
                PARSERMSG("mpa: check next frame at offset %d...\n", next_frame_offset);
                if ((next_frame_offset + MPEG_AUDIO_FRAME_HEAD_SIZE) > buf_size) {
                    PARSERMSG("mpa: ERR! Not enough to check next frame header\n");
                    return err;  // MMF_FAILURE;
                }

                err = mpa_check_next_frame_header(frame_buffer + next_frame_offset, mpeg_version,
                                                  layer, bit_rate, sampling_frequency,
                                                  nb_samples_per_frame, channel_mode);

                if (PARSER_SUCCESS != err) {
                    PARSERMSG("mpa: Fail to check next frame header. Search frame header again\n");
                    index_tmp = prev_frame_offset + 1;
                    continue;
                }
            }

            PARSERMSG("mpa: Valid mp3 frame header got at offset %lld\n\n",
                      mpa_context->first_frame_offset);
            /*fill stream info */
            mpa_context->layer = layer;
            mpa_context->nb_channels = channel_num;
            mpa_context->bitrate = bit_rate * 1000;  // kbps
            mpa_context->sampling_frequency = sampling_frequency;

            mpa_context->nb_samples_per_frame = nb_samples_per_frame;
            mpa_context->frame_size = frame_size;
            err = PARSER_SUCCESS;
            break;
        } else
            index_tmp++;
    }

    return err;
}

static uint16 CalcCRC16(uint8* pBuffer, uint32 dwBitSize) {
    uint32 n;
    uint16 tmpchar, crcmask, tmpi;

    uint16 crc = 0xffff;  // start with inverted value of 0

    crcmask = tmpchar = 0;
    // start with byte 2 of header
    for (n = 16; n < dwBitSize; n++) {
        if (n < 32 || n >= 48)  // skip the 2 bytes of the crc itself
        {
            if (0 == (n & 0x07)) {
                crcmask = 1 << 8;
                tmpchar = pBuffer[n >> 3];
            }
            crcmask >>= 1;
            tmpi = crc & 0x8000;
            crc <<= 1;

            if (!tmpi ^ !(tmpchar & crcmask))
                crc ^= 0x8005;
        }
    }
    crc &= 0xffff;  // invert the result
    // crc ^= 0xffff;        // invert the result
    return crc;
}

/*********************************************************************************************
The TOC values are in the range between 0 and 256.
The 100 values stand for percentage of the full duration.
If you want to seek to time position 90% for example,
you take the toc value at position 90, divide it by 256 and multiply it by the file size.
This algorithm gives you the position in bytes to which you have to seek to.
For more details look at the method SeekPosition in Class CXINGHeader.
*********************************************************************************************/
static int32 mp3_dmx_read_xing_header(char* frame_buffer, int buf_size,
                                      mp3_audio_context* mpa_context) {
    int index_tmp = 0;
    uint32 vbr_flag;

    index_tmp += 4;  // jump over xing head tag
    /* flag, big endian, 4 bytes */
    PARSERMSG("\t xing flag: %x %x %x %x\n", frame_buffer[index_tmp], frame_buffer[index_tmp + 1],
              frame_buffer[index_tmp + 2], frame_buffer[index_tmp + 3]);
    vbr_flag = MAKE_TAG(frame_buffer[index_tmp + 3], frame_buffer[index_tmp + 2],
                        frame_buffer[index_tmp + 1], frame_buffer[index_tmp]);
    PARSERMSG("\t vbr flag 0x%x\n", (int32)vbr_flag);
    index_tmp += 4;  // jump over xing head flag

    if (XING_FRAMES_PRESENT & vbr_flag) {
        mpa_context->nb_frames = MAKE_TAG(frame_buffer[index_tmp + 3], frame_buffer[index_tmp + 2],
                                          frame_buffer[index_tmp + 1], frame_buffer[index_tmp]);
        index_tmp += 4;
        PARSERMSG("\t Total %d frames\n", mpa_context->nb_frames);
    }

    if (XING_BYTES_PRESENT & vbr_flag) {
        PARSERMSG("\t xing has byte field\n");
        if ((index_tmp + 4) > buf_size) {
            PARSERMSG("mpa: not enough data for xing data field\n");
            return PARSER_SUCCESS;  // NOT an error
        }
        mpa_context->media_size = MAKE_TAG(frame_buffer[index_tmp + 3], frame_buffer[index_tmp + 2],
                                           frame_buffer[index_tmp + 1], frame_buffer[index_tmp]);
        index_tmp += 4;
        PARSERMSG("\t xing: file size %lld bytes\n", mpa_context->media_size);
    }

    if (XING_TOC_PRESENT & vbr_flag) {
        PARSERMSG("\t xing has TOC field\n");
        if ((index_tmp + XING_TOC_SIZE) > buf_size) {
            PARSERMSG("mpa: not enough data for xing TOC field\n");
            return PARSER_SUCCESS;  // NOT an error
        }

        mpa_context->toc = LOCALMalloc(XING_TOC_SIZE);
        if (mpa_context->toc) {
            memcpy(mpa_context->toc, frame_buffer + index_tmp, XING_TOC_SIZE);

#ifdef DBG_MP3_DMX_SHOW_VBR_TOC
            {
                int i;
                /*display TOC */
                for (i = 0; i < XING_TOC_SIZE; i++) {
                    if (!(i & 0x03)) {
                        PARSERMSG("\n");
                    }
                    PARSERMSG("\t %d", *(mpa_context->toc + i));
                }
                PARSERMSG("\n");
            }
#endif
        } else {
            PARSERMSG("mpa: fail to alloc TOC\n");
        }
    }

    return PARSER_SUCCESS;
}

/* NOTE:make sure there are at least 4 bytes in the buffer for an entire mp3 audio frame header.
Check where a MP3 frame header starts from the buffer with the reference properties
*/
int32 mpa_check_next_frame_header(char* frame_buffer, int32 ref_mpeg_version, int32 ref_layer,
                                  int32 ref_bit_rate, int32 ref_sampling_frequency,
                                  int32 ref_nb_samples_per_frame, int32 ref_channel_mode) {
    int32 err = PARSER_ERR_UNKNOWN; /* return value */
    int sync_mp3;
    int index_tmp = 0;

    uint8 header[4];
    int mpeg_version; /* 0 for MPEG-1, 1 for MPEG-2, 2 for MPEG-2.5 */
    int layer;        /* 1 for layer 1, 2 for layer 2, 3 for layer 3 */
    int bit_rate_index;
    int bit_rate;  // bitrate in kbps
    int sampling_frequency;
    int sampling_frequency_index;
    int channel_mode;
    int nb_samples_per_frame = 0;

    sync_mp3 = ((int)(frame_buffer[index_tmp] & 0x00FF) << 4) +
               (int)((frame_buffer[index_tmp + 1] & 0x00E0) >> 4);

    if (MP3_SYNC != sync_mp3) {
        PARSERMSG("mpa: 2nd mp3 frame header NOT found!\n");
        return err;
    }

    memcpy(header, &frame_buffer[index_tmp], 4);

    /* version,  then convert version code to index */
    mpeg_version = ((int)header[1] & 0x18) >> 3;
    /*convert version before comparing */
    if (3 == mpeg_version) {
        mpeg_version = 0;  // mpeg ver 1
        PARSERMSG("mpa: version MPEG-1\n");
    } else if (2 == mpeg_version) {
        mpeg_version = 1;  // mpeg ver 2
        PARSERMSG("mpa: version MPEG-2\n");
    } else if (0 == mpeg_version) {
        mpeg_version = 2;  // mpeg ver 2.5
        PARSERMSG("mpa: version MPEG-2.5\n");
    } else {
        PARSERMSG("mpa: Warnig!reseved version code\n");
        return err;
    }

    if (mpeg_version != ref_mpeg_version) {
        PARSERMSG("mpa: mpeg version %d NOT match to refer value %d\n", mpeg_version,
                  ref_mpeg_version);
        return err;
    }

    /* layer */
    layer = 4 - (((int)header[1] & 0x06) >> 1);
    if (layer != ref_layer) {
        PARSERMSG("mpa: layer %d NOT match to refer value %d\n", layer, ref_layer);
        return err;
    }

    if (4 == layer) {
        PARSERMSG("mpa: Warnig!reseved layer code 0x%02x.\n", 4 - layer);
        return err;
    } else if (1 == layer) {
        PARSERMSG("\t layer 1\n");
    } else if (2 == layer) {
        PARSERMSG("\t layer 2\n");
    } else if (3 == layer) {
        PARSERMSG("\t layer 3\n");
    }

    /* bit rate, in kbps */
    bit_rate_index = ((int)header[2] & 0xF0) >> 4;
    if (mpeg_version == 0) /* mpeg-1 */
        bit_rate = mp3_bit_rate[bit_rate_index][mpeg_version + layer - 1];
    else /* mpeg-2/2.5 */
        bit_rate = mp3_bit_rate[bit_rate_index][3 + layer - 1];
    if (bit_rate != ref_bit_rate) {
        PARSERMSG("mpa: bitrate changes from value %d to %d -> VBR\n", ref_bit_rate, bit_rate);
    }

    if (0 == bit_rate) {
        PARSERMSG("mpa: Warning!free bitrate (0), not support\n");
        return err;

    } else if (bit_rate > 448) {
        PARSERMSG("mpa: exceed max bitrate 448 kbps\n");
        return err;
    }

    /* sample rate */
    sampling_frequency_index = ((int)header[2] & 0x0C) >> 2;
    sampling_frequency = sampling_rate[sampling_frequency_index][mpeg_version];
    PARSERMSG("\t sample rate %d hz\n", sampling_frequency);
    if (sampling_frequency != ref_sampling_frequency) {
        PARSERMSG("mpa: sample rate %d NOT match to refer value %d\n", sampling_frequency,
                  ref_sampling_frequency);
        return err;
    }

    if (sampling_frequency > 48000) {
        PARSERMSG("mpa: exceed max sample rate, not support\n");
        return err;
    }

    /* samples per frame */
    nb_samples_per_frame = sample_per_frame[layer - 1][mpeg_version];
    PARSERMSG("\t %d samples/frame\n", nb_samples_per_frame);
    if (nb_samples_per_frame != ref_nb_samples_per_frame) {
        PARSERMSG("mpa: samples/frame %d NOT match to refer value %d\n", nb_samples_per_frame,
                  ref_nb_samples_per_frame);
        return err;
    }

    channel_mode = ((int)header[3] & 0xC0) >> 6;
    if (channel_mode != ref_channel_mode) {
        PARSERMSG("mpa: channel mode %d NOT match to refer value %d\n", channel_mode,
                  ref_channel_mode);
        return err;
    }

    /* frame size may vary in 1 bytes, not check here */
    err = PARSER_SUCCESS;
    return err;
}
