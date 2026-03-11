/*
***********************************************************************
* Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
* Copyright 2025-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fsl_types.h"
#include "fsl_osal.h"
#include "flist.h"
#include "queue_mgr.h"
#include "memory_mgr.h"
#include "err_logs.h"
#include "fsl_parser.h"
#include "fsl_parser_drm.h"

#if defined(__WINCE)
// #define QUEUE_MSG(cond, fmt, ...) DEBUGMSG(cond, _T(fmt), __VA_ARGS__))
#define QUEUE_MSG(fmt, ...) DEBUGMSG(1, (_T(fmt), __VA_ARGS__))

#elif defined(WIN32)
// #define QUEUE_MSG(cond, fmt, ...) DEBUGMSG(cond, _T(fmt), __VA_ARGS__))
#define QUEUE_MSG(fmt, ...) DEBUGMSG(1, (fmt, __VA_ARGS__))

#else /* linux platfom */
#ifdef DEBUG
#define QUEUE_MSG printf
#else
#define QUEUE_MSG(fmt...)
#endif
#endif

typedef struct _BufferType BufferType;

struct _BufferType {
    void* mem_ptr;  /*!< memory address */
    int block_size; /*!< buffer size */
    int data_size;

    uint64 usTimeStamp;

    void* context;

    /*! link item to make double link list */
    FLIST_LINK_ITEM(BufferType);
};

typedef struct {
    uint32 maxSizeBytes; /* for gstreamer style */

    uint32 maxSizeBuffers;  /* for dshow style */
    uint32 fixedBufferSize; /* in bytes */

    uint32 memorySizeUsed; /* total memory size used (not the actual data size), used by both given
                              out queue and decoding queue */
    uint32 peakMemorySize;

    uint32 dataSizeInQueue; /* actual data size in the decoding queue */
    uint32 peakDataSizeInQueue;

    uint32 numBuffersUsed; /* only for dshow style */
    uint32 peakNumBuffersUsed;

    uint64 usClockTime;
    uint64 usLastTimeStamp;

    int32 rate;

    /* given out queue */
    BufferType* buffersGivenOut;

    /* decoding queue */
    BufferType* buffersInQueue;

} tFslQueue;

void flushSTDBuffer(tFslQueue* queue);

HANDLE createQueue(uint32 maxSizeBytes, uint32 maxSizeBuffers, uint32 fixedBufferSize) {
    tFslQueue* queue = NULL;

    if ((maxSizeBytes && maxSizeBuffers) || (maxSizeBuffers && (0 == fixedBufferSize))) {
        PARSER_ERROR("Invalid parameter, neither Gstreamer nor DShow style!\n");
        goto bail;
    }

    queue = fsl_osal_calloc(1, sizeof(tFslQueue));
    if (!queue)
        goto bail;

    if (maxSizeBytes) {
        queue->maxSizeBytes = maxSizeBytes;
        PARSER_INFO(PARSER_INFO_BUFFER, "Queue max size in bytes: %ld bytes\n", maxSizeBytes);
    } else if (maxSizeBuffers) {
        queue->maxSizeBuffers = maxSizeBuffers;
        queue->fixedBufferSize = fixedBufferSize;
        PARSER_INFO(PARSER_INFO_BUFFER,
                    "Queue max buffer count: %ld, single buffer size: %ld bytes\n", maxSizeBuffers,
                    fixedBufferSize);
    }

bail:
    return (HANDLE)queue;
}

void deleteQueue(HANDLE hQueue) {
    tFslQueue* queue = (tFslQueue*)hQueue;

    BufferType *buf, *buf_tmp;

    flushSTDBuffer(queue);

    PARSER_INFO(PARSER_INFO_BUFFER, "release given out buffers ...\n");
    FLIST_FOR_EACH_SAFE(queue->buffersGivenOut, buf, buf_tmp) {
        FLIST_DELETE(queue->buffersGivenOut, buf);
        if (buf != buf->context)
            PARSER_ERROR("ERR! Corrupted buffer struct\n");

        PARSER_INFO(PARSER_INFO_BUFFER, "free buffer 0x%p, size %d, pts %lld\n", buf->mem_ptr,
                    buf->block_size, buf->usTimeStamp);
        if (buf->mem_ptr)
            MM_Free(buf->mem_ptr);

        MM_Free(buf);
    }

    fsl_osal_free(queue);

    return;
}

void flushQueue(HANDLE hQueue) {
    flushSTDBuffer((tFslQueue*)hQueue);
}

uint8* getBuffer(HANDLE hQueue, uint32 sizeRequested, uint32* sizeGot, void** bufferContext) {
    tFslQueue* queue = (tFslQueue*)hQueue;
    BufferType* buf = NULL;
    uint8* mem_ptr = NULL;
    uint32 size = 0;

    if (queue->maxSizeBytes) {
        size = sizeRequested;
        if (queue->maxSizeBytes - queue->memorySizeUsed < size) {
            PARSER_ERROR("queue %p, STD buffer full, max data size %d bytes, free size %d bytes\n",
                         queue, queue->maxSizeBytes, queue->maxSizeBytes - queue->memorySizeUsed);
            goto bail;
        }
    }

    if (queue->maxSizeBuffers) {
        size = queue->fixedBufferSize;
        if (queue->maxSizeBuffers <= queue->numBuffersUsed) {
            PARSER_ERROR(
                    "queue %p, STD buffer full, all %d buffer are used, data size in queue %ld "
                    "bytes\n",
                    queue, queue->maxSizeBuffers, queue->dataSizeInQueue);
            goto bail;
        }
    }

    mem_ptr = (uint8*)MM_Malloc(size);
    if (!mem_ptr)
        goto bail;

    buf = (BufferType*)MM_Calloc(1, sizeof(BufferType));
    if (!buf) {
        MM_Free(mem_ptr);
        mem_ptr = NULL;
        goto bail;
    }

    buf->mem_ptr = mem_ptr;
    buf->block_size = size;
    buf->context = buf;
    *bufferContext = buf;

    queue->memorySizeUsed += size;
    queue->numBuffersUsed += 1;

    if (queue->peakMemorySize < queue->memorySizeUsed)
        queue->peakMemorySize = queue->memorySizeUsed;

    if (queue->peakNumBuffersUsed < queue->numBuffersUsed)
        queue->peakNumBuffersUsed = queue->numBuffersUsed;

    FLIST_ITEM_INIT(buf);
    FLIST_ADD(queue->buffersGivenOut, buf);

    *sizeGot = size;

    if (queue->maxSizeBytes)
        PARSER_INFO(PARSER_INFO_BUFFER, "queue %p, STD add %d bytes, total size %d, free size %d\n",
                    queue, buf->block_size, queue->memorySizeUsed,
                    queue->maxSizeBytes - queue->memorySizeUsed);
    else
        PARSER_INFO(PARSER_INFO_BUFFER,
                    "queue %p, STD add %d bytes, total size %d, free buffer count %d\n", queue,
                    buf->block_size, queue->memorySizeUsed,
                    queue->maxSizeBuffers - queue->numBuffersUsed);

bail:
    return mem_ptr;
}

int32 pushBuffer(HANDLE hQueue, uint8* buffer, void* bufferContext, uint32 dataSize,
                 uint64 usTimeStamp, uint64 usReferenceClock, uint64 usLastTimeStamp)

{
    int32 err = 0;
    tFslQueue* queue = (tFslQueue*)hQueue;
    BufferType* buf = (BufferType*)bufferContext;
    BufferType* buf_tmp;
    uint64 usClockTime;
    int32 rate = queue->rate;
    ;

    if (buf != buf->context) {
        PARSER_INFO(PARSER_INFO_BUFFER, "ERR! queue %p, push buf 0x%p, buffer context mismatch!\n",
                    queue, buffer);
        err = -1;
        goto bail;
    }

    PARSER_INFO(PARSER_INFO_BUFFER,
                "\nPush buf 0x%p, buffer size %d, data size %d, pts %lld, ref clk %lld\n", buffer,
                buf->block_size, dataSize, usTimeStamp, usReferenceClock);

    buf->data_size = dataSize;
    queue->dataSizeInQueue += dataSize;
    if (queue->peakDataSizeInQueue < queue->dataSizeInQueue)
        queue->peakDataSizeInQueue = queue->dataSizeInQueue;
    PARSER_INFO(PARSER_INFO_BUFFER, "data size in queue %ld, peak data size %d\n",
                queue->dataSizeInQueue, queue->peakDataSizeInQueue);

    FLIST_DELETE(queue->buffersGivenOut, buf);

    if ((uint64)PARSER_UNKNOWN_TIME_STAMP != usTimeStamp)
        buf->usTimeStamp = usTimeStamp;
    else {
        // how can we set buf time if the timestamp is PARSER_UNKNOWN_TIME_STAMP
    }

    FLIST_ADD(queue->buffersInQueue, buf);

    if (0 == rate) {
        PARSER_ERROR("ERR! segment rate not set yet\n");
        return -1;
    }

    if (0 < rate) {
        if ((uint64)PARSER_UNKNOWN_TIME_STAMP != usTimeStamp &&
            queue->usLastTimeStamp > usTimeStamp)
            PARSER_INFO(PARSER_INFO_PTS, "Waring! Abnormal PTS %lld, prev PTS %lld! (rate %f)\n",
                        usTimeStamp, queue->usLastTimeStamp, queue->rate);
    } else { /* rewind */
        if ((uint64)PARSER_UNKNOWN_TIME_STAMP != usTimeStamp &&
            queue->usLastTimeStamp < usTimeStamp)
            PARSER_INFO(PARSER_INFO_PTS, "Waring! Abnormal PTS %lld, prev PTS %lld! (rate %f)\n",
                        usTimeStamp, queue->usLastTimeStamp, queue->rate);
    }

    if ((uint64)PARSER_UNKNOWN_TIME_STAMP != usTimeStamp)
        queue->usLastTimeStamp = usTimeStamp;

    queue->usClockTime = usReferenceClock;

    usClockTime = queue->usClockTime;

    PARSER_INFO(PARSER_INFO_PTS, "Clock time %lld us (rate %d)\n", usClockTime, rate);
    FLIST_FOR_EACH_SAFE(queue->buffersInQueue, buf, buf_tmp) {
        if (0 < (int64)(usClockTime - buf->usTimeStamp) * rate ||
            usTimeStamp == (uint64)PARSER_UNKNOWN_TIME_STAMP) {
            FLIST_DELETE(queue->buffersInQueue, buf);
            if (buf != buf->context)
                PARSER_ERROR("ERR! Corrupted buffer struct\n");

            queue->memorySizeUsed -= buf->block_size;
            queue->numBuffersUsed -= 1;
            queue->dataSizeInQueue -= buf->data_size;

            if (queue->maxSizeBytes)
                PARSER_INFO(PARSER_INFO_BUFFER, "STD remove %d bytes, pts %lld, size left %d\n",
                            buf->block_size, buf->usTimeStamp, queue->memorySizeUsed);
            else
                PARSER_INFO(PARSER_INFO_BUFFER,
                            "STD remove %d bytes, pts %lld, size left %d, buffer count used %d\n",
                            buf->block_size, buf->usTimeStamp, queue->memorySizeUsed,
                            queue->numBuffersUsed);

            if (buf->mem_ptr)
                MM_Free(buf->mem_ptr);

            MM_Free(buf);
        }
    }

    (void)usLastTimeStamp;
    (void)buffer;
bail:
    return err;
}

void unrefBuffer(HANDLE hQueue, uint8* buffer, void* bufferContext) {
    tFslQueue* queue = (tFslQueue*)hQueue;
    BufferType* buf = (BufferType*)bufferContext;

    if (buf != buf->context) {
        PARSER_ERROR("ERR! queue %p, buffer context mismatch!\n", queue);
        goto bail;
    }

    /* PTS is 0 because not set yet */
    PARSER_INFO(PARSER_INFO_BUFFER, "\nUnref buf 0x%p, size %d, pts %lld\n", buf, buf->block_size,
                buf->usTimeStamp);

    queue->memorySizeUsed -= buf->block_size;
    queue->numBuffersUsed -= 1;

    if (queue->maxSizeBytes)
        PARSER_INFO(PARSER_INFO_BUFFER, "STD remove %d bytes, size left %d\n", buf->block_size,
                    queue->memorySizeUsed);
    else
        PARSER_INFO(PARSER_INFO_BUFFER, "STD remove %d bytes, size left %d, buffer count used %d\n",
                    buf->block_size, queue->memorySizeUsed, queue->numBuffersUsed);

    FLIST_DELETE(queue->buffersGivenOut, buf);
    if (buf->mem_ptr) {
        MM_Free(buf->mem_ptr);
        buf->mem_ptr = NULL;
    }

    /* todo: discard time out data */
    (void)buffer;
bail:
    return;
}

int32 pushEvent(HANDLE hQueue, uint32 eventType, uint64 usStartTime, int32 rate) {
    int32 err = 0;
    tFslQueue* queue = (tFslQueue*)hQueue;

    if ((EVENT_NEW_SEGMENT != eventType) && (EVENT_EOS != eventType))
        return -1; /* nothing to do */

    if (EVENT_NEW_SEGMENT == eventType) {
        if (!rate) {
            PARSER_ERROR("Err: invalid rate %d for new segment\n", rate);
            return -1;
        }
        queue->usClockTime = usStartTime;
        queue->usLastTimeStamp = usStartTime;
        queue->rate = rate;

        PARSER_INFO(PARSER_INFO_BUFFER, "\nqueue %p, new segment, start time %lld, rate %d\n",
                    hQueue, usStartTime, rate);
    } else {
        PARSER_INFO(PARSER_INFO_BUFFER, "\nEOS\n");
    }
    flushSTDBuffer(queue);

    // bail:
    return err;
}

void flushSTDBuffer(tFslQueue* queue) {
    BufferType* buf;
    BufferType* buf_tmp;

    PARSER_INFO(PARSER_INFO_BUFFER, "flush buffers in STD queue ...\n");
    FLIST_FOR_EACH_SAFE(queue->buffersInQueue, buf, buf_tmp) {
        FLIST_DELETE(queue->buffersInQueue, buf);
        if (buf != buf->context)
            PARSER_INFO(PARSER_INFO_BUFFER, "ERR! Corrupted buffer struct\n");

        PARSER_INFO(PARSER_INFO_BUFFER, "free buffer 0x%p, size %d, pts %lld\n", buf->mem_ptr,
                    buf->block_size, buf->usTimeStamp);
        if (buf->mem_ptr)
            MM_Free(buf->mem_ptr);

        queue->memorySizeUsed -= buf->block_size;
        queue->numBuffersUsed -= 1;
        queue->dataSizeInQueue -= buf->data_size;

        MM_Free(buf);
    }

    queue->peakMemorySize = 0;
    queue->peakNumBuffersUsed = 0;
    queue->peakDataSizeInQueue = 0;
}

void displayQueueStatistics(HANDLE hQueue) {
    tFslQueue* queue = (tFslQueue*)hQueue;

    if (queue->maxSizeBytes) {
        printf("queue %p, max size: %u bytes, peak data fullness: %u bytes ( %.2f %% )\n", queue,
               queue->maxSizeBytes, queue->peakMemorySize,
               (double)queue->peakMemorySize * 100 / queue->maxSizeBytes);
    } else {
        uint32 maxSizeBytes = queue->maxSizeBuffers * queue->fixedBufferSize;

        printf("queue %p, max size: %u buffers, peak buffer usage: %u buffers( %.2f %% ), peak "
               "data fullness: %u bytes ( %.2f %% )\n",
               queue, queue->maxSizeBuffers, queue->peakNumBuffersUsed,
               (double)queue->peakNumBuffersUsed * 100 / queue->maxSizeBuffers,
               queue->peakDataSizeInQueue, (double)queue->peakDataSizeInQueue * 100 / maxSizeBytes);
    }
}

void displayQueueFullness(HANDLE hQueue) {
    tFslQueue* queue = (tFslQueue*)hQueue;

    if (queue->maxSizeBytes) {
        printf("queue %p, %u bytes used\n", queue, queue->memorySizeUsed);
    }

    else if (queue->maxSizeBuffers) {
        printf("queue %p, %u buffers used, %u bytes used, data size in queue %u bytes\n", queue,
               queue->numBuffersUsed, queue->memorySizeUsed, queue->dataSizeInQueue);
    }
}

#define BUFFER_SIZE 10
#define BUFFER_SIZE_STEP 10
#define MEM_POOL_SIZE 1024

#define DECODING_DELAY_IN_MS 45
#define PTS_STEP_IN_US 10

int32 queue_unit_test() {
    int32 err = 0;
    HANDLE hQueue = NULL;

    BufferType* buffersOnHand = NULL;
    uint32 bufferCount = 0;
    uint64 usTimeStamp = 0;
    uint64 usRefClock = 0;
    int32 playRate = 1;

    uint32 bufferSizeRequeted = BUFFER_SIZE;
    uint32 bufferSizeGot;

    uint8* mem_ptr;
    void* bufferContext;

    BufferType *buf, *buf_tmp, *tail;

    uint32 i;

    hQueue = createQueue(0, 10, 100);
    if (!hQueue) {
        PARSER_INFO(PARSER_INFO_BUFFER, "fail to create queue\n");
        err = -1;
        goto bail;
    }

    err = pushEvent(hQueue, EVENT_NEW_SEGMENT, 0, playRate);

    for (i = 0; i < 1000; i++) {
        mem_ptr = getBuffer(hQueue, bufferSizeRequeted, &bufferSizeGot, &bufferContext);
        if (!mem_ptr) {
            QUEUE_MSG("can not get buffer from queue\n");
            break;
        }

        buf = (BufferType*)MM_Calloc(1, sizeof(BufferType));
        if (!buf) {
            QUEUE_MSG("fail to malloc list node\n");
            err = -1;
            goto bail;
        }

        QUEUE_MSG("get buffer 0x%p, size %d, pts %lld\n", mem_ptr, (int)bufferSizeGot, usTimeStamp);
        buf->mem_ptr = mem_ptr;
        buf->block_size = bufferSizeGot;
        buf->context = bufferContext;
        buf->usTimeStamp = usTimeStamp;

        FLIST_ITEM_INIT(buf);
        FLIST_ADD(buffersOnHand, buf);

        usTimeStamp += PTS_STEP_IN_US;
        bufferSizeRequeted += BUFFER_SIZE_STEP;

        bufferCount++;
    }

#if 0
    FLIST_FIND_TAIL(buffersOnHand, buf_tmp, tail);
    QUEUE_MSG("\nTail buffer 0x%p, pts %lld\n", tail->mem_ptr, tail->usTimeStamp);
    
    FLIST_FOR_EACH_SAFE_REVERSE(tail, buf, buf_tmp)
    {
        FLIST_DELETE (buffersOnHand, buf);
        
        pushBuffer(hQueue, buf->mem_ptr, buf->context, buf->usTimeStamp);
        MM_Free(buf);
    }
#endif

#if 0
    FLIST_FIND_TAIL(buffersOnHand, buf_tmp, tail);
    QUEUE_MSG("\nTail buffer 0x%p, pts %lld\n", tail->mem_ptr, tail->usTimeStamp);
    
    FLIST_FOR_EACH_SAFE_REVERSE(tail, buf, buf_tmp)
    {
        FLIST_DELETE (buffersOnHand, buf);
        
        unrefBuffer(hQueue, buf->mem_ptr, buf->context);
        MM_Free(buf);
    }
#endif

    if (buffersOnHand) {
        FLIST_FIND_TAIL(buffersOnHand, buf_tmp, tail);
        i = 0;

        QUEUE_MSG("\nTail buffer 0x%p, pts %lld, size %d\n", tail->mem_ptr, tail->usTimeStamp,
                  buf->block_size);

        FLIST_FOR_EACH_SAFE_REVERSE(tail, buf, buf_tmp) {
            FLIST_DELETE(buffersOnHand, buf);

            if (i)
                unrefBuffer(hQueue, buf->mem_ptr, buf->context);
            else
                pushBuffer(hQueue, buf->mem_ptr, buf->context, buf->block_size / 2,
                           buf->usTimeStamp, usRefClock, 0);

            MM_Free(buf);

            i = ~i;
        }
    }

bail:
    QUEUE_MSG("++++++++++++++++++++++++\n");
    if (hQueue) {
        displayQueueStatistics(hQueue);
        deleteQueue(hQueue);
    }

    QUEUE_MSG("queue unit test ends. err %d\n", (int)err);
    return err;
}
