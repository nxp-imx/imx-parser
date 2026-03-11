/****************************************************************************
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Copyright (c) 2005-2011 by Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 ****************************************************************************/

#include <stdlib.h>
#include <string.h>

#if !(defined(__WINCE) || defined(WIN32))
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#else
#include <windows.h>
// In case of Wince entry point is _tmain instead of main()
#endif

#include <assert.h>
#include "fsl_types.h"

#define MM_PRINT printf
#define MM_ASSERT assert

typedef int MM_BOOL;

/* thread control */
#if !(defined(__WINCE) || defined(WIN32)) /* LINUX */

typedef pthread_mutex_t m_mutex_t;
#define M_MUTEX_INIT(mutex) pthread_mutex_init((mutex), NULL)
#define M_MUTEX_DESTROY(mutex) pthread_mutex_destroy((mutex))
#define M_MUTEX_LOCK(mutex) pthread_mutex_lock((mutex))
#define M_MUTEX_UNLOCK(mutex) pthread_mutex_unlock((mutex))

#else /* WINCE or WIN32 */

typedef HANDLE m_mutex_t;
#define M_MUTEX_INIT(mutex) \
    ((*(mutex) = (m_mutex_t)CreateMutex(NULL, FALSE, NULL)) != NULL ? 0 : GetLastError())
#define M_MUTEX_DESTROY(mutex) (CloseHandle(*(mutex)) != 0 ? 0 : GetLastError())
#define M_MUTEX_LOCK(mutex) (WaitForSingleObject(*(mutex), INFINITE) == WAIT_OBJECT_0 ? 0 : -1)
#define M_MUTEX_UNLOCK(mutex) (ReleaseMutex(*(mutex)) != 0 ? 0 : GetLastError())
#endif

// configuration
#define MM_MAX_SIZE 512
#define MM_STEP_SIZE 8
#define MM_REFILL_COUNT 4

// do not change following macros
#define MM_POOL_COUNT (MM_MAX_SIZE / MM_STEP_SIZE)
#define MM_ALIGN 8
#define MM_HEADER_SIZE 8

#define MM_BLOCK_SIZE(index) (((index) + 1) * MM_STEP_SIZE)
#define MM_POOL_INDEX(size) (((size) - 1) / MM_STEP_SIZE)

#define MM_ROUND_UP(size) (((size) + (MM_ALIGN - 1)) & ~(MM_ALIGN - 1))

#define MM_MAGIC 0x75839F15
#define MM_MAGIC2 0xABCD1234
#define MM_MAGIC3 0x012D1246

#ifdef AVI_MEM_DEBUG_SELF
typedef struct mm_use_list {
    int code;
    int code2;
    struct mm_use_list* prev;
    struct mm_use_list* next;
    const char* filename;
    int line;
} mm_use_list;
#endif

typedef struct mm_mem_pool {
#ifdef AVI_MEM_DEBUG_SELF
    int block_count;  // number of blocks returned by malloc()
    int alloc_count;  // times of mmf_malloc() called
    int free_count;   // times of mmf_free() called
    int in_use;       // alloced but not freed
    int peak_in_use;  // peak value of in_use
    mm_use_list list;
#endif

    void** prev_block;  // previous block (returned by malloc())
    void** item;        // list of fixed-size blocks
} mm_mem_pool;

static mm_mem_pool mem_pool[MM_POOL_COUNT];  // memory pools
static m_mutex_t mem_mutex;

#ifdef AVI_MEM_DEBUG_SELF
static mm_use_list big_block_list;  // by malloc()
#endif

static MM_BOOL mm_refill(int index);

#ifdef AVI_MEM_DEBUG_SELF
MM_BOOL mm_mm_check_sanity(void);
static const char* last_malloc_filename;
static int last_malloc_line;
#endif

void mm_mm_init(void) {
    M_MUTEX_INIT(&mem_mutex);
    memset(&mem_pool[0], 0, sizeof(mem_pool));

#ifdef AVI_MEM_DEBUG_SELF
    big_block_list.next = &big_block_list;
    big_block_list.prev = &big_block_list;

    {
        mm_mem_pool* pool;
        int i;

        for (i = 0; i < MM_POOL_COUNT; i++) {
            pool = &mem_pool[i];
            pool->list.prev = &pool->list;
            pool->list.next = &pool->list;
        }
    }

#endif
}

void mm_mm_exit(void) {
    mm_mem_pool* pool;
    void* block;
    void* temp;
    int i;

    // check and free all memory in pools
    for (i = 0; i < MM_POOL_COUNT; i++) {
        pool = &mem_pool[i];

#ifdef AVI_MEM_DEBUG_SELF

        if (pool->alloc_count != 0 || pool->free_count != 0)
            MM_PRINT("%d: alloc = %d (%d), free = %d (%d)\n", (i + 1) * 8, pool->alloc_count,
                     pool->peak_in_use, pool->free_count, pool->in_use);

        {
            mm_use_list* node = pool->list.next;
            if (node != NULL)
                for (; node != &pool->list; node = node->next)
                    MM_PRINT("%s : %d\n", node->filename, node->line);
        }
#endif

        block = pool->prev_block;
        while (block != NULL) {
            temp = *(void**)block;
            free(block);
            block = temp;
        }
    }

#ifdef AVI_MEM_DEBUG_SELF
    // check memory managed by malloc()/free()
    {
        mm_use_list* node;

        MM_PRINT("=== big blocks ===\n");
        node = big_block_list.next;
        for (; node != &big_block_list; node = node->next)
            MM_PRINT("%s : %d\n", node->filename, node->line);
    }
#endif

    M_MUTEX_DESTROY(&mem_mutex);
}

#ifdef AVI_MEM_DEBUG_SELF
void* mm_malloc(int size, const char* filename, int line)
#else
void* mm_malloc(int size)
#endif
{
    int index;
    void* ptr;
    mm_mem_pool* pool;

    size = MM_ROUND_UP(size);

    // if the block is too big, call malloc() directly
    if (size > MM_MAX_SIZE) {
        size += MM_HEADER_SIZE;

#ifdef AVI_MEM_DEBUG_SELF
        size += sizeof(mm_use_list);
#endif

        ptr = malloc(size);
        if (ptr == NULL)
            return NULL;

#ifdef AVI_MEM_DEBUG_SELF
        {
            mm_use_list* node;

            mm_mm_check_sanity();

            node = (mm_use_list*)((char*)ptr + size - sizeof(mm_use_list));

            M_MUTEX_LOCK(&mem_mutex);
            last_malloc_filename = filename;
            last_malloc_line = line;
            node->next = big_block_list.next;
            big_block_list.next->prev = node;
            node->prev = &big_block_list;
            big_block_list.next = node;
            M_MUTEX_UNLOCK(&mem_mutex);

            node->code = MM_MAGIC;
            node->code2 = MM_MAGIC2;
            node->filename = filename;
            node->line = line;
        }
#endif

        *(int*)ptr = size;
#ifdef AVI_MEM_DEBUG_SELF
        *((int*)ptr + 1) = MM_MAGIC3;
#endif
        return (char*)ptr + MM_HEADER_SIZE;
    }

    // find which pool the memory should be allocated
    index = MM_POOL_INDEX(size);
    MM_ASSERT(index >= 0 && index < MM_POOL_COUNT);

    M_MUTEX_LOCK(&mem_mutex);

    pool = &mem_pool[index];
    if (pool->item == NULL)
        if (!mm_refill(index)) {
            M_MUTEX_UNLOCK(&mem_mutex);
            return NULL;
        }

    // get the first block in pool
    ptr = pool->item;
    pool->item = *pool->item;

#ifdef AVI_MEM_DEBUG_SELF
    pool->alloc_count++;
    pool->in_use++;
    if (pool->peak_in_use < pool->in_use)
        pool->peak_in_use = pool->in_use;

    // save the block in list
    {
        mm_use_list* node;

        last_malloc_filename = filename;
        last_malloc_line = line;

        mm_mm_check_sanity();

        node = (mm_use_list*)((char*)ptr + MM_HEADER_SIZE + MM_BLOCK_SIZE(index));
        node->next = pool->list.next;
        pool->list.next->prev = node;
        node->prev = &pool->list;
        pool->list.next = node;

        node->code = MM_MAGIC;
        node->code2 = MM_MAGIC2;
        node->filename = filename;
        node->line = line;
    }
#endif

    M_MUTEX_UNLOCK(&mem_mutex);

    *(int*)ptr = index;
#ifdef AVI_MEM_DEBUG_SELF
    *((int*)ptr + 1) = MM_MAGIC3;
#endif
    return (char*)ptr + MM_HEADER_SIZE;
}

#ifdef AVI_MEM_DEBUG_SELF
void* mm_calloc(int nobj, int size, const char* filename, int line)
#else
void* mm_calloc(int nobj, int size)
#endif
{
    void* ptr;
    int total_size = nobj * size;
#ifdef AVI_MEM_DEBUG_SELF
    ptr = mm_malloc(total_size, filename, line);
#else
    ptr = mm_malloc(total_size);
#endif

    if (NULL != ptr) {
        memset(ptr, 0, size);
    }

    return ptr;
}

#ifdef AVI_MEM_DEBUG_SELF
void mm_free(void* ptr, const char* filename, int line)
#else
void mm_free(void* ptr)
#endif
{
    int index;
    mm_mem_pool* pool;

    if (ptr == NULL)
        return;

    ptr = (void*)((char*)ptr - MM_HEADER_SIZE);
    index = *(int*)ptr;

#ifdef AVI_MEM_DEBUG_SELF
    if (*((int*)ptr + 1) != MM_MAGIC3) {
        MM_PRINT("bad free 1: %p\n", ptr);
        return;
    }
    *((int*)ptr + 1) = MM_MAGIC3 - 1; /* a quick flag for a just freed memory block */
#endif

    if (index > MM_MAX_SIZE) {
#ifdef AVI_MEM_DEBUG_SELF
        {
            mm_use_list* node;
            mm_use_list* tmp;

            M_MUTEX_LOCK(&mem_mutex);

            mm_mm_check_sanity();

            node = (mm_use_list*)((char*)ptr + index - sizeof(mm_use_list));

            // check sanity
            if (node->code != MM_MAGIC || node->code2 != MM_MAGIC2) {
                MM_PRINT("the memory is defected: %p\n", ptr);
            } else {
                // search in the block list
                for (tmp = big_block_list.next; tmp != &big_block_list; tmp = tmp->next)
                    if (tmp == node)
                        break;

                if (tmp == &big_block_list) {
                    // not found
                    MM_PRINT("bad free 2\n");
                } else {
                    // remove from our list
                    node->next->prev = node->prev;
                    node->prev->next = node->next;
                }
            }

            M_MUTEX_UNLOCK(&mem_mutex);
        }
#endif

        free(ptr);
        return;
    }

    MM_ASSERT(index >= 0 && index < MM_POOL_COUNT);

    M_MUTEX_LOCK(&mem_mutex);

    // return the buffer to its pool
    pool = &mem_pool[index];
    *(void**)ptr = pool->item;
    pool->item = ptr;

#ifdef AVI_MEM_DEBUG_SELF
    pool->free_count++;
    pool->in_use--;

    // remove the block from list
    {
        mm_use_list* node;
        mm_use_list* tmp;

        mm_mm_check_sanity();

        node = (mm_use_list*)((char*)ptr + MM_HEADER_SIZE + MM_BLOCK_SIZE(index));

        // check sanity
        if (node->code != MM_MAGIC || node->code2 != MM_MAGIC2) {
            MM_PRINT("the memory is defected: %p\n", ptr);
        } else {
            // search in the list
            for (tmp = pool->list.next; tmp != &pool->list; tmp = tmp->next)
                if (tmp == node)
                    break;

            if (tmp == &pool->list) {
                // not found
                MM_PRINT("bad free 3\n");
            } else {
                // remove from the list
                node->next->prev = node->prev;
                node->prev->next = node->next;
            }
        }
    }

#endif

    M_MUTEX_UNLOCK(&mem_mutex);
}

/*
DESCRIPTION
The realloc() function shall change the size of the memory object pointed to by ptr
to the size specified by size. The contents of the object shall remain unchanged
up to the lesser of the new and old sizes.
If the new size of the memory object would require movement of the object,
the space for the previous instantiation of the object is freed.
If the new size is larger, the contents of the newly allocated portion of the object
are unspecified.
If size is 0 and ptr is not a null pointer, the object pointed to is freed.
If the space cannot be allocated, the object shall remain unchanged.

If ptr is a null pointer, realloc() shall be equivalent to malloc() for the specified size.

If ptr does not match a pointer returned earlier by calloc(), malloc(), or realloc()
or if the space has previously been deallocated by a call to free() or realloc(),
the behavior is undefined.

The order and contiguity of storage allocated by successive calls to realloc()
is unspecified.
The pointer returned if the allocation succeeds shall be suitably aligned
so that it may be assigned to a pointer to any type of object
and then used to access such an object in the space allocated
(until the space is explicitly freed or reallocated).
Each such allocation shall yield a pointer to an object disjoint from any other object.
The pointer returned shall point to the start (lowest byte address) of the allocated space.
If the space cannot be allocated, a null pointer shall be returned.

RETURN VALUE
Upon successful completion with a size not equal to 0,
realloc() shall return a pointer to the (possibly moved) allocated space.
If size is 0, either a null pointer
or a unique pointer that can be successfully passed to free() shall be returned.

If there is not enough available memory, realloc() shall return a null pointer
and set errno to [ENOMEM].
*/
#ifdef AVI_MEM_DEBUG_SELF
void* mm_realloc(void* ptr, int size, const char* filename, int line)
#else
void* mm_realloc(void* ptr, int size)
#endif
{
    void* new_ptr = NULL;
    void* inter_ptr;
    int index;
    int ori_size_roundup;

    if (NULL == ptr) {
#ifdef AVI_MEM_DEBUG_SELF
        new_ptr = mm_malloc(size, filename, line);
#else
        new_ptr = mm_malloc(size);
#endif
        return new_ptr;
    }

    if ((0 == size) && (NULL != ptr)) {
        goto FREE_OLD_MEMBLK;
    }

    /* check the ptr and original size */
    inter_ptr = (void*)((char*)ptr - MM_HEADER_SIZE);
    index = *(int*)inter_ptr;

#ifdef AVI_MEM_DEBUG_SELF
    if (*((int*)inter_ptr + 1) != MM_MAGIC3) {
        MM_PRINT("bad realloc ptr: %p\n", ptr);
        return NULL;
    }
#endif

    if (index > MM_MAX_SIZE) {
        ori_size_roundup = index - MM_HEADER_SIZE;
#ifdef AVI_MEM_DEBUG_SELF
        ori_size_roundup -= sizeof(mm_use_list);
#endif
    } else
        ori_size_roundup = MM_BLOCK_SIZE(index);

    /* original memory remains unchanged if new size is no larger */
    if (ori_size_roundup >= size) {
        return ptr;
    }

/* new size must be larger */
#ifdef AVI_MEM_DEBUG_SELF
    new_ptr = mm_malloc(size, filename, line);
#else
    new_ptr = mm_malloc(size);
#endif

    if (NULL == new_ptr) {
        MM_PRINT("fail to realloc new memory, size %d\n", size);
        return NULL; /* original memory remains unchaged */
    } else {         /* content movement */
        memcpy(new_ptr, ptr, ori_size_roundup);
    }

FREE_OLD_MEMBLK:
#ifdef AVI_MEM_DEBUG_SELF
    mm_free(ptr, filename, line);
#else
    mm_free(ptr);
#endif

    return new_ptr;
}

static MM_BOOL mm_refill(int index) {
    mm_mem_pool* pool;
    void* block;
    void* next;
    int size;
    int i;

    size = MM_HEADER_SIZE + MM_BLOCK_SIZE(index);

#ifdef AVI_MEM_DEBUG_SELF
    size += sizeof(mm_use_list);
#endif

    block = malloc(MM_HEADER_SIZE + size * MM_REFILL_COUNT);
    if (block == NULL)
        return FALSE;

    pool = &mem_pool[index];
    *(void**)block = pool->prev_block;
    pool->prev_block = block;

    block = (void*)((char*)block + MM_HEADER_SIZE);
    pool->item = block;
    for (i = 0; i < MM_REFILL_COUNT - 1; i++) {
        next = (char*)block + size;
        *(void**)block = next;
        block = next;
    }
    *(void**)block = NULL;

#ifdef AVI_MEM_DEBUG_SELF
    pool->block_count++;
#endif

    return TRUE;
}

#ifdef AVI_MEM_DEBUG_SELF
MM_BOOL mm_mm_check_sanity(void) {
    mm_mem_pool* pool;
    mm_use_list* node;
    int i;

    for (i = 0; i < MM_POOL_COUNT; i++) {
        pool = &mem_pool[i];
        node = pool->list.next;
        for (; node != &pool->list; node = node->next)
            if (node->code != MM_MAGIC || node->code2 != MM_MAGIC2) {
                MM_PRINT("list corrupted.\n");
                return FALSE;
            }
    }

    node = big_block_list.next;
    for (; node != &big_block_list; node = node->next)
        if (node->code != MM_MAGIC || node->code2 != MM_MAGIC2) {
            MM_PRINT("big block list corrupted.\n");
            return FALSE;
        }

    return TRUE;
}

#endif
