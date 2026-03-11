/*
 ***********************************************************************
 * Copyright (c) 2005-2013, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#ifndef INCLUDED_AVI_UTILITIES_H
#define INCLUDED_AVI_UTILITIES_H

// #include <assert.h>
// #include <stdio.h>

#ifdef __WINCE
#include <windows.h>
#endif

#if defined(__WINCE)
// #define AVIMSG(cond, fmt, ...) DEBUGMSG(cond, _T(fmt), __VA_ARGS__))
#define AVIMSG(fmt, ...) DEBUGMSG(1, (_T(fmt), __VA_ARGS__))

#elif defined(WIN32)
#define AVIMSG(fmt, ...) DEBUGMSG(1, (fmt, __VA_ARGS__))

#elif defined(ANDROID)
#include "android/log.h"
#define printf(...) __android_log_print(ANDROID_LOG_INFO, "AVI PARSER", __VA_ARGS__)
#ifdef DEBUG
#define AVIMSG printf
#else
#define AVIMSG(...)
#endif

#else /* linux platfom */
#ifdef DEBUG
#define AVIMSG printf
#else
#define AVIMSG(fmt...)
#endif
#endif

#define DRMMSG AVIMSG

#ifndef NULL
#define NULL 0
#endif

#define TESTMALLOC(ptr)                   \
    if ((ptr) == 0) {                     \
        err = PARSER_INSUFFICIENT_MEMORY; \
        goto bail;                        \
    }

#define BAILWITHERROR(v) \
    {                    \
        err = (v);       \
        goto bail;       \
    }

/* Warning! "LocalReAlloc" and "LocalFree" is already declared by the windows and have different
definition. So we use uppercase "LOCAL***" to avoid conflict.*/

#ifndef AVI_MEM_DEBUG
/* default, no memory debug feature. Parser never alloc/free memory by itself */

/* Local Fucntion for calloc. */
void* LOCALCalloc(uint32 number, uint32 size);

/* Local Fucntion for malloc. */
void* LOCALMalloc(uint32 size);

/* Local Fucntion to Free the Memory. */
void LOCALFree(void* MemoryBlock);

void* LOCALReAlloc(void* MemoryBlock, uint32 size);

#else
#ifdef AVI_MEM_DEBUG_SELF
/* self memory debug */
void mm_mm_init(void);
void mm_mm_exit(void);

void* mm_calloc(int nobj, int size, const char* filename, int line);
void* mm_malloc(int size, const char* filename, int line);
void* mm_realloc(void* ptr, int size, const char* filename, int line);
void mm_free(void* ptr, const char* filename, int line);

#define LOCALMalloc(size) mm_malloc((size), __FILE__, __LINE__)
#define LOCALCalloc(nobj, size) mm_calloc((nobj), (size), __FILE__, __LINE__)
#define LOCALReAlloc(ptr, size) mm_realloc((ptr), (size), __FILE__, __LINE__)
#define LOCALFree(ptr) mm_free((ptr), __FILE__, __LINE__)

#else
/* AVI_MEM_DEBUG_OTHER is defined, for 3-party memory debug tool, eg. valgrind */
#define LOCALCalloc calloc
#define LOCALMalloc malloc
#define LOCALReAlloc realloc
#define LOCALFree free

#endif
#endif /* AVI_MEM_DEBUG */

void* alignedMalloc(uint32 size, uint32 alignSize);
void alignedFree(void* ptr);

/* file I/O */
typedef struct {
    FslFileHandle fileHandle;

    FslFileHandle (*Open)(const uint8* fileName, const uint8* mode,
                          void* context);                /* Open a file or URL */
    int32 (*Close)(FslFileHandle handle, void* context); /* Close the stream */
    uint32 (*Read)(FslFileHandle handle, void* buffer, uint32 size,
                   void* context); /* Read data from the stream */
    int32 (*Seek)(FslFileHandle handle, int64 offset, int32 whence,
                  void* context); /* Seek the stream */
    int64 (*Tell)(FslFileHandle handle,
                  void* context); /* Tell the current position from start of the stream */
    int64 (*Size)(FslFileHandle handle, void* context); /* Get the size of the entire stream */
    int64 (*CheckAvailableBytes)(FslFileHandle handle, int64 bytesRequested,
                                 void* context); /* How many bytes cached but not read yet */

} AviInputStream;

#define LocalFileOpen g_streamOps.Open
#define LocalFileRead(inputStream, buffer, size, context) \
    g_streamOps.Read(inputStream->fileHandle, buffer, size, context)
#define LocalFileSeek(inputStream, offset, whence, context) \
    g_streamOps.Seek(inputStream->fileHandle, offset, whence, context)

#define LocalFileTell(inputStream, context) g_streamOps.Tell(inputStream->fileHandle, context)
#define LocalFileSize(inputStream, context) g_streamOps.Size(inputStream->fileHandle, context)
#define LocalFileClose g_streamOps.Close

extern FslFileStream g_streamOps;
extern ParserMemoryOps g_memOps;
extern ParserOutputBufferOps g_outputBufferOps;

int32 readData(AviInputStream* s, void* buffer, uint32 size, void* context);
int32 read16(AviInputStream* s, uint16* outVal, void* context);
int32 read32(AviInputStream* s, uint32* outVal, void* context);
int32 read64(AviInputStream* s, uint64* outVal, void* context);

uint16 read16At(uint8* pBytePtr);
uint32 read32At(uint8* pBytePtr);

/* note: length is 32-bit integer, AVI has not atom size of 64-bit long.
Warning:sometimes file pointer will step backward, because some atom can be less than it shall be!*/
#define SKIPBYTES_FORWARD(length)                                      \
    {                                                                  \
        uint32 available = self->size - self->bytesRead;               \
        if (available < (length))                                      \
            length = available;                                        \
    }                                                                  \
    LocalFileSeek(inputStream, (int64)(length), SEEK_CUR, appContext); \
    self->bytesRead += (length)

#define GETBYTES(buff, length)                             \
    err = readData(inputStream, buff, length, appContext); \
    if (err)                                               \
        goto bail;                                         \
    self->bytesRead += (length)

#define GET8(membername)                                   \
    {                                                      \
        uint8 val8;                                        \
        err = readData(inputStream, &val8, 1, appContext); \
        if (err)                                           \
            goto bail;                                     \
        self->membername = val8;                           \
    }                                                      \
    self->bytesRead += 1

#define GET16(membername)                              \
    {                                                  \
        uint16 val16;                                  \
        err = read16(inputStream, &val16, appContext); \
        if (err)                                       \
            goto bail;                                 \
        self->membername = val16;                      \
    }                                                  \
    self->bytesRead += 2

#define GET32(membername)                              \
    {                                                  \
        uint32 val32;                                  \
        err = read32(inputStream, &val32, appContext); \
        if (err)                                       \
            goto bail;                                 \
        self->membername = val32;                      \
    }                                                  \
    self->bytesRead += 4

#define GET64(membername)                              \
    {                                                  \
        uint64 val64;                                  \
        err = read64(inputStream, &val64, appContext); \
        if (err)                                       \
            goto bail;                                 \
        self->membername = val64;                      \
    }                                                  \
    self->bytesRead += 8

void PrintTag(uint32 tag);
void PrintTagSize(uint32 tag, uint32 size);

#define GetTag GET32

#define fourcc(a, b, c, d) (((d) << 24) | ((c) << 16) | ((b) << 8) | (a))

#define STREAM_NUM_FROM_TAG(fourcc) \
    (((((fourcc) & 0xff) - '0') * 10) + ((((fourcc) & 0xff00) >> 8) - '0'))

// #define STREAM_NUM_FROM_TAG( fourcc)   (((((fourcc)&0xff) - '0')<<4)|((((fourcc)&0xff00)>>8) -
// '0')) #define NUM_FROM_IDX1_TAG( fourcc)    ((((((fourcc)&0xff0000)>>16) -
// '0')<<4)|((((fourcc)&0xff000000)>>24) - '0'))

// #define STREAM_NUM_FROM_TAG( fourcc)  ((fourcc[0]-'0')*10+ (fourcc[1]-'0'))

void getVideoCodecType(uint32 fccHandler, uint32 fccCompression, uint32* decoderType,
                       uint32* decoderSubtype);

void getAudioCodecType(uint32 fccHandler, uint16 formatTag, uint32 bitPerSample,
                       uint32* decoderType, uint32* decoderSubtype);

AviInputStream* duplicateFileHandler(AviInputStream* src);
void disposeFileHandler(AviInputStream* inputStream, void* appContext);

#endif /* INCLUDED_AVI_UTILITIES_H */
