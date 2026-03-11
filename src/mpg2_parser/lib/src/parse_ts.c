/*
***********************************************************************
* Copyright (c) 2005-2013, Freescale Semiconductor, Inc.
*
* Copyright 2017-2022, 2024-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
/*****************************************************************************
 * parse_ts.c
 *
 * Use of Freescale code is governed by terms and conditions
 * stated in the accompanying licensing statement.
 *
 * Description:
 * MPEG TS parser implementation.
 *
 ****************************************************************************/
/*   */
#include <stdio.h>
#include <string.h>

#include "mpeg2_parser_internal.h"
#include "parse_ts_defines.h"

#include "parse_cfg.h"

#include "mpeg2_epson_exvi.h"
#include "parse_ps_context.h"
#include "parse_ps_sc.h"
#include "parse_ts.h"

#ifdef DEMUX_DEBUG
#include <assert.h>
#endif

// #define MPG2_PARSER_TS_DBG
#ifdef MPG2_PARSER_TS_DBG
#define MPG2_PARSER_TS_LOG printf
#define MPG2_PARSER_TS_ERR printf
#else
#define MPG2_PARSER_TS_LOG(...)
#define MPG2_PARSER_TS_ERR(...)
#endif
#define ASSERT(exp)                                                                      \
    if (!(exp)) {                                                                        \
        MPG2_PARSER_TS_ERR("%s: %d : assert condition !!!\r\n", __FUNCTION__, __LINE__); \
    }

#define MIN_PES_NUM 5
#define TEMP_BUFFER_SIZE 1024 * 10

extern U32 ParseMPEGAudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, U32 MaxLen);
extern U32 ParseMp4VideoInfo(FSL_MPG_DEMUX_CNXT_T* pCnxt, FSL_MPG_DEMUX_VSHeader_T* pVHeader,
                             U8* pBuf, U32 MaxLen);
extern U32 ParseH264VideoInfo(ParserMemoryOps* memOps, FSL_MPG_DEMUX_VSHeader_T* pVHeader, U8* pBuf,
                              U32 MaxLen);
extern U32 ParseMPEG2VideoInfo(FSL_MPG_DEMUX_CNXT_T* pCnxt, FSL_MPG_DEMUX_VSHeader_T* pVHeader,
                               U8* pBuf, U32 MaxLen);

extern U32 ParseDTSAudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, U32 MaxLen);
extern U32 ParseAC3AudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, U32 MaxLen);
extern U32 ParseAC4AudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, U32 MaxLen);
extern U32 ParseAACAudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pBuf, U32 MaxLen);
extern U32 ParseLPCMAudioInfo(FSL_MPG_DEMUX_AHeader_T* pAHeader, U8* pInput, U32 Offset,
                              int MaxLen);

extern FslFileStream g_streamOps;

// calculate max PTS delta between two frames
// here set max PTS delta as 100 * frame duration
U64 calculate_PTS_delta_threshold(U32 num, U32 den) {
    U64 max_PTS_delta = 60000000;  // 60s
    U64 threshold = 0;
    U64 frameUs = 0;

    if (num != 0 && den != 0)
        frameUs = 1000000 / num * den;

    threshold = 100 * frameUs;
    return threshold > max_PTS_delta ? threshold : max_PTS_delta;
}

/*
Sync the  TS
Return 0 if sucess,
return 1 if not enough information,
return 2 otherwise (regarded as PS/PES)
*/
U32 TSSync(U8* pBuf, U32 Len, U32* Offset, bool* hastimecode, S32 strict) {
    U32 i = 0;
    U8* pTemp;
    U32 timeCodeOffsets = 0;
    U32 less4found = 0;
    U32 less4offset = 0;

    if (*hastimecode)
        goto TS_TIMECODE;

    *hastimecode = FALSE;

    /*if Len is samller than 188, not enough information return 1  */
    if (Len < MIN_TS_STREAM_LEN)
        return 1;
    {
        /*find the first sync word   */
        while (i < (TS_PACKET_LENGTH + timeCodeOffsets)) {
            if (TS_SYNC_BYTE == (*(pBuf + i))) {
                U32 j = (TS_PACKET_LENGTH + timeCodeOffsets);
                U32 Valid = 1;
                pTemp = pBuf + i;
                while ((i + j) < Len) {
                    if (((0 == strict) && (TS_SYNC_BYTE != (*(pTemp + j)))) ||
                        ((1 == strict) &&
                         (((TS_SYNC_BYTE != (*(pTemp + j))) || ((*(pTemp + j + 1)) & 0x80) ||
                           (0 == ((*(pTemp + j + 3)) & 0x30))))))
                    /*strict: sync_byte, transport_error_indicator,adaptation_field_control */
                    {
                        Valid = 0;
                        break;
                    } else {
                        j += TS_PACKET_LENGTH + timeCodeOffsets;
                    }
                }
                if (Valid) {
                    *Offset = i;
                    return 0;
                }
            }
            i++;
        }

    TS_TIMECODE:
        *hastimecode = TRUE;
        timeCodeOffsets = 4;
        i = 0;

    TS_SYNC1:
        while (i < (TS_PACKET_LENGTH + timeCodeOffsets)) {
            if (TS_SYNC_BYTE == (*(pBuf + i))) {
                U32 j = (TS_PACKET_LENGTH + timeCodeOffsets);
                U32 Valid = 1;
                pTemp = pBuf + i;
                while ((i + j) < Len) {
                    if (((0 == strict) && (TS_SYNC_BYTE != (*(pTemp + j)))) ||
                        ((1 == strict) &&
                         (((TS_SYNC_BYTE != (*(pTemp + j))) || ((*(pTemp + j + 1)) & 0x80) ||
                           (0 == ((*(pTemp + j + 3)) & 0x30))))))
                    /*strict: sync_byte, transport_error_indicator,adaptation_field_control */
                    {
                        Valid = 0;
                        break;
                    } else {
                        j += TS_PACKET_LENGTH + timeCodeOffsets;
                    }
                }
                if (Valid) {
                    *Offset = i;
                    if (i < 4) {
                        less4found = 1;
                        less4offset = i;
                        i = 4;
                        goto TS_SYNC1;
                    }

                    return 0;
                }
            }
            i++;
        }

        if (less4found) {
            *Offset = less4offset;
            return 0;
        } else
            return 2;
    }
}

/*
Return 0 if success
Return 1 if there is no room for the new PID
*/
U32 EnablePID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U16 PID) {
    U32 i;
    FSL_MPG_DEMUX_TS_PIDS_T* pPIDs;

    pPIDs = &(pCnxt->TSCnxt.ValidPIDs);

    /*if already exist   */
    for (i = 0; i < TS_PACKET_SUPPORT_MAX; i++) {
        if ((1 == pPIDs->Valid[i]) && (PID == pPIDs->TS_PID[i]))
            return 0;
    }

    /*add    */
    for (i = 0; i < TS_PACKET_SUPPORT_MAX; i++) {
        if (0 == pPIDs->Valid[i])
            break;
    }

    if (TS_PACKET_SUPPORT_MAX == i)
        return 1;

    pPIDs->Valid[i] = 1;
    pPIDs->TS_PID[i] = PID;
    return 0;
}

/*
Return 0 if success
Return 1 if the PID is not in enabled list.
*/
U32 DisablePID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U16 PID) {
    U32 i;
    FSL_MPG_DEMUX_TS_PIDS_T* pPIDs;

    pPIDs = &(pCnxt->TSCnxt.ValidPIDs);

    /*if already exist   */
    for (i = 0; i < TS_PACKET_SUPPORT_MAX; i++) {
        if ((1 == pPIDs->Valid[i]) && (PID == pPIDs->TS_PID[i])) {
            pPIDs->Valid[i] = 0;
            pPIDs->TS_PID[i] = 0;
            return 0;
        }
    }

    return 1;
}

/*
Return 1 if enabled
Return 0 if not
*/
static U32 IS_PIDEnabled(FSL_MPG_DEMUX_CNXT_T* pCnxt, U16 PID) {
    U32 i;
    FSL_MPG_DEMUX_TS_PIDS_T* pPIDs;

    pPIDs = &(pCnxt->TSCnxt.ValidPIDs);

    for (i = 0; i < TS_PACKET_SUPPORT_MAX; i++) {
        if ((1 == pPIDs->Valid[i]) && (PID == pPIDs->TS_PID[i]))
            return 1;
    }

    return 0;
}

/*
Return 0 if success
Retrun 1 if the section is alrady complete;
return 2 if no enough room left in buffer.
*/
static U32 CopyPATPacket(FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuf, U32 Size, U32 PayloadStart) {
    FSL_MPG_DEMUX_TS_BUFFER_T* pPATSectionBuf;

    pPATSectionBuf = &(pCnxt->TSCnxt.TempBufs.PATSectionBuf);
#ifdef DEMUX_DEBUG
    assert(Size <= pPATSectionBuf->Size);
#endif

    if (1 == pPATSectionBuf->Complete)
        return 1;

    if (!PayloadStart) {
        if (0 == pPATSectionBuf->Filled)
            return 0;
    }
    /*copy the buffer anyway  */
    if ((Size + pPATSectionBuf->Filled) <= pPATSectionBuf->Size) {
        memcpy(pPATSectionBuf->pBuf + pPATSectionBuf->Filled, pBuf, Size);
        pPATSectionBuf->Filled += Size;
    } else
        return 2;

    /*get the PAT section Length  */
    if (0 == pPATSectionBuf->PESLen) {
        if (pPATSectionBuf->Filled >= 3) {
            U8* pTemp;
            pTemp = pPATSectionBuf->pBuf;
            pPATSectionBuf->PESLen = (*(pTemp + 2)) | (((*(pTemp + 1)) & 0xF) << 8);
            pPATSectionBuf->PESLen += 3;
        }
    }

    if (pPATSectionBuf->PESLen > 0) {
        if (pPATSectionBuf->Filled >= pPATSectionBuf->PESLen)
            pPATSectionBuf->Complete = 1;
    }

    return 0;
}

/*
Return 0 if success
Retrun 1 if the section is alrady complete;
return 2 if no enough room left in buffer.
*/
static U32 CopyPMTPacket(FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuf, U32 Size, U32 PayloadStart,
                         U32 index) {
    FSL_MPG_DEMUX_TS_BUFFER_T* pPMTSectionBuf;

    pPMTSectionBuf = &(pCnxt->TSCnxt.TempBufs.PMTSectionBuf[index]);
#ifdef DEMUX_DEBUG
    assert(Size <= pPMTSectionBuf->Size);
#endif

    if (1 == pPMTSectionBuf->Complete)
        return 1;
    if (!PayloadStart) {
        if (0 == pPMTSectionBuf->Filled)
            return 0;
    }
    /*copy the buffer anyway  */
    if ((Size + pPMTSectionBuf->Filled) <= pPMTSectionBuf->Size) {
        memcpy(pPMTSectionBuf->pBuf + pPMTSectionBuf->Filled, pBuf, Size);
        pPMTSectionBuf->Filled += Size;
    } else
        return 2;

    /*get the PMT section Length  */
    if (0 == pPMTSectionBuf->PESLen) {
        if (pPMTSectionBuf->Filled >= 3) {
            U8* pTemp;
            pTemp = pPMTSectionBuf->pBuf;
            pPMTSectionBuf->PESLen = (*(pTemp + 2)) | (((*(pTemp + 1)) & 0xF) << 8);
            pPMTSectionBuf->PESLen += 3;
        }
    }

    if (pPMTSectionBuf->PESLen > 0) {
        if (pPMTSectionBuf->Filled >= pPMTSectionBuf->PESLen)
            pPMTSectionBuf->Complete = 1;
    }

    return 0;
}

U32 streamNumFromPID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 PID) {
    U32 i, streamPID;

    for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
        streamPID = pCnxt->TSCnxt.Streams.ReorderedStreamPID[i];
        if (streamPID == PID)
            return i;
    }

    return -1;
}

U32 programNumFromPID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 PID) {
    U32 i, j, k;

    for (i = 0; i < pCnxt->TSCnxt.nPMTs; i++) {
        for (j = 0; j < pCnxt->TSCnxt.PMT[i].Sections; j++) {
            for (k = 0; k < pCnxt->TSCnxt.PMT[i].PMTSection[j].Streams; k++) {
                if (pCnxt->TSCnxt.PMT[i].PMTSection[j].StreamPID[k] == PID ||
                    pCnxt->TSCnxt.PMT[i].PMTSection[j].PCR_PID == PID)
                    return i;
            }
        }
    }
    return -1;
}

U32 ScanTSStreamPacket(MPEG2ObjectPtr pDemuxer, U8* pBuffer, U32 streamPID, U16 streamID,
                       bool pesStart) {
    U8* pTemp;
    U32 PayloadStart = 0;
    U32 PID;
    U32 ScarmblingCtrl = 0;
    U32 AdaptationCtrl = 0;
    U32 ContinuityCnt = 0;
    U32 AdaptationBytes = 0;
    S32 PayloadBytes = 0;
    U32 Ret;
    FSL_MPG_DEMUX_CNXT_T* pCnxt = pDemuxer->pDemuxContext;

    pTemp = pBuffer;
    if (pCnxt->TSCnxt.hasTimeCode)
        pTemp += 4;
#ifdef DEMUX_DEBUG
    assert(TS_SYNC_BYTE == (*pTemp));
#endif

    // fix ENGR00174120
    if (*pTemp != TS_SYNC_BYTE)  // check sync byte
        return 1;

    pTemp++;
    /*temporarily disable, it should be enable I think   */
#if 0
    if ((*pTemp) & 0x80) //error
        return 1;
#endif
    PayloadStart = (*pTemp) & 0x40;
    if (pesStart && !PayloadStart)
        return 0;

    PID = (*(pTemp + 1)) | (((*pTemp) & 0x1F) << 8);
    if (PID != streamPID)
        return 0;

    pTemp += 2;
    ScarmblingCtrl = ((*pTemp) & 0xC0) >> 6;
    if (ScarmblingCtrl != 0) {
        return 0;
    }
    AdaptationCtrl = ((*pTemp) & 0x30) >> 4;
    ContinuityCnt = (*pTemp) & 0xF;
    pTemp++;
    Ret = UpdateTSPacketCnxt(pCnxt, PID, ContinuityCnt);
#if 0
    if (0!=Ret)
        return 2;
#endif
    PayloadBytes = TS_PACKET_LENGTH - 4;
    /*adaptation field  */
    if ((0x2 == AdaptationCtrl) || (0x3 == AdaptationCtrl)) {
        AdaptationBytes = (U32)(*pTemp);
        pTemp += (AdaptationBytes + 1);
        /*At this moment we just skip the adaptation field
        If any information is to be used, parse the adaptation field*/
        PayloadBytes -= (AdaptationBytes + 1);
    }

    if (PayloadBytes > 0) {
        /*PAT   */
        if (PID == PID_PAT) {
        }
        /* PMT */

        /*PES  */
        else {
            Ret = ScanPESStreamPacket(pDemuxer, pCnxt, pTemp, PayloadBytes, PID, PayloadStart,
                                      streamID);
            if (16 == Ret)
                return 16;
            else if (0 != Ret)
                return 4;
        }
    }
    return 0;
}

/*
Return 0 if success
Return 1 if PID not enabled
Return 2 if PID not supported one
Return 3 if the previous PES has not been cleared
Return 4 if the buffer is too small
Return 5 if the wrong buffer
Return 16 if the packet is not copied and previous PES is complete
*/
U32 ScanPESStreamPacket(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuf,
                               U32 Size, U32 PID, U32 PayloadStart, U32 StreamIDSelected) {
    FSL_MPG_DEMUX_TS_BUFFER_T* pPESBuf;
    U32 StreamID;

    if (PayloadStart) {
        StreamID = ((*pBuf) << 24) | ((*(pBuf + 1)) << 16) | ((*(pBuf + 2)) << 8) | (*(pBuf + 3));
        if (PS_ID(StreamID) != PS_ID(StreamIDSelected))
            return 0;
    }

    // video or audio
    {
        U32 i;
        for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
            if (PID == pCnxt->TSCnxt.Streams.StreamPID[i])
                break;
        }
        if (i == pCnxt->TSCnxt.Streams.SupportedStreams)
            return 2;

        pPESBuf = &(pCnxt->TSCnxt.TempBufs.PESStreamBuf[i]);
        if (pPESBuf->pBuf == NULL) {
            U32 BufSize = 0;

            if (IsSupportedVideoStream(pCnxt->TSCnxt.Streams.StreamType[i]))
                BufSize = SIZEOF_PESVIDEOBUF;
            else if (IsSupportedAudioStream(pCnxt->TSCnxt.Streams.StreamType[i]))
                BufSize = SIZEOF_PESAUDIOBUF;
            else
                return 0; /* just skip, not report error  */

            pPESBuf->pBuf = LOCALMalloc(BufSize);
            if (NULL == pPESBuf->pBuf)
                return PARSER_INSUFFICIENT_MEMORY;
            pPESBuf->Size = BufSize;
            pPESBuf->Complete = 0;
            pPESBuf->PID = PID;
            pPESBuf->StreamNum = streamNumFromPID(pCnxt, PID);
        }
    }
    /*  */
    if (!PayloadStart) {
        if (0 == pPESBuf->Filled)
            return 0;
    }
    /*PES len is 0, how to determine the PES is complete or not  */
    if (PayloadStart) {
        if (PID == pPESBuf->PID)
            if (6 == pPESBuf->PESLen) {
                pPESBuf->Complete = 1;
                pPESBuf->PESLen = pPESBuf->Filled;
                return 16;
            }
    }

    if (PayloadStart) {
        pPESBuf->PID = PID;
        if ((0 != pPESBuf->Filled) || (0 != pPESBuf->PESLen))
            return 3;
    }

    if (PID != pPESBuf->PID)
        return 5;

    if ((Size + pPESBuf->Filled) <= pPESBuf->Size) {
        memcpy(pPESBuf->pBuf + pPESBuf->Filled, pBuf, Size);
        pPESBuf->Filled += Size;
    } else
        return 4;

    /*get the PES packet length  */
    if (0 == pPESBuf->PESLen) {
        if (pPESBuf->Filled >= 6) {
            pPESBuf->PESLen = (*(pBuf + 5)) | ((*(pBuf + 4)) << 8);
            pPESBuf->PESLen += 6;
        }
    }

    if (6 != pPESBuf->PESLen)
        if (pPESBuf->Filled >= pPESBuf->PESLen)
            pPESBuf->Complete = 1;

    return 0;
}

/*
Return 0 if success
Return 1 if PID not enabled
Return 2 if PID not supported one
Return 3 if the previous PES has not been cleared
Return 4 if the buffer is too small
Return 5 if the wrong buffer
Return 16 if the packet is not copied and previous PES is complete
*/
static U32 CopyPESStreamPacket(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuf,
                               U32 Size, U32 PID, U32 PayloadStart, U64 qwSyncOffset) {
    FSL_MPG_DEMUX_TS_BUFFER_T* pPESBuf;
    U32 streamNum;

    // video or audio
    {
        U32 i, BufSize;
        for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
            if (PID == pCnxt->TSCnxt.Streams.StreamPID[i])
                break;
        }
        if (i == pCnxt->TSCnxt.Streams.SupportedStreams)
            return 2;

        streamNum = streamNumFromPID(pDemuxer->pDemuxContext, PID);
        if (!pDemuxer->SystemInfo.Stream[streamNum].isEnabled)
            return 0;

        pPESBuf = &(pCnxt->TSCnxt.TempBufs.PESStreamBuf[i]);
        if (IsSupportedVideoStream(pCnxt->TSCnxt.Streams.StreamType[i])) {
            BufSize = SIZEOF_PESVIDEOBUF;
            ;
        } else if (IsSupportedAudioStream(pCnxt->TSCnxt.Streams.StreamType[i])) {
            BufSize = SIZEOF_PESAUDIOBUF;
        } else
            return 0; /* just skip, not report error  */

        if (pPESBuf->pBuf == NULL) {
            pPESBuf->pBuf = LOCALMalloc(BufSize);
            if (NULL == pPESBuf->pBuf)
                return PARSER_INSUFFICIENT_MEMORY;
            pPESBuf->Size = BufSize;
            pPESBuf->Complete = 0;
            pPESBuf->PID = PID;
            pPESBuf->lastPESOffset = 0;
            pPESBuf->StreamNum = streamNum;
            pPESBuf->newSegFlag = 0;
        }
    }

    /*  */
    if (!PayloadStart) {
        if (0 == pPESBuf->Filled)
            return 0;
    }
    /*PES len is 0, how to determine the PES is complete or not  */
    if (PayloadStart) {
        if (PID == pPESBuf->PID)
            if (6 == pPESBuf->PESLen) {
                pPESBuf->Complete = 1;
                pPESBuf->PESLen = pPESBuf->Filled;
                return 16;
            }
    }

    if (PayloadStart) {
        if (0 != pPESBuf->Filled) {
            if (pPESBuf->PESLen == 0) {  // in such case filled < 6, drop previous data
                pPESBuf->Filled = 0;
            } else if (pPESBuf->Filled >= pPESBuf->PESLen) {
                pPESBuf->Complete = 1;
            } else {  // last packet is not completed, drop it to avoid pontential risk in following
                      // parser operation
                pPESBuf->PESLen = 0;
                pPESBuf->Filled = 0;
            }
            return 16;
        }
    }

    if (PID != pPESBuf->PID)
        return 5;

    if ((Size + pPESBuf->Filled) <= pPESBuf->Size) {
        memcpy(pPESBuf->pBuf + pPESBuf->Filled, pBuf, Size);
        if (PayloadStart & FLAG_SAMPLE_NEWSEG)
            pPESBuf->newSegFlag |= FLAG_SAMPLE_NEWSEG;
        pPESBuf->Filled += Size;
    } else
        return 4;

    /*get the PES packet length  */
    if (0 == pPESBuf->PESLen) {
        if (pPESBuf->Filled >= 6) {
            pPESBuf->PESLen = (*(pBuf + 5)) | ((*(pBuf + 4)) << 8);
            pPESBuf->EXVI.isParsed = FALSE;
            if (0x02 == (*(pBuf + 6) >> 6) && (*(pBuf + 7) & 0x01)) {
                pPESBuf->EXVI.isParsed = EPSON_ReadEXVI(pBuf + 7, Size - 7, &(pPESBuf->EXVI));
                if (pPESBuf->EXVI.isParsed)
                    pPESBuf->PESLen = pPESBuf->EXVI.PESLength;
            }
            pPESBuf->PESLen += 6;
        }
        pPESBuf->qwOffset = qwSyncOffset;
    }

    if (6 < pPESBuf->PESLen) {
        if (pPESBuf->Filled >= pPESBuf->PESLen)
            pPESBuf->Complete = 1;
    }

    return 0;
}

/*
Return 0 if success
Return 1 if PID not enabled
Return 2 if PID not supported one
Return 3 if the previous PES has not been cleared
Return 4 if the buffer is too small
Return 5 if the wrong buffer
Return 16 if the packet is not copied and previous PES is complete
*/
static U32 CopyPESPacket(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuf, U32 Size,
                         U32 PID, U32 PayloadStart, U32 Index, U32 Offset) {
    FSL_MPG_DEMUX_TS_BUFFER_T* pPESBuf;

    if (0 == IS_PIDEnabled(pCnxt, (U16)PID))
        return 1;
    if (1 == pCnxt->TSCnxt.PAT.NonProgramSelected) {
        U32 i;
        for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
            if (PID == pCnxt->TSCnxt.Streams.StreamPID[i])
                break;
        }
        if ((i == pCnxt->TSCnxt.Streams.SupportedStreams) && (PayloadStart > 0)) {
            /*not detected yet   */
            U32 StreamID;
            if (Size >= 4) {
                StreamID = ((*pBuf) << 24) | ((*(pBuf + 1)) << 16) | ((*(pBuf + 2)) << 8) |
                           (*(pBuf + 3));
                if (IS_PES(StreamID)) {
                    StreamID = PS_ID(StreamID);
                    if (IS_AUDIO_PES(StreamID)) {
                        pCnxt->TSCnxt.Streams.StreamPID[i] = PID;
                        pCnxt->TSCnxt.Streams.StreamType[i] = MPEG2_AUDIO;
                        pCnxt->TSCnxt.Streams.SupportedStreams++;
                    } else if (IS_VIDEO_PES(StreamID)) {
                        pCnxt->TSCnxt.Streams.StreamPID[i] = PID;
                        pCnxt->TSCnxt.Streams.StreamType[i] = MPEG2_VIDEO;
                        pCnxt->TSCnxt.Streams.SupportedStreams++;
                    } else if (IS_PRIV1(StreamID)) {
                        pCnxt->TSCnxt.Streams.StreamPID[i] = PID;
                        pCnxt->TSCnxt.Streams.StreamType[i] = MPEG2_PRIVATEPES_DVB;
                        pCnxt->TSCnxt.Streams.SupportedStreams++;
                    } else {
                        MPG2_PARSER_TS_LOG("unknow stream id: 0x%X \r\n", StreamID);
                    }
                }
            }
        }
    }

    /*video or audio  */
    {
        U32 i, BufSize, streamNum;
        for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
            if (PID == pCnxt->TSCnxt.Streams.StreamPID[i])
                break;
        }
        if (i == pCnxt->TSCnxt.Streams.SupportedStreams)
            return 0;

        streamNum = streamNumFromPID(pCnxt, PID);
        pPESBuf = &(pCnxt->TSCnxt.TempBufs.PESStreamBuf[i]);
        if (IsSupportedVideoStream(pCnxt->TSCnxt.Streams.StreamType[i])) {
            BufSize = SIZEOF_PESVIDEOBUF;
            ;
        } else if (IsSupportedAudioStream(pCnxt->TSCnxt.Streams.StreamType[i])) {
            BufSize = SIZEOF_PESAUDIOBUF;
        } else
            return 0; /* just skip, not report error  */

        if (pPESBuf->pBuf == NULL) {
            pPESBuf->pBuf = LOCALMalloc(BufSize);
            if (NULL == pPESBuf->pBuf)
                return PARSER_INSUFFICIENT_MEMORY;
            pPESBuf->Size = BufSize;
            pPESBuf->Complete = 0;
            pPESBuf->PID = PID;
            pPESBuf->StreamNum = streamNum;
            pPESBuf->lastPESOffset = 0;
        }
    }

    if (!PayloadStart) {
        if (0 == pPESBuf->Filled)
            return 0;
    }

    /*PES len is 0, how to determine the PES is complete or not  */
    if (PayloadStart) {
        if (0 != pPESBuf->Filled) {
            if (pPESBuf->PESLen == 0) {  // in such case filled < 6, drop previous data
                pPESBuf->Filled = 0;
            } else if (pPESBuf->Filled >= pPESBuf->PESLen) {
                pPESBuf->Complete = 1;
            } else {  // last packet is not completed, drop it to avoid pontential risk in following
                      // parser operation
                pPESBuf->PESLen = 0;
                pPESBuf->Filled = 0;
            }
            return 16;
        }
    }

    if (PayloadStart) {
        pPESBuf->PID = PID;
        pPESBuf->IndexInputBuf = Index;
        pPESBuf->OffsetInputBuf = Offset;
        pPESBuf->Filled = 0;
        pPESBuf->PESLen = 0;
    }

    if (PID != pPESBuf->PID)
        return 5;

    if ((Size + pPESBuf->Filled) <= pPESBuf->Size) {
        memcpy(pPESBuf->pBuf + pPESBuf->Filled, pBuf, Size);
        pPESBuf->Filled += Size;
    } else
        return 4;

    /*get the PES packet length  */
    if (0 == pPESBuf->PESLen) {
        if (pPESBuf->Filled >= 6) {
            pPESBuf->PESLen = (*(pBuf + 5)) | ((*(pBuf + 4)) << 8);
            pPESBuf->EXVI.isParsed = FALSE;
            if (0x02 == (*(pBuf + 6) >> 6) && (*(pBuf + 7) & 0x01)) {
                pPESBuf->EXVI.isParsed = EPSON_ReadEXVI(pBuf + 7, Size - 7, &(pPESBuf->EXVI));
                if (pPESBuf->EXVI.isParsed)
                    pPESBuf->PESLen = pPESBuf->EXVI.PESLength;
            }
            pPESBuf->PESLen += 6;
        }
    }

    if (6 < pPESBuf->PESLen) {
        // fix ENGR00214164
        // ref to MPEG2_ParsePES_Process in Mpeg2ParserProcessFile,
        // if pTempBuf->Filled > pTempBuf->PESLen, still use pTempBuf->PESLen
        if (pPESBuf->Filled > pPESBuf->PESLen)
            pPESBuf->Filled = pPESBuf->PESLen;

        if (pPESBuf->Filled >= pPESBuf->PESLen)
            pPESBuf->Complete = 1;
    }

    return 0;
}

/*
return value:
0----success;
1----context buffer full;
2----discontinuity.

*/
U32 UpdateTSPacketCnxt(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 PID, U32 ContinuityCnt) {
    U32 i;

    if (0 == IS_PIDEnabled(pCnxt, PID)) {
        if (1 == pCnxt->TSCnxt.PAT.NonProgramSelected) {
            U32 i;
            for (i = 0; i < MAX_MPEG2_STREAMS; i++) {
                if (pCnxt->TSCnxt.Streams.ReorderedStreamPID[i] == PID)
                    return 0;
            }
            EnablePID(pCnxt, PID);
        } else
            return 0;
    }

    if (pCnxt->TSCnxt.Packets.ValidEntries > TS_PACKET_SUPPORT_MAX)
        return 1;

    for (i = 0; i < pCnxt->TSCnxt.Packets.ValidEntries; i++) {
        if (PID == pCnxt->TSCnxt.Packets.TSPackets[i].PID) {
            U32 OldCC;
            OldCC = pCnxt->TSCnxt.Packets.TSPackets[i].CC;
            pCnxt->TSCnxt.Packets.TSPackets[i].CC = (U8)ContinuityCnt;
            if (((OldCC + 1) & 0xF) == ContinuityCnt)
                return 0;
            else
                return 2;
        }
    }

    if (i == pCnxt->TSCnxt.Packets.ValidEntries) {
        if (i >= TS_PACKET_SUPPORT_MAX)
            return 1;
        pCnxt->TSCnxt.Packets.ValidEntries++;
    }

    pCnxt->TSCnxt.Packets.TSPackets[i].PID = (U16)PID;
    pCnxt->TSCnxt.Packets.TSPackets[i].CC = (U8)ContinuityCnt;

    return 0;
}

FSL_VOID ResetTSPacketCnxt(FSL_MPG_DEMUX_CNXT_T* pCnxt) {
    U32 i;

    for (i = 0; i < pCnxt->TSCnxt.Packets.ValidEntries; i++) {
        pCnxt->TSCnxt.Packets.TSPackets[i].PID = 0;
        pCnxt->TSCnxt.Packets.TSPackets[i].CC = 0;
    }

    pCnxt->TSCnxt.Packets.ValidEntries = 0;
}

/*
Return 0 if success
Return 1 if the section is not complete
Return 2 if no room for the PAT section
Retrun 3 if fatal error
Return 4 if programs contained is out of what can be supported
*/
U32 ParsePATSection(FSL_MPG_DEMUX_CNXT_T* pCnxt) {
    FSL_MPG_DEMUX_TS_BUFFER_T* pPATSectionBuf;
    FSL_MPG_DEMUX_PATSECTION_T* pPATSection;

    pPATSectionBuf = &(pCnxt->TSCnxt.TempBufs.PATSectionBuf);

    if (pPATSectionBuf->Filled > pPATSectionBuf->Size)
        return 3;
    if (0 == pPATSectionBuf->Complete)
        return 1;
    if (pCnxt->TSCnxt.PAT.Sections >= NO_PATSECTION_SUPPORT_MAX)
        return 2;

    pPATSection = &(pCnxt->TSCnxt.PAT.PATSection[pCnxt->TSCnxt.PAT.Sections]);
    pCnxt->TSCnxt.PAT.Sections++;
    /*start to parse  */
    {
        U32 i;
        U8* pTemp;
        int program_number, pmt_pid;

        pTemp = pPATSectionBuf->pBuf;

        /*check the table ID   */
        if (TID_PAT != (*pTemp))
            return 3;
        pTemp++;
        pPATSection->SectionLen = (*(pTemp + 1)) | (((*pTemp) & 0xF) << 8);

        /*check the section length  */
        if (pPATSection->SectionLen != (pPATSectionBuf->PESLen - 3))
            return 3;
        pTemp += 2;

        /*transport stream ID  */
        pPATSection->TSID = (*(pTemp + 1)) | ((*pTemp) << 8);
        pTemp += 2;

        /*version number; current or next indicator   */
        pPATSection->VersionNum = ((*pTemp) >> 1) & 0x1F;
        pPATSection->CurrentOrNext = (*pTemp) & 0x1;
        pTemp++;

        /*section number  */
        pPATSection->SectionNum = *pTemp;
        pTemp++;

        /*last section number  */
        pPATSection->LastSectionNum = *pTemp;
        pTemp++;

        i = 0;
        for (; pTemp < pPATSectionBuf->pBuf + pPATSectionBuf->PESLen - 4;) {
            if (i >= NO_PROGRAM_SUPPORT_MAX)
                return 4;
            program_number = (*(pTemp + 1)) | ((*pTemp) << 8);
            if (program_number < 0)
                break;
            pTemp += 2;
            pmt_pid = (*(pTemp + 1)) | (((*pTemp) & 0x1F) << 8);
            if (pmt_pid < 0)
                break;
            pTemp += 2;
            if (program_number == 0)
                continue;  // network pid
            pPATSection->ProgramNum[i] = program_number;
            pPATSection->PID_PMT[i] = pmt_pid;
            i++;
        }
        pPATSection->Programs = i;
    }

    /*clear the PAT section buffer after used   */
    pPATSectionBuf->Complete = 0;
    pPATSectionBuf->Filled = 0;
    pPATSectionBuf->PESLen = 0;

    return 0;
}

/*
Return 1 if the stream is supported by the demuxer
Return 0 if not
*/
U32 IsSupportedStream(U32 StreamType) {
    if ((MPEG1_AUDIO == StreamType) || (MPEG1_VIDEO == StreamType) || (MPEG2_AUDIO == StreamType) ||
        (MPEG2_VIDEO == StreamType) || (MPEG2_AAC == StreamType) ||
        (MPEG2_PRIVATEPES_ATSC == StreamType) || (MPEG2_PRIVATEPES_DVB == StreamType) ||
        (MPEG2_H264 == StreamType) || (MPEG2_HEVC == StreamType) || (MPEG4_VIDEO == StreamType) ||
        (AVS_VIDEO == StreamType) || (AAC_LATM == StreamType) || (MPEG2_EAC3 == StreamType) ||
        (DVB_AC4 == StreamType))
        return 1;
    else
        return 0;
}

/*
Return 1 if the stream is supported by the demuxer
Return 0 if not
*/
U32 IsSupportedVideoStream(U32 StreamType) {
    if ((MPEG1_VIDEO == StreamType) || (MPEG2_VIDEO == StreamType) || (MPEG2_H264 == StreamType) ||
        (MPEG2_HEVC == StreamType) || (AVS_VIDEO == StreamType) || (MPEG4_VIDEO == StreamType))
        return 1;
    else
        return 0;
}

/*
Return 1 if the stream is supported by the demuxer
Return 0 if not
*/
U32 IsSupportedAudioStream(U32 StreamType) {
    if ((MPEG1_AUDIO == StreamType) || (MPEG2_AUDIO == StreamType) || (MPEG2_AAC == StreamType) ||
        (MPEG2_PRIVATEPES_ATSC == StreamType) || (MPEG2_PRIVATEPES_DVB == StreamType) ||
        (AAC_LATM == StreamType) || (MPEG2_EAC3 == StreamType) || (DVB_AC4 == StreamType))
        return 1;
    else
        return 0;
}

U32 FindProgramBySupportedStream(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 dwSupportedStreamIdx,
                                 U32* pdwProgramIdx) {
    U32 p;
    U32 dwSupportedStreamIdxStart;

    if ((pCnxt == NULL) || (pdwProgramIdx == NULL)) {
        return -1;
    }

    if (dwSupportedStreamIdx >= pCnxt->TSCnxt.Streams.SupportedStreams) {
        return -1;
    }

    dwSupportedStreamIdxStart = 0;
    for (p = 0; p < pCnxt->TSCnxt.nPMTs; p++) {
        if ((dwSupportedStreamIdx >= dwSupportedStreamIdxStart) &&
            (dwSupportedStreamIdx <
             dwSupportedStreamIdxStart + pCnxt->TSCnxt.PMT[p].SupportedStreamNum)) {
            *pdwProgramIdx = p;
            return 0;
        }

        dwSupportedStreamIdxStart += pCnxt->TSCnxt.PMT[p].SupportedStreamNum;
    }

    return -1;
}

/*

*/
FSL_VOID ListSupportedStreams(FSL_MPG_DEMUX_CNXT_T* pCnxt) {
    U32 p, i, j, n;
    // keep consist with FSL_MPG_DEMUX_TS_STREAMS_T array member size, or memory access exception.
    U32 StreamPIDTemp[MAX_MPEG2_STREAMS];  // NO_PMTSECTION_SUPPORT_MAX*NO_STREAMSINPMT_SUPPORT_MAX];
    U32 StreamTypeTemp
            [MAX_MPEG2_STREAMS];  // NO_PMTSECTION_SUPPORT_MAX*NO_STREAMSINPMT_SUPPORT_MAX];

    n = 0;
    pCnxt->TSCnxt.Streams.SupportedStreams = 0;

    for (p = 0; p < pCnxt->TSCnxt.nPMTs; p++) {
        for (i = 0; i < pCnxt->TSCnxt.PMT[p].Sections; i++) {
            for (j = 0; j < pCnxt->TSCnxt.PMT[p].PMTSection[i].Streams; j++) {
                if (IsSupportedStream((U32)pCnxt->TSCnxt.PMT[p].PMTSection[i].StreamType[j])) {
                    // Cable_ch64.ts
                    if (n >= MAX_MPEG2_STREAMS) {
                        break;
                    }
                    StreamTypeTemp[n] = (U32)pCnxt->TSCnxt.PMT[p].PMTSection[i].StreamType[j];
                    StreamPIDTemp[n] = (U32)pCnxt->TSCnxt.PMT[p].PMTSection[i].StreamPID[j];
                    pCnxt->TSCnxt.PMT[p].SupportedStreamNum++;
                    n++;
                }
            }
        }
    }

    /*At this moment, 1 video and 1audio are supported   */
    /*associate PES buffer with stream PID   */
    /*video  */
    for (i = 0; i < n; i++) {
        if (IsSupportedStream((U32)(StreamTypeTemp[i]))) {
            pCnxt->TSCnxt.Streams.StreamPID[pCnxt->TSCnxt.Streams.SupportedStreams] =
                    StreamPIDTemp[i];
            pCnxt->TSCnxt.Streams.StreamType[pCnxt->TSCnxt.Streams.SupportedStreams] =
                    StreamTypeTemp[i];
            EnablePID(pCnxt, StreamPIDTemp[i]);
            pCnxt->TSCnxt.Streams.SupportedStreams++;
        }
    }
}

S32 ParsePMTDescriptor(U8* pDesc, U32 size, FSL_MPG_DEMUX_PMTSECTION_T* pPMTSection, U32 index) {
    S32 tag, len, i;
    S32 left_size = size;
    U8* p = pDesc;
    U32 code;

    do {
        U8* pData;
        if (left_size < 2) {
            return 0;
        }
        tag = *p++;
        len = *p++;
        left_size -= 2;
        if (left_size < len) {
            MPG2_PARSER_TS_LOG("data is not enough: tag: 0x%X, len: %d, left size: %d  \r\n", tag,
                               len, left_size);
            return 0;
        }
        MPG2_PARSER_TS_LOG("%s: stream index: %d, tag: 0x%X, len: %d, str: %s \r\n", __FUNCTION__,
                           index, tag, len, p);
        pData = p;
        switch (tag) {
            case 0x1E: /* SL descriptor */
                break;
            case 0x1F: /* FMC descriptor */
                break;
            case 0x56: /* DVB teletext descriptor */
                break;
            case 0x59: /* subtitling descriptor */
                break;
            case 0x0a: /* ISO 639 language descriptor */
                for (i = 0; (i + 4 <= len) && (i + 4 <= LANGUAGE_MAX_LEN); i += 4) {
                    pPMTSection->Language[index][i + 0] = *pData++;
                    pPMTSection->Language[index][i + 1] = *pData++;
                    pPMTSection->Language[index][i + 2] = *pData++;
                    pPMTSection->Language[index][i + 3] = ',';
                    pData++;  // skip audio byte: 0: Undefined; 1: Clean effects; 2:Hearing
                              // impaired; 3:Visual impaired commentary
                }
                if (i) {
                    pPMTSection->Language[index][i - 1] = 0;
                }
                MPG2_PARSER_TS_LOG("%s: ISO 639 language: %s \r\n", __FUNCTION__,
                                   pPMTSection->Language[index]);
                break;
            case 0x05: /* registration descriptor */
                if (len == 5 && pData[0] == 'H' && pData[1] == 'D' && pData[2] == 'C' &&
                    pData[3] == 'P')
                    pPMTSection->bHDCPEncrypted = TRUE;
                else if (pPMTSection->StreamType[index] == MPEG2_PRIVATEPES_DVB && len == 4 &&
                         pData[0] == 'H' && pData[1] == 'E' && pData[2] == 'V' && pData[3] == 'C')
                    pPMTSection->StreamType[index] = MPEG2_HEVC;
                break;
            case 0x52: /* stream identifier descriptor */
                break;
            case MPEG2_DESC_DVB_AC3: /* dvb ac3 descriptor */
                // fall through
            case MPEG2_DESC_DVB_ENHANCED_AC3: /* enhanced ac3 descriptor */
                // fall through
            case MPEG2_DESC_DVB_DTS: /* DTS descriptor */
                pPMTSection->StreamDescriptorTag[index] = tag;
                break;
            case 0x02: /*video stream descriptor*/
                code = (*pData >> 3) & 0x0F;
                switch (code) {
                    case 0x01:
                        pPMTSection->VideoFrameRate[index][0] = 24000;
                        pPMTSection->VideoFrameRate[index][1] = 1001;
                        break;
                    case 0x02:
                        pPMTSection->VideoFrameRate[index][0] = 24;
                        pPMTSection->VideoFrameRate[index][1] = 1;
                        break;
                    case 0x03:
                        pPMTSection->VideoFrameRate[index][0] = 25;
                        pPMTSection->VideoFrameRate[index][1] = 1;
                        break;
                    case 0x04:
                        pPMTSection->VideoFrameRate[index][0] = 30000;
                        pPMTSection->VideoFrameRate[index][1] = 1001;
                        break;
                    case 0x05:
                        pPMTSection->VideoFrameRate[index][0] = 30;
                        pPMTSection->VideoFrameRate[index][1] = 1;
                        break;
                    case 0x06:
                        pPMTSection->VideoFrameRate[index][0] = 50;
                        pPMTSection->VideoFrameRate[index][1] = 1;
                        break;
                    case 0x07:
                        pPMTSection->VideoFrameRate[index][0] = 60000;
                        pPMTSection->VideoFrameRate[index][1] = 1001;
                        break;
                    case 0x08:
                        pPMTSection->VideoFrameRate[index][0] = 60;
                        pPMTSection->VideoFrameRate[index][1] = 1;
                        break;
                    default:
                        pPMTSection->VideoFrameRate[index][0] = 0;
                        pPMTSection->VideoFrameRate[index][1] = 0;
                        break;
                }
                break;
            case DVB_RESERVED_MAX: /* The AC4 descriptor is used in the PSI PMT to identify streams
                                      which carry AC4 */
            {
                U32 descTagExt = 0;
                if (pPMTSection->StreamType[index] == MPEG2_PRIVATEPES_DVB ||
                    pPMTSection->StreamType[index] == DVB_AC4)
                    descTagExt = pData[0];

                pData++;
                pPMTSection->StreamDescriptorTag[index] = descTagExt;
                if (descTagExt == DVB_AUDIO_PRESELECTION) {
                    int num_preselections = (pData[0] >> 3);
                    int i, byteLeft = left_size;
                    for (i = 0; i < num_preselections; i++) {
                        bool language_code_present = FALSE;
                        if (byteLeft < 3)
                            break;
                        pPMTSection->AudioPresentation[index][i].presentationId = (pData[1] >> 3);
                        pPMTSection->AudioPresentation[index][i].masteringIndication =
                                (pData[1] & 0x7);
                        pPMTSection->AudioPresentation[index][i].audioDescriptionAvailable =
                                ((pData[2] & 0x80) > 0);
                        pPMTSection->AudioPresentation[index][i].spokenSubtitlesAvailable =
                                ((pData[2] & 0x40) > 0);
                        pPMTSection->AudioPresentation[index][i].dialogueEnhancementAvailable =
                                ((pData[2] & 0x20) > 0);

                        language_code_present = ((pData[2] & 0x8) > 0);
                        pData += 3;
                        byteLeft -= 3;
                        if (language_code_present) {
                            if (byteLeft < 3)
                                break;
                            pPMTSection->AudioPresentation[index][i].language[0] = pData[0];
                            pPMTSection->AudioPresentation[index][i].language[1] = pData[1];
                            pPMTSection->AudioPresentation[index][i].language[2] = pData[2];
                            pPMTSection->AudioPresentation[index][i].language[3] = 0;
                            pData += 3;
                            byteLeft -= 3;
                        }
                        pPMTSection->AudioPresentationNum[index]++;
                    }
                } else if (descTagExt == MPEG2_EXT_DESC_AC4) {
                    pPMTSection->StreamType[index] = DVB_AC4;
                }
            } break;
            default:
                break;
        }
        left_size -= len;
        p += len;
    } while (left_size > 0);
    return 1;
}

/*
Return 0 if success
Return 1 if the section is not complete
Return 2 if no room for the PMT section
Retrun 3 if fatal error
Return 4 if programs contained is out of what can be supported
Retuen 5 if PMT section is duplicated
Return 256 if PMT is updated according to version number
*/
U32 ParsePMTSection(FSL_MPG_DEMUX_CNXT_T* pCnxt, int index) {
    FSL_MPG_DEMUX_TS_BUFFER_T* pPMTSectionBuf;
    FSL_MPG_DEMUX_PMTSECTION_T* pPMTSection;
    U8 sectionIdx = pCnxt->TSCnxt.PMT[index].Sections;
    U32 i = 0, ret = 0;
    U8* pTemp;
    U32 Len = 0;

    pPMTSectionBuf = &(pCnxt->TSCnxt.TempBufs.PMTSectionBuf[index]);
    pTemp = pPMTSectionBuf->pBuf;

    if (pPMTSectionBuf->Filled > pPMTSectionBuf->Size)
        return 3;
    if (0 == pPMTSectionBuf->Complete)
        return 1;

    pPMTSection = &(pCnxt->TSCnxt.PMT[index].PMTSection[0]);
    /*start to parse  */
    do {
        /*check the table ID   */
        if (TID_PMT != (*pTemp)) {
            ret = 3;
            break;
        }
        pTemp++;

        if (sectionIdx >= NO_PMTSECTION_SUPPORT_MAX) {
            U8 versionNum = (pTemp[4] >> 1) & 0x1F;
            if (pPMTSection->VersionNum >= versionNum) {
                ret = 5;
                break;
            } else {
                ret = 256;
            }
        }
        pPMTSection->SectionLen = (*(pTemp + 1)) | (((*pTemp) & 0xF) << 8);

        /*check the section length  */
        if (pPMTSection->SectionLen != (pPMTSectionBuf->PESLen - 3)) {
            ret = 3;
            break;
        }
        pTemp += 2;

        /*program number */
        pPMTSection->ProgramNum = (*(pTemp + 1)) | ((*pTemp) << 8);
        /*check  disabled for multi program support*/

        pTemp += 2;

        /*version number; current or next indicator   */
        pPMTSection->VersionNum = ((*pTemp) >> 1) & 0x1F;
        pPMTSection->CurrentOrNext = (*pTemp) & 0x1;
        pTemp++;

        /*section number  */
        pPMTSection->SectionNum = *pTemp;
        pTemp++;

        /*last section number  */
        pPMTSection->LastSectionNum = *pTemp;
        pTemp++;

        /*PCR_PID   */
        pPMTSection->PCR_PID = (*(pTemp + 1)) | (((*pTemp) & 0x1F) << 8);
        pTemp += 2;

        /*program info length  */
        Len = (*(pTemp + 1)) | (((*pTemp) & 0xF) << 8);
        pTemp += 2;

        /* descriptor */
        ParsePMTDescriptor(pTemp, Len, pPMTSection, 0);

        pTemp += Len;

        Len = pPMTSection->SectionLen - Len - 9 - 4;

        while ((Len > 0) && (i < NO_STREAMSINPMT_SUPPORT_MAX)) {
            U32 ESInfoLen = 0;
            pPMTSection->StreamType[i] = *pTemp;
            pTemp++;
            pPMTSection->StreamPID[i] = (*(pTemp + 1)) | (((*pTemp) & 0x1F) << 8);
            pTemp += 2;
            ESInfoLen = (*(pTemp + 1)) | (((*pTemp) & 0xF) << 8);
            pTemp += 2;
            ParsePMTDescriptor(pTemp, ESInfoLen, pPMTSection, i);
            pTemp += ESInfoLen;
            Len -= (ESInfoLen + 5);
            MPG2_PARSER_TS_LOG("stream ID: 0x%X, type: 0x%X \r\n", pPMTSection->StreamPID[i],
                               pPMTSection->StreamType[i]);
            i++;
        }

        pPMTSection->Streams = i;
        pCnxt->TSCnxt.PCRInfo[pCnxt->TSCnxt.nParsedPMTs].PID = pPMTSection->PCR_PID;
        pCnxt->TSCnxt.PCRInfo[pCnxt->TSCnxt.nParsedPMTs].programNum = pPMTSection->ProgramNum;
        pCnxt->TSCnxt.PMT[index].Sections = sectionIdx + 1;
    } while (0);

    if (pPMTSection->bHDCPEncrypted)
        pCnxt->dataEncrypted = TRUE;

    /*clear the PAT section buffer after used   */
    pPMTSectionBuf->Complete = 0;
    pPMTSectionBuf->Filled = 0;
    pPMTSectionBuf->PESLen = 0;

    return ret;
}

/*
return value:
0----success;
1----Error in stream;
2----Discontinuity occurs or there is no available TS packet context buffer.
3----No room left in PES temp buffer.
4----error when copy audio/video Packets.
16---The current packet is not copied into PES buffer, please try in next round.
*/
U32 ParseTSStreamPacket(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuffer,
                        U16 streamID) {
    U8* pTemp;
    U32 PayloadStart = 0;
    U32 PID;

    U32 ScarmblingCtrl = 0;
    U32 AdaptationCtrl = 0;
    U32 ContinuityCnt = 0;
    U32 AdaptationBytes = 0;
    S32 PayloadBytes = 0;
    U32 Ret;
    U32 streamNum = 0;
    U32 selectedStreamNum = 0;
    U64 qwSyncOffset = 0;
    bool isPCR_PID = FALSE;
    bool isPMT_PID = FALSE;
    U32 index = 0;

    if (pCnxt->TSCnxt.hasTimeCode) {
        if (MPEG2FilePos(pDemuxer, 0) >= TS_PACKET_LENGTH + 4)
            qwSyncOffset = MPEG2FilePos(pDemuxer, 0) - TS_PACKET_LENGTH - 4;
        else
            return 1;
    } else {
        if (MPEG2FilePos(pDemuxer, 0) >= TS_PACKET_LENGTH)
            qwSyncOffset = MPEG2FilePos(pDemuxer, 0) - TS_PACKET_LENGTH;
        else
            return 1;
    }
    pTemp = pBuffer;

    if (pCnxt->TSCnxt.hasTimeCode)
        pTemp += 4;

#ifdef DEMUX_DEBUG
    assert(TS_SYNC_BYTE == (*pTemp));
#endif
    pTemp++;

    /*temporarily disable, it should be enable I think   */
#if 0
    if ((*pTemp) & 0x80) //error
        return 1;
#endif
    PayloadStart = (*pTemp) & 0x40;

    PID = (*(pTemp + 1)) | (((*pTemp) & 0x1F) << 8);
    if (PID > 0) {
        for (index = 0; index < pCnxt->TSCnxt.nPMTs; index++) {
            if (PID == pCnxt->TSCnxt.TempBufs.PMTSectionBuf[index].PID) {
                isPMT_PID = TRUE;
                break;
            }
        }
    }

    streamNum = streamNumFromPID(pDemuxer->pDemuxContext, PID);
    if (pCnxt->TSCnxt.bOutputPCR && IS_PIDEnabled(pCnxt, (U16)PID)) {
        U32 i;
        for (i = 0; i < pCnxt->TSCnxt.nParsedPMTs; i++) {
            if (PID == pCnxt->TSCnxt.PCRInfo[i].PID) {
                isPCR_PID = TRUE;
                break;
            }
        }
    }
    if (streamNum == (U32)(-1)) {
        if (isPCR_PID || isPMT_PID)
            goto PARSE_TS_PACKET;
        else
            return 0;  // packet with unknown PID, skip it
    }
    selectedStreamNum = streamNumFromStreamId(pDemuxer, streamID, 1);
    if (selectedStreamNum == (U32)(-1)) {
        selectedStreamNum = streamNum;  // for file mode, the parameter 'streamID' is meaningless,
                                        // just ignore it
        streamID = pDemuxer->SystemInfo.Stream[streamNum].streamId;
    }

    if (!pDemuxer->SystemInfo.Stream[streamNum].isEnabled)
        return 0;
    if (PS_ID(pDemuxer->SystemInfo.Stream[streamNum].streamId) != PS_ID(streamID) &&
        pDemuxer->outputMode == OUTPUT_BYTRACK) {
        if (pDemuxer->SystemInfo.Stream[streamNum].isBlocked)
            return 0;
        if (pDemuxer->SystemInfo.Stream[selectedStreamNum].isBlocked)
            return 0;
    }

PARSE_TS_PACKET:

    pTemp += 2;
    ScarmblingCtrl = ((*pTemp) & 0xC0) >> 6;
    if (ScarmblingCtrl != 0) {
        return 0;
    }
    AdaptationCtrl = ((*pTemp) & 0x30) >> 4;
    ContinuityCnt = (*pTemp) & 0xF;
    pTemp++;
    Ret = UpdateTSPacketCnxt(pCnxt, PID, ContinuityCnt);
#if 0
    if (0!=Ret)
        return 2;
#endif
    PayloadBytes = TS_PACKET_LENGTH - 4;
    /*adaptation field  */
    if ((0x2 == AdaptationCtrl) || (0x3 == AdaptationCtrl)) {
        AdaptationBytes = (U32)(*pTemp);
        if (AdaptationBytes > 0) {
            U32 discontinuity_indicator = ((*(pTemp + 1)) >> 7);
            U32 random_access_indicator = ((*(pTemp + 1)) >> 6) & 0x1;
            U32 PCR_flag = (*(pTemp + 1) >> 4) & 0x01;
            if (discontinuity_indicator) {
                if (!PayloadStart)
                    PayloadStart = 1;
                PayloadStart |= FLAG_SAMPLE_NEWSEG;
            }
            if ((random_access_indicator == 1) && streamNum != (U32)(-1) &&
                (FSL_MPG_DEMUX_VIDEO_STREAM ==
                 pDemuxer->SystemInfo.Stream[streamNum].enuStreamType))
                pDemuxer->random_access = 1;
            if (isPCR_PID && PCR_flag) {
                U64 PCR, PCR_base, PCR_ext;
                U8* ptr = pTemp + 2;
                FSL_MPG_DEMUX_TS_PCR_INFO_T* pPCRInfo = pCnxt->TSCnxt.PCRInfo;
                U32 programNum = programNumFromPID(pCnxt, PID);

                PCR_base = ((U64)ptr[0] << 25) | (ptr[1] << 17) | (ptr[2] << 9) | (ptr[3] << 1) |
                           (ptr[4] >> 7);
                PCR_ext = ((ptr[4] & 0x01) << 8) | ptr[5];
                PCR = PCR_base * 300 + PCR_ext;

                if (programNum < pCnxt->TSCnxt.nParsedPMTs && pPCRInfo[programNum].PCR != PCR) {
                    pPCRInfo[programNum].PCR = PCR;
                    pCnxt->TSCnxt.bGetPCR = TRUE;
                }
            }
        }
        pTemp += (AdaptationBytes + 1);
        /*At this moment we just skip the adaptation field
        If any information is to be used, parse the adaptation field*/
        PayloadBytes -= (AdaptationBytes + 1);
    }

    if (PayloadBytes > 0) {
        /* PMT */
        if (isPMT_PID) {
            if (PayloadStart) {
                U32 PointerField = *pTemp;
                pTemp++;
                PayloadBytes--;
                // fix engr213303^M
                if ((U32)PayloadBytes <= PointerField) {
                    return 0;
                }
                pTemp += PointerField;
                PayloadBytes -= PointerField;
            }

            /* monitor if PMT is changed */
            if (memcmp(pTemp, pCnxt->TSCnxt.TempBufs.PMTSectionBuf[index].pBuf, PayloadBytes) !=
                0) {
                MPG2_PARSER_TS_LOG("PMT changed\n");
                Ret = CopyPMTPacket(pCnxt, pTemp, PayloadBytes, PayloadStart, index);
                if (0 == Ret && 1 == pCnxt->TSCnxt.TempBufs.PMTSectionBuf[index].Complete) {
                    // PMT is updated, so reset previous parsed PMT section
                    U32 i = 0;
                    FSL_MPG_DEMUX_PMTSECTION_T* pPMTSection = NULL;
                    pCnxt->TSCnxt.PMT[index].Sections--;
                    pPMTSection = &(
                            pCnxt->TSCnxt.PMT[index].PMTSection[pCnxt->TSCnxt.PMT[index].Sections]);
                    memset(pPMTSection, 0, sizeof(FSL_MPG_DEMUX_PMTSECTION_T));

                    if (ParsePMTSection(pCnxt, index) != 0)
                        return 0;

                    for (i = 0; i < pDemuxer->SystemInfo.uliNoStreams; i++) {
                        if (pDemuxer->SystemInfo.Stream[i].enuStreamType ==
                                    FSL_MPG_DEMUX_AUDIO_STREAM &&
                            pDemuxer->SystemInfo.Stream[i]
                                            .MediaProperty.AudioProperty.enuAudioType ==
                                    FSL_MPG_DEMUX_AC4_AUDIO)
                            pDemuxer->SystemInfo.Stream[i].isAudioPresentationChanged = TRUE;
                    }
                }
            }
        }
        /*PES  */
        else if (0)  // 1 (pDemuxer->random_access==0)
        {
            MPG2_PARSER_TS_LOG("skip %d \r\n", (int)PayloadBytes);
        } else {
            Ret = CopyPESStreamPacket(pDemuxer, pCnxt, pTemp, PayloadBytes, PID, PayloadStart,
                                      qwSyncOffset);
            if (16 == Ret)
                return 16;
            else if (0 != Ret)
                return 4;
        }
    }
    return 0;
}

/*
return value:
0----success;
1----Error in stream;
2----Discontinuity occurs or there is no available TS packet context buffer.
3----No room left in PES temp buffer.
4----error when copy audio/video Packets.
16---The current packet is not copied into PES buffer, please try in next round.
*/
U32 ParseTSPacket(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt, U8* pBuffer, U32 Index) {
    U8* pTemp;
    U32 PayloadStart = 0;
    U32 PID;
    U32 ScarmblingCtrl = 0;
    U32 AdaptationCtrl = 0;
    U32 ContinuityCnt = 0;
    U32 AdaptationBytes = 0;
    U32 PayloadBytes = 0;
    U32 Ret;
    U32 Offset = 0;

    pTemp = pBuffer;
    if (pCnxt->TSCnxt.hasTimeCode)
        pTemp += 4;
#ifdef DEMUX_DEBUG
    assert(TS_SYNC_BYTE == (*pTemp));
#endif
    pTemp++;

    /*temporarily disable, it should be enable I think   */
#if 0
    if ((*pTemp) & 0x80) //Error
        return 1;
#endif
    PayloadStart = (*pTemp) & 0x40;

    PID = (*(pTemp + 1)) | (((*pTemp) & 0x1F) << 8);
    pTemp += 2;
    ScarmblingCtrl = ((*pTemp) & 0xC0) >> 6;
    if (ScarmblingCtrl != 0) {
        return 0;
    }
    AdaptationCtrl = ((*pTemp) & 0x30) >> 4;
    ContinuityCnt = (*pTemp) & 0xF;
    pTemp++;

    Ret = UpdateTSPacketCnxt(pCnxt, PID, ContinuityCnt);
#if 0
    if (0!=Ret)
        return 2;
#endif
    PayloadBytes = TS_PACKET_LENGTH - 4;
    /*adaptation field  */
    if ((0x2 == AdaptationCtrl) || (0x3 == AdaptationCtrl)) {
        AdaptationBytes = (U32)(*pTemp);
        pTemp += (AdaptationBytes + 1);
        /*At this moment we just skip the adaptation field
        If any information is to be used, parse the adaptation field*/
        if (PayloadBytes > (AdaptationBytes + 1))
            PayloadBytes -= (AdaptationBytes + 1);
        else
            PayloadBytes = 0;
    }

    if (PayloadBytes > 0) {
        if (IS_PIDEnabled(pCnxt, PID)) {
            /*PAT   */
            if (PID == PID_PAT) {
                if (PayloadStart) {
                    U32 PointerField = 0;
                    PointerField = *pTemp;
                    pTemp++;
                    PayloadBytes--;
                    // fix engr213303
                    if (PayloadBytes <= PointerField) {
                        return 0;
                    }
                    pTemp += PointerField;
                    PayloadBytes -= PointerField;
                }
                Ret = CopyPATPacket(pCnxt, pTemp, PayloadBytes, PayloadStart);
                if (0 != Ret)
                    return 3;
            }

            /* PMT */
            if (PID > 0) {
                U32 i;

                for (i = 0; i < pCnxt->TSCnxt.nPMTs; i++) {
                    if (PID == pCnxt->TSCnxt.TempBufs.PMTSectionBuf[i].PID) {
                        if (PayloadStart) {
                            U32 PointerField = 0;
                            PointerField = *pTemp;
                            pTemp++;
                            PayloadBytes--;
                            // fix engr213303
                            if (PayloadBytes <= PointerField) {
                                return 0;
                            }
                            pTemp += PointerField;
                            PayloadBytes -= PointerField;
                        }
                        Ret = CopyPMTPacket(pCnxt, pTemp, PayloadBytes, PayloadStart, i);
                        return 0;
                    }
                }
            }

            /*PES  */
            // else

            {
                Ret = CopyPESPacket(pDemuxer, pCnxt, pTemp, PayloadBytes, PID, PayloadStart, Index,
                                    Offset);
                if (16 == Ret)
                    return 16;
                else if (0 != Ret)
                    return 4;
            }
        }
    }
    return 0;
}

MPEG2_PARSER_ERROR_CODE MPEG2ParserGetPSI(MPEG2ObjectPtr pDemuxer) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt;
    U8* pInput;
    MPEG2_PARSER_ERROR_CODE Err = PARSER_SUCCESS;
    U32 Offset = 0;
    U32 SyncOffset = 0;
    U32 timecodeOffset = 0;
    U32 errCount = 0;
    U32 Ret;
    bool hasTimeCode = FALSE;

    if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, MIN_TS_STREAM_LEN)))
        goto bail;

    /*get the input buffer context*/
    pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;
    if (0 == pCnxt->TSCnxt.Synced) {
        Ret = TSSync(pInput + Offset, MIN_TS_STREAM_LEN, &SyncOffset, &hasTimeCode, 0);
        pCnxt->TSCnxt.hasTimeCode = hasTimeCode;
        pCnxt->TSCnxt.Synced = 1;
        /*not TS, assume it is PS/PES   */
        if ((0 != Ret) || (SyncOffset >= TS_PACKET_LENGTH)) {
            pDemuxer->TS_PSI.IsTS = 0;
            goto bail;
        }
        if (pCnxt->TSCnxt.hasTimeCode)
            timecodeOffset = 4;
        /*TS, the first call*/
        if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0,
                                           (MIN_TS_STREAM_LEN - SyncOffset + timecodeOffset))))
            goto bail;

        pDemuxer->TS_PSI.IsTS = 1;

        /*allocate memory for temp buffers   */
        pCnxt->TSCnxt.TempBufs.TSTempBuf.pBuf = LOCALMalloc(SIZEOF_TSTEMPBUF);
        if (NULL == pCnxt->TSCnxt.TempBufs.TSTempBuf.pBuf)
            return PARSER_INSUFFICIENT_MEMORY;
        pCnxt->TSCnxt.TempBufs.TSTempBuf.Size = SIZEOF_TSTEMPBUF;

        pCnxt->TSCnxt.TempBufs.PATSectionBuf.pBuf = LOCALMalloc(SIZEOF_PATSECTIONBUF);
        if (NULL == pCnxt->TSCnxt.TempBufs.PATSectionBuf.pBuf)
            return PARSER_INSUFFICIENT_MEMORY;
        pCnxt->TSCnxt.TempBufs.PATSectionBuf.Size = SIZEOF_PATSECTIONBUF;

        /*
        pCnxt->TSCnxt.TempBufs.PMTSectionBuf.pBuf= LOCALMalloc(SIZEOF_PMTSECTIONBUF);
        if (NULL==pCnxt->TSCnxt.TempBufs.PMTSectionBuf.pBuf)
        return PARSER_INSUFFICIENT_MEMORY;
        pCnxt->TSCnxt.TempBufs.PMTSectionBuf.Size=SIZEOF_PMTSECTIONBUF;
        */

        /*
        pCnxt->TSCnxt.TempBufs.PESVideoBuf.pBuf= LOCALMalloc(SIZEOF_PESVIDEOBUF);
        if (NULL==pCnxt->TSCnxt.TempBufs.PESVideoBuf.pBuf)
        return PARSER_INSUFFICIENT_MEMORY;
        pCnxt->TSCnxt.TempBufs.PESVideoBuf.Size=SIZEOF_PESVIDEOBUF;


        pCnxt->TSCnxt.TempBufs.PESAudioBuf.pBuf= LOCALMalloc(SIZEOF_PESAUDIOBUF);
        if (NULL==pCnxt->TSCnxt.TempBufs.PESAudioBuf.pBuf)
        return PARSER_INSUFFICIENT_MEMORY;
        pCnxt->TSCnxt.TempBufs.PESAudioBuf.Size=SIZEOF_PESAUDIOBUF;
        */

        /*add PID for PAT to PID list  */
        Ret = EnablePID(pCnxt, PID_PAT);
        if (0 != Ret)
            return PARSER_ERR_UNKNOWN;

    } else {
        if (0 == pDemuxer->TS_PSI.IsTS) {
            Err = PARSER_SUCCESS;
            goto bail;
        }
    }

    /*if already parse PAT, directly return success   */
    if (1 == pCnxt->TSCnxt.PAT.Parsed)
        goto bail;

TS_PARSE_PSI:
    /*Direct parse TS packet from input buffer if there is no buffer switch   */
    /*if come to here, start parse PSI   */
    if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, TS_PACKET_LENGTH + timecodeOffset))) {
        // no PAT found
        DisablePID(pCnxt, PID_PAT);
        Err = PARSER_SUCCESS;
        goto bail;
    }

    /* check if need to resync ts packet */
    if (pInput[0 + timecodeOffset] != TS_SYNC_BYTE) {
        U32 Offset = 0;

        if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0, TS_PACKET_LENGTH + timecodeOffset)))
            goto bail;

        if ((Err = MPEG2ParserReadBuffer(pDemuxer, 0, &pInput, MIN_TS_STREAM_LEN)))
            goto bail;

        hasTimeCode = pCnxt->TSCnxt.hasTimeCode;
        if (TSSync(pInput, MIN_TS_STREAM_LEN, &Offset, &hasTimeCode, 0) != 0) {
            if (errCount < MPEG2_MAX_ERR_TIMES) {
                errCount++;
                goto TS_PARSE_PSI;
            } else {
                Err = PARSER_ERR_INVALID_MEDIA;
                goto bail;
            }
        } else if (Offset < MIN_TS_STREAM_LEN) {
            if (pCnxt->TSCnxt.hasTimeCode)
                timecodeOffset = 4;
            if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0,
                                               MIN_TS_STREAM_LEN - Offset + timecodeOffset)))
                goto bail;
            goto TS_PARSE_PSI;
        }
    }

    Ret = ParseTSPacket(pDemuxer, pCnxt, pInput, 0);
    if (0 != Ret) {
        if (16 == Ret) {
            if ((Err = MPEG2ParserRewindNBytes(pDemuxer, 0, TS_PACKET_LENGTH + timecodeOffset)))
                return Err;

        } else {
            Err = PARSER_ERR_UNKNOWN;
            goto bail;
        }
    }

    /*check if the PAT section is ready for parsing  */
    if (1 == pCnxt->TSCnxt.TempBufs.PATSectionBuf.Complete) {
        Ret = ParsePATSection(pCnxt);
        if (0 != Ret) {
            Err = PARSER_ERR_UNKNOWN;
            goto bail;
        }
        /*check if the PAT sections are all parsed   */

        if (pCnxt->TSCnxt.PAT.PATSection[pCnxt->TSCnxt.PAT.Sections - 1].SectionNum ==
            pCnxt->TSCnxt.PAT.PATSection[pCnxt->TSCnxt.PAT.Sections - 1].LastSectionNum) {
            /*make the PAT transparent   */
            PassOutPAT(pDemuxer);
            pCnxt->TSCnxt.PAT.Parsed = 1;
            /*save context and return  */
            return PARSER_SUCCESS;
        }
    }

    goto TS_PARSE_PSI;

bail:
    MPEG2FileSeek(pDemuxer, 0, 0, SEEK_SET);
    return Err;
}

/*
Return 0 if success;
Return 1 if not enough room.
*/
U32 PassOutPAT(MPEG2ObjectPtr pDemuxer) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt;
    U32 i;
    U32 j;
    U32 Index = 0;

    pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;

    for (i = 0; i < pCnxt->TSCnxt.PAT.Sections; i++) {
        for (j = 0; j < pCnxt->TSCnxt.PAT.PATSection[i].Programs; j++) {
            if (Index >= NO_PROGRAM_SUPPORT_MAX) {
                /*do not regard this as fatal error  */
                pDemuxer->TS_PSI.Programs = NO_PROGRAM_SUPPORT_MAX;
                return 1;
            }
            pDemuxer->TS_PSI.ProgramNumber[Index] = pCnxt->TSCnxt.PAT.PATSection[i].ProgramNum[j];
            Index++;
        }
    }
    pDemuxer->TS_PSI.Programs = Index;
    return 0;
}

/*
Return 0 if success;
Return 1 for wrong program number

*/

/*
U32 GetPIDForPMT(FSL_MPG_DEMUX_CNXT_T  *pCnxt)
{
U32 i,j;
FSL_MPG_DEMUX_PAT_T *pPAT;

pPAT=&(pCnxt->TSCnxt.PAT);

if (0==pPAT->SelectedPNum)
return 1;
for (i=0;i<pPAT->Sections;i++)
for (j=0;j<pPAT->PATSection[i].Programs;j++)
{
if (pPAT->SelectedPNum==pPAT->PATSection[i].ProgramNum[j])
{
pPAT->SelectedPPMTPID=pPAT->PATSection[i].PID_PMT[j];
return 0;
}
}

return 1;
}
*/

U32 GetTypeFromPID(FSL_MPG_DEMUX_CNXT_T* pCnxt, U32 PID) {
    U32 i;

    for (i = 0; i < pCnxt->TSCnxt.Streams.SupportedStreams; i++) {
        if (PID == pCnxt->TSCnxt.Streams.StreamPID[i])
            return pCnxt->TSCnxt.Streams.StreamType[i];
    }

    return 0;
}

int SetTempStreamBuffer(MPEG2ObjectPtr pDemuxer, U8* pBuf, U32 size, U32 PID) {
    STREAM_BUFFER_T* pTempBuf = &(pDemuxer->SystemInfo.TempStreamBuf);
    bool ret = 0;

    if (!pTempBuf->pBuf) {
        pTempBuf->pBuf = (U8*)LOCALMalloc(TEMP_BUFFER_SIZE);
        if (!pTempBuf->pBuf)
            return ret;
        pTempBuf->nSize = pTempBuf->nPESNum = 0;
        pTempBuf->PID = PID;
    }
    if (PID != pTempBuf->PID)
        return ret;
    if (pTempBuf->nSize + size > TEMP_BUFFER_SIZE)
        size = TEMP_BUFFER_SIZE - pTempBuf->nSize;
    memcpy(pTempBuf->pBuf + pTempBuf->nSize, pBuf, size);
    pTempBuf->nSize += size;
    pTempBuf->nPESNum++;
    if (pTempBuf->nPESNum >= MIN_PES_NUM)
        ret = 1;

    return ret;
}

void FreeTempStreamBuffer(MPEG2ObjectPtr pDemuxer) {
    STREAM_BUFFER_T* pTempBuf = &(pDemuxer->SystemInfo.TempStreamBuf);
    if (pTempBuf->pBuf) {
        LOCALFree(pTempBuf->pBuf);
        pTempBuf->pBuf = NULL;
    }
    pTempBuf->PID = pTempBuf->nSize = pTempBuf->nPESNum = 0;
}

void FillMediaStreamInfo(MPEG2ObjectPtr pDemuxer, U32 streamNum, U32 streamID, U32 streamPID,
                         void* streamHeader, U32 isVideo, U8* pCodecDataBuf, U32 nCodecDataSize) {
    FSL_MPG_DEMUX_CNXT_T* pCnxt = (FSL_MPG_DEMUX_CNXT_T*)pDemuxer->pDemuxContext;
    FSL_MPEGSTREAM_T* pStream = &(pDemuxer->SystemInfo.Stream[streamNum]);

    pCnxt->Media.StreamID[streamNum] = streamID;
    pCnxt->Media.SysInfoFound[streamNum] = 0x55;

    pStream->streamId = streamID;
    pStream->streamPID = streamPID;
    pStream->streamNum = streamNum;
    pStream->uliPropertyValid = 1;
    pStream->enuStreamType = isVideo ? FSL_MPG_DEMUX_VIDEO_STREAM : FSL_MPG_DEMUX_AUDIO_STREAM;

    if (isVideo) {
        FSL_MPG_DEMUX_VSHeader_T* pHeader = (FSL_MPG_DEMUX_VSHeader_T*)streamHeader;
        FSL_VIDEO_PROPERTY_T* pVideoProp = &(pStream->MediaProperty.VideoProperty);

        pCnxt->Media.VideoInfoFound = 1;
        pVideoProp->enuVideoType = pHeader->enuVideoType;
        pVideoProp->uliVideoWidth = pHeader->HSize;
        pVideoProp->uliVideoHeight = pHeader->VSize;
        pVideoProp->uliVideoBitRate = pHeader->BitRate;
        pVideoProp->uliFRNumerator = pHeader->FRNumerator;
        pVideoProp->uliFRDenominator = pHeader->FRDenominator;
        pVideoProp->uliScanType = pHeader->ScanType;
        pStream->maxPTSDelta = calculate_PTS_delta_threshold(pVideoProp->uliFRNumerator,
                                                             pVideoProp->uliFRDenominator);
    } else {  // is audio
        FSL_MPG_DEMUX_AHeader_T* pHeader = (FSL_MPG_DEMUX_AHeader_T*)streamHeader;
        FSL_AUDIO_PROPERTY_T* pAudioProp = &(pStream->MediaProperty.AudioProperty);

        pCnxt->Media.AudioInfoFound += 1;
        pAudioProp->enuAudioChannelMode = pHeader->ChannelMode;
        pAudioProp->uliAudioBitRate = pHeader->BitRate;
        pAudioProp->uliAudioSampleRate = pHeader->SampleRate;
        pAudioProp->usiAudioChannels = pHeader->Channels;
        pAudioProp->usiAudioBitsPerSample = (U16)pHeader->BitsPerSample;
        pAudioProp->enuAudioType = pHeader->enuAudioType;
    }

    pCnxt->TSCnxt.Streams.ReorderedStreamPID[streamNum] = streamPID;
    if (streamNum == pCnxt->Media.NoMediaFound) {
        pCnxt->Media.NoMediaFound++;
        pDemuxer->SystemInfo.uliNoStreams++;
    }

    if (pCodecDataBuf && nCodecDataSize > 0) {
        if (pStream->codecSpecInformation == NULL) {
            pStream->codecSpecInformation = LOCALMalloc(nCodecDataSize);
            if (pStream->codecSpecInformation) {
                memcpy(pStream->codecSpecInformation, pCodecDataBuf, nCodecDataSize);
                pStream->codecSpecInfoSize = nCodecDataSize;
            }
        }
    }
}

/*
Retrun 0 if success
Return 1 if fatal error
*/
U32 ParsePES_Probe(MPEG2ObjectPtr pDemuxer, FSL_MPG_DEMUX_CNXT_T* pCnxt,
                   FSL_MPG_DEMUX_TS_BUFFER_T* pPESbuf, U32 streamIdx, bool isTs) {
    U8* pTemp;
    U32 PESLen = 0;
    U32 StreamID = 0;
    U32 TempWord = 0;
    U32 i;
    U32 SubStreamID = 0, AStreamID = 0;
    U32 StreamType = 0;
    U32 localOffset = 0;
    U32 byteValue = 0;
    (void)streamIdx;

    if (isTs) {
        StreamType = GetTypeFromPID(pCnxt, pPESbuf->PID);
        MPG2_PARSER_TS_LOG("stream type: %d \r\n", StreamType);
    }
    pTemp = pPESbuf->pBuf;
    if (pPESbuf->PESLen == 0) {
        return 0;
    }
    if (pPESbuf->PESLen < 6)
        return 1;

    if (pPESbuf->PESLen < pPESbuf->Filled)
        pPESbuf->PESLen = pPESbuf->Filled;
    /*get the sync word  */
    TempWord = NextNBufferBytes(pTemp, 4, &localOffset);

    if (0 == IS_SC(TempWord))
        return 1;
    StreamID = PS_ID(TempWord);

    /*get PES length  */
    PESLen = NextNBufferBytes(pTemp, 2, &localOffset);
    if (0 != PESLen) {
        if (PESLen != (pPESbuf->PESLen - 6))
            return 1;
    } else {
        PESLen = pPESbuf->PESLen - 6;
    }

    /* some streams don't have PES header, skip parsing and clear the buf and return 0 */
    if (IS_NO_PES_HEADER(StreamID))
        goto bail;

    /*parse the PES header  */
    while (localOffset < pPESbuf->PESLen) {
        byteValue = NextNBufferBytes(pTemp, 1, &localOffset);
        if (0xFF != byteValue)
            break;
    };

    if (localOffset == pPESbuf->PESLen)
        goto bail;

    if (0x01 == (byteValue >> 6)) {
        byteValue = NextNBufferBytes(pTemp, 1, &localOffset);
        byteValue = NextNBufferBytes(pTemp, 1, &localOffset);
    }
    if (0x02 == (byteValue >> 4)) {
        localOffset += 4;
    } else if (0x03 == (byteValue >> 4)) {
        localOffset += 9;
    } else if (0x02 == (byteValue >> 6)) {
        S32 HdrLen = 0;
        U8 PTS_DTS_flags, PES_extension_flag;

        if ((byteValue & 0x01) && !pPESbuf->EXVI.isParsed) {
            // In Case there is not enougth buffer when the first TS packet is copied to PES
            pPESbuf->EXVI.isParsed = EPSON_ReadEXVI(pTemp + localOffset + 1,
                                                    PESLen - localOffset - 1, &(pPESbuf->EXVI));
            if (pPESbuf->EXVI.isParsed) {
                if (PESLen > pPESbuf->EXVI.PESLength)
                    PESLen = pPESbuf->EXVI.PESLength;
            }
        }

        byteValue = NextNBufferBytes(pTemp, 1, &localOffset);
        HdrLen = NextNBufferBytes(pTemp, 1, &localOffset);

        PTS_DTS_flags = byteValue >> 6;
        if (PTS_DTS_flags == 0x10) {
            localOffset += 5;
            HdrLen -= 5;
        } else if (PTS_DTS_flags == 0x11) {
            localOffset += 10;
            HdrLen -= 5;
        }

        if (0 == HdrLen && (byteValue & 0x3F)) {
            /* header data length has no byte for these flags */
            byteValue &= 0xC0;
        }

        PES_extension_flag = byteValue & 0x01;
        if (PES_extension_flag) {
            U32 ext_size = 0, ext2_size = 0;
            byteValue = NextNBufferBytes(pTemp, 1, &localOffset);
            HdrLen--;

            if (byteValue & 0x40 /*pack_header_field_flag*/) {
                byteValue = 0;
            } else {
                if (byteValue & 0x80 /*PES_private_data_flag*/)
                    ext_size += 16;
                if (byteValue & 0x20 /*program_packet_sequence_counter_flag*/)
                    ext_size += 2;
                if (byteValue & 0x10 /*P-STD_buffer_flag*/)
                    ext_size += 2;
            }
            localOffset += ext_size;
            HdrLen -= ext_size;

            if (byteValue & 0x1 /*PES_extension_flag_2*/) {
                ext2_size = NextNBufferBytes(pTemp, 1, &localOffset);
                HdrLen--;
                if (ext2_size & 0x7F) {
                    byteValue = NextNBufferBytes(pTemp, 1, &localOffset);
                    HdrLen--;
                    if (0 == (byteValue & 0x80)) {
                        AStreamID = ((StreamID & 0xff) << 8) | byteValue;
                    }
                }
            }
        }

        if (HdrLen < 0)
            return 1;
        else
            localOffset += HdrLen;
    } else {
        if (0x0F != *(pTemp + localOffset))
            return 1;
    }

    if (IS_PRIV1(StreamID)) {
        if (isTs) {
            SubStreamID = pTemp[localOffset];
            AStreamID = (SubStreamID << 8) | StreamID;
        } else {
            bool isRawAc3 = FALSE;
            SubStreamID = NextNBufferBytes(pTemp, 1, &localOffset);
            AStreamID = (SubStreamID << 8) | StreamID;
            /* check ac3 sync word */
            if (0x0b == SubStreamID) {
                if (0x77 == NextNBufferBytes(pTemp, 1, &localOffset)) {
                    isRawAc3 = TRUE;
                    SubStreamID = AC3_ID_MIN1;
                    localOffset -= 2;
                } else
                    localOffset -= 1;
            }
            if (!isRawAc3 && SubStreamID >= AC3_ID_MIN1 && SubStreamID <= AC3_ID_MAX2) {
                localOffset += 3;
                if (SubStreamID >= 0xb0 && SubStreamID <= 0xbf) {
                    /* 4-byte header, skip one more byte */
                    localOffset++;
                } else if (SubStreamID >= 0xa0 && SubStreamID <= 0xaf) {
                    /* no header, rewind three bytes */
                    localOffset -= 3;
                }
            }
        }
    }

    if (!isTs) {
        /*update maximum PES size   */
        /*search the media information to find whether it  already exists */
        U32 currentStreamId = IS_PRIV1(StreamID) ? AStreamID : StreamID;
        for (i = 0; i < pCnxt->Media.NoMediaFound; i++) {
            if (currentStreamId == pCnxt->Media.StreamID[i]) {
                if (0x55 == pCnxt->Media.SysInfoFound[i]) {
                    pCnxt->ProbeCnxt.ProbeStage = FSL_MPG_DEMUX_PROBE_SYSINFO;
                    return 0;
                } else {
                    break;
                }
            }
        }
    }

    /*the payload starts here   */
    if (localOffset >= pPESbuf->PESLen)
        return 1;

    pTemp += localOffset;
    PESLen = pPESbuf->PESLen - localOffset;
    localOffset = 0;

    if (IS_VIDEO_PES(StreamID)) {
        FSL_MPG_DEMUX_VSHeader_T VHeader;
        U32 Ret = 1;

        memset(&VHeader, 0, sizeof(FSL_MPG_DEMUX_VSHeader_T));

        if (!isTs) {
            if (0 == ParseMPEG2VideoInfo(pCnxt, &VHeader, pTemp, PESLen)) {
                Ret = 0;
            } else if (0 == ParseMp4VideoInfo(pCnxt, &VHeader, pTemp, PESLen)) {
                Ret = 0;
            } else if (0 == ParseH264VideoInfo(pDemuxer->memOps, &VHeader, pTemp, PESLen)) {
                Ret = 0;
            }
        } else if (StreamType == MPEG1_VIDEO || StreamType == MPEG2_VIDEO) {
            Ret = ParseMPEG2VideoInfo(pCnxt, &VHeader, pTemp, PESLen);
        } else if (StreamType == MPEG2_PRIVATEPES_DVB || StreamType == MPEG2_HEVC) {
            HevcVideoHeader tmpHeader;

            memset(&tmpHeader, 0, sizeof(HevcVideoHeader));
            Ret = HevcParseVideoHeader(&tmpHeader, pTemp, PESLen);
            VHeader.HSize = tmpHeader.HSize;
            VHeader.VSize = tmpHeader.VSize;
            VHeader.BitRate = tmpHeader.BitRate;
            VHeader.FRDenominator = tmpHeader.FRDenominator;
            VHeader.FRNumerator = tmpHeader.FRNumerator;
            VHeader.enuVideoType = FSL_MPG_DEMUX_HEVC_VIDEO;
        } else if (StreamType == AVS_VIDEO) {
            Ret = 0;  // FIXME: Will Add parse AVS function later if it is needed.
            VHeader.enuVideoType = FSL_MPG_DEMUX_AVS_VIDEO;
        } else {
            if (pPESbuf->EXVI.isParsed) {
                Ret = 0;
                VHeader.HSize = pPESbuf->EXVI.vWidth;
                VHeader.VSize = pPESbuf->EXVI.vHeight;
                VHeader.FRNumerator = pPESbuf->EXVI.frameNumerator;
                VHeader.FRDenominator = pPESbuf->EXVI.frameDemoninator;
                VHeader.BitRate = 0;
                if (StreamType == MPEG2_H264)
                    Ret = ParseH264VideoInfo(pDemuxer->memOps, &VHeader, pTemp, PESLen);
                else if (StreamType == MPEG4_VIDEO)
                    Ret = ParseMp4VideoInfo(pCnxt, &VHeader, pTemp, PESLen);
            } else {
                if (StreamType == MPEG2_H264)
                    Ret = ParseH264VideoInfo(pDemuxer->memOps, &VHeader, pTemp, PESLen);
                else if (StreamType == MPEG4_VIDEO)
                    Ret = ParseMp4VideoInfo(pCnxt, &VHeader, pTemp, PESLen);
            }
        }

        // fix SR# 1-976597011
        // no pat, so the StreamType is set to default MPEG2_VIDEO
        // for some clips we need try MPEG2_H264
        if (Ret && (1 == pCnxt->TSCnxt.PAT.NonProgramSelected)) {
            Ret = ParseH264VideoInfo(pDemuxer->memOps, &VHeader, pTemp, PESLen);

            if (Ret) {
                Ret = ParseMp4VideoInfo(pCnxt, &VHeader, pTemp, PESLen);
            }
        }

        if (0 == Ret) {
            /*pass out video system info  */
            U32 i = pDemuxer->SystemInfo.uliNoStreams;

            FillMediaStreamInfo(pDemuxer, i, StreamID, pPESbuf->PID, &VHeader, 1 /*isVideo*/,
                                pCnxt->SeqHdrBuf.pSH, pCnxt->SeqHdrBuf.Size);

            pPESbuf->StreamNum = i;
            pCnxt->SeqHdrBuf.Size = 0;

            /*remove the PID from list  */
            if (isTs) {
                DisablePID(pCnxt, pPESbuf->PID);
            }
        }
    } else if (IS_AUDIO_PES(StreamID)) {
        FSL_MPG_DEMUX_AHeader_T AudioHeader;
        U32 Ret = 0;
        int IsExceeded = 0;

        memset(&AudioHeader, 0, sizeof(FSL_MPG_DEMUX_AHeader_T));
        AudioHeader.pCodecData = pCnxt->SeqHdrBuf.pSH;
        IsExceeded = SetTempStreamBuffer(pDemuxer, pTemp, PESLen, pPESbuf->PID);
        Ret = ParseMPEGAudioInfo(&AudioHeader, pDemuxer->SystemInfo.TempStreamBuf.pBuf,
                                 pDemuxer->SystemInfo.TempStreamBuf.nSize);
        if (IsExceeded)
            FreeTempStreamBuffer(pDemuxer);
        if (0 == Ret) {
            U32 i = pDemuxer->SystemInfo.uliNoStreams;
            pCnxt->SeqHdrBuf.Size = AudioHeader.nCodecDataSize;

            if (FSL_MPG_DEMUX_AAC_AUDIO == AudioHeader.enuAudioType &&
                pDemuxer->bForceGetAacAdtsCsd) {
                FillMediaStreamInfo(pDemuxer, i, StreamID, pPESbuf->PID, &AudioHeader,
                                    0 /*isVideo*/, pCnxt->SeqHdrBuf.pSH, pCnxt->SeqHdrBuf.Size);
            } else
                FillMediaStreamInfo(pDemuxer, i, StreamID, pPESbuf->PID, &AudioHeader,
                                    0 /*isVideo*/, NULL, 0);

            pCnxt->SeqHdrBuf.Size = 0;
            pPESbuf->StreamNum = i;
            /*remove the PID from list  */
            if (isTs) {
                DisablePID(pCnxt, pPESbuf->PID);
            }
            FreeTempStreamBuffer(pDemuxer);
        } else if (StreamType == AAC_LATM && isTs) {  // added for parrot
            U32 i = 0;
            FSL_MPG_DEMUX_AHeader_T AudioHeader;
            AAC_LATM_AUDIO_INFO sLatmInfo;
            memset(&AudioHeader, 0, sizeof(FSL_MPG_DEMUX_AHeader_T));
            memset(&sLatmInfo, 0, sizeof(AAC_LATM_AUDIO_INFO));
            sLatmInfo.pCodecData = pCnxt->SeqHdrBuf.pSH;
            Ret = ParseAacLatmAudioInfo(pTemp, PESLen, &sLatmInfo);
            if (Ret)
                goto bail;

            pCnxt->SeqHdrBuf.Size = sLatmInfo.nCodecDataSize;
            AudioHeader.enuAudioType = FSL_MPG_DEMUX_AAC_AUDIO;
            AudioHeader.SampleRate = sLatmInfo.nSampleRate;
            AudioHeader.Channels = sLatmInfo.nChannles;
            AudioHeader.ChannelMode = 0;
            AudioHeader.BitRate = 0;
            i = pDemuxer->SystemInfo.uliNoStreams;

            FillMediaStreamInfo(pDemuxer, i, StreamID, pPESbuf->PID, &AudioHeader, 0 /*isVideo*/,
                                pCnxt->SeqHdrBuf.pSH, pCnxt->SeqHdrBuf.Size);

            pDemuxer->SystemInfo.Stream[i].MediaProperty.AudioProperty.enuAudioSubType =
                    FSL_MPG_DEMUX_AAC_RAW;
            pPESbuf->StreamNum = i;
            /*remove the PID from list  */
            DisablePID(pCnxt, pPESbuf->PID);
            pCnxt->SeqHdrBuf.Size = 0;
        }
    } else if (IS_PRIV1(StreamID)) {
        FSL_MPG_DEMUX_AHeader_T AudioHeader;
        U32 Ret = 1;
        U32 PMTSectionNum = pCnxt->TSCnxt.PMT[0].Sections;
        U32 i, j, descTag = 0, PMTStreamType = 0;

        memset(&AudioHeader, 0, sizeof(FSL_MPG_DEMUX_AHeader_T));
        AudioHeader.pCodecData = pCnxt->SeqHdrBuf.pSH;

        if (isTs) {
            for (i = 0; i < PMTSectionNum; i++) {
                for (j = 0; j < NO_STREAMSINPMT_SUPPORT_MAX; j++) {
                    if (pCnxt->TSCnxt.PMT[0].PMTSection[i].StreamPID[j] == pPESbuf->PID) {
                        descTag = pCnxt->TSCnxt.PMT[0].PMTSection[i].StreamDescriptorTag[j];
                        PMTStreamType = pCnxt->TSCnxt.PMT[0].PMTSection[i].StreamType[j];
                        break;
                    }
                }
            }
            if (DVB_AC4 == PMTStreamType) {
                Ret = ParseAC4AudioInfo(&AudioHeader, pTemp, PESLen);
            } else if (MPEG2_DESC_DVB_DTS == descTag || MPEG2_EXT_DESC_DTS_HD == descTag ||
                       MPEG2_EXT_DESC_DTS_UHD == descTag) {
                Ret = ParseDTSAudioInfo(&AudioHeader, pTemp, PESLen);
            } else if (0 == ParseAC3AudioInfo(&AudioHeader, pTemp, PESLen)) {
                Ret = 0;
            } else if (IS_PCM(SubStreamID) &&
                       0 == ParseLPCMAudioInfo(&AudioHeader, pTemp, localOffset, PESLen)) {
                Ret = 0;
            } else if (0 == ParseAACAudioInfo(&AudioHeader, pTemp, PESLen)) {
                Ret = 0;
            }
        } else {
            /*DVD sub-stream */
            if (IS_AC3(SubStreamID) && 0 == ParseAC3AudioInfo(&AudioHeader, pTemp, PESLen)) {
                Ret = 0;
            } else if (IS_DTS(SubStreamID) && 0 == ParseDTSAudioInfo(&AudioHeader, pTemp, PESLen)) {
                Ret = 0;
            } else if (IS_PCM(SubStreamID) &&
                       0 == ParseLPCMAudioInfo(&AudioHeader, pTemp, localOffset, PESLen)) {
                Ret = 0;
            }
        }

        if (0 == Ret) {
            i = pDemuxer->SystemInfo.uliNoStreams;
            if (pDemuxer->bForceGetAacAdtsCsd &&
                AudioHeader.enuAudioType == FSL_MPG_DEMUX_AAC_AUDIO) {
                pCnxt->SeqHdrBuf.Size = AudioHeader.nCodecDataSize;
                FillMediaStreamInfo(pDemuxer, i, AStreamID, pPESbuf->PID, &AudioHeader,
                                    0 /*isVideo*/, pCnxt->SeqHdrBuf.pSH, pCnxt->SeqHdrBuf.Size);
                pCnxt->SeqHdrBuf.Size = 0;
            } else {
                FillMediaStreamInfo(pDemuxer, i, AStreamID, pPESbuf->PID, &AudioHeader,
                                    0 /*isVideo*/, NULL, 0);
            }
            pPESbuf->StreamNum = i;
            if (isTs) {
                DisablePID(pCnxt, pPESbuf->PID); /*remove the PID from list  */
            }
        } else {
            /* unsupported audio type */
            /*remove the PID from list  */
            MPG2_PARSER_TS_ERR("unknow private audio format \r\n");
        }
    } else {
        /*not supported    */
        /*remove the PID from list  */
        FSL_MPG_DEMUX_AHeader_T AudioHeader;
        U32 Ret = 0;

        // fix ENGR00216253, ??? why need time code to enable ac3
        if (/*pCnxt->TSCnxt.hasTimeCode &&*/ IS_AC3_BLUERAY(StreamID)) {
            Ret = ParseAC3AudioInfo(&AudioHeader, pTemp, PESLen);
            if (0 == Ret) {
                U32 i = pDemuxer->SystemInfo.uliNoStreams;

                FillMediaStreamInfo(pDemuxer, i, StreamID, pPESbuf->PID, &AudioHeader,
                                    0 /*isVideo*/, NULL, 0);

                pPESbuf->StreamNum = i;
                /*remove the PID from list  */
                DisablePID(pCnxt, pPESbuf->PID);
            }

        } else {
            DisablePID(pCnxt, pPESbuf->PID);

            /*clear the temp buffer  */
            pPESbuf->Complete = 0;
            pPESbuf->Filled = 0;
            pPESbuf->PESLen = 0;

            // fix ENGR00216253, if not supported, just go through
            return 0;  // 1;
        }
    }

bail:
    /*clear the temp buffer  */
    pPESbuf->Complete = 0;
    pPESbuf->Filled = 0;
    pPESbuf->PESLen = 0;

    return 0;
}
