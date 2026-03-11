/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (c) 2012-2016, Freescale Semiconductor, Inc.
 * Copyright 2017-2020, 2026 NXP
*/

#include <stdlib.h>
#include <string.h>

#include "fsl_types.h"
#include "fsl_parser.h"
#include "ID3ParserCore.h"

#define METADATA_MAXSIZE (uint32)(3 << 20)
#define FSL_ASSERT(x, returnval)                                                     \
    do {                                                                             \
        if (!(x)) {                                                                  \
            ID3MSG("assert %s failed in %s, line %d\n", #x, __FUNCTION__, __LINE__); \
            returnval;                                                               \
        }                                                                            \
    } while (0)

#define BYTE_MASK (uint32)0x000000BF
#define BYTE_MARK (uint32)0x00000080

#define UNICODE_BEGIN (uint32)0x0000D800
#define UNICODE_END (uint32)0x0000DFFF
#define UNICODE_MAX_CODEPOINT (uint32)0x0010FFFF

static const uint32 g_adwFirstByteMark[] = {0x00000000, 0x00000000, 0x000000C0, 0x000000E0,
                                            0x000000F0};

static __inline uint16 bswap_16(uint16 x) {
    uint16 y = (x << 8);
    x = y | (x >> 8);

    return x;
}

static uint16 GetU16BE(const uint8* pbyAddr) {
    return pbyAddr[0] << 8 | pbyAddr[1];
}

static uint32 GetU32BE(const uint8* pbyAddr) {
    return pbyAddr[0] << 24 | pbyAddr[1] << 16 | pbyAddr[2] << 8 | pbyAddr[3];
}

static void DupChar(ID3* self, char** ppDst, const char* pSrc) {
    char* pDst = NULL;
    uint32 dwSize = (uint32)strlen(pSrc);

    SAFE_FREE(*ppDst);

    pDst = (char*)FSL_MALLOC(dwSize + 1);
    memcpy(pDst, pSrc, dwSize);
    pDst[dwSize] = '\0';

    *ppDst = pDst;

    return;
}

static void DupNChar(ID3* self, char** ppDst, const char* pSrc, uint32 dwSize) {
    char* pDst = NULL;

    SAFE_FREE(*ppDst);

    pDst = (char*)FSL_MALLOC(dwSize + 1);
    memcpy(pDst, pSrc, dwSize);
    pDst[dwSize] = '\0';

    *ppDst = pDst;

    return;
}

static __inline uint32 UTF32ToUTF8LenForOneEle(uint32 srcVal) {
    if (srcVal < 0x00000080) {
        return 1;
    } else if (srcVal < 0x00000800) {
        return 2;
    } else if (srcVal < 0x00010000) {
        if ((srcVal < UNICODE_BEGIN) || (srcVal > UNICODE_END)) {
            return 3;
        } else  // invalid
        {
            return 0;
        }
    }
    // Max code point
    else if (srcVal <= UNICODE_MAX_CODEPOINT) {
        return 4;
    } else  // Invalid
    {
        return 0;
    }
}

static __inline void UTF32ToUTF8ForOneEle(uint8* dstP, uint32 srcVal, uint32 bytes) {
    dstP += bytes;
    switch (bytes) { /* fall through. */
        case 4:
            *--dstP = (uint8)((srcVal | BYTE_MARK) & BYTE_MASK);
            srcVal >>= 6;
            __attribute__((fallthrough));
        case 3:
            *--dstP = (uint8)((srcVal | BYTE_MARK) & BYTE_MASK);
            srcVal >>= 6;
            __attribute__((fallthrough));
        case 2:
            *--dstP = (uint8)((srcVal | BYTE_MARK) & BYTE_MASK);
            srcVal >>= 6;
            __attribute__((fallthrough));
        case 1:
            *--dstP = (uint8)(srcVal | g_adwFirstByteMark[bytes]);
            break;
    }
}

void UTF16ToUTF8(const uint16* src, uint32 src_len, char* dst) {
    const uint16* cur_utf16 = src;
    const uint16* const end_utf16 = src + src_len;
    char* cur = dst;

    if (src == NULL || src_len == 0 || dst == NULL) {
        return;
    }

    while (cur_utf16 < end_utf16) {
        uint32 utf32;
        uint32 len;
        // surrogate pairs
        if ((*cur_utf16 & 0xFC00) == 0xD800) {
            utf32 = (*cur_utf16++ - 0xD800) << 10;
            utf32 |= *cur_utf16++ - 0xDC00;
            utf32 += 0x10000;
        } else {
            utf32 = (uint32)*cur_utf16++;
        }
        len = UTF32ToUTF8LenForOneEle(utf32);
        UTF32ToUTF8ForOneEle((uint8*)cur, utf32, len);
        cur += len;
    }
    *cur = '\0';
}

int32 UTF16ToUTF8Len(const uint16* src, uint32 src_len) {
    uint32 ret = 0;
    const uint16* const end = src + src_len;

    if (src == NULL || src_len == 0) {
        return -1;
    }

    while (src < end) {
        if ((*src & 0xFC00) == 0xD800 && (src + 1) < end && (*(src + 1) & 0xFC00) == 0xDC00) {
            // surrogate pairs are always 4 bytes.
            ret += 4;
            src += 2;
        } else {
            ret += UTF32ToUTF8LenForOneEle((uint32)*src++);
        }
    }
    return ret;
}

static void DupNUTF16(ID3* self, char** ppDst, const uint16* pSrc, uint32 dwSize) {
    char* pDst = NULL;
    int32 dwDstLen = 0;
    int32 padding = 8;

    SAFE_FREE(*ppDst);

    dwDstLen = UTF16ToUTF8Len(pSrc, dwSize);
    if (dwDstLen < 0) {
        return;
    }

    // MA-13282, Fix invalid data makes dwDstLen less than actual size, add padding size
    pDst = (char*)FSL_MALLOC(dwDstLen + 1 + padding);

    UTF16ToUTF8(pSrc, dwSize, pDst);

    *ppDst = pDst;

    return;
}

void ID3CoreInit(ID3* self, ParserMemoryOps* memOps, bool isConvert) {
    memset(self, 0, sizeof(ID3));
    self->m_tMemOps = *memOps;
    self->mIsConvert = isConvert;
}

void ID3CoreExit(ID3* self) {
    SAFE_FREE(self->mData);
}

void IteratorNext(Iterator* self) {
    if (self->mFrameData == NULL) {
        return;
    }

    self->mOffset += self->mFrameSize;
    SearchFrame(self);
}

bool IsValid(ID3* self) {
    return self->mIsValid;
}

Version ID3Ver(ID3* self) {
    return self->mVersion;
}

// static
static bool ParseSyncsafeInteger(const uint8 encoded[4], uint32* x) {
    int32 i = 0;

    *x = 0;
    for (i = 0; i < 4; ++i) {
        if (encoded[i] & 0x80) {
            return FALSE;
        }

        *x = ((*x) << 7) | encoded[i];
    }

    return TRUE;
}

bool ID3V2Parse(ID3* self, const char* sourceBuf) {
    uint32 size = 0;
    id3_header header;
    memcpy(&header, sourceBuf, sizeof(header));

    if (memcmp(header.id, "ID3", 3)) {
        return FALSE;
    }

    if (header.version_major == 0xff || header.version_minor == 0xff) {
        return FALSE;
    }

    if (header.version_major == 2) {
        if (header.flags & 0x3f) {
            // We only support the 2 high bits, if any of the lower bits are
            // set, we cannot guarantee to understand the tag format.
            return FALSE;
        }

        if (header.flags & 0x40) {
            // No compression scheme has been decided yet, ignore the
            // tag if compression is indicated.

            return FALSE;
        }
    } else if (header.version_major == 3) {
        if (header.flags & 0x1f) {
            // We only support the 3 high bits, if any of the lower bits are
            // set, we cannot guarantee to understand the tag format.
            return FALSE;
        }
    } else if (header.version_major == 4) {
        if (header.flags & 0x0f) {
            // The lower 4 bits are undefined in this spec.
            return FALSE;
        }
    } else {
        return FALSE;
    }

    if (!ParseSyncsafeInteger(header.enc_size, &size)) {
        return FALSE;
    }

    if (size > METADATA_MAXSIZE) {
        ID3MSG("skipping huge ID3 metadata of size %d\n", (int)size);
        return FALSE;
    }

    self->mData = (uint8*)FSL_MALLOC(size);
    if (self->mData == NULL) {
        return FALSE;
    }

    self->mSize = size;

    memcpy(self->mData, sourceBuf + sizeof(header), self->mSize);

    if (header.flags & 0x80) {
        ID3MSG("removing unsynchronization\n");

        UnsyncRemove(self);
    }

    self->mFirstFrameOffset = 0;
    if (header.version_major == 3 && (header.flags & 0x40)) {
        // Version 2.3 has an optional extended header.
        uint32 extendedHeaderSize = 0;
        uint16 extendedFlags = 0;

        if (self->mSize < 4) {
            SAFE_FREE(self->mData);
            return FALSE;
        }

        extendedHeaderSize = GetU32BE(&self->mData[0]) + 4;

        if (extendedHeaderSize > self->mSize) {
            SAFE_FREE(self->mData);
            return FALSE;
        }

        self->mFirstFrameOffset = extendedHeaderSize;

        if (extendedHeaderSize >= 6) {
            extendedFlags = GetU16BE(&self->mData[4]);

            if (extendedHeaderSize >= 10) {
                uint32 paddingSize = GetU32BE(&self->mData[6]);

                // avoid overflow issue by using subtraction instead of addictive operation
                if (paddingSize > self->mSize - self->mFirstFrameOffset) {
                    SAFE_FREE(self->mData);
                    return FALSE;
                }

                self->mSize -= paddingSize;
            }

            if (extendedFlags & 0x8000) {
                ID3MSG("have crc\n");
            }
        }
    } else if (header.version_major == 4 && (header.flags & 0x40)) {
        // Version 2.4 has an optional extended header, that's different
        // from Version 2.3's...
        uint32 ext_size = 0;

        if (self->mSize < 4) {
            SAFE_FREE(self->mData);
            return FALSE;
        }

        if (!ParseSyncsafeInteger(self->mData, &ext_size)) {
            SAFE_FREE(self->mData);
            return FALSE;
        }

        if (ext_size < 6 || ext_size > self->mSize) {
            SAFE_FREE(self->mData);
            return FALSE;
        }

        self->mFirstFrameOffset = ext_size;
    }

    // the spec is not clear about this part, just refer to android's ID3
    if (header.version_major == 4) {
        void* pDataBak = FSL_MALLOC(self->mSize);
        uint32 sizeBak = self->mSize;

        if (pDataBak == NULL) {
            SAFE_FREE(self->mData);
            return FALSE;
        }

        memcpy(pDataBak, self->mData, self->mSize);

        bool success = UnsyncRemoveV2_4(self, FALSE);
        if (!success) {
            memcpy(self->mData, pDataBak, sizeBak);
            self->mSize = sizeBak;

            success = UnsyncRemoveV2_4(self, TRUE);
            if (success) {
                ID3MSG("parse ID3 tag with the iTunes hack successfully");
            }
        }

        SAFE_FREE(pDataBak);

        if (!success) {
            SAFE_FREE(self->mData);
            return FALSE;
        }
    }

    if (header.version_major == 2) {
        self->mVersion = ID3_V2_2;
    } else if (header.version_major == 3) {
        self->mVersion = ID3_V2_3;
    } else {
        FSL_ASSERT((header.version_major == 4), return FALSE);
        self->mVersion = ID3_V2_4;
    }

    return TRUE;
}

void UnsyncRemove(ID3* self) {
    uint32 i = 0;
    uint32 left = 0, right = 0, offset = 0;

    for (i = 0; i + 1 < self->mSize; ++i) {
        if (self->mData[i] == 0xff && self->mData[i + 1] == 0x00) {
            right = i + 1;
            memmove(&self->mData[offset], &self->mData[left], right - left);
            offset += (right - left);
            left = i + 2;
        }
    }

    if (right + 1 < self->mSize) {
        memmove(&self->mData[offset], &self->mData[left], self->mSize - left);
        offset += (self->mSize - left);
    }

    self->mSize = offset;
}

static void WriteSyncsafeInteger(uint8* dst, uint32 x) {
    uint32 i = 0;

    for (i = 0; i < 4; ++i) {
        dst[3 - i] = (uint8)(x & 0x7f);
        x >>= 7;
    }
}

bool UnsyncRemoveV2_4(ID3* self, bool iTunesHack) {
    uint32 oldSize = self->mSize;
    uint32 offset = self->mFirstFrameOffset;

    while (offset + 10 <= self->mSize) {
        uint32 dataSize = 0;
        uint16 flags = 0;
        uint16 prevFlags = 0;

        if (!memcmp(&self->mData[offset], "\0\0\0\0", 4)) {
            break;
        }

        if (iTunesHack) {
            dataSize = GetU32BE(&self->mData[offset + 4]);
        } else if (!ParseSyncsafeInteger(&self->mData[offset + 4], &dataSize)) {
            return FALSE;
        }

        if ((offset + dataSize + 10 > self->mSize) || (int)dataSize < 0) {
            return FALSE;
        }

        flags = GetU16BE(&self->mData[offset + 8]);
        prevFlags = flags;

        if (flags & 1) {
            // Strip data length indicator
            if (self->mSize < 14 || self->mSize < offset + 14 || dataSize < 4)
                return FALSE;

            memmove(&self->mData[offset + 10], &self->mData[offset + 14],
                    self->mSize - offset - 14);
            self->mSize -= 4;
            dataSize -= 4;

            flags &= ~1;
        }

        if ((dataSize >= 2) && (flags & 2)) {
            // Unsynchronization added.
            uint32 i = 0;
            for (i = 0; i + 1 < dataSize; ++i) {
                if (self->mData[offset + 10 + i] == 0xff && self->mData[offset + 11 + i] == 0x00) {
                    memmove(&self->mData[offset + 11 + i], &self->mData[offset + 12 + i],
                            self->mSize - offset - 12 - i);
                    --self->mSize;
                    --dataSize;
                }
            }

            flags &= ~2;
        }

        if (iTunesHack || flags != prevFlags) {
            WriteSyncsafeInteger(&self->mData[offset + 4], dataSize);
            self->mData[offset + 8] = flags >> 8;
            self->mData[offset + 9] = flags & 0xff;
        }

        offset += 10 + dataSize;
    }

    memset(&self->mData[self->mSize], 0, oldSize - self->mSize);

    return TRUE;
}

void IteratorInit(Iterator* self, const ID3* parent, const char* id) {
    self->mParent = (ID3*)parent;
    self->mID = NULL;
    self->mOffset = parent->mFirstFrameOffset;
    self->mFrameData = NULL;
    self->mFrameSize = 0;
    self->m_tMemOps = parent->m_tMemOps;

    if (id) {
        DupChar((ID3*)parent, &self->mID, (const char*)id);
    }

    SearchFrame(self);
}

void IteratorExit(Iterator* self) {
    SAFE_FREE(self->mID);
}

bool Miss(Iterator* self) {
    return self->mFrameData == NULL;
}

void FetchFrameID(Iterator* self, char** id) {
    *id = NULL;

    if (self->mFrameData == NULL) {
        return;
    }

    if (self->mParent->mVersion == ID3_V2_2) {
        DupNChar(self->mParent, id, (const char*)&self->mParent->mData[self->mOffset], 3);
    } else if (self->mParent->mVersion == ID3_V2_3 || self->mParent->mVersion == ID3_V2_4) {
        DupNChar(self->mParent, id, (const char*)&self->mParent->mData[self->mOffset], 4);
    } else {
        FSL_ASSERT((self->mParent->mVersion == ID3_V1 || self->mParent->mVersion == ID3_V1_1),
                   return);

        switch (self->mOffset) {
            case 3:
                DupChar(self->mParent, id, "TT2");
                break;
            case 33:
                DupChar(self->mParent, id, "TP1");
                break;
            case 63:
                DupChar(self->mParent, id, "TAL");
                break;
            case 93:
                DupChar(self->mParent, id, "TYE");
                break;
            case 97:
                DupChar(self->mParent, id, "COM");
                break;
            case 126:
                DupChar(self->mParent, id, "TRK");
                break;
            case 127:
                DupChar(self->mParent, id, "TCO");
                break;
            default:
                ID3MSG("FetchFrameID, should not be here\n");
                break;
        }
    }
}

static void convertISO8859ToString8(Iterator* self, const uint8* data, uint32 size, char** s) {
    uint32 utf8len = 0;
    uint32 i = 0;
    char* tmp = NULL;
    char* ptr = NULL;

    *s = NULL;

    for (i = 0; i < size; ++i) {
        if (data[i] == '\0') {
            size = i;
            break;
        } else if (data[i] < 0x80) {
            ++utf8len;
        } else {
            utf8len += 2;
        }
    }

    if (size == 0) {
        return;
    }

    if (utf8len == size) {
        // Only ASCII characters present.

        DupNChar(self->mParent, s, (const char*)data, size);
        return;
    }

    tmp = (char*)FSL_MALLOC(utf8len);
    ptr = tmp;

    for (i = 0; i < size; ++i) {
        if (data[i] == '\0') {
            break;
        } else if (data[i] < 0x80) {
            *ptr++ = data[i];
        } else if (data[i] < 0xc0) {
            *ptr++ = 0xc2;
            *ptr++ = data[i];
        } else {
            *ptr++ = 0xc3;
            *ptr++ = data[i] - 64;
        }
    }

    DupNChar(self->mParent, s, tmp, utf8len);

    SAFE_FREE(tmp);
}

void FetchFrameVal(Iterator* self, char** id, bool otherdata) {
    uint32 n = 0;

    *id = NULL;

    if (self->mFrameData == NULL) {
        return;
    }

    if (self->mParent->mVersion == ID3_V1 || self->mParent->mVersion == ID3_V1_1) {
        if (self->mOffset == 126 || self->mOffset == 127) {
            // Special treatment for the track number and genre.
            char tmp[16];
            sprintf(tmp, "%d", (int)*self->mFrameData);

            DupChar(self->mParent, id, tmp);
            return;
        }

        if (self->mParent->mIsConvert)
            convertISO8859ToString8(self, self->mFrameData, self->mFrameSize, id);
        else
            DupNChar(self->mParent, id, (const char*)self->mFrameData, self->mFrameSize);

        return;
    }

    n = self->mFrameSize - GetHeadSize(self) - 1;
    if (otherdata) {
        uint32 i = n - 4;
        int skipped;
        const uint8* framedata = (const uint8*)self->mFrameData;

        framedata += 4;
        do {
            framedata++;
            i--;
        } while (i > 0 && *framedata != 0);
        skipped = (framedata - self->mFrameData);
        if (skipped >= (int)n) {
            return;
        }
        n -= skipped;
        self->mFrameData += skipped;
    }
    if (*self->mFrameData == 0x00) {
        // ISO 8859-1
        if (self->mParent->mIsConvert)
            convertISO8859ToString8(self, self->mFrameData + 1, n, id);
        else
            DupNChar(self->mParent, id, (const char*)(self->mFrameData + 1), n);
    } else if (*self->mFrameData == 0x03) {
        // UTF-8
        DupNChar(self->mParent, id, (const char*)(self->mFrameData + 1), n);
    } else if (*self->mFrameData == 0x02) {
        // UTF-16 BE, no byte order mark.
        // API wants number of characters, not number of bytes...
        int i = 0;
        int len = n / 2;
        const uint16* framedata = (const uint16*)(self->mFrameData + 1);
        uint16* framedatacopy = NULL;

#if 1  // LITTLE_ENDIAN
        framedatacopy = (uint16*)FSL_MALLOC(n);
        for (i = 0; i < len; i++) {
            framedatacopy[i] = bswap_16(framedata[i]);
        }
        framedata = framedatacopy;
#endif
        DupNUTF16(self->mParent, id, (const uint16*)framedata, len);
        SAFE_FREE(framedatacopy);
    } else {
        // UCS-2
        // API wants number of characters, not number of bytes...
        int i = 0;
        int len = n / 2;
        const uint16* framedata = (const uint16*)(self->mFrameData + 1);
        uint16* framedatacopy = NULL;
        bool eightBit = TRUE;

        if (*framedata == 0xfffe) {
            // endianness marker doesn't match host endianness, convert
            framedatacopy = (uint16*)FSL_MALLOC(n);
            for (i = 0; i < len; i++) {
                framedatacopy[i] = bswap_16(framedata[i]);
            }
            framedata = framedatacopy;
        }
        // If the string starts with an endianness marker, skip it
        if (*framedata == 0xfeff) {
            framedata++;
            len--;
        }

        for (i = 0; i < len; i++) {
            if (framedata[i] > 0xff) {
                eightBit = FALSE;
                break;
            }
        }
        if (eightBit) {
            // collapse to 8 bit, then let the media scanner client figure out the real encoding
            char* frame8 = (char*)FSL_MALLOC(len * sizeof(char));
            if (frame8 != NULL) {
                for (i = 0; i < len; i++) frame8[i] = framedata[i];
                DupNChar(self->mParent, id, frame8, len);
                SAFE_FREE(frame8);
            }
        } else {
            DupNUTF16(self->mParent, id, (const uint16*)framedata, len);
        }
        SAFE_FREE(framedatacopy);
    }
}

const uint8* FetchArtWorkFrame(Iterator* self, uint32* length) {
    *length = 0;

    if (self->mFrameData == NULL) {
        return NULL;
    }

    *length = self->mFrameSize - GetHeadSize(self);

    return self->mFrameData;
}

uint32 GetHeadSize(Iterator* self) {
    if (self->mParent->mVersion == ID3_V2_2) {
        return 6;
    } else if (self->mParent->mVersion == ID3_V2_3 || self->mParent->mVersion == ID3_V2_4) {
        return 10;
    } else {
        return 0;
    }
}

void SearchFrame(Iterator* self) {
    for (;;) {
        self->mFrameData = NULL;
        self->mFrameSize = 0;

        if (self->mParent->mVersion == ID3_V2_2) {
            char id[4];

            if (self->mOffset + 6 > self->mParent->mSize) {
                return;
            }

            if (!memcmp(&self->mParent->mData[self->mOffset], "\0\0\0", 3)) {
                return;
            }

            self->mFrameSize = (self->mParent->mData[self->mOffset + 3] << 16) |
                               (self->mParent->mData[self->mOffset + 4] << 8) |
                               self->mParent->mData[self->mOffset + 5];

            self->mFrameSize += 6;

            if (self->mOffset + self->mFrameSize > self->mParent->mSize) {
                ID3MSG("partial frame at offset %d (size = %d, bytes-remaining = %d)\n",
                       (int)self->mOffset, (int)self->mFrameSize,
                       (int)(self->mParent->mSize - self->mOffset - 6));
                return;
            }

            self->mFrameData = &self->mParent->mData[self->mOffset + 6];

            if (!self->mID) {
                break;
            }

            memcpy(id, &self->mParent->mData[self->mOffset], 3);
            id[3] = '\0';

            if (!strcmp(id, self->mID)) {
                break;
            }
        } else if (self->mParent->mVersion == ID3_V2_3 || self->mParent->mVersion == ID3_V2_4) {
            char id[5];
            uint32 baseSize = 0;
            uint16 flags = 0;

            if (self->mOffset + 10 > self->mParent->mSize) {
                return;
            }

            if (!memcmp(&self->mParent->mData[self->mOffset], "\0\0\0\0", 4)) {
                return;
            }

            if (self->mParent->mVersion == ID3_V2_4) {
                if (!ParseSyncsafeInteger(&self->mParent->mData[self->mOffset + 4], &baseSize)) {
                    return;
                }
            } else {
                baseSize = GetU32BE(&self->mParent->mData[self->mOffset + 4]);
            }

            // Prevent integer overflow when adding
            if (baseSize == 0 || baseSize >= ((uint32)(-1) - 10))
                return;

            self->mFrameSize = 10 + baseSize;

            if (self->mOffset + self->mFrameSize > self->mParent->mSize) {
                ID3MSG("partial frame at offset %d (size = %d, bytes-remaining = %d)\n",
                       (int)self->mOffset, (int)self->mFrameSize,
                       (int)(self->mParent->mSize - self->mOffset - 10));
                return;
            }

            flags = GetU16BE(&self->mParent->mData[self->mOffset + 8]);

            if ((self->mParent->mVersion == ID3_V2_4 && (flags & 0x000c)) ||
                (self->mParent->mVersion == ID3_V2_3 && (flags & 0x00c0))) {
                // Compression or encryption are not supported at this time.
                // Per-frame unsynchronization and data-length indicator
                // have already been taken care of.

                ID3MSG("Skipping unsupported frame (compression, encryption "
                       "or per-frame unsynchronization flagged\n");

                self->mOffset += self->mFrameSize;
                continue;
            }

            self->mFrameData = &self->mParent->mData[self->mOffset + 10];

            if (!self->mID) {
                break;
            }

            memcpy(id, &self->mParent->mData[self->mOffset], 4);
            id[4] = '\0';

            if (!strcmp(id, self->mID)) {
                break;
            }
        } else {
            char* id = NULL;

            FSL_ASSERT((self->mParent->mVersion == ID3_V1 || self->mParent->mVersion == ID3_V1_1),
                       return);

            if (self->mOffset >= self->mParent->mSize) {
                return;
            }

            self->mFrameData = &self->mParent->mData[self->mOffset];

            switch (self->mOffset) {
                case 3:
                case 33:
                case 63:
                    self->mFrameSize = 30;
                    break;
                case 93:
                    self->mFrameSize = 4;
                    break;
                case 97:
                    if (self->mParent->mVersion == ID3_V1) {
                        self->mFrameSize = 30;
                    } else {
                        self->mFrameSize = 29;
                    }
                    break;
                case 126:
                    self->mFrameSize = 1;
                    break;
                case 127:
                    self->mFrameSize = 1;
                    break;
                default:
                    ID3MSG("SearchFrame, Should not be here, invalid offset %d\n",
                           (int)self->mOffset);
                    break;
            }

            if (!self->mID) {
                break;
            }

            FetchFrameID(self, &id);
            if (strcmp(id, self->mID) == 0) {
                SAFE_FREE(id);
                break;
            }

            SAFE_FREE(id);
        }

        self->mOffset += self->mFrameSize;
    }
}

static uint32 StringSize(const uint8* start, uint8 encoding) {
    uint32 n = 0;

    if (encoding == 0x00 || encoding == 0x03) {
        // ISO 8859-1 or UTF-8
        return (uint32)strlen((const char*)start) + 1;
    }

    // UCS-2
    while (start[n] != '\0' || start[n + 1] != '\0') {
        n += 2;
    }

    return n + 2;
}

const void* GetArtWork(ID3* self, uint32* length, char** mime) {
    Iterator it;

    *mime = NULL;
    *length = 0;

    if (!(self->mVersion == ID3_V2_2 || self->mVersion == ID3_V2_3 || self->mVersion == ID3_V2_4)) {
        return NULL;
    }

    IteratorInit(&it, self,
                 (self->mVersion == ID3_V2_3 || self->mVersion == ID3_V2_4) ? "APIC" : "PIC");

    while (!Miss(&it)) {
        uint32 size;
        const uint8* data = FetchArtWorkFrame(&it, &size);

        if (self->mVersion == ID3_V2_3 || self->mVersion == ID3_V2_4) {
            uint32 mimeLen = 0;
            uint32 descLen = 0;
            uint8 encoding = data[0];

            DupChar(self, mime, (const char*)&data[1]);
            mimeLen = (uint32)strlen((const char*)&data[1]) + 1;
            descLen = StringSize(&data[2 + mimeLen], encoding);
            if (size > 2 + mimeLen + descLen) {
                *length = size - 2 - mimeLen - descLen;
                IteratorExit(&it);
                return &data[2 + mimeLen + descLen];
            } else {
                *length = 0;
                IteratorExit(&it);
                return NULL;
            }
        } else {
            uint8 encoding = data[0];
            uint32 descLen = 0;

            if (!memcmp(&data[1], "PNG", 3)) {
                DupChar(self, mime, "image/png");
            } else if (!memcmp(&data[1], "JPG", 3)) {
                DupChar(self, mime, "image/jpeg");
            } else if (!memcmp(&data[1], "-->", 3)) {
                DupChar(self, mime, "text/plain");
            } else {
                IteratorExit(&it);
                return NULL;
            }

            descLen = StringSize(&data[5], encoding);
            if (size > 5 + descLen) {
                *length = size - 5 - descLen;
                IteratorExit(&it);
                return &data[5 + descLen];
            } else {
                *length = 0;
                IteratorExit(&it);
                return NULL;
            }
        }
    }

    IteratorExit(&it);
    return NULL;
}

bool ID3V1Parse(ID3* self, const char* sourceBuf) {
    self->mData = (uint8*)FSL_MALLOC(ID3V1_SIZE);
    memcpy(self->mData, sourceBuf, ID3V1_SIZE);

    if (memcmp("TAG", self->mData, 3)) {
        SAFE_FREE(self->mData);
        return FALSE;
    }

    self->mSize = ID3V1_SIZE;
    self->mFirstFrameOffset = 3;

    if (self->mData[ID3V1_SIZE - 3] != 0) {
        self->mVersion = ID3_V1;
    } else {
        self->mVersion = ID3_V1_1;
    }

    return TRUE;
}
