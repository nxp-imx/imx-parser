/*
***********************************************************************
* Copyright (c) 2011-2016, Freescale Semiconductor, Inc.
* Copyright 2017-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
#include "mpeg2_parser_internal.h"
#include "h264parser.h"
#include "hevcparser.h"
#include "mpeg2_epson_exvi.h"
#include "mpeg2_parser_api.h"
#include "parse_ps_context.h"
#include "parse_ps_sc.h"
#include "parse_ts.h"
#include "parse_ts_context.h"
#include "parse_ts_defines.h"

// FslFileStream  g_streamOps;
// ParserMemoryOps g_memOps;

// #define MPG2_PARSER_DBG
#ifdef MPG2_PARSER_DBG
#define MPG2_PARSER_INTERNAL_LOG printf
#define MPG2_PARSER_INTERNAL_ERR printf
// #define MPG2_PARSER_DUMP
#else
#define MPG2_PARSER_INTERNAL_LOG(...)
#define MPG2_PARSER_INTERNAL_ERR(...)
#endif
#define ASSERT(exp)                                                                            \
    if (!(exp)) {                                                                              \
        MPG2_PARSER_INTERNAL_ERR("%s: %d : assert condition !!!\r\n", __FUNCTION__, __LINE__); \
    }

#define H264_FRAME_BUFFER_SIZE 1024 * 512

#ifdef ANDROID_BUILD
#include <android/log.h>
#define LOG_BUF_SIZE 1024

void LogOutput(const char* fmt, ...) {
    va_list ap;
    char buf[LOG_BUF_SIZE];
    va_start(ap, fmt);
    vsnprintf(buf, LOG_BUF_SIZE, fmt, ap);
    va_end(ap);
    __android_log_write(ANDROID_LOG_INFO, "MPEG2 PARSER", buf);
    return;
}

#define LOG_PRINTF LogOutput
#endif

#ifdef MPG2_PARSER_DUMP
#define MAX_DUMP_ID 10

void mpeg2_parser_memory(unsigned char* addr, int width, int height, int stride) {
    int i, j;
    unsigned char* ptr;

    ptr = addr;
    MPG2_PARSER_INTERNAL_LOG("addr: 0x%X \r\n", (unsigned int)addr);
    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            MPG2_PARSER_INTERNAL_LOG("%2X ", ptr[j]);
        }
        MPG2_PARSER_INTERNAL_LOG("\r\n");
        ptr += stride;
    }
    MPG2_PARSER_INTERNAL_LOG("\r\n");
    return;
}

void mpeg2_parser_DumpSampleRaw(FILE** ppFp, unsigned char* pBits, unsigned int nSize,
                                unsigned int id) {
    if (nSize == 0) {
        return;
    }

    if (*ppFp == NULL) {
        unsigned char filename[255];
        sprintf(filename, "temp_mpg2_parser_%d.bit", id);
        *ppFp = fopen(filename, "wb");
        if (*ppFp == NULL) {
            MPG2_PARSER_INTERNAL_LOG("open %s failure \r\n", filename);
            return;
        }
        MPG2_PARSER_INTERNAL_LOG("open %s OK \r\n", filename);
    }

    fwrite(pBits, 1, nSize, *ppFp);
    fflush(*ppFp);
    return;
}

void mpeg2_parser_DumpSamplePTS(FILE** ppFp, unsigned int nSize, uint64 time, unsigned int flag,
                                unsigned int id) {
    static int cnt[MAX_DUMP_ID] = {0};
    static int framelen[MAX_DUMP_ID] = {0};
    static int framenum[MAX_DUMP_ID] = {0};

    if (*ppFp == NULL) {
        unsigned char filename[255];
        sprintf(filename, "temp_mpg2_parser_%d.log", id);
        *ppFp = fopen(filename, "wb");
        if (*ppFp == NULL) {
            MPG2_PARSER_INTERNAL_LOG("open %s failure \r\n", filename);
            return;
        }
        MPG2_PARSER_INTERNAL_LOG("open %s OK \r\n", filename);
        cnt[id] = 1;
        framelen[id] = 0;
        framenum[id] = 1;
    }

    fprintf(*ppFp, "[%d]:   size: %d,   time: %lld,   flag: 0x%X \r\n", cnt[id], nSize, time, flag);
    cnt[id]++;
    framelen[id] = framelen[id] + nSize;
    if (0 == (flag & FLAG_SAMPLE_NOT_FINISHED)) {
        fprintf(*ppFp, "frame[%d]:   framesize: %d,   time: %lld,   flag: 0x%X \r\n", framenum[id],
                framelen[id], time, flag);
        framenum[id]++;
        framelen[id] = 0;
    }
    fflush(*ppFp);
    return;
}
#endif

MPEG2_PARSER_ERROR_CODE Mpeg2CreateParserInternal(FslFileStream* stream, ParserMemoryOps* memOps,
                                                  ParserOutputBufferOps* requestBufferOps,
                                                  void* context, uint32 flags,
                                                  FslParserHandle* parserHandle) {
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;
    MPEG2ObjectPtr self = NULL;
    FSL_MPG_DEMUX_CNXT_T* pDemuxCnxt;
    FslFileHandle fileHandle;
    MPEG2_PLAYMODE playMode = FILE_MODE;
    bool bNeedH264Convert = TRUE;

    *parserHandle = NULL;

    if ((flags & FILE_FLAG_NON_SEEKABLE) && (flags & FILE_FLAG_READ_IN_SEQUENCE)) {
        playMode = STREAMING_MODE;
    }
    if (flags & FLAG_H264_NO_CONVERT) {
        bNeedH264Convert = FALSE;
    }

#ifdef MPEG2_MEM_DEBUG_SELF
    mm_mm_init();
#endif

    self = memOps->Malloc(sizeof(MPEG2Object));
    TESTMALLOC(self)
    memset(self, 0, sizeof(MPEG2Object));

    memcpy(&(self->sMemOps), memOps, sizeof(ParserMemoryOps));
    self->memOps = &(self->sMemOps);

    /* try to open the source stream */
    fileHandle = stream->Open(NULL, (const uint8*)"rb", context);
    if (fileHandle == NULL) {
        MPEG2MSG("MPEG2CreateParser: error: can not open source stream.\n");
        BAILWITHERROR(PARSER_FILE_OPEN_ERROR)
    }

    self->fileHandle = fileHandle;

    self->fileSize = (uint64)stream->Size(fileHandle, context);
    self->playMode = playMode;
    if (flags & FLAG_OUTPUT_H264_SEI_POS_DATA)
        self->bNeedH264SeiPosData = TRUE;

    if (flags & FLAG_FETCH_AAC_ADTS_CSD)
        self->bForceGetAacAdtsCsd = TRUE;

    if (self->fileSize == 0) {
        MPG2_PARSER_INTERNAL_LOG("can't get the file size, enable stream mode \r\n");
        self->playMode = STREAMING_MODE;
    }

    memcpy(&(self->sIputStream), stream, sizeof(FslFileStream));
    self->inputStream = &(self->sIputStream);

    self->bNeedH264Convert = bNeedH264Convert;
    self->appContext = context;

    memcpy(&(self->sRequestBufferOps), requestBufferOps, sizeof(ParserOutputBufferOps));
    self->pRequestBufferOps = &(self->sRequestBufferOps);

    if (self->playMode == FILE_MODE) {
#ifdef SUPPORT_LARGE_MPEG2_FILE
        if (MIN_MPEG2_FILE_SIZE >= (int64)self->fileSize)
#else
        if (MIN_MPEG2_FILE_SIZE >= (int32)self->fileSize)
#endif
        {
            MPEG2MSG("error: file size %lld is bad or exceeds parser's capacity!\n",
                     self->fileSize);
            BAILWITHERROR(MPEG2_ERR_WRONG_FILE_SIZE)
        }
    }

    pDemuxCnxt = (FSL_MPG_DEMUX_CNXT_T*)memOps->Malloc(sizeof(FSL_MPG_DEMUX_CNXT_T));
    TESTMALLOC(pDemuxCnxt);
    {
        ResetCnxt(pDemuxCnxt);
        self->pDemuxContext = pDemuxCnxt;
        pDemuxCnxt->SeqHdrBuf.pSH = (U8*)memOps->Malloc(MAX_SEQHDRBUF_SIZE);
        TESTMALLOC(pDemuxCnxt->SeqHdrBuf.pSH);
        pDemuxCnxt->SeqHdrBuf.BufLen = MAX_SEQHDRBUF_SIZE;
    }

    self->outputMode = OUTPUT_BYFILE;

    err = Mpeg2ParseHeaderInternal(self);
    if (err == PARSER_SUCCESS) {
        if (flags & FLAG_OUTPUT_PCR)
            pDemuxCnxt->TSCnxt.bOutputPCR = TRUE;
    }

    self->streamEnabled = 0;
    if (self->playMode != STREAMING_MODE) {  // for stream mode, need to avoid seek backward
        self->fileOffset = 0;
    }

    self->random_access = 0;
bail:
    if (PARSER_SUCCESS != err) {
        if (self) {
            Mpeg2DeleteParser((FslParserHandle)self);
            self = NULL;
        }

#ifdef MPEG2_MEM_DEBUG_SELF
        mm_mm_exit();
#endif
    } else {
        *parserHandle = (FslParserHandle)self;
        MPEG2MSG("Mpeg2CreateParser:parser created successfully\n");
    }

    return err;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ParserInitialIndex(MPEG2ObjectPtr pDemuxer, uint32 dwTrackIdx) {
    int i = dwTrackIdx;
    FSL_MPG_DEMUX_SYSINFO_T* pSysInfo = &(pDemuxer->SystemInfo);
    MPEG2_Index* pIndex;

    if ((U32)i >= pSysInfo->uliNoStreams) {
        return PARSER_SUCCESS;
    }

    pIndex = &(pDemuxer->index[i]);

    if (pSysInfo->Stream[i].enuStreamType != FSL_MPG_DEMUX_VIDEO_STREAM ||
        pDemuxer->playMode != FILE_MODE) {
        memset(pIndex, 0, sizeof(MPEG2_Index));
        return PARSER_SUCCESS;
    }

    pIndex->version = 2;
    pIndex->dwTrackIdx = i;
    pIndex->status = 0;
    pIndex->offsetbytes = (pDemuxer->fileSize > (uint64)0x7FFFFFFF ? 8 : 4);
    memset(pIndex->reserved, 0, 12 * sizeof(uint8));
    pIndex->period = 500;  // set period
    pIndex->itemcount = (pDemuxer->usLongestStreamDuration + 999) / 1000 / pIndex->period;

    if (pIndex->itemcount > 0) {
        pIndex->pts = LOCALMalloc(sizeof(uint64) * pIndex->itemcount);
        if (NULL == pIndex->pts)
            return PARSER_INSUFFICIENT_MEMORY;
        pIndex->pItem = LOCALMalloc(pIndex->offsetbytes * pIndex->itemcount);
        if (NULL == pIndex->pItem)
            return PARSER_INSUFFICIENT_MEMORY;

        memset(pIndex->pItem, INDEX_NOSCANNED, pIndex->offsetbytes * pIndex->itemcount);
        memset(pIndex->pts, PARSER_UNKNOWN_TIME_STAMP, sizeof(uint64) * pIndex->itemcount);
    } else
        pIndex->pItem = NULL;

    pIndex->lastitem = -1;
    pIndex->lastdir = 0;
    pIndex->lastQueriedRWItem = -1;
    pIndex->lastQueriedFWItem = -1;
    pIndex->rewardPTS = PARSER_UNKNOWN_TIME_STAMP;
    pIndex->forwardPTS = PARSER_UNKNOWN_TIME_STAMP;
    pIndex->indexbreak = FALSE;
    pIndex->indexdirection = 0;  // forward

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ParserUpdateIndex(MPEG2ObjectPtr pDemuxer, U32 dwTrackIdx, U64 offset,
                                               U64 PTS) {
    U32* pindex4 = (U32*)pDemuxer->index[dwTrackIdx].pItem;
    U64* pindex8 = (U64*)pDemuxer->index[dwTrackIdx].pItem;
    U32 itemCount;

    if (NULL == pDemuxer->index[dwTrackIdx].pItem || 0 == pDemuxer->index[dwTrackIdx].itemcount)
        return PARSER_ERR_UNKNOWN;
    if (PTS == (U64)PARSER_UNKNOWN_TIME_STAMP)
        return PARSER_SUCCESS;

    itemCount = (PTS + 999) / 1000 / pDemuxer->index[dwTrackIdx].period;

    if (itemCount >= pDemuxer->index[dwTrackIdx].itemcount)
        itemCount = pDemuxer->index[dwTrackIdx].itemcount - 1;  // return
                                                                // MPEG2_ERR_INDEX_OUTOFRANGE?

    if (pDemuxer->index[dwTrackIdx].offsetbytes == 4) {
        if (pindex4[itemCount] == (uint32)INDEX_NOSCANNED) {
            pindex4[itemCount] = (uint32)offset;
            pDemuxer->index[dwTrackIdx].pts[itemCount] = PTS;
        } else if (itemCount == pDemuxer->index[dwTrackIdx].itemcount - 1) {
            if (offset > pindex4[itemCount]) {
                pindex4[itemCount] = (uint32)offset;
                pDemuxer->index[dwTrackIdx].pts[itemCount] = PTS;
            }
        }
    } else {
        if (pindex8[itemCount] == (U64)INDEX_NOSCANNED) {
            pindex8[itemCount] = offset;
            pDemuxer->index[dwTrackIdx].pts[itemCount] = PTS;
        } else if (itemCount == pDemuxer->index[dwTrackIdx].itemcount - 1) {
            if (offset > pindex8[itemCount]) {
                pindex8[itemCount] = offset;
                pDemuxer->index[dwTrackIdx].pts[itemCount] = PTS;
            }
        }
    }

    pDemuxer->index[dwTrackIdx].lastitem = itemCount;

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ParserQueryIndex(MPEG2ObjectPtr pDemuxer, uint32 dwTrackIdx,
                                              uint64 PTS, uint32 direction, uint64* pOffset) {
#ifdef AVOID_INDEX_DEADLOOP
#define MAX_INDEX_DEADLOOP (10)
    static int deadloop_cnt = 0;
#endif

    uint32* pindex4 = (uint32*)pDemuxer->index[dwTrackIdx].pItem;
    uint64* pindex8 = (uint64*)pDemuxer->index[dwTrackIdx].pItem;
    uint32 item = 0;
    MPEG2_Index* pIndex = &(pDemuxer->index[dwTrackIdx]);

    *pOffset = INDEX_NOSCANNED;

    if (0 == pIndex->itemcount)
        return PARSER_SEEK_ERROR;

    if (PTS == (uint64)PARSER_UNKNOWN_TIME_STAMP) {
        return PARSER_ERR_INVALID_PARAMETER;
    } else {
        item = (PTS + 999) / 1000 / pIndex->period;
    }

    if (item >= pIndex->itemcount)
        item = pIndex->itemcount - 1;

#ifdef AVOID_INDEX_DEADLOOP
    if ((item == pIndex->lastitem) && (direction == pIndex->lastdir)) {
        deadloop_cnt++;
        if (deadloop_cnt > MAX_INDEX_DEADLOOP) {
            if (1 == direction) {
                MPG2_PARSER_INTERNAL_ERR("forward dead loop \r\n");
                return PARSER_EOS;
            }
            if (2 == direction) {
                MPG2_PARSER_INTERNAL_ERR("backward dead loop \r\n");
                return PARSER_BOS;
            }
        }
    } else {
        deadloop_cnt = 0;
        pIndex->lastitem = item;
        pIndex->lastdir = direction;
    }
#endif
    bool is4bytes = pIndex->offsetbytes == 4;

    switch (direction) {
        case 0:
            *pOffset = is4bytes ? (int64)pindex4[item] : (int64)pindex8[item];
            return PARSER_SUCCESS;
            break;
        case 1:
            while (item < pIndex->itemcount) {
                if (item == (U32)pIndex->lastQueriedFWItem ||
                    (is4bytes && pindex4[item] == (uint32)INDEX_NOSCANNED) ||
                    (!is4bytes && pindex8[item] == (uint64)INDEX_NOSCANNED)) {
                    item++;
                } else
                    break;
            }

            // In case of FF, repeat on wince, if just return EOS, no chance to rw again.
            //  Set *pOffset to INDEX_NOSCANNED, can search from the begin if repeat rw.
            //  If not repeat, can still return EOS in Mpeg2ParserScan.
            if (item >= pIndex->itemcount) {
                *pOffset = INDEX_NOSCANNED;
                return PARSER_EOS;
            }

            *pOffset = is4bytes ? (uint64)pindex4[item] : (uint64)pindex8[item];
            if (isIndexRangeContinuous(pDemuxer->pIndexRange, pDemuxer->fileOffset, *pOffset)) {
                pIndex->lastQueriedFWItem = item;
                pIndex->forwardPTS = pIndex->pts[item];
                return PARSER_SUCCESS;
            } else {
                *pOffset = INDEX_NOSCANNED;
                return MPEG2_ERR_CORRUPTED_INDEX;
            }
        case 2:
            while (item > 0) {
                if (item >= (U32)pIndex->lastQueriedRWItem ||
                    (is4bytes && pindex4[item] == (uint32)INDEX_NOSCANNED) ||
                    (!is4bytes && pindex8[item] == (uint64)INDEX_NOSCANNED)) {
                    if (0 == item) {
                        *pOffset = INDEX_NOSCANNED;
                        return PARSER_BOS;
                    } else
                        item--;
                } else
                    break;
            }

            *pOffset = is4bytes ? (uint64)pindex4[item] : (uint64)pindex8[item];
            if (isIndexRangeContinuous(pDemuxer->pIndexRange, *pOffset, pDemuxer->fileOffset)) {
                pIndex->lastQueriedRWItem = item;
                pIndex->rewardPTS = pIndex->pts[item];
                return PARSER_SUCCESS;
            } else {
                // printf("rw not found for PTS %lld\n", PTS);
                *pOffset = INDEX_NOSCANNED;
                return MPEG2_ERR_CORRUPTED_INDEX;
            }
        default:
            return PARSER_ERR_INVALID_PARAMETER;
    }

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ParserQueryIndexRange(MPEG2ObjectPtr pDemuxer, uint32 dwTrackIdx,
                                                   uint64 PTS, int32* pItems, uint64* pOffsets) {
    // will define a range thread, in this range we can find top and bottom range, the range is set
    // to 10 seconds, beyond this range, pItems[0]   top item pItems[1]   middle  item pItems[2]
    // bottom   item

    int32 item = (PTS + 999) / 1000 / pDemuxer->index[dwTrackIdx].period;
    int32 i;
    uint32* pindex4 = (uint32*)pDemuxer->index[dwTrackIdx].pItem;
    uint64* pindex8 = (uint64*)pDemuxer->index[dwTrackIdx].pItem;

    if (0 == pDemuxer->index[dwTrackIdx].itemcount)
        return MPEG2_ERR_EMPTY_INDEX;

    pItems[0] = pItems[1] = pItems[2] = INDEX_NOSCANNED;
    pOffsets[0] = pOffsets[1] = pOffsets[2] = (uint64)INDEX_NOSCANNED;

    if ((uint32)item >= pDemuxer->index[dwTrackIdx].itemcount)
        item = pDemuxer->index[dwTrackIdx].itemcount - 1;

    if (pDemuxer->index[dwTrackIdx].pts[item] == PTS) {
        if (pDemuxer->index[dwTrackIdx].offsetbytes == 4 &&
            pindex4[item] != (uint32)INDEX_NOSCANNED) {
            pItems[1] = item;
            pOffsets[1] = (uint64)pindex4[item];
        } else if (pDemuxer->index[dwTrackIdx].offsetbytes == 8 &&
                   pindex8[item] != (uint64)INDEX_NOSCANNED) {
            pItems[1] = item;
            pOffsets[1] = pindex8[item];
        }
    }

    i = item;
    while (i >= 0) {
        if (pDemuxer->index[dwTrackIdx].offsetbytes == 4 && pindex4[i] != (uint32)INDEX_NOSCANNED &&
            pDemuxer->index[dwTrackIdx].pts[i] < PTS) {
            pItems[0] = i;
            pOffsets[0] = (uint64)pindex4[i];
            break;
        } else if (pDemuxer->index[dwTrackIdx].offsetbytes == 8 &&
                   pindex8[i] != (uint64)INDEX_NOSCANNED &&
                   pDemuxer->index[dwTrackIdx].pts[i] < PTS) {
            pItems[0] = i;
            pOffsets[0] = pindex8[i];
            break;
        }
        i--;
    }

    i = item;
    while ((U32)i <= pDemuxer->index[dwTrackIdx].itemcount - 1) {
        if (pDemuxer->index[dwTrackIdx].offsetbytes == 4 && pindex4[i] != (uint32)INDEX_NOSCANNED &&
            pDemuxer->index[dwTrackIdx].pts[i] > PTS) {
            pItems[2] = i;
            pOffsets[2] = (uint64)pindex4[i];
            break;
        } else if (pDemuxer->index[dwTrackIdx].offsetbytes == 8 &&
                   pindex8[i] != (uint64)INDEX_NOSCANNED &&
                   pDemuxer->index[dwTrackIdx].pts[i] > PTS) {
            pItems[2] = i;
            pOffsets[2] = pindex8[i];
            break;
        }
        i++;
    }

    if (pDemuxer->index[dwTrackIdx].status == 0) {
        if (pOffsets[0] != (uint64)INDEX_NOSCANNED && pOffsets[2] != (uint64)INDEX_NOSCANNED &&
            isIndexRangeContinuous(pDemuxer->pIndexRange, pOffsets[0], pOffsets[2])) {
            return PARSER_SUCCESS;
        }
        return MPEG2_ERR_EMPTY_INDEX;
    } else if (pItems[0] >= 0 || pItems[1] >= 0 || pItems[2] >= 0)
        return PARSER_SUCCESS;
    else
        return MPEG2_ERR_EMPTY_INDEX;
}

U32 NextNBufferBytes(U8* pInput, int n, U32* Offset) {
    U8 Byte;
    int i;
    U32 LocalOffset;
    U32 Ret = 0;

    LocalOffset = *Offset;
    for (i = 0; i < n; i++) {
        Byte = pInput[LocalOffset];
        Ret = (Ret << 8) | Byte;
        LocalOffset++;
    }

    *Offset = LocalOffset;
    return Ret;
};

MPEG2_PARSER_ERROR_CODE MPEG2ParserReadBuffer(MPEG2ObjectPtr pDemuxer, uint32 streamNum, U8** pBuf,
                                              unsigned int size) {
    U32 sizeRead = 0;

    sizeRead = MPEG2FileRead(pDemuxer, streamNum, pBuf, size);
    if (sizeRead < size)
        return PARSER_EOS;
    else
        return PARSER_SUCCESS;
}

uint64 MPEG2FilePos(MPEG2ObjectPtr pDemuxer, uint32 streamNum) {
    if (pDemuxer->playMode == FILE_MODE) {
        if (pDemuxer->streamEnabled && pDemuxer->SystemInfo.Stream[streamNum].isBlocked) {
            return pDemuxer->SystemInfo.Stream[streamNum].fileOffset;
        } else
            return pDemuxer->fileOffset;
    } else
        return pDemuxer->fileOffset;
}

MPEG2_PARSER_ERROR_CODE MPEG2FileSeek(MPEG2ObjectPtr pDemuxer, uint32 streamNum, int64 offset,
                                      int32 whence) {
    int64 seekpos = offset + whence;  // make sure whence is SEEK_SET
    U64* pFileOffset;

    if (pDemuxer->playMode == FILE_MODE) {
        if ((U64)seekpos > pDemuxer->fileSize) {
            printf("MPEG2FileSeek seekpos(%lld) > filesize(%lld)\n", seekpos, pDemuxer->fileSize);
            return PARSER_SEEK_ERROR;
        }

        if (pDemuxer->streamEnabled && pDemuxer->SystemInfo.Stream[streamNum].isBlocked) {
            pFileOffset = &(pDemuxer->SystemInfo.Stream[streamNum].fileOffset);
        } else {
            pFileOffset = &(pDemuxer->fileOffset);
        }
        *pFileOffset = seekpos;
    } else {
        // for the streamming mode which will forbit the fileseek
        pFileOffset = &(pDemuxer->fileOffset);
        if ((U64)seekpos <= pDemuxer->CacheBuffer.cacheFileOffset &&
            (U64)seekpos + pDemuxer->CacheBuffer.cacheFilled >=
                    pDemuxer->CacheBuffer.cacheFileOffset)
            *pFileOffset = seekpos;
#if 0  // avoid seek backward in stream mode
        else if(seekpos ==0)
            *pFileOffset = 0;
#endif
        else {
            MPG2_PARSER_INTERNAL_ERR(
                    "warning: seek pos overpass the cache region, skip current seeking \r\n");
        }
    }

    return PARSER_SUCCESS;
}

// This is the only place allowed to call LocalFileRead
uint32 MPEG2FileRead(MPEG2ObjectPtr pDemuxer, uint32 streamNum, U8** pData, uint32 dataSize) {
    CACHEBUFFER* pCacheBuffer = &(pDemuxer->CacheBuffer);
    U64 cacheFileOffset, fileOffset, *pFileOffset;
    U32 isBlocked = 0;
    U32 total_cache_size;
    U32 cache_size;
    U32 cache_block_size;
    U32 half_block_size;

    if (STREAMING_MODE == pDemuxer->playMode) {
        total_cache_size = 16 * 1024;
        cache_size = 8 * 1024;
        cache_block_size = 2 * 1024;
        half_block_size = 4 * 1024;
    } else {
        total_cache_size = 256 * 1024;
        cache_size = 128 * 1024;
        cache_block_size = 32 * 1024;
        half_block_size = 64 * 1024;
    }

    // init FILE IO cache
    if (pCacheBuffer->pCacheBuffer == NULL) {
        pCacheBuffer->pCacheBuffer = (U8*)LOCALMalloc(total_cache_size);
        if (pCacheBuffer->pCacheBuffer == NULL) {
            // there is insufficient memory here
            return 0;
        } else {
            pCacheBuffer->cacheFilled = 0;
            pCacheBuffer->cacheFileOffset = 0;
            pCacheBuffer->pLowAddress = pCacheBuffer->pCacheBuffer + half_block_size;
            pCacheBuffer->pHighAddress = pCacheBuffer->pLowAddress + cache_size;
            pCacheBuffer->pReadAddress = pCacheBuffer->pLowAddress;  // where read begins
        }
    }

    if (pDemuxer->playMode == FILE_MODE) {
        if (pDemuxer->streamEnabled && pDemuxer->SystemInfo.Stream[streamNum].isBlocked) {
            if (!pDemuxer->SystemInfo.Stream[streamNum].isEnabled)
                return 0;
            fileOffset = pDemuxer->SystemInfo.Stream[streamNum].fileOffset;
            pFileOffset = &(pDemuxer->SystemInfo.Stream[streamNum].fileOffset);
            isBlocked = 1;
        } else {
            fileOffset = pDemuxer->fileOffset;
            pFileOffset = &(pDemuxer->fileOffset);
        }
    } else {
        fileOffset = pDemuxer->fileOffset;
        pFileOffset = &(pDemuxer->fileOffset);
    }

    // first to check if file position is out of the main cache,
    // if it is, cache will be refreshed
    if (pDemuxer->playMode == FILE_MODE) {
        if (fileOffset < (pCacheBuffer->cacheFileOffset - pCacheBuffer->cacheFilled) ||
            fileOffset > pCacheBuffer->cacheFileOffset) {
            LocalFileSeek(fileOffset, SEEK_SET);
            pCacheBuffer->cacheFileOffset = fileOffset;
            pCacheBuffer->cacheFilled = 0;
            pCacheBuffer->pReadAddress = pCacheBuffer->pLowAddress;
        }
    } else {
        if (fileOffset < (pCacheBuffer->cacheFileOffset - pCacheBuffer->cacheFilled) ||
            fileOffset > pCacheBuffer->cacheFileOffset) {
            if (0 == fileOffset) {
                LocalFileSeek(fileOffset, SEEK_SET);
                pCacheBuffer->cacheFileOffset = fileOffset;
                pCacheBuffer->cacheFilled = 0;
                pCacheBuffer->pReadAddress = pCacheBuffer->pLowAddress;
            } else {
                MPG2_PARSER_INTERNAL_ERR(
                        "warning: read pos overpass cache region, skip current read operation "
                        "\r\n");
                return MPEG2_ERR_FILE_READ_POS;
            }
        }
    }

    while ((pCacheBuffer->cacheFilled < cache_block_size) ||
           (fileOffset + dataSize > pCacheBuffer->cacheFileOffset)) {
        S32 readBytes;
        U32 targetReadBytes = cache_block_size;
        U32 temp = (pCacheBuffer->pReadAddress - pCacheBuffer->pLowAddress) % cache_block_size;
        if (temp > 0)
            targetReadBytes = cache_block_size - temp;
        readBytes = LocalFileRead(pCacheBuffer->pReadAddress, targetReadBytes);
        if (readBytes < 0)
            break;  // the return value maybe negative when meet eos/error.
        pCacheBuffer->cacheFilled += readBytes;
        if (pCacheBuffer->cacheFilled > cache_size)
            pCacheBuffer->cacheFilled = cache_size;

        pCacheBuffer->cacheFileOffset += readBytes;
        // pReadAddress points to the last byte of ring cache
        pCacheBuffer->pReadAddress += readBytes;
        if (pCacheBuffer->pReadAddress == pCacheBuffer->pHighAddress)
            pCacheBuffer->pReadAddress = pCacheBuffer->pLowAddress;
        // maybe at the end of the file
        if ((U32)readBytes < targetReadBytes) {
            break;
        }
    }
    cacheFileOffset = pCacheBuffer->cacheFileOffset;

    // now the data is in the cache, just return the correct location
    {
        // readOffset points to the last byte
        U32 readOffset = pCacheBuffer->pReadAddress - pCacheBuffer->pLowAddress;
        // the offset in the ring buffer
        U32 Offset = fileOffset - (cacheFileOffset - pCacheBuffer->cacheFilled);

        // readOffset points to the first byte
        readOffset = readOffset + cache_size - pCacheBuffer->cacheFilled;

        if (readOffset >= cache_size)
            readOffset -= cache_size;

        // Offset points to the location where read begins
        Offset += readOffset;
        if (Offset >= cache_size)
            Offset -= cache_size;
        if (dataSize > cacheFileOffset - fileOffset)
            dataSize = cacheFileOffset - fileOffset;

        if (Offset + dataSize < cache_size) {
            *pData = pCacheBuffer->pLowAddress + Offset;

            *pFileOffset += dataSize;
            if (isBlocked) {
                if (*pFileOffset >= pDemuxer->fileOffset) {
                    pDemuxer->fileOffset = *pFileOffset;
                    pDemuxer->SystemInfo.Stream[streamNum].isBlocked = 0;
                }
            }
            return dataSize;
        } else {
            // need memcopy here
            U32 partHSize = cache_size - Offset;
            U32 partLSize = dataSize - (cache_size - Offset);
            if (cache_size - Offset < dataSize - (cache_size - Offset)) {
                memcpy(pCacheBuffer->pLowAddress - partHSize,
                       pCacheBuffer->pHighAddress - partHSize, partHSize);
                *pData = (pCacheBuffer->pLowAddress - partHSize);
            } else {
                memcpy(pCacheBuffer->pHighAddress, pCacheBuffer->pLowAddress, partLSize);
                *pData = pCacheBuffer->pHighAddress - partHSize;
            }

            *pFileOffset += dataSize;
            if (isBlocked) {
                if (*pFileOffset >= pDemuxer->fileOffset) {
                    pDemuxer->fileOffset = *pFileOffset;
                    pDemuxer->SystemInfo.Stream[streamNum].isBlocked = 0;
                }
            }
            return dataSize;
        }
    }
}

MPEG2_PARSER_ERROR_CODE MPEG2ParserNextNBytes(MPEG2ObjectPtr parserHandle, uint32 streamNum,
                                              uint32 n, uint32* pRet) {
    U32 Ret = 0;
    unsigned char* Value;
    U32 sizeRead;
    U32 i;

#ifdef DEMUX_DEBUG
    assert(n <= 4);
#endif

    sizeRead = MPEG2FileRead(parserHandle, streamNum, &Value, n);

    if (sizeRead == 0)
        return PARSER_EOS;
    if (sizeRead != n)
        return PARSER_INSUFFICIENT_DATA;

    for (i = 0; i < n; i++) Ret = (Ret << 8) | Value[i];
    *pRet = Ret;

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE MPEG2ParserRewindNBytes(MPEG2ObjectPtr pDemuxer, uint32 streamNum,
                                                uint32 n) {
    /*
    if(n>*pFileOffset)
    return PARSER_ERR_INVALID_PARAMETER;
    *pFileOffset -=n;
    */
    uint64* pFileOffset;

    if (pDemuxer->streamEnabled && pDemuxer->SystemInfo.Stream[streamNum].isBlocked) {
        pFileOffset = &(pDemuxer->SystemInfo.Stream[streamNum].fileOffset);
        if (*pFileOffset >= n)
            *pFileOffset -= n;
        else
            *pFileOffset = 0;
    } else {
        pFileOffset = &(pDemuxer->fileOffset);
        if (*pFileOffset >= n)
            *pFileOffset -= n;
        else
            *pFileOffset = 0;
    }
    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE MPEG2ParserForwardNBytes(MPEG2ObjectPtr pDemuxer, uint32 streamNum,
                                                 uint32 n) {
    FSL_MPEGSTREAM_T* pStreamInfo = (FSL_MPEGSTREAM_T*)&(pDemuxer->SystemInfo.Stream[streamNum]);

    if (pStreamInfo->isBlocked) {
        pStreamInfo->fileOffset += n;
        if (pStreamInfo->fileOffset >= pDemuxer->fileOffset) {
            pDemuxer->fileOffset = pStreamInfo->fileOffset;
            pStreamInfo->isBlocked = 0;
        }
        if (pDemuxer->playMode == FILE_MODE)
            if (pStreamInfo->fileOffset >= pDemuxer->fileSize) {
                pStreamInfo->fileOffset = pDemuxer->fileSize;
                return PARSER_EOS;
            }

    } else {
        pDemuxer->fileOffset += n;
        if (pDemuxer->fileOffset > pDemuxer->fileSize) {
            if (0 == pDemuxer->fileSize) {
                MPG2_PARSER_INTERNAL_LOG("warning: stream mode, can't check EOS \r\n");
                return PARSER_SUCCESS;
            }
            return PARSER_EOS;
        }
    }
    return PARSER_SUCCESS;
}

#define AC3_STARTCODE 0x0b77
#define MPG2_STARTCODE 0x00000100

// determine the frame structure of the picture
// pStartPos points to the position after picture start code 00000100

U8 NextOneBits(U8* pBuf, U32 startLoc) {
    U32 Bytes = startLoc / 8;
    U32 Pos = startLoc % 8;
    U8 byte = pBuf[Bytes];

    byte = (byte << Pos);
    byte = (byte >> (7 - Pos));
    return byte;
}

// ref to is13818-2.pdf, 6.2.3 Picture header
int MPEG2FindFrameStructure(U8* pStartPos, U32 segSize, bool* pIsSizeEnough) {
    U8 key = 0;
    U32 picHeaderBits = 29;
    U32 picHeaderBytes = 0;
    U32 fourBytes = 0xFFFFFFFF;
    U32 structure = 0;

    *pIsSizeEnough = TRUE;

    if (segSize < 4) {
        *pIsSizeEnough = FALSE;
        return 0;
    }

    key = pStartPos[1];
    key = (key << 2);
    key = (key >> 5);

    if (key == MPG2_PFRAME_TYPE)
        picHeaderBits += 4;
    else if (key == MPG2_BFRAME_TYPE)
        picHeaderBits += 8;

    while (NextOneBits(pStartPos, picHeaderBits)) {
        picHeaderBits += 1;
        picHeaderBits += 8;
    }
    picHeaderBits += 1;

    picHeaderBytes = picHeaderBits / 8;
    if ((picHeaderBits % 8) != 0)
        picHeaderBytes++;
    // fix ENGR00175224, picture_coding_extension field is 6 byte.
    // fix ENGR177045
    if (picHeaderBytes + 6 >= segSize) {
        *pIsSizeEnough = FALSE;
        return key;
    }

    fourBytes = (fourBytes << 8) | pStartPos[picHeaderBytes];
    fourBytes = (fourBytes << 8) | pStartPos[picHeaderBytes + 1];
    fourBytes = (fourBytes << 8) | pStartPos[picHeaderBytes + 2];
    fourBytes = (fourBytes << 8) | pStartPos[picHeaderBytes + 3];

    if (fourBytes != MPG2_EXTENSION_START_CODE)
        return key;

    structure = (pStartPos[picHeaderBytes + 6] & 0x03);
    if (structure == MPG2_TOP_FIELD) {
        structure = (structure << 8) | key;
        return structure;
    } else if (structure == MPG2_BOTTOM_FIELD) {
        structure = (structure << 8) | key;
        return structure;
    } else
        return key;
}

#define MPEG4_VOS 0x000001B0
#define MPEG4_VOP 0x000001B6

U32 MPEG4HalfStartcodeLen(U32 fourbytes) {
    if (0x000001 == (fourbytes & 0xFFFFFF))
        return 3;
    else if (0x0 == (fourbytes & 0xFFFF))
        return 2;
    else if (0x0 == (fourbytes & 0xFF))
        return 1;
    else
        return 0;
}

U32 MPEG4CodingType2Mpeg2(U32 codingType) {
    switch (codingType) {
        case 0x0:
            return MPG2_IFRAME_TYPE;
        case 0x01:
            return MPG2_PFRAME_TYPE;
        case 0x10:
            return MPG2_BFRAME_TYPE;
        default:
            return MPG2_UNKNOWN_FRAMETYPE;
    }
}

// As there may exist multiple frames in on packet,
// we have to do scan from the beginning!

MPEG2_PARSER_ERROR_CODE MPEG2FindMPEG2Frames(MPEG2ObjectPtr pDemuxer, U8* pNewSeg, U32* pNewSegSize,
                                             FSL_MPEGSTREAM_T* pStreamInfo, U32* pFrameOffsets,
                                             U32* pCount, U32* frameTypes, U8** ppSplitLocation,
                                             S32* pStartFill) {
    U32 fourbytes = 0xFFFFFFFF, frameOffset = 0xFFFFFFFF;
    U32 i;
    U8 byte;
    U32 count = 0;
    U32 lastFourBytes = pStreamInfo->lastFourBytes;
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    U32 newSegSize = *pNewSegSize;
    bool isSizeEnough, isFrameStart;
    *pCount = 0;

    *pStartFill = 0;
    *ppSplitLocation = NULL;

    fourbytes = lastFourBytes;
    for (i = 0; i < newSegSize; i++) {
        byte = pNewSeg[i];
        fourbytes = (fourbytes << 8) | byte;
        if (frameOffset == 0xFFFFFFFF &&
            (fourbytes == MPG2_STARTCODE || fourbytes == MPEG2_SEQUENCE_HEADER)) {
            frameOffset = i;
        }
        if (fourbytes == MPG2_STARTCODE) {
            if (pDemuxer->playMode == FILE_MODE)
                frameOffset = i;

            fourbytes = 0xFFFFFFFF;
            *frameTypes = MPEG2FindFrameStructure((U8*)(pNewSeg + i + 1), newSegSize - i - 1,
                                                  &isSizeEnough);

            if (!isSizeEnough) {
                // we can not decide the frame type yet
                pStreamInfo->tFrameBuffer[0] = 0x00;
                pStreamInfo->tFrameBuffer[1] = 0x00;
                pStreamInfo->tFrameBuffer[2] = 0x01;
                pStreamInfo->tFrameBuffer[3] = 0x00;

                memcpy((pStreamInfo->tFrameBuffer + 4), pNewSeg + i + 1, newSegSize - i - 1);
                pStreamInfo->payloadInsert = 1;
                pStreamInfo->tFrameBufferFilled = (newSegSize - i - 1 + 4);
                pStreamInfo->tFrameBufferSync = pDemuxer->lastsyncoffset;

                if (count > 0)
                    pStreamInfo->frameBufferPTS = PARSER_UNKNOWN_TIME_STAMP;
                else
                    pStreamInfo->frameBufferPTS = pStreamInfo->currentPTS;

                *pCount = count;
                *pNewSegSize -= pStreamInfo->tFrameBufferFilled;
                return Err;
            }

            isFrameStart = TRUE;
            if (*frameTypes & 0xFF00) {
                isFrameStart = (0 == pStreamInfo->filedCounter);
                if (isFrameStart) {
                    *frameTypes = *frameTypes & 0xFF;
                    pStreamInfo->filedCounter = 1;
                } else {
                    pStreamInfo->filedCounter = 0;
                }
            }

            if (isFrameStart) {
                count++;
                if (frameOffset >= 3)
                    *pFrameOffsets++ = frameOffset - 3;
                else {
                    *pStartFill = 3 - frameOffset;
                    *ppSplitLocation = pNewSeg;
                    MPG2_PARSER_INTERNAL_LOG(
                            "frame start code is split in different pes buf: need to fill %d bytes "
                            "\r\n",
                            *pStartFill);
                    *pFrameOffsets++ = 0;
                }
                frameTypes++;
            }
            frameOffset = 0xFFFFFFFF;
        }
    }
    *pCount = count;
    return PARSER_SUCCESS;
}

int MPEG2FastFindMPEG2Frames(MPEG2ObjectPtr pDemuxer, U8* pNewSeg, U32 newSegSize,
                             U32 lastFourBytes, FSL_MPEGSTREAM_T* pStreamInfo) {
    U32 fourbytes = 0xFFFFFFFF;
    U32 i;
    U8 byte;
    U8 key;
    U32 flag = 0;
    U32 videoType;

    // audio stream don't support fast find sync frame
    if (pStreamInfo->enuStreamType != FSL_MPG_DEMUX_VIDEO_STREAM)
        return 0;

    videoType = pStreamInfo->MediaProperty.VideoProperty.enuVideoType;

    if (videoType == FSL_MPG_DEMUX_H264_VIDEO || videoType == FSL_MPG_DEMUX_HEVC_VIDEO) {
        int ret = PARSER_ERR_UNKNOWN;

        if (FSL_MPG_DEMUX_HEVC_VIDEO == pStreamInfo->MediaProperty.VideoProperty.enuVideoType) {
            ret = FindHevcKeyFrame(pStreamInfo->pParser, pNewSeg, newSegSize);
        } else {
            ret = FindH264KeyFrame(pStreamInfo->pParser, pNewSeg, newSegSize);
        }
        if (PARSER_SUCCESS == ret) {
            flag |= FLAG_SYNC_SAMPLE;
        }
        return flag;
    } else if (videoType == FSL_MPG_DEMUX_MP4_VIDEO) {
        // handle mpeg video
        fourbytes = lastFourBytes;
        for (i = 0; i < newSegSize; i++) {
            fourbytes = (fourbytes << 8) | pNewSeg[i];
            if (MPEG4_VOP == fourbytes) {
                U32 frameType = MPEG4CodingType2Mpeg2(pNewSeg[i + 1] >> 6);
                if (MPG2_IFRAME_TYPE == frameType) {
                    flag |= FLAG_SYNC_SAMPLE;
                    return flag;
                }
            }
        }
        pStreamInfo->lastFourBytes = fourbytes;
    } else {  // process Mpeg2

        if (pStreamInfo->codecSpecInformation != NULL) {
            fourbytes = lastFourBytes;

            while (newSegSize > 0) {
                byte = *(pNewSeg++);
                newSegSize--;
                fourbytes = (fourbytes << 8) | byte;
                if (fourbytes == MPG2_STARTCODE) {
                    fourbytes = 0xFFFFFFFF;
                    if (2 < newSegSize) {
                        key = pNewSeg[1];
                        key = (key << 2);
                        key = (key >> 5);
                        if (key == MPG2_IFRAME_TYPE) {
                            flag |= FLAG_SYNC_SAMPLE;
                            return flag;
                        } else {
                            key = 0;
                            fourbytes = 0xFFFFFFFF;
                            continue;
                        }
                    }
                }
            }
        }

        else {
            U32 offset1 = -1, offset2 = -1, SHLen = 0;
            fourbytes = lastFourBytes;
            for (i = 0; i < newSegSize; i++) {
                byte = pNewSeg[i];
                fourbytes = (fourbytes << 8) | byte;
                if (fourbytes == MPEG2_SEQUENCE_HEADER) {
                    offset1 = i - 3;
                    fourbytes = 0xFFFFFFFF;

                    U32 LoadIntraMatrix = 0, LoadInterMatrix = 0;
                    if (i + 8 < newSegSize) {
                        LoadIntraMatrix = ((pNewSeg[i + 8] >> 1) & 0x1);
                        LoadInterMatrix = (pNewSeg[i + 8] & 0x1);
                    }
                    if (LoadIntraMatrix && (i + 8 + 64) < newSegSize)
                        LoadInterMatrix = (pNewSeg[i + 8 + 64] & 0x1);

                    SHLen = 12 + (LoadIntraMatrix + LoadInterMatrix) * 64;
                    if (offset1 + SHLen > newSegSize - 1)
                        SHLen = newSegSize - 1 - SHLen;
                } else if (fourbytes == MPG2_STARTCODE) {
                    offset2 = i - 3;
                    fourbytes = 0xFFFFFFFF;
                    if ((i + 2) < newSegSize) {
                        key = pNewSeg[i + 2];
                        key = (key << 2);
                        key = (key >> 5);
                        if (key == MPG2_IFRAME_TYPE) {
                            flag |= FLAG_SYNC_SAMPLE;
                        }
                    }
                    if (offset2 > offset1) {
                        pStreamInfo->codecSpecInformation = LOCALMalloc(offset2 - offset1);
                        pStreamInfo->codecSpecInfoSize = offset2 - offset1;
                        MPG2_PARSER_INTERNAL_LOG("find codec spec info,  size: %d \r\n",
                                                 pStreamInfo->codecSpecInfoSize);
                        memcpy(pStreamInfo->codecSpecInformation, pNewSeg + offset1,
                               pStreamInfo->codecSpecInfoSize);
                    }
                    break;
                }
            }

            if (pStreamInfo->codecSpecInformation == NULL && SHLen > 0) {
                pStreamInfo->codecSpecInformation = LOCALMalloc(SHLen);
                pStreamInfo->codecSpecInfoSize = SHLen;
                memcpy(pStreamInfo->codecSpecInformation, pNewSeg + offset1,
                       pStreamInfo->codecSpecInfoSize);
            }
        }
    }
    return flag;
}

MPEG2_PARSER_ERROR_CODE MPEG2FindMPEG4Frames(MPEG2ObjectPtr pDemuxer, U32 streamNum, U8* pNewSeg,
                                             U32 newSegSize) {
    U32 fourbytes = 0xFFFFFFFF;
    U32 i;
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);
    U32 lastFourBytes = pStreamInfo->lastFourBytes;
    U32 lastState = pStreamInfo->lastMP4State;
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;
    U32 vop_coding_type = 0xFF;
    bool dropFrame = FALSE;
    U64 usPresTime;
    U32 frameType = MPG2_UNKNOWN_FRAMETYPE;
    U32 frameOffset = 0;
    U32 frameSize = 0;
    U32 flag = FLAG_SAMPLE_NOT_FINISHED;
    U32 startcodeLen = 0;
    U8* pFrame = NULL;
    U8* pTempFrame = NULL;

    fourbytes = lastFourBytes;
    for (i = 0; i < newSegSize; i++) {
        fourbytes = (fourbytes << 8) | pNewSeg[i];
        if (MPEG4_VOS == fourbytes) {
            if (FSL_MPG_DEMUX_MP4_START == lastState) {
                lastState = FSL_MPG_DEMUX_MP4_VOS;
                frameOffset = (i >= 3 ? i - 3 : 0);
                continue;
            }
            lastState = FSL_MPG_DEMUX_MP4_VOS;
        } else if (MPEG4_VOP == fourbytes) {
            vop_coding_type = (pNewSeg[i + 1] >> 6);
            frameType = MPEG4CodingType2Mpeg2(vop_coding_type);
            if (FSL_MPG_DEMUX_MP4_START == lastState) {
                lastState = FSL_MPG_DEMUX_MP4_VOP;
                frameOffset = (i >= 3 ? i - 3 : 0);
                continue;
            } else if (FSL_MPG_DEMUX_MP4_VOS == lastState) {
                lastState = FSL_MPG_DEMUX_MP4_VOP;
                continue;
            }
            lastState = FSL_MPG_DEMUX_MP4_VOP;
        } else if (i != newSegSize - 1)
            continue;

        if (pStreamInfo->isLastSampleSync) {
            usPresTime = pStreamInfo->cachedPTS;
            pStreamInfo->cachedPTS = PARSER_UNKNOWN_TIME_STAMP;
        } else {
            usPresTime = pStreamInfo->currentPTS;
            pStreamInfo->cachedPTS = pStreamInfo->currentPTS;
            pStreamInfo->currentPTS = PARSER_UNKNOWN_TIME_STAMP;
        }

        if (i == newSegSize - 1) {
            pStreamInfo->isLastSampleSync = 1;
            flag = FLAG_SAMPLE_NOT_FINISHED;

            /*check half start code*/
            startcodeLen = MPEG4HalfStartcodeLen(fourbytes);
            frameSize = newSegSize - frameOffset - startcodeLen;
        } else {
            pStreamInfo->isLastSampleSync = 0;
            flag &= (~FLAG_SAMPLE_NOT_FINISHED);
            if (i < 3)
                frameSize = 0;
            else
                frameSize = i - 3 - frameOffset;
        }

        if (pStreamInfo->tFrameBufferFilled > 0 && 0 == frameOffset && frameSize > 0) {
            pTempFrame = (U8*)malloc(frameSize + pStreamInfo->tFrameBufferFilled);
            if (!pTempFrame)
                return PARSER_ERR_UNKNOWN;
            memcpy(pTempFrame, pStreamInfo->tFrameBuffer, pStreamInfo->tFrameBufferFilled);
            memcpy(pTempFrame + pStreamInfo->tFrameBufferFilled, pNewSeg, frameSize);
            frameSize += pStreamInfo->tFrameBufferFilled;
            pFrame = pTempFrame;
            pStreamInfo->tFrameBufferFilled = 0;
        } else {
            pFrame = pNewSeg + frameOffset;
        }

        if (MPG2_IFRAME_TYPE == frameType)
            flag |= FLAG_SYNC_SAMPLE;

        if (pStreamInfo->isFirstAfterSeek) {
            if (flag & FLAG_SYNC_SAMPLE) {
                pStreamInfo->isFirstAfterSeek = 0;
            } else {
                // drop non-key frame after seek, need to check seek function.
                err = Mpeg2ResetOuputBuffer(pDemuxer, streamNum);
                dropFrame = TRUE;
            }
        }

        if (!dropFrame) {
            err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum, pFrame, &frameSize, flag,
                                            usPresTime, 0, 0);
        }

        if (startcodeLen > 0) {
            pStreamInfo->tFrameBufferFilled = startcodeLen;
            memset(pStreamInfo->tFrameBuffer, 0, 4);
            if (startcodeLen == 3)
                pStreamInfo->tFrameBuffer[3] = 0x01;
        }

        dropFrame = FALSE;
        flag = FLAG_SAMPLE_NOT_FINISHED;
        frameOffset = (i >= 3 ? i - 3 : 0);
        frameType = MPEG4CodingType2Mpeg2(vop_coding_type);
    }

    pStreamInfo->lastFourBytes = fourbytes;
    pStreamInfo->lastMP4State = lastState;
    if (pTempFrame)
        free(pTempFrame);
    return err;
}

// #define DEBUG_HEADER
#ifdef DEBUG_HEADER

void printAudioInfo(FSL_AUDIO_PROPERTY_T* pAudioProp) {
    MPG2_PARSER_INTERNAL_LOG("AudioType:%u\n", pAudioProp->enuAudioType);
    MPG2_PARSER_INTERNAL_LOG("Samplerate:%u\n", pAudioProp->uliAudioSampleRate);
    MPG2_PARSER_INTERNAL_LOG("Channels:%u\n", pAudioProp->usiAudioChannels);
    MPG2_PARSER_INTERNAL_LOG("ChannelMode:%u\n", pAudioProp->enuAudioChannelMode);
    MPG2_PARSER_INTERNAL_LOG("Bitrate:%u\n", pAudioProp->uliAudioBitRate);
}

void printVideoInfo(FSL_VIDEO_PROPERTY_T* pVideoProp) {
    /*
    FSL_MPG_DEMUX_VIDEO_TYPE_T    enuVideoType;
    U32                           uliVideoWidth;
    U32                           uliVideoHeight;
    U32                           uliVideoBitRate;
    U32                           uliFRNumerator;
    U32                           uliFRDenominator;
    */

    MPG2_PARSER_INTERNAL_LOG("VideoType:%u\n", pVideoProp->enuVideoType);
    MPG2_PARSER_INTERNAL_LOG("Width:%u\n", pVideoProp->uliVideoWidth);
    MPG2_PARSER_INTERNAL_LOG("Height:%u\n", pVideoProp->uliVideoHeight);
    MPG2_PARSER_INTERNAL_LOG("Bitrate:%u\n", pVideoProp->uliVideoBitRate);
    MPG2_PARSER_INTERNAL_LOG("FRNumerator:%u\n", pVideoProp->uliFRNumerator);
    MPG2_PARSER_INTERNAL_LOG("FRDenoinator:%u\n", pVideoProp->uliFRDenominator);
}

#endif

MPEG2_PARSER_ERROR_CODE Mpeg2ParserCodecSpecificInfo(MPEG2ObjectPtr pDemuxer, uint32 i) {
    U64 origFileOffset, fileOffset1, fileOffset2, PTS;
    U32 seekFlag;
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[i]);
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;

    if (pStreamInfo->isBlocked)
        pStreamInfo->isBlocked = FALSE;
    if (pStreamInfo->codecSpecInformation != NULL)
        return PARSER_SUCCESS;

    origFileOffset = fileOffset2 = MPEG2FilePos(pDemuxer, i);

    while (pStreamInfo->codecSpecInformation == NULL) {
        seekFlag = 0;
        fileOffset1 = fileOffset2;
        Err = Mpeg2ParserScan(pDemuxer, i, &fileOffset1, &fileOffset2, &PTS, &seekFlag, 0);
        if (Err != PARSER_SUCCESS)
            return Err;
        if (fileOffset1 - origFileOffset > MAX_PARSE_HEAD_SIZE)
            break;
    }

    if (pDemuxer->playMode == FILE_MODE)
        pDemuxer->fileOffset = origFileOffset;
    else {
        if (origFileOffset + pDemuxer->CacheBuffer.cacheFilled >=
            pDemuxer->CacheBuffer.cacheFileOffset)
            pDemuxer->fileOffset = origFileOffset;
        else {
            MPG2_PARSER_INTERNAL_LOG(
                    "can't backward to previous location, some data will be discard \r\n");
            pDemuxer->fileOffset =
                    pDemuxer->CacheBuffer.cacheFileOffset - pDemuxer->CacheBuffer.cacheFilled;
        }
    }

    return Err;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ParseHeaderInternal(MPEG2ObjectPtr pDemuxer) {
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;
    FSL_MPEGSTREAM_T* pStream = &(pDemuxer->SystemInfo.Stream[0]);
    U32 i;
    U64 startTime;

    if ((err = MPEG2FileSeek(pDemuxer, 0, 0, SEEK_SET)))
        return err;

    if ((err = MPEG2ParserGetPSI(pDemuxer)))
        return err;

    err = MPEG2ParserProbe(pDemuxer);
    if (err != PARSER_SUCCESS && err != PARSER_EOS)
        return err;

    RemapProgram(pDemuxer);

    pDemuxer->pDemuxContext->ProbeCnxt.Probed = 1;
    pDemuxer->usLongestStreamDuration = 0;
    pDemuxer->PTSDeta = 0;

    // Initalize the splitter
    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        if (pStream[i].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
            if (pStream[i].MediaProperty.VideoProperty.enuVideoType == FSL_MPG_DEMUX_H264_VIDEO) {
                uint32 flags = pDemuxer->bNeedH264SeiPosData ? FLAG_OUTPUT_H264_SEI_POS_DATA : 0;
                err = CreateH264Parser(&(pStream[i].pParser), pDemuxer->memOps, flags);
                if (err != PARSER_SUCCESS)
                    return err;
            } else if (pStream[i].MediaProperty.VideoProperty.enuVideoType ==
                       FSL_MPG_DEMUX_HEVC_VIDEO) {
                err = CreateHevcParser(&(pStream[i].pParser), pDemuxer->memOps);
                if (err != PARSER_SUCCESS)
                    return err;
            }
        }

        if (pStream[i].enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM) {
            if (pStream[i].MediaProperty.AudioProperty.enuAudioType == FSL_MPG_DEMUX_AAC_AUDIO &&
                pStream[i].MediaProperty.AudioProperty.enuAudioSubType == FSL_MPG_DEMUX_AAC_RAW) {
                err = CreateAacLatmParser(&(pStream[i].pParser), pDemuxer->memOps);
                if (err != PARSER_SUCCESS)
                    return err;
            }
        }
    }

    if (pDemuxer->playMode == FILE_MODE) {
        if ((pDemuxer->TS_PSI.IsTS == 0) ||
            (pDemuxer->pDemuxContext->TSCnxt.PAT.NonProgramSelected == 1)) {
            for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
                err = Mpeg2ParserScanStreamDuration(pDemuxer, i);
                if (MPEG2_ERR_NO_DURATION == err)
                    err = PARSER_SUCCESS;
            }

            startTime = (U64)0xFFFFFFFFFFFFFFFFULL;
            for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
                if (startTime > pStream[i].startTime)
                    startTime = pStream[i].startTime;
            }

            for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
                pStream[i].startTime = startTime;
                if (pStream[i].usDuration != 0)
                    pStream[i].usDuration = pStream[i].endTime - startTime;
                if (pStream[i].usDuration > pDemuxer->usLongestStreamDuration)
                    pDemuxer->usLongestStreamDuration = pStream[i].usDuration;
            }
        } else {
            U32 dwProgramIdx;
            U32 dwValidTrackIdx;
            U32 i = 0;  // track in the user view
            FSL_MPG_DEMUX_TS_CNXT_T* pTSCnxt = &(pDemuxer->pDemuxContext->TSCnxt);

            for (dwProgramIdx = 0; dwProgramIdx < pTSCnxt->nParsedPMTs; dwProgramIdx++) {
                for (dwValidTrackIdx = 0;
                     dwValidTrackIdx < pTSCnxt->PMT[dwProgramIdx].ValidTrackNum;
                     dwValidTrackIdx++) {
                    i = pTSCnxt->PMT[dwProgramIdx].adwValidTrackIdx[dwValidTrackIdx];
                    err = Mpeg2ParserScanStreamDuration(pDemuxer, i);
                    if (MPEG2_ERR_NO_DURATION == err)
                        err = PARSER_SUCCESS;
                }

                startTime = (U64)0xFFFFFFFFFFFFFFFFULL;
                for (dwValidTrackIdx = 0;
                     dwValidTrackIdx < pTSCnxt->PMT[dwProgramIdx].ValidTrackNum;
                     dwValidTrackIdx++) {
                    i = pTSCnxt->PMT[dwProgramIdx].adwValidTrackIdx[dwValidTrackIdx];

                    if (startTime > pStream[i].startTime)
                        startTime = pStream[i].startTime;
                }

                for (dwValidTrackIdx = 0;
                     dwValidTrackIdx < pTSCnxt->PMT[dwProgramIdx].ValidTrackNum;
                     dwValidTrackIdx++) {
                    i = pTSCnxt->PMT[dwProgramIdx].adwValidTrackIdx[dwValidTrackIdx];

                    pStream[i].startTime = startTime;
                    if (pStream[i].usDuration != 0)
                        pStream[i].usDuration = pStream[i].endTime - startTime;
                    if (pStream[i].usDuration > pDemuxer->usLongestStreamDuration)
                        pDemuxer->usLongestStreamDuration = pStream[i].usDuration;
                }
            }  // for dwProgramIdx
        }
    } else {
        // streaming mode
        for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
            pStream[i].startTime = 0;
            pStream[i].usDuration = 0;
        }
    }

    // now set the fileOffset of eachstream
    pDemuxer->streamEnabled = TRUE;

    if (pDemuxer->playMode == FILE_MODE) {
        pDemuxer->fileOffset = 0;
    } else {
        pDemuxer->fileOffset =
                pDemuxer->CacheBuffer.cacheFileOffset - pDemuxer->CacheBuffer.cacheFilled;
        MPG2_PARSER_INTERNAL_LOG("stream mode: start location: %lld \r\n", pDemuxer->fileOffset);
    }

    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        pStream[i].frameBuffer = NULL;
        pStream[i].frameBufferLength = 0;
        InitOuputBufArray(pDemuxer->memOps, &(pStream[i].outputArray));
        pStream[i].isBlocked = 0;
        if ((err = Mpeg2ResetStreamInfo(pDemuxer, i, pDemuxer->fileOffset)))
            return err;
        pStream[i].lastPTS = PARSER_UNKNOWN_TIME_STAMP;

        if (pStream[i].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM &&
            pDemuxer->playMode == FILE_MODE) {
            err = Mpeg2ParserCodecSpecificInfo(pDemuxer, i);
            if (err) {
                MPG2_PARSER_INTERNAL_LOG("Mpeg2 stream %d doesn't get codec data\n", i);
                err = PARSER_SUCCESS;
            }
        }
    }

#ifdef DEBUG_HEADER
    MPG2_PARSER_INTERNAL_LOG("%u streams found!\n", pDemuxer->SystemInfo.uliNoStreams);
    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        if (pDemuxer->SystemInfo.Stream[i].enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM) {
            printAudioInfo(&(pDemuxer->SystemInfo.Stream[i].MediaProperty.AudioProperty));

        } else if (pDemuxer->SystemInfo.Stream[i].enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
            printVideoInfo(&(pDemuxer->SystemInfo.Stream[i].MediaProperty.VideoProperty));
        }
    }
#endif

    pDemuxer->pDemuxContext->ProbeCnxt.Probed = 1;  // force probed to true
    ResyncCnxt(pDemuxer->pDemuxContext);
    {
        FSL_MPG_DEMUX_TS_BUFFER_T* pPESBuf;
        for (i = 0; i < MAX_MPEG2_STREAMS; i++) {
            pPESBuf = (FSL_MPG_DEMUX_TS_BUFFER_T*)&(
                    pDemuxer->pDemuxContext->TSCnxt.TempBufs.PESStreamBuf[i]);
            pPESBuf->Complete = 0;
            pPESBuf->Filled = 0;
            pPESBuf->PESLen = 0;
        }
    }
    return err;
}

/*
this function is to calcrate the file offset in seek function;

fileOffset = usTime*fileLength/duration
*/
uint64 Mpeg2CalcFileOffset(uint64 usTime, uint64 fileLength, uint64 duration) {
    return (((usTime >> 10) * fileLength / duration) << 10);
}

MPEG2_PARSER_ERROR_CODE Mpeg2ScanVideoFrame(MPEG2ObjectPtr pDemuxer, U32* flag, U8* pNewSeg,
                                            U32 newSegSize, FSL_MPEGSTREAM_T* pStreamInfo) {
    U32 fastFindFlag;
    // we will not save the last fourbytes in scan mode, so you can say

    pStreamInfo->lastFourBytes = 0xFFFFFFFF;

    fastFindFlag = MPEG2FastFindMPEG2Frames(pDemuxer, pNewSeg, newSegSize,
                                            pStreamInfo->lastFourBytes, pStreamInfo);
    *flag |= fastFindFlag;

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ParserMakeHistoryBuffer(MPEG2ObjectPtr pDemuxer,
                                                     FSL_MPEGSTREAM_T* pStreamInfo, uint32 size) {
    if (size < MAX_PES_PACKET_SIZE) {
        if (pStreamInfo->frameBuffer == NULL) {
            pStreamInfo->frameBuffer = LOCALMalloc(MAX_PES_PACKET_SIZE);
            if (pStreamInfo->frameBuffer == NULL) {
                pStreamInfo->frameBufferLength = 0;
                return PARSER_INSUFFICIENT_MEMORY;
            }
            pStreamInfo->frameBufferLength = MAX_PES_PACKET_SIZE;
        }
        return PARSER_SUCCESS;
    }

    else {
        if (size > pStreamInfo->frameBufferLength) {
            LOCALFree(pStreamInfo->frameBuffer);
            pStreamInfo->frameBuffer = LOCALMalloc(size);
            if (pStreamInfo->frameBuffer == NULL) {
                pStreamInfo->frameBufferLength = 0;
                return PARSER_INSUFFICIENT_MEMORY;
            }
            pStreamInfo->frameBufferLength = size;
            return PARSER_SUCCESS;
        } else
            return PARSER_ERR_UNKNOWN;
    }
}

MPEG2_PARSER_ERROR_CODE Mpeg2OutputMpeg2VideoFrame(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32 flag,
                                                   U8* pNewSeg, U32 newSegSize) {
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);
    MPEG2_PARSER_ERROR_CODE err;
    U32 frameCount;
    U32* frameOffsets = (U32*)(pStreamInfo->frameStartPos);
    U32* frameTypes = (U32*)(pStreamInfo->frameTypes);
    U32 i, stackFlag;
    uint64 usPresTime = pStreamInfo->currentPTS;
    uint32 consumeBytes, leftBytes;
    uint32 tComsumedBytes = 0;
    U8* pOrigSeg = pNewSeg;
    U8* pSplitLoc = NULL;
    S32 startFill = 0;
    U32 consumed = 0, dataSize = newSegSize;
    U8* pData = pNewSeg;

    err = PARSER_SUCCESS;

    switch (pStreamInfo->MediaProperty.VideoProperty.enuVideoType) {
        case FSL_MPG_DEMUX_MPEG2_VIDEO:
            if (pStreamInfo->EXVI.isParsed) {
                frameCount = 1;
                frameOffsets[0] = 0;
                if (pStreamInfo->EXVI.isKeyFrame)
                    frameTypes[0] = MPG2_IFRAME_TYPE;
                else
                    frameTypes[0] = MPG2_UNKNOWN_FRAMETYPE;
                pStreamInfo->isLastSampleSync = 0;
            } else if ((err = MPEG2FindMPEG2Frames(pDemuxer, pNewSeg, &newSegSize, pStreamInfo,
                                                   frameOffsets, &frameCount, frameTypes,
                                                   &pSplitLoc, &startFill)))
                return err;
            break;
        case FSL_MPG_DEMUX_HEVC_VIDEO:
        case FSL_MPG_DEMUX_H264_VIDEO:
            if (pStreamInfo->EXVI.isParsed) {
                frameCount = 1;
                frameOffsets[0] = 0;
                if (pStreamInfo->EXVI.isKeyFrame)
                    frameTypes[0] = MPG2_IFRAME_TYPE;
                else
                    frameTypes[0] = MPG2_UNKNOWN_FRAMETYPE;
                pStreamInfo->isLastSampleSync = 0;
            } else {
                int ret = PARSER_SUCCESS;
                ParseStreamFunc ParseStream;
                GetFrameBufferFunc GetFrameBuffer;
                if (FSL_MPG_DEMUX_HEVC_VIDEO ==
                    pStreamInfo->MediaProperty.VideoProperty.enuVideoType) {
                    ParseStream = (ParseStreamFunc)&ParseHevcStream;
                    GetFrameBuffer = (GetFrameBufferFunc)&GetHevcFrameBuffer;
                } else {
                    ParseStream = (ParseStreamFunc)&ParseH264Stream;
                    GetFrameBuffer = (GetFrameBufferFunc)&GetH264FrameBuffer;
                }

                FrameInfo frame;

                while (consumed < dataSize) {
                    memset(&frame, 0, sizeof(FrameInfo));
                    frame.flags = FLAG_SAMPLE_NOT_FINISHED;

                    ret = ParseStream(pStreamInfo->pParser, pData, dataSize, 0, &consumed);

                    if (pStreamInfo->isLastSampleSync) {
                        usPresTime = pStreamInfo->cachedPTS;
                        pStreamInfo->cachedPTS = PARSER_UNKNOWN_TIME_STAMP;
                    } else {
                        usPresTime = pStreamInfo->currentPTS;
                        pStreamInfo->cachedPTS = pStreamInfo->currentPTS;
                        pStreamInfo->currentPTS = PARSER_UNKNOWN_TIME_STAMP;
                    }
                    if (ret == H264PARSER_HAS_ONE_FRAME || ret == HevcPARSER_HAS_ONE_FRAME)
                        GetFrameBuffer(pStreamInfo->pParser, &frame);

                    if (frame.flags & FLAG_SAMPLE_NOT_FINISHED)
                        pStreamInfo->isLastSampleSync = 1;
                    else
                        pStreamInfo->isLastSampleSync = 0;

                    if (pStreamInfo->isFirstAfterSeek) {
                        if (frame.flags & FLAG_SYNC_SAMPLE) {
                            pStreamInfo->isFirstAfterSeek = 0;
                        } else if (!(FLAG_SAMPLE_NOT_FINISHED & frame.flags)) {
                            // drop non-key frame after seek, need to check seek function.
                            err = Mpeg2ResetOuputBuffer(pDemuxer, streamNum);
                            goto next;
                        }
                    }
                    if (FLAG_SAMPLE_H264_SEI_POS_DATA & frame.flags) {
                        leftBytes = frame.data_size;
                        usPresTime = PARSER_UNKNOWN_TIME_STAMP;
                        err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum, frame.buffer,
                                                        &leftBytes, frame.flags, usPresTime, 0, 0);
                    } else {
                        // force set last avc/hevc frame flag to SAMPLE_FINISHED
                        if (pDemuxer->fileOffset >= pDemuxer->fileSize && pDemuxer->fileSize != 0 &&
                            dataSize == consumed)
                            frame.flags &= (~FLAG_SAMPLE_NOT_FINISHED);
                        leftBytes = consumed;
                        err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum, pData, &leftBytes,
                                                        frame.flags, usPresTime, 0, 0);
                    }
                next:
                    dataSize -= consumed;
                    pData += consumed;
                    consumed = 0;
                }

                return err;

#ifndef MPG2_PARSER_SUPPORT_H264_FIELD_MERGE
                pStreamInfo->isLastSampleSync = 1;  // uncoment it ???
#endif
            }
#ifndef MPG2_PARSER_SUPPORT_H264_FIELD_MERGE
            // h264 do not use this logic
            pStreamInfo->isFirstAfterSeek = 0;
#endif
            break;
        case FSL_MPG_DEMUX_MP4_VIDEO:
            if (pStreamInfo->EXVI.isParsed) {
                frameCount = 1;
                frameOffsets[0] = 0;
                if (pStreamInfo->EXVI.isKeyFrame)
                    frameTypes[0] = MPG2_IFRAME_TYPE;
                else
                    frameTypes[0] = MPG2_UNKNOWN_FRAMETYPE;
                pStreamInfo->isLastSampleSync = 0;
            } else {
                err = MPEG2FindMPEG4Frames(pDemuxer, streamNum, pNewSeg, newSegSize);
                return err;
            }

            break;
        case FSL_MPG_DEMUX_AVS_VIDEO: {
            leftBytes = newSegSize;
            flag &= ~FLAG_SAMPLE_NOT_FINISHED;
            if (pStreamInfo->isFirstAfterSeek)
                pStreamInfo->isFirstAfterSeek = 0;
            err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum, pNewSeg, &leftBytes, flag,
                                            usPresTime, 0, 0);
            return err;
        }
        default:
            frameCount = 0;
            break;
    }

    if (pStreamInfo->isFirstAfterSeek) {
        if (frameCount == 0) {
            if (pStreamInfo->filedCounter == 0 && pStreamInfo->lastFiledType == MPG2_IFRAME_TYPE) {
                pStreamInfo->isFirstAfterSeek = 0;
                flag |= FLAG_SYNC_SAMPLE;
                goto STORE_FRAME;
            } else
                return PARSER_SUCCESS;
        }

        for (i = 0; i < frameCount; i++) {
            if (frameTypes[i] == MPG2_IFRAME_TYPE)
                break;
        }
        if (i < frameCount) {
            uint32 j, startOffset;
            pStreamInfo->isFirstAfterSeek = 0;
            pNewSeg += frameOffsets[i];
            newSegSize -= frameOffsets[i];
            frameCount -= i;
            startOffset = frameOffsets[i];

            for (j = 0; j < frameCount; j++) {
                frameTypes[j] = frameTypes[j + i];
                frameOffsets[j] = frameOffsets[j + i] - startOffset;
            }
        } else
            return PARSER_SUCCESS;
    }
    //}

STORE_FRAME:

    i = 0;
    stackFlag = flag;

    if (frameCount != 0)  // SynWord found in the new segment
    {
        if (pStreamInfo->isLastSampleSync) {
            flag &= ~FLAG_SAMPLE_NOT_FINISHED;
            // add code to restore the history PTS and flag here

            usPresTime = pStreamInfo->cachedPTS;
            pStreamInfo->cachedPTS = PARSER_UNKNOWN_TIME_STAMP;

            leftBytes = consumeBytes = frameOffsets[0];

            // here, should not check start_fill bytes
            err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum, pNewSeg, &leftBytes, flag,
                                            usPresTime, 0, 0);
            // The TimeStamp has been used.
            pStreamInfo->cachedPTS = PARSER_UNKNOWN_TIME_STAMP;

            if (!(flag & FSL_MPG_DEMUX_PTS_VALID))
                pStreamInfo->currentPTS = PARSER_UNKNOWN_TIME_STAMP;

            if (leftBytes > 0) {
                pNewSeg += (consumeBytes - leftBytes);
                tComsumedBytes += (consumeBytes - leftBytes);
                goto STOREHISTORY;
            } else {
                pNewSeg += consumeBytes;
                tComsumedBytes += consumeBytes;
            }
        }
        pStreamInfo->isLastSampleSync = 1;

        for (; i < frameCount; i++) {
            flag = stackFlag;
            stackFlag &= (~FLAG_SAMPLE_NEWSEG);

            if (frameTypes[i] == MPG2_IFRAME_TYPE)
                flag |= FLAG_SYNC_SAMPLE;
            else
                flag &= ~FLAG_SYNC_SAMPLE;

            usPresTime = pStreamInfo->currentPTS;
            pStreamInfo->cachedPTS = pStreamInfo->currentPTS;
            pStreamInfo->currentPTS = PARSER_UNKNOWN_TIME_STAMP;

            if (i == frameCount - 1) {
                if (!pStreamInfo->EXVI.isParsed)
                    flag |= FLAG_SAMPLE_NOT_FINISHED;
                else
                    flag &= ~FLAG_SAMPLE_NOT_FINISHED;
                leftBytes = consumeBytes = newSegSize - tComsumedBytes;
            } else {
                flag &= (~FLAG_SAMPLE_NOT_FINISHED);
                leftBytes = consumeBytes = frameOffsets[i + 1] - frameOffsets[i];
            }

            // here, should check start_fill bytes
            if (pNewSeg != pSplitLoc) {
                startFill = 0;
            }
            err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum, pNewSeg, &leftBytes, flag,
                                            usPresTime, startFill, pStreamInfo->lastFourBytes);
            // if one frame is finished, the cachePTS is cleared
            if ((flag & FLAG_SAMPLE_NOT_FINISHED) == 0)
                pStreamInfo->cachedPTS = PARSER_UNKNOWN_TIME_STAMP;

            if (leftBytes > 0) {
                pNewSeg += (consumeBytes - leftBytes);
                tComsumedBytes += (consumeBytes - leftBytes);
                i = i + 1;
                goto STOREHISTORY;
            } else {
                pNewSeg += consumeBytes;
                tComsumedBytes += consumeBytes;
            }
        }
    }

    else {
        if (pStreamInfo->isLastSampleSync) {
            flag |= FLAG_SAMPLE_NOT_FINISHED;
            leftBytes = consumeBytes = newSegSize;
            usPresTime = pStreamInfo->cachedPTS;

            // here, should not check start_fill bytes
            err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum, pNewSeg, &leftBytes, flag,
                                            usPresTime, 0, 0);

        } else {
            leftBytes = 0;
            consumeBytes = newSegSize;
        }

        if (leftBytes > 0) {
            pNewSeg += (consumeBytes - leftBytes);
            tComsumedBytes += (consumeBytes - leftBytes);
            goto STOREHISTORY;
        } else {
            pNewSeg += consumeBytes;
            tComsumedBytes += consumeBytes;
        }
    }

STOREHISTORY:
    if (tComsumedBytes == newSegSize) {
        pStreamInfo->frameBufferFilled = 0;
        pStreamInfo->sampleBytesLeft = pStreamInfo->lastSampleOffset = 0;
        pStreamInfo->frameStartCounts = 0;
        if (pStreamInfo->fileOffset == pDemuxer->fileOffset)
            pStreamInfo->isBlocked = 0;

        // last frame reach end of file, push an empty buffer with ~FLAG_SAMPLE_NOT_FINISHED
        // otherwise the last sample will be dismissed.
        if (pStreamInfo->isLastSampleSync && pDemuxer->fileOffset >= pDemuxer->fileSize) {
            pStreamInfo->isLastSampleSync = 0;
            usPresTime = pStreamInfo->cachedPTS;
            flag &= (~FLAG_SAMPLE_NOT_FINISHED);
            leftBytes = 0;
            err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum, pNewSeg, &leftBytes, flag,
                                            usPresTime, 0, 0);
        }
    } else {
        U32 j;
        MPEG2_PARSER_ERROR_CODE Err;
        if ((Err = Mpeg2ParserMakeHistoryBuffer(pDemuxer, pStreamInfo,
                                                newSegSize - tComsumedBytes)))
            return Err;

        if (pDemuxer->playMode == FILE_MODE) {
            if (!pStreamInfo->isBlocked) {
                pStreamInfo->isBlocked = 1;
                pStreamInfo->fileOffset = pDemuxer->fileOffset;
            }
        } else {
            MPG2_PARSER_INTERNAL_ERR("warning: block occur in stream mode \r\n");
        }

        // FIXME: here, it is almost impossible to need fill missing start codes, eg should not
        // check start_fill
        ASSERT(pNewSeg != pSplitLoc);
        memcpy(pStreamInfo->frameBuffer, pNewSeg, newSegSize - tComsumedBytes);
        pStreamInfo->lastSampleOffset = 0;
        pStreamInfo->sampleBytesLeft = leftBytes;
        pStreamInfo->frameBufferFilled = (newSegSize - tComsumedBytes);
        pStreamInfo->frameStartCounts = frameCount - i;
        pStreamInfo->frameBufferPTS = usPresTime;

        j = 0;
        for (; i < frameCount; i++) {
            frameOffsets[j] = frameOffsets[i] - tComsumedBytes;
            frameTypes[j] = frameTypes[i];
            j++;
        }
    }

    {
        U32 i;
        U32 fourBytes = 0;
        U8 byte;
        if (newSegSize > 4)
            for (i = newSegSize - 4; i < newSegSize; i++) {
                byte = pOrigSeg[i];
                fourBytes = (fourBytes << 8 | byte);
            }
        else
            for (i = 0; i < newSegSize; i++) {
                byte = pOrigSeg[i];
                fourBytes = (fourBytes << 8 | byte);
            }
        pStreamInfo->lastFourBytes = fourBytes;
    }

    // we process the PTS here
    if (!pStreamInfo->payloadInsert)
        pStreamInfo->frameBufferPTS = usPresTime;
    pStreamInfo->frameBufferFlag = 0;

    return err;
}

MPEG2_PARSER_ERROR_CODE Mpeg2OutputAacLatmData(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32 flag,
                                               U8* pNewSeg, U32 newSegSize) {
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);
    uint64 usPresTime;
    U32 allocSize = 0, consumed = 0;
    OutputBufArray* pOutputArray = &(pDemuxer->SystemInfo.Stream[streamNum].outputArray);
    ParserOutputBufferOps* pRequestBufferOps = pDemuxer->pRequestBufferOps;
    U8* pData = pNewSeg;
    U32 dataSize = newSegSize;
    FrameInfo sInFrame, sOutFrame;

    flag &= (~FLAG_SAMPLE_NOT_FINISHED);

    // send out codec data
    if (0 == pStreamInfo->codecSpecInfoSize) {
        AAC_LATM_AUDIO_INFO sLatmInfo;
        int ret = 0;
        U8* pOutput = NULL;
        void* bufContext = NULL;
        U32 tmpFlag = flag;

        memset(&sLatmInfo, 0, sizeof(AAC_LATM_AUDIO_INFO));
        sLatmInfo.pCodecData = pDemuxer->pDemuxContext->SeqHdrBuf.pSH;

        ret = ParseAacLatmAudioInfo(pNewSeg, newSegSize, &sLatmInfo);

        if (0 == ret) {
            pStreamInfo->codecSpecInfoSize = sLatmInfo.nCodecDataSize;
            allocSize = sLatmInfo.nCodecDataSize;
            pOutput = pRequestBufferOps->RequestBuffer(streamNum, &allocSize, &bufContext,
                                                       pDemuxer->appContext);
            if (!pOutput)
                return PARSER_INSUFFICIENT_MEMORY;
            else if (allocSize < (U32)sLatmInfo.nCodecDataSize) {
                pRequestBufferOps->ReleaseBuffer(streamNum, pOutput, bufContext,
                                                 pDemuxer->appContext);
                return PARSER_INSUFFICIENT_MEMORY;
            }
            memcpy(pOutput, sLatmInfo.pCodecData, sLatmInfo.nCodecDataSize);
            tmpFlag |= FLAG_SAMPLE_CODEC_DATA;
            err = InputOneUnitToArray(pDemuxer->memOps, pOutputArray, pOutput, allocSize,
                                      sLatmInfo.nCodecDataSize, PARSER_UNKNOWN_TIME_STAMP, tmpFlag,
                                      bufContext);
            if (err)
                return err;
        }
    }

    // send out aac latm raw data
    while (dataSize > 0) {
        if (0 == pStreamInfo->has_frame_buffer) {
            memset(&sInFrame, 0, sizeof(FrameInfo));
            sInFrame.alloc_size = newSegSize;
            sInFrame.buffer = pRequestBufferOps->RequestBuffer(streamNum, &sInFrame.alloc_size,
                                                               &(pStreamInfo->curr_bufContext),
                                                               pDemuxer->appContext);
            if (!sInFrame.buffer || sInFrame.alloc_size < newSegSize)
                return PARSER_INSUFFICIENT_MEMORY;
            sInFrame.data_size = 0;
            SetAacLatmBuffer(pStreamInfo->pParser, &sInFrame);
            pStreamInfo->has_frame_buffer = 1;
        }

        err = ParseAacLatmData(pStreamInfo->pParser, pData, dataSize, &consumed);

        if (LATMPARSER_HAS_OUTPUT == err) {
            usPresTime = pStreamInfo->currentPTS;
            pStreamInfo->cachedPTS = pStreamInfo->currentPTS;
            pStreamInfo->currentPTS = PARSER_UNKNOWN_TIME_STAMP;

            memset(&sOutFrame, 0, sizeof(FrameInfo));
            GetAacLatmBuffer(pStreamInfo->pParser, &sOutFrame);
            err = InputOneUnitToArray(pDemuxer->memOps, pOutputArray, sOutFrame.buffer,
                                      sOutFrame.alloc_size, sOutFrame.data_size, usPresTime, flag,
                                      pStreamInfo->curr_bufContext);
            pStreamInfo->has_frame_buffer = 0;
            if (err)
                return err;
        }

        pData += consumed;
        dataSize -= consumed;
    }
    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE Mpeg2OutputNomalData(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32 flag,
                                             U8* pNewSeg, U32 newSegSize) {
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);
    MPEG2_PARSER_ERROR_CODE err, err2;
    U32 leftSize = newSegSize;

    uint64 usPresTime = pStreamInfo->currentPTS;
    flag &= (~FLAG_SAMPLE_NOT_FINISHED);

    if (pStreamInfo->isAudioPresentationChanged) {
        flag |= FLAG_SAMPLE_AUDIO_PRESENTATION_CHANGED;
        pStreamInfo->isAudioPresentationChanged = FALSE;
    }

    if ((err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum, pNewSeg, &leftSize, flag, usPresTime,
                                         0, 0))) {
        if (leftSize > 0) {
            if (pDemuxer->playMode == FILE_MODE) {
                if (!pStreamInfo->isBlocked) {
                    pStreamInfo->isBlocked = 1;
                    pStreamInfo->fileOffset = pDemuxer->fileOffset;
                }
            } else {
                MPG2_PARSER_INTERNAL_ERR("warning: Blocked in stream mode !!! \r\n");
            }
            if ((err2 = Mpeg2ParserMakeHistoryBuffer(pDemuxer, pStreamInfo, leftSize)))
                return err2;

            memcpy(pStreamInfo->frameBuffer, pNewSeg + newSegSize - leftSize, leftSize);
            pStreamInfo->frameBufferFilled = leftSize;
            pStreamInfo->sampleBytesLeft = leftSize;
            pStreamInfo->frameStartCounts = 0;
            if ((flag & FSL_MPG_DEMUX_PTS_VALID)) {
                pStreamInfo->frameBufferFlag |= FSL_MPG_DEMUX_PTS_VALID;
                pStreamInfo->frameBufferPTS = pStreamInfo->currentPTS;
            }
        }
        return err;
    } else {
        if (pStreamInfo->fileOffset == pDemuxer->fileOffset)
            pStreamInfo->isBlocked = 0;
        return PARSER_SUCCESS;
    }
}

/*
Retrun 0 if success
Return 1 if fatal error
Return 2 if not this stream
Return 3 if discontinuity(request output buffer failed), did not clear the temp buffer
Return 4 if discontinuity and temp buffer cleared
*/

// pPESbuf contains a PES packet without the 4 bytes synword and the 2 bytes length
// pesBufLen is the PES packet PESHeaderLen

// #define DEBUG_RECORD

MPEG2_PARSER_ERROR_CODE MPEG2_ParsePES_Process(MPEG2ObjectPtr pDemuxer, U32 streamNum,
                                               U32 fourBytes, U8* pPESbuf, U32 pesBufLen, U32 isTS,
                                               U32 PID) {
    U8* pTemp;
    U32 Len = 0;
    U64 PTS = 0;
    U64 DTS = 0;
    U64 PTSDiff = 0;
    U32 Flag = 0;
    U32 Temp = 0;
    U32 PayloadSize = 0;
    U32 streamId, packetStreamNum;
    U32 isMPEG2Video = 0;
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);
    FSL_MPEGSTREAM_T* pPacketStreamInfo;
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;
    EPSON_EXVI exvi;
    exvi.isParsed = FALSE;

#ifdef DEBUG_RECORD
    FILE* fpRecord;
#endif

    pTemp = pPESbuf;
    streamId = PS_ID(fourBytes);

    /*parse the PES header  */
    while (Len < pesBufLen) {
        if (0xFF != (*pTemp)) {
            break;
        }
        pTemp++;
        Len++;
    };

    if (Len == pesBufLen)
        return PARSER_SUCCESS;

    if (0x01 == ((*pTemp) >> 6)) {
        if (Len + 2 < pesBufLen) {
            Len += 2;
            pTemp += 2;
        }
    }
    if (0x02 == ((*pTemp) >> 4)) {
        if (Len + 5 >= pesBufLen)
            return PARSER_SUCCESS;  // no payload, just return ok.

        PTS = ((*pTemp) >> 1) & 0x7;
        PTS = PTS << 30;
        pTemp++;
        Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) | (*(pTemp + 3));
        PTS = PTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
        Flag |= FSL_MPG_DEMUX_PTS_VALID;
        Len += 5;
        pTemp += 4;
    } else if (0x03 == ((*pTemp) >> 4)) {
        if (Len + 10 >= pesBufLen)
            return PARSER_SUCCESS;  // no payload, just return ok.

        PTS = ((*pTemp) >> 1) & 0x7;
        PTS = PTS << 30;
        pTemp++;
        Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) | (*(pTemp + 3));
        pTemp += 4;
        PTS = PTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);

        Temp = *pTemp;
        pTemp++;
        DTS = (Temp >> 1) & 0x7;
        DTS = DTS << 30;
        Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) | (*(pTemp + 3));
        DTS = DTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
        Flag |= (FSL_MPG_DEMUX_PTS_VALID | FSL_MPG_DEMUX_DTS_VALID);

        Len += 10;
        pTemp += 4;
    } else if (0x02 == ((*pTemp) >> 6)) {
        U32 HdrLen = 0;
        U32 PTS_DTS_Flag;

        if (Len + 3 >= pesBufLen)
            return PARSER_SUCCESS;  // no payload, just return ok.
        pTemp++;
        Temp = *pTemp;
        pTemp++;
        PTS_DTS_Flag = (Temp >> 6);
        HdrLen = *pTemp;
        if (Temp & 0x01) {
            // The extension flag
            exvi.isParsed = EPSON_ReadEXVI(pTemp, pesBufLen - Len - 2, &exvi);
        }
        pTemp++;
        Len += (3 + HdrLen);
        if (Len >= pesBufLen)
            return PARSER_SUCCESS;  // no payload, just return ok.

        // if (PTS_DTS_flags == '10')
        //     |  4   |       3       |     1       |       15      |     1       |      15      |
        //     1     | '0010'    PTS [32..30]    marker_bit    PTS [29..15]    marker_bit    PTS
        //     [14..0]    marker_bit
        // if (PTS_DTS_flags == '11')
        //     |  4   |       3       |     1       |       15      |     1       |      15      |
        //     1     | '0011'    PTS [32..30]    marker_bit    PTS [29..15]    marker_bit    PTS
        //     [14..0]    marker_bit |  4   |       3       |     1       |       15      |     1 |
        //     15      |     1     | '0001'    DTS [32..30]    marker_bit    DTS [29..15] marker_bit
        //     DTS [14..0]    marker_bit

        if (2 == PTS_DTS_Flag && HdrLen >= 5) {
            Temp = *pTemp;
            pTemp++;
            PTS = (Temp >> 1) & 0x7;
            PTS = PTS << 30;
            Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) |
                   (*(pTemp + 3));
            pTemp += 4;
            PTS = PTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
            Flag |= FSL_MPG_DEMUX_PTS_VALID;

            pTemp += (HdrLen - 5);
        } else if (3 == PTS_DTS_Flag && HdrLen >= 10) {
            Temp = *pTemp;
            pTemp++;
            PTS = (Temp >> 1) & 0x7;
            PTS = PTS << 30;
            Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) |
                   (*(pTemp + 3));
            pTemp += 4;
            PTS = PTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
            Flag |= FSL_MPG_DEMUX_PTS_VALID;

            Temp = *pTemp;
            pTemp++;
            DTS = (Temp >> 1) & 0x7;
            DTS = DTS << 30;
            Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) |
                   (*(pTemp + 3));
            pTemp += 4;
            DTS = DTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
            Flag |= FSL_MPG_DEMUX_DTS_VALID;

            pTemp += (HdrLen - 10);
        } else {
            pTemp += HdrLen;
        }
    } else {
        if (0x0F != *pTemp)
            return PARSER_SUCCESS;
        pTemp++;
        Len++;
    }
    /*the payload starts here   */
    if (Len >= pesBufLen)
        return PARSER_SUCCESS;  // fix zads1-10.mpg, no payload, just return ok.

    PayloadSize = pesBufLen - Len;

    if (PayloadSize > 0) {
        if (isTS & FLAG_SAMPLE_NEWSEG)
            Flag |= FLAG_SAMPLE_NEWSEG;
        if (IS_PRIV1(fourBytes)) {
            if (!isTS) {
                U32 subStreamID = *pTemp;
                streamId = ((subStreamID << 8) | streamId);

                if (Len + 4 >= pesBufLen)
                    return 1;
                pTemp += 4;
                Len += 4;
                PayloadSize -= 4;
                if (IS_PCM(subStreamID)) {
                    Len += 3;
                    PayloadSize -= 3;
                    pTemp += 3;
                    if (Len >= pesBufLen)
                        return PARSER_ERR_INVALID_MEDIA;
                }
            }
        }
    } else
        return PARSER_SUCCESS;

    if (!isTS)
        packetStreamNum = streamNumFromStreamId(pDemuxer, streamId, isTS);
    else
        packetStreamNum = streamNumFromPID(pDemuxer->pDemuxContext, PID);

    if (packetStreamNum >= pDemuxer->SystemInfo.uliNoStreams)
        return PARSER_SUCCESS;
    pPacketStreamInfo = &(pDemuxer->SystemInfo.Stream[packetStreamNum]);
    if (exvi.isParsed)
        memcpy(&(pPacketStreamInfo->EXVI), &exvi, sizeof(EPSON_EXVI));

    if (pPacketStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
        if ((pPacketStreamInfo->MediaProperty.VideoProperty.enuVideoType ==
             FSL_MPG_DEMUX_MPEG2_VIDEO) ||
            (pPacketStreamInfo->MediaProperty.VideoProperty.enuVideoType ==
             FSL_MPG_DEMUX_H264_VIDEO) ||
            (pPacketStreamInfo->MediaProperty.VideoProperty.enuVideoType ==
             FSL_MPG_DEMUX_MP4_VIDEO) ||
            (pPacketStreamInfo->MediaProperty.VideoProperty.enuVideoType ==
             FSL_MPG_DEMUX_AVS_VIDEO) ||
            (pPacketStreamInfo->MediaProperty.VideoProperty.enuVideoType ==
             FSL_MPG_DEMUX_HEVC_VIDEO))
            isMPEG2Video = 1;
    }

    if (!pPacketStreamInfo->isEnabled)
        return PARSER_SUCCESS;

    if (packetStreamNum != streamNum) {
        if (pPacketStreamInfo->isBlocked)
            return 0;
        if (pStreamInfo->isBlocked)
            return 0;
    }
    if (Flag & FSL_MPG_DEMUX_PTS_VALID) {
        MPG2_PARSER_INTERNAL_LOG("PTS: %lld , PTS*100/9: %lld, starttime: %lld \r\n", PTS,
                                 (PTS * 100 / 9), pPacketStreamInfo->startTime);
        PTS = MPG2_PTS_TO_TIMESTAMP(PTS);
        if (PTS >= pPacketStreamInfo->startTime)
            PTS -= pPacketStreamInfo->startTime;
        else if (FILE_MODE == pDemuxer->playMode)
            PTS = PARSER_UNKNOWN_TIME_STAMP;

        // fix ENGR00180871
        if (isMPEG2Video) {
            PTSDiff = (PTS > pPacketStreamInfo->lastPTS) ? (PTS - pPacketStreamInfo->lastPTS)
                                                         : (pPacketStreamInfo->lastPTS - PTS);
            if (PTSDiff > pPacketStreamInfo->maxPTSDelta && pPacketStreamInfo->maxPTSDelta > 0 &&
                FILE_MODE == pDemuxer->playMode &&
                (U64)PARSER_UNKNOWN_TIME_STAMP != pPacketStreamInfo->lastPTS) {
                PTS = -1;
            } else
                pPacketStreamInfo->lastPTS = PTS;
        } else {
            pPacketStreamInfo->lastPTS = PTS;
        }
        pPacketStreamInfo->currentPTS = PTS;
    } else {
        pPacketStreamInfo->currentPTS = pPacketStreamInfo->cachedPTS;
    }

    if (isMPEG2Video && (0 == pDemuxer->TS_PSI.IsTS)) {
        pDemuxer->lastsyncoffset = MPEG2FilePos(pDemuxer, packetStreamNum) - pesBufLen - 2 -
                                   4;  // pointer to pes header
        MPG2_PARSER_INTERNAL_LOG("record video lastsyncoffset: %lld(0x%llX) \r\n",
                                 pDemuxer->lastsyncoffset, pDemuxer->lastsyncoffset);
    }

    if (pPacketStreamInfo->payloadInsert) {
        if (pPacketStreamInfo->frameBuffer == NULL)
            if (PARSER_SUCCESS !=
                Mpeg2ParserMakeHistoryBuffer(pDemuxer, pPacketStreamInfo,
                                             PayloadSize + pPacketStreamInfo->tFrameBufferFilled))
                return PARSER_INSUFFICIENT_MEMORY;

        memcpy(pPacketStreamInfo->frameBuffer, pPacketStreamInfo->tFrameBuffer,
               pPacketStreamInfo->tFrameBufferFilled);
        memcpy(pPacketStreamInfo->frameBuffer + pPacketStreamInfo->tFrameBufferFilled, pTemp,
               PayloadSize);
        pTemp = pPacketStreamInfo->frameBuffer;
        PayloadSize = PayloadSize + pPacketStreamInfo->tFrameBufferFilled;
        pPacketStreamInfo->payloadInsert = 0;
        pPacketStreamInfo->tFrameBufferFilled = 0;
        pPacketStreamInfo->currentPTS = pPacketStreamInfo->frameBufferPTS;

        // some picture header is not followed by picture_coding_extension field in the same PES
        // picture_coding_extension may exist in the next Program Packet, so adjust sync offset
        pDemuxer->lastsyncoffset = pPacketStreamInfo->tFrameBufferSync;
    }

    bool isAudioAacLatm = (!isMPEG2Video) &&
                          (pPacketStreamInfo->MediaProperty.AudioProperty.enuAudioType ==
                           FSL_MPG_DEMUX_AAC_AUDIO) &&
                          (pPacketStreamInfo->MediaProperty.AudioProperty.enuAudioSubType ==
                           FSL_MPG_DEMUX_AAC_RAW);

    /* Only encrypted stream and audio stream(except aac latm) isNoFrameBoundary is TRUE */
    if (pPacketStreamInfo->isNoFrameBoundary) {
        // For HDCP encrypted steam, output the whole PES
        FSL_MPG_DEMUX_TS_BUFFER_T* pPESStreamBuf =
                &(pDemuxer->pDemuxContext->TSCnxt.TempBufs.PESStreamBuf[packetStreamNum]);
        if (NULL == pPESStreamBuf)
            return PARSER_SUCCESS;
        err = Mpeg2OutputNomalData(pDemuxer, packetStreamNum, Flag, pPESStreamBuf->pBuf,
                                   pPESStreamBuf->PESLen);
        pPacketStreamInfo->cachedPTS = PARSER_UNKNOWN_TIME_STAMP;
    } else if (isMPEG2Video) {
        if ((U64)-1 == pPacketStreamInfo->lastFramePosition)
            pPacketStreamInfo->lastFramePosition = pDemuxer->lastsyncoffset;
        err = Mpeg2OutputMpeg2VideoFrame(pDemuxer, packetStreamNum, Flag, pTemp, PayloadSize);
#ifdef DEBUG_RECORD
        fpRecord = fopen("StreamRecord.bin", "ab+");
        fwrite(pTemp, 1, PayloadSize, fpRecord);
        fclose(fpRecord);
#endif
    } else {
        if (isAudioAacLatm)
            err = Mpeg2OutputAacLatmData(pDemuxer, packetStreamNum, Flag, pTemp, PayloadSize);
        else
            err = Mpeg2OutputNomalData(pDemuxer, packetStreamNum, Flag, pTemp, PayloadSize);
        pPacketStreamInfo->cachedPTS = PARSER_UNKNOWN_TIME_STAMP;
    }

    if (pDemuxer->playMode == FILE_MODE) {
        if (pDemuxer->outputMode == OUTPUT_BYTRACK)
            if ((err == PARSER_ERR_NO_OUTPUT_BUFFER) && (packetStreamNum != streamNum))
                err = PARSER_SUCCESS;
    }

    return err;
}

U32 MPEG2_ParsePES_Scan(MPEG2ObjectPtr pDemuxer, FSL_MPEGSTREAM_T* pStreamInfo, U8* pPESbuf,
                        U32 pesBufLen, U32 isTS, U64* pPTS, U32* flag, U32 streamNum) {
    U8* pTemp;
    U32 Len = 0;
    U64 PTS = 0;
    U64 DTS = 0;
    U32 Flag = 0;
    U32 Temp = 0;
    U32 PayloadSize = 0;
    U32 streamId;
    U32 isMPEG2Video = 0;

    pTemp = pPESbuf;

    if (pStreamInfo->enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM) {
        streamId = pStreamInfo->streamId;
    } else if (pStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
        streamId = pStreamInfo->streamId;
        if (SUPPORT_SEEK(pStreamInfo->MediaProperty.VideoProperty.enuVideoType))
            isMPEG2Video = 1;
    } else
        return 2;

    /*parse the PES header  */
    while (Len < pesBufLen) {
        if (0xFF != (*pTemp)) {
            break;
        }
        pTemp++;
        Len++;
    };

    if (Len == pesBufLen)
        return 1;

    if (0x01 == ((*pTemp) >> 6)) {
        if (Len + 2 < pesBufLen) {
            Len += 2;
            pTemp += 2;
        } else
            return 1;
    }
    if (0x02 == ((*pTemp) >> 4)) {
        if (Len + 5 >= pesBufLen)
            return 1;

        PTS = ((*pTemp) >> 1) & 0x7;
        PTS = PTS << 30;
        pTemp++;
        Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) | (*(pTemp + 3));
        PTS = PTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
        Flag |= FSL_MPG_DEMUX_PTS_VALID;
        Len += 5;
        pTemp += 4;
    } else if (0x03 == ((*pTemp) >> 4)) {
        if (Len + 10 >= pesBufLen)
            return 1;

        PTS = ((*pTemp) >> 1) & 0x7;
        PTS = PTS << 30;
        pTemp++;
        Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) | (*(pTemp + 3));
        pTemp += 4;
        PTS = PTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);

        Temp = *pTemp;
        pTemp++;
        DTS = (Temp >> 1) & 0x7;
        DTS = DTS << 30;
        Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) | (*(pTemp + 3));
        DTS = DTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
        Flag |= (FSL_MPG_DEMUX_PTS_VALID | FSL_MPG_DEMUX_DTS_VALID);

        Len += 10;
        pTemp += 4;
    } else if (0x02 == ((*pTemp) >> 6)) {
        U32 HdrLen = 0;
        U32 PTS_DTS_Flag;

        if (Len + 3 >= pesBufLen)
            return 1;
        pTemp++;
        Temp = *pTemp;
        pTemp++;
        PTS_DTS_Flag = (Temp >> 6);
        HdrLen = *pTemp;
        pTemp++;
        Len += (3 + HdrLen);
        if (Len >= pesBufLen)
            return 1;

        if (2 == PTS_DTS_Flag && HdrLen >= 5) {
            Temp = *pTemp;
            pTemp++;
            PTS = (Temp >> 1) & 0x7;
            PTS = PTS << 30;
            Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) |
                   (*(pTemp + 3));
            pTemp += 4;
            PTS = PTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
            Flag |= FSL_MPG_DEMUX_PTS_VALID;

            pTemp += (HdrLen - 5);
        } else if (3 == PTS_DTS_Flag && HdrLen >= 10) {
            Temp = *pTemp;
            pTemp++;
            PTS = (Temp >> 1) & 0x7;
            PTS = PTS << 30;
            Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) |
                   (*(pTemp + 3));
            pTemp += 4;
            PTS = PTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
            Flag |= FSL_MPG_DEMUX_PTS_VALID;

            Temp = *pTemp;
            pTemp++;
            DTS = (Temp >> 1) & 0x7;
            DTS = DTS << 30;
            Temp = ((*pTemp) << 24) | ((*(pTemp + 1)) << 16) | ((*(pTemp + 2)) << 8) |
                   (*(pTemp + 3));
            pTemp += 4;
            DTS = DTS | ((Temp >> 2) & 0x3FFF8000) | ((Temp >> 1) & 0x7FFF);
            Flag |= FSL_MPG_DEMUX_DTS_VALID;

            pTemp += (HdrLen - 10);
        } else {
            pTemp += HdrLen;
        }
    } else {
        if (0x0F != *pTemp++)
            return 1;
    }
    /*the payload starts here   */
    if (Len >= pesBufLen)
        return 1;

    PayloadSize = pesBufLen - Len;
    if (PayloadSize > 0) {
        if (IS_PRIV1(streamId)) {
            U32 subStreamID = *pTemp;
            if (!isTS) {
                if (subStreamID != (streamId >> 8))
                    return 2;
            }

            if (Len + 4 >= pesBufLen)
                return 1;
            pTemp += 4;
            Len += 4;
            PayloadSize -= 4;
            if (IS_PCM(subStreamID)) {
                Len += 3;
                PayloadSize -= 3;
                pTemp += 3;
                if (Len >= pesBufLen)
                    return 1;
            }
        }

        if (Flag & FSL_MPG_DEMUX_PTS_VALID) {
            *pPTS = MPG2_PTS_TO_TIMESTAMP(PTS);
            MPG2_PARSER_INTERNAL_LOG("PTS: %lld , PTS*100/9: %lld, starttime: %lld \r\n", PTS,
                                     *pPTS, pStreamInfo->startTime);
            if (*pPTS >= pStreamInfo->startTime)
                pStreamInfo->lastPTS = pStreamInfo->currentPTS = *pPTS - pStreamInfo->startTime;
        } else
            pStreamInfo->currentPTS = PARSER_UNKNOWN_TIME_STAMP;

        if (isMPEG2Video) {
            if (!(pStreamInfo->scanForDuration)) {
                if (0 == pDemuxer->TS_PSI.IsTS) {
                    pDemuxer->lastsyncoffset = MPEG2FilePos(pDemuxer, streamNum) - pesBufLen - 2 -
                                               4;  // pointer to pes header
                    MPG2_PARSER_INTERNAL_LOG(
                            "record scan video(%d) lastsyncoffset: %lld(0x%llX) \r\n", streamNum,
                            pDemuxer->lastsyncoffset, pDemuxer->lastsyncoffset);
                }
                Mpeg2ScanVideoFrame(pDemuxer, flag, pTemp, PayloadSize, pStreamInfo);
            }
        }
    }

    *flag |= Flag;
    return 0;
}

MPEG2_PARSER_ERROR_CODE FoundPESSycnWord(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32* pFourBytes) {
    MPEG2_PARSER_ERROR_CODE Err;
    U32 Byte;
    U32 FourBytes = *pFourBytes;
    U32 SCFound = 0;

    do {
        if ((Err = MPEG2ParserNextNBytes(pDemuxer, streamNum, 1, &Byte)))
            return Err;

        FourBytes = (FourBytes << 8) | Byte;
        if (IS_SC(FourBytes)) {
            if (IS_PES(FourBytes)) {
                SCFound = 1;
            }
            /*End of stream   */
            if (PS_EOS == PS_ID(FourBytes)) {
                return PARSER_EOS;
            }
        }
    } while (SCFound == 0);

    *pFourBytes = FourBytes;

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE FoundPESSycnWordInBuffer(U8* pBuffer, U32 bufferSize, U32* pOffset,
                                                 U32* pFourBytes) {
    U32 Byte;
    U32 FourBytes = *pFourBytes;
    U32 SCFound = 0;
    U32 Offset = *pOffset;

    do {
        Byte = pBuffer[Offset++];
        FourBytes = (FourBytes << 8) | Byte;
        if (IS_SC(FourBytes)) {
            if (IS_PES(FourBytes)) {
                SCFound = 1;
            }
            /*End of stream   */
            if (PS_EOS == PS_ID(FourBytes)) {
                return PARSER_EOS;
            }
        }
    } while (SCFound == 0 && Offset < bufferSize);
    if (!SCFound)
        return PARSER_ERR_INVALID_MEDIA;

    *pFourBytes = FourBytes;
    *pOffset = Offset;
    return PARSER_SUCCESS;
}

// Use this function to Copy the bitstream to output buffer
MPEG2_PARSER_ERROR_CODE Mpeg2Parser_FillOutputBuf(MPEG2ObjectPtr pDemuxer, uint32 streamNum,
                                                  uint8* pStreamData, uint32* pDataSize,
                                                  uint32 flag, uint64 usPresTime, int32 start_fill,
                                                  uint32 four_bytes) {
    FSL_MPEGSTREAM_T* pStream = &(pDemuxer->SystemInfo.Stream[streamNum]);
    OutputBufArray* pOutputArray = &(pStream->outputArray);
    ParserOutputBufferOps* pRequestBufferOps = pDemuxer->pRequestBufferOps;
    uint32 leftDataSize = *pDataSize;
    MPEG2_PARSER_ERROR_CODE err = PARSER_SUCCESS;
    int32 i;
    int32 additional = 0;
    bool sampleSync = (flag | pStream->lastFrameFlag) & FLAG_SYNC_SAMPLE;

    additional = (start_fill == 3) ? 1 : 0;

    if ((flag & FLAG_SAMPLE_NOT_FINISHED) == 0) {
        if (0 == leftDataSize) {
            /* merge last 0-byte frame to previous unfinished frame */
            uint8* pBuffer = NULL;
            void* bufContext = NULL;

            if (pOutputArray->validCount > 0) {
                OutputBufLink* pLink = pOutputArray->pValidTail;
                if (pLink->flag & FLAG_SAMPLE_NOT_FINISHED) {
                    pLink->flag &= (~FLAG_SAMPLE_NOT_FINISHED);
                }
                if (flag & FLAG_SYNC_SAMPLE) {
                    pLink->flag |= FLAG_SYNC_SAMPLE;
                }
            } else {
                err = InputOneUnitToArray(pDemuxer->memOps, pOutputArray, pBuffer, 0, 0, usPresTime,
                                          flag, bufContext);
                if (err)
                    return err;
            }
        }
        if (sampleSync && usPresTime != (U64)PARSER_UNKNOWN_TIME_STAMP &&
            FSL_MPG_DEMUX_VIDEO_STREAM == pStream->enuStreamType) {
            Mpeg2ParserUpdateIndex(pDemuxer, streamNum, pStream->lastFramePosition, usPresTime);
        }
        pStream->lastFrameFlag = 0;
        pStream->lastFramePosition = pDemuxer->lastsyncoffset;
    } else if (FSL_MPG_DEMUX_VIDEO_STREAM == pStream->enuStreamType) {
        pStream->lastFrameFlag |= flag;
    }

    *pDataSize = leftDataSize;

    if (leftDataSize > 0) {
        if (pOutputArray->validCount > 0) {
            OutputBufLink* pLink = pOutputArray->pValidTail;

            if ((pLink->flag & FLAG_SAMPLE_NOT_FINISHED) && (pLink->filledLen < pLink->bufLen)) {
                uint32 apSize = (pLink->bufLen - pLink->filledLen - start_fill - additional);
                if (apSize > leftDataSize)
                    apSize = leftDataSize;

                // may need to fill 1 to 3 bytes for missing start code
                if (additional == 1)  // 00 00 01 + 0xB1 + 00 00 01 + 00
                {
                    // we need to inserted one additional byte to avoid conflict
                    uint8* p = pLink->pBuffer + pLink->filledLen;
                    p[0] = 0xB1;  // reserved start code
                    MPG2_PARSER_INTERNAL_LOG("start fill 1 bytes: 0x%X \r\n", p[0]);
                    pLink->filledLen += 1;
                }
                if ((start_fill >= 1) && (start_fill <= 3)) {
                    uint8* p = pLink->pBuffer + pLink->filledLen;
                    MPG2_PARSER_INTERNAL_LOG("start fill %d bytes: ", start_fill);
                    for (i = 0; i < start_fill; i++) {
                        p[i] = (four_bytes >> (start_fill - 1 - i)) & 0xFF;
                        MPG2_PARSER_INTERNAL_LOG("0x%X  ", p[i]);
                    }
                    MPG2_PARSER_INTERNAL_LOG("\r\n");
                    pLink->filledLen += start_fill;
                }

                memcpy((pLink->pBuffer + pLink->filledLen), pStreamData, apSize);

                leftDataSize -= apSize;
                pStreamData += apSize;
                pLink->filledLen += apSize;
                if ((flag & FLAG_SAMPLE_NOT_FINISHED) == 0 && leftDataSize == 0)
                    pLink->flag &= (~FLAG_SAMPLE_NOT_FINISHED);

                if (flag & FSL_MPG_DEMUX_PTS_VALID) {
                    pLink->flag |= FSL_MPG_DEMUX_PTS_VALID;
                    pLink->usPresTime = usPresTime;
                }
            }
        }
    }

    while (leftDataSize > 0) {
        uint32 requestBufSize = 0;
        uint8* pBuffer;
        void* bufContext;
        uint32 bufGetSize;
        uint32 filledSize;
        uint32 inflag = flag;

        requestBufSize = leftDataSize + start_fill + additional;

        bufGetSize = requestBufSize;

        if (bufGetSize == 0) {
            pBuffer = NULL;
            bufContext = NULL;
        } else if (NULL == (pBuffer = pRequestBufferOps->RequestBuffer(
                                    streamNum, &bufGetSize, &bufContext, pDemuxer->appContext))) {
            err = PARSER_ERR_NO_OUTPUT_BUFFER;
            break;
        }

        filledSize = bufGetSize - start_fill - additional;
        if (filledSize >= leftDataSize)
            filledSize = leftDataSize;

        inflag = (flag | FLAG_SAMPLE_NOT_FINISHED);

        if ((err = InputOneUnitToArray(pDemuxer->memOps, pOutputArray, pBuffer, bufGetSize, 0,
                                       usPresTime, inflag, bufContext)))
            return err;

        if (filledSize > 0) {
            // may need to fill 1 to 3 bytes for missing start code
            if (additional == 1)  // 00 00 01 + 0xB1 + 00 00 01 + 00
            {
                // we need to inserted one additional byte to avoid conflict
                pBuffer[0] = 0xB1;  // reserved start code
                MPG2_PARSER_INTERNAL_LOG("start fill 1 bytes: 0x%X \r\n", pBuffer[0]);
            }
            if ((start_fill >= 1) && (start_fill <= 3)) {
                MPG2_PARSER_INTERNAL_LOG("will start fill %d bytes: ", start_fill);
                for (i = 0; i < start_fill; i++) {
                    pBuffer[i + additional] = (four_bytes >> (start_fill - 1 - i)) & 0xFF;
                    MPG2_PARSER_INTERNAL_LOG("0x%X  ", pBuffer[i + additional]);
                }
                MPG2_PARSER_INTERNAL_LOG("\r\n");
            }
            memcpy(pBuffer + start_fill + additional, pStreamData, filledSize);
            pOutputArray->pValidTail->filledLen = filledSize + start_fill + additional;
        }

        if ((flag & FLAG_SAMPLE_NOT_FINISHED) == 0 && (filledSize == leftDataSize))
            pOutputArray->pValidTail->flag = flag;

        pStreamData += filledSize;
        leftDataSize -= filledSize;
        if (leftDataSize == 0)
            break;
    }
    *pDataSize = leftDataSize;

    return err;
}

MPEG2_PARSER_ERROR_CODE Mpeg2Parser_Request_History_OutBuffer(MPEG2ObjectPtr pDemuxer,
                                                              uint32 streamNum) {

    FSL_MPEGSTREAM_T* pStreamInfo = &pDemuxer->SystemInfo.Stream[streamNum];
    MPEG2_PARSER_ERROR_CODE err;
    uint32 flag, i;
    uint64 usPresTime = 0;
    uint32 isMpeg2Video;
    uint32 sampleBytesLeft = pStreamInfo->sampleBytesLeft;

    isMpeg2Video = 0;
    if ((FSL_MPG_DEMUX_VIDEO_STREAM == pStreamInfo->enuStreamType) &&
        (pStreamInfo->MediaProperty.VideoProperty.enuVideoType == FSL_MPG_DEMUX_MPEG2_VIDEO ||
         pStreamInfo->MediaProperty.VideoProperty.enuVideoType == FSL_MPG_DEMUX_H264_VIDEO ||
         pStreamInfo->MediaProperty.VideoProperty.enuVideoType == FSL_MPG_DEMUX_MP4_VIDEO))
        isMpeg2Video = 1;

    if (pStreamInfo->EXVI.isParsed)
        pStreamInfo->frameStartCounts = 0;

OUTPUT_FRAMEBUFFER:
    if (sampleBytesLeft > 0) {
        // must require output buffer for the data contained in framebuffer
        flag = 0;
        if (pStreamInfo->frameBufferFlag != 0) {
            flag = pStreamInfo->frameBufferFlag;
            usPresTime = pStreamInfo->frameBufferPTS;
        }

        if (isMpeg2Video) {
            if (pStreamInfo->frameStartCounts == 0) {
                if (!pStreamInfo->EXVI.isParsed)
                    flag |= FLAG_SAMPLE_NOT_FINISHED;
                else
                    flag &= ~FLAG_SAMPLE_NOT_FINISHED;
            } else {
                flag &= ~FLAG_SAMPLE_NOT_FINISHED;
            }
        } else
            flag &= ~FLAG_SAMPLE_NOT_FINISHED;

        err = Mpeg2Parser_FillOutputBuf(pDemuxer, streamNum,
                                        (pStreamInfo->frameBuffer + pStreamInfo->lastSampleOffset),
                                        &(pStreamInfo->sampleBytesLeft), flag, usPresTime, 0, 0);

        if (pStreamInfo->sampleBytesLeft > 0) {
            sampleBytesLeft -= pStreamInfo->sampleBytesLeft;
            pStreamInfo->lastSampleOffset += sampleBytesLeft;
            pStreamInfo->frameBufferFlag |= FLAG_SAMPLE_NOT_FINISHED;
            if (pDemuxer->playMode == FILE_MODE) {
                if (!pStreamInfo->isBlocked) {
                    pStreamInfo->isBlocked = 1;
                    pStreamInfo->fileOffset = pDemuxer->fileOffset;
                }
            } else {
                MPG2_PARSER_INTERNAL_ERR("warning: Blocked in stream mode !\r\n");
            }
        } else {
            pStreamInfo->sampleBytesLeft = 0;
            // clear the data if no other frames
            if (pStreamInfo->frameStartCounts == 0) {
                pStreamInfo->frameBufferFilled = 0;
                pStreamInfo->lastSampleOffset = 0;
            }

            pStreamInfo->frameBufferFlag = 0;
            if (pStreamInfo->fileOffset == pDemuxer->fileOffset)
                pStreamInfo->isBlocked = 0;
        }

        if (err != PARSER_SUCCESS)
            return err;
    }

    if (pStreamInfo->frameStartCounts > 0) {
        flag = 0;
        if (pStreamInfo->frameTypes[0] == MPG2_IFRAME_TYPE) {
            flag |= FLAG_SYNC_SAMPLE;
            if (pStreamInfo->frameBufferFlag & FSL_MPG_DEMUX_PTS_VALID) {
                flag |= FSL_MPG_DEMUX_PTS_VALID;
            }
        }
        if (pStreamInfo->frameStartCounts == 1) {
            pStreamInfo->sampleBytesLeft =
                    pStreamInfo->frameBufferFilled - pStreamInfo->frameStartPos[0];
        } else {
            pStreamInfo->sampleBytesLeft =
                    pStreamInfo->frameStartPos[1] - pStreamInfo->frameStartPos[0];
        }

        sampleBytesLeft = pStreamInfo->sampleBytesLeft;
        pStreamInfo->frameBufferFlag = flag;
        pStreamInfo->lastSampleOffset = pStreamInfo->frameStartPos[0];
        pStreamInfo->frameStartCounts--;
        for (i = 0; i < pStreamInfo->frameStartCounts; i++) {
            pStreamInfo->frameStartPos[i] = pStreamInfo->frameStartPos[i + 1];
            pStreamInfo->frameTypes[i] = pStreamInfo->frameTypes[i + 1];
        }
        goto OUTPUT_FRAMEBUFFER;
    }

    return PARSER_SUCCESS;
}

MPEG2_PARSER_ERROR_CODE Mpeg2Paser_OuputSample_FromArray(OutputBufArray* pOutputArray,
                                                         uint8** sampleData, void** pAppContext,
                                                         uint32* dataSize, uint64* usPresTime,
                                                         uint32* flag, uint32 dump_id) {
#ifdef MPG2_PARSER_DUMP
    static FILE* fpSampRaw[MAX_DUMP_ID] = {NULL};
    static FILE* fpSampPTS[MAX_DUMP_ID] = {NULL};
#endif
    if (pOutputArray->validCount == 0)
        return MPEG2_ERR_NO_MORE_ARRAY_PACKETS;
    else {
        OutputBufLink* pHead;
        pHead = pOutputArray->pHead;
        if (pHead == NULL)
            return MPEG2_ERR_NO_MORE_ARRAY_PACKETS;

        if (pOutputArray->validCount > 1 || (pHead->filledLen == pHead->bufLen) ||
            ((pHead->flag & FLAG_SAMPLE_NOT_FINISHED) == 0)) {
            // Ouput head from the array
            OutputOneUnitFromArray(pOutputArray, sampleData, dataSize, usPresTime, flag,
                                   pAppContext);
#ifdef MPG2_PARSER_DUMP
            if (dump_id < MAX_DUMP_ID) {
                mpeg2_parser_DumpSampleRaw(&fpSampRaw[dump_id], *sampleData, *dataSize, dump_id);
                mpeg2_parser_DumpSamplePTS(&fpSampPTS[dump_id], *dataSize, *usPresTime, *flag,
                                           dump_id);
            }
#else
            (void)dump_id;
#endif
            return PARSER_SUCCESS;
        }
        return MPEG2_ERR_NO_MORE_ARRAY_PACKETS;
    }
}

U32 streamNumFromStreamId(MPEG2ObjectPtr pDemuxer, U32 streamId, U32 isTS) {
    U32 i;

    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        if (!isTS) {
            if (pDemuxer->SystemInfo.Stream[i].streamId == streamId)
                return i;
        } else {
            if (PS_ID(pDemuxer->SystemInfo.Stream[i].streamId) == PS_ID(streamId))
                return i;
        }
    }
    return -1;
}

#if 0
U32 isStreamEnabled(MPEG2ObjectPtr pDemuxer, uint32 streamNum)
{

    if (streamNum>=pDemuxer->SystemInfo.uliNoStreams)
        return 0;
    else
        return pDemuxer->SystemInfo.Stream[streamNum].isEnabled;

}
#endif

MPEG2_PARSER_ERROR_CODE Mpeg2ReturnOutputSample(MPEG2ObjectPtr pDemuxer, uint32* streamNum,
                                                uint8** sampleData, void** pAppContext,
                                                uint32* dataSize, uint64* usPresTime,
                                                uint32* flag) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt = NULL;
    FSL_MPEGSTREAM_T* pStreamInfo = NULL;
    OutputBufArray* pOutputBufArray = NULL;
    U32 i;

    pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;
    if (pCnxt->TSCnxt.bGetPCR) {
        *sampleData = *pAppContext = NULL;
        *usPresTime = PARSER_UNKNOWN_TIME_STAMP;
        *dataSize = 0;
        *flag |= FLAG_SAMPLE_PCR_INFO;
        pCnxt->TSCnxt.bGetPCR = FALSE;
        return PARSER_SUCCESS;
    }

    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
        pStreamInfo = &(pDemuxer->SystemInfo.Stream[i]);
        pOutputBufArray = &(pStreamInfo->outputArray);
        if (pStreamInfo->isFirstAfterSeek &&
            pStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM)
            continue;  // h264 & hevc stream need to wait for a complete key frame after seek;
        else if (Mpeg2Paser_OuputSample_FromArray(pOutputBufArray, sampleData, pAppContext,
                                                  dataSize, usPresTime, flag,
                                                  i) == PARSER_SUCCESS) {
            *streamNum = i;
            return PARSER_SUCCESS;
        }
    }

    return PARSER_NOT_READY;
}

// use this function to skip un-nessary	packets
MPEG2_PARSER_ERROR_CODE Mpeg2SkipPackets(MPEG2ObjectPtr pDemuxer, uint32 streamNum) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt;
    U8* pInput = NULL;
    U32 FourBytes = 0xFFFFFFFF;
    U32 streamId;
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    U32 timeCodeBytes = 0;
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);

    streamId = pStreamInfo->streamId;

    pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;
    if (pCnxt->TSCnxt.hasTimeCode)
        timeCodeBytes = 4;

    /*TS process  */
    if (pDemuxer->TS_PSI.IsTS) {
        U32 Offset = 0;
        U32 timecodeOffset = 0;
        bool hasTimeCode;
        if ((Err = MPEG2ParserReadBuffer(pDemuxer, streamNum, &pInput, MIN_TS_STREAM_LEN)))
            return Err;
        hasTimeCode = pCnxt->TSCnxt.hasTimeCode;
        if (TSSync(pInput, MIN_TS_STREAM_LEN, &Offset, &hasTimeCode, 0) != 0) {
            Err = PARSER_ERR_INVALID_MEDIA;
            return Err;
        } else if (Offset < MIN_TS_STREAM_LEN) {
            if (pCnxt->TSCnxt.hasTimeCode)
                timecodeOffset = 4;
            if ((Err = MPEG2ParserRewindNBytes(pDemuxer, streamNum,
                                               MIN_TS_STREAM_LEN - Offset + timecodeOffset)))
                return Err;
        }

        {
            U32 PID;
            U32 streamPID = pDemuxer->pDemuxContext->TSCnxt.Streams.ReorderedStreamPID[streamNum];
            /*
            FSL_MPG_DEMUX_TS_BUFFER_T* pTempBuf;
            if(pStreamInfo->enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM)
            pTempBuf= &(pCnxt->TSCnxt.TempBufs.PESAudioBuf);
            else
            pTempBuf= &(pCnxt->TSCnxt.TempBufs.PESVideoBuf);
            */

        TS_SKIP:
            if (pStreamInfo->fileOffset >= pDemuxer->fileOffset) {
                pStreamInfo->fileOffset = pDemuxer->fileOffset;
                pStreamInfo->isBlocked = 0;
                return Err;
            }

            if ((Err = MPEG2ParserReadBuffer(pDemuxer, streamNum, &pInput,
                                             TS_PACKET_LENGTH + timeCodeBytes)))
                return Err;

            PID = (*(pInput + 2 + timeCodeBytes)) | (((*(pInput + 1 + timeCodeBytes)) & 0x1F) << 8);
            if (PID != streamPID) {
                goto TS_SKIP;
            } else {
                Err = MPEG2ParserRewindNBytes(pDemuxer, streamNum,
                                              TS_PACKET_LENGTH + timeCodeBytes);
                return Err;
            }
        }
    }

    U32 PESHeaderLen = 0;
    /* The first thing to do is check the output array of the stream to see if samples available */
SKIP_PACKET:
    /*search the first PES*/
    /*First in the current buffer, search start code */
    FourBytes = 0xFFFFFFFF;

    if (pStreamInfo->fileOffset >= pDemuxer->fileOffset) {
        pStreamInfo->fileOffset = pDemuxer->fileOffset;
        pStreamInfo->isBlocked = 0;
        return Err;
    }

    if ((Err = FoundPESSycnWord(pDemuxer, streamNum, &FourBytes)))
        return Err;
    /*Now start code found   */
    if ((Err = MPEG2ParserNextNBytes(pDemuxer, streamNum, 2, &PESHeaderLen)))
        return Err;

    if (PS_ID(streamId) != PS_ID(FourBytes)) {
        if ((Err = MPEG2ParserForwardNBytes(pDemuxer, streamNum, PESHeaderLen)))
            return Err;
        goto SKIP_PACKET;
    } else {
        Err = MPEG2ParserRewindNBytes(pDemuxer, streamNum, 6);
        return Err;
    }

    // shouldn't update pDemuxer->lastsyncoffset, right ??

}

MPEG2_PARSER_ERROR_CODE Mpeg2ParserProcess(MPEG2ObjectPtr pDemuxer, uint32 streamNum,
                                           uint8** sampleData, void** pAppContext, uint32* dataSize,
                                           uint64* usPresTime, uint32* flag) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt;
    U8* pInput = NULL;
    U32 FourBytes = 0xFFFFFFFF;
    U32 streamId;
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);
    OutputBufArray* pOutputBufArray = &(pStreamInfo->outputArray);

    *flag = 0;    // contain flags for return

    if (Mpeg2Paser_OuputSample_FromArray(pOutputBufArray, sampleData, pAppContext, dataSize,
                                         usPresTime, flag, streamNum) == PARSER_SUCCESS)
        return PARSER_SUCCESS;

    streamId = pStreamInfo->streamId;

    if (pDemuxer->playMode == FILE_MODE) {
        if ((Err = Mpeg2Parser_Request_History_OutBuffer(pDemuxer, streamNum)))
            return Err;
    } else {
        U32 i;
        for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
            if (pDemuxer->SystemInfo.Stream[i].isEnabled)
                if ((Err = Mpeg2Parser_Request_History_OutBuffer(pDemuxer, i)))
                    return Err;
        }
    }

    pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;

    /*To ensure that probe must be called
    Otherwise there is no stream information */
    if (0 == pCnxt->ProbeCnxt.Probed) {
        return PARSER_ERR_INVALID_MEDIA;
    }

    /*TS process  */
    if (pDemuxer->TS_PSI.IsTS) {
        U32 Offset = 0;
        U32 timeCodeOffset = 0;
        bool hasTimeCode;
        if ((Err = MPEG2ParserReadBuffer(pDemuxer, streamNum, &pInput, MIN_TS_STREAM_LEN)))
            return Err;
        hasTimeCode = pCnxt->TSCnxt.hasTimeCode;
        if (TSSync(pInput, MIN_TS_STREAM_LEN, &Offset, &hasTimeCode, 0) != 0) {
            Err = PARSER_ERR_INVALID_MEDIA;
            return Err;
        } else if (Offset < MIN_TS_STREAM_LEN) {
            if (pCnxt->TSCnxt.hasTimeCode)
                timeCodeOffset = 4;
            if ((Err = MPEG2ParserRewindNBytes(pDemuxer, streamNum,
                                               MIN_TS_STREAM_LEN - Offset + timeCodeOffset)))
                return Err;
        }

        {
            U32 i, Ret = 0;

        TS_PROCESS:

            if (!(pStreamInfo->isFirstAfterSeek &&
                  (pStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM))) {
                // h264 & hevc stream need to wait for a complete key frame after seek;
                if (Mpeg2Paser_OuputSample_FromArray(pOutputBufArray, sampleData, pAppContext,
                                                     dataSize, usPresTime, flag,
                                                     streamNum) == PARSER_SUCCESS)
                    return PARSER_SUCCESS;
            }
            if ((Err = MPEG2ParserReadBuffer(pDemuxer, streamNum, &pInput,
                                             TS_PACKET_LENGTH + timeCodeOffset)))
                return Err;

            if (pInput[0 + timeCodeOffset] != TS_SYNC_BYTE) {
                U32 Offset = 0;
                if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0, TS_PACKET_LENGTH + timeCodeOffset)))
                    return Err;

                if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, MIN_TS_STREAM_LEN)))
                    return Err;
                hasTimeCode = pCnxt->TSCnxt.hasTimeCode;
                if (TSSync(pInput, MIN_TS_STREAM_LEN, &Offset, &hasTimeCode, 0) != 0) {
                    Err = PARSER_ERR_INVALID_MEDIA;
                    return Err;
                } else if (Offset < MIN_TS_STREAM_LEN) {
                    if (pCnxt->TSCnxt.hasTimeCode)
                        timeCodeOffset = 4;
                    if ((Err = MPEG2ParserRewindNBytes(
                                 pDemuxer, 0, MIN_TS_STREAM_LEN - Offset + timeCodeOffset)))
                        return Err;
                }
            }

            Ret = ParseTSStreamPacket(pDemuxer, pCnxt, pInput, streamId);
            if (pInput[0 + timeCodeOffset] != TS_SYNC_BYTE) {
                Err = PARSER_ERR_INVALID_MEDIA;
                return Err;
            }
            if (0 != Ret && Ret != 16) {
                Err = PARSER_ERR_UNKNOWN;
                goto TS_PROCESS;
            }
            if (16 == Ret) {
                if ((Err = MPEG2ParserRewindNBytes(pDemuxer, streamNum,
                                                   TS_PACKET_LENGTH + timeCodeOffset)))
                    return Err;
            } /*Parse PES and get system info   */

            for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
                if (1 == (pCnxt->TSCnxt.TempBufs.PESStreamBuf[i].Complete)) {
                    FSL_MPG_DEMUX_TS_BUFFER_T* pTempBuf = &(pCnxt->TSCnxt.TempBufs.PESStreamBuf[i]);
                    U32 Offset = 0;
                    U32 FourBytes = NextNBufferBytes(pTempBuf->pBuf, 4, &Offset);
                    // it's accurate offset
                    pDemuxer->lastsyncoffset = pTempBuf->qwOffset;
                    Err = MPEG2_ParsePES_Process(pDemuxer, streamNum, FourBytes, pTempBuf->pBuf + 6,
                                                 pTempBuf->PESLen - 6, (1 | pTempBuf->newSegFlag),
                                                 pTempBuf->PID);
                    pTempBuf->newSegFlag = 0;
                    pTempBuf->Complete = 0;
                    pTempBuf->Filled = 0;
                    pTempBuf->PESLen = 0;
                    if (Err != PARSER_SUCCESS)
                        return Err;
                }
            }
            goto TS_PROCESS;
        }
    }

PROCESS_AV: {
    U32 PESHeaderLen = 0;
    /* The first thing to do is check the output array of the stream to see if samples available */
    if (Mpeg2Paser_OuputSample_FromArray(pOutputBufArray, sampleData, pAppContext, dataSize,
                                         usPresTime, flag, streamNum) == PARSER_SUCCESS)
        return PARSER_SUCCESS;

// PROCESS_PACKET:
    /*search the first PES*/
    /*First in the current buffer, search start code */
    FourBytes = 0xFFFFFFFF;
    if ((Err = FoundPESSycnWord(pDemuxer, streamNum, &FourBytes)))
        return Err;

    /*Now start code found   */
    if ((Err = MPEG2ParserNextNBytes(pDemuxer, streamNum, 2, &PESHeaderLen)))
        return Err;

    if ((Err = MPEG2ParserReadBuffer(pDemuxer, streamNum, &pInput, PESHeaderLen)))
        return Err;

    if ((Err = MPEG2_ParsePES_Process(pDemuxer, streamNum, FourBytes, pInput, PESHeaderLen, 0, 0)))
        return Err;

    // to catch the fileOffset
    if (pStreamInfo->isBlocked)
        Mpeg2SkipPackets(pDemuxer, streamNum);

    goto PROCESS_AV;
}
}

MPEG2_PARSER_ERROR_CODE Mpeg2ParserProcessFile(MPEG2ObjectPtr pDemuxer, uint32* streamNum,
                                               uint8** sampleData, void** pAppContext,
                                               uint32* dataSize, uint64* usPresTime, uint32* flag) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt;
    U8* pInput = NULL;
    U32 FourBytes = 0xFFFFFFFF;
    U32 streamId = 0;
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    FSL_MPEGSTREAM_T* pStreamInfo = NULL;
    U32 str = 0;
    bool hasTimeCode;
    U32 errCount = 0;
    U32 timecodeOffset = 0;
    *flag = 0;  // contain flags for return

    for (str = 0; str < pDemuxer->SystemInfo.uliNoStreams; str++) {
        if (pDemuxer->SystemInfo.Stream[str].isEnabled) {
            if ((Err = Mpeg2Parser_Request_History_OutBuffer(pDemuxer, str)))
                return Err;
            pDemuxer->SystemInfo.Stream[str].isBlocked = FALSE;
        }
    }

    pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;
    /*To ensure that probe must be called
    Otherwise there is no stream information */
    if (0 == pCnxt->ProbeCnxt.Probed) {
        return PARSER_ERR_INVALID_MEDIA;
    }

    if (pDemuxer->TS_PSI.IsTS) {
        U32 Offset = 0;

        if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, MIN_TS_STREAM_LEN)))
            goto BAIL;
        hasTimeCode = pCnxt->TSCnxt.hasTimeCode;
        if (TSSync(pInput, MIN_TS_STREAM_LEN, &Offset, &hasTimeCode, 0) != 0) {
            errCount++;
        } else if (Offset < MIN_TS_STREAM_LEN) {
            if (pCnxt->TSCnxt.hasTimeCode)
                timecodeOffset = 4;
            if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0,
                                               MIN_TS_STREAM_LEN - Offset + timecodeOffset)))
                return Err;
        }

        {
            U32 i, Ret = 0;

        TS_FILE_PROCESS:

            Err = Mpeg2ReturnOutputSample(pDemuxer, streamNum, sampleData, pAppContext, dataSize,
                                          usPresTime, flag);
            if (Err == PARSER_SUCCESS)
                return Err;

            if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput,
                                             TS_PACKET_LENGTH + timecodeOffset)))
                goto BAIL;

            if (pInput[0 + timecodeOffset] != TS_SYNC_BYTE) {
                U32 Offset = 0;

                if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0, TS_PACKET_LENGTH + timecodeOffset)))
                    return Err;

                if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, MIN_TS_STREAM_LEN)))
                    goto BAIL;
                hasTimeCode = pCnxt->TSCnxt.hasTimeCode;
                if (TSSync(pInput, MIN_TS_STREAM_LEN, &Offset, &hasTimeCode, 0) != 0) {
                    if (errCount < MPEG2_MAX_ERR_TIMES) {
                        errCount++;
                        goto TS_FILE_PROCESS;
                    } else {
                        Err = PARSER_ERR_INVALID_MEDIA;
                        return Err;
                    }
                } else if (Offset < MIN_TS_STREAM_LEN) {
                    if (pCnxt->TSCnxt.hasTimeCode)
                        timecodeOffset = 4;
                    if ((Err = MPEG2ParserRewindNBytes(
                                 pDemuxer, 0, MIN_TS_STREAM_LEN - Offset + timecodeOffset)))
                        return Err;
                }
            }

            Ret = ParseTSStreamPacket(pDemuxer, pCnxt, pInput, streamId);
            if (0 != Ret && Ret != 16) {
                Err = PARSER_ERR_UNKNOWN;
                goto TS_FILE_PROCESS;
            }
            if (16 == Ret) {
                if ((Err = MPEG2ParserRewindNBytes(pDemuxer, *streamNum,
                                                   TS_PACKET_LENGTH + timecodeOffset)))
                    return Err;
            } /*Parse PES and get system info   */

        PROCESS_LAST_PES:
            for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
                if (1 == (pCnxt->TSCnxt.TempBufs.PESStreamBuf[i].Complete)) {
                    FSL_MPG_DEMUX_TS_BUFFER_T* pTempBuf = &(pCnxt->TSCnxt.TempBufs.PESStreamBuf[i]);
                    U32 Offset = 0;
                    U32 FourBytes = NextNBufferBytes(pTempBuf->pBuf, 4, &Offset);

                    // it's accurate offset
                    pDemuxer->lastsyncoffset = pTempBuf->qwOffset;

                    Err = MPEG2_ParsePES_Process(pDemuxer, 0, FourBytes, pTempBuf->pBuf + 6,
                                                 pTempBuf->PESLen - 6, (1 | pTempBuf->newSegFlag),
                                                 pTempBuf->PID);
                    pTempBuf->newSegFlag = 0;
                    pTempBuf->Complete = 0;
                    pTempBuf->Filled = 0;
                    pTempBuf->PESLen = 0;
                    if (Err != PARSER_SUCCESS) {
                        return Err;
                    }
                }
            }
            goto TS_FILE_PROCESS;
        }
    }

PROCESS_FILE_AV: {
    U32 PESHeaderLen = 0;
    /* The first thing to do is check the output array of the stream to see if samples available */
    if (PARSER_SUCCESS == Mpeg2ReturnOutputSample(pDemuxer, streamNum, sampleData, pAppContext,
                                                  dataSize, usPresTime, flag))
        return PARSER_SUCCESS;

    /*search the first PES*/
    /*First in the current buffer, search start code */
    FourBytes = 0xFFFFFFFF;
    if ((Err = FoundPESSycnWord(pDemuxer, 0, &FourBytes))) {
        /* push an empty buffer with flag=0 to finish last unfinished sample buffer */
        if (Err == PARSER_EOS) {
            U32 i;
            for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
                pStreamInfo = &(pDemuxer->SystemInfo.Stream[i]);
                if (pStreamInfo->isLastSampleSync &&
                    pStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
                    uint32 leftBytes = 0;
                    Mpeg2Parser_FillOutputBuf(pDemuxer, i, NULL, &leftBytes, 0, -1, 0, 0);
                    pStreamInfo->isLastSampleSync = 0;
                    goto BAIL;
                }
            }
        }
        return Err;
    }

    /*Now start code found   */
    if ((Err = MPEG2ParserNextNBytes(pDemuxer, 0, 2, &PESHeaderLen)))
        return Err;

    if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, PESHeaderLen)))
        return Err;

    if ((Err = MPEG2_ParsePES_Process(pDemuxer, 0, FourBytes, pInput, PESHeaderLen, 0, 0)))
        return Err;

    // to catch the fileOffset
    goto PROCESS_FILE_AV;
}

BAIL:
    if (Err == PARSER_EOS) {
        U32 i;
        U32 processLastPes = 0;
        for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
            FSL_MPG_DEMUX_TS_BUFFER_T* pTempBuf = &(pCnxt->TSCnxt.TempBufs.PESStreamBuf[i]);
            // if last pes len is 0(invalid), this pes isn't processed yet, send it out as well.
            if (pTempBuf->PESLen == 6 && pTempBuf->Filled > 6 && pTempBuf->Complete == 0) {
                pTempBuf->Complete = 1;
                pTempBuf->PESLen = pTempBuf->Filled;
                processLastPes = 1;
            }
        }
        if (processLastPes)
            goto PROCESS_LAST_PES;
        // try to output from array if there are samples.
        if (PARSER_SUCCESS == Mpeg2ReturnOutputSample(pDemuxer, streamNum, sampleData, pAppContext,
                                                      dataSize, usPresTime, flag))
            return PARSER_SUCCESS;
    }
    return Err;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ParserScan(MPEG2ObjectPtr pDemuxer, uint32 streamNum,
                                        uint64* fileOffset, uint64* fileOffsetSeeked, uint64* PTS,
                                        uint32* flag, int32 strict) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt;
    U8* pInput = NULL;
    U32 FourBytes = 0xFFFFFFFF;
    U64 bytesDeep = *fileOffset;
    U64 syncOffset = *fileOffset;
    U32 streamId = 0, nResyncCnt = 0;
    bool hasTimeCode;
    bool pesStart = TRUE;

    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);

    if ((Err = MPEG2FileSeek(pDemuxer, streamNum, bytesDeep, SEEK_SET)))
        goto bailscan;

    if (pStreamInfo->enuStreamType == FSL_MPG_DEMUX_AUDIO_STREAM ||
        pStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) {
        streamId = pStreamInfo->streamId;
    }

    /*get the input buffer context*/
    pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;

    /*To ensure that probe must be called
    Otherwise there is no stream information */
    if (0 == pCnxt->ProbeCnxt.Probed) {
        return PARSER_ERR_INVALID_MEDIA;
    }

    /*TS process  */
    if (pDemuxer->TS_PSI.IsTS) {
        U32 i, Offset = 0;
        U32 timecodeOffset = 0;
        FSL_MPG_DEMUX_TS_BUFFER_T* pPESBuf = NULL;

        for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
            pPESBuf = (FSL_MPG_DEMUX_TS_BUFFER_T*)&(
                    pDemuxer->pDemuxContext->TSCnxt.TempBufs.PESStreamBuf[i]);
            if (pPESBuf->StreamNum == streamNum && pPESBuf->PID != 0) {
                pPESBuf->Complete = 0;
                pPESBuf->Filled = 0;
                pPESBuf->PESLen = 0;
                break;
            }
        }

    RESYNC:
        if ((Err = MPEG2ParserReadBuffer(pDemuxer, streamNum, &pInput, MIN_TS_STREAM_LEN)))
            goto bailscan;
        hasTimeCode = pCnxt->TSCnxt.hasTimeCode;
        if (TSSync(pInput, MIN_TS_STREAM_LEN, &Offset, &hasTimeCode, strict) != 0) {
            MPG2_PARSER_INTERNAL_LOG("TSSync fail.\n");
            nResyncCnt++;
            if (nResyncCnt >= SEEK_RESYNC_CNT) {
                Err = PARSER_ERR_INVALID_MEDIA;
                goto bailscan;
            }
            goto RESYNC;
        } else if (Offset < MIN_TS_STREAM_LEN) {
            if (pCnxt->TSCnxt.hasTimeCode)
                timecodeOffset = 4;
            if ((Err = MPEG2ParserRewindNBytes(pDemuxer, streamNum,
                                               MIN_TS_STREAM_LEN - Offset + timecodeOffset)))
                goto bailscan;
        }

        {
            U32 Ret = 0;
            U32 TempOffset;
            U32 streamPID = pDemuxer->pDemuxContext->TSCnxt.Streams.ReorderedStreamPID[streamNum];

        TS_SCAN:
            if (0 == pPESBuf->Filled) {
                syncOffset = MPEG2FilePos(pDemuxer, streamNum);
            }

            if ((Err = MPEG2ParserReadBuffer(pDemuxer, streamNum, &pInput,
                                             TS_PACKET_LENGTH + timecodeOffset)))
                goto bailscan;

            pesStart = (0 == pPESBuf->Filled);
            Ret = ScanTSStreamPacket(pDemuxer, pInput, streamPID, streamId, pesStart);

            // fix ENGR00174120
            if (Ret == 1)  // sync byte err
            {
                if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0, TS_PACKET_LENGTH + timecodeOffset)))
                    return Err;
                goto RESYNC;
            }
            if (0 != Ret && Ret != 16) {
                Err = PARSER_ERR_UNKNOWN;
                goto TS_SCAN;
            }
            if (16 == Ret) {
                if ((Err = MPEG2ParserRewindNBytes(pDemuxer, streamNum,
                                                   TS_PACKET_LENGTH + timecodeOffset)))
                    goto bailscan;
            }
            /*Parse PES and get system info   */

            if ((pPESBuf->Filled > MIN_SCAN_STREAM_LEN || pPESBuf->Complete) &&
                pPESBuf->StreamNum == streamNum) {
                FourBytes = 0xFFFFFFFF;
                TempOffset = 0;
                if ((Err = FoundPESSycnWordInBuffer(pPESBuf->pBuf, pPESBuf->PESLen, &TempOffset,
                                                    &FourBytes)))
                    goto TS_SCAN;
                if (TempOffset != 4)
                    goto TS_SCAN;
                if ((Err = MPEG2_ParsePES_Scan(pDemuxer, pStreamInfo, pPESBuf->pBuf + 6,
                                               pPESBuf->PESLen - 6, 1, PTS, flag, streamNum)))
                    goto TS_SCAN;

                pPESBuf->Complete = 0;
                pPESBuf->Filled = 0;
                pPESBuf->PESLen = 0;
                goto scanok;
            }
            goto TS_SCAN;
        }
    }

SCAN_AV: {
    U32 PESHeaderLen = 0, scanLen = 0;
    int64 offset = 0;
    /*Get context  */

    FourBytes = 0xFFFFFFFF;

    /*search the first PES*/
    /*First in the current buffer, search start code */
    if ((Err = FoundPESSycnWord(pDemuxer, streamNum, &FourBytes)))
        goto bailscan;
    syncOffset = MPEG2FilePos(pDemuxer, streamNum) - 4;

    /*Now start code found   */
    if ((Err = MPEG2ParserNextNBytes(pDemuxer, streamNum, 2, &PESHeaderLen)))
        goto bailscan;

    if (PS_ID(streamId) != PS_ID(FourBytes)) {
        if ((Err = MPEG2ParserForwardNBytes(pDemuxer, streamNum, PESHeaderLen)))
            goto bailscan;
        goto SCAN_AV;
    }

    if (PESHeaderLen > MIN_SCAN_STREAM_LEN) {
        scanLen = MIN_SCAN_STREAM_LEN;
        offset = (int64)(PESHeaderLen - scanLen);
    } else {
        scanLen = PESHeaderLen;
    }

    if ((Err = MPEG2ParserReadBuffer(pDemuxer, streamNum, &pInput, scanLen)))
        goto bailscan;

    if (MPEG2_ParsePES_Scan(pDemuxer, pStreamInfo, pInput, scanLen, 0, PTS, flag, streamNum))
        goto SCAN_AV;

    if (offset != 0) {
        Err = MPEG2FileSeek(pDemuxer, streamNum, offset, pDemuxer->fileOffset);
        if (Err)
            goto bailscan;
        offset = 0;
    }
    goto scanok;
}

bailscan:
    if (Err == PARSER_EOS)
        goto scanok;
    return Err;
scanok:
    *fileOffset = syncOffset;
    *fileOffsetSeeked = MPEG2FilePos(pDemuxer, streamNum);
    return Err;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ParserScanStreamDuration(MPEG2ObjectPtr pDemuxer, uint32 streamNum) {
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    U32 flag = 0;
    U64 fileLength;
    U64 startFileOffset1, endFileOffset1, startFileOffset2, endFileOffset2;
    U64 basicStep = 1024 * 128;
    U32 endTimeFounded = 0;
    U32 reachTop = 0;
    U64 starttime, endtime;

    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);

    pStreamInfo->startTime = 0;
    pStreamInfo->endTime = 0;
    pStreamInfo->usDuration = 0;
    pStreamInfo->scanForDuration = 1;

    fileLength = pDemuxer->fileSize;
    if (fileLength == 0)
        return PARSER_ERR_INVALID_MEDIA;

    if (streamNum >= pDemuxer->SystemInfo.uliNoStreams)
        return PARSER_ERR_INVALID_PARAMETER;

    if ((Err = MPEG2FileSeek(pDemuxer, streamNum, 0, SEEK_SET)))
        return Err;

    startFileOffset1 = 0;
    pStreamInfo->startTime = (U64)0xFFFFFFFFFFFFFFFFULL;
    do {
        flag = 0;
        if ((Err = Mpeg2ParserScan(pDemuxer, streamNum, &startFileOffset1, &endFileOffset1,
                                   &starttime, &flag, 0)))
            return Err;
        startFileOffset1 = endFileOffset1;
        if (startFileOffset1 > SCAN_DURATION_SIZE) {
            Err = MPEG2_ERR_NO_DURATION;
            goto SCAN_DURATION_OUT;
        }
    } while ((flag & FSL_MPG_DEMUX_PTS_VALID) == 0);
    pStreamInfo->startTime = starttime;

    while (fileLength < basicStep) basicStep = (basicStep >> 1);
    startFileOffset2 = fileLength - basicStep;
    startFileOffset1 = startFileOffset2;

    if (startFileOffset2 < endFileOffset1) {
        startFileOffset2 = endFileOffset1;
        reachTop = 1;
    }

    while (startFileOffset2 >= endFileOffset1) {
        endFileOffset2 = startFileOffset2;
        do {
            flag = 0;
            startFileOffset2 = endFileOffset2;
            Err = Mpeg2ParserScan(pDemuxer, streamNum, &startFileOffset2, &endFileOffset2, &endtime,
                                  &flag, 0);
            if (Err != PARSER_SUCCESS)
                break;
        } while ((flag & FSL_MPG_DEMUX_PTS_VALID) == 0);

        if (Err == PARSER_EOS) {
            if ((!endTimeFounded) && (flag & FSL_MPG_DEMUX_PTS_VALID)) {
                endTimeFounded = 1;
                Err = PARSER_SUCCESS;
                break;
            } else if (endTimeFounded)
                break;
            else if (reachTop) {
                endtime = starttime;
                Err = PARSER_SUCCESS;
                break;
            } else {
                basicStep = (basicStep << 1);
                if (startFileOffset1 < basicStep) {
                    basicStep = startFileOffset1;
                }
                startFileOffset2 = startFileOffset1 - basicStep;
                if (fileLength - startFileOffset2 > SCAN_DURATION_SIZE) {
                    Err = MPEG2_ERR_NO_DURATION;
                    goto SCAN_DURATION_OUT;
                }
                if (startFileOffset2 < endFileOffset1) {
                    startFileOffset2 = endFileOffset1;
                    reachTop = 1;
                }
                startFileOffset1 = startFileOffset2;
            }
        } else if (Err != PARSER_SUCCESS)
            goto SCAN_DURATION_OUT;
        else {
            if (flag & FSL_MPG_DEMUX_PTS_VALID) {
                endTimeFounded = 1;
            }
            startFileOffset2 = endFileOffset2;
        }
    }

    if (endTimeFounded && (endtime < starttime)) {
        endtime = starttime;
    }

    if (endTimeFounded)
        pStreamInfo->endTime = endtime;
    else {
        Err = MPEG2_ERR_NO_DURATION;
        goto SCAN_DURATION_OUT;
    }

    if (endtime < starttime) {
        pStreamInfo->startTime = pStreamInfo->endTime = 0;
        Err = MPEG2_ERR_NO_DURATION;
        goto SCAN_DURATION_OUT;
    }
    pStreamInfo->usDuration = pStreamInfo->endTime - pStreamInfo->startTime;

    // add protection for currupted PTS.
    if (pStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM &&
        pStreamInfo->MediaProperty.VideoProperty.uliVideoBitRate &&
        fileLength * 8 < pStreamInfo->usDuration / 1000000000 *
                                 pStreamInfo->MediaProperty.VideoProperty.uliVideoBitRate)
        pStreamInfo->usDuration = 0;

    Err = PARSER_SUCCESS;
SCAN_DURATION_OUT:
    pStreamInfo->scanForDuration = 0;
    return Err;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ResetOuputBuffer(MPEG2ObjectPtr pDemuxer, U32 streamNum) {
    OutputBufArray* pOutputBufArray = &(pDemuxer->SystemInfo.Stream[streamNum].outputArray);
    ParserOutputBufferOps* pRequestBufferOps = pDemuxer->pRequestBufferOps;
    return ReleaseArrayUnits(pOutputBufArray, streamNum, pRequestBufferOps, pDemuxer->appContext);
}

MPEG2_PARSER_ERROR_CODE Mpeg2FlushStreamInternal(MPEG2ObjectPtr pDemuxer, U32 streamNum) {
    FSL_MPEGSTREAM_T* pStreamInfo = &(pDemuxer->SystemInfo.Stream[streamNum]);
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;

    pDemuxer->PTSDeta = 0;
    pStreamInfo->lastFourBytes = 0xFFFFFFFF;
    pStreamInfo->isLastSampleSync = 0;
    pStreamInfo->frameBufferFilled = 0;
    pStreamInfo->lastSampleOffset = 0;
    pStreamInfo->sampleBytesLeft = 0;
    pStreamInfo->payloadInsert = 0;
    pStreamInfo->lastSyncPosition = -1;
    pStreamInfo->lastFramePosition = -1;
    pStreamInfo->lastFrameFlag = 0;
    pStreamInfo->frameStartCounts = 0;
    pStreamInfo->frameBufferPTS = PARSER_UNKNOWN_TIME_STAMP;  // PTS of the current FrameBuffer
    pStreamInfo->currentPTS = PARSER_UNKNOWN_TIME_STAMP;      // PTS of the current PES
    pStreamInfo->frameBufferFlag = 0;                         // is PTS of the frameBufferValid

    pStreamInfo->cachedPTS = PARSER_UNKNOWN_TIME_STAMP;

    pStreamInfo->filedCounter = 0;
    pStreamInfo->lastFiledType = 0;
    pStreamInfo->isBlocked = 0;
    pStreamInfo->isFirstAfterSeek = 1;
    pStreamInfo->isSyncFinished = 1;
    pStreamInfo->isSPSfindAfterSeek = 0;
    pStreamInfo->isAudioPresentationChanged = FALSE;

    if ((pStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM) &&
        (pStreamInfo->MediaProperty.VideoProperty.enuVideoType == FSL_MPG_DEMUX_H264_VIDEO ||
         pStreamInfo->MediaProperty.VideoProperty.enuVideoType == FSL_MPG_DEMUX_HEVC_VIDEO)) {
        if (FSL_MPG_DEMUX_HEVC_VIDEO == pStreamInfo->MediaProperty.VideoProperty.enuVideoType) {
            Err = ResetHevcParser(pStreamInfo->pParser);
        } else {
            Err = ResetH264Parser(pStreamInfo->pParser);
        }

        if (Err != PARSER_SUCCESS)
            return Err;
    }
    // add more reset opreration if needed
    // clear the Video and audio Buffer

    {
        if (pDemuxer->TS_PSI.IsTS) {
            FSL_MPG_DEMUX_TS_BUFFER_T* pTempBuf;
            U32 i;
            for (i = 0; i < pDemuxer->pDemuxContext->TSCnxt.Streams.SupportedStreams; i++) {
                pTempBuf = (FSL_MPG_DEMUX_TS_BUFFER_T*)&(
                        pDemuxer->pDemuxContext->TSCnxt.TempBufs.PESStreamBuf[i]);
                if (pTempBuf->StreamNum == streamNum) {
                    pTempBuf->Complete = 0;
                    pTempBuf->Filled = 0;
                    pTempBuf->PESLen = 0;
                    pTempBuf->lastPESOffset = 0;
                }
            }
        }
    }

    return Err;
}

MPEG2_PARSER_ERROR_CODE Mpeg2ResetStreamInfo(MPEG2ObjectPtr pDemuxer, U32 streamNum,
                                             U64 fileOffset) {

    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;

    pDemuxer->PTSDeta = 0;

    if ((Err = MPEG2FileSeek(pDemuxer, streamNum, fileOffset, SEEK_SET)))
        return Err;

    Err = Mpeg2FlushStreamInternal(pDemuxer, streamNum);

    return Err;
}

bool intersectIndexRange(INDEX_RANGE_T* a, INDEX_RANGE_T* b) {
    if (a->left <= b->left && a->right >= b->right)
        return TRUE;

    if (b->left <= a->left && b->right >= a->right) {
        a->left = b->left;
        a->right = b->right;
        return TRUE;
    }

    if (a->right >= b->left) {
        a->right = b->right;
        return TRUE;
    }

    if (a->right + 1 < b->left || b->right + 1 < a->left)
        return FALSE;

    if (a->right >= b->left) {
        a->right = b->right;
        return TRUE;
    }

    if (b->right >= a->left) {
        a->left = b->left;
        return TRUE;
    }

    // should not come here
    return FALSE;
}

bool isIndexRangeContinuous(INDEX_RANGE_T* index, U64 left, U64 right) {
    if (left >= right)
        return FALSE;  // invalid range

    while (index) {
        if (index->left <= left && index->right >= right)
            return TRUE;
        index = index->next;
    }
    return FALSE;
}

void mergeIndexRange(INDEX_RANGE_T** index, U64 left, U64 right) {
    INDEX_RANGE_T* curr = *index;
    INDEX_RANGE_T* insert = NULL;
    INDEX_RANGE_T target;
    bool merged;

    target.left = left;
    target.right = right;

    if (curr == NULL || target.right + 1 < curr->left) {
        INDEX_RANGE_T* temp = (INDEX_RANGE_T*)malloc(sizeof(INDEX_RANGE_T));
        if (temp) {
            temp->left = left;
            temp->right = right;
            temp->next = NULL;
            *index = temp;
            temp->next = curr;
        }
        return;
    }

    while (curr) {
        merged = intersectIndexRange(curr, &target);
        if (merged) {
            break;
        }
        if (target.left > curr->right + 1)
            insert = curr;
        curr = curr->next;
    }

    if (!merged) {
        INDEX_RANGE_T* temp = (INDEX_RANGE_T*)malloc(sizeof(INDEX_RANGE_T));
        if (temp) {
            temp->left = left;
            temp->right = right;
            temp->next = insert->next;
            insert->next = temp;
        }
        return;
    }

    while (curr && curr->next) {
        if (curr->right + 1 < curr->next->left)
            break;

        curr->right = curr->next->right;
        curr->next = curr->next->next;
    }
}

MPEG2_PARSER_ERROR_CODE Mpeg2SeekStream(MPEG2ObjectPtr pDemuxer, uint32 streamNum, uint64* usTime,
                                        uint32 flag) {
#define MAX_SCAN_LOOP \
    500  // avoid slow or dead loop for some tough clips: such as clips which are merged by
         // multi-clips.
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    U64 foundOffset = 0;
    U64 fileOffset = 0, fileOffset2 = 0, topOffset = 0, bottomOffset = 0;
    U64 PTS = 0, topPTS = 0, bottomPTS = 0;
    U64 offsets[3];
    int32 items[3];
    U32 seekFlag = 0;
    U32 topFound = 0, bottomFound = 0;
    FSL_MPEGSTREAM_T* pStreamInfo = (FSL_MPEGSTREAM_T*)&(pDemuxer->SystemInfo.Stream[streamNum]);
    bool needScan = TRUE;
    bool isVideo = pStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM;

    MPG2_PARSER_INTERNAL_LOG(
            "%s: stream: %d, target time: %lld, flag: %d, start time: %lld,  "
            "usLongestStreamDuration: %lld \r\n",
            __FUNCTION__, streamNum, *usTime, flag, pStreamInfo->startTime,
            pDemuxer->usLongestStreamDuration);

    pDemuxer->PTSDeta = 0;
    offsets[0] = offsets[1] = offsets[2] = INDEX_NOSCANNED;
    items[0] = items[1] = items[2] = -1;

    if (isVideo) {
        Err = Mpeg2ParserQueryIndexRange(pDemuxer, streamNum, *usTime, items, offsets);
        if (PARSER_SUCCESS == Err)
            needScan = FALSE;
    }

    if (needScan) {
        bool validKeyFrame = FALSE;
        U64 beginOffset = 0, endOffset = 0;
        U64 keyFrameInterval = 0;
        U64 estimatedOffset = Mpeg2CalcFileOffset(*usTime, pDemuxer->fileSize,
                                                  pDemuxer->usLongestStreamDuration) *
                              9 / 10;

        if (isVideo) {
            if (pDemuxer->usLongestStreamDuration > 1000000) {
                keyFrameInterval =
                        pDemuxer->fileSize * 1000000 / pDemuxer->usLongestStreamDuration * 9 / 10;
                if (pDemuxer->index[streamNum].period > 0)
                    keyFrameInterval = keyFrameInterval * pDemuxer->index[streamNum].period / 1000;
            }

            if (offsets[0] == (U64)INDEX_NOSCANNED)
                fileOffset = 0;
            else {
                fileOffset = offsets[0] + TS_PACKET_LENGTH;
                beginOffset = offsets[0];
            }

            if (estimatedOffset > fileOffset &&
                estimatedOffset - fileOffset > MPG2_MAX_SEARCH_FRAME_SIZE) {
                fileOffset = estimatedOffset;
                beginOffset = estimatedOffset;
            }
        } else {
            fileOffset = estimatedOffset;
        }

        fileOffset2 = fileOffset;

        do {
            fileOffset = fileOffset2;
            seekFlag = 0;

            Err = Mpeg2ParserScan(pDemuxer, streamNum, &fileOffset, &fileOffset2, &PTS, &seekFlag,
                                  0);
            if (Err != PARSER_SUCCESS) {
                break;
            }

            endOffset = fileOffset2;

            if (pStreamInfo->enuStreamType == FSL_MPG_DEMUX_VIDEO_STREAM)
                validKeyFrame =
                        (seekFlag & FSL_MPG_DEMUX_PTS_VALID) && (seekFlag & FLAG_SYNC_SAMPLE);
            else
                validKeyFrame = (seekFlag & FSL_MPG_DEMUX_PTS_VALID);
            if (validKeyFrame) {
                if (isVideo) {
                    Mpeg2ParserUpdateIndex(pDemuxer, streamNum, fileOffset,
                                           pStreamInfo->currentPTS);
                    if (fileOffset + keyFrameInterval > fileOffset2)
                        fileOffset2 = fileOffset + keyFrameInterval;
                } else {
                    // fuzzy handle audio stream
                    if (pStreamInfo->currentPTS < *usTime) {
                        topFound = 1;
                        topPTS = pStreamInfo->currentPTS;
                        topOffset = fileOffset;
                    } else {
                        bottomFound = 1;
                        bottomPTS = pStreamInfo->currentPTS;
                        bottomOffset = fileOffset;
                        break;
                    }
                }
            }
        } while (!validKeyFrame || pStreamInfo->currentPTS < *usTime);

        if (isVideo && endOffset > beginOffset) {
            mergeIndexRange(&(pDemuxer->pIndexRange), beginOffset, endOffset);
            if (isIndexRangeContinuous(pDemuxer->pIndexRange, 0, pDemuxer->fileSize))
                pDemuxer->index[streamNum].status = 1;

            Err = Mpeg2ParserQueryIndexRange(pDemuxer, streamNum, *usTime, items, offsets);
        }
    }

    if (isVideo) {
        if (items[0] >= 0) {
            topFound = 1;
            topPTS = pDemuxer->index[streamNum].pts[items[0]];
            topOffset = offsets[0];
        }
        if (items[2] >= 0) {
            bottomFound = 1;
            bottomPTS = pDemuxer->index[streamNum].pts[items[2]];
            bottomOffset = offsets[2];
        }
        if (items[1] >= 0) {
            if (flag == SEEK_FLAG_NO_LATER) {
                bottomFound = 1;
                bottomPTS = pDemuxer->index[streamNum].pts[items[1]];
                bottomOffset = offsets[1];
            } else {
                topFound = 1;
                topPTS = pDemuxer->index[streamNum].pts[items[1]];
                topOffset = offsets[1];
            }
        }
    } else {
        foundOffset = 0;
        *usTime = 0;
    }

    if (topFound && bottomFound) {
        if (flag == SEEK_FLAG_NEAREST) {
            if (*usTime - topPTS < bottomPTS - *usTime) {
                foundOffset = topOffset;
                *usTime = topPTS;
            } else {
                foundOffset = bottomOffset;
                *usTime = bottomPTS;
            }
        } else if (flag == SEEK_FLAG_NO_EARLIER) {
            foundOffset = bottomOffset;
            *usTime = bottomPTS;
        } else {
            foundOffset = topOffset;
            *usTime = topPTS;
        }
    } else if (topFound) {
        if (flag != SEEK_FLAG_NO_EARLIER) {
            foundOffset = topOffset;
            *usTime = topPTS;
        } else {
            foundOffset = pDemuxer->fileSize;
            *usTime = pStreamInfo->usDuration;
        }
    } else if (bottomFound) {
        if (flag != SEEK_FLAG_NO_LATER) {
            foundOffset = bottomOffset;
            *usTime = bottomPTS;
        } else {
            foundOffset = pDemuxer->fileSize;
            *usTime = pStreamInfo->usDuration;
        }
    }

    MPG2_PARSER_INTERNAL_LOG("seek: top [%d, %lld, %lld], bottom [%d, %lld, %lld]\n", topFound,
                             topPTS, topOffset, bottomFound, bottomPTS, bottomOffset);
    return Mpeg2ResetStreamInfo(pDemuxer, streamNum, foundOffset);
}

/*EOF*/
