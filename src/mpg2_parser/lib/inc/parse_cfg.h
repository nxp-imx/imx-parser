/*
 ***********************************************************************
 * Copyright 2005-2011, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */
/*****************************************************************************
 * demux_cfg.h
 * Description:
 * Configure file for MPG demuxer.
 *
 ****************************************************************************/
#ifndef FSL_MPG_DEMUX_CFG_H
#define FSL_MPG_DEMUX_CFG_H

// #define DEMUX_DEBUG

// #define MAX_SEQHDRBUF_SIZE  160

#define MAX_SEQHDRBUF_SIZE 512

#define NO_PATSECTION_SUPPORT_MAX 4
#define NO_PMTSECTION_SUPPORT_MAX 1

#define NO_STREAMSINPMT_SUPPORT_MAX 32

#define TS_PACKET_SUPPORT_MAX 64

// The temp buffer size used in TS demux
#define SIZEOF_TSTEMPBUF 188
#define SIZEOF_PATSECTIONBUF 1024
#define SIZEOF_PMTSECTIONBUF 1024
#define SIZEOF_PESVIDEOBUF 0xA00000
#define SIZEOF_PESAUDIOBUF 0x10000 + 0x10000

#endif /*FSL_MPG_DEMUX_CFG_H */
