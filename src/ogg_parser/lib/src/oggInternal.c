/************************************************************************
 *  Copyright (c) 2011-2013, 2016, Freescale Semiconductor, Inc.
 *  Copyright 2025-2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 ************************************************************************/

#include "ogg_parser_api.h"
#include "oggInternal.h"

#include <string.h>
#include <strings.h>

#define CHECK_CRC
// #define TIME_PROFILE

#ifdef CHECK_CRC
static const uint32 crc_lookup[256] = {
        0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2,
        0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3,
        0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac,
        0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
        0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e,
        0x95609039, 0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
        0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d, 0xd4326d90,
        0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
        0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a,
        0xec7dd02d, 0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c,
        0x2e003dc5, 0x2ac12072, 0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13,
        0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
        0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1,
        0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba, 0xaca5c697, 0xa864db20,
        0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f,
        0x8e6c3698, 0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
        0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055,
        0xfef34de2, 0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
        0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632,
        0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
        0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53, 0x251d3b9e, 0x21dc2629, 0x2c9f00f0,
        0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91,
        0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e,
        0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
        0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604,
        0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2, 0xaafbe615,
        0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a,
        0x8cf30bad, 0x81b02d74, 0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
        0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f,
        0x76c15bf8, 0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
        0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec, 0x3793a651,
        0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
        0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb,
        0xdbee767c, 0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa,
        0xf9278673, 0xfde69bc4, 0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5,
        0x9e7d9662, 0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
        0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4};

#ifdef TIME_PROFILE
#include <sys/time.h>
uint64 tCRCtime = 0;

#endif

static uint32 ogg_page_checksum_set(unsigned char* pHeader, unsigned char* pBody, uint32 header_len,
                                    uint32 body_len) {
    uint32 crc_reg = 0;
    uint32 i;

#ifdef TIME_PROFILE
    uint64 interval;
    struct timeval time0, time1;

    gettimeofday(&time0, NULL);
#endif

    /* safety; needed for API behavior, but not framing code */
    pHeader[22] = 0;
    pHeader[23] = 0;
    pHeader[24] = 0;
    pHeader[25] = 0;

    for (i = 0; i < header_len; i++)
        crc_reg = (crc_reg << 8) ^ crc_lookup[((crc_reg >> 24) & 0xff) ^ pHeader[i]];
    for (i = 0; i < body_len; i++)
        crc_reg = (crc_reg << 8) ^ crc_lookup[((crc_reg >> 24) & 0xff) ^ pBody[i]];

#ifdef TIME_PROFILE
    gettimeofday(&time1, NULL);
    interval = ((uint64)time1.tv_sec * 1000000 + time1.tv_usec) -
               ((uint64)time0.tv_sec * 1000000 + time0.tv_usec);
#endif
    return crc_reg;
}

#endif

static OGGPARSER_ERR_CODE Ogg_ParseMetaData(OggParser* pParser);

OGGPARSER_ERR_CODE Ogg_CreateParserInternal(FslFileStream* stream, ParserMemoryOps* memOps,
                                            ParserOutputBufferOps* requestBufferOps, void* context,
                                            bool isLive, FslParserHandle* parserHandle) {
    OGGPARSER_ERR_CODE err = PARSER_SUCCESS;
    OggParserPtr pParser = NULL;
    FslFileHandle fileHandle;
    uint64 usTime;
    uint32 i = 0;

    *parserHandle = NULL;

    pParser = memOps->Malloc(sizeof(OggParser));
    if (NULL == pParser) {
        err = PARSER_INSUFFICIENT_MEMORY;
        goto bail;
    }
    memset(pParser, 0, sizeof(OggParser));

    memcpy(&(pParser->sMemOps), memOps, sizeof(ParserMemoryOps));
    pParser->memOps = &(pParser->sMemOps);

    memcpy(&(pParser->sInputStream), stream, sizeof(FslFileStream));
    pParser->inputStream = &(pParser->sInputStream);

    memcpy(&(pParser->sRequestBufferOps), requestBufferOps, sizeof(ParserOutputBufferOps));
    pParser->pRequestBufferOps = &(pParser->sRequestBufferOps);

    /* try to open the source stream */
    fileHandle = stream->Open(NULL, (const uint8*)"rb", context);
    if (fileHandle == NULL) {
        err = PARSER_FILE_OPEN_ERROR;
        goto bail;
    }

    pParser->fileSize = (uint64)stream->Size(fileHandle, context);
    pParser->fileHandle = fileHandle;
    pParser->appContext = context;

    pParser->isLive = isLive;

    err = Ogg_ParseHeaderInfo(pParser);
    if (err)
        goto bail;

    for (i = 0; i < pParser->numofStreams; i++) {
        pParser->Streams[i].prevPartialPacket.pBufferData = LocalMalloc(MAX_PACKET_SIZE);
        if (pParser->Streams[i].prevPartialPacket.pBufferData == NULL) {
            err = PARSER_INSUFFICIENT_MEMORY;
            goto bail;
        }
    }

    if (!pParser->isLive) {
        usTime = -1;
        err = Ogg_SeekStream(pParser, 0, &usTime, 0);
        if (err == PARSER_ERR_NOT_SEEKABLE) {
            pParser->Streams[0].usDuration = 0;
            err = PARSER_SUCCESS;
            goto next;
        }
        if (err != PARSER_EOS)
            goto bail;
        pParser->Streams[0].usDuration = usTime;
        usTime = 0;
        err = Ogg_SeekStream(pParser, 0, &usTime, 0);
    } else
        pParser->Streams[0].usDuration = 0;
next:
    Ogg_ParseMetaData(pParser);

bail:
    if (PARSER_SUCCESS != err) {
        /*
        if(streamOpened)
          stream->Close(fileHandle, context);
        if(pParser)
        {
            memOps->Free(pParser);
            pParser= NULL;
        }
        */
        if (pParser)
            OggDeleteParser((FslParserHandle)pParser);
    } else {
        *parserHandle = (FslParserHandle)pParser;
    }
    return err;
}

OGGPARSER_ERR_CODE Ogg_ParsePageHeader(uint8* pBuffer, uint32 sizes, OggPageHeaderInfo* pHeaderInfo,
                                       OggParser* pParser) {
    uint32 i;
    uint64 posValue = 0;
    uint32 temp;

    pHeaderInfo->HeaderBuffer[0] = 'O';
    pHeaderInfo->HeaderBuffer[1] = 'g';
    pHeaderInfo->HeaderBuffer[2] = 'g';
    pHeaderInfo->HeaderBuffer[3] = 'S';
    memcpy(&pHeaderInfo->HeaderBuffer[4], pBuffer, 23);

    pHeaderInfo->structVersion = *pBuffer++;
    pHeaderInfo->headerTypeFlag = *pBuffer++;

    // Note that the header information has
    pBuffer += 7;
    for (i = 0; i < 8; i++) {
        posValue = (posValue << 8) | *pBuffer--;
    }
    pHeaderInfo->grandulePos = posValue;
    pBuffer += 9;

    temp = 0;
    pBuffer += 3;
    for (i = 0; i < 4; i++) {
        temp = (temp << 8) | *pBuffer--;
    }
    pHeaderInfo->bitSerial = temp;
    pBuffer += 5;

    temp = 0;
    pBuffer += 3;
    for (i = 0; i < 4; i++) {
        temp = (temp << 8) | *pBuffer--;
    }
    pHeaderInfo->pageSeqence = temp;
    pBuffer += 5;

    temp = 0;
    pBuffer += 3;
    for (i = 0; i < 4; i++) {
        temp = (temp << 8) | *pBuffer--;
    }
    pBuffer += 5;
    pHeaderInfo->checkSum = temp;

    pHeaderInfo->numofSegs = *pBuffer++;
    pHeaderInfo->segIndex = 0;

    pHeaderInfo->SegTableBuffer = &(pHeaderInfo->HeaderBuffer[27]);
    if (LocalFileRead(pHeaderInfo->SegTableBuffer, pHeaderInfo->numofSegs) < pHeaderInfo->numofSegs)
        return PARSER_EOS;
    pHeaderInfo->headerSize = pHeaderInfo->numofSegs + 27;
    pHeaderInfo->bodySize = 0;
    pHeaderInfo->numofPackets = 0;

    for (i = 0; i < pHeaderInfo->numofSegs; i++) {
        pHeaderInfo->bodySize += pHeaderInfo->SegTableBuffer[i];
        if (pHeaderInfo->SegTableBuffer[i] < 255)
            pHeaderInfo->numofPackets++;
    }

    if (pHeaderInfo->numofPackets == 0)
        pHeaderInfo->numofPackets = 1;

    pHeaderInfo->bodyBuffer = pParser->currentPageBuffer;
    pHeaderInfo->bodyOffset = 0;
    (void)sizes;

    return 0;
}

OGGPARSER_ERR_CODE Ogg_SeekPageHeader(OggParser* pParser, uint32* pSerialNumber,
                                      uint8* pHeaderBuffer) {
    uint8 tempBuf[OGG_PAGE_HEADER_LEN] = {0};
    uint32 readSize;
    uint32 fourBytes;
    int32 i;
    uint32 seekLength = 0;
    static const uint32 OggPageStartCode = 0x4F676753;
    static const uint32 SeekMaxLength = (65536 + 256 + 27);

    fourBytes = 0;

SEEK_HEADER:

    readSize = LocalFileRead(tempBuf, OGG_PAGE_HEADER_LEN);
    if (readSize < OGG_PAGE_HEADER_LEN)
        return PARSER_EOS;

    for (i = 0; i < OGG_PAGE_HEADER_LEN; i++) {
        fourBytes = (fourBytes << 8) | tempBuf[i];
        if (fourBytes == OggPageStartCode)
            break;
    }

    if (i < OGG_PAGE_HEADER_LEN) {
        uint32 validLen = OGG_PAGE_HEADER_LEN - i - 1;
        /* In case i < 3, which means sync word OGGS is match by previous buffer, hence validLen may
         * exceed 23 which is wrong*/
        if (validLen > HEADER_BUFFER_LEN)
            validLen = HEADER_BUFFER_LEN;

        if (i < (OGG_PAGE_HEADER_LEN - 1))
            memcpy(pHeaderBuffer, tempBuf + i + 1, validLen);

        if (validLen < HEADER_BUFFER_LEN) {
            uint32 validLen2 = OGG_PAGE_HEADER_LEN - 4 - validLen;
            readSize = LocalFileRead(tempBuf, validLen2);
            if (readSize < validLen2)
                return PARSER_EOS;
            memcpy((pHeaderBuffer + validLen), tempBuf, validLen2);
        }

        *pSerialNumber = 0;
        for (i = 0; i < 4; i++)
            *pSerialNumber = (*pSerialNumber << 8) | pHeaderBuffer[SERIAL_POS - i];

        return PARSER_SUCCESS;
    } else {
        seekLength += OGG_PAGE_HEADER_LEN;
        if (seekLength >= SeekMaxLength)
            return PARSER_ERR_INVALID_MEDIA;

        goto SEEK_HEADER;
    }
}

#define DEFAULT_CACHED_PACKETNUM 32

OGGPARSER_ERR_CODE Ogg_AddCachedPackets(OggParser* pParser, OggStream* pStream,
                                        stream_packet* pPacket) {
    if (NULL == pStream->pCachedPackets) {
        pStream->pCachedPackets = LocalMalloc(DEFAULT_CACHED_PACKETNUM * sizeof(stream_packet));
        if (pStream->pCachedPackets == NULL)
            return PARSER_INSUFFICIENT_MEMORY;
        pStream->cachedNum = 0;
        pStream->cPacketIndex = 0;
        pStream->tPacketNum = DEFAULT_CACHED_PACKETNUM;
        memset(pStream->pCachedPackets, 0, DEFAULT_CACHED_PACKETNUM * sizeof(stream_packet));
    }

    pStream->cachedNum++;
    if (pStream->cachedNum > pStream->tPacketNum) {
        pStream->pCachedPackets = LocalReAlloc(
                pStream->pCachedPackets,
                (pStream->tPacketNum + DEFAULT_CACHED_PACKETNUM) * sizeof(stream_packet));
        if (NULL == pStream->pCachedPackets)
            return PARSER_INSUFFICIENT_MEMORY;
        // initialize the new adding memory block
        memset(&(pStream->pCachedPackets[pStream->tPacketNum]), 0,
               DEFAULT_CACHED_PACKETNUM * sizeof(stream_packet));
        pStream->tPacketNum += DEFAULT_CACHED_PACKETNUM;
    }

    memcpy(&(pStream->pCachedPackets[pStream->cachedNum - 1]), pPacket, sizeof(stream_packet));

    return PARSER_SUCCESS;
}

OGGPARSER_ERR_CODE Ogg_GetCachedPackets(OggStream* pStream, stream_packet* pPacket) {
    /*
     typedef struct{
        bool isEnabled;

        OGG_STREAM_TYPE type;
        //identifier of the stream
        uint32 serialNum;
        //
        uint32 lastPageSequence;
        //
        uint64 usDuration;

        union
        {
            OGG_VIDEO_PROPERTY    VideoProperty;
         OGG_AUDIO_PROPERY     AudioProperty;
        } MediaProperty;

          uint32   cachedNum;
        uint32   cPacketIndex;
        uint32   tPacketNum;
        stream_packet* pCachedPackets;
        //the output buffer array
    }OggStream;
    */
    if (pStream->cPacketIndex >= pStream->cachedNum)
        return OGGPARSER_NO_CHACHEDPACKETS;

    memcpy(pPacket, &(pStream->pCachedPackets[pStream->cPacketIndex]), sizeof(stream_packet));
    pStream->cPacketIndex++;

    return PARSER_SUCCESS;
}

#if __WINCE
#define INSENSITIVE_STRCMP _strnicmp
#elif WIN32
#define INSENSITIVE_STRCMP strnicmp
#else
#define INSENSITIVE_STRCMP strncasecmp
#endif

OGGPARSER_ERR_CODE Ogg_ParseMetaData(OggParser* pParser) {
    int64 savePos = 0;
    MetaDataTable* pMetaDataTable = NULL;
    uint8* pTmpBuf = NULL;
    uint32 searchPos = 0;
    uint64 fileSize = 0;
    uint32 dwTmpBufSize = 0;
    OGGPARSER_ERR_CODE err = PARSER_SUCCESS;
    uint32 venderLength = 0;
    uint32 commentNum = 0;
    uint32 commentIdx = 0;
    uint32 commentLen = 0;
    uint32 commentValueLen = 0;

    if (pParser == NULL) {
        return PARSER_ERR_UNKNOWN;
    }

    savePos = LocalFileTell();
    LocalFileSeek(0, SEEK_SET);

    fileSize = (uint64)LocalFileSize();
    dwTmpBufSize = (COMMENT_SEEK_RANGE <= fileSize) ? COMMENT_SEEK_RANGE : fileSize;

    pTmpBuf = LocalMalloc(dwTmpBufSize);
    if (pTmpBuf == NULL) {
        err = PARSER_INSUFFICIENT_MEMORY;
        goto bail;
    }

    if (LocalFileRead(pTmpBuf, dwTmpBufSize) < dwTmpBufSize) {
        err = PARSER_EOS;
        goto bail;
    }

    searchPos = 0;
    while ((searchPos + 7 <= dwTmpBufSize) &&
           strncmp((const char*)(pTmpBuf + searchPos), "\003vorbis", 7))
        searchPos++;

    // not find vorbis comment field
    if (searchPos + 7 > dwTmpBufSize) {
        goto bail;
    }

    // now we can parser vorbis comment field
    LocalFileSeek(searchPos + 7, SEEK_SET);

    // jump vendor string
    LocalFileRead(pTmpBuf, 4);
    venderLength = READ32BIT(pTmpBuf);
    LENGTH_CHECK(venderLength);
    LocalFileSeek(venderLength, SEEK_CUR);

    // comment list len
    LocalFileRead(pTmpBuf, 4);
    commentNum = READ32BIT(pTmpBuf);
    if (commentNum > 1000) {
        err = PARSER_ERR_UNKNOWN;
        goto bail;
    }

    for (commentIdx = 0; commentIdx < commentNum; commentIdx++) {
        // comment len
        LocalFileRead(pTmpBuf, 4);
        commentLen = READ32BIT(pTmpBuf);
        LENGTH_CHECK(commentLen);

        if ((commentLen > dwTmpBufSize) || (commentLen < 3)) {
            LocalFileSeek(commentLen, SEEK_CUR);
            continue;
        }

        // comment, such as "ARTIST=me"
        LocalFileRead(pTmpBuf, commentLen);

        searchPos = 0;
        while ((searchPos < commentLen - 1) && (pTmpBuf[searchPos] != '=')) searchPos++;

        if (searchPos >= commentLen - 1) {
            err = PARSER_ERR_UNKNOWN;
            goto bail;
        }

        commentValueLen = commentLen - 1 - searchPos;
        pMetaDataTable = &(pParser->m_tMetaDataTable);

        if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "TITLE", 5) == 0) {
            pMetaDataTable->Title.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->Title.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->Title.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->Title.iDataLen = commentValueLen;
            pMetaDataTable->Title.eDataFormat = USER_DATA_FORMAT_UTF8;
        } else if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "ALBUM", 5) == 0) {
            pMetaDataTable->Album.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->Album.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->Album.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->Album.iDataLen = commentValueLen;
            pMetaDataTable->Album.eDataFormat = USER_DATA_FORMAT_UTF8;
        } else if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "TRACKNUMBER", 11) == 0) {
            pMetaDataTable->TrackNumber.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->TrackNumber.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->TrackNumber.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->TrackNumber.iDataLen = commentValueLen;
            pMetaDataTable->TrackNumber.eDataFormat = USER_DATA_FORMAT_UTF8;
        } else if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "ARTIST", 6) == 0) {
            pMetaDataTable->Artist.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->Artist.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->Artist.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->Artist.iDataLen = commentValueLen;
            pMetaDataTable->Artist.eDataFormat = USER_DATA_FORMAT_UTF8;
        } else if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "COPYRIGHT", 9) == 0) {
            pMetaDataTable->Copyright.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->Copyright.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->Copyright.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->Copyright.iDataLen = commentValueLen;
            pMetaDataTable->Copyright.eDataFormat = USER_DATA_FORMAT_UTF8;
        } else if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "ORGANIZATION", 12) == 0) {
            pMetaDataTable->Organization.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->Organization.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->Organization.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->Organization.iDataLen = commentValueLen;
            pMetaDataTable->Organization.eDataFormat = USER_DATA_FORMAT_UTF8;
        } else if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "DESCRIPTION", 11) == 0) {
            pMetaDataTable->Description.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->Description.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->Description.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->Description.iDataLen = commentValueLen;
            pMetaDataTable->Description.eDataFormat = USER_DATA_FORMAT_UTF8;
        } else if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "GENRE", 5) == 0) {
            pMetaDataTable->Genre.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->Genre.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->Genre.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->Genre.iDataLen = commentValueLen;
            pMetaDataTable->Genre.eDataFormat = USER_DATA_FORMAT_UTF8;
        } else if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "DATE", 4) == 0) {
            pMetaDataTable->Date.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->Date.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->Date.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->Date.iDataLen = commentValueLen;
            pMetaDataTable->Date.eDataFormat = USER_DATA_FORMAT_UTF8;
        } else if (INSENSITIVE_STRCMP((const char*)pTmpBuf, "PERFORMER", 9) == 0) {
            pMetaDataTable->Performer.pData = LocalMalloc(commentValueLen);
            if (pMetaDataTable->Performer.pData == NULL) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }
            memcpy(pMetaDataTable->Performer.pData, pTmpBuf + searchPos + 1, commentValueLen);

            pMetaDataTable->Performer.iDataLen = commentValueLen;
            pMetaDataTable->Performer.eDataFormat = USER_DATA_FORMAT_UTF8;
        }
    }

bail:
    if (pTmpBuf)
        LocalFree(pTmpBuf);

    LocalFileSeek(savePos, SEEK_SET);

    return err;
}

OGGPARSER_ERR_CODE Ogg_ParseHeaderInfo(OggParser* pParser) {
    uint32 i, streamNum, serialNumber;
    uint8 headerTempBuffer[HEADER_BUFFER_LEN];
    OggPageHeaderInfo* pHeaderInfo = &(pParser->currentPage);
    OggStream* pStream = NULL;
    OGGPARSER_ERR_CODE err;
    uint32 vorbisStreamNum = -1;
    pParser->numofStreams = 0;
    pParser->isParsingHead = TRUE;

SEEK_HEADER:

    pHeaderInfo->segIndex = 0;
    if ((err = Ogg_SeekPageHeader(pParser, &serialNumber, headerTempBuffer)))
        return err;
    if ((err = Ogg_ParsePageHeader(headerTempBuffer, HEADER_BUFFER_LEN, pHeaderInfo, pParser)))
        return err;
    if (pHeaderInfo->bodySize) {
        if (LocalFileRead(pHeaderInfo->bodyBuffer, pHeaderInfo->bodySize) < pHeaderInfo->bodySize)
            return PARSER_EOS;
    }

    if (IS_BOS(pHeaderInfo->headerTypeFlag)) {
        unsigned char* pSegData;
        uint32 segSize;
        uint32 value;

        if (pHeaderInfo->numofSegs < 1)
            goto SEEK_HEADER;

        pStream = &(pParser->Streams[pParser->numofStreams]);
        for (i = 0; i < pParser->numofStreams; i++)
            if (serialNumber == pParser->Streams[i].serialNum)
                break;
        if (i < pParser->numofStreams)
            goto SEEK_HEADER;

        // cache packet of the BOS page.
        pStream->isEnabled = TRUE;
        pStream->serialNum = serialNumber;

        while (pHeaderInfo->segIndex < pHeaderInfo->numofSegs) {
            stream_packet sPacket;

            if ((err = Ogg_ParseGetNextPacket(pParser, &streamNum, &(sPacket.pBufferData),
                                              &(sPacket.pDataContext), &(sPacket.size),
                                              &(sPacket.PTS), &(sPacket.flag))))
                return err;

            sPacket.offset = 0;

            if ((err = Ogg_AddCachedPackets(pParser, pStream, &sPacket)))
                return err;
        }

        pSegData = pStream->pCachedPackets[0].pBufferData;
        segSize = pStream->pCachedPackets[0].size;
        if (segSize < pHeaderInfo->SegTableBuffer[0])
            return OGGPARSER_PACKET_NOTENOUGHDATA;

        if (strncmp((const char*)pSegData, "\001vorbis", 7) == 0) {
            pStream = &(pParser->Streams[pParser->numofStreams]);
            vorbisStreamNum = pParser->numofStreams;
            pStream->type = OGG_VORBIS;
            pStream->serialNum = pHeaderInfo->bitSerial;

            pStream->MediaProperty.AudioProperty.channels = pSegData[11];
            value = pSegData[15];
            value = (value << 8) | pSegData[14];
            value = (value << 8) | pSegData[13];
            value = (value << 8) | pSegData[12];
            pStream->MediaProperty.AudioProperty.sampleRate = value;
            value = pSegData[19];
            value = (value << 8) | pSegData[18];
            value = (value << 8) | pSegData[17];
            value = (value << 8) | pSegData[16];
            pStream->MediaProperty.AudioProperty.maxBitrate = value;
            value = pSegData[23];
            value = (value << 8) | pSegData[22];
            value = (value << 8) | pSegData[21];
            value = (value << 8) | pSegData[20];
            pStream->MediaProperty.AudioProperty.averBitrate = value;
            value = pSegData[27];
            value = (value << 8) | pSegData[26];
            value = (value << 8) | pSegData[25];
            value = (value << 8) | pSegData[24];
            pStream->MediaProperty.AudioProperty.minBitrate = value;

            pParser->numofStreams++;

        }

        else if (strncmp((const char*)pSegData, "Speex   ", 8) == 0) {
            pStream = &(pParser->Streams[pParser->numofStreams]);
            pStream->type = OGG_SPEEX;
            pParser->numofStreams++;

        } else if (strncmp((const char*)pSegData, "fLaC", 4) == 0) {
            pStream = &(pParser->Streams[pParser->numofStreams]);
            pStream->type = OGG_FLAC;
            pParser->numofStreams++;
        } else if (strncmp((const char*)pSegData, "\177FLAC", 5) == 0) {
            pStream = &(pParser->Streams[pParser->numofStreams]);
            pStream->type = OGG_FLAC_NEW;
            pParser->numofStreams++;
        } else if (strncmp((const char*)pSegData, "\200theora", 7) == 0) {
            pStream = &(pParser->Streams[pParser->numofStreams]);
            pStream->type = OGG_THEORA;
            pParser->numofStreams++;
        } else if (strncmp((const char*)pSegData, "\001video\000\000\000", 9) == 0) {
            pStream = &(pParser->Streams[pParser->numofStreams]);
            pStream->type = OGG_VIDEO;
            pParser->numofStreams++;
        } else {
            pStream = &(pParser->Streams[pParser->numofStreams]);
            pStream->type = OGG_UNKNOWN;
            pParser->numofStreams++;
        }

        goto SEEK_HEADER;
    }

    else {
        if (vorbisStreamNum != (uint32)(-1)) {
            pStream = &(pParser->Streams[vorbisStreamNum]);
            pStream->codecPackets = 3;

            while (pStream->cachedNum < pStream->codecPackets) {
                stream_packet sPacket;
                if ((err = Ogg_ParseGetNextPacket(pParser, &streamNum, &(sPacket.pBufferData),
                                                  &(sPacket.pDataContext), &(sPacket.size),
                                                  &(sPacket.PTS), &(sPacket.flag))))
                    return err;

                sPacket.offset = 0;
                if (sPacket.flag & FLAG_SAMPLE_NOT_FINISHED)
                    pStream->codecPackets++;

                if ((err = Ogg_AddCachedPackets(pParser, &(pParser->Streams[streamNum]), &sPacket)))
                    return err;
            }

            pStream->codecInfoLen = 0;
            for (i = 0; i < pStream->codecPackets; i++)
                pStream->codecInfoLen += pStream->pCachedPackets[i].size;

            if (pStream->codecInfoLen != 0) {
                uint32 cpoffset = 0;
                pStream->pCodecInfo = LocalMalloc(pStream->codecInfoLen);
                if (pStream->pCodecInfo == NULL)
                    return PARSER_INSUFFICIENT_MEMORY;
                for (i = 0; i < pStream->codecPackets; i++) {
                    memcpy(pStream->pCodecInfo + cpoffset, pStream->pCachedPackets[i].pBufferData,
                           pStream->pCachedPackets[i].size);
                    cpoffset += pStream->pCachedPackets[i].size;
                    LocalFree(pStream->pCachedPackets[i].pBufferData);
                    pStream->pCachedPackets[i].pBufferData = NULL;
                    pStream->pCachedPackets[i].size = 0;
                }
                pStream->cPacketIndex = pStream->codecPackets;
            }
        }

        pParser->dataOffset = 0;  // LocalFileTell();

        pParser->isParsingHead = FALSE;
        return PARSER_SUCCESS;
    }
}

OGGPARSER_ERR_CODE Ogg_ResetParserContext(OggParser* pParser, uint32 streamNum, uint32 pageSequence,
                                          uint64 ganulePos) {
    OggPageHeaderInfo* pPageInfo = &(pParser->currentPage);
    OggStream* pStream = NULL;
    uint32 str, i;

    pParser->isLastSampleFinished = TRUE;
    pParser->lastSamplePTS = 0;
    pPageInfo->segIndex = pPageInfo->numofSegs;

    for (str = 0; str < pParser->numofStreams; str++) {
        pStream = &(pParser->Streams[str]);
        pStream->isFirstAfterSeek = TRUE;
        pStream->lastPageSequence = pageSequence;
        pStream->lastGranulePos = ganulePos;
        pStream->prevPartialPacket.size = 0;
        pStream->prevPartialPacket.offset = 0;

        for (i = 0; i < pStream->cachedNum; i++) {
            if (pStream->pCachedPackets[i].pBufferData)
                LocalFree(pStream->pCachedPackets[i].pBufferData);
            pStream->pCachedPackets[i].pBufferData = NULL;
        }
        pStream->cachedNum = 0;
        pStream->cPacketIndex = 0;
    }
    (void)streamNum;

    return PARSER_SUCCESS;
}

uint32 Ogg_GetNextPacketSize(OggPageHeaderInfo* pPageInfo, uint32* pPacketFlag) {
    uint32 nextPacketSize = 0;
    uint32 segIndex = pPageInfo->segIndex;
    uint8 nextSegSize;

    *pPacketFlag = FLAG_SAMPLE_NOT_FINISHED;

    for (; segIndex < pPageInfo->numofSegs; segIndex++) {
        nextSegSize = pPageInfo->SegTableBuffer[segIndex];
        nextPacketSize += nextSegSize;
        if (nextSegSize < 255) {
            *pPacketFlag = 0;
            break;
        }
    }

    if (nextPacketSize > pPageInfo->segOffset)
        nextPacketSize -= pPageInfo->segOffset;
    else
        nextPacketSize = 0;

    if (nextPacketSize > pPageInfo->bodySize - pPageInfo->bodyOffset)
        nextPacketSize = pPageInfo->bodySize - pPageInfo->bodyOffset;

    return nextPacketSize;
}

uint32 Ogg_ForwardNBytes(OggPageHeaderInfo* pPageInfo, uint32 nBytes) {
    uint32 segIndex = pPageInfo->segIndex;
    uint32 tBytes = nBytes;
    uint8 nextSegSize;

    tBytes += pPageInfo->segOffset;
    for (; segIndex < pPageInfo->numofSegs; segIndex++) {
        nextSegSize = pPageInfo->SegTableBuffer[segIndex];
        if (tBytes >= nextSegSize)
            tBytes -= nextSegSize;
        else {
            pPageInfo->segOffset = tBytes;
            break;
        }
    }

    pPageInfo->segIndex = segIndex;

    return 0;
}

OGGPARSER_ERR_CODE Ogg_CalcPTS(OggPageHeaderInfo* pPageInfo, OggStream* pStream, uint64* pPTS) {
    (void)pPageInfo;
    if (pStream->type == OGG_VORBIS) {
        uint32 samplerate = pStream->MediaProperty.AudioProperty.sampleRate;
        uint64 lastGranulePos = pStream->lastGranulePos;

        if (lastGranulePos != (uint64)(-1)) {
            if (samplerate != 0) {
                *pPTS = (lastGranulePos * 1000 / samplerate) * 1000;
                if (*pPTS > pStream->usDuration)
                    pStream->usDuration = *pPTS;
            } else
                *pPTS = 0;
        } else
            *pPTS = -1;

    } else
        *pPTS = -1;

    return PARSER_SUCCESS;
}

OGGPARSER_ERR_CODE Ogg_ParseGetNextPacket(OggParser* pParser, uint32* streamNum, uint8** packetData,
                                          void** pAppContext, uint32* dataSize, uint64* usPresTime,
                                          uint32* flag) {
    OggPageHeaderInfo* pPageInfo = &(pParser->currentPage);
    OGGPARSER_ERR_CODE err;
    OggStream* pStream;
    uint32 nextPacketSize;
    uint32 packetFlag;

    ParserOutputBufferOps* pRequestBufferOps = pParser->pRequestBufferOps;
    uint8* pPacketBuffer;
    void* bufferContext;
    uint32 bufferSize;
    uint64 presTime;

    (void)LocalFileTell();

    if (pPageInfo->segIndex >= pPageInfo->numofSegs) {
        // current page is finished, look for new page
        uint32 serialNumber = 0;
        char headerBuffer[HEADER_BUFFER_LEN];
        uint32 str;

        pStream = &(pParser->Streams[pPageInfo->streamNum]);
        pStream->lastGranulePos = pPageInfo->grandulePos;
        pStream->lastPageSequence = pPageInfo->pageSeqence;

    SEEK_PAGE_HEADER:

        if ((err = Ogg_SeekPageHeader(pParser, &serialNumber, (uint8*)headerBuffer)))
            return err;
        (void)LocalFileTell();

        for (str = 0; str < pParser->numofStreams; str++) {
            if ((pParser->Streams[str].serialNum == serialNumber) &&
                (pParser->Streams[str].isEnabled)) {
                pPageInfo->streamNum = str;
                break;
            }
        }

        if (str >= pParser->numofStreams)
            goto SEEK_PAGE_HEADER;

        if ((err = Ogg_ParsePageHeader((uint8*)headerBuffer, HEADER_BUFFER_LEN, pPageInfo,
                                       pParser)))
            return err;

        if (pPageInfo->bodySize) {
            uint32 freadSize = LocalFileRead(pPageInfo->bodyBuffer, pPageInfo->bodySize);
            if (freadSize < pPageInfo->bodySize)
                pPageInfo->bodySize = freadSize;
            if (freadSize == 0)
                return PARSER_EOS;
        }

#ifdef CHECK_CRC
        if (ogg_page_checksum_set(pPageInfo->HeaderBuffer, pPageInfo->bodyBuffer,
                                  pPageInfo->headerSize,
                                  pPageInfo->bodySize) != pPageInfo->checkSum) {
            Ogg_ForwardNBytes(pPageInfo, pPageInfo->bodySize);
            goto SEEK_PAGE_HEADER;
        }
#endif
    }

    pStream = &(pParser->Streams[pPageInfo->streamNum]);
    *streamNum = pPageInfo->streamNum;

    nextPacketSize = Ogg_GetNextPacketSize(pPageInfo, &packetFlag);

    if (((pPageInfo->pageSeqence > pStream->lastPageSequence + 1) || pStream->isFirstAfterSeek) &&
        (pPageInfo->headerTypeFlag & 0x01)) {
        stream_packet* pPacket = &(pStream->prevPartialPacket);
        pPacket->size = 0;
        pPacket->offset = 0;
        memset(pPacket->pBufferData, 0, MAX_PACKET_SIZE);
        pPageInfo->bodyOffset += nextPacketSize;
        Ogg_ForwardNBytes(pPageInfo, nextPacketSize);
        nextPacketSize = Ogg_GetNextPacketSize(pPageInfo, &packetFlag);
        if (nextPacketSize == 0) {
            pStream->lastGranulePos = pPageInfo->grandulePos;
            pStream->lastPageSequence = pPageInfo->pageSeqence;
            goto SEEK_PAGE_HEADER;
        }
        pPageInfo->headerTypeFlag &= ~(0x01);
    }

    /*
   if(pStream->isFirstAfterSeek)
   {
   uint8 packetType;
   LocalFileRead(&packetType, 1);
   while((packetType&0x01)!=0)
   {
   LocalFileSeek(nextPacketSize-1, SEEK_CUR);
   nextPacketSize = Ogg_GetNextPacketSize(pPageInfo,&packetFlag);
   if(nextPacketSize==0)
   {
     pStream->lastGranulePos = pPageInfo->grandulePos;
     pStream->lastPageSequence = pPageInfo->pageSeqence;
     goto SEEK_PAGE_HEADER;
   }
   LocalFileRead(&packetType, 1);
   }
   LocalFileSeek(-1,SEEK_CUR);
   }
   */

    stream_packet* pPacket = &(pStream->prevPartialPacket);
    if (pPageInfo->segIndex == 0 && pPacket->size > 0 && (!pParser->isParsingHead)) {
        bufferSize = pPacket->size;
        if (NULL ==
            (pPacketBuffer = pRequestBufferOps->RequestBuffer(pPageInfo->streamNum, &bufferSize,
                                                              &bufferContext, pParser->appContext)))
            return PARSER_ERR_NO_OUTPUT_BUFFER;
        else {
            if (bufferSize > pPacket->size)
                bufferSize = pPacket->size;
        }
        memcpy(pPacketBuffer, pPacket->pBufferData + pPacket->offset, bufferSize);
        pPacket->offset += bufferSize;
        pPacket->size -= bufferSize;

        *packetData = pPacketBuffer, *pAppContext = bufferContext;
        *dataSize = bufferSize;
        *usPresTime = PARSER_UNKNOWN_TIME_STAMP;
        *flag |= FLAG_SAMPLE_NOT_FINISHED;
        return PARSER_SUCCESS;
    }

    if (packetFlag && !pParser->isParsingHead) {
        memcpy(pPacket->pBufferData, pPageInfo->bodyBuffer + pPageInfo->bodyOffset, nextPacketSize);
        pPacket->size += nextPacketSize;
        pPacket->offset = 0;
        pStream->lastGranulePos = pPageInfo->grandulePos;
        pStream->lastPageSequence = pPageInfo->pageSeqence;
        Ogg_ForwardNBytes(pPageInfo, nextPacketSize);
        pPageInfo->bodyOffset += nextPacketSize;
        goto SEEK_PAGE_HEADER;
    }
    // Apply Output buffer and
    {
        bufferSize = nextPacketSize;
        if (pParser->isParsingHead) {
            pPacketBuffer = LocalMalloc(bufferSize);
            if (pPacketBuffer == NULL)
                return PARSER_INSUFFICIENT_MEMORY;
            bufferContext = NULL;
        } else {
            if (NULL ==
                (pPacketBuffer = pRequestBufferOps->RequestBuffer(
                         pPageInfo->streamNum, &bufferSize, &bufferContext, pParser->appContext)))
                return PARSER_ERR_NO_OUTPUT_BUFFER;
            else {
                if (bufferSize > nextPacketSize) {
                    bufferSize = nextPacketSize;
                }
            }
        }

        memcpy(pPacketBuffer, pPageInfo->bodyBuffer + pPageInfo->bodyOffset, bufferSize);
        pPageInfo->bodyOffset += bufferSize;

        if (pStream->isFirstAfterSeek) {
            if ((err = Ogg_CalcPTS(pPageInfo, pStream, &presTime)))
                return err;
            pStream->isFirstAfterSeek = FALSE;
        } else {
            presTime = PARSER_UNKNOWN_TIME_STAMP;
        }

        Ogg_ForwardNBytes(pPageInfo, bufferSize);

        *packetData = pPacketBuffer, *pAppContext = bufferContext;
        *dataSize = bufferSize;
        *usPresTime = presTime;
        *flag = 0;

        if (!pParser->isLastSampleFinished) {
            *usPresTime = pParser->lastSamplePTS;
        }

        if (packetFlag) {
            *flag |= packetFlag;
            pParser->isLastSampleFinished = FALSE;
            pParser->lastSamplePTS = presTime;
        } else if (bufferSize < nextPacketSize) {
            *flag |= FLAG_SAMPLE_NOT_FINISHED;
            pParser->isLastSampleFinished = FALSE;
            pParser->lastSamplePTS = presTime;
        } else {
            pParser->isLastSampleFinished = TRUE;
        }

        return PARSER_SUCCESS;
    }
}

/*
OGGPARSER_ERR_CODE Ogg_SeekVorbisDuration(OggParser* pParser, uint32 streamNum)
{

    uint64 curfileOffset = LocalFileTell();
    uint64 fileEnd = pParser->fileSize;
    OggStream *pStream = &(pParser->Streams[streamNum]);
       uint32 serialNumber;
       uint8 headerBuffer[HEADER_BUFFER_LEN];
    OGGPARSER_ERR_CODE err= PARSER_SUCCESS;
    uint64 usDuration= 0;


    if fileEnd ==)

      err=Ogg_SeekPageHeader(pParser, &serialNumber, headerBuffer);

}
*/

OGGPARSER_ERR_CODE Ogg_ParseScanStreamDuration(OggParser* pParser, uint32 streamNum) {
    uint64 fileSize = pParser->fileSize;
    OggStream* pStream = &(pParser->Streams[streamNum]);
    uint32 aVerageBitrate;

    if (pStream->type == OGG_VORBIS || pStream->type == OGG_FLAC || pStream->type == OGG_SPEEX ||
        pStream->type == OGG_FLAC_NEW)
        aVerageBitrate = pStream->MediaProperty.AudioProperty.averBitrate;
    else if (pStream->type == OGG_VIDEO || pStream->type == OGG_THEORA)
        aVerageBitrate = pStream->MediaProperty.VideoProperty.averBitRate;
    else
        return PARSER_ERR_INVALID_MEDIA;

    if (aVerageBitrate != 0) {
        pStream->usDuration = (((fileSize << 3) * 1000 / aVerageBitrate) * 1000);
        /*
          if(pStream->type == OGG_VORBIS)
          {

             return Ogg_SeekVorbisDuration(pParser,streamNum);
          }
          */
    } else {
        pStream->usDuration = 0;
    }

    return PARSER_SUCCESS;

    /*
    uint64 fileOffset = pParser->fileSize;
    uint64 searchStep = 65536+255+27;
    OggPageHeaderInfo tempPageInfo;
    uint32 serialNumber;
    OggStream *pStream = &(pParser->Streams[streamNum]);
    uint64 ptsStart,ptsEnd;
    uint64 fileOffsetStack = LocalFileTell();
    uint64 stackGranulePos = pStream->lastGranulePos;

    char headerBuffer[HEADER_BUFFER_LEN];
    OGGPARSER_ERR_CODE err;

    if(pStream->type!= OGG_VORBIS)
     return PARSER_ERR_NOT_SEEKABLE;

    while(searchStep>=fileOffset)
     searchStep = searchStep>>1

    fileOffset -= searchStep;

    if(LocalFileSeek(fileOffset, SEEK_SET)!=0)
    {
     err= PARSER_SEEK_ERROR;
     goto SCAN_RETURN;
    }


 SCAN_PAGE_HEADER:

       err=Ogg_SeekPageHeader(pParser, &serialNumber, headerBuffer));
       if(err==PARSER_EOS)
       {
          if(fileOffset >searchStep)
          {
              fileOffset -= searchStep;
           if(LocalFileSeek(fileOffset, SEEK_SET)!=0)
           {
                err =PARSER_SEEK_ERROR;
             goto SCAN_RETURN;
             }

         goto SCAN_PAGE_HEADER;
          }
       }
      else if(err!=PARSER_SUCCESS)
         goto SCAN_RETURN;

       if(err = Ogg_ParsePageHeader(headerBuffer,HEADER_BUFFER_LEN, &tempPageInfo, pParser))
         goto SCAN_RETURN;

       if(serialNumber!= pStream->serialNum)
       {
           if(err=LocalFileSeek(tempPageInfo.bodySize, SEEK_CUR))
             goto SCAN_RETURN;
         goto SCAN_PAGE_HEADER;
       }

        pStream->lastGranulePos = tempPageInfo.grandulePos;
     if(err=Ogg_CalcPTS(&tempPageInfo, pStream, &ptsEnd))
          goto SCAN_RETURN;

     pStream->usDuration = ptsEnd;


 SCAN_RETURN:

     pStream->lastGranulePos = stackGranulePos;
     if(LocalFileSeek(fileOffsetStack, SEEK_SET))
         return PARSER_SEEK_ERROR;
     return err;
     */
}

OGGPARSER_ERR_CODE Ogg_ForwardPackets(OggParser* pParser, uint32 streamNum, uint32 numPackets) {
    OggPageHeaderInfo* pPageInfo = &(pParser->currentPage);
    OGGPARSER_ERR_CODE err = PARSER_SUCCESS;
    OggStream* pStream;
    uint32 nextPacketSize;
    uint32 packetFlag;
    uint32 skipPackets = 0;

SEEK_SKIPPAGE:
    if (pPageInfo->segIndex >= pPageInfo->numofSegs) {
        // current page is finished, look for new page
        uint32 serialNumber = 0;
        char headerBuffer[HEADER_BUFFER_LEN];
        uint32 str;

        pStream = &(pParser->Streams[pPageInfo->streamNum]);
        pStream->lastGranulePos = pPageInfo->grandulePos;
        pStream->lastPageSequence = pPageInfo->pageSeqence;

    SEEK_SKIPPAGE_HEADER:

        if ((err = Ogg_SeekPageHeader(pParser, &serialNumber, (uint8*)headerBuffer)))
            return err;

        for (str = 0; str < pParser->numofStreams; str++) {
            if (pParser->Streams[str].serialNum == serialNumber) {
                pPageInfo->streamNum = str;
                break;
            }
        }

        if (str >= pParser->numofStreams)
            goto SEEK_SKIPPAGE_HEADER;

        if ((err = Ogg_ParsePageHeader((uint8*)headerBuffer, HEADER_BUFFER_LEN, pPageInfo,
                                       pParser)))
            return err;
    }

    if (pPageInfo->streamNum != streamNum) {
        LocalFileSeek(pPageInfo->bodySize, SEEK_CUR);
        pPageInfo->segIndex = pPageInfo->numofSegs;
        goto SEEK_SKIPPAGE;
    }

    nextPacketSize = Ogg_GetNextPacketSize(pPageInfo, &packetFlag);

    LocalFileSeek(nextPacketSize, SEEK_CUR);
    Ogg_ForwardNBytes(pPageInfo, nextPacketSize);
    nextPacketSize = Ogg_GetNextPacketSize(pPageInfo, &packetFlag);
    skipPackets++;

    if (skipPackets < numPackets)
        goto SEEK_SKIPPAGE;

    return PARSER_SUCCESS;
}

OGGPARSER_ERR_CODE Ogg_SeekStream(OggParser* pParser, uint32 streamNum, uint64* usTime,
                                  uint32 flag) {
    uint64 fileSize = pParser->fileSize;
    OggStream* pStream = &(pParser->Streams[streamNum]);
    uint64 usDuration = pStream->usDuration;
    uint32 sStep = 2048;
    uint64 fileOffset;
    uint64 fileOffsetStack;
    uint64 granulePosStack;
    uint64 cPTS = 0, ePTS = 0, stackGranulePos = 0;
    OggPageHeaderInfo tempPageInfo;

    OGGPARSER_ERR_CODE err = PARSER_SUCCESS;
    (void)flag;

    if (pStream->type != OGG_VORBIS)
        return PARSER_ERR_NOT_SEEKABLE;
    if (0 == usDuration && ((*usTime != 0) && (*usTime != (uint64)(-1))))
        return PARSER_ERR_NOT_SEEKABLE;

    if (*usTime == 0) {
        if ((err = LocalFileSeek(pParser->dataOffset, SEEK_SET)))
            return err;
        memset(&pParser->currentPage, 0, sizeof(OggPageHeaderInfo));
        err = Ogg_ResetParserContext(pParser, streamNum, 0, 0);
        // now igore the second and third packet
        err = Ogg_ForwardPackets(pParser, streamNum, pParser->Streams[streamNum].codecPackets);

        return err;
    }

    if (*usTime > usDuration) {
        fileOffset = fileSize;
        if (usDuration != 0)
            *usTime = usDuration;
    } else {
        if (usDuration > 1024)
            fileOffset = (fileSize) * ((*usTime) >> 10) / (usDuration >> 10);
        else
            fileOffset = fileSize / 2;
    }

    fileOffsetStack = LocalFileTell();
    granulePosStack = pStream->lastGranulePos;

    memset(&tempPageInfo, 0, sizeof(OggPageHeaderInfo));

    if (pStream->MediaProperty.AudioProperty.averBitrate != 0)
        sStep = (pStream->MediaProperty.AudioProperty.averBitrate >> 4);

    if (fileOffset > sStep)
        fileOffset -= sStep;
    if (LocalFileSeek(fileOffset, SEEK_SET) != 0) {
        err = PARSER_SEEK_ERROR;
        goto BAIL;
    }
    if (*usTime == 0)
        goto Reset_Info;

    {
        uint32 serialNumber;
        char headerBuffer[HEADER_BUFFER_LEN];
        bool isealierreached = FALSE;
        bool isnoeosreached = FALSE;
        uint32 dwScanHeadCount = 0;

    SCAN_PAGE_HEADER2:
        dwScanHeadCount++;
        // avoid dead loop in case of plug out usb
        if (dwScanHeadCount > 10000) {
            return PARSER_SEEK_ERROR;
        }

        err = Ogg_SeekPageHeader(pParser, &serialNumber, (uint8*)headerBuffer);
        if (err == PARSER_EOS) {
            if (isnoeosreached) {
                if (*usTime == (uint64)(-1))
                    *usTime = cPTS;
                goto BAIL;
            }

            while (fileOffset <= sStep) sStep = sStep >> 1;

            {
                fileOffset -= sStep;
                err = LocalFileSeek(fileOffset, SEEK_SET);
                if (err != 0)
                    goto BAIL;

                goto SCAN_PAGE_HEADER2;
            }

        } else
            isnoeosreached = TRUE;

        if ((err = Ogg_ParsePageHeader((uint8*)headerBuffer, HEADER_BUFFER_LEN, &tempPageInfo,
                                       pParser)))
            goto BAIL;

        if (serialNumber != pStream->serialNum) {
            if ((err = LocalFileSeek(tempPageInfo.bodySize, SEEK_CUR)))
                goto BAIL;
            goto SCAN_PAGE_HEADER2;
        }

        // success return
        pStream->lastGranulePos = tempPageInfo.grandulePos;
        if ((err = Ogg_CalcPTS(&tempPageInfo, pStream, &cPTS)))
            goto BAIL;

        if (*usTime > cPTS) {
            fileOffset = LocalFileTell() + tempPageInfo.bodySize;
            ePTS = cPTS;
            stackGranulePos = tempPageInfo.grandulePos;
            isealierreached = TRUE;
            goto SCAN_PAGE_HEADER2;
        } else if (*usTime < cPTS) {
            if (!isealierreached) {
                if (fileOffset > sStep) {
                    fileOffset -= sStep;
                    if ((err = LocalFileSeek(fileOffset, SEEK_SET)))
                        goto BAIL;
                    goto SCAN_PAGE_HEADER2;
                }
                fileOffset = LocalFileTell() + tempPageInfo.bodySize;
            }
        } else {
            fileOffset = LocalFileTell() + tempPageInfo.bodySize;
            ePTS = cPTS;
        }

        if (isealierreached) {
            cPTS = ePTS;
            tempPageInfo.grandulePos = stackGranulePos;
        }
    }

    // success

    memcpy(&pParser->currentPage, &tempPageInfo, sizeof(OggPageHeaderInfo));
    *usTime = cPTS;
    if ((err = LocalFileSeek(fileOffset, SEEK_SET)))
        goto BAIL;
    pParser->currentPage.segIndex = pParser->currentPage.numofSegs;

Reset_Info:

    err = Ogg_ResetParserContext(pParser, streamNum, tempPageInfo.pageSeqence,
                                 tempPageInfo.grandulePos);

    // seek successfully
    if (err == PARSER_SUCCESS)
        return err;
// fail return
BAIL:
    if (LocalFileSeek(fileOffsetStack, SEEK_SET) != 0)
        return PARSER_SEEK_ERROR;
    pStream->lastGranulePos = granulePosStack;
    return err;
}
