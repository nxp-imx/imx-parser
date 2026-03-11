/*
***********************************************************************
* Copyright (c) 2005-2013, Freescale Semiconductor, Inc.
* Copyright 2022-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
/*****************************************************************************
 * demux_ps.c
 *
 * Description:
 * MPEG parser API functions implementation.
 *
 *
 ****************************************************************************/
/*for memset()  */
#include <stdio.h>
#include <string.h>

#include "mpg_demuxer_api.h"
#include "parse_cfg.h"
#include "parse_ps_context.h"
#include "parse_ps_sc.h"
#include "parse_ts.h"
#include "parse_ts_defines.h"

#include "mpeg2_parser_api.h"
#include "mpeg2_parser_internal.h"

extern FslFileStream g_streamOps;
extern ParserMemoryOps g_memOps;

#ifdef DEMUX_DEBUG
#include <assert.h>
#endif

// #define MPG2_PARSER_PS_DBG
#ifdef MPG2_PARSER_PS_DBG
#define MPG2_PARSER_PS_LOG printf
#define MPG2_PARSER_PS_ERR printf
#else
#define MPG2_PARSER_PS_LOG(...)
#define MPG2_PARSER_PS_ERR(...)
#endif
#define ASSERT(exp)                                                                      \
    if (!(exp)) {                                                                        \
        MPG2_PARSER_PS_ERR("%s: %d : assert condition !!!\r\n", __FUNCTION__, __LINE__); \
    }

/*    */
extern U32 ParseSystemHeader(FSL_MPG_DEMUX_SYSHDR_T* pSysHdr, U8* pInput, U32 Offset, U32 MaxLen);

extern FSL_VOID ListSupportedStreams(FSL_MPG_DEMUX_CNXT_T* pCnxt);

extern U32 FindProgramBySupportedStream(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 dwSupportedStreamIdx,
                                        U32* pdwProgramIdx);

static S32 doublecheck_pesheader(U8* p, U8* end) {  // reference 0.6.90
    S32 pes1;
    S32 pes2 = (p[3] & 0xC0) == 0x80 && (p[4] & 0xC0) != 0x40 &&
               ((p[4] & 0xC0) == 0x00 || (p[4] & 0xC0) >> 2 == (p[6] & 0xF0));

    for (p += 3; p < end && *p == 0xFF; p++);
    if ((*p & 0xC0) == 0x40)
        p += 2;
    if ((*p & 0xF0) == 0x20) {
        pes1 = p[0] & p[2] & p[4] & 1;
    } else if ((*p & 0xF0) == 0x30) {
        pes1 = p[0] & p[2] & p[4] & p[5] & p[7] & p[9] & 1;
    } else
        pes1 = *p == 0x0F;

    return pes1 || pes2;
}

S32 ResetCnxt(FSL_MPG_DEMUX_CNXT_T* pCnxt) {
    /*Most of the variables are reset to zero  */
    int i;
    memset(pCnxt, 0, sizeof(FSL_MPG_DEMUX_CNXT_T));

    /*The input buffer  */
    pCnxt->InputBuf.Last4Bytes = 0xFFFFFFFF;
    pCnxt->InputBuf.ReadBufIndex = 0;
    pCnxt->InputBuf.ReadBufOffset = 0;

    /*Media information */

    /*Probe status  */
    pCnxt->ProbeCnxt.ProbeStage = FSL_MPG_DEMUX_PROBE_PSM;
    pCnxt->TSCnxt.TSProbeState = FSL_MPG_DEMUX_TS_PROBE_INIT;
    pCnxt->TSCnxt.TSProcessState = FSL_MPG_DEMUX_TS_PROCESS_INIT;
    pCnxt->TSCnxt.TSScanState = FSL_MPG_DEMUX_TS_SCAN_INIT;

    for (i = 0; i < MAX_MPEG2_STREAMS; i++) pCnxt->TSCnxt.TempBufs.PESStreamBuf[i].pBuf = NULL;

    return 0;
}

S32 ResyncCnxt(FSL_MPG_DEMUX_CNXT_T* pCnxt) {
    /*Most of the variables are reset to zero  */

    /*The input buffer  */
    pCnxt->InputBuf.Last4Bytes = 0xFFFFFFFF;
    pCnxt->InputBuf.ReadBufIndex = 0;
    pCnxt->InputBuf.ReadBufOffset = 0;

    /*Media information */

    /*Probe status  */

    /*if TS, should resync the input  */
    pCnxt->TSCnxt.Synced = 0;
    /*reset TS pakckets context   */
    ResetTSPacketCnxt(pCnxt);
    /*bug fix, reset scan and process state also   */
    pCnxt->TSCnxt.TSProcessState = FSL_MPG_DEMUX_TS_PROCESS_INIT;
    pCnxt->TSCnxt.TSScanState = FSL_MPG_DEMUX_TS_SCAN_INIT;

    return 0;
}

#define CONVERT_MPEGAUDIO_TYPE(x)                   \
    ((x) == 1 ? FSL_MPG_DEMUX_MP3_AUDIO             \
              : ((x) == 2 ? FSL_MPG_DEMUX_MP2_AUDIO \
                          : ((x) == 3 ? FSL_MPG_DEMUX_MP1_AUDIO : FSL_MPG_DEMUX_AAC_AUDIO)))

#define MAX_PMT_SEARCH_BYTES (U64)(10 << 20)
#define MAX_PMT_SEARCH_BYTES_STREAMING_MODE (U64)(256 << 10)

U32 IsDuplicatedStreamPID(MPEG2ObjectPtr pDemuxer, U32 stream_cnt, U32 PID) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;
    U32 i = 0;
    while (i < stream_cnt) {
        if (pCnxt->TSCnxt.TempBufs.PESStreamBuf[i++].PID == PID)
            return 1;
    }
    return 0;
}

void MPEG2GetMediaStreamType(U32 type_value, U32 descriptor_tag, U32* media_type,
                             U32* stream_type) {
    switch (type_value) {
        case MPEG1_VIDEO:
        case MPEG2_VIDEO:
            *media_type = FSL_MPG_DEMUX_VIDEO_STREAM;
            *stream_type = FSL_MPG_DEMUX_MPEG2_VIDEO;
            break;
        case MPEG4_VIDEO:
            *media_type = FSL_MPG_DEMUX_VIDEO_STREAM;
            *stream_type = FSL_MPG_DEMUX_MP4_VIDEO;
            break;
        case MPEG2_H264:
            *media_type = FSL_MPG_DEMUX_VIDEO_STREAM;
            *stream_type = FSL_MPG_DEMUX_H264_VIDEO;
            break;
        case MPEG2_HEVC:
            *media_type = FSL_MPG_DEMUX_VIDEO_STREAM;
            *stream_type = FSL_MPG_DEMUX_HEVC_VIDEO;
            break;
        case MPEG1_AUDIO:
            *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
            *stream_type = FSL_MPG_DEMUX_MP1_AUDIO;
            break;
        case MPEG2_AUDIO:
            *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
            *stream_type = FSL_MPG_DEMUX_MP2_AUDIO;
            break;
        case MPEG2_AC3:
            *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
            *stream_type = FSL_MPG_DEMUX_AC3_AUDIO;
            break;
        case MPEG2_AAC:
        case AAC_LATM:
            *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
            *stream_type = FSL_MPG_DEMUX_AAC_AUDIO;
            break;
        case MPEG2_DTS:
            *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
            *stream_type = FSL_MPG_DEMUX_DTS_AUDIO;
            break;
        case MPEG2_LPCM:
            *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
            *stream_type = FSL_MPG_DEMUX_PCM_AUDIO;
            break;
        case MPEG2_PRIVATEPES_DVB:
            if (MPEG2_DESC_DVB_AC3 == descriptor_tag) {
                *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
                *stream_type = FSL_MPG_DEMUX_AC3_AUDIO;
            } else if (MPEG2_DESC_DVB_ENHANCED_AC3 == descriptor_tag) {
                *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
                *stream_type = FSL_MPG_DEMUX_EAC3_AUDIO;
            } else if (MPEG2_DESC_DVB_DTS == descriptor_tag) {
                *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
                *stream_type = FSL_MPG_DEMUX_DTS_AUDIO;
            } else if (MPEG2_EXT_DESC_AC4 == descriptor_tag) {
                *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
                *stream_type = FSL_MPG_DEMUX_AC4_AUDIO;
            } else if (MPEG2_EXT_DESC_DTS_HD == descriptor_tag) {
                *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
                *stream_type = FSL_MPG_DEMUX_DTS_HD_AUDIO;
            } else if (MPEG2_EXT_DESC_DTS_UHD == descriptor_tag) {
                *media_type = FSL_MPG_DEMUX_AUDIO_STREAM;
                *stream_type = FSL_MPG_DEMUX_DTS_UHD_AUDIO;
            }
            break;
        default:
            *media_type = FSL_MPG_DEMUX_UNKNOWN_MEDIA;
            break;
    }
}

MPEG2_PARSER_ERROR_CODE MPEG2GetStreamInfoFromPMT(MPEG2ObjectPtr pDemuxer) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;
    FSL_MPG_DEMUX_SYSINFO_T* pSysInfo = &(pDemuxer->SystemInfo);
    FSL_MPG_DEMUX_PMTSECTION_T* pPMTSection;
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    U32 i = 0, j = 0, k = 0, nPMTs = pCnxt->TSCnxt.nPMTs;
    U32 PID, type_value, stream_cnt = 0;

    for (i = 0; i < nPMTs; i++) {
        pPMTSection = &(pCnxt->TSCnxt.PMT[i].PMTSection[0]);
        if (NULL == pPMTSection)
            break;

        for (j = 0; j < pPMTSection->Streams; j++) {
            U32 media_type, stream_type;
            FSL_MPEGSTREAM_T* pStream = NULL;

            k = pDemuxer->SystemInfo.uliNoStreams;
            PID = pPMTSection->StreamPID[j];
            type_value = pPMTSection->StreamType[j];
            media_type = stream_type = 0;
            pStream = &(pSysInfo->Stream[k]);

            if (IsDuplicatedStreamPID(pDemuxer, stream_cnt, PID)) {
                stream_cnt++;
                continue;
            }

            MPEG2GetMediaStreamType(type_value, pPMTSection->StreamDescriptorTag[j], &media_type,
                                    &stream_type);

            if (FSL_MPG_DEMUX_VIDEO_STREAM == media_type) {
                pCnxt->Media.VideoInfoFound = 1;
                pStream->MediaProperty.VideoProperty.enuVideoType = stream_type;
                pStream->MediaProperty.VideoProperty.uliFRNumerator =
                        pPMTSection->VideoFrameRate[j][0];
                pStream->MediaProperty.VideoProperty.uliFRDenominator =
                        pPMTSection->VideoFrameRate[j][1];
                pStream->MediaProperty.VideoProperty.uliVideoWidth = VIDEO_WIDTH_DEFAULT;
                pStream->MediaProperty.VideoProperty.uliVideoHeight = VIDEO_HEIGHT_DEFAULT;
                pStream->maxPTSDelta = calculate_PTS_delta_threshold(
                        pStream->MediaProperty.VideoProperty.uliFRNumerator,
                        pStream->MediaProperty.VideoProperty.uliFRDenominator);

            } else if (FSL_MPG_DEMUX_AUDIO_STREAM == media_type) {
                if (FSL_MPG_DEMUX_PCM_AUDIO == stream_type) {
                    Err = PARSER_NEED_MORE_DATA;
                    continue;
                } else {
                    // set default sample rate, channels
                    pStream->MediaProperty.AudioProperty.uliAudioSampleRate = 44100;
                    pStream->MediaProperty.AudioProperty.usiAudioChannels = 2;
                }
                if (AAC_LATM == type_value) {
                    pStream->MediaProperty.AudioProperty.enuAudioSubType = FSL_MPG_DEMUX_AAC_RAW;
                }
                pCnxt->Media.AudioInfoFound = 1;
                pStream->MediaProperty.AudioProperty.enuAudioType = stream_type;

            } else if (FSL_MPG_DEMUX_UNKNOWN_MEDIA == media_type &&
                       MPEG2_PRIVATEPES_DVB == type_value) {
                pCnxt->TSCnxt.TempBufs.PESStreamBuf[stream_cnt].PID = PID;
                pCnxt->TSCnxt.TempBufs.PESStreamBuf[stream_cnt].StreamNum = (U32)(-1);
                stream_cnt++;
                continue;
            } else {
                continue;
            }

            pCnxt->TSCnxt.Streams.ReorderedStreamPID[k] = PID;
            pCnxt->TSCnxt.TempBufs.PESStreamBuf[stream_cnt].PID = PID;
            pCnxt->TSCnxt.TempBufs.PESStreamBuf[stream_cnt].StreamNum = pCnxt->Media.NoMediaFound;
            pCnxt->TSCnxt.PMT[i].adwValidTrackIdx[pCnxt->TSCnxt.PMT[i].ValidTrackNum] =
                    pDemuxer->SystemInfo.uliNoStreams;
            pCnxt->TSCnxt.PMT[i].adwValidTrackPID[pCnxt->TSCnxt.PMT[i].ValidTrackNum] = PID;
            pCnxt->TSCnxt.PMT[i].ValidTrackNum++;
            pStream->enuStreamType = media_type;
            pStream->uliPropertyValid = 1;
            pStream->streamNum = pCnxt->Media.NoMediaFound;
            pStream->isNoFrameBoundary = pPMTSection->bHDCPEncrypted;
            pSysInfo->uliNoStreams++;
            pCnxt->Media.NoMediaFound++;
            pCnxt->Media.SysInfoFound[k] = 0x55;
            stream_cnt++;
            DisablePID(pCnxt, PID);
        }
    }
    pCnxt->ProbeCnxt.Probed = 1;
    if (0 == pCnxt->Media.NoMediaFound)
        return PARSER_ERR_INVALID_MEDIA;
    return Err;
}

MPEG2_PARSER_ERROR_CODE MPEG2ParserProbe(MPEG2ObjectPtr pDemuxer) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt;
    U8* pInput = NULL;
    U32 Offset = 0;
    U32 FourBytes = 0;
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    U32 i = 0;
    U32 lastSearchLength = 0;
    bool hasTimeCode;
    U64 qwMaxPMTSearchBytes = 0;

    /*get the input buffer context*/
    pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;

    /*TS probe process   */
    if (pDemuxer->TS_PSI.IsTS) {
        U32 Ret = 0;
        U32 timecodeOffset = 0;
        if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, MIN_TS_STREAM_LEN)))
            goto bailprobe;

        /* first call  */
        if (FSL_MPG_DEMUX_TS_PROBE_INIT == pCnxt->TSCnxt.TSProbeState) {
            U32 SyncOffset = 0;
            /*reset TS packets context(continuity counter)   */
            ResetTSPacketCnxt(pCnxt);
            /*if no PAT found, direct go to the next state   */
            if (0 == pCnxt->TSCnxt.PAT.Parsed) {
                pCnxt->TSCnxt.TSProbeState = FSL_MPG_DEMUX_TS_PROBE_SYSINFO;
                pCnxt->TSCnxt.PAT.NonProgramSelected = 1;
                if ((Err = MPEG2FileSeek(pDemuxer, 0, 0, SEEK_SET)))
                    return Err;
            } else {
                /*PAT already parsed, remove from the PID list. Add PMT into PID list   */
                Ret = DisablePID(pCnxt, PID_PAT);

                // The original code is to enable to the SelectedPMT table

                {
                    // Enable PIDs
                    FSL_MPG_DEMUX_PAT_T* pPAT;
                    U32 k, m, n = 0;

                    pPAT = &(pCnxt->TSCnxt.PAT);
                    for (k = 0; k < pPAT->Sections; k++) {
                        for (m = 0; m < pPAT->PATSection[k].Programs; m++) {
                            if (0 != pPAT->PATSection[k].ProgramNum[m]) {
                                Ret = EnablePID(pCnxt, pPAT->PATSection[k].PID_PMT[m]);
                                if (Ret != 0) {
                                    return PARSER_ERR_UNKNOWN;
                                }

                                pCnxt->TSCnxt.TempBufs.PMTSectionBuf[n].pBuf =
                                        LOCALMalloc(SIZEOF_PMTSECTIONBUF);
                                if (NULL == pCnxt->TSCnxt.TempBufs.PMTSectionBuf[n].pBuf)
                                    return PARSER_INSUFFICIENT_MEMORY;
                                pCnxt->TSCnxt.TempBufs.PMTSectionBuf[n].Size = SIZEOF_PMTSECTIONBUF;
                                pCnxt->TSCnxt.TempBufs.PMTSectionBuf[n].PID =
                                        pCnxt->TSCnxt.PMT[n].PID = pPAT->PATSection[k].PID_PMT[m];
                                n++;
                            }
                        }
                    }
                    pCnxt->TSCnxt.nPMTs = n;
                }

                pCnxt->TSCnxt.TSProbeState = FSL_MPG_DEMUX_TS_PROBE_PMT;
            }
            /*sync TS  */
            hasTimeCode = pCnxt->TSCnxt.hasTimeCode;
            Ret = TSSync(pInput + Offset, MIN_TS_STREAM_LEN, &SyncOffset, &hasTimeCode, 0);
            if (0 != Ret) {
                Err = PARSER_ERR_INVALID_MEDIA;
                goto bailprobe;
            }
            {
                if (pCnxt->TSCnxt.hasTimeCode)
                    timecodeOffset = 4;
                if ((Err = MPEG2ParserRewindNBytes(
                             pDemuxer, 0, MIN_TS_STREAM_LEN - SyncOffset + timecodeOffset)))
                    goto bailprobe;
                if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput,
                                                 TS_PACKET_LENGTH + timecodeOffset)))
                    goto bailprobe;
            }
        }

        if (FSL_MPG_DEMUX_TS_PROBE_PMT == pCnxt->TSCnxt.TSProbeState) {
            /*the PID list only has PMT   */
            bool probePmtDone = FALSE;

            if (pDemuxer->fileSize > MIN_TS_STREAM_LEN)
                qwMaxPMTSearchBytes = pDemuxer->fileSize - MIN_TS_STREAM_LEN;
            else
                qwMaxPMTSearchBytes = pDemuxer->fileSize;
        PROBE_PMT:
            Ret = ParseTSPacket(pDemuxer, pCnxt, pInput, 0);
            if (0 != Ret) {
                if (16 == Ret) {
                    if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0,
                                                       TS_PACKET_LENGTH + timecodeOffset)))
                        return Err;
                } else {
                    Err = PARSER_ERR_UNKNOWN;
                    goto bailprobe;
                }
            }
            /*If the complete PMT section is available, parse it   */

            for (i = 0; i < pCnxt->TSCnxt.nPMTs; i++) {
                if (1 == pCnxt->TSCnxt.TempBufs.PMTSectionBuf[i].Complete) {
                    Ret = ParsePMTSection(pCnxt, i);
                    if (0 == Ret)
                        pCnxt->TSCnxt.nParsedPMTs++;
                    else if (Ret != 256)
                        continue;

                    if (STREAMING_MODE == pDemuxer->playMode) {
                        bool hasVideo = FALSE;
                        bool hasAudio = FALSE;
                        U32 k, m, n;

                        qwMaxPMTSearchBytes =
                                pDemuxer->fileOffset + MAX_PMT_SEARCH_BYTES_STREAMING_MODE;
                        for (k = 0; k < pCnxt->TSCnxt.nPMTs; k++) {
                            FSL_MPG_DEMUX_PMT_T* pmt = &(pCnxt->TSCnxt.PMT[k]);
                            for (m = 0; m < pmt->Sections; m++) {
                                for (n = 0; n < pmt->PMTSection[m].Streams; n++) {
                                    if (IsSupportedVideoStream(
                                                (U32)pmt->PMTSection[m].StreamType[n]))
                                        hasVideo = TRUE;
                                    else if (IsSupportedAudioStream(
                                                     (U32)pmt->PMTSection[m].StreamType[n]))
                                        hasAudio = TRUE;
                                }
                            }
                            if (hasVideo && hasAudio) {
                                probePmtDone = TRUE;
                                break;
                            }
                        }
                    }
                }
            }

            // support multi-program
            // some clips have no enough programs as PAT told, so at worst case, parsed max to
            // MAX_PMT_SEARCH_BYTES.
            if (qwMaxPMTSearchBytes > MAX_PMT_SEARCH_BYTES)
                qwMaxPMTSearchBytes = MAX_PMT_SEARCH_BYTES;

            if (pDemuxer->fileOffset >= qwMaxPMTSearchBytes && pCnxt->TSCnxt.nParsedPMTs > 0)
                probePmtDone = TRUE;

            // fix ENGR00236479
            if (probePmtDone) {
                for (i = 0; i < pCnxt->TSCnxt.nPMTs; i++) {
                    DisablePID(pCnxt, pCnxt->TSCnxt.TempBufs.PMTSectionBuf[i].PID);
                }
                ListSupportedStreams(pCnxt);
                /*remove PMT from PID list   */
                pCnxt->TSCnxt.TSProbeState = FSL_MPG_DEMUX_TS_PROBE_SYSINFO;
                if ((Err = MPEG2FileSeek(pDemuxer, 0, 0, SEEK_SET)))
                    return Err;
                // sync stream
                {
                    U32 SyncOffset = 0;
                    if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, MIN_TS_STREAM_LEN)))
                        goto bailprobe;
                    hasTimeCode = pCnxt->TSCnxt.hasTimeCode;
                    Ret = TSSync(pInput, MIN_TS_STREAM_LEN, &SyncOffset, &hasTimeCode, 0);
                    if (0 != Ret) {
                        Err = PARSER_ERR_INVALID_MEDIA;
                        goto bailprobe;
                    }
                    {
                        timecodeOffset = 0;
                        if (pCnxt->TSCnxt.hasTimeCode)
                            timecodeOffset = 4;
                        if ((Err = MPEG2ParserRewindNBytes(
                                     pDemuxer, 0, MIN_TS_STREAM_LEN - SyncOffset + timecodeOffset)))
                            goto bailprobe;
                        if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput,
                                                         TS_PACKET_LENGTH + timecodeOffset)))
                            goto bailprobe;
                    }
                }
            } else {
                if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput,
                                                 TS_PACKET_LENGTH + timecodeOffset)))
                    goto bailprobe;
                goto PROBE_PMT;
            }
        }
        /****************************************************************
        PMT parsed, then get system information
        *****************************************************************/
        Ret = 0;
        if (FSL_MPG_DEMUX_TS_PROBE_SYSINFO == pCnxt->TSCnxt.TSProbeState) {
            if (STREAMING_MODE == pDemuxer->playMode || TRUE == pCnxt->dataEncrypted) {
                Err = MPEG2GetStreamInfoFromPMT(pDemuxer);
                if (Err == PARSER_SUCCESS)
                    goto probeok;
                else if (Err != PARSER_NEED_MORE_DATA)
                    goto bailprobe;
            }

        TS_PROBE_SYSINFO:
            if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput,
                                             TS_PACKET_LENGTH + timecodeOffset)))
                goto bailprobe;

            Ret = ParseTSPacket(pDemuxer, pCnxt, pInput, 0);
            if (0 != Ret) {
                if (16 == Ret) {
                    if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0,
                                                       TS_PACKET_LENGTH + timecodeOffset)))
                        goto bailprobe;
                    Ret = 0;

                } else {
                    Err = PARSER_ERR_UNKNOWN;
                    goto bailprobe;
                }
            }

            /*Parse PES and get system info   */
            /* Caution: since in the layer of PES, may filter again, so
             * pCnxt->TSCnxt.Streams.SupportedStreams >= pDemuxer->SystemInfo.uliNoStreams */
            // fix me ??? In case of pCnxt->TSCnxt.PAT.NonProgramSelected == 1, SupportedStreams is
            // dynamic incerased in TS_PROBE_SYSINFO state, so may exit this state just after one
            // stream is selected.
            for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
                U32 dwProgramIdx;
                U32 dwPreNoStreams = pDemuxer->SystemInfo.uliNoStreams;
                if (1 == pCnxt->TSCnxt.TempBufs.PESStreamBuf[i].Complete) {
                    Ret = ParsePES_Probe(pDemuxer, pCnxt, &(pCnxt->TSCnxt.TempBufs.PESStreamBuf[i]),
                                         i, TRUE);
                    if (0 != Ret) {
                        Err = PARSER_ERR_UNKNOWN;
                        goto bailprobe;
                    }

                    // find a valid stream in PES layer
                    if ((0 == pCnxt->TSCnxt.PAT.NonProgramSelected) &&
                        (pDemuxer->SystemInfo.uliNoStreams > dwPreNoStreams)) {
                        // since i < pCnxt->TSCnxt.Streams.SupportedStreams, so must sucess
                        FindProgramBySupportedStream(pCnxt, i, &dwProgramIdx);
                        pCnxt->TSCnxt.PMT[dwProgramIdx]
                                .adwValidTrackIdx[pCnxt->TSCnxt.PMT[dwProgramIdx].ValidTrackNum] =
                                dwPreNoStreams;
                        pCnxt->TSCnxt.PMT[dwProgramIdx]
                                .adwValidTrackPID[pCnxt->TSCnxt.PMT[dwProgramIdx].ValidTrackNum] =
                                pCnxt->TSCnxt.TempBufs.PESStreamBuf[i].PID;
                        pCnxt->TSCnxt.PMT[dwProgramIdx].ValidTrackNum++;
                    }
                }
            }
            if (0 != Ret) {
                Err = PARSER_ERR_UNKNOWN;
                goto bailprobe;
            };
            /*all the system information have been got, return success   */
            // #ifdef MPG2_PROBE_STREAMING_ACCELERATE
            if ((pCnxt->Media.NoMediaFound >= pCnxt->TSCnxt.Streams.SupportedStreams &&
                 pCnxt->Media.NoMediaFound > 0) ||
                (pDemuxer->fileOffset > MAX_PARSE_HEAD_SIZE)) {
                pCnxt->ProbeCnxt.Probed = 1;
                Err = PARSER_SUCCESS;
                goto probeok;
            }
            goto TS_PROBE_SYSINFO;
        }
    }

    /**********************************************************************************
    TS versus PS
    **********************************************************************************/

    /*restructure me later !!!!!  */
    {
    PROBE_PSM:
        if (FSL_MPG_DEMUX_PROBE_PSM == pCnxt->ProbeCnxt.ProbeStage) {
            U32 SCFound = 0;
            U32 PESHeaderLen = 0;
            U32 searchLength = 0;

            /*search the first PES*/
            /*First in the current buffer, search start code */

            while (!SCFound) {
                U32 Byte = 0;
                if ((Err = MPEG2ParserNextNBytes(pDemuxer, 0, 1, &Byte)))
                    goto bailprobe;

                FourBytes = (FourBytes << 8) | Byte;
                if (IS_SC(FourBytes) && (IS_PES(FourBytes) || (IS_SH(FourBytes)))) {
                    SCFound = 1;
                    lastSearchLength = searchLength;
                    MPG2_PARSER_PS_LOG("find one start code: 0x%X, search length: %d \r\n",
                                       FourBytes, searchLength);
                }
                if (PS_PACK_HEADER == FourBytes) {
                    pDemuxer->SystemInfo.uliPSFlag = 1;
                }
                searchLength++;
                if (searchLength >= MAX_PES_SYNC_SIZE) {
                    Err = PARSER_ERR_INVALID_MEDIA;
                    goto bailprobe;
                } else if (!SCFound && searchLength > PES_SEQ_SYNC_SIZE &&
                           lastSearchLength > PES_SEQ_SYNC_SIZE) {
                    Err = PARSER_ERR_INVALID_MEDIA;
                    goto bailprobe;
                }
            }

            /*Now start code found   */
            if ((Err = MPEG2ParserNextNBytes(pDemuxer, 0, 2, &PESHeaderLen)))
                goto bailprobe;
            if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, PESHeaderLen + 4)))
                goto bailprobe;
            Offset = 0;

            /*PSM */
            if (PS_PSM_ID == PS_ID(FourBytes)) {
                /*Parse PSM here   */
                /*At this moment, just skip this PES   */
                /*add the PSM parer later   */
                Offset += PESHeaderLen;
            } else if (PS_SYSTEM_HEADER_ID == PS_ID(FourBytes)) {
                ParseSystemHeader(&(pCnxt->SystemHeader), pInput, Offset, PESHeaderLen);
                Offset += PESHeaderLen;
            } else if (IS_PES(FourBytes)) {
                if (IS_PRIV1(FourBytes) || IS_AUDIO_PES(FourBytes) || IS_VIDEO_PES(FourBytes)) {
                    U8* pbyPSHead = pInput + PESHeaderLen;

                    // judge the next PS head, or PES head, since a PS may have multiple PES
                    if (!((pbyPSHead[0] == 0x00) && (pbyPSHead[1] == 0x00) &&
                          (pbyPSHead[2] == 0x01))) {
                        Err = PARSER_ERR_INVALID_MEDIA;
                        goto bailprobe;
                    }

                    if (0 == doublecheck_pesheader(pInput - 3, pInput + PESHeaderLen + 4)) {
                        // invalid pes header
                        goto PROBE_PSM;
                    }
                }
                if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0, PESHeaderLen + 2 + 4)))
                    goto bailprobe;
            }

            /* goto PROBE_SYSINFO to parse this PES */
            if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0, 4)))
                goto bailprobe;

            pCnxt->ProbeCnxt.ProbeStage = FSL_MPG_DEMUX_PROBE_SYSINFO;
        }
    }

PROBE_SYSINFO:
    if (pCnxt->Media.NoMediaFound >= MAX_MPEG2_STREAMS) {
        pCnxt->ProbeCnxt.Probed = 1;
        return PARSER_SUCCESS;
    }

    if ((1 == pCnxt->SystemHeader.Found) &&
        (pCnxt->Media.AudioInfoFound >= pCnxt->SystemHeader.AudioBound &&
         pCnxt->SystemHeader.AudioBound > 0) &&
        (pCnxt->Media.VideoInfoFound >= pCnxt->SystemHeader.VideoBound &&
         pCnxt->SystemHeader.VideoBound > 0)) {
        pCnxt->ProbeCnxt.Probed = 1;
        return PARSER_SUCCESS;
    } else if (pDemuxer->fileOffset > MAX_PARSE_HEAD_SIZE ||
               pDemuxer->fileOffset > (pDemuxer->fileSize >> 1)) {
        if (0 == pDemuxer->fileSize) {
            MPG2_PARSER_PS_ERR("file size is zero !!!! \r\n");
        }
        if (0 == pCnxt->Media.NoMediaFound) {
            // fix some .mp3 case
            Err = PARSER_ERR_INVALID_MEDIA;
            goto bailprobe;
        } else {
            pCnxt->ProbeCnxt.Probed = 1;
            Err = PARSER_SUCCESS;
            goto probeok;
        }
    }

    if (FSL_MPG_DEMUX_PROBE_SYSINFO == pCnxt->ProbeCnxt.ProbeStage) {
        U32 SCFound = 0;
        U32 PESHeaderLen = 0;
        U32 searchLength = 0;
        FSL_MPG_DEMUX_TS_BUFFER_T PesBuf;

        Offset = 0;
        lastSearchLength = 0;
        FourBytes = pCnxt->InputBuf.Last4Bytes;

        /*search the first PES*/
        /*First in the current buffer, search start code */
        while (!SCFound) {
            U32 Byte;

            if ((Err = MPEG2ParserNextNBytes(pDemuxer, 0, 1, &Byte)))
                goto bailprobe;

            FourBytes = (FourBytes << 8) | Byte;
            if (IS_SC(FourBytes) && (IS_PES(FourBytes))) {
                SCFound = 1;
                lastSearchLength = searchLength;
                MPG2_PARSER_PS_LOG("find one start code: 0x%X, search length: %d \r\n", FourBytes,
                                   searchLength);
            }
            if (PS_PACK_HEADER == FourBytes) {
                pDemuxer->SystemInfo.uliPSFlag = 1;
            }

            searchLength++;
            if (searchLength > MAX_PES_SYNC_SIZE) {
                Err = PARSER_ERR_INVALID_MEDIA;
                goto bailprobe;

            } else if (!SCFound && searchLength > PES_SEQ_SYNC_SIZE &&
                       lastSearchLength > PES_SEQ_SYNC_SIZE) {
                Err = PARSER_ERR_INVALID_MEDIA;
                goto bailprobe;
            }
        }

        /*Now start code found   */
        if ((Err = MPEG2ParserNextNBytes(pDemuxer, 0, 2, &PESHeaderLen)))
            goto bailprobe;

        if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, PESHeaderLen + 4)))
            goto bailprobe;

        memset(&PesBuf, 0, sizeof(FSL_MPG_DEMUX_TS_BUFFER_T));
        PesBuf.Complete = 1;
        PesBuf.pBuf = pInput - 6;
        PesBuf.PESLen = PESHeaderLen + 6;
        PesBuf.Filled = PesBuf.PESLen;
        PesBuf.Size = PesBuf.PESLen;

        Err = ParsePES_Probe(pDemuxer, pCnxt, &PesBuf, 0, FALSE);

        /*continue parse system information   */
        pCnxt->ProbeCnxt.ProbeStage = FSL_MPG_DEMUX_PROBE_SYSINFO;

        /*save the context   */
        if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0, 4)))
            goto bailprobe;

        goto PROBE_SYSINFO;
    }

probeok:
    Err = PARSER_SUCCESS;
    return Err;

bailprobe:
    MPEG2FileSeek(pDemuxer, 0, 0, SEEK_SET);
    return Err;
}

// fix ENGR00214108, for some case, nParsedPMTs is not equal to nPMTs,
// so need to move the sparse PMT array to a compact array.
MPEG2_PARSER_ERROR_CODE RemapProgram(MPEG2ObjectPtr pDemuxer) {
    U32 dwProgramIdx = 0;
    FSL_MPG_DEMUX_PMT_T* pCompactPMTTable = NULL;
    U32 dwCompactedIdx = 0;
    FSL_MPG_DEMUX_TS_CNXT_T* pTSCnxt;

    if ((pDemuxer == NULL) || (pDemuxer->pDemuxContext == NULL)) {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    pTSCnxt = &pDemuxer->pDemuxContext->TSCnxt;

    if (pTSCnxt->nParsedPMTs == 0) {
        return PARSER_SUCCESS;
    }

    if (pTSCnxt->nParsedPMTs == pTSCnxt->nPMTs) {
        return PARSER_SUCCESS;
    }

    pCompactPMTTable =
            (FSL_MPG_DEMUX_PMT_T*)LOCALCalloc(pTSCnxt->nParsedPMTs, sizeof(FSL_MPG_DEMUX_PMT_T));
    if (NULL == pCompactPMTTable)
        return PARSER_INSUFFICIENT_MEMORY;

    dwCompactedIdx = 0;
    for (dwProgramIdx = 0; dwProgramIdx < pTSCnxt->nPMTs; dwProgramIdx++) {
        if (pTSCnxt->PMT[dwProgramIdx].ValidTrackNum > 0) {
            pCompactPMTTable[dwCompactedIdx] = pTSCnxt->PMT[dwProgramIdx];
            dwCompactedIdx++;
            if (dwCompactedIdx >= pTSCnxt->nParsedPMTs) {
                break;
            }
        }
    }

    memcpy(pTSCnxt->PMT, pCompactPMTTable, sizeof(FSL_MPG_DEMUX_PMT_T) * pTSCnxt->nParsedPMTs);

    MPG2_PARSER_PS_LOG("decrease nPMTs from %d to %d \r\n", pTSCnxt->nPMTs, pTSCnxt->nParsedPMTs);
    pTSCnxt->nPMTs = pTSCnxt->nParsedPMTs;  // update nPMTs
    LOCALFree(pCompactPMTTable);

    return PARSER_SUCCESS;
}
