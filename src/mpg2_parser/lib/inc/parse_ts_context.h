/*
 ***********************************************************************
 * Copyright (c) 2005-2013, Freescale Semiconductor, Inc.
 *
 * Copyright 2020, 2025-2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */
/*****************************************************************************
 * demux_ts_context.h
 *
 * Description:
 * Define the context structure for TS demuxer.
 *
 *
 ****************************************************************************/
#ifndef FSL_MPG_DEMUX_TS_CONTEXT_H
#define FSL_MPG_DEMUX_TS_CONTEXT_H

#include "mpeg2_epson_exvi.h"
#include "mpg_demuxer_api.h"
#include "parse_cfg.h"

typedef struct FSL_MPG_DEMUX_PATSECTION_S {
    U16 SectionLen;
    U16 TSID;
    U8 VersionNum;
    U8 CurrentOrNext;
    U8 SectionNum;
    U8 LastSectionNum;
    U32 Programs;
    U16 ProgramNum[NO_PROGRAM_SUPPORT_MAX];
    U16 PID_PMT[NO_PROGRAM_SUPPORT_MAX];

} FSL_MPG_DEMUX_PATSECTION_T;

typedef struct FSL_MPG_DEMUX_PAT_S {
    U8 Sections;
    U8 Parsed;
    U8 NonProgramSelected;
    FSL_MPG_DEMUX_PATSECTION_T PATSection[NO_PATSECTION_SUPPORT_MAX];
} FSL_MPG_DEMUX_PAT_T;

typedef struct MPG_AUDIO_PRESENTATION_S {
    S32 presentationId;
    char language[4];
    U32 masteringIndication;
    bool audioDescriptionAvailable;
    bool spokenSubtitlesAvailable;
    bool dialogueEnhancementAvailable;
} MPG_AUDIO_PRESENTATION_T;

/*context for PMT   */
typedef struct FSL_MPG_DEMUX_PMTSECTION_S {
    U16 SectionLen;
    U16 ProgramNum;
    U8 VersionNum;
    U8 CurrentOrNext;
    U8 SectionNum;
    U8 LastSectionNum;
    U16 PCR_PID;
    U16 Streams;
    U8 StreamType[NO_STREAMSINPMT_SUPPORT_MAX];
    U16 StreamPID[NO_STREAMSINPMT_SUPPORT_MAX];
    U8 Language[NO_STREAMSINPMT_SUPPORT_MAX][LANGUAGE_MAX_LEN];
    U8 StreamDescriptorTag[NO_STREAMSINPMT_SUPPORT_MAX];  // not save all the descriptor tags, only
                                                          // save the tag indicates the stream type.
    MPG_AUDIO_PRESENTATION_T AudioPresentation[NO_STREAMSINPMT_SUPPORT_MAX][32];
    U32 AudioPresentationNum[NO_STREAMSINPMT_SUPPORT_MAX];
    U32 VideoFrameRate[NO_STREAMSINPMT_SUPPORT_MAX]
                      [2];  // VideoFrameRate[0]: numerator, VideoFrameRate[1]: denominator
    bool bHDCPEncrypted;

} FSL_MPG_DEMUX_PMTSECTION_T;

typedef struct FSL_MPG_DEMUX_PMT_S {
    U8 Sections;
    FSL_MPG_DEMUX_PMTSECTION_T PMTSection[NO_PMTSECTION_SUPPORT_MAX];
    U32 Parsed;
    U32 PID;
    U32 SupportedStreamNum;  // supported stream in TS layer
    U32 ValidTrackNum;       // valid track in PES layer, will export to user, <= SupportedStreamNum
    U32 adwValidTrackIdx[NO_STREAMSINPMT_SUPPORT_MAX];
    U32 adwValidTrackPID[NO_STREAMSINPMT_SUPPORT_MAX];
} FSL_MPG_DEMUX_PMT_T;

typedef struct FSL_MPG_DEMUX_TS_PACKET_CNXT_S {
    U16 PID;
    U8 CC; /*continuity counter   */

} FSL_MPG_DEMUX_TS_PACKET_CNXT_T;

typedef struct FSL_MPG_DEMUX_TS_PACKET_S {
    U32 ValidEntries;
    FSL_MPG_DEMUX_TS_PACKET_CNXT_T TSPackets[TS_PACKET_SUPPORT_MAX];

} FSL_MPG_DEMUX_TS_PACKET_T;

typedef struct FSL_MPG_DEMUX_TS_BUFFER_S {
    U8* pBuf;
    U32 Size;
    U32 Filled;
    U32 PESLen;
    U32 Complete;
    U32 PID;
    U32 StreamNum;
    U32 IndexInputBuf;
    U32 OffsetInputBuf;
    U32 lastPESOffset;
    U32 newSegFlag;
    EPSON_EXVI EXVI;
    U64 qwOffset;  // offset of the ts sync word
} FSL_MPG_DEMUX_TS_BUFFER_T;

typedef struct FSL_MPG_DEMUX_TS_BUFFERS_S {
    FSL_MPG_DEMUX_TS_BUFFER_T TSTempBuf;
    FSL_MPG_DEMUX_TS_BUFFER_T PATSectionBuf;
    FSL_MPG_DEMUX_TS_BUFFER_T PMTSectionBuf[NO_PROGRAM_SUPPORT_MAX];
    FSL_MPG_DEMUX_TS_BUFFER_T PESStreamBuf[MAX_MPEG2_STREAMS];

} FSL_MPG_DEMUX_TS_BUFFERS_T;

typedef struct FSL_MPG_DEMUX_TS_PIDS_S {
    U16 Valid[TS_PACKET_SUPPORT_MAX];
    U16 TS_PID[TS_PACKET_SUPPORT_MAX];

} FSL_MPG_DEMUX_TS_PIDS_T;

typedef enum {
    FSL_MPG_DEMUX_TS_PROBE_INIT = 0,
    FSL_MPG_DEMUX_TS_PROBE_PMT,
    FSL_MPG_DEMUX_TS_PROBE_SYSINFO,
    FSL_MPG_DEMUX_TS_PROBE_MAX
} FSL_MPG_DEMUX_TS_PROBE_STATE_T;

typedef enum {
    FSL_MPG_DEMUX_TS_PROCESS_INIT = 0,
    FSL_MPG_DEMUX_TS_PROCESS_GETPIDLIST,
    FSL_MPG_DEMUX_TS_PROCESS_PARSE,
    FSL_MPG_DEMUX_TS_PROCESS_MAX
} FSL_MPG_DEMUX_TS_PROCESS_STATE_T;

typedef enum {
    FSL_MPG_DEMUX_TS_SCAN_INIT = 0,
    FSL_MPG_DEMUX_TS_SCAN_GETPIDLIST,
    FSL_MPG_DEMUX_TS_SCAN_PARSE,
    FSL_MPG_DEMUX_TS_SCAN_MAX
} FSL_MPG_DEMUX_TS_SCAN_STATE_T;

typedef struct FSL_MPG_DEMUX_TS_STREAMS_S {
    U32 SupportedStreams;
    U32 StreamPID[MAX_MPEG2_STREAMS];
    U32 ReorderedStreamPID[MAX_MPEG2_STREAMS];
    U32 StreamType[MAX_MPEG2_STREAMS];
    U32 StreamEnable[MAX_MPEG2_STREAMS];

} FSL_MPG_DEMUX_TS_STREAMS_T;

typedef struct FSL_MPG_DEMUX_TS_PCR_INFO_S {
    U64 PCR;
    U32 PID;
    U32 programNum;
} FSL_MPG_DEMUX_TS_PCR_INFO_T;

typedef struct FSL_MPG_DEMUX_TS_CNXT_S {
    U32 Synced;
    bool hasTimeCode;  // offsets between packets
    U32 nPMTs;         // number of Program Map Tables
    U32 nParsedPMTs;
    FSL_MPG_DEMUX_PAT_T PAT;
    FSL_MPG_DEMUX_PMT_T PMT[NO_PROGRAM_SUPPORT_MAX];
    FSL_MPG_DEMUX_TS_PACKET_T Packets;
    FSL_MPG_DEMUX_TS_BUFFERS_T TempBufs;
    FSL_MPG_DEMUX_TS_PIDS_T ValidPIDs;
    FSL_MPG_DEMUX_TS_PROBE_STATE_T TSProbeState;
    FSL_MPG_DEMUX_TS_PROCESS_STATE_T TSProcessState;
    FSL_MPG_DEMUX_TS_SCAN_STATE_T TSScanState;
    FSL_MPG_DEMUX_TS_STREAMS_T Streams;
    U8* pbyProInfoMenu;
    PMTInfoList* pPMTInfoList;
    FSL_MPG_DEMUX_TS_PCR_INFO_T PCRInfo[NO_PROGRAM_SUPPORT_MAX];
    bool bOutputPCR;  // TRUE for parsing PCR field
    bool bGetPCR;     // TRUE for finding PCR info
} FSL_MPG_DEMUX_TS_CNXT_T;

#endif /*FSL_MPG_DEMUX_TS_CONTEXT_H  */
