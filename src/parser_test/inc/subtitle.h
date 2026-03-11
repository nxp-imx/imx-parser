/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef INCLUDED_AVI_TEST_SUBTITLE_H
#define INCLUDED_AVI_TEST_SUBTITLE_H

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__GNUC__) && !defined(_MSC_VER)
#define SUBPARSERINLINE
#else
#define SUBPARSERINLINE __inline
#endif

typedef struct {
    int32 max_raw_size;
    uint8* decodedRawBuffer;

    int32 max_bmp_size;
    uint8* decodedBmpBuffer;

} SubtitleBuffers;

int32 getAviSubtitleTimeStamp(uint8* inputBuffer, uint64* usSampleStartTime,
                              uint64* usSampleDuration);

int32 decodeSubtitleText(uint32 trackNum, uint32 sampleCount, SubtitleBuffers* subtitleBuffers,
                         uint8* inputBuffer, uint32 dataSize);

int32 allocateSubtitleBuffers(int bmpMaxWidth, int bmpMaxHeight, SubtitleBuffers* subtitleBuffers);
void freeSubtitleBuffers(SubtitleBuffers* subtitleBuffers);

#define SUBTITLEPARSER_SUCCESS 0
#define SUBTITLEPARSER_BUFFER_TOO_SMALL 1
#define SUBTITLEPARSER_INVALID_DATA 2

/** @defgroup subtitle libSubtitleParser API
 *  Description of subtitle parser
 *  @{
 */

//
// DivX RGB color information
//

typedef struct _DivXSubPictColor {
    uint8 red;
    uint8 green;
    uint8 blue;
} DivXSubPictColor;

#define DIVXSUBPICTCOLOR_SIZE (0x03)

//
// DivX Subpicture Packet header
//

typedef struct _DivXSubPictHdr {
    //
    // Duration format : [HH:MM:SS.XXX-hh:mm:ss.xxx]
    //
    // NOTE: This string is not null terminated!
    //

    int8 duration[27];  // 0x00

    //
    // Dimensions and coordinates
    //

    uint16 width;         // 0x1B
    uint16 height;        // 0x1D
    uint16 left;          // 0x1F
    uint16 top;           // 0x21
    uint16 right;         // 0x23
    uint16 bottom;        // 0x25
    uint16 field_offset;  // 0x27

    //
    // 2-bit (4 color) palette
    //

    DivXSubPictColor background;  // 0x29
    DivXSubPictColor pattern;     // 0x2C
    DivXSubPictColor emphasis1;   // 0x2F
    DivXSubPictColor emphasis2;   // 0x32

    //
    // Rle data
    //

    uint8* rleData;  // 0x35
} DivXSubPictHdr;

#define SUBPICT_PACKHDR_TIMESTAMP_SIZE (0x1B)
#define SUBPICT_PACKHDR_SIZE (0x35)

//
// DivX Subpicture configuration list
//

typedef struct _DivXSubPictConfigList {
    DivXSubPictHdr* subPictHdr;
    char* bmpFilePath;
    struct _DivXSubPictConfigList* next;
} DivXSubPictConfigList;

//
// Bitmap file header
//

typedef struct _DIVXBITMAPFILEHEADER {
    uint16 bfType;       // 0x00
    int32 bfSize;        // 0x02
    uint16 bfReserved1;  // 0x06
    uint16 bfReserved2;  // 0x08
    int32 bfOffBits;     // 0x0A
} DIVXBITMAPFILEHEADER;

#define DIVXBITMAPFILEHEADER_SIZE (0x0E)

//
// Bitmap info header
//

typedef struct _DIVXBITMAPINFOHEADER {
    int32 biSize;           // 0x00
    int32 biWidth;          // 0x04
    int32 biHeight;         // 0x08
    uint16 biPlanes;        // 0x0C
    uint16 biBitCount;      // 0x0E
    int32 biCompression;    // 0x10
    int32 biSizeImage;      // 0x14
    int32 biXPelsPerMeter;  // 0x18
    int32 biYPelsPerMeter;  // 0x1C
    int32 biClrUsed;        // 0x20
    int32 biClrImportant;   // 0x24
} DIVXBITMAPINFOHEADER;

#define DIVXBITMAPINFOHEADER_SIZE (0x28)

//
// Bitmap RGB color data
//

typedef struct _DIVXBITMAPRGBQUAD {
    uint8 rgbBlue;
    uint8 rgbGreen;
    uint8 rgbRed;
    uint8 rgbReserved;
} DIVXBITMAPRGBQUAD;

#define DIVXBITMAPRGBQUAD_SIZE (0x04)

/**
@brief Converts Byte-aligned Subtitle Packet -> DivXSubPictHdr

Utility function to convert byte-aligned SubPict packet to
DivXSubPictHdr structure. This is done efficiently so that
there is no memory copy done on the RLE data.

@param[in] rawSubPacket - Byte-aligned subpicture packet
@param[in,out] subPictHdr - pointer to populated DivXSubPictHdr structure

@return void

@b NOTE: subPictHdr should be allocated before entry to the function, including the
buffer for rleData. the calling function must have sufficient memory allocated to accept the size of
data
*/
void subParserSubPacket2Rle(uint8* rawSubPacket, DivXSubPictHdr* subPictHdr);

/**
@brief Converts DivXSubPictHdr -> Byte-aligned Subtitle Packet

Utility function to convert DivXSubPictHdr structure to
byte-aligned SubPict packet. This is done using a memory
copy on subPictHdr->rleData. Unlike subParserSubPacket2Rle,
subPictHdr can be deallocated after this call without any
worries about the Subtitle packet being deallocated with it.


@param[in] subPictHdr  - Rle subtitle data
@param[in] rleSize - Size (in bytes) of Rle data
@param[in,out] rawSubPacket - pointer to raw sub-packet buffer

@return void

@b NOTE: The raw sub packet buffer must be allocated prior to the call to the function
and must be large enough to accept the data.
*/
void subParserRle2SubPacket(DivXSubPictHdr* subPictHdr, uint32 rleSize, uint8* rawSubPacket);

/**
@brief Converts Rle -> Raw

Example color data : 0 1 2 3 (width : 4, height : 2)
             3 2 1 0

Resulting Raw format data :

+----+----+----+----+
|b[0]|b[1]|b[2]|[b3]|
+----+----+----+----+ (4 bytes total)
|0x01|0x23|0x32|0x10|
+----+----+----+----+


@param[in] subPictHdr - Rle subtitle data
@param[in] rleSize - Size (in bytes) of Rle data
@param[out] rawSize - Size (in bytes) of Raw data. Set to size of rawData buffer on entry, value is
exact size on return
@param[in,out] rawData - ponter to buffer holding the raw data (2-bit color, 1 nibble per pixel)

@return int
@retval 0 - success
@retval 1 - failed

@b Note The fawData buffer must be allocated before entry to the function, and must be
large enough to hold the data. rawSize should be set to the size of the buffer. If the rawSize is
less than the actual size required, the function will return with a value of 0
*/
int subParserRle2Raw(DivXSubPictHdr* subPictHdr, int32 rleSize, int32* rawSize, uint8* rawData);

/**
@brief Converts Raw -> Rle

@param[in] rawData - Raw data (2-bit color, 1 nibble per pixel)
@param[in] rawSize - Size (in bytes) of Raw data
@param[in] width -
@param[in] height -
@param[out] rleSize - Size (in bytes) of Rle data
@param[in,out] subPictHdr - pointer to filled DivXSubPictHdr

@return int
@retval 0 - success
@retval 1 - failed

@b NOTE: the returned DivXSubPictHdr * contains only valid width, height,
field_offset, and rleData. The other fields must be filled in by the
calling program. The rleSize should be set to the size of the rleData buffer in subPictHdr on
calling the function. If the size of the data is greater than the buffer, the function will return
with a value of 0. On successful return, rleSize will be the true length of the data.
*/
int subParserRaw2Rle(uint8* rawData, int32 rawSize, int32 width, int32 height, int32* rleSize,
                     DivXSubPictHdr* subPictHdr);

/**
@brief Converts Raw -> Bmp

@param[in] rawData - Raw data (2-bit, 1 nibble per pixel)
@param[in] rawSize - Size (in bytes) of Raw data
@param[in] width - Width of rawData
@param[in] height - Height of rawData
@param[in] palette - 4 RGB quads specifying the palette to be used
@param[out] bmpSize - Size (in bytes) of Bmp data
@param[in,out] bmpData - pointer to buffer containing a valid bitmap (.bmp) with full 'BM' file
header

@return int
@retval 0 - success
@retval 1 - failed


@b Note bmpData buffer must be allocated before calling the function. bmpSize should be set to the
size of the buffer on calling the function. If the calculated size is greater then the bmpSize, the
function will return with 0, otherwise the return value is the calculated size of the bmp.
*/
int subParserRaw2Bmp(uint8* rawData, int32 rawSize, int32 width, int32 height,
                     DIVXBITMAPRGBQUAD palette[4], int32* bmpSize, uint8* bmpData);

/**
@brief Converts Bmp file -> Raw


@param[in] bmpData - pointer to a valid 4-bit bitmap (.bmp) file
@param[out] width - Width of rawData
@param[out] height - Height of rawData
@param[out] palette - 4 RGB quads specifying the palette found
@param[out] rawSize - Size (in bytes) of Raw data
@param[in,out] rawData -  pointer to raw data (2-bit, 1 nibble per pixel)

@return int
@retval 0 - success
@retval 1 - failed

@b Note The rawData buffer must be allocated before calling the function. rawSize should be set to
the size of the buffer on calling the function. If the calculated size is greater then the actual
rawSize, the function will return with 0, otherwise the return value is the calculated size.

*/
int subParserBmp2Raw(uint8* bmpData, int32* width, int32* height, DIVXBITMAPRGBQUAD palette[4],
                     int32* rawSize, uint8* rawData);

/**
@brief Converts ::DivXSubPictColor strcuture -> ::DIVXBITMAPRGBQUAD structure


@param[in] subPictColor - pointer to DivXSubPictColor
@param[out] rgbQuad - returned DIVXBITMAPRGBQUAD

@return void

*/

static SUBPARSERINLINE void subParserSubPictColor2RgbQuad(DivXSubPictColor* subPictColor,
                                                          DIVXBITMAPRGBQUAD* rgbQuad) {
    rgbQuad->rgbRed = subPictColor->red;
    rgbQuad->rgbGreen = subPictColor->green;
    rgbQuad->rgbBlue = subPictColor->blue;

    // unused
    rgbQuad->rgbReserved = 0;
}

/**
@brief Converts ::DIVXBITMAPRGBQUAD structure -> ::DivXSubPictColor structure

@param[in] rgbQuad - pointer to memory containing DIVXBITMAPRGBQUAD
@param[out] subPictColor - pointer to memory containing DivXSubPictColor

@return void

*/

static SUBPARSERINLINE void subParserRgbQuad2SubPictColor(DIVXBITMAPRGBQUAD* rgbQuad,
                                                          DivXSubPictColor* subPictColor) {
    subPictColor->red = rgbQuad->rgbRed;
    subPictColor->green = rgbQuad->rgbGreen;
    subPictColor->blue = rgbQuad->rgbBlue;
}

int32 getBmpSize(uint8* bmpData, int32* width, int32* height, int32* size);

#ifdef __cplusplus
}
#endif

#endif /* INCLUDED_AVI_TEST_SUBTITLE_H */
