/*****************************************************************************
 * demux_ps_context.h
 *
 * Copyright (c) 2008-2012, Freescale Semiconductor, Inc.
 * Copyright 2020, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Description:
 * Define the context structure for PS demuxer.
 *
 *
 ****************************************************************************/
#ifndef FSL_MPG_DEMUX_CONTEXT_H
#define FSL_MPG_DEMUX_CONTEXT_H

#include "mpg_demuxer_api.h"
#include "parse_ts_context.h"

typedef struct FSL_MPG_DEMUX_CNXT_INPUT_S {
    U32 ReadBufIndex;
    U32 ReadBufOffset;
    U32 Last4Bytes;

} FSL_MPG_DEMUX_CNXT_INPUT_T;

typedef enum {
    FSL_MPG_DEMUX_PROBE_INI = 0,
    FSL_MPG_DEMUX_PROBE_PACK_HEADER,

    FSL_MPG_DEMUX_PROBE_PSM,
    FSL_MPG_DEMUX_PROBE_SYSINFO,
    FSL_MPG_DEMUX_PROBE_SUCCESS,
    FSL_MPG_DEMUX_PROBE_END

} FSL_MPG_DEMUX_PROBE_STATUS_T;

typedef struct FSL_MPG_DEMUX_PROBE_CNXT_S {
    FSL_MPG_DEMUX_PROBE_STATUS_T ProbeStage;
    U32 HavePSM;          /*PSM found in targeted PS  */
    U32 Probed;           /*the system information has been parsed   */
    U32 HaveSystemHeader; /*system header found in stream   */

} FSL_MPG_DEMUX_PROBE_CNXT_T;

typedef struct FSL_MPG_DEMUX_MEDIA_S {
    U8 MediaEnable[MAX_MPEG2_STREAMS]; /*set to 0xFF to select the program */
    U32 StreamID[MAX_MPEG2_STREAMS];   /*stream id   */
    U8 SysInfoFound[MAX_MPEG2_STREAMS];
    U8 NoMediaFound;
    U8 AudioInfoFound;
    U8 VideoInfoFound;
    U32 MaxAudioPESSizeFound;
    U32 MaxVideoPESSizeFound;

} FSL_MPG_DEMUX_MEDIA_T;

typedef struct FSL_MPG_DEMUX_PROCESS_CNXT_S {
    U32 Temp;
    U32 MaxVBufSize;
    U32 MaxABufSize;
    U32 SkipEnable;

} FSL_MPG_DEMUX_PROCESS_CNXT_T;

/*Information in system header */
typedef struct FSL_MPG_DEMUX_SYSHDR_S {
    U32 Found;
    U32 RateBound;
    U32 AudioBound;
    U32 VideoBound;
    U8 StreamID[MAX_MPEG2_STREAMS];

} FSL_MPG_DEMUX_SYSHDR_T;

/*sequence buffer, max size is 240, extension and user data not inculded   */
typedef struct FSL_MPG_DEMUX_CNXT_SH_S {
    U8* pSH;
    U32 BufLen; /*total buffer length   */
    U32 Size;   /*valid data size   */
} FSL_MPG_DEMUX_CNXT_SH_T;

typedef struct FSL_MPG_DEMUX_CNXT_S {
    FSL_MPG_DEMUX_CNXT_INPUT_T InputBuf;
    FSL_MPG_DEMUX_PROBE_CNXT_T ProbeCnxt;
    FSL_MPG_DEMUX_MEDIA_T Media;
    FSL_MPG_DEMUX_SYSHDR_T SystemHeader;
    FSL_MPG_DEMUX_PROCESS_CNXT_T ProcessCnxt;
    FSL_MPG_DEMUX_TS_CNXT_T TSCnxt;
    FSL_MPG_DEMUX_CNXT_SH_T SeqHdrBuf;

    bool m_bSPSFound[MAX_MPEG2_STREAMS];
    U8 m_abySPSBuf[MAX_MPEG2_STREAMS][MAX_SEQHDRBUF_SIZE];
    U32 m_dwSPSLen[MAX_MPEG2_STREAMS];
    bool dataEncrypted;

} FSL_MPG_DEMUX_CNXT_T;

/*Information contained in video sequence header    */
typedef struct FSL_MPG_DEMUX_VSHeader_S {
    U16 HSize;
    U16 VSize;
    U8 AR;              /*Aspect Ratio  */
    U8 FRCode;          /*Frame Rate code */
    U8 ConstrainedFlag; /*constrained parameter flag */
    FSL_MPG_DEMUX_VIDEO_TYPE_T enuVideoType;
    U32 BitRate;
    U32 VBVSize;
    U32 FRNumerator;
    U32 FRDenominator;
    U32 ScanType;

} FSL_MPG_DEMUX_VSHeader_T;

/*Information contained in audio sequence header    */
typedef struct FSL_MPG_DEMUX_AHeader_S {
    U8 AudioVersion;
    U8 Layer;
    U8 ChannelMode;
    FSL_MPG_DEMUX_AUDIO_TYPE_T enuAudioType;
    U32 BitRate;
    U32 SampleRate;
    U32 Channels;
    U32 BitsPerSample;
    U8* pCodecData;
    U32 nCodecDataSize;
} FSL_MPG_DEMUX_AHeader_T;

S32 ResetCnxt(FSL_MPG_DEMUX_CNXT_T* pCnxt);
S32 ResyncCnxt(FSL_MPG_DEMUX_CNXT_T* pCnxt);

#endif /* FSL_MPG_DEMUX_CONTEXT_H  */
