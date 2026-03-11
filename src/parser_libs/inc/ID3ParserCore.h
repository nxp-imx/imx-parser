/*
***********************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2017, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef ID3_PARSER_CORE_H
#define ID3_PARSER_CORE_H

typedef enum eVersion {
    ID3_UNKNOWN,
    ID3_V1,
    ID3_V1_1,
    ID3_V2_2,
    ID3_V2_3,
    ID3_V2_4,
} Version;

typedef struct tag_id3_header {
    char id[3];
    uint8 version_major;
    uint8 version_minor;
    uint8 flags;
    uint8 enc_size[4];
} id3_header;

typedef struct tagID3 {
    bool mIsValid;
    bool mIsConvert;
    uint8* mData;
    uint32 mSize;
    uint32 mFirstFrameOffset;
    Version mVersion;
    ParserMemoryOps m_tMemOps;
} ID3;

void ID3CoreInit(ID3* self, ParserMemoryOps* memOps, bool isConvert);
void ID3CoreExit(ID3* self);

bool ID3V1Parse(ID3* self, const char* sourceBuf);
bool ID3V2Parse(ID3* self, const char* sourceBuf);
bool IsValid(ID3* self);
Version ID3Ver(ID3* self);
const void* GetArtWork(ID3* self, uint32* length, char** mime);
void UnsyncRemove(ID3* self);
bool UnsyncRemoveV2_4(ID3* self, bool iTunesHack);

typedef struct tagIterator {
    ID3* mParent;
    char* mID;
    uint32 mOffset;
    uint8* mFrameData;
    uint32 mFrameSize;
    ParserMemoryOps m_tMemOps;
} Iterator;

void IteratorInit(Iterator* self, const ID3* parent, const char* id);
void IteratorExit(Iterator* self);
void IteratorNext(Iterator* self);
bool Miss(Iterator* self);
void FetchFrameID(Iterator* self, char** id);
void FetchFrameVal(Iterator* self, char** s, bool otherdata);
const uint8* FetchArtWorkFrame(Iterator* self, uint32* length);
void SearchFrame(Iterator* self);
uint32 GetHeadSize(Iterator* self);

#define ID3MSG(format, ...)  // printf(format, ##__VA_ARGS__)
#define ID3V1_SIZE 128

#define FSL_MALLOC self->m_tMemOps.Malloc
#define SAFE_FREE(p)                 \
    do {                             \
        if (p) {                     \
            self->m_tMemOps.Free(p); \
            p = NULL;                \
        }                            \
    } while (0)

#ifndef TESTMALLOC
#define TESTMALLOC(ptr)                   \
    if ((ptr) == 0) {                     \
        err = PARSER_INSUFFICIENT_MEMORY; \
        goto bail;                        \
    }
#endif

#endif  // ID3_PARSER_CORE_H
