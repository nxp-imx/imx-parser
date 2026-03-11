/*
 *    Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
 *    Copyright 2022-2023, 2026 NXP
 *    SPDX-License-Identifier: BSD-3-Clause
 *
 */
#ifndef _STREAM_BUF_H_
#define _STREAM_BUF_H_

#include "mkv_parser_api.h"

#define STREAM_MODE 0
#define NON_STREAM 1
#ifndef NULL
#define NULL ((void*)0)
#endif
#define MIN_SEEK_SPACE (16 + 8)  // max size(id+offset+size)
#define MKV_INVALID_SEEK_POS (uint64)(0xFFFFFFFFFFFFFFFFULL)

typedef int32 (*fseek_func_ptr)(void*, int64, int32, void*);
typedef uint32 (*fread_func_ptr)(void*, void*, uint32, void*);
typedef void* (*malloc_func_ptr)(uint32);
typedef void (*free_func_ptr)(void*);
typedef int32 (*fclose_func_ptr)(void* handle, void* context); /* Close the stream */

typedef struct {
    void* context;
    int64 filesize;
    void* filehandle;

    malloc_func_ptr malloc_ptr;
    free_func_ptr free_ptr;
    fseek_func_ptr fseek_ptr;
    fread_func_ptr fread_ptr;
    fclose_func_ptr fclose_ptr;
} io_deps;

typedef struct tagCacheBlock {
    uint64 qwTag;
    bool bValid;
    uint32 dwTrace;  // Reserved
    char* pbyBlock;
} TCacheBlock;

typedef struct tagStreamCache {
    TCacheBlock* pBlockList;
    uint32 dwBlockNum;
    uint32 dwBlockSize;
    uint64 qwTagMask;
    uint32 dwCurTrace;

    uint32 m_dwReadCount;
    uint32 m_dwMissCount;

    uint64 qwFileLen;
    fread_func_ptr fpread;
    fseek_func_ptr fpseek;
    void* fhandle;
    void* context;

} StreamCache;

typedef struct {
    void* fhandle;
    void* context;

    int eofflag;
    uint64 currseek;
    uint64 currpos;
    uint64 filesize;

    int bufsize;
    char* pbuffer;
    char* buf_sta;
    char* buf_end;

    fread_func_ptr fpread;
    fseek_func_ptr fpseek;
    malloc_func_ptr fpmalloc;
    free_func_ptr fpfree;

    int default_cachesize;
    int default_max_ebml_offset;

    StreamCache* m_pTStreamCache;
} ByteStream;

int init_stream_buffer(ByteStream* pbs, io_deps* pio_deps, int cachesize, int maxnodesize);
int deinit_stream_buffer(ByteStream* pbs);

char* realloc_stream_buffer(ByteStream* pbs, char* ptr, int old_size, int new_size, int stream);
char* alloc_stream_buffer(ByteStream* pbs, int size, int stream);
int free_stream_buffer(ByteStream* pbs, char* ptr, int stream);

int read_stream_buffer(ByteStream* pbs, char** pptr, int size);
uint64 seek_stream_buffer(ByteStream* pbs, uint64 seekpos, int force);

uint64 get_stream_position(ByteStream* pbs);
int eof_stream_buffer(ByteStream* pbs);

StreamCache* create_stream_cache(uint32 dwBlockNum, uint32 dwBlockSizeBits, ByteStream* pbs);
bool destory_stream_cache(StreamCache* pStreamCache, ByteStream* pbs);
int CacheRead(StreamCache* pCache, uint64 qwFilePos, char* pbyBuf, uint32 dwSize);

void dumpBuffer(void* inBuf, int size);
bool clear_stream_cache(ByteStream* pbs);

#endif
