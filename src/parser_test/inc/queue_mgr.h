/************************************************************************
 * Copyright 2005-2010 by Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ************************************************************************/

#ifndef _FSL_MMLAYER_QUEUE_H
#define _FSL_MMLAYER_QUEUE_H

enum {
    EVENT_NEW_SEGMENT,
    EVENT_EOS
}; /* Event type of the queue */

HANDLE createQueue(uint32 maxSizeBytes, uint32 maxSizeBuffers, uint32 fixedBufferSize);

void deleteQueue(HANDLE hQueue);

void flushQueue(HANDLE hQueue);

uint8* getBuffer(HANDLE hQueue, uint32 sizeRequested, uint32* sizeGot, void** bufferContext);

int32 pushBuffer(HANDLE hQueue, uint8* buffer, void* bufferContext, uint32 dataSize,
                 uint64 usTimeStamp, uint64 usReferenceClock, uint64 usLastTimeStamp);

void unrefBuffer(HANDLE hQueue, uint8* buffer,
                 void* bufferContext); /* Not output data, just release it */

int32 pushEvent(HANDLE hQueue, uint32 eventType, uint64 usTimeStamp, /* start time of the segment */
                int32 rate);                                         /* rate of the segment */

void displayQueueStatistics(HANDLE hQueue);
void displayQueueFullness(HANDLE hQueue);

int32 queue_unit_test();

#endif /* _FSL_MMLAYER_QUEUE_H */
