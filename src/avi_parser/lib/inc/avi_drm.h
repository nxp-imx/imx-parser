/*
***********************************************************************
* Copyright (c) 2012, Freescale Semiconductor, Inc.
* Copyright 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#ifndef __AVI_DRM_H__
#define __AVI_DRM_H__

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

#include "fsl_types.h"
///
#if !(defined(__WINCE) || defined(WIN32)) /* LINUX */
typedef void* HANDLE;
#endif

bool LoadDrmLibrary(AviObjectPtr pAviObj);

bool UnloadDrmLibrary(AviObjectPtr pAviObj);

#endif  // __AVI_DRM_H__