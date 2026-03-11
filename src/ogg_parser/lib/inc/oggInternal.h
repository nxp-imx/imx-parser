/************************************************************************
 *  Copyright (c) 2011-2012, Freescale Semiconductor, Inc.
 *  Copyright 2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 ************************************************************************/

#ifndef OGG_INTERNAL_H
#define OGG_INTERNAL_H

#define CHECK_CRC
// #define TIME_PROFILE

#define MAXPAGESEGS 255
#define MAXPAGEPACKETS 255

#define OGG_PAGE_HEADER_LEN 27
#define HEADER_BUFFER_LEN 23
#define SERIAL_POS 13
#define MAX_PACKET_SIZE 256 * 256

typedef struct {
    uint8* pBufferData;
    void* pDataContext;
    uint32 size;
    uint32 flag;
    uint64 PTS;
    uint32 offset;
} stream_packet;

typedef struct {
    uint8 HeaderBuffer[MAXPAGESEGS + OGG_PAGE_HEADER_LEN];
    uint8 structVersion;   // 1Byte
    uint8 headerTypeFlag;  // 1Byte
    uint64 grandulePos;    // 8Byte
    uint32 bitSerial;      // 4Byte
    uint32 pageSeqence;    // 4Byte
    uint32 checkSum;       // 4Byte
    uint32 numofSegs;      // 1Byte

    uint32 headerSize;  // headerSize;
    uint32 bodySize;    // bodySize

    uint8* SegTableBuffer;  // Pointer to the segment table
    uint32 segIndex;        // index to the current seg
    uint32 segOffset;       // Offset in the current seg
    uint32 numofPackets;    // How many Packets in the Page

    // which stream does this page belong...
    uint32 streamNum;

    uint8* bodyBuffer;
    uint32 bodyOffset;

} OggPageHeaderInfo;

typedef struct {
    uint32 width;
    uint32 height;
    uint32 averBitRate;
    uint32 minBitRate;
    uint32 maxBitRate;
    uint32 fRNumerator;
    uint32 fRDenominator;

} OGG_VIDEO_PROPERTY;

typedef struct {
    uint32 sampleRate;
    uint16 channels;
    uint32 averBitrate;
    uint32 minBitrate;
    uint32 maxBitrate;

} OGG_AUDIO_PROPERY;

typedef enum {
    OGG_VORBIS = 0,
    OGG_SPEEX = 1,
    OGG_FLAC = 2,
    OGG_FLAC_NEW = 3,
    OGG_THEORA = 4,
    OGG_VIDEO = 5,
    OGG_UNKNOWN = 6
} OGG_STREAM_TYPE;

typedef struct {
    bool isEnabled;
    bool isFirstAfterSeek;

    OGG_STREAM_TYPE type;
    // identifier of the stream
    uint32 serialNum;
    //
    uint32 lastPageSequence;
    //
    uint64 usDuration;

    union {
        OGG_VIDEO_PROPERTY VideoProperty;
        OGG_AUDIO_PROPERY AudioProperty;
    } MediaProperty;

    uint32 cachedNum;
    uint32 cPacketIndex;
    uint32 tPacketNum;
    stream_packet* pCachedPackets;
    stream_packet prevPartialPacket;

    uint64 lastGranulePos;
    uint8* pCodecInfo;
    uint32 codecInfoLen;
    uint32 codecPackets;
    // the output buffer array
} OggStream;

typedef struct {
    uint32 iDataLen;
    UserDataFormat eDataFormat;
    uint8* pData;
} MetaDataItem;

typedef struct {
    /* from content descriptor */
    MetaDataItem Title;
    MetaDataItem Album;
    MetaDataItem TrackNumber;
    MetaDataItem Artist;
    MetaDataItem Performer;
    MetaDataItem Copyright;
    MetaDataItem Organization;
    MetaDataItem Description;
    MetaDataItem Genre;
    MetaDataItem Date;
} MetaDataTable;

#define MAX_OGG_STREAMS 16

#define IS_BOS(x) ((x & 0x02) == 0x02 ? 1 : 0)
#define IS_EOS(x) ((x & 0x04) == 0x04 ? 1 : 0)
#define IS_LASTPACKETUNFINISHED(x) ((x & 0x01) == 0x01 ? 1 : 0)

typedef struct {
    void* appContext;

    FslFileStream sInputStream;
    ParserOutputBufferOps sRequestBufferOps;
    ParserMemoryOps sMemOps;

    FslFileStream* inputStream;
    ParserOutputBufferOps* pRequestBufferOps;
    ParserMemoryOps* memOps;

    FslFileHandle fileHandle;
    /*The file size */
    uint64 fileSize;
    bool isLive;

    uint32 numofStreams;
    OggStream Streams[MAX_OGG_STREAMS];

    bool isParsingHead;
    OggPageHeaderInfo currentPage;

    bool isLastSampleFinished;
    uint64 lastSamplePTS;

    uint64 dataOffset;  // the offset of databegin

    uint8 currentPageBuffer[256 * 256];

    MetaDataTable m_tMetaDataTable;

} OggParser, *OggParserPtr;

#define LocalFileOpen(mode) \
    pParser->inputStream->Open(pParser->fileHanle, mode, pParser->appContext)
#define LocalFileRead(buffer, size) \
    pParser->inputStream->Read(pParser->fileHandle, buffer, size, pParser->appContext)
#define LocalFileSeek(offset, whence) \
    pParser->inputStream->Seek(pParser->fileHandle, offset, whence, pParser->appContext)
#define LocalFileTell()          \
    pParser->inputStream->Tell(  \
            pParser->fileHandle, \
            pParser->appContext) /* Tell the current position from start of the stream */
#define LocalFileSize() pParser->inputStream->Size(pParser->fileHandle, pParser->appContext)
#define LocalFileClose() pParser->inputStream->Close(pParser->fileHandle, pParser->appContext)

#define LocalCalloc(number, size) pParser->memOps->Calloc(number, size)
#define LocalMalloc(size) pParser->memOps->Malloc(size)
#define LocalFree(MemoryBlock) pParser->memOps->Free(MemoryBlock)
#define LocalReAlloc(MemoryBlock, size) pParser->memOps->ReAlloc(MemoryBlock, size)

#define SAFE_FREE(p)      \
    do {                  \
        if (p) {          \
            LocalFree(p); \
            p = NULL;     \
        }                 \
    } while (0)
#define READ32BIT(pbBuf)                                                     \
    ((uint32)pbBuf[0] + ((uint32)pbBuf[1] << 8) + ((uint32)pbBuf[2] << 16) + \
     ((uint32)pbBuf[3] << 24))

#define LENGTH_CHECK(len)             \
    do {                              \
        if (len > fileSize) {         \
            err = PARSER_ERR_UNKNOWN; \
            goto bail;                \
        }                             \
    } while (0)

#define COMMENT_SEEK_RANGE (uint32)(64 << 10)

OGGPARSER_ERR_CODE Ogg_CreateParserInternal(FslFileStream* stream, ParserMemoryOps* memOps,
                                            ParserOutputBufferOps* requestBufferOps, void* context,
                                            bool isLive, FslParserHandle* parserHandle);

OGGPARSER_ERR_CODE Ogg_ParsePageHeader(uint8* pBuffer, uint32 sizes, OggPageHeaderInfo* pHeaderInfo,
                                       OggParser* pParser);
uint32 Ogg_Page_Checksum_Set(uint8* pBuffer, uint32 Size);
OGGPARSER_ERR_CODE Ogg_SeekPageHeader(OggParser* pParser, uint32* pSerialNumber,
                                      uint8* pHeaderBuffer);
OGGPARSER_ERR_CODE Ogg_AddCachedPackets(OggParser* pParser, OggStream* pStream,
                                        stream_packet* pPacket);
OGGPARSER_ERR_CODE Ogg_GetCachedPackets(OggStream* pStream, stream_packet* pPacket);
OGGPARSER_ERR_CODE Ogg_ParseHeaderInfo(OggParser* pParser);
OGGPARSER_ERR_CODE Ogg_ResetParserContext(OggParser* pParser, uint32 streamNum, uint32 pageSequence,
                                          uint64 ganulePos);
uint32 Ogg_GetNextPacketSize(OggPageHeaderInfo* pPageInfo, uint32* pPacketFlag);
uint32 Ogg_ForwardNBytes(OggPageHeaderInfo* pPageInfo, uint32 nBytes);
OGGPARSER_ERR_CODE Ogg_CalcPTS(OggPageHeaderInfo* pPageInfo, OggStream* pStream, uint64* pPTS);

OGGPARSER_ERR_CODE Ogg_ParseGetNextPacket(OggParser* pParser, uint32* streamNum, uint8** packetData,
                                          void** pAppContext, uint32* dataSize, uint64* usPresTime,
                                          uint32* flag);

OGGPARSER_ERR_CODE Ogg_ParseScanStreamDuration(OggParser* pParser, uint32 streamNum);

OGGPARSER_ERR_CODE Ogg_ForwardPackets(OggParser* pParser, uint32 streamNum, uint32 numPackets);

OGGPARSER_ERR_CODE Ogg_SeekStream(OggParser* pParser, uint32 streamNum, uint64* usTime,
                                  uint32 flag);

#endif
