/*
 ***********************************************************************
 * Copyright 2015 by Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#ifndef _APE_PARSE_
#define _APE_PARSE_
#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct {
    uint32 iDataLen;
    UserDataFormat eDataFormat;
    uint8* pData;
} MetaDataItem;

typedef struct {
    MetaDataItem Album;
    MetaDataItem Artist;
    MetaDataItem CopyRight;
    MetaDataItem Band;
    MetaDataItem Composer;
    MetaDataItem Genre;
    MetaDataItem Title;
    MetaDataItem Year;
    MetaDataItem TrackNumber;
    MetaDataItem DiscNumber;
    MetaDataItem ArtWork;
    MetaDataItem AlbumArtist;
    MetaDataItem Comment;
} MetaDataArray;

typedef struct APE_FRAME {
    uint32 offset;
    uint32 size;
    uint32 skip;
    uint32 blocks;
    uint64 timestamp;
} APE_FRAME;

typedef struct APE_HEADER {
    uint32 tag;
    uint16 fileVersion;
    uint16 padding;
    uint32 descriptorLen;
    uint32 headerLen;
    uint32 seekTableLen;
    uint32 waveHeaderLen;
    uint32 audioDataLen;
    uint32 audioDataLenHigh;
    uint32 waveTailLen;
    uint16 md5[8];
    uint16 compressionLevel;
    uint16 formatFlags;
    uint32 blocksPerFrame;
    uint32 finalFrameBlocks;
    uint32 totalFrames;
    uint16 bitsPerSample;
    uint16 channelsNum;
    uint32 sampleRate;
    uint32 bitRate;
} APE_HEADER;

typedef struct APE_PARSER {
    FslFileHandle fileHandle;
    FslFileStream streamOps;
    ParserMemoryOps memOps;
    void* appContext;
    ParserOutputBufferOps outputOps;
    MetaDataArray metaDataList;

    int64 fileSize;      /* actual file size in bytes */
    int64 dataChunkSize; /* total data size in bytes, from the first frame to the last frame */
    uint32 usDuration;
    APE_HEADER* header;
    uint32* seekTable;
    uint8* bitTable;
    APE_FRAME* frames;
    uint32 currFrameIndex;
    uint32 lastFrameIndex;
    uint32 currFrameOffset;
    bool isSeekable;
    bool isLive;
    uint32 totalTracks;
    uint32 readMode;
    uint32 totalSamples;

} APE_PARSER;


#define LocalFileOpen h->streamOps.Open
#define LocalFileRead h->streamOps.Read
#define LocalFileSeek h->streamOps.Seek
#define LocalFileTell h->streamOps.Tell
#define LocalFileSize h->streamOps.Size
#define LocalFileClose h->stream_ops.Close

#define LocalCalloc(number, size) h->memOps.Calloc(number, size)
#define LocalMalloc(size) h->memOps.Malloc(size)
#define LocalFree(MemoryBlock) h->memOps.Free(MemoryBlock)
#define LocalReAlloc(MemoryBlock, size) h->memOps.ReAlloc(MemoryBlock, size)

#define FourCC(a, b, c, d) ((a) | (b << 8) | (c << 16) | (d << 24))

#define MAX_JUNK_DATA_LENGTH (2 << 20)
#define APE_TAG_MAX_LENGTH (8 << 20)
#define APE_CODEC_EXTRADATA_SIZE 6
#define APE_TAG_MAX_FIELD_NUM 256
#define APE_TAG_FOOTER_SIZE 32
#define APE_TAG_VERSION 2000
#define APE_TAG_FLAG_IS_HEADER (1 << 29)
#define APE_TAG_FLAG_WITH_FOOTER (1 << 30)
#define APE_TAG_FLAG_WIHT_HEADER (1 << 31)
#define APE_TAG_FLAG_IS_BINARY 0x2

#define TESTMALLOC(ptr)                   \
    if ((ptr) == 0) {                     \
        ret = PARSER_INSUFFICIENT_MEMORY; \
        goto bail;                        \
    }

#define BAILWITHERROR(v) \
    {                    \
        ret = (v);       \
        goto bail;       \
    }

int32 readData(APE_PARSER* h, FslFileHandle s, void* buffer, uint32 size,
                             void* context);

int32 skipData(APE_PARSER* h, FslFileHandle s, uint32 size, void* context);

int32 read16(APE_PARSER* h, FslFileHandle s, uint16* outVal, void* context);

int32 read32(APE_PARSER* h, FslFileHandle s, uint32* outVal, void* context);

int32 write16(uint16 src, uint8** dst);

int32 write32(uint32 src, uint8** dst);

int32 ApeParserReadHeader(APE_PARSER* h);

int32 ApeParserReadSeekTable(APE_PARSER* h);

int32 ApeParserReadOneFrame(APE_PARSER* h, uint8** sampleBuffer, void** bufferContext,
                                          uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                                          uint32* sampleFlags);

int32 ApeParserReadTag(APE_PARSER* h);

int32 ApeParserDoSeek(APE_PARSER* h, uint64* usTime, uint32 flag);

int32 ApeParserDoGetMetaData(APE_PARSER* h, UserDataID userDataId,
                                           UserDataFormat* userDataFormat, uint8** userData,
                                           uint32* userDataLength);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif