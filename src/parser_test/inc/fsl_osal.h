/************************************************************************
 * Copyright 2005-2010 by Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ************************************************************************/

/*=============================================================================

  Module Name:  fsl_parser_test.c

  General Description:  Common unit test for all FSL core parser libraries.

  ===============================================================================
  INCLUDE FILES
  =============================================================================*/
#ifndef _FSL_MMLAYER_OSAL_H
#define _FSL_MMLAYER_OSAL_H

#include <stdlib.h>

#if !(defined(__WINCE) || defined(WIN32))  // Wince doesn't support stat method TLSbo80080
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#else
#include <windows.h>
#endif
#include <string.h>

#include "fsl_types.h"

#if !(defined(__WINCE) || defined(WIN32)) /* LINUX */
typedef void* HANDLE;
void M_SLEEP(int ms);

#else /* WINCE */
#define M_SLEEP(ms) Sleep(ms)
void asciiToUnicodeString(const uint8* src, WCHAR* des);

#endif

/***************************************************************************************
 *
 *          Threads
 *
 ***************************************************************************************/

typedef void* (*fsl_osal_task_func)(void* arg);
HANDLE fsl_osal_create_thread(fsl_osal_task_func func, void* arg);
int32 fsl_osal_thread_join(HANDLE handle);

/***************************************************************************************
 *
 *          Synchronization Objects
 *
 ***************************************************************************************/

typedef enum {
    fsl_osal_mutex_normal = 0,
    fsl_osal_mutex_recursive,
    fsl_osal_mutex_errorcheck,
    fsl_osal_mutex_default
} fsl_osal_mutex_type;

HANDLE fsl_osal_mutex_create(fsl_osal_mutex_type type);
int32 fsl_osal_mutex_destroy(HANDLE sync_obj);
int32 fsl_osal_mutex_lock(HANDLE sync_obj);
int32 fsl_osal_mutex_unlock(HANDLE sync_obj);

/***************************************************************************************
 *
 *          Memory
 *
 ***************************************************************************************/
void* fsl_osal_calloc(uint32 numItems, uint32 size);
void* fsl_osal_malloc(uint32 size);
void* fsl_osal_realloc(void* MemoryBlock, uint32 size);
void fsl_osal_free(void* MemoryBlock);

/***************************************************************************************
 *
 *          DLL
 *
 * For WINCE, search title "Using Run-Time Dynamic Linking" in MSDN.
 ***************************************************************************************/

HANDLE fsl_osal_dll_open(const char* filename);
int32 fsl_osal_dll_close(HANDLE);
void* fsl_osal_dll_symbol(HANDLE handle, const char* symbol);

#endif /* _FSL_MMLAYER_OSAL_H */
