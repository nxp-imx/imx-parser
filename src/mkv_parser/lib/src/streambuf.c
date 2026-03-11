/*
 *    Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
 *    Copyright 2022, 2026 NXP
 *    SPDX-License-Identifier: BSD-3-Clause
 *
 */
#include "streambuf.h"
#include <memory.h>
#include <string.h>

// #define STREAM_IO_STAT

#if 0
#define STREAM_LOG printf
#else
#define STREAM_LOG(...)
#endif

int init_stream_buffer(ByteStream* pbs, io_deps* pio_deps, int cachesize, int maxnodelen) {
    if (!pbs || !pio_deps)
        return -1;

    pbs->currpos = 0;
    pbs->bufsize = 0;
    pbs->eofflag = 0;
    pbs->currseek = 0;

    pbs->pbuffer = NULL;
    pbs->buf_sta = NULL;
    pbs->buf_end = NULL;

    pbs->context = pio_deps->context;
    pbs->filesize = pio_deps->filesize;
    pbs->fhandle = pio_deps->filehandle;

    pbs->fpread = pio_deps->fread_ptr;
    pbs->fpseek = pio_deps->fseek_ptr;
    pbs->fpmalloc = pio_deps->malloc_ptr;
    pbs->fpfree = pio_deps->free_ptr;

    pbs->default_cachesize = cachesize;
    pbs->default_max_ebml_offset = maxnodelen;

#define CACHE_BLOCK_NUM 4
#define CAHCE_BLOCK_SIZE (32 * 1024)
    pbs->m_pTStreamCache = create_stream_cache(CACHE_BLOCK_NUM, CAHCE_BLOCK_SIZE, pbs);

    if (!pbs->m_pTStreamCache)
        return -1;
    pbs->m_pTStreamCache->fpread = pbs->fpread;
    pbs->m_pTStreamCache->fpseek = pbs->fpseek;
    pbs->m_pTStreamCache->fhandle = pbs->fhandle;
    pbs->m_pTStreamCache->context = pbs->context;
    pbs->m_pTStreamCache->qwFileLen = pbs->filesize;
    return 0;
}

int deinit_stream_buffer(ByteStream* pbs) {
    if (!pbs)
        return 0;

    pbs->currpos = 0;
    pbs->bufsize = 0;
    pbs->eofflag = 0;
    pbs->currseek = 0;

    if (pbs->pbuffer)
        pbs->fpfree(pbs->pbuffer);

    pbs->filesize = 0;

    pbs->pbuffer = NULL;
    pbs->buf_sta = NULL;
    pbs->buf_end = NULL;
    pbs->fhandle = NULL;

    if (pbs->m_pTStreamCache)
        destory_stream_cache(pbs->m_pTStreamCache, pbs);

    pbs->fpread = NULL;
    pbs->fpseek = NULL;
    pbs->fpfree = NULL;
    pbs->fpmalloc = NULL;

    return 0;
}

static uint64 seek_stream_buffer2(ByteStream* pbs, uint64 seekpos) {
    if (!pbs || !pbs->fpseek)
        return MKV_INVALID_SEEK_POS;

    if (seekpos == pbs->currpos)
        return seekpos;

    if (0 == pbs->fpseek(pbs->fhandle, seekpos, SEEK_SET, pbs->context)) {
        pbs->currpos = seekpos;
        return seekpos;
    } else {
        return MKV_INVALID_SEEK_POS;
    }
}

uint64 seek_stream_buffer(ByteStream* pbs, uint64 seekpos, int force) {
    if (!pbs)
        return MKV_INVALID_SEEK_POS;

    if (!pbs->fpseek)
        return MKV_INVALID_SEEK_POS;

    if (pbs->filesize == 0)
        return seek_stream_buffer2(pbs, seekpos);

    pbs->eofflag = 0;
    if (seekpos >= pbs->filesize) {
        pbs->eofflag = 1;
        return 0;
    }

    if ((force) || pbs->currseek > seekpos ||
        pbs->currseek + pbs->bufsize < seekpos + pbs->default_max_ebml_offset ||
        (NULL == pbs->buf_sta)) {
        pbs->buf_sta = NULL;
        pbs->buf_end = NULL;

        pbs->currseek = seekpos;
        pbs->currpos = seekpos;

        return seekpos;
    }

    pbs->buf_sta = pbs->pbuffer + (seekpos - pbs->currseek);
    pbs->buf_end = pbs->pbuffer + pbs->bufsize;

    return seekpos;
}

char* alloc_stream_buffer(ByteStream* pbs, int size, int stream) {
    if (!pbs)
        return NULL;

    if (!pbs->fpmalloc)
        return NULL;

    if (stream == NON_STREAM)
        return pbs->fpmalloc(size);

    if (pbs->pbuffer && size <= pbs->bufsize)
        return pbs->pbuffer;

    if (pbs->pbuffer)
        pbs->fpfree(pbs->pbuffer);
    pbs->bufsize = 0;

    pbs->pbuffer = pbs->fpmalloc(size);
    if (!pbs->pbuffer)
        return NULL;

    pbs->bufsize = size;

    pbs->buf_sta = NULL;
    pbs->buf_end = NULL;

    return pbs->pbuffer;
}

char* realloc_stream_buffer(ByteStream* pbs, char* ptr, int old_size, int new_size, int stream) {
    if (!pbs)
        return NULL;

    if (!pbs->fpmalloc)
        return NULL;

    if (stream == NON_STREAM) {
        char* new_ptr = pbs->fpmalloc(new_size);
        memcpy(new_ptr, ptr, old_size);
        pbs->fpfree(ptr);
        return new_ptr;
    }

    if (ptr != pbs->pbuffer)
        return NULL;

    if (new_size <= pbs->bufsize)
        return pbs->pbuffer;

    pbs->pbuffer = pbs->fpmalloc(new_size);

    memcpy(pbs->pbuffer, ptr, pbs->bufsize);

    pbs->buf_sta = NULL;
    pbs->buf_end = NULL;

    return pbs->pbuffer;
}

int free_stream_buffer(ByteStream* pbs, char* ptr, int stream) {
    if (!pbs || !ptr)
        return -1;

    if (stream == NON_STREAM) {
        pbs->fpfree(ptr);
        return 0;
    }

    if (pbs->pbuffer != ptr)
        return 0;

    if (ptr)
        pbs->fpfree(ptr);

    pbs->bufsize = 0;
    pbs->pbuffer = NULL;
    pbs->buf_sta = NULL;
    pbs->buf_end = NULL;

    return 0;
}

#ifdef STREAM_IO_STAT
static int real_read = 0;
static int virtual_read = 0;
#endif

int read_stream_buffer2(ByteStream* pbs, char* pptr, int size) {
    uint32 readSize = 0;
    if (!pbs || !pptr || 0 == size)
        return 0;
    readSize = pbs->fpread(pbs->fhandle, pptr, size, pbs->context);
    pbs->currpos += readSize;
    return readSize;
}

int read_stream_buffer(ByteStream* pbs, char** pptr, int size) {
    int bytes = 0;
    int read_size = size;

    if (!pbs || !pptr)
        return 0;

    if (pbs->eofflag)
        return 0;

    if (!size || !pbs->fpread)
        return 0;

    if (pbs->filesize == 0)
        return read_stream_buffer2(pbs, *pptr, size);

    if (!pbs->buf_sta || *pptr != pbs->pbuffer) {
        if (pbs->currpos + size > pbs->filesize)
            read_size = (int)(pbs->filesize - pbs->currpos);

        bytes = CacheRead(pbs->m_pTStreamCache, pbs->currpos, *pptr, read_size);

        if (bytes < 0) {
            return 0;
        }

        if (bytes != size)
            pbs->eofflag = 1;
        else
            pbs->eofflag = 0;

        pbs->currpos += bytes;

        // duplicate the data in the stream buffer
        if (*pptr != pbs->pbuffer) {
            if (bytes >= pbs->bufsize)
                memcpy(pbs->pbuffer, *pptr, pbs->bufsize);
            else {
                memcpy(pbs->pbuffer, *pptr, bytes);
                read_size = pbs->bufsize - bytes;
                read_size = CacheRead(pbs->m_pTStreamCache, pbs->currpos, (pbs->pbuffer + bytes),
                                      read_size);

                pbs->currpos += read_size;
            }
        }
#ifdef STREAM_IO_STAT
        real_read++;
#endif
    } else {
        *pptr = pbs->buf_sta;
        bytes = (int)(pbs->buf_end - pbs->buf_sta);

#ifdef STREAM_IO_STAT
        virtual_read++;
#endif
    }

    return bytes;
}

uint64 get_stream_position(ByteStream* pbs) {
    if (!pbs)
        return 0;

    return pbs->currpos;
}

int eof_stream_buffer(ByteStream* pbs) {
    if (!pbs)
        return 0;

    return pbs->eofflag;
}

#define MAX_BLOCKNUM \
    128  // Since we use full associated cache, if too big, search time when hit is too long
#define SAFE_FREE(p, pbs)   \
    {                       \
        if (p) {            \
            pbs->fpfree(p); \
            p = NULL;       \
        }                   \
    }

StreamCache* create_stream_cache(uint32 dwBlockNum, uint32 dwBlockSize, ByteStream* pbs) {
    uint32 dwBlockIdx;
    uint64 qwTagMask = 0;
    StreamCache* ptCacheMag = NULL;

    if ((dwBlockNum == 0) || (dwBlockNum > MAX_BLOCKNUM) || (dwBlockSize == 0) || (NULL == pbs)) {
        return NULL;
    }

    qwTagMask = ~((uint64)dwBlockSize - 1);

    ptCacheMag = (StreamCache*)pbs->fpmalloc(sizeof(StreamCache));
    if (NULL == ptCacheMag) {
        goto bail;
    }
    memset(ptCacheMag, 0, sizeof(StreamCache));

    ptCacheMag->pBlockList = (TCacheBlock*)pbs->fpmalloc(sizeof(TCacheBlock) * dwBlockNum);
    if (NULL == ptCacheMag->pBlockList) {
        goto bail;
    }
    memset(ptCacheMag->pBlockList, 0, sizeof(TCacheBlock) * dwBlockNum);

    for (dwBlockIdx = 0; dwBlockIdx < dwBlockNum; dwBlockIdx++) {
        ptCacheMag->pBlockList[dwBlockIdx].pbyBlock = (char*)pbs->fpmalloc(dwBlockSize);
        if (NULL == ptCacheMag->pBlockList[dwBlockIdx].pbyBlock) {
            goto bail;
        }
    }

    ptCacheMag->dwBlockNum = dwBlockNum;
    ptCacheMag->dwBlockSize = dwBlockSize;
    ptCacheMag->qwTagMask = qwTagMask;

    return ptCacheMag;

bail:

    if (ptCacheMag && ptCacheMag->pBlockList) {
        for (dwBlockIdx = 0; dwBlockIdx < ptCacheMag->dwBlockNum; dwBlockIdx++) {
            SAFE_FREE(ptCacheMag->pBlockList[dwBlockIdx].pbyBlock, pbs);
        }
    }

    if (ptCacheMag)
        SAFE_FREE(ptCacheMag->pBlockList, pbs);

    SAFE_FREE(ptCacheMag, pbs);

    return NULL;
}

bool destory_stream_cache(StreamCache* pStreamCache, ByteStream* pbs) {
    uint32 dwBlockIdx;

    for (dwBlockIdx = 0; dwBlockIdx < pStreamCache->dwBlockNum; dwBlockIdx++) {
        SAFE_FREE(pStreamCache->pBlockList[dwBlockIdx].pbyBlock, pbs);
    }

    SAFE_FREE(pStreamCache->pBlockList, pbs);

    SAFE_FREE(pStreamCache, pbs);

    return TRUE;
}

bool clear_stream_cache(ByteStream* pbs) {
    uint32 dwBlockIdx;
    StreamCache* pStreamCache = NULL;

    if (pbs == NULL)
        return FALSE;

    pStreamCache = pbs->m_pTStreamCache;

    if (pStreamCache == NULL)
        return FALSE;

    for (dwBlockIdx = 0; dwBlockIdx < pStreamCache->dwBlockNum; dwBlockIdx++) {
        memset(pStreamCache->pBlockList[dwBlockIdx].pbyBlock, 0, pStreamCache->dwBlockSize);
        pStreamCache->pBlockList[dwBlockIdx].bValid = FALSE;
    }

    return TRUE;
}

static bool IsHit(StreamCache* ptCacheMag, uint64 qwTag, uint32* pdwBlockIdx) {
    uint32 dwBlockIdx;

    if (!ptCacheMag)
        return FALSE;
    if (!pdwBlockIdx)
        return FALSE;

    for (dwBlockIdx = 0; dwBlockIdx < ptCacheMag->dwBlockNum; dwBlockIdx++) {
        TCacheBlock* ptCacheBlock = ptCacheMag->pBlockList + dwBlockIdx;
        if (ptCacheBlock->bValid && (ptCacheBlock->qwTag == qwTag)) {
            *pdwBlockIdx = dwBlockIdx;
            return TRUE;
        }
    }

    return FALSE;
}

static uint32 FindBlock2Load(StreamCache* ptCacheMag) {
    uint32 dwBlockIdx;
    uint32 dwMinTraceBlockIdx = 0;
    uint32 dwMinTrace;

    // First, find a invalid block
    for (dwBlockIdx = 0; dwBlockIdx < ptCacheMag->dwBlockNum; dwBlockIdx++) {
        TCacheBlock* ptCacheBlock = ptCacheMag->pBlockList + dwBlockIdx;
        if (ptCacheBlock->bValid == FALSE) {
            return dwBlockIdx;
        }
    }

    dwMinTrace = 0xffffffff;

    // If no invalid, then use LRU to spill out a block
    // May not LRU when rewind, but doesn't matter.
    for (dwBlockIdx = 0; dwBlockIdx < ptCacheMag->dwBlockNum; dwBlockIdx++) {
        TCacheBlock* ptCacheBlock = ptCacheMag->pBlockList + dwBlockIdx;
        if (ptCacheBlock->dwTrace < dwMinTrace) {
            dwMinTrace = ptCacheBlock->dwTrace;
            dwMinTraceBlockIdx = dwBlockIdx;
        }
    }

    return dwMinTraceBlockIdx;
}

static int LoadFromFile(StreamCache* ptCacheMag, uint64 qwTag, uint32* pdwBlockIdx) {
    uint32 dwBlockIdx;
    TCacheBlock* ptCacheBlock;
    uint32 dwLoadSize;
    int ret;
    int bytes;

    if ((!ptCacheMag) || (!pdwBlockIdx))
        return -1;

    if (qwTag >= ptCacheMag->qwFileLen) {
        return -1;
    }

    dwBlockIdx = FindBlock2Load(ptCacheMag);

    ptCacheBlock = ptCacheMag->pBlockList + dwBlockIdx;

    dwLoadSize = ptCacheMag->dwBlockSize;

    // if not enough size left in file, just load left size
    if ((uint32)(ptCacheMag->qwFileLen - qwTag) < ptCacheMag->dwBlockSize) {
        dwLoadSize = (uint32)(ptCacheMag->qwFileLen - qwTag);
    }

    if (NULL == ptCacheMag->fpread)
        return -1;

    if (ptCacheMag->fpseek) {
        ptCacheMag->fpseek(ptCacheMag->fhandle, qwTag, SEEK_SET, ptCacheMag->context);
        ret = ptCacheMag->fpread(ptCacheMag->fhandle, ptCacheBlock->pbyBlock, dwLoadSize,
                                 ptCacheMag->context);

        STREAM_LOG("LoadFromFile 1 ptCacheMag->fpread size %d,return %d, %x%x%x%x \n", dwLoadSize,
                   ret, *ptCacheBlock->pbyBlock, *(ptCacheBlock->pbyBlock + 1),
                   *(ptCacheBlock->pbyBlock + 2), *(ptCacheBlock->pbyBlock + 3));
        if (ret != (int)dwLoadSize) {
            return -1;
        }
    } else {
        bytes = ptCacheMag->fpread(ptCacheMag->fhandle, ptCacheBlock->pbyBlock, dwLoadSize,
                                   ptCacheMag->context);
        STREAM_LOG("LoadFromFile 2 ptCacheMag->fpread size %d,return %d, %x%x%x%x \n", dwLoadSize,
                   ret, *ptCacheBlock->pbyBlock, *(ptCacheBlock->pbyBlock + 1),
                   *(ptCacheBlock->pbyBlock + 2), *(ptCacheBlock->pbyBlock + 3));

        if (bytes != (int)dwLoadSize) {
            return bytes;
        }
    }

    ptCacheBlock->bValid = TRUE;
    ptCacheBlock->qwTag = qwTag;
    ptCacheBlock->dwTrace = ptCacheMag->dwCurTrace;
    ptCacheMag->dwCurTrace++;

    *pdwBlockIdx = dwBlockIdx;

    return 0;
}

int CacheRead(StreamCache* pCache, uint64 qwFilePos, char* pbyBuf, uint32 dwSize) {
    uint64 qwTag = 0;
    uint64 qwReadPos = qwFilePos;
    uint32 dwBlockIdx = 0;
    uint32 dwOffsetInBlock = 0;
    uint32 dwReadSize = 0;
    uint32 dwSize2Read = dwSize;
    char* pbyUserBuf = pbyBuf;
    TCacheBlock* ptCacheBlock;
    int iActualReadSize = 0;

    bool bHit = FALSE;

    if (!pCache)
        return -1;

    if ((dwSize == 0) || (qwFilePos >= pCache->qwFileLen)) {
        return -1;
    }

    do {
        qwTag = qwReadPos & pCache->qwTagMask;
        dwOffsetInBlock = (uint32)(qwReadPos % pCache->dwBlockSize);

        // If beyond file len, trunc read size
        if (qwReadPos + dwSize2Read >= pCache->qwFileLen) {
            dwSize2Read = (uint32)(pCache->qwFileLen - qwReadPos);
        }

        if (dwSize2Read <= pCache->dwBlockSize - dwOffsetInBlock) {
            dwReadSize = dwSize2Read;
            dwSize2Read = 0;
        } else {
            dwReadSize = pCache->dwBlockSize - dwOffsetInBlock;
            dwSize2Read -= dwReadSize;
        }

        qwReadPos += dwReadSize;

        bHit = IsHit(pCache, qwTag, &dwBlockIdx);
        if (FALSE == bHit) {
            if (LoadFromFile(pCache, qwTag, &dwBlockIdx) < 0)
                return -1;
        }

        // read from cache
        ptCacheBlock = pCache->pBlockList + dwBlockIdx;
        memcpy(pbyUserBuf, ptCacheBlock->pbyBlock + dwOffsetInBlock, dwReadSize);
        pbyUserBuf += dwReadSize;
        iActualReadSize += dwReadSize;

    } while (dwSize2Read > 0);

    return iActualReadSize;
}
#ifdef ANDROID
#define DUMP_DEC_OUTPUT_FILE "/data/dump.mkv"
#else
#define DUMP_DEC_OUTPUT_FILE "/home/root/dump.mkv"
#endif

void dumpBuffer(void* inBuf, int size) {
    FILE* pfile = NULL;
    int ret;

    if (!inBuf) {
        return;
    }

    pfile = fopen(DUMP_DEC_OUTPUT_FILE, "ab");

    if (pfile) {
        ret = fwrite(inBuf, 1, size, pfile);
        if (ret) {
            STREAM_LOG("dumpBuffer write %d, tar=%d\n", ret, size);
        }
        fclose(pfile);
    }

    return;
}
