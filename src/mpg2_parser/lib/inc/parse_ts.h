/*
 ***********************************************************************
 * Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
 *
 * Copyright 2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */
/*****************************************************************************
 * demux_ts.h
 * Description:
 * MPEG TS parser implementation header file.
 *
 ****************************************************************************/
#ifndef FSL_MPG_DEMUX_DEMUX_TS_H
#define FSL_MPG_DEMUX_DEMUX_TS_H

#include "mpeg2_parser_internal.h"
#include "mpg_demuxer_api.h"

U32 TSSync(U8* pBuf, U32 Len, U32* Offset, bool* hasTimeCode, S32 strict);

U32 ParseTSPacket(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuf, U32 index);

U32 ParseTSStreamPacket(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuffer,
                        U16 streamID);
U32 ScanTSStreamPacket(MPEG2ObjectPtr pDemuxer, U8* pBuffer, U32 streamPID, U16 streamID,
                       bool pesStart);

U32 programNumFromPID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 PID);

MPEG2_PARSER_ERROR_CODE MPEG2ParserGetPSI(MPEG2ObjectPtr pDemuxer);

U32 EnablePID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U16 PID);

U32 DisablePID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U16 PID);

U32 ParsePATSection(FSL_MPG_DEMUX_CNXT_T* pCnxt);

U32 ParsePMTSection(FSL_MPG_DEMUX_CNXT_T* pCnxt, int index);

U32 PassOutPAT(MPEG2ObjectPtr pDemuxer);

U32 GetPIDForPMT(FSL_MPG_DEMUX_CNXT_T* pCnxt);

FSL_VOID ResetTSPacketCnxt(FSL_MPG_DEMUX_CNXT_T* pCnxt);

U32 IsSupportedStream(U32 StreamType);

U32 IsSupportedVideoStream(U32 StreamType);

U32 IsSupportedAudioStream(U32 StreamType);

U32 GetTypeFromPID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 PID);

U32 ParsePES_Probe(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt,
                   FSL_MPG_DEMUX_TS_BUFFER_T* pPESbuf, U32 streamIdx, bool isTs);

U32 ParsePES_Process(MPEG2ObjectPtr* pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt,
                     FSL_MPG_DEMUX_TS_BUFFER_T* pPESbuf);

U32 ParsePES_Scan(MPEG2ObjectPtr* pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt,
                  FSL_MPG_DEMUX_TS_BUFFER_T* pPESbuf);

U32 ResetTSTempBuffer(FSL_MPG_DEMUX_CNXT_T* pCnxt);

MPEG2_PARSER_ERROR_CODE MPEG2ParserGetPSI(MPEG2ObjectPtr pDemuxer);

MPEG2_PARSER_ERROR_CODE MPEG2ParserProbe(MPEG2ObjectPtr pDemuxer);

U32 ScanPESStreamPacket(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuf,
                               U32 Size, U32 PID, U32 PayloadStart, U32 StreamIDSelected);
U32 UpdateTSPacketCnxt(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 PID, U32 ContinuityCnt);

int SetTempStreamBuffer(MPEG2ObjectPtr pDemuxer, U8* pBuf, U32 size, U32 PID);

void FreeTempStreamBuffer(MPEG2ObjectPtr pDemuxer);

void FillMediaStreamInfo(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32 streamID, U32 streamPID,
                         void* streamHeader, U32 isVideo, U8* pCodecDataBuf, U32 nCodecDataSize);

U64 calculate_PTS_delta_threshold(U32 num, U32 den);

#endif /*FSL_MPG_DEMUX_DEMUX_TS_H  */
