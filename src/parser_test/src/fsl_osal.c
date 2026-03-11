/*
***********************************************************************
* Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

/*=============================================================================

  Module Name:  fsl_osal.c

  General Description:  Simple OSAL layer implementation.
  - Memory allocate/free.
  - Synchronization object
  - Thread


  ===============================================================================
  INCLUDE FILES
  =============================================================================*/

#include "fsl_osal.h"
#include "memory_mgr.h"

/* include following files only for error check */
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "fsl_parser_drm.h"
#include "aac.h"
#include "fsl_parser_test.h"
#include "err_check.h"

// #define USE_LIB_MALLOC

#if !(defined(__WINCE) || defined(WIN32)) /* Linux */

void M_SLEEP(int ms) {
    struct timespec ts;
    if ((ms) > 0) {
        ts.tv_sec = (ms) / 1000;
        ts.tv_nsec = ((ms)-1000 * ts.tv_sec) * 1000000;
        while (nanosleep(&ts, &ts) &&
               errno == EINTR); /* continue sleeping when interrupted by signal */
    }
    return;
}
#endif

#if defined(__WINCE) || defined(WIN32)
void asciiToUnicodeString(const uint8* src, WCHAR* des) {
    uint32 size;
    uint32 i;

    size = strlen(src);

    for (i = 0; i < size; i++) {
        des[i] = (uint32)src[i];
    }
    des[i] = 0;
}
#endif

/*****************************************************************************
 * Function:    Calloc
 *
 * Description: Implements the local calloc
 *
 * Return:      pointer to the memory block.
 *
 ****************************************************************************/
void* fsl_osal_calloc(uint32 numItems, uint32 size) {
    /* Void Pointer. */
    void* PtrCalloc = NULL;
    uint32 totalSize = numItems * size;

    if ((0 == totalSize) || (MAX_MEMORY_BLOCK_SIZE < totalSize)) {
        PARSER_WARNNING("\nWarning: invalid size IN LOCAL CALLOC: %d\n", totalSize);
        logInvalidMalloc(totalSize);
    }

/* Allocate the memory. */
#ifdef USE_LIB_MALLOC
    PtrCalloc = calloc(numItems, size);
#else
    PtrCalloc = MM_Calloc(numItems, size);
#endif

    if (PtrCalloc == NULL) {
        PARSER_ERROR(
                "\nError: MEMORY FAILURE IN LOCAL CALLOC. number of items %ld, atom size %ld\n",
                numItems, size);
    }

    return (PtrCalloc);
}

/*****************************************************************************
 * Function:    Malloc
 *
 * Description: Implements the local malloc
 *
 * Return:      pointer to the memory block.
 *
 ****************************************************************************/

void* fsl_osal_malloc(uint32 size) {
    /* Void pointer to the malloc. */
    void* PtrMalloc = NULL;

    if ((0 == size) || (MAX_MEMORY_BLOCK_SIZE < size)) {
        PARSER_WARNNING("\nWarning: ZERO size IN LOCAL MALLOC: %ld\n", size);
        logInvalidMalloc(size);
    }

/* Allocate the mempry. */
#ifdef USE_LIB_MALLOC
    PtrMalloc = malloc(size);
#else
    PtrMalloc = MM_Malloc(size);
#endif

    if (PtrMalloc == NULL) {
        PARSER_ERROR("\nError: MEMORY FAILURE IN LOCAL MALLOC, size %ld\n", size);
    }
    return (PtrMalloc);
}

/*****************************************************************************
 * Function:    Free
 *
 * Description: Implements to Free the memory.
 *
 * Return:      Void
 *
 ****************************************************************************/
void fsl_osal_free(void* MemoryBlock) {
    if (MemoryBlock) {
#ifdef USE_LIB_MALLOC
        free(MemoryBlock);
#else
        MM_Free(MemoryBlock);
#endif
    }
}

/*****************************************************************************
 * Function:    ReAlloc
 *
 * Description: Implements to Free the memory.
 *
 * Return:      Void
 *
 ****************************************************************************/
void* fsl_osal_realloc(void* MemoryBlock, uint32 size) {
    void* PtrMalloc = NULL;

    if ((0 == size) || (MAX_MEMORY_BLOCK_SIZE < size)) {
        PARSER_WARNNING("\nWarning: ZERO size IN LOCAL REALLOC: %ld\n", size);
        logInvalidMalloc(size);
    }

/* Allocate the mempry. */
#ifdef USE_LIB_MALLOC
    PtrMalloc = realloc(MemoryBlock, size);
#else
    PtrMalloc = MM_ReAlloc(MemoryBlock, size);
#endif

    if (PtrMalloc == NULL) {
        PARSER_ERROR("\nError: MEMORY FAILURE IN LOCAL REALLOC, size %ld\n", size);
    }

    return PtrMalloc;
}

HANDLE fsl_osal_dll_open(const char* filename) {
#if defined(__WINCE) || defined(WIN32)
    WCHAR wstrFileName[255];
    HMODULE handle = NULL;

    asciiToUnicodeString(filename, wstrFileName);
    handle = LoadLibrary(wstrFileName); /* TEXT("MyPuts.dll") need unicode string */

#else
    void* handle = NULL;
    handle = dlopen(filename, RTLD_NOW);
    if (NULL == handle) {
        PARSER_ERROR("%s\n", dlerror());
    }
#endif

    return (HANDLE)handle;
}

/* returns 0 on success, and non-zero on error */
int32 fsl_osal_dll_close(HANDLE handle) {
    int32 ret;

#if defined(__WINCE) || defined(WIN32)
    bool err;

    err = FreeLibrary((HMODULE)handle);
    if (err)
        ret = 0; /* WINCE use TRUE on success */
    else
        ret = -1;

#else
    ret = dlclose((void*)handle);
#endif
    return ret;
}

void* fsl_osal_dll_symbol(HANDLE handle, const char* symbol) {
    void* addr = NULL;

#if defined(__WINCE)
    WCHAR wstrSymbol[255];
    asciiToUnicodeString(symbol, wstrSymbol);
    addr = (void*)GetProcAddress((HMODULE)handle, wstrSymbol);

#elif defined(WIN32)
    addr = (void*)GetProcAddress((HMODULE)handle, symbol);

#else
    addr = dlsym((void*)handle, symbol);
#endif

    return addr;
}

HANDLE fsl_osal_mutex_create(fsl_osal_mutex_type type) {
#if defined(__WINCE) || defined(WIN32)
    HANDLE sync_obj;
    sync_obj = CreateMutex(NULL, FALSE, NULL);

#else

    pthread_mutexattr_t attr;
    pthread_mutex_t* sync_obj = NULL;

    sync_obj = (pthread_mutex_t*)fsl_osal_malloc(sizeof(pthread_mutex_t));
    if (NULL == sync_obj) {
        PARSER_ERROR("\n malloc of mutex object failed.");
        return NULL;
    }

    pthread_mutexattr_init(&attr);
    switch (type) {
        case fsl_osal_mutex_normal:
#ifdef ANDROID
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
#else
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_TIMED_NP);
#endif
            break;
        case fsl_osal_mutex_recursive:
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
            break;
        case fsl_osal_mutex_errorcheck:
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK_NP);
            break;
        default:
#ifdef ANDROID
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_NORMAL);
#else
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_TIMED_NP);
#endif
            break;
    }

    if (pthread_mutex_init(sync_obj, &attr)) {
        PARSER_ERROR("\n fail to init mutex by pthread");
        fsl_osal_free(sync_obj);
        sync_obj = NULL;
    }

#endif

    return (HANDLE)sync_obj;
}

int32 fsl_osal_mutex_destroy(HANDLE sync_obj) {
    int32 err;

#if defined(__WINCE) || defined(WIN32)
    err = CloseHandle(sync_obj) ? 0 : GetLastError();

#else
    err = pthread_mutex_destroy((pthread_mutex_t*)sync_obj);
    if (err) {
        PARSER_ERROR("\n Error in destroying mutex by pthread.");
    }

    fsl_osal_free(sync_obj);
#endif

    return err;
}

int32 fsl_osal_mutex_lock(HANDLE sync_obj) {
    int32 err;

#if defined(__WINCE) || defined(WIN32)

    err = (WaitForSingleObject(sync_obj, INFINITE) == WAIT_OBJECT_0) ? 0 : -1;

#else
    err = pthread_mutex_lock((pthread_mutex_t*)sync_obj);
    if (err) {
        PARSER_ERROR("\n Error in locking the pthread mutex");
    }
#endif

    return err;
}

int32 fsl_osal_mutex_unlock(HANDLE sync_obj) {
    int32 err;

#if defined(__WINCE) || defined(WIN32)

    err = ReleaseMutex(sync_obj) ? 0 : GetLastError();

#else

    err = pthread_mutex_unlock((pthread_mutex_t*)sync_obj);
    if (err) {
        PARSER_ERROR("\n Error while trying to unlock the pthread mutex.");
    }

#endif

    return err;
}

HANDLE fsl_osal_create_thread(fsl_osal_task_func func, void* arg) {
#if defined(__WINCE) || defined(WIN32)
    HANDLE handle;
    handle = CreateThread(NULL, 8 * 1024, (LPTHREAD_START_ROUTINE)(func), (LPVOID)(arg), 0, NULL);
#else
    pthread_t* handle;

    handle = (pthread_t*)fsl_osal_malloc(sizeof(pthread_t));
    if (NULL == handle) {
        PARSER_ERROR("\n malloc of pthread object failed.");
        return NULL;
    }

    if (pthread_create(handle, NULL, (void* (*)(void*))(func), (void*)(arg))) {
        PARSER_ERROR("\n fail to create pthread.");
        fsl_osal_free(handle);
        handle = NULL;
    }

#endif
    return (HANDLE)handle;
}

/* wait for thread termination */
int32 fsl_osal_thread_join(HANDLE handle) {
    int32 err;

#if defined(__WINCE) || defined(WIN32)
    err = (WaitForSingleObject(handle, INFINITE) == WAIT_OBJECT_0) ? 0 : -1;
    if (err)
        PARSER_ERROR("\n fail to join WINCE thread.");

    err = CloseHandle(handle) ? 0 : GetLastError();
    if (err)
        PARSER_ERROR("\n fail to close WINCE thread handle.");

#else
    pthread_t thread_obj;

    memcpy(&thread_obj, (pthread_t*)handle, sizeof(pthread_t));
    err = pthread_join(thread_obj, NULL);
    if (err)
        PARSER_ERROR("\n fail to join pthread.");

    fsl_osal_free(handle);

#endif

    return err;
}
