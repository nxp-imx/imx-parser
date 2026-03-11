/*
***********************************************************************
* Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include <stdlib.h>

#if !(defined(__WINCE) || defined(WIN32))  // Wince doesn't support stat method TLSbo80080
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/types.h>
#else
#include <windows.h>
// In case of Wince entry point is _tmain instead of main()
#endif
#include <string.h>

#include "fsl_types.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "fsl_parser_drm.h"
#include "subtitle.h"
#include "err_logs.h"

// #define DISPLAY_PICTURE_INFO

int32 getAviSubtitleTimeStamp(uint8* inputBuffer, uint64* usSampleStartTime,
                              uint64* usSampleDuration) {
    int32 err = PARSER_SUCCESS;
    DivXSubPictHdr subPictHdr;
    uint8 start_timestamp[13], end_timestamp[13];

    int32 hh, mm, ss, xxx;     /* convert divx duration hh:mm:ss.xxx ->  time stamp in ms */
    uint64 start_pts, end_pts; /* time stamp in ms */

    if (!inputBuffer) {
        PARSER_ERROR("can not decoder subtitile pts from a NULL buffer\n");
        return PARSER_ERR_UNKNOWN;
    }

    subParserSubPacket2Rle(inputBuffer, &subPictHdr);

    /* time stamp */
    memcpy(start_timestamp, &subPictHdr.duration[1], 12);
    memcpy(end_timestamp, &subPictHdr.duration[14], 12);
    start_timestamp[12] = '\0'; /* HH:MM:SS:XXX */
    end_timestamp[12] = '\0';

    hh = (start_timestamp[0] - '0') * 10 + start_timestamp[1] - '0';
    mm = (start_timestamp[3] - '0') * 10 + start_timestamp[4] - '0';
    ss = (start_timestamp[6] - '0') * 10 + start_timestamp[7] - '0';
    xxx = (start_timestamp[9] - '0') * 100 + (start_timestamp[10] - '0') * 10 +
          start_timestamp[11] - '0';
    start_pts = ((hh * 60 + mm) * 60 + ss) * 1000 + xxx;

    hh = (end_timestamp[0] - '0') * 10 + end_timestamp[1] - '0';
    mm = (end_timestamp[3] - '0') * 10 + end_timestamp[4] - '0';
    ss = (end_timestamp[6] - '0') * 10 + end_timestamp[7] - '0';
    xxx = (end_timestamp[9] - '0') * 100 + (end_timestamp[10] - '0') * 10 + end_timestamp[11] - '0';
    end_pts = ((hh * 60 + mm) * 60 + ss) * 1000 + xxx;

    /* ms -> us */
    *usSampleStartTime = start_pts * 1000;
    *usSampleDuration = (end_pts - start_pts) * 1000;

    return err;
}

int32 decodeSubtitleText(uint32 trackNum, uint32 sampleCount, SubtitleBuffers* subtitleBuffers,
                         uint8* inputBuffer, uint32 dataSize) {
    int32 err = PARSER_SUCCESS;
    uint8* decodedRawBuffer = subtitleBuffers->decodedRawBuffer;
    uint8* decodedBmpBuffer = subtitleBuffers->decodedBmpBuffer;

    uint8 start_timestamp[13], end_timestamp[13];
    DivXSubPictHdr subPictHdr;
    int32 rawSize = 0;
    int32 bmpSize = 0;
    int retVal;                   // return value of subtitle library
    DIVXBITMAPRGBQUAD rgbQuad[4]; /* rgb color of subpicture,
                                     entry [0]: background, entry [1]: foreground,
                                     entry [2]: emphasis 1, entry [3]: emphasis 2 (not used for
                                     feature subtitle) */
#ifdef DISPLAY_PICTURE_INFO
    int32 hh, mm, ss, xxx; /* convert divx duration hh:mm:ss.xxx ->  time stame in ms */
    uint64 start_pts, end_pts;
#endif
    FILE* fpPacket = NULL;
    char packetFileName[255];

#ifdef __WINCE
    sprintf(packetFileName, "\\SDMemory\\trk%d_subpic%d.bmp", trackNum, sampleCount);
#else
    sprintf(packetFileName, "trk%d_subpic%d.bmp", (int)trackNum, (int)sampleCount);
#endif
    fpPacket = fopen(packetFileName, "w+b");
    if (NULL == fpPacket) {
        PARSER_ERROR("fail to open dump file for text track %d, packet %d\n", trackNum,
                     sampleCount);
        err = PARSER_FILE_OPEN_ERROR;
        goto bail;
    }

    /*decode, rle (run-length) -> raw -> bitmap */
    rawSize = subtitleBuffers->max_raw_size;
    bmpSize = subtitleBuffers->max_bmp_size;
    // convert subpicture to bitmap and write file

    /* read subpic header, data is run-length coded. */
    subParserSubPacket2Rle(inputBuffer, &subPictHdr);

#ifdef DISPLAY_PICTURE_INFO
    printf("\ntrk %d, pic %d, w:h= (%d : %d), (l,t,r,b)=(%d, %d, %d, %d)\n", trackNum, sampleCount,
           subPictHdr.width, subPictHdr.height, subPictHdr.left, subPictHdr.top, subPictHdr.right,
           subPictHdr.bottom);
#endif

    /* get color for forground, background, emphasis1, and emphasis2 */
    subParserSubPictColor2RgbQuad(&subPictHdr.background, &rgbQuad[0]);
    subParserSubPictColor2RgbQuad(&subPictHdr.pattern, &rgbQuad[1]);
    subParserSubPictColor2RgbQuad(&subPictHdr.emphasis1, &rgbQuad[2]);
    subParserSubPictColor2RgbQuad(&subPictHdr.emphasis2, &rgbQuad[3]);

    retVal = subParserRle2Raw(&subPictHdr, dataSize - SUBPICT_PACKHDR_SIZE, &rawSize,
                              decodedRawBuffer);

    if (SUBTITLEPARSER_SUCCESS != retVal) {
        if (retVal == SUBTITLEPARSER_BUFFER_TOO_SMALL) {
            PARSER_ERROR("divx sub: pic %ld.Buffer was too small for subParserRle2Raw decode\n",
                         sampleCount);
            err = PARSER_ERR_INVALID_MEDIA;
            goto bail;
        } else if (retVal == SUBTITLEPARSER_INVALID_DATA) {
            PARSER_ERROR("divx sub: pic %ld.Data was invalid for subParserRle2Raw decode\n",
                         sampleCount);
            err = PARSER_ERR_INVALID_MEDIA;
            goto bail;
        }
    }

    /* time stamp */
    memcpy(start_timestamp, &subPictHdr.duration[1], 12);
    memcpy(end_timestamp, &subPictHdr.duration[14], 12);
    start_timestamp[12] = '\0'; /* HH:MM:SS:XXX */
    end_timestamp[12] = '\0';
#ifdef DISPLAY_PICTURE_INFO
    hh = (start_timestamp[0] - '0') * 10 + start_timestamp[1] - '0';
    mm = (start_timestamp[3] - '0') * 10 + start_timestamp[4] - '0';
    ss = (start_timestamp[6] - '0') * 10 + start_timestamp[7] - '0';
    xxx = (start_timestamp[9] - '0') * 100 + (start_timestamp[10] - '0') * 10 +
          start_timestamp[11] - '0';
    start_pts = ((hh * 60 + mm) * 60 + ss) * 1000 + xxx;

    hh = (end_timestamp[0] - '0') * 10 + end_timestamp[1] - '0';
    mm = (end_timestamp[3] - '0') * 10 + end_timestamp[4] - '0';
    ss = (end_timestamp[6] - '0') * 10 + end_timestamp[7] - '0';
    xxx = (end_timestamp[9] - '0') * 100 + (end_timestamp[10] - '0') * 10 + end_timestamp[11] - '0';
    end_pts = ((hh * 60 + mm) * 60 + ss) * 1000 + xxx;

    printf("pic %ld, duration (%s -%s), start pts %d ms, end pts %d ms\n", sampleCount,
           start_timestamp, end_timestamp, (uint32)start_pts, (uint32)end_pts);
#endif

    /*convert raw to rgb data */
    /* output a standard bitmap, current way */
    retVal = subParserRaw2Bmp(decodedRawBuffer, rawSize, subPictHdr.width, subPictHdr.height,
                              rgbQuad, &bmpSize, decodedBmpBuffer);

    if (SUBTITLEPARSER_SUCCESS != retVal) {
        if (retVal == SUBTITLEPARSER_BUFFER_TOO_SMALL) {
            PARSER_ERROR("divx sub: pic %ld.Buffer was too small for subParserRle2Raw decode\n",
                         sampleCount);
            err = PARSER_ERR_INVALID_MEDIA;
            goto bail;
        } else if (retVal == SUBTITLEPARSER_INVALID_DATA) {
            PARSER_ERROR("divx sub: pic %ld.Data was invalid for subParserRle2Raw decode\n",
                         sampleCount);
            err = PARSER_ERR_INVALID_MEDIA;
            goto bail;
        }
    }

    fwrite(decodedBmpBuffer, bmpSize, 1, fpPacket);

bail:
    if (fpPacket) {
        fclose(fpPacket);
        fpPacket = NULL;
    }
    return err;
}

int32 allocateSubtitleBuffers(int bmpMaxWidth, int bmpMaxHeight, SubtitleBuffers* subtitleBuffers) {
    // alignment adjustment, in nibbles
    int32 err = PARSER_SUCCESS;
    int32 align = 0;
    uint8* bmp = NULL;
    uint8* raw = NULL;
    uint32 bmpSize;
    uint32 rawSize;

    subtitleBuffers->max_raw_size = 0;
    subtitleBuffers->max_bmp_size = 0;
    subtitleBuffers->decodedRawBuffer = NULL;
    subtitleBuffers->decodedBmpBuffer = NULL;

    if (bmpMaxWidth % 8 != 0) {
        align = (8 - bmpMaxWidth % 8);
    }

    // the max bitmap size
    bmpSize = ((bmpMaxWidth + align) * bmpMaxHeight >> 1) + 118;

    bmp = (uint8*)malloc(bmpSize);
    if (NULL == bmp) {
        err = PARSER_INSUFFICIENT_MEMORY;
        goto bail;
    }

    rawSize = bmpMaxWidth * bmpMaxHeight / 2;

    if ((bmpMaxWidth * bmpMaxHeight) % 2 == 1) {
        rawSize++;
    }

    raw = (uint8*)malloc(rawSize);
    if (NULL == raw) {
        err = PARSER_INSUFFICIENT_MEMORY;
        goto bail;
    }

bail:
    if (PARSER_SUCCESS == err) {
        subtitleBuffers->max_raw_size = rawSize;
        subtitleBuffers->max_bmp_size = bmpSize;
        subtitleBuffers->decodedRawBuffer = raw;
        subtitleBuffers->decodedBmpBuffer = bmp;
    } else {
        if (raw)
            free(raw);
        if (bmp)
            free(bmp);
    }
    return err;
}

void freeSubtitleBuffers(SubtitleBuffers* subtitleBuffers) {
    if (subtitleBuffers->decodedRawBuffer) {
        free(subtitleBuffers->decodedRawBuffer);
        subtitleBuffers->decodedRawBuffer = NULL;
    }

    if (subtitleBuffers->decodedBmpBuffer) {
        free(subtitleBuffers->decodedBmpBuffer);
        subtitleBuffers->decodedBmpBuffer = NULL;
    }
}

// read a nibble from pBuffer
static SUBPARSERINLINE uint8 getNibble(uint8* pBuffer, int32 offs) {
    int32 b = offs / 2;
    int32 h = (offs % 2 == 1);

    uint8 ret;

    if (h == 0) {
        ret = (pBuffer[b] & 0xF0) >> 4;
    } else {
        ret = (pBuffer[b] & 0x0F) >> 0;
    }

    return ret;
}

// set a nibble inside pBuffer
static SUBPARSERINLINE void setNibble(uint8* pBuffer, int32 offs, uint8 n) {
    int32 b = offs / 2;
    int32 h = (offs % 2 == 1);

    if (h == 0) {
        pBuffer[b] = (pBuffer[b] & 0x0F) | (n << 4);
    } else {
        pBuffer[b] = (pBuffer[b] & 0xF0) | (n << 0);
    }

    return;
}

// write a single uint8 to pBuffer
static SUBPARSERINLINE void writeUint8(uint8** ppBuffer, uint8 val) {
    uint8** ppUint8 = (uint8**)ppBuffer;

    *(*ppUint8)++ = val;
}

// write a single uint16 to pBuffer
static SUBPARSERINLINE void writeUint16(uint8** ppBuffer, uint16 val) {
    *(*ppBuffer)++ = val & 0xFF;
    *(*ppBuffer)++ = (val >> 8) & 0xFF;

    return;
}

// write a single int32 to pBuffer
static SUBPARSERINLINE void writeInt32(uint8** ppBuffer, int32 val) {
    *(*ppBuffer)++ = val & 0xFF;
    *(*ppBuffer)++ = (val >> 8) & 0xFF;
    *(*ppBuffer)++ = (val >> 16) & 0xFF;
    *(*ppBuffer)++ = (val >> 24) & 0xFF;

    return;
}

// read a single uint8 from pBuffer
static SUBPARSERINLINE uint8 readUint8(uint8* pBuffer, uint32 offs) {
    uint8* pUint8 = pBuffer + offs;

    return (*pUint8);
}

// read a single uint16 from pBuffer
static SUBPARSERINLINE uint16 readUint16(uint8* pBuffer, uint32 offs) {
    uint16 val16;

    val16 = *(pBuffer + offs) + (*(pBuffer + offs + 1) << 8);

    return val16;
}

// read a single int32 from pBuffer
static SUBPARSERINLINE int32 readInt32(uint8* pBuffer, uint32 offs) {
    uint32 val32;

    val32 = *(pBuffer + offs) + (*(pBuffer + offs + 1) << 8) + (*(pBuffer + offs + 2) << 16) +
            (*(pBuffer + offs + 3) << 24);

    return (int32)val32;
}

void subParserSubPacket2Rle(uint8* rawSubPacket, DivXSubPictHdr* subPictHdr) {
    memcpy(subPictHdr->duration, rawSubPacket, 27);

    subPictHdr->width = readUint16(rawSubPacket, 0x1B);
    subPictHdr->height = readUint16(rawSubPacket, 0x1D);
    subPictHdr->left = readUint16(rawSubPacket, 0x1F);
    subPictHdr->top = readUint16(rawSubPacket, 0x21);
    subPictHdr->right = readUint16(rawSubPacket, 0x23);
    subPictHdr->bottom = readUint16(rawSubPacket, 0x25);
    subPictHdr->field_offset = readUint16(rawSubPacket, 0x27);

    subPictHdr->background.red = readUint8(rawSubPacket, 0x29);
    subPictHdr->background.green = readUint8(rawSubPacket, 0x2A);
    subPictHdr->background.blue = readUint8(rawSubPacket, 0x2B);

    subPictHdr->pattern.red = readUint8(rawSubPacket, 0x2C);
    subPictHdr->pattern.green = readUint8(rawSubPacket, 0x2D);
    subPictHdr->pattern.blue = readUint8(rawSubPacket, 0x2E);

    subPictHdr->emphasis1.red = readUint8(rawSubPacket, 0x2F);
    subPictHdr->emphasis1.green = readUint8(rawSubPacket, 0x30);
    subPictHdr->emphasis1.blue = readUint8(rawSubPacket, 0x31);

    subPictHdr->emphasis2.red = readUint8(rawSubPacket, 0x32);
    subPictHdr->emphasis2.green = readUint8(rawSubPacket, 0x33);
    subPictHdr->emphasis2.blue = readUint8(rawSubPacket, 0x34);

    subPictHdr->rleData = rawSubPacket + SUBPICT_PACKHDR_SIZE;
}

void subParserRle2SubPacket(DivXSubPictHdr* subPictHdr, uint32 rleSize, uint8* rawSubPacket) {
    uint8* writeCursor = NULL;

    writeCursor = rawSubPacket + 0x1B;

    memcpy(&rawSubPacket[0], subPictHdr->duration, 0x1B);  // 0x00

    writeUint16(&writeCursor, subPictHdr->width);         // 0x1B
    writeUint16(&writeCursor, subPictHdr->height);        // 0x1D
    writeUint16(&writeCursor, subPictHdr->left);          // 0x1F
    writeUint16(&writeCursor, subPictHdr->top);           // 0x21
    writeUint16(&writeCursor, subPictHdr->right);         // 0x23
    writeUint16(&writeCursor, subPictHdr->bottom);        // 0x25
    writeUint16(&writeCursor, subPictHdr->field_offset);  // 0x27

    writeUint8(&writeCursor, subPictHdr->background.red);    // 0x29
    writeUint8(&writeCursor, subPictHdr->background.green);  // 0x2A
    writeUint8(&writeCursor, subPictHdr->background.blue);   // 0x2B

    writeUint8(&writeCursor, subPictHdr->pattern.red);    // 0x2C
    writeUint8(&writeCursor, subPictHdr->pattern.green);  // 0x2D
    writeUint8(&writeCursor, subPictHdr->pattern.blue);   // 0x2E

    writeUint8(&writeCursor, subPictHdr->emphasis1.red);    // 0x2F
    writeUint8(&writeCursor, subPictHdr->emphasis1.green);  // 0x30
    writeUint8(&writeCursor, subPictHdr->emphasis1.blue);   // 0x31

    writeUint8(&writeCursor, subPictHdr->emphasis2.red);    // 0x32
    writeUint8(&writeCursor, subPictHdr->emphasis2.green);  // 0x33
    writeUint8(&writeCursor, subPictHdr->emphasis2.blue);   // 0x34

    memcpy(writeCursor, subPictHdr->rleData, rleSize);  // 0x35
}

int subParserRle2Raw(DivXSubPictHdr* subPictHdr, int32 rleSize, int32* rawSize, uint8* rawData) {
    int32 width, height, field_offset;
    int32 calcSize;

    // encoded top/bottom fields
    uint8* pEncoded[2];

    // write/read cursors for top/bottom fields
    int32 w[2], r[2] = {0, 0};

    // read stop offsets
    int32 rstop[2];

    // cache metrics
    width = subPictHdr->width;
    height = subPictHdr->height;
    field_offset = subPictHdr->field_offset;

    calcSize = (width * height) / 2;

    if ((width * height) % 2 == 1) {
        calcSize++;
    }

    // Test that size of memory was sufficient
    if (*rawSize < calcSize) {
        return SUBTITLEPARSER_BUFFER_TOO_SMALL;
    }
    // set to actual size of data
    *rawSize = calcSize;

    // read stop offsets
    rstop[0] = field_offset * 2;
    rstop[1] = (rleSize - field_offset) * 2;

    // encode field (top)
    pEncoded[0] = subPictHdr->rleData;

    // encode field (bottom)
    pEncoded[1] = subPictHdr->rleData + field_offset;

    // write cursors
    w[0] = width * (height - 1);
    w[1] = width * (height - 2);

    // continuously decode lines until height is reached
    while (w[0] >= 0 || w[1] >= 0) {
        int32 v;

        // iterate through both top and bottom fields
        for (v = 0; v < 2; v++) {
            int32 col, len, i;

            if (w[v] < 0)
                continue;

            if (r[v] < rstop[v]) {
                // grab next input nibble
                int32 rle = getNibble(pEncoded[v], r[v]++);

                if (rle < 0x04) {
                    rle = (rle << 4) | getNibble(pEncoded[v], r[v]++);

                    if (rle < 0x10) {
                        rle = (rle << 4) | getNibble(pEncoded[v], r[v]++);

                        if (rle < 0x040) {
                            rle = (rle << 4) | getNibble(pEncoded[v], r[v]++);

                            if (rle < 0x0004)
                                rle |= (width - w[v] % width) << 2;
                        }
                    }
                }

                col = rle & 0x03;
                len = rle >> 2;

                if (len > (width - w[v] % width) || len == 0)
                    len = width - w[v] % width;
            } else {
                col = 0;
                len = width - w[v] % width;
            }

            for (i = 0; i < len; i++) setNibble(rawData, w[v]++, (uint8)col);

            if (w[v] % width == 0) {
                w[v] -= width * 3;

                if (r[v] % 2 == 1)
                    r[v]++;
            }
        }
    }

    return 0;
}

int subParserRaw2Rle(uint8* rawData, int32 rawSize, int32 width, int32 height, int32* rleSize,
                     DivXSubPictHdr* subPictHdr) {
    int32 fieldOffset = 0;

    // read/write cursors
    int32 r = 0, w = 0;

    int32 x, y;  // loop counters

    int32 guardSize = 5;  // Don't know the exact size until width is incremented, therefore leave
                          // guardSize bytes clear

    // loop variable
    int32 v = 0;

    uint8* rleData = subPictHdr->rleData;

    // default field offset (should never occur)
    fieldOffset = 0;

    // loop through top/bottom fields
    for (v = 0; v < 2; v++) {
        if (v == 0) {
            r = width * (height - 1);
        } else {
            r = width * (height - 2);

            fieldOffset = w / 2;
        }

        // perform run length encoding
        for (y = v; y < height; y += 2) {
            for (x = 0; x < width;) {
                uint8 col = getNibble(rawData, r);

                int32 len = 0;

                if (*rleSize < (w + guardSize)) {
                    // close to exceeding the length of buffer
                    return SUBTITLEPARSER_BUFFER_TOO_SMALL;
                }

                // obtain the length of this run
                while (x < width && (getNibble(rawData, r) == col)) {
                    len++, r++, x++;
                }

                // a run cant be longer than 255, unless it is the rest of a row
                if (x < width && len > 255) {
                    int32 diff = len - 255;

                    len -= diff;
                    x -= diff;
                    r -= diff;
                }

                // more than 255 pixels flow to end of the line
                if (x >= width && len > 255) {
                    // encode 14 bits of zero, and 2-bit color
                    setNibble(rleData, w++, 0);
                    setNibble(rleData, w++, 0);
                    setNibble(rleData, w++, 0);
                    setNibble(rleData, w++, (uint8)(0 | col));
                }
                // more than 64 pixels, less than 255
                else if (len >= 64 && len <= 255) {
                    // divide length up into delicious pieces
                    int32 l = (len & 0xC0) >> 6;      // 2 bit left
                    int32 m = (len & 0x3C) >> 2;      // 4 bit middle
                    int32 right = (len & 0x03) >> 0;  // 2 bit right

                    // encode 6 bits of zero, and 2-bit length
                    setNibble(rleData, w++, 0);
                    setNibble(rleData, w++, (uint8)(0 | l));

                    // encode middle 4-bit length
                    setNibble(rleData, w++, (uint8)m);

                    // encode right 2-bit length, and 2-bit color
                    setNibble(rleData, w++, (uint8)((right << 2) | col));
                } else if (len >= 16 && len <= 63) {
                    int32 l = (len & 0x3C) >> 2;      // 4 bit left
                    int32 right = (len & 0x03) >> 0;  // 2 bit right

                    // set 4 bits of zero
                    setNibble(rleData, w++, 0);

                    // encode left 4-bit component
                    setNibble(rleData, w++, (uint8)l);

                    // encode right 2-bit component, and 2-bit color
                    setNibble(rleData, w++, (uint8)((right << 2) | col));
                } else if (len >= 4 && len <= 15) {
                    int32 l = (len & 0x0C) >> 2;      // 2 bit left
                    int32 right = (len & 0x03) >> 0;  // 2 bit right

                    // set 2 bits of zero, and left 2-bit length component
                    setNibble(rleData, w++, (uint8)l);

                    // set right 2-bit length component, and 2-bit color
                    setNibble(rleData, w++, (uint8)((right << 2) | col));
                } else {
                    // set 2 bits of len, and 2-bit color
                    setNibble(rleData, w++, (uint8)((len << 2) | col));
                }
            }

            // byte alignment
            if (w % 2 == 1) {
                setNibble(rleData, w++, 0);
            }

            // skip back three line
            r -= 3 * width;
        }
    }

    *rleSize = w / 2;

    if (w % 2 == 1) {
        (*rleSize)++;
    }

    subPictHdr->width = (uint16)width;
    subPictHdr->height = (uint16)height;
    subPictHdr->field_offset = (uint16)fieldOffset;
    (void)rawSize;

    return 0;
}

int subParserRaw2Bmp(uint8* rawData, int32 rawSize, int32 width, int32 height,
                     DIVXBITMAPRGBQUAD palette[4], int32* bmpSize, uint8* bmpData) {
    // bitmap rgb data
    uint8* bmpCursor;

    int32 calcSize;

    // alignment adjustment, in nibbles
    int32 align = 0;

    if (width % 8 != 0)
        align += (8 - width % 8);

    if (rawData == NULL)
        return SUBTITLEPARSER_INVALID_DATA;

    // the correct bitmap size
    calcSize = ((width + align) * height >> 1) + 118;

    if (*bmpSize < calcSize) {
        return SUBTITLEPARSER_BUFFER_TOO_SMALL;
    }

    *bmpSize = calcSize;

    // begin write cursor
    bmpCursor = bmpData;

    //
    // Bitmap File Header
    //

    writeUint16(&bmpCursor, 19778);    // bfType (Magic Number 'BM')
    writeInt32(&bmpCursor, *bmpSize);  // bfSize
    writeUint16(&bmpCursor, 0);        // bfReserved1
    writeUint16(&bmpCursor, 0);        // bfReserved2
    writeInt32(&bmpCursor, 118);       // bfOffBits

    //
    // Bitmap Info Header
    //

    writeInt32(&bmpCursor, 40);                               // biSize
    writeInt32(&bmpCursor, width);                            // biWidth
    writeInt32(&bmpCursor, height);                           // biHeight
    writeUint16(&bmpCursor, 1);                               // biPlanes
    writeUint16(&bmpCursor, 4);                               // biBitCount
    writeInt32(&bmpCursor, 0);                                // biCompression
    writeInt32(&bmpCursor, ((width + align) * height >> 1));  // biSizeImage
    writeInt32(&bmpCursor, 0);                                // biXPelsPerMeter
    writeInt32(&bmpCursor, 0);                                // biYPelsPerMeter
    writeInt32(&bmpCursor, 16);                               // biClrUsed
    writeInt32(&bmpCursor, 0);                                // biClrImportant

    //
    // Bitmap Palette
    //

    {
        int v;

        for (v = 0; v < 4; v++) {
            writeUint8(&bmpCursor, palette[v].rgbBlue);
            writeUint8(&bmpCursor, palette[v].rgbGreen);
            writeUint8(&bmpCursor, palette[v].rgbRed);
            writeUint8(&bmpCursor, palette[v].rgbReserved);
        }
    }

    memset(bmpCursor, 0, bmpData + 118 - bmpCursor);

    bmpCursor = bmpData + 118;

    //
    // Bitmap RGB data
    //

    {
        int32 w = 0, r = 0, x = 0, y = 0, v = 0;

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                int32 val = 0;

                int32 roffs = r / 2;
                int32 rhigh = (r % 2 == 0);

                int32 woffs = w / 2;
                int32 whigh = (w % 2 == 0);

                if (rhigh) {
                    val = (rawData[roffs] & 0xF0) >> 4;
                } else {
                    val = (rawData[roffs] & 0x0F) >> 0;
                }

                if (whigh) {
                    bmpCursor[woffs] = (uint8)((bmpCursor[woffs] & 0x0F) | (val << 4));
                } else {
                    bmpCursor[woffs] = (uint8)((bmpCursor[woffs] & 0xF0) | (val << 0));
                }

                r++;
                w++;
            }

            // 4 byte align
            for (v = 0; v < align; v++) {
                int32 woffs = w / 2;
                int32 whigh = (w % 2 == 0);

                if (whigh) {
                    bmpCursor[woffs] &= 0x0F;
                } else {
                    bmpCursor[woffs] &= 0xF0;
                }

                w++;
            }
        }
    }
    (void)rawSize;
    return 0;
}

int subParserBmp2Raw(uint8* bmpData, int32* width, int32* height, DIVXBITMAPRGBQUAD palette[4],
                     int32* rawSize, uint8* rawData) {
    uint8* bmpFileHdr = bmpData;
    uint8* bmpInfoHdr = bmpData + DIVXBITMAPFILEHEADER_SIZE;
    uint8* bmpCursor = bmpData;

    int32 bmpWidth = 0, bmpHeight = 0;

    // alignment adjustment, in nibbles
    int32 align = 0;

    int32 calcSize;

    if (bmpData == NULL)
        return SUBTITLEPARSER_INVALID_DATA;

    //
    // Verify bitmap file header
    //

    if (readUint16(bmpFileHdr, 0) != 19778)  // bfType (Magic number 'BM')
        return SUBTITLEPARSER_INVALID_DATA;

    //
    // Verify bitmap info header
    //

    if (readUint16(bmpInfoHdr, 0x0C) != 1)  // biPlanes
        return SUBTITLEPARSER_INVALID_DATA;

    if (readUint16(bmpInfoHdr, 0x0E) != 4)  // biBitCount
        return SUBTITLEPARSER_INVALID_DATA;

    if (readInt32(bmpInfoHdr, 0x10) != 0)  // biCompression
        return SUBTITLEPARSER_INVALID_DATA;

    //
    // Update width/height
    //

    bmpWidth = readInt32(bmpInfoHdr, 0x04);   // biWidth
    bmpHeight = readInt32(bmpInfoHdr, 0x08);  // biHeight

    *width = bmpWidth;
    *height = bmpHeight;
    calcSize = (bmpWidth * bmpHeight) / 2;

    if ((bmpWidth * bmpHeight) % 2 == 1)
        (calcSize)++;

    if (*rawSize < calcSize) {
        return SUBTITLEPARSER_BUFFER_TOO_SMALL;
    }

    *rawSize = calcSize;

    if (bmpWidth % 8 != 0)
        align += (8 - bmpWidth % 8);

    //
    // Update palette data
    //

    bmpCursor += DIVXBITMAPFILEHEADER_SIZE;
    bmpCursor += DIVXBITMAPINFOHEADER_SIZE;

    //
    // Bitmap Palette
    //

    {
        int v, offs = 0;

        for (v = 0; v < 4; v++) {
            palette[v].rgbBlue = readUint8(bmpCursor, offs++);
            palette[v].rgbGreen = readUint8(bmpCursor, offs++);
            palette[v].rgbRed = readUint8(bmpCursor, offs++);
            palette[v].rgbReserved = readUint8(bmpCursor, offs++);
        }
    }

    //
    // Convert to Raw
    //

    bmpCursor = bmpData + readInt32(bmpFileHdr, 0x0A);

    {
        int32 w = 0, r = 0, x = 0, y = 0;

        for (y = 0; y < bmpHeight; y++) {
            for (x = 0; x < bmpWidth; x++) {
                int32 val = 0;

                int32 roffs = r / 2;
                int32 rhigh = (r % 2 == 0);

                int32 woffs = w / 2;
                int32 whigh = (w % 2 == 0);

                if (rhigh) {
                    val = (bmpCursor[roffs] & 0xF0) >> 4;
                } else {
                    val = (bmpCursor[roffs] & 0x0F) >> 0;
                }

                if (whigh) {
                    rawData[woffs] = (uint8)((rawData[woffs] & 0x0F) | (val << 4));
                } else {
                    rawData[woffs] = (uint8)((rawData[woffs] & 0xF0) | (val << 0));
                }

                r++;
                w++;
            }

            // 4 byte align
            r += align;
        }
    }

    return 0;
}

int32 getBmpSize(uint8* bmpData, int32* width, int32* height, int32* size) {
    uint8* bmpFileHdr = bmpData;
    uint8* bmpInfoHdr = bmpData + DIVXBITMAPFILEHEADER_SIZE;

    int32 bmpWidth = 0, bmpHeight = 0;

    int32 calcSize;

    if (bmpData == NULL)
        return SUBTITLEPARSER_INVALID_DATA;

    //
    // Verify bitmap file header
    //

    if (readUint16(bmpFileHdr, 0) != 19778)  // bfType (Magic number 'BM')
        return SUBTITLEPARSER_INVALID_DATA;

    //
    // Verify bitmap info header
    //

    if (readUint16(bmpInfoHdr, 0x0C) != 1)  // biPlanes
        return SUBTITLEPARSER_INVALID_DATA;

    if (readUint16(bmpInfoHdr, 0x0E) != 4)  // biBitCount
        return SUBTITLEPARSER_INVALID_DATA;

    if (readInt32(bmpInfoHdr, 0x10) != 0)  // biCompression
        return SUBTITLEPARSER_INVALID_DATA;

    //
    // Update width/height
    //

    bmpWidth = readInt32(bmpInfoHdr, 0x04);   // biWidth
    bmpHeight = readInt32(bmpInfoHdr, 0x08);  // biHeight

    *width = bmpWidth;
    *height = bmpHeight;
    calcSize = (bmpWidth * bmpHeight) / 2;

    if ((bmpWidth * bmpHeight) % 2 == 1)
        (calcSize)++;

    *size = calcSize;

    return 0;
}
