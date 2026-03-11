/*
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 */

void mm_mm_init(void);
void mm_mm_exit(void);

#ifdef FSL_MEM_DEBUG_SELF
#define MM_Malloc(size) mm_malloc((size), __FILE__, __LINE__)
#define MM_Calloc(nobj, size) mm_calloc((nobj), (size), __FILE__, __LINE__)
#define MM_ReAlloc(ptr, size) mm_realloc((ptr), (size), __FILE__, __LINE__)
#define MM_Free(ptr) mm_free((ptr), __FILE__, __LINE__)

void* mm_malloc(int size, const char* filename, int line);
void* mm_calloc(int nobj, int size, const char* filename, int line);
void* mm_realloc(void* ptr, int size, const char* filename, int line);
void mm_free(void* ptr, const char* filename, int line);

#else
#define MM_Malloc mm_malloc
#define MM_Calloc mm_calloc
#define MM_ReAlloc mm_realloc
#define MM_Free mm_free

void* mm_malloc(int size);
void* mm_calloc(int nobj, int size);
void* mm_realloc(void* ptr, int size);
void mm_free(void* ptr);

#endif
