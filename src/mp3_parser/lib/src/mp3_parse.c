/*
 ***********************************************************************
 * Copyright (c) 2005-2014, Freescale Semiconductor Inc.,
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#include <stdio.h>
#include <string.h>
#include "AudioCoreParser.h"
#include "mp3_parse.h"

#define IDENTIFIER_V1 "TAG"
#define IDENTIFIER_V2 "ID3"

/*MP3 frame header should be 0xFFE not 0xFFF,we also support mpeg audio 2.5*/
#define MP3_SYNC 0xFFE
#define MPEG_AUDIO_FRAME_HEAD_SIZE 4 /* size of mpeg audio frame header size, in bytes */

/*For parsing Xing header and find frame header more correctly,add CRC*/
#define MAKE_TAG(ch0, ch1, ch2, ch3)                                                   \
    ((unsigned char)(unsigned char)(ch0) | ((unsigned int)(unsigned char)(ch1) << 8) | \
     ((unsigned int)(unsigned char)(ch2) << 16) | ((unsigned int)(unsigned char)(ch3) << 24))
#define XING_FRAMES_PRESENT 0x01
#define XING_BYTES_PRESENT 0x02
#define XING_TOC_PRESENT 0x04

/*For parsing Xing header and find frame header more correctly,add CRC*/
/* Bitrate table*/
short mp3_bit_rate[16][6] = {
        /* bitrate in Kbps
         * MPEG-1 layer 1, 2, 3; MPEG-2 layer 1,2,3; MPEG-2.5 layer 1,2,3
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
        {999, 999, 999, 999, 999, 999}};

/* sampling rate table */
unsigned int sampling_rate[4][3] = {
        {44100, 22050, 11025}, {48000, 24000, 12000}, {32000, 16000, 8000}, {99999, 99999, 99999}};

/* sample per frame table */
unsigned int sample_per_frame[3][3] = {{384, 384, 84}, {1152, 1152, 1152}, {1152, 576, 576}};

/* size, in bytes, for layer 3 size information, MPEG-1, MPEG-2, MPEG-2.5 */
unsigned int lay3_side_info_size[2][3] = {
        {32, 17, 17}, /* two channels */
        {17, 9, 9}    /* mono */
};
__attribute__((unused))
static void clean_string(char* buf, int len) {
    int count = 0;

    for (count = 0; count < len; count++) {
        if (buf[count] == 0)
            buf[count] = 0x20;
    }

    buf[len] = '\0';
}

int mp3_parser_get_id3_v2_size(char* buf) {

    int size = 0;
    char frame_flags = 0;

    if (!strncmp(buf, IDENTIFIER_V2, 3)) {
        buf += 3;

        buf += 2;
        frame_flags = buf[0];

        buf += 1;
        size = ((int)(buf[0] << 21) + (int)(buf[1] << 14) + (int)(buf[2] << 7) + (int)buf[3]);

        if (frame_flags & 0x10) {
            size += 20;
        } else {
            size += 10;
        }
    }

    return size;
}

/*For parsing Xing header and find frame header more correctly,add CRC*/
__attribute__((unused))
static unsigned short CalcCRC16(unsigned char* pBuffer, unsigned int dwBitSize) {
    unsigned int n;
    unsigned short tmpchar, crcmask, tmpi;
    unsigned short crc = 0xffff; /* start with inverted value of 0  */

    crcmask = tmpchar = 0;
    /* start with byte 2 of header  */
    for (n = 16; n < dwBitSize; n++) {
        if (n < 32 || n >= 48) /* skip the 2 bytes of the crc itself  */
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
    crc &= 0xffff; /* invert the result */
    return crc;
}

FRAME_INFO mp3_parser_parse_frame_header(char* frame_buffer, int buf_size, FRAME_INFO* in_info) {
    int sync_mp3;
    int index_tmp = 0;
    char header[4];
    int mpeg_version;
    int layer;
    int bit_rate_index;
    int bit_rate;
    int sampling_frequency;
    int sampling_frequency_index;
    int padding;
    int channel_mode;
    int frame_size;
    FRAME_INFO ret;

    int channel_num;
    unsigned int vbr_head_tag = 0;
    unsigned int xing_tag = MAKE_TAG('X', 'i', 'n', 'g');
    unsigned int vbri_tag = MAKE_TAG('V', 'B', 'R', 'I');
    unsigned int lame_tag = MAKE_TAG('L', 'A', 'M', 'E');
    unsigned int Info_tag = MAKE_TAG('I', 'n', 'f', 'o');
    unsigned int Lavf_tag = MAKE_TAG('L', 'a', 'v', 'f');
    unsigned int Lavc_tag = MAKE_TAG('L', 'a', 'v', 'c');
    int prev_frame_offset = 0; /* offset , in bytes, of the previously found frame header */
    int next_frame_offset = 0; /* offset , in bytes, of the next  frame header */

    memset(&ret, 0, sizeof(ret));

    ret.index = buf_size; /* if can not find frame header, the index should be buf_size */
    while (index_tmp <= buf_size - 4) {
        ret.frm_size = 0;
        ret.xing_exist = 0;
        ret.vbri_exist = 0;
        ret.total_frame_num = 0;
        ret.total_bytes = 0;
        ret.flags = FLAG_SUCCESS;
        ret.index = buf_size; /* if can not find frame header, the index should be buf_size */
        sync_mp3 = ((int)(frame_buffer[index_tmp] & 0x00FF) << 4) +
                   (int)((frame_buffer[index_tmp + 1] & 0x00E0) >> 4);

        if (MP3_SYNC == sync_mp3) {
            prev_frame_offset = index_tmp;
            memcpy(header, &frame_buffer[index_tmp], 4);
            mpeg_version = ((int)header[1] & 0x18) >> 3;

            if (mpeg_version == 3)
                mpeg_version = 0; /* mpeg ver 1 */
            else if (mpeg_version == 2) {
                mpeg_version = 1; /* mpeg ver 2 */
            } else if (mpeg_version == 0) {
                mpeg_version = 2; /* mpeg ver 2.5 */
            }

            layer = 4 - (((int)header[1] & 0x06) >> 1);

            if ((3 != layer) && (2 != layer) && (1 != layer)) {
                index_tmp++;
                continue;
            }

            ret.version = mpeg_version;
            ret.layer = layer;

            bit_rate_index = ((int)header[2] & 0xF0) >> 4;
            if (mpeg_version == 0)
                bit_rate = mp3_bit_rate[bit_rate_index][mpeg_version + layer - 1];
            else
                bit_rate = mp3_bit_rate[bit_rate_index][3 + layer - 1];

            ret.b_rate = bit_rate;

            if (bit_rate > 448 || bit_rate == 0) {
                index_tmp++;
                continue;
            }

            sampling_frequency_index = ((int)header[2] & 0x0C) >> 2;
            sampling_frequency = sampling_rate[sampling_frequency_index][mpeg_version];

            ret.sampling_rate = sampling_frequency;
            ret.sample_per_fr = sample_per_frame[layer - 1][mpeg_version];

            if (sampling_frequency > 48000) {
                index_tmp++;
                continue;
            }
            if (in_info != NULL) {
                if (mpeg_version != (int)in_info->version || layer != (int)in_info->layer ||
                    sampling_frequency != (int)in_info->sampling_rate) {
                    index_tmp++;
                    continue;
                }
            }
            padding = ((int)header[2] & 0x02) >> 1;

            channel_mode = ((int)header[3] & 0xC0) >> 6;

            if (layer == 1)
                frame_size = (int)((12 * bit_rate * 1000) / sampling_frequency + padding) * 4;
            else
                frame_size = (int)((ret.sample_per_fr / 8 * bit_rate * 1000) / sampling_frequency) +
                             padding;

            ret.frm_size = frame_size;

            if (MAD_MODE_SINGLE_CHANNEL == channel_mode) {
                channel_num = 1;
            } else {
                channel_num = 2;
            }

            ret.channels = channel_num;

            ret.index = index_tmp;

            /* find VBR header (Xing header) for layer 3 audio */
            if (3 == layer) {
                index_tmp += 4; /* jump over frame header */
                index_tmp += lay3_side_info_size[2 - channel_num][mpeg_version];
                if ((index_tmp + 116) > buf_size) {
                    ret.flags = FLAG_NEEDMORE_DATA; /* need more data  */
                    return ret;
                } else {
                    vbr_head_tag =
                            MAKE_TAG(frame_buffer[index_tmp], frame_buffer[index_tmp + 1],
                                     frame_buffer[index_tmp + 2], frame_buffer[index_tmp + 3]);

                    if (vbr_head_tag == xing_tag || vbr_head_tag == Info_tag) {
                        char flag = frame_buffer[index_tmp + 7];
                        ret.xing_exist = 1;
                        index_tmp += 8;
                        if (flag & XING_FRAMES_PRESENT)  // Xing header frame number valid
                        {
                            /* Big Endian  */
                            ret.total_frame_num = MAKE_TAG(
                                    frame_buffer[index_tmp + 3], frame_buffer[index_tmp + 2],
                                    frame_buffer[index_tmp + 1], frame_buffer[index_tmp]);
                            index_tmp += 4;
                        }
                        if (flag & XING_BYTES_PRESENT)  // Xing header bytes number valid
                        {
                            // Big Endian
                            ret.total_bytes = MAKE_TAG(
                                    frame_buffer[index_tmp + 3], frame_buffer[index_tmp + 2],
                                    frame_buffer[index_tmp + 1], frame_buffer[index_tmp]);
                            index_tmp += 4;
                        }
                        if (flag & XING_TOC_PRESENT)  // Xing header TOC valid
                        {
                            memcpy(ret.TOC, frame_buffer + index_tmp, 100);
                            index_tmp += 100;
                        }
#ifdef PARSER_MP3_LAME_ENC_TAG
                        index_tmp += 4;  // skip Quality indicator
                        vbr_head_tag =
                                MAKE_TAG(frame_buffer[index_tmp], frame_buffer[index_tmp + 1],
                                         frame_buffer[index_tmp + 2], frame_buffer[index_tmp + 3]);
                        if (vbr_head_tag == lame_tag || vbr_head_tag == Lavf_tag ||
                            vbr_head_tag == Lavc_tag) {
                            int delay, enc_delay, enc_padding;

                            ret.lame_exist = 1;
                            index_tmp += 21;
                            // get encode delay and padding
                            delay = (unsigned int)((unsigned char)frame_buffer[index_tmp]);
                            delay = delay << 8;
                            delay += (unsigned int)((unsigned char)frame_buffer[index_tmp + 1]);
                            delay = delay << 8;
                            delay += (unsigned int)((unsigned char)frame_buffer[index_tmp + 2]);
                            enc_delay = delay >> 12;
                            enc_padding = delay & 0xFFF;
                            /* check for reasonable values (this may be an old Xing header, */
                            /* not a INFO tag) */
                            if (enc_delay < 0 || enc_delay > 3000)
                                enc_delay = -1;
                            if (enc_padding < 0 || enc_padding > 3000)
                                enc_padding = -1;
                            ret.enc_delay = enc_delay;
                            ret.enc_padding = enc_padding;
                        }
                        // else
#endif
                    } else if (vbr_head_tag == vbri_tag) {
                        int enc_delay;
                        ret.vbri_exist = 1;
                        index_tmp += 6;  // skip version ID
                        enc_delay = (unsigned int)((unsigned char)frame_buffer[index_tmp]);
                        enc_delay = enc_delay << 8;
                        enc_delay += (unsigned int)((unsigned char)frame_buffer[index_tmp + 1]);
#ifdef PARSER_MP3_LAME_ENC_TAG
                        ret.enc_delay = enc_delay;
#endif
                        index_tmp += 4;  // skip Quality indicator
                        ret.total_bytes =
                                MAKE_TAG(frame_buffer[index_tmp + 3], frame_buffer[index_tmp + 2],
                                         frame_buffer[index_tmp + 1], frame_buffer[index_tmp]);
                        index_tmp += 4;
                        ret.total_frame_num =
                                MAKE_TAG(frame_buffer[index_tmp + 3], frame_buffer[index_tmp + 2],
                                         frame_buffer[index_tmp + 1], frame_buffer[index_tmp]);
                    }
                }
            }

            if (in_info == NULL && !ret.xing_exist && !ret.vbri_exist) {
                /* for CBR audio, check next frame header */
                next_frame_offset = prev_frame_offset + frame_size;
                if ((next_frame_offset + MPEG_AUDIO_FRAME_HEAD_SIZE) > buf_size) {
                    ret.flags = FLAG_NEEDMORE_DATA;  // need more data
                    return ret;
                }

                sync_mp3 = ((int)(frame_buffer[next_frame_offset] & 0x00FF) << 4) +
                           (int)((frame_buffer[next_frame_offset + 1] & 0x00E0) >> 4);
                if (MP3_SYNC != sync_mp3) {
                    // poke 1 byte around next_frame_offset
                    sync_mp3 = ((int)(frame_buffer[next_frame_offset - 1] & 0x00FF) << 4) +
                               (int)((frame_buffer[next_frame_offset] & 0x00E0) >> 4);
                    if (MP3_SYNC != sync_mp3) {
                        sync_mp3 = ((int)(frame_buffer[next_frame_offset + 1] & 0x00FF) << 4) +
                                   (int)((frame_buffer[next_frame_offset + 2] & 0x00E0) >> 4);
                        if (MP3_SYNC == sync_mp3)
                            next_frame_offset++;
                    } else
                        next_frame_offset--;
                }

                if (mp3_check_next_frame_header(frame_buffer + next_frame_offset, mpeg_version,
                                                layer, sampling_frequency) != MP3_OK) {
                    index_tmp = prev_frame_offset + 1;
                    continue;
                }
            }
            break;
        } else
            index_tmp++;
    }

    return ret;
}

/* NOTE:make sure there are at least 4 bytes in the buffer for an entire mp3 audio frame header.
   Check where a MP3 frame header starts from the buffer with the reference properties
   */
mp3_err mp3_check_next_frame_header(char* frame_buffer, int ref_mpeg_version, int ref_layer,
                                    int ref_sampling_frequency) {
    mp3_err ret = MP3_ERR;
    int sync_mp3;
    int index_tmp = 0;
    char header[4];
    int mpeg_version; /* 0 for MPEG-1, 1 for MPEG-2, 2 for MPEG-2.5 */
    int layer;        /* 1 for layer 1, 2 for layer 2, 3 for layer 3 */
    int sampling_frequency;
    int sampling_frequency_index;

    sync_mp3 = ((int)(frame_buffer[index_tmp] & 0x00FF) << 4) +
               (int)((frame_buffer[index_tmp + 1] & 0x00E0) >> 4);
    if (MP3_SYNC != sync_mp3)
        return ret;

    memcpy(header, &frame_buffer[index_tmp], 4);
    mpeg_version = ((int)header[1] & 0x18) >> 3;

    if (mpeg_version == 3)
        mpeg_version = 0; /* mpeg ver 1 */
    else if (mpeg_version == 2) {
        mpeg_version = 1; /* mpeg ver 2 */
    } else if (mpeg_version == 0) {
        mpeg_version = 2; /* mpeg ver 2.5 */
    }

    if (mpeg_version != ref_mpeg_version)
        return ret;

    layer = 4 - (((int)header[1] & 0x06) >> 1);
    if (layer != ref_layer)
        return ret;

    sampling_frequency_index = ((int)header[2] & 0x0C) >> 2;
    sampling_frequency = sampling_rate[sampling_frequency_index][mpeg_version];

    if (sampling_frequency != ref_sampling_frequency)
        return ret;

    ret = MP3_OK;

    return ret;
}

#define FRAME_HEADER_SIZE 4

AUDIO_PARSERRETURNTYPE Mp3ParserFileHeader(AUDIO_FILE_INFO* pFileInfo, uint8* pBuffer,
                                           uint32 nBufferLen) {
    AUDIO_PARSERRETURNTYPE ret = AUDIO_PARSERRETURNSUCESS;

    pFileInfo->bIsCBR = FALSE;
    pFileInfo->bSeekable = TRUE;
    pFileInfo->bGotDuration = FALSE;
    pFileInfo->nFrameHeaderSize = FRAME_HEADER_SIZE;

    /** FIXEDME: Do we need parser Xing and VBRI header and TOC of MP3 */
    (void)pBuffer;
    (void)nBufferLen;

    return ret;
}

AUDIO_PARSERRETURNTYPE Mp3ParserFrame(AUDIO_FRAME_INFO* pFrameInfo, uint8* pBuffer,
                                      uint32 nBufferLen) {
    AUDIO_PARSERRETURNTYPE ret = AUDIO_PARSERRETURNSUCESS;

    pFrameInfo->bGotOneFrame = FALSE;
    pFrameInfo->nFrameHeaderConsumed = 0;
    pFrameInfo->nFrameSize = 0;
    pFrameInfo->nBitRate = 0;
    pFrameInfo->nSamplesPerFrame = 0;

    if (pFrameInfo->nFrameCount == 0) {
        pFrameInfo->FrameInfo = mp3_parser_parse_frame_header((char*)(pBuffer), nBufferLen, NULL);

        if (pFrameInfo->FrameInfo.flags == FLAG_NEEDMORE_DATA) {
            return ret;
        }
        if (pFrameInfo->FrameInfo.index >= nBufferLen) {
            pFrameInfo->nFrameHeaderConsumed = nBufferLen;
            return ret;
        }
        pFrameInfo->bGotOneFrame = TRUE;
        pFrameInfo->nFrameCount++;
        pFrameInfo->nFrameHeaderConsumed = pFrameInfo->FrameInfo.index + FRAME_HEADER_SIZE;
        pFrameInfo->nFrameSize = pFrameInfo->FrameInfo.frm_size - FRAME_HEADER_SIZE;
        pFrameInfo->nBitRate = pFrameInfo->FrameInfo.b_rate;
        pFrameInfo->nSamplesPerFrame = pFrameInfo->FrameInfo.sample_per_fr;
        pFrameInfo->nSamplingRate = pFrameInfo->FrameInfo.sampling_rate;
        pFrameInfo->nChannels = pFrameInfo->FrameInfo.channels;
#ifdef PARSER_MP3_LAME_ENC_TAG
        if (1 == pFrameInfo->FrameInfo.lame_exist) {
            pFrameInfo->lame_exist = 1;
            pFrameInfo->enc_delay = pFrameInfo->FrameInfo.enc_delay;
            pFrameInfo->enc_padding = pFrameInfo->FrameInfo.enc_padding;
        } else {
            pFrameInfo->lame_exist = 0;
            pFrameInfo->enc_delay = 0;
            pFrameInfo->enc_padding = 0;
        }
#endif
    } else {
        FRAME_INFO FrameInfoTmp;
        FrameInfoTmp = mp3_parser_parse_frame_header((char*)(pBuffer), nBufferLen,
                                                     &(pFrameInfo->FrameInfo));
        if (FrameInfoTmp.flags == FLAG_NEEDMORE_DATA) {
            return ret;
        }
        if (FrameInfoTmp.index >= nBufferLen) {
            pFrameInfo->nFrameHeaderConsumed = nBufferLen;
            return ret;
        }

        if (FrameInfoTmp.b_rate != pFrameInfo->FrameInfo.b_rate) {
            pFrameInfo->bIsCBR = FALSE;
        }
        pFrameInfo->bGotOneFrame = TRUE;
        pFrameInfo->nFrameCount++;
        pFrameInfo->nFrameHeaderConsumed = FrameInfoTmp.index + FRAME_HEADER_SIZE;
        pFrameInfo->nFrameSize = FrameInfoTmp.frm_size - FRAME_HEADER_SIZE;
        pFrameInfo->nBitRate = FrameInfoTmp.b_rate;
        pFrameInfo->nSamplesPerFrame = FrameInfoTmp.sample_per_fr;
        pFrameInfo->nSamplingRate = FrameInfoTmp.sampling_rate;
        pFrameInfo->nChannels = FrameInfoTmp.channels;
    }

    return ret;
}

AUDIO_PARSERRETURNTYPE Mp3GetFrameSize(uint32 header, uint32* frame_size, int* out_sampling_rate,
                                       int* out_channels, int* out_bitrate, int* out_num_samples) {
    if (frame_size == NULL || out_sampling_rate == NULL || out_bitrate == NULL ||
        out_num_samples == NULL) {
        return AUDIO_PARSERRETURNFAIL;
    } else if (((header >> 20) & MP3_SYNC) != MP3_SYNC) {
        return AUDIO_PARSERRETURNFAIL;
    }

    *frame_size = 0;
    *out_sampling_rate = 0;
    *out_bitrate = 0;
    *out_num_samples = 1152;

    if (out_channels) {
        *out_channels = 0;
    }

    unsigned int version = (header >> 19) & 3;
    unsigned int layer = (header >> 17) & 3;
    unsigned int bitrate_index = (header >> 12) & 0x0f;
    unsigned int sampling_rate_index = (header >> 10) & 3;
    unsigned int padding = (header >> 9) & 1;
    int channel_mode = (header >> 6) & 3;

    if (version == 0x01 || layer == 0x00 || sampling_rate_index == 3) {
        return AUDIO_PARSERRETURNFAIL;
    } else if (bitrate_index == 0 || bitrate_index == 0x0f) {
        // never allow "free" bitrate.
        return AUDIO_PARSERRETURNFAIL;
    }

    int sample_rate = sampling_rate[sampling_rate_index][0];
    if (version == 2 /* V2 */) {
        sample_rate /= 2;
    } else if (version == 0 /* V2.5 */) {
        sample_rate /= 4;
    }

    if (layer == 3) {
        // layer I
        int bitrate = (version == 3 /* V1 */) ? mp3_bit_rate[bitrate_index][0]
                                              : mp3_bit_rate[bitrate_index][3];
        *out_bitrate = bitrate;
        *frame_size = (12000 * bitrate / sample_rate + padding) * 4;
        *out_num_samples = 384;
    } else {
        // layer II or III
        int bitrate;
        if (version == 3 /* V1 */) {
            bitrate = (layer == 2 /* L2 */) ? mp3_bit_rate[bitrate_index][1]
                                            : mp3_bit_rate[bitrate_index][2];

            *out_num_samples = 1152;
        } else {
            // V2 (or 2.5)

            bitrate = mp3_bit_rate[bitrate_index][4];
            *out_num_samples = (layer == 1 /* L3 */) ? 576 : 1152;
        }
        *out_bitrate = bitrate;

        if (version == 3 /* V1 */) {
            *frame_size = 144000 * bitrate / sample_rate + padding;
        } else {
            // V2 or V2.5
            uint32 tmp = (layer == 1 /* L3 */) ? 72000 : 144000;
            *frame_size = tmp * bitrate / sample_rate + padding;
        }
    }

    *out_sampling_rate = sample_rate;
    if (out_channels) {
        *out_channels = (channel_mode == 3) ? 1 : 2;
    }

    return AUDIO_PARSERRETURNSUCESS;
}
