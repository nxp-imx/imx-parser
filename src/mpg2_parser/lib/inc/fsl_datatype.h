/*****************************************************************************
 * fsl_datatype.h
 *
 * Copyright (c) 2008-2012, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 *
 *
 * Description:
 * Define the common types.
 *
 *
 ****************************************************************************/

#ifndef FSL_DATATYPE_H
#define FSL_DATATYPE_H

typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int U32;  // typedef unsigned long      U32;
typedef char S8;
typedef short S16;
typedef int S32;  // typedef long               S32;
typedef void FSL_VOID;

#ifdef _LINUX
typedef unsigned long long U64;
typedef long long S64;
#define INLINE inline

#else
typedef unsigned __int64 U64;
typedef __int64 S64;
#define INLINE __inline

#endif

#endif /* FSL_DATATYPE_H  */
