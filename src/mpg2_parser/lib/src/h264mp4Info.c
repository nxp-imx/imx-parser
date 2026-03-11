/*
***********************************************************************
* Copyright (c) 2011-2012, Freescale Semiconductor, Inc.
*
* Copyright 2020, 2024, 2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/

#include "h264mp4info.h"

//#define MPG2_PARSER_H264_DBG
#ifdef MPG2_PARSER_H264_DBG
#define H264_LOG printf
#define H264_LOG_ERR printf
#else
#define H264_LOG(...)
#define H264_LOG_ERR(...)
#endif
#define ASSERT(exp)	if(!(exp)) {H264_LOG_ERR("%s: %d : assert condition !!!\r\n",__FUNCTION__,__LINE__);}

#if 0
static const unsigned char ZZ_SCAN[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};
static const unsigned char ZZ_SCAN8[64] = {
        0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
        41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
        30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};
#endif
int Buf_initContext(BUF_CONTEXT_T* pContext, uint8* pBuffer, uint32 bufLen) {
    pContext->pBuffer = pBuffer;
    pContext->bitsOffset = 0;
    pContext->bytesOffset = 0;
    pContext->bufLen = bufLen;

    return 0;
}

#if 0
int Buf_fnGetBits(BUF_CONTEXT_T* pContext, uint32 nBits, int* pValue)
{
    int Ret =0;
    uint8* pBuf;
    uint32* bytesOffset;
    uint32 bitsOffset = pContext->bitsOffset;
    uint32 leftbits = nBits;

    pBuf = pContext->pBuffer;
    bytesOffset= &(pContext->bytesOffset);

    //if exceed the total buffer len, return error
    if(leftbits  > (pContext->bufLen - pContext->bytesOffset) * 8 - bitsOffset)
        return 1;

    while(leftbits>0)
    {
        uint32 abits = 8-bitsOffset;
        uint8  byte = pBuf[*bytesOffset];
        uint8  value;

        if(abits>leftbits)
            abits = leftbits;

        value = (byte<<bitsOffset);
        value = value>>(8-abits);

        bitsOffset += abits;
        leftbits -= abits;

        Ret = Ret|value;

        if(bitsOffset>=8)
        {
            (*bytesOffset)++;
            if(*bytesOffset>=pContext->bufLen)
            {
                if(leftbits>0)
                    return 1;
            }

            bitsOffset -=8;
            Ret = (Ret<<(leftbits > 8 ? 8 : leftbits));
        }
    }

    pContext->pBuffer = pBuf;
    pContext->bitsOffset = bitsOffset;
    *pValue = Ret;

    return 0;
}
#endif

int BR_fnGetBits(uint32 nBits, BUF_CONTEXT_T* pContext) {
    int Ret = 0;
    uint8* pBuf;
    uint32* bytesOffset;
    uint32 bitsOffset = pContext->bitsOffset;
    uint32 leftbits = nBits;

    pBuf = pContext->pBuffer;
    bytesOffset = &(pContext->bytesOffset);
    if (*bytesOffset >= pContext->bufLen)
        return -1;

    while (leftbits > 0) {
        uint32 abits = 8 - bitsOffset;
        uint8 byte = pBuf[*bytesOffset];
        uint8 value;

        if (abits > leftbits)
            abits = leftbits;

        value = (byte << bitsOffset);
        value = value >> (8 - abits);

        bitsOffset += abits;
        leftbits -= abits;

        Ret = Ret | value;

        if (bitsOffset >= 8) {
            (*bytesOffset)++;
            bitsOffset -= 8;
            if (*bytesOffset >= pContext->bufLen) {
                if (leftbits > 0)
                    return -1;
            }
            Ret = (Ret << leftbits);
        }
    }

    pContext->pBuffer = pBuf;
    pContext->bitsOffset = bitsOffset;
    return Ret;
}

#define GET_ONE_BYTE(context) BR_fnGetBits(8, context)
#define GET_ONE_BIT(context) BR_fnGetBits(1, context)

#if 0
int GetVLCSymbol (int *info, BUF_CONTEXT_T * pContext, int* pValue)
{
    register int inf;
    int  len        = 0;
    int  bitcounter = 1;
    int  ctr_bit;
    int err_code = 0;

    if((err_code = Buf_fnGetBits( pContext, 1, &ctr_bit)))
        return err_code;

    while (ctr_bit == 0)
    {                 // find leading 1 bit
        len++;
        bitcounter++;
        if((err_code = Buf_fnGetBits( pContext, 1, &ctr_bit)))
            return err_code;
    }

    // make infoword
    inf = 0;                          // shortest possible code is 1, then info is always 0    
    while (len--)
    {
        bitcounter++;
        inf <<= 1;
        if((err_code = Buf_fnGetBits( pContext, 1, &ctr_bit)))
            return err_code;
        inf |= ctr_bit;
    }

    *info = inf;
    *pValue = bitcounter;           // return absolute offset in bit from start of frame

    return 0;
}


int readSyntaxElement_VLC(SyntaxElement *sym, BUF_CONTEXT_T * pContext, int* pValue)
{
    int  errCode = 0;

    if((errCode = GetVLCSymbol (&(sym->inf), pContext, &(sym->len))))
        return errCode;

    if (sym->len == -1)
    {  
        *pValue = -1;
        return errCode;
    }
    sym->mapping(sym->len,sym->inf,&(sym->value1),&(sym->value2));
    *pValue = 1;

    return errCode;
}

void linfo_ue(int len, int info, int *value1, int *dummy)
{
    *value1 = (1 << (len >> 1)) + info - 1;
}

void linfo_se(int len,  int info, int *value1, int *dummy)
{
    int n;
    //assert ((len >> 1) < 32);
    n = (1 << (len >> 1)) + info - 1;
    *value1 = (n + 1) >> 1;
    if((n & 0x01) == 0)                           // lsb is signed bit
        *value1 = -*value1;
}


int ue_v ( BUF_CONTEXT_T * pContext, int* pValue)
{
    int errCode = 0;
    int ret =0;
    SyntaxElement symbol;
    symbol.mapping = linfo_ue;

    errCode = readSyntaxElement_VLC(&symbol, pContext,&ret);
    *pValue= symbol.value1;

    return errCode;
}

int se_v ( BUF_CONTEXT_T * pContext, int* pValue)
{
    int errCode = 0;
    int ret =0;
    SyntaxElement symbol;

    symbol.mapping = linfo_se;
    errCode = readSyntaxElement_VLC(&symbol, pContext,&ret);
    *pValue = symbol.value1;
    return errCode;
}

int H264R_fnScaling_List(int *scalingList, int sizeOfScalingList, int *UseDefaultScalingMatrix, BUF_CONTEXT_T * pContext)
{
    int j, scanj;
    int delta_scale, lastScale, nextScale;
    int errCode = 0;

    lastScale      = 8;
    nextScale      = 8;

    for(j=0; j<sizeOfScalingList; j++)
    {
        scanj = (sizeOfScalingList==16) ? ZZ_SCAN[j]:ZZ_SCAN8[j];

        if(nextScale!=0)
        {
            if((errCode = se_v ( pContext,&delta_scale)))
                return errCode;
            nextScale = (lastScale + delta_scale + 256) % 256;
            *UseDefaultScalingMatrix = (int) (scanj==0 && nextScale==0);
        }

        scalingList[scanj] = (nextScale==0) ? lastScale:nextScale;
        lastScale = scalingList[scanj];
    }

    return errCode;
}
#endif

/* header start code of MPEG-4 visual */
#define START_CODE_PREFIX 0x01
#define VDO_START_CODE_MIN 0x00  /* video object min start code */
#define VDO_START_CODE_MAX 0x1F  /* video object max start code */
#define VOL_START_CODE_MIN 0x20  /* video object layer min start code */
#define VOL_START_CODE_MAX 0x2F  /* video object layer max start code */
#define VOS_START_CODE 0xB0      /* visual object sequence start code */
#define USR_START_CODE 0xB2      /* user data start code */
#define GOV_START_CODE 0xB3      /* group of VOP start code */
#define VSO_START_CODE 0xB5      /* visual object start code */
#define VOP_START_CODE 0xB6      /* vop start */
#define RESERVED_START_CODE 0xC4 /* reserved start code */
#define NUMBITS_START_CODE_PREFIX 24
#define NUMBITS_BYTE 8

/* VSO overhead information */
#define NUMBITS_VSO_VERID 4
#define NUMBITS_VSO_PRIORITY 3
#define NUMBITS_VSO_TYPE 4

/* VO overhead information */
#define NUMBITS_VO_START_CODE 32
#define NUMBITS_VO_ID 5

/* VOL overhead information */
#define VO_TYPE_STREAMING_VIDEO 18 /* Streaming Video */
#define FGS_LAYER_TYPE_FGS 1
#define FGS_LAYER_TYPE_FGST 2
#define FGS_LAYER_TYPE_FGS_FGST 3
#define NUMBITS_VOL_ID 4
#define NUMBITS_FGS_LAYER_TYPE 2
#define NUMBITS_VOL_SHAPE 2
#define NUMBITS_TIME_RESOLUTION 16
#define NUMBITS_VOP_WIDTH 13
#define NUMBITS_VOP_HEIGHT 13
#define NUMBITS_SPRITE_HDIM 13
#define NUMBITS_SPRITE_VDIM 13
#define NUMBITS_SPRITE_LEFT_EDGE 13
#define NUMBITS_SPRITE_TOP_EDGE 13
#define NUMBITS_NUM_SPRITE_POINTS 6
#define NUMBITS_WARPING_ACCURACY 2
#define NUMBITS_QMATRIX 8

/* VOP overhead information */
#define NUMBITS_VOP_PRED_TYPE 2
#define NUMBITS_VOP_TIMEBASE 1
#define NUMBITS_VOP_ID_FOR_PRED 1
#define NUMBITS_VOP_HORI_SPA_REF 13
#define NUMBITS_VOP_VERT_SPA_REF 13
#define NUMBITS_VOP_ALPHA_QUANTIZER 6
#define NUMBITS_VOP_FCODE 3

/* short header */
#define NUMBITS_SHORT_HEADER_START_CODE 22
#define SHORT_VIDEO_START_MARKER 32
#define TEMPORAL_REFERENCE_MAX 256

/* marker bit */
#define NUMBITS_MARKERBIT 1
#define TIME_MAX LONG_MAX    /* max time stamp */
#define TEMP_BUFFER_SIZE 100 /* temp buffer size(byte) */
#define SIZE_OF_HEADER 4     /* size of header start */

#ifdef MPEG4_SHORT_HEADER
/********************************************************************
 *
 * MPG4VP_fnGetShortHdr - parse short header
 *
 * Description:
 *   This function parses short video header
 *
 * Arguments:
 *   pMPG4Obj               [IN/OUT]MPEG-4 object structure pointer
 *
 * Return:
 *   E_MPG4_EOF_127         reach the end of file
 *   E_MPG4_OK_0            parse VOS header successfully
 *   E_MPG4_BROKEN_HDR_129  the VOS header is broken
 *
 ********************************************************************/
static int MPG4VP_fnGetShortHdr(MPG4_OBJECT* _pMPG4Obj) {
    MPG4_OBJECT* pMPG4Obj = _pMPG4Obj;
    MPG4_VDO_HEADER* psVdoHdr = NULL;
    MPG4_VOL_HEADER* psVolHdr = NULL;
    MPG4_VOP_HEADER* psVopHdr = NULL;
    MPG4_RETURN eRetVal = E_MPG4_OK_0;
    BUF_CONTEXT_T* pStreamReader = NULL;
    U32 uiSourceFormat, uiPictureCodingType;
    U32 uiMarker, uiZero;

    pStreamReader = pMPG4Obj->pStreamReader;
    psVdoHdr = pMPG4Obj->psVdoHdr;
    psVolHdr = pMPG4Obj->psVolHdr;
    psVopHdr = pMPG4Obj->psVopHdr;

    /* Temporal_reference */
    psVopHdr->uiPrevTemporalRef = psVopHdr->uiCurrTemporalRef;
    psVopHdr->uiCurrTemporalRef = GET_ONE_BYTE(pStreamReader);
    if (psVopHdr->uiCurrTemporalRef < psVopHdr->uiPrevTemporalRef) {
        psVopHdr->uiNum256++;
    }
    psVopHdr->iTimeStamp =
            psVopHdr->uiNum256 * TEMPORAL_REFERENCE_MAX + +psVopHdr->uiCurrTemporalRef;
    uiMarker = GET_ONE_BIT(pStreamReader);           /* marker_bit */
    uiZero = GET_ONE_BIT(pStreamReader);             /* zero_bit */
    GET_ONE_BIT(pStreamReader);                      /* split_screen_indicator */
    GET_ONE_BIT(pStreamReader);                      /* document_camera_indicator */
    GET_ONE_BIT(pStreamReader);                      /* full_picture_freeze_release */
    uiSourceFormat = BR_fnGetBits(3, pStreamReader); /* source_format */

    if (uiSourceFormat == 1) {
        psVopHdr->uiNumGobsInVop = 6;
        psVopHdr->uiNumMacroblocksInGob = 8;
        psVolHdr->iVolWidth = 128;
        psVolHdr->iVolHeight = 96;
    } else if (uiSourceFormat == 2) {
        psVopHdr->uiNumGobsInVop = 9;
        psVopHdr->uiNumMacroblocksInGob = 11;
        psVolHdr->iVolWidth = 176;
        psVolHdr->iVolHeight = 144;
    } else if (uiSourceFormat == 3) {
        psVopHdr->uiNumGobsInVop = 18;
        psVopHdr->uiNumMacroblocksInGob = 22;
        psVolHdr->iVolWidth = 352;
        psVolHdr->iVolHeight = 288;
    } else if (uiSourceFormat == 4) {
        psVopHdr->uiNumGobsInVop = 18;
        psVopHdr->uiNumMacroblocksInGob = 88;
        psVolHdr->iVolWidth = 704;
        psVolHdr->iVolHeight = 576;
    } else if (uiSourceFormat == 5) {
        psVopHdr->uiNumGobsInVop = 18;
        psVopHdr->uiNumMacroblocksInGob = 352;
        psVolHdr->iVolWidth = 1408;
        psVolHdr->iVolHeight = 1152;
    } else {
        eRetVal = E_MPG4_BROKEN_HDR_129;
        return eRetVal;
    }
    uiPictureCodingType = GET_ONE_BIT(pStreamReader); /* picture_coding_type */
    if (uiPictureCodingType == 0) {
        psVopHdr->vopPredType = IVOP;
    } else {
        psVopHdr->vopPredType = PVOP;
    }

    BR_fnGetBits(4, pStreamReader);                        /* four_reserved_zero_bits */
    psVopHdr->uiVopQuant = BR_fnGetBits(5, pStreamReader); /* vop_quant */
    psVopHdr->intStepI = psVopHdr->uiVopQuant;
    psVopHdr->intStep = psVopHdr->uiVopQuant;  // idem
    GET_ONE_BIT(pStreamReader);
    do {
        psVopHdr->uiPei = GET_ONE_BIT(pStreamReader);
        if (psVopHdr->uiPei == 1) {
            GET_ONE_BYTE(pStreamReader); /* psupp */
        }
    } while (psVopHdr->uiPei == 1);

    psVdoHdr->uiVOId = 1;
    psVolHdr->iClockRate = 30;
    psVolHdr->dFrameHz = 30;
    psVolHdr->iNumBitsTimeIncr = 4;
    psVolHdr->bShapeOnly = FALSE;
    psVolHdr->fAUsage = RECTANGLE;
    psVolHdr->bAdvPredDisable = TRUE;
    psVolHdr->uiSprite = FALSE;
    psVolHdr->bNot8Bit = FALSE;
    psVolHdr->uiQuantPrecision = 5;
    psVolHdr->nBits = 8;
    psVolHdr->fQuantizer = Q_H263;
    psVolHdr->bDataPartitioning = FALSE;
    psVolHdr->bReversibleVlc = FALSE;
    psVolHdr->volType = BASE_LAYER;
    psVolHdr->ihor_sampling_factor_n = 1;
    psVolHdr->ihor_sampling_factor_m = 1;
    psVolHdr->iver_sampling_factor_n = 1;
    psVolHdr->iver_sampling_factor_m = 1;
    psVolHdr->bDeblockFilterDisable = TRUE;
    psVolHdr->bQuarterSample = 0;
    psVolHdr->bRoundingControlDisable = 0;
    psVolHdr->iInitialRoundingType = 0;
    psVolHdr->bResyncMarkerDisable = 1;
    psVolHdr->bVPBitTh = 0;
    psVolHdr->bSadctDisable = 1;
    psVolHdr->bComplexityEstimationDisable = 1;
    psVolHdr->bTrace = 0;
    psVolHdr->bDumpMB = 0;
    psVolHdr->breduced_resolution_vop_enable = 0;
    psVolHdr->iAuxCompCount = 0;
    psVolHdr->bTemporalFGS = FALSE;
    psVolHdr->bFGS = FALSE;
    psVolHdr->bFGSScalability = FALSE;
    psVolHdr->bNewpredEnable = FALSE;
    psVopHdr->bVopCoded = TRUE;

exit:
    return eRetVal;
}
#endif

/********************************************************************
 *
 * MPG4VP_fnGetVosHdr - parse Visual Object Sequence header from MPEG-4 bistream
 *
 * Description:
 *   This function parses Visual Object Sequence header from MPEG-4 bistream
 *
 * Arguments:
 *   pMPG4Obj               [IN/OUT]MPEG-4 object structure pointer
 *
 * Return:
 *   E_MPG4_EOF_127         reach the end of file
 *   E_MPG4_OK_0            parse VOS header successfully
 *   E_MPG4_BROKEN_HDR_129  the VOS header is broken
 *
 ********************************************************************/
static int MPG4VP_fnGetVosHdr(MPG4_OBJECT* _pMPG4Obj) {
    MPG4_OBJECT* pMPG4Obj = _pMPG4Obj;
    VOID* pStreamReader = NULL;
    MPG4_VOS_HEADER* psVosHdr = NULL;
    MPG4_RETURN eRetVal = E_MPG4_OK_0;

    pStreamReader = pMPG4Obj->pStreamReader;
    psVosHdr = pMPG4Obj->psVosHdr;

    /* visual session header */
    psVosHdr->uiProfile = GET_ONE_BYTE(pStreamReader);

    return eRetVal;
}

/********************************************************************
 *
 * MPG4VP_fnGetVsoHdr - parse Visual Object header from MPEG-4 bistream
 *
 * Description:
 *   This function parses Visual Object header from MPEG-4 bistream
 *
 * Arguments:
 *   pMPG4Obj               [IN/OUT]MPEG-4 object structure pointer
 *
 * Return:
 *   E_MPG4_EOF_127         reach the end of file
 *   E_MPG4_OK_0            parse VSO header successfully
 *   E_MPG4_BROKEN_HDR_129  the VSO header is broken
 *
 ********************************************************************/
static int MPG4VP_fnGetVsoHdr(MPG4_OBJECT* _pMPG4Obj) {
    MPG4_OBJECT* pMPG4Obj = _pMPG4Obj;
    VOID* pStreamReader = NULL;
    MPG4_VSO_HEADER* psVsoHdr = NULL;
    MPG4_RETURN eRetVal = E_MPG4_OK_0;

    pStreamReader = pMPG4Obj->pStreamReader;
    psVsoHdr = pMPG4Obj->psVsoHdr;

    /* visual object header */
    psVsoHdr->uiIsVisualObjectIdent = GET_ONE_BIT(pStreamReader);
    if (psVsoHdr->uiIsVisualObjectIdent) {
        psVsoHdr->uiVSOVerID = BR_fnGetBits(NUMBITS_VSO_VERID, pStreamReader);
        psVsoHdr->uiVSOPriority = BR_fnGetBits(NUMBITS_VSO_PRIORITY, pStreamReader);
    }
    psVsoHdr->uiVSOType = BR_fnGetBits(NUMBITS_VSO_TYPE, pStreamReader);

    return eRetVal;
}

/********************************************************************
 *
 * MPG4VP_fnGetVdoHdr - parse Video Object header from MPEG-4 bistream
 *
 * Description:
 *   This function parses Video Object header from MPEG-4 bistream
 *
 * Arguments:
 *   pMPG4Obj               [IN/OUT]MPEG-4 object structure pointer
 *   uiStartCode            [IN]    VDO start code
 *
 * Return:
 *   E_MPG4_OK_0            parse VDO header successfully
 *
 ********************************************************************/
static int MPG4VP_fnGetVdoHdr(MPG4_OBJECT* _pMPG4Obj, U32 uiStartCode) {
    MPG4_OBJECT* pMPG4Obj = _pMPG4Obj;
    MPG4_VDO_HEADER* psVdoHdr = NULL;
    MPG4_RETURN eRetVal = E_MPG4_OK_0;

    psVdoHdr = pMPG4Obj->psVdoHdr;

    psVdoHdr->uiVOId = uiStartCode & 0x1F;

    return eRetVal;
}

static I32 MPG4VP_fnGetAuxCompCount(I32 _iVolShapeExtension) {
    I32 iVolShapeExtension = _iVolShapeExtension;

    switch (iVolShapeExtension) {
        case -1:
            return 1;
        case 0:
            return 1;
        case 1:
            return 1;
        case 2:
            return 2;
        case 3:
            return 2;
        case 4:
            return 3;
        case 5:
            return 1;
        case 6:
            return 2;
        case 7:
            return 1;
        case 8:
            return 1;
        case 9:
            return 2;
        case 10:
            return 3;
        case 11:
            return 2;
        case 12:
            return 3;
        default:
            return 1;
    }
}

/********************************************************************
 *
 * MPG4VP_fnGetVolHdr - parse Video Object Layer header from MPEG-4 bistream
 *
 * Description:
 *   This function parses Video Object Layer header from MPEG-4 bistream
 *
 * Arguments:
 *   pMPG4Obj               [IN/OUT]MPEG-4 object structure pointer
 *
 * Return:
 *   E_MPG4_EOF_127         reach the end of file
 *   E_MPG4_OK_0            parse VOL header successfully
 *   E_MPG4_BROKEN_HDR_129  the VOL header is broken
 *   E_MPG4_NOT_SUPPORT_131 not supported feature in the header
 *
 ********************************************************************/
static int MPG4VP_fnGetVolHdr(MPG4_OBJECT* _pMPG4Obj) {
    MPG4_OBJECT* pMPG4Obj = _pMPG4Obj;
    VOID* pStreamReader = NULL;
    MPG4_VOL_HEADER* psVolHdr = NULL;
    MPG4_VOP_HEADER* psVopHdr = NULL;
    I32 iClockRate;
    U32 uiAUsage, uiMarker;
    MPG4_RETURN eRetVal = E_MPG4_OK_0;

    pStreamReader = pMPG4Obj->pStreamReader;
    psVolHdr = pMPG4Obj->psVolHdr;
    psVopHdr = pMPG4Obj->psVopHdr;

    psVolHdr->dClockRateScale = 1.0; /* Larry: only for base layer. */
    /* for enhance layer, it may not be 1.0 */
    /* need to be modified */

    /*psVolHdr->uiVOLId = BR_fnGetBits( NUMBITS_VOL_ID); */
    psVolHdr->bRandom = GET_ONE_BIT(pStreamReader);   /*VOL_Random_Access */
    psVolHdr->uiOLType = GET_ONE_BYTE(pStreamReader); /* VOL_type_indication */

    if (psVolHdr->uiVOLId) {
        psVolHdr->volType = ENHN_LAYER;
    }

    if (psVolHdr->uiOLType == VO_TYPE_STREAMING_VIDEO) {
        psVolHdr->fAUsage = RECTANGLE; /* must be set */

        psVolHdr->iClockRate = 30;
        psVolHdr->iNumBitsTimeIncr = 4;
        psVolHdr->bShapeOnly = FALSE;
        psVolHdr->bAdvPredDisable = TRUE;
        psVolHdr->uiSprite = 0;
        psVolHdr->bNot8Bit = FALSE;
        psVolHdr->uiQuantPrecision = 5;
        psVolHdr->nBits = 8;
        psVolHdr->fQuantizer = Q_H263;
        psVolHdr->bDataPartitioning = FALSE;
        psVolHdr->bReversibleVlc = FALSE;
        psVolHdr->ihor_sampling_factor_n = 1;
        psVolHdr->ihor_sampling_factor_m = 1;
        psVolHdr->iver_sampling_factor_n = 1;
        psVolHdr->iver_sampling_factor_m = 1;
        psVolHdr->bDeblockFilterDisable = TRUE;
        psVolHdr->bNewpredEnable = FALSE;
        psVolHdr->breduced_resolution_vop_enable = 0;
        psVopHdr->RRVmode.iOnOff = 0;
        psVolHdr->bSadctDisable = TRUE;

        psVolHdr->uiFgsLayerType =
                BR_fnGetBits(NUMBITS_FGS_LAYER_TYPE, pStreamReader); /* fgs_layer_type */

        if (psVolHdr->uiFgsLayerType == FGS_LAYER_TYPE_FGST) {
            psVolHdr->bTemporalFGS = TRUE;
            psVolHdr->bFGS = FALSE;                                     /* FGST Stream */
        } else if (psVolHdr->uiFgsLayerType == FGS_LAYER_TYPE_FGS_FGST) /* FGS_FGST Stream */
        {
            psVolHdr->bTemporalFGS = TRUE;
            psVolHdr->bFGS = TRUE;
        } else if (psVolHdr->uiFgsLayerType ==
                   FGS_LAYER_TYPE_FGS) /* FGS Stream is not handled here */
        {
            psVolHdr->bTemporalFGS = FALSE;
            psVolHdr->bFGS = TRUE;
        }
        psVolHdr->uiVolPrio = BR_fnGetBits(3, pStreamReader); /* video_oject_layer_priority */
        psVolHdr->uiAspect = BR_fnGetBits(4, pStreamReader);
        if (psVolHdr->uiAspect == 15) /* extended PAR */
        {
            psVolHdr->uiParWidth = GET_ONE_BYTE(pStreamReader);
            psVolHdr->uiParHeight = GET_ONE_BYTE(pStreamReader);
        }

        psVolHdr->uiCTP =
                GET_ONE_BIT(pStreamReader); /*VOL_Control_Parameter, useless flag for now */
        if (psVolHdr->uiCTP) {
            psVolHdr->uiChromaFormat = BR_fnGetBits(2, pStreamReader);
            psVolHdr->uiLowDelay = GET_ONE_BIT(pStreamReader);
        }

        uiMarker = GET_ONE_BIT(pStreamReader);

        psVolHdr->iClockRate = BR_fnGetBits(NUMBITS_TIME_RESOLUTION, pStreamReader);
        uiMarker = GET_ONE_BIT(pStreamReader);

        iClockRate = psVolHdr->iClockRate;

        for (psVolHdr->iNumBitsTimeIncr = 1; psVolHdr->iNumBitsTimeIncr < NUMBITS_TIME_RESOLUTION;
             psVolHdr->iNumBitsTimeIncr++) {
            if (iClockRate == 1)
                break;
            iClockRate = (iClockRate >> 1);
        }

        psVolHdr->bFixFrameRate = GET_ONE_BIT(pStreamReader);
        if (psVolHdr->bFixFrameRate) {
            psVolHdr->uiFixedVOPTimeIncrement =
                    BR_fnGetBits(psVolHdr->iNumBitsTimeIncr, pStreamReader);
        }

        uiMarker = GET_ONE_BIT(pStreamReader);

        psVolHdr->iVolWidth = BR_fnGetBits(NUMBITS_VOP_WIDTH, pStreamReader);
        uiMarker = GET_ONE_BIT(pStreamReader);

        psVolHdr->iVolHeight = BR_fnGetBits(NUMBITS_VOP_HEIGHT, pStreamReader);
        uiMarker = GET_ONE_BIT(pStreamReader);

        psVopHdr->bInterlace = GET_ONE_BIT(pStreamReader);    /* interlace (was vop flag) */
        psVolHdr->uiVolPrio = BR_fnGetBits(4, pStreamReader); /* ref_layer_id */

        /* Loading FGS_Freq_Weighting_Matrix */
        if (psVolHdr->uiFgsLayerType == FGS_LAYER_TYPE_FGS ||
            psVolHdr->uiFgsLayerType == FGS_LAYER_TYPE_FGS_FGST) {
            psVolHdr->bFGSFreqWeightingEnable = GET_ONE_BIT(pStreamReader);
            if (psVolHdr->bFGSFreqWeightingEnable == TRUE) {
                int i = 0;
                for (i = 0; i < BLOCK_SQUARE_SIZE; i++) {
                    psVolHdr->rgiWeightMatrix[i] = 0;
                }

                psVolHdr->bLoadWeightMatrix = GET_ONE_BIT(pStreamReader);
#if 0 /* no need to load zigzag matrix */
                if (psVolHdr->bLoadWeightMatrix) 
                {
                    int val;
                    /* 3-bit fixed length values with zero ending */
                    i = 0;
                    do {
                        val = BR_fnGetBits( 3);
                        psVolHdr->rgiWeightMatrix[grgiStandardZigzag[i]] = val;
                        i++;
                    } while ((i < BLOCK_SQUARE_SIZE) && (val!=0));
                }
#endif
            }
        }

        /* Loading FGST_Freq_Weighting_Matrix */
        psVolHdr->bFGSTFreqWeightingEnable = GET_ONE_BIT(pStreamReader);
        if (psVolHdr->bFGSTFreqWeightingEnable == TRUE) {
            int i = 0;
            for (i = 0; i < BLOCK_SQUARE_SIZE; i++) {
                psVolHdr->rgiFGSTWeightMatrix[i] = 0;
            }

            psVolHdr->bLoadFGSTWeightMatrix = GET_ONE_BIT(pStreamReader);
#if 0 /* no need to load zigzag matrix */
            if (psVolHdr->bLoadFGSTWeightMatrix) 
            {
                int val;
                /* 3-bit fixed length values with zero ending */
                i = 0;
                do {
                    val = BR_fnGetBits( 3);
                    psVolHdr->rgiFGSTWeightMatrix[grgiStandardZigzag[i]] = val;
                    i++;
                } while ((i < BLOCK_SQUARE_SIZE) && (val != 0));
            }
#endif
        }

        psVolHdr->bQuarterSample = GET_ONE_BIT(pStreamReader);
        psVolHdr->bFGSErrorResilienceDisable = GET_ONE_BIT(pStreamReader);

        psVolHdr->iEnhnType = 0;

        psVolHdr->iAuxCompCount = 0;

    } else /* psVolHdr->uiOLType != VIDEO_OBJECT_TYPE_STREAMING_VIDEO */
    {
        psVolHdr->bFGS = FALSE;
        psVolHdr->bTemporalFGS = FALSE;

        psVolHdr->uiOLI = GET_ONE_BIT(
                pStreamReader); /*VOL_Is_Object_Layer_Identifier, useless flag for now */
        if (psVolHdr->uiOLI) {
            psVolHdr->uiVerID = BR_fnGetBits(4, pStreamReader);   /* video_oject_layer_verid */
            psVolHdr->uiVolPrio = BR_fnGetBits(3, pStreamReader); /* video_oject_layer_priority */
        } else {
            psVolHdr->uiVerID = 1;
        }

        psVolHdr->uiAspect = BR_fnGetBits(4, pStreamReader);
        if (psVolHdr->uiAspect == 15) /* extended PAR */
        {
            psVolHdr->uiParWidth = GET_ONE_BYTE(pStreamReader);
            psVolHdr->uiParHeight = GET_ONE_BYTE(pStreamReader);
        }

        psVolHdr->uiCTP =
                GET_ONE_BIT(pStreamReader); /*VOL_Control_Parameter, useless flag for now */
        if (psVolHdr->uiCTP) {
            psVolHdr->uiChromaFormat = BR_fnGetBits(2, pStreamReader);
            psVolHdr->uiLowDelay = GET_ONE_BIT(pStreamReader);
            psVolHdr->uiVBVParams = GET_ONE_BIT(pStreamReader);

            if (psVolHdr->uiVBVParams) {
                psVolHdr->uiFirstHalfBitRate = BR_fnGetBits(15, pStreamReader);
                uiMarker = GET_ONE_BIT(pStreamReader);
                ASSERT(uiMarker == 1);
                psVolHdr->uiLatterHalfBitRate = BR_fnGetBits(15, pStreamReader);
                uiMarker = GET_ONE_BIT(pStreamReader);
                ASSERT(uiMarker == 1);
                psVolHdr->uiFirstHalfVbvBufferSize = BR_fnGetBits(15, pStreamReader);
                uiMarker = GET_ONE_BIT(pStreamReader);
                ASSERT(uiMarker == 1);
                psVolHdr->uiLatterHalfVbvBufferSize = BR_fnGetBits(3, pStreamReader);
                psVolHdr->uiFirstHalfVbvBufferOccupany = BR_fnGetBits(11, pStreamReader);
                uiMarker = GET_ONE_BIT(pStreamReader);
                ASSERT(uiMarker == 1);
                psVolHdr->uiLatterHalfVbvBufferOccupany = BR_fnGetBits(15, pStreamReader);
                uiMarker = GET_ONE_BIT(pStreamReader);
                ASSERT(uiMarker == 1);
                /* combine the first half and latter half */
                psVolHdr->uiBitRate =
                        (psVolHdr->uiFirstHalfBitRate << 15) + psVolHdr->uiLatterHalfBitRate;
                psVolHdr->uiVbvBufferSize = (psVolHdr->uiFirstHalfVbvBufferSize << 3) +
                                            psVolHdr->uiLatterHalfVbvBufferSize;
                psVolHdr->uiVbvBufferOccupany = (psVolHdr->uiFirstHalfVbvBufferOccupany << 15) +
                                                psVolHdr->uiLatterHalfVbvBufferOccupany;
            }
        }

        uiAUsage = BR_fnGetBits(NUMBITS_VOL_SHAPE, pStreamReader);

        if (uiAUsage == 3) { /* gray scale */
            if (psVolHdr->uiVerID != 1) {
                psVolHdr->iAlphaShapeExtension = BR_fnGetBits(4, pStreamReader);
                psVolHdr->iAuxCompCount = MPG4VP_fnGetAuxCompCount(psVolHdr->iAlphaShapeExtension);
            } else {
                psVolHdr->iAuxCompCount = 1;
            }
        } else {
            psVolHdr->iAuxCompCount = 0;
        }

        uiMarker = GET_ONE_BIT(pStreamReader);
        ASSERT(uiMarker == 1);
        psVolHdr->iClockRate = BR_fnGetBits(NUMBITS_TIME_RESOLUTION, pStreamReader);
        uiMarker = GET_ONE_BIT(pStreamReader);
        ASSERT(uiMarker == 1);
        iClockRate = psVolHdr->iClockRate - 1;
        ASSERT(iClockRate < 65536);
        if (iClockRate > 0) {
            for (psVolHdr->iNumBitsTimeIncr = 1;
                 psVolHdr->iNumBitsTimeIncr < NUMBITS_TIME_RESOLUTION;
                 psVolHdr->iNumBitsTimeIncr++) {
                if (iClockRate == 1) {
                    break;
                }
                iClockRate = (iClockRate >> 1);
            }
        } else {
            psVolHdr->iNumBitsTimeIncr = 1;
        }

        psVolHdr->bFixFrameRate = GET_ONE_BIT(pStreamReader);
        if (psVolHdr->bFixFrameRate) {
            psVolHdr->uiFixedVOPTimeIncrement = 1;
            if (psVolHdr->iNumBitsTimeIncr != 0)
                psVolHdr->uiFixedVOPTimeIncrement =
                        BR_fnGetBits(psVolHdr->iNumBitsTimeIncr, pStreamReader) + 1;
        }

        if (uiAUsage == 2) /* shape-only mode */
        {
            if (psVolHdr->uiVerID == 2) {
                psVolHdr->volType = (GET_ONE_BIT(pStreamReader) == 0) ? BASE_LAYER : ENHN_LAYER;
                psVolHdr->iEnhnType = 0;      /*OBSSFIX_BSO */
                psVolHdr->iHierarchyType = 0; /*OBSSFIX_BSO */
                psVolHdr->ihor_sampling_factor_n = 1;
                psVolHdr->ihor_sampling_factor_m = 1;
                psVolHdr->iver_sampling_factor_n = 1;
                psVolHdr->iver_sampling_factor_m = 1;
                psVolHdr->ihor_sampling_factor_n_shape = 1;
                psVolHdr->ihor_sampling_factor_m_shape = 1;
                psVolHdr->iver_sampling_factor_n_shape = 1;
                psVolHdr->iver_sampling_factor_m_shape = 1;
                if (psVolHdr->volType == ENHN_LAYER) {
                    psVolHdr->ihor_sampling_factor_n_shape = BR_fnGetBits(5, pStreamReader);
                    psVolHdr->ihor_sampling_factor_m_shape = BR_fnGetBits(5, pStreamReader);
                    psVolHdr->iver_sampling_factor_n_shape = BR_fnGetBits(5, pStreamReader);
                    psVolHdr->iver_sampling_factor_m_shape = BR_fnGetBits(5, pStreamReader);
                    psVolHdr->ihor_sampling_factor_n = psVolHdr->ihor_sampling_factor_n_shape;
                    psVolHdr->ihor_sampling_factor_m = psVolHdr->ihor_sampling_factor_m_shape;
                    psVolHdr->iver_sampling_factor_n = psVolHdr->iver_sampling_factor_n_shape;
                    psVolHdr->iver_sampling_factor_m = psVolHdr->iver_sampling_factor_m_shape;
                }
            }
            psVolHdr->uiResyncMarkerDisable = GET_ONE_BIT(pStreamReader);

            /* default to some values - probably not all needed */
            psVolHdr->bShapeOnly = TRUE;
            psVolHdr->fAUsage = ONE_BIT;
            psVolHdr->bAdvPredDisable = 0;
            psVolHdr->fQuantizer = Q_H263;
            psVolHdr->bSadctDisable = 1;
            psVolHdr->bNewpredEnable = 0;
            psVolHdr->bQuarterSample = 0;
            psVolHdr->bDeblockFilterDisable = TRUE;
            psVolHdr->uiSprite = 0;
            psVolHdr->bNot8Bit = 0;
            psVolHdr->bComplexityEstimationDisable = 1;
            psVolHdr->bDataPartitioning = 0;
            psVolHdr->bReversibleVlc = FALSE;
            psVolHdr->bDeblockFilterDisable = TRUE;

            return eRetVal;
        }

        psVolHdr->bShapeOnly = FALSE;
        if (uiAUsage == 3)
            uiAUsage = 2;
        psVolHdr->fAUsage = (ALPHA_USAGE)uiAUsage;
        if (psVolHdr->fAUsage == RECTANGLE) {
            uiMarker = GET_ONE_BIT(pStreamReader);
            ASSERT(uiMarker == 1);
            psVolHdr->iVolWidth = BR_fnGetBits(NUMBITS_VOP_WIDTH, pStreamReader);
            uiMarker = GET_ONE_BIT(pStreamReader);
            ASSERT(uiMarker == 1);
            psVolHdr->iVolHeight = BR_fnGetBits(NUMBITS_VOP_HEIGHT, pStreamReader);
            uiMarker = GET_ONE_BIT(pStreamReader);
            ASSERT(uiMarker == 1);
        }

        psVopHdr->bInterlace = GET_ONE_BIT(pStreamReader);      /* interlace (was vop flag) */
        psVolHdr->bAdvPredDisable = GET_ONE_BIT(pStreamReader); /*VOL_obmc_Disable */

        /* decode sprite info */
        if (psVolHdr->uiVerID == 1) {
            psVolHdr->uiSprite = GET_ONE_BIT(pStreamReader);
        } else {
            psVolHdr->uiSprite = BR_fnGetBits(2, pStreamReader);
        }

        if (psVolHdr->uiSprite == 1) { /* sprite information */
            psVolHdr->iSpriteWidth = BR_fnGetBits(NUMBITS_SPRITE_HDIM, pStreamReader);
            uiMarker = BR_fnGetBits(NUMBITS_MARKERBIT, pStreamReader);
            ASSERT(uiMarker == 1);
            psVolHdr->iSpriteHeight = BR_fnGetBits(NUMBITS_SPRITE_VDIM, pStreamReader);
            uiMarker = BR_fnGetBits(NUMBITS_MARKERBIT, pStreamReader);
            ASSERT(uiMarker == 1);
            psVolHdr->iSpriteLeftEdge =
                    (GET_ONE_BIT(pStreamReader) == 0)
                            ? BR_fnGetBits(NUMBITS_SPRITE_LEFT_EDGE - 1, pStreamReader)
                            : ((I32)BR_fnGetBits(NUMBITS_SPRITE_LEFT_EDGE - 1, pStreamReader) -
                               (1 << 12));
            ASSERT(psVolHdr->iSpriteLeftEdge % 2 == 0);
            uiMarker = BR_fnGetBits(NUMBITS_MARKERBIT, pStreamReader);
            ASSERT(uiMarker == 1);
            psVolHdr->iSpriteTopEdge =
                    (GET_ONE_BIT(pStreamReader) == 0)
                            ? BR_fnGetBits(NUMBITS_SPRITE_TOP_EDGE - 1, pStreamReader)
                            : ((I32)BR_fnGetBits(NUMBITS_SPRITE_LEFT_EDGE - 1, pStreamReader) -
                               (1 << 12));
            ASSERT(psVolHdr->iSpriteTopEdge % 2 == 0);
            uiMarker = BR_fnGetBits(NUMBITS_MARKERBIT, pStreamReader);
            ASSERT(uiMarker == 1);
        }
        psVolHdr->bLightChange = 0;

        if (psVolHdr->uiSprite == 1 || psVolHdr->uiSprite == 2) { /* sprite information */
            psVolHdr->iNumOfPnts = BR_fnGetBits(NUMBITS_NUM_SPRITE_POINTS, pStreamReader);
            if (psVolHdr->uiSprite == 2) {
                ASSERT(psVolHdr->iNumOfPnts == 0 || psVolHdr->iNumOfPnts == 1 ||
                       psVolHdr->iNumOfPnts == 2 || psVolHdr->iNumOfPnts == 3);
            }
#if 0
            m_rgstSrcQ = new CSiteD [psVolHdr->iNumOfPnts];
            m_rgstDstQ = new CSiteD [psVolHdr->iNumOfPnts];
#else
            // CHKEXPEXIT( FALSE, "Sorry, not support this feature" );
#endif
            psVolHdr->uiWarpingAccuracy = BR_fnGetBits(NUMBITS_WARPING_ACCURACY, pStreamReader);
            psVolHdr->bLightChange = GET_ONE_BIT(pStreamReader);

            if (psVolHdr->uiSprite == 2) {
                ASSERT(psVolHdr->bLightChange == 0);
            }
        }
        if (psVolHdr->uiSprite == 1) { /* sprite information */

            psVolHdr->bSpriteMode = GET_ONE_BIT(pStreamReader); /* Low_latency_sprite_enable */
#if 0
            if ( bsptMode )
                m_sptMode = LOW_LATENCY ;
            else            
                m_sptMode = BASIC_SPRITE ;
#endif
        }

        if (psVolHdr->fAUsage != RECTANGLE) {
            if (psVolHdr->uiVerID == 1) {
                psVolHdr->bSadctDisable = TRUE;
            } else {
                psVolHdr->bSadctDisable = GET_ONE_BIT(pStreamReader);
            }
        } else {
            psVolHdr->bSadctDisable = TRUE;
        }

        psVolHdr->bNot8Bit = (bool)GET_ONE_BIT(pStreamReader);
        if (psVolHdr->bNot8Bit) {
            psVolHdr->uiQuantPrecision = (U32)BR_fnGetBits(4, pStreamReader);
            psVolHdr->nBits = (U32)BR_fnGetBits(4, pStreamReader);
            ASSERT(psVolHdr->nBits > 3);
        } else {
            psVolHdr->uiQuantPrecision = 5;
            psVolHdr->nBits = 8;
        }

        if (psVolHdr->fAUsage == EIGHT_BIT) {
            psVolHdr->bNoGrayQuantUpdate = GET_ONE_BIT(pStreamReader);
            psVolHdr->uiCompMethod = GET_ONE_BIT(pStreamReader);
            psVolHdr->uiLinearComp = GET_ONE_BIT(pStreamReader);
        }

        psVolHdr->fQuantizer = (QUANTIZER)GET_ONE_BIT(pStreamReader);
        if (psVolHdr->fQuantizer == Q_MPEG) {
            psVolHdr->bLoadIntraMatrix = GET_ONE_BIT(pStreamReader);
#if 0 /* no need to load quantization matrix */
            if (psVolHdr->bLoadIntraMatrix)
            {
                U32 i = 0, j;
                I32 iElem;
                do 
                {
                    iElem = BR_fnGetBits( NUMBITS_QMATRIX);
                    psVolHdr->rgiIntraQuantizerMatrix [grgiStandardZigzag[i]] = iElem;
                } while (iElem != 0 && ++i < BLOCK_SQUARE_SIZE);

                for (j = i; j < BLOCK_SQUARE_SIZE; j++)
                {
                    psVolHdr->rgiIntraQuantizerMatrix [grgiStandardZigzag[j]] = psVolHdr->rgiIntraQuantizerMatrix [grgiStandardZigzag[i - 1]];
                }
            }
            else
            {
                memcpy (psVolHdr->rgiIntraQuantizerMatrix, rgiDefaultIntraQMatrix, BLOCK_SQUARE_SIZE * sizeof (I32));
            }
#else
            if (psVolHdr->bLoadIntraMatrix) {
                U32 i = 0;
                I32 iElem;
                do {
                    iElem = BR_fnGetBits(NUMBITS_QMATRIX, pStreamReader);
                } while (iElem != 0 && ++i < BLOCK_SQUARE_SIZE);
            }
#endif
            psVolHdr->bLoadInterMatrix = GET_ONE_BIT(pStreamReader);
#if 0 /* no need to load quantization matrix */
            if (psVolHdr->bLoadInterMatrix)
            {
                U32 i = 0, j;
                I32 iElem;
                do
                {
                    iElem = BR_fnGetBits( NUMBITS_QMATRIX);
                    psVolHdr->rgiInterQuantizerMatrix [grgiStandardZigzag[i]] = iElem;
                } while (iElem != 0 && ++i < BLOCK_SQUARE_SIZE);

                for (j = i; j < BLOCK_SQUARE_SIZE; j++)
                {
                    psVolHdr->rgiInterQuantizerMatrix [grgiStandardZigzag[j]] = psVolHdr->rgiInterQuantizerMatrix [grgiStandardZigzag[i - 1]];
                }
            }
            else {
                memcpy (psVolHdr->rgiInterQuantizerMatrix, rgiDefaultInterQMatrix, BLOCK_SQUARE_SIZE * sizeof (I32));
            }
#else
            if (psVolHdr->bLoadInterMatrix) {
                U32 i = 0;
                I32 iElem;
                do {
                    iElem = BR_fnGetBits(NUMBITS_QMATRIX, pStreamReader);
                } while (iElem != 0 && ++i < BLOCK_SQUARE_SIZE);
            }
#endif
            if (psVolHdr->fAUsage == EIGHT_BIT) {
                I32 iAuxComp;
                for (iAuxComp = 0; iAuxComp < psVolHdr->iAuxCompCount; iAuxComp++) {
                    psVolHdr->bLoadIntraMatrixAlpha = GET_ONE_BIT(pStreamReader);
#if 0 /* no need to load quantization matrix */
                    if (psVolHdr->bLoadIntraMatrixAlpha)
                    {
                        U32 i = 0;
                        for (i = 0; i < BLOCK_SQUARE_SIZE; i++)
                        {
                            I32 iElem = BR_fnGetBits( NUMBITS_QMATRIX);
                            if (iElem != 0)
                            {
                                psVolHdr->rgiIntraQuantizerMatrixAlpha[iAuxComp] [grgiStandardZigzag[i]] = iElem;
                            }
                            else
                            {
                                psVolHdr->rgiIntraQuantizerMatrixAlpha[iAuxComp] [i] = psVolHdr->rgiIntraQuantizerMatrixAlpha[iAuxComp] [grgiStandardZigzag[i - 1]];
                            }
                        }
                    }
                    else
                    {
                        // use rgiDefaultIntraQMatrixAlpha instead of rgiDefaultIntraQMatrix (both defined in ./sys/global.hpp), mwi
                        memcpy (psVolHdr->rgiIntraQuantizerMatrixAlpha[iAuxComp], rgiDefaultIntraQMatrixAlpha, BLOCK_SQUARE_SIZE * sizeof (I32));
                    }
#else
                    if (psVolHdr->bLoadIntraMatrixAlpha) {
                        U32 i = 0;
                        for (i = 0; i < BLOCK_SQUARE_SIZE; i++) {
                            (void)BR_fnGetBits(NUMBITS_QMATRIX, pStreamReader);
                        }
                    }
#endif
                    psVolHdr->bLoadInterMatrixAlpha = GET_ONE_BIT(pStreamReader);
#if 0 /* no need to load quantization matrix */
                    if (psVolHdr->bLoadInterMatrixAlpha)
                    {
                        U32 i = 0;
                        for (i = 0; i < BLOCK_SQUARE_SIZE; i++)
                        {
                            I32 iElem = BR_fnGetBits( NUMBITS_QMATRIX);
                            if (iElem != 0)
                            {
                                psVolHdr->rgiInterQuantizerMatrixAlpha[iAuxComp] [grgiStandardZigzag[i]] = iElem;
                            }
                            else
                            {
                                psVolHdr->rgiInterQuantizerMatrixAlpha[iAuxComp] [i] = psVolHdr->rgiInterQuantizerMatrixAlpha[iAuxComp] [grgiStandardZigzag[i - 1]];
                            }
                        }
                    }
                    else
                    {
                        // use rgiDefaultInterQMatrixAlpha instead of rgiDefaultInterQMatrix (both defined in ./sys/global.hpp), mwi
                        memcpy (psVolHdr->rgiInterQuantizerMatrixAlpha[iAuxComp], rgiDefaultInterQMatrixAlpha, BLOCK_SQUARE_SIZE * sizeof (I32));
                    }
#else
                    if (psVolHdr->bLoadInterMatrixAlpha) {
                        U32 i = 0;
                        for (i = 0; i < BLOCK_SQUARE_SIZE; i++) {
                            (void)BR_fnGetBits(NUMBITS_QMATRIX, pStreamReader);
                        }
                    }
#endif
                }
            }
        }

        if (psVolHdr->uiVerID == 1) {
            /* added for compatibility with version 1 video */
            /* this is tentative integration */
            /* please change codes if there is any problems */
            psVolHdr->bQuarterSample = 0;  // Quarter sample
        } else {
            psVolHdr->bQuarterSample = GET_ONE_BIT(pStreamReader); /*Quarter sample */
        }

        /* START: Complexity Estimation syntax support */

        psVolHdr->bComplexityEstimationDisable = GET_ONE_BIT(pStreamReader);
        if (!psVolHdr->bComplexityEstimationDisable) {
            psVolHdr->iEstimationMethod = BR_fnGetBits(2, pStreamReader);
            if ((psVolHdr->iEstimationMethod == 0) || (psVolHdr->iEstimationMethod == 1)) {
                psVolHdr->bShapeComplexityEstimationDisable = GET_ONE_BIT(pStreamReader);
                if (!psVolHdr->bShapeComplexityEstimationDisable) {
                    psVolHdr->bOpaque = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bTransparent = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bIntraCAE = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bInterCAE = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bNoUpdate = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bUpsampling = GET_ONE_BIT(pStreamReader);
                    if (!(psVolHdr->bOpaque || psVolHdr->bTransparent || psVolHdr->bIntraCAE ||
                          psVolHdr->bInterCAE || psVolHdr->bNoUpdate || psVolHdr->bUpsampling)) {
                        eRetVal = E_MPG4_NOT_SUPPORT_131;
                        return eRetVal;
                    }
                } else {
                    psVolHdr->bOpaque = psVolHdr->bTransparent = psVolHdr->bIntraCAE =
                            psVolHdr->bInterCAE = psVolHdr->bNoUpdate = psVolHdr->bUpsampling =
                                    FALSE;
                }

                psVolHdr->bTextureComplexityEstimationSet1Disable = GET_ONE_BIT(pStreamReader);
                if (psVolHdr->uiSprite == 2) {
                    ASSERT(psVolHdr->bTextureComplexityEstimationSet1Disable == TRUE);
                }
                if (!psVolHdr->bTextureComplexityEstimationSet1Disable) {
                    psVolHdr->bIntraBlocks = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bInterBlocks = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bInter4vBlocks = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bNotCodedBlocks = GET_ONE_BIT(pStreamReader);
                    if (!(psVolHdr->bIntraBlocks || psVolHdr->bInterBlocks ||
                          psVolHdr->bInter4vBlocks || psVolHdr->bNotCodedBlocks)) {
                        eRetVal = E_MPG4_NOT_SUPPORT_131;
                        return eRetVal;
                    }
                } else {
                    psVolHdr->bIntraBlocks = psVolHdr->bInterBlocks = psVolHdr->bInter4vBlocks =
                            psVolHdr->bNotCodedBlocks = FALSE;
                }

                uiMarker = GET_ONE_BIT(pStreamReader);
                ASSERT(uiMarker == 1);

                psVolHdr->bTextureComplexityEstimationSet2Disable = GET_ONE_BIT(pStreamReader);
                if (!psVolHdr->bTextureComplexityEstimationSet2Disable) {
                    psVolHdr->bDCTCoefs = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bDCTLines = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bVLCSymbols = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bVLCBits = GET_ONE_BIT(pStreamReader);
                    if (!(psVolHdr->bDCTCoefs || psVolHdr->bDCTLines || psVolHdr->bVLCSymbols ||
                          psVolHdr->bVLCBits)) {
                        eRetVal = E_MPG4_NOT_SUPPORT_131;
                        return eRetVal;
                    }
                } else {
                    psVolHdr->bDCTCoefs = psVolHdr->bDCTLines = psVolHdr->bVLCSymbols =
                            psVolHdr->bVLCBits = FALSE;
                }

                psVolHdr->bMotionCompensationComplexityDisable = GET_ONE_BIT(pStreamReader);
                if (!psVolHdr->bMotionCompensationComplexityDisable) {
                    psVolHdr->bAPM = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bNPM = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bInterpolateMCQ = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bForwBackMCQ = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bHalfpel2 = GET_ONE_BIT(pStreamReader);
                    psVolHdr->bHalfpel4 = GET_ONE_BIT(pStreamReader);
                    if (!(psVolHdr->bAPM || psVolHdr->bNPM || psVolHdr->bInterpolateMCQ ||
                          psVolHdr->bForwBackMCQ || psVolHdr->bHalfpel2 || psVolHdr->bHalfpel4)) {
                        eRetVal = E_MPG4_NOT_SUPPORT_131;
                        return eRetVal;
                    }
                } else {
                    psVolHdr->bAPM = psVolHdr->bNPM = psVolHdr->bInterpolateMCQ =
                            psVolHdr->bForwBackMCQ = psVolHdr->bHalfpel2 = psVolHdr->bHalfpel4 =
                                    FALSE;
                }

                uiMarker = GET_ONE_BIT(pStreamReader);
                ASSERT(uiMarker == 1);

                /* START: Complexity Estimation syntax support */
                if (psVolHdr->iEstimationMethod == 1) {
                    psVolHdr->bVersion2ComplexityEstimationDisable = GET_ONE_BIT(pStreamReader);
                    if (!psVolHdr->bVersion2ComplexityEstimationDisable) {
                        psVolHdr->bSadct = GET_ONE_BIT(pStreamReader);
                        psVolHdr->bQuarterpel = GET_ONE_BIT(pStreamReader);
                        if (!(psVolHdr->bSadct || psVolHdr->bQuarterpel)) {
                            eRetVal = E_MPG4_NOT_SUPPORT_131;
                            return eRetVal;
                        }
                    } else {
                        psVolHdr->bSadct = psVolHdr->bQuarterpel = FALSE;
                    }

                } else {
                    psVolHdr->bVersion2ComplexityEstimationDisable = TRUE;
                    psVolHdr->bSadct = psVolHdr->bQuarterpel = FALSE;
                }
                /* END: Complexity Estimation syntax support */

                /* START: Complexity Estimation syntax support */
                /* Main complexity estimation flag test */
                if (psVolHdr->bShapeComplexityEstimationDisable &&
                    psVolHdr->bTextureComplexityEstimationSet1Disable &&
                    psVolHdr->bTextureComplexityEstimationSet2Disable &&
                    psVolHdr->bMotionCompensationComplexityDisable &&
                    psVolHdr->bVersion2ComplexityEstimationDisable) {
                    eRetVal = E_MPG4_NOT_SUPPORT_131;
                    return eRetVal;
                }
                /* END: Complexity Estimation syntax support */
            }
        }
        /* END: Complexity Estimation syntax support */

        psVolHdr->uiResyncMarkerDisable = GET_ONE_BIT(pStreamReader);

        psVolHdr->bDataPartitioning = GET_ONE_BIT(pStreamReader);
        if (psVolHdr->bDataPartitioning) {
            psVolHdr->bReversibleVlc = GET_ONE_BIT(pStreamReader);
        } else {
            psVolHdr->bReversibleVlc = FALSE;
        }

        if (psVolHdr->uiVerID == 1) {
            psVolHdr->bNewpredEnable = FALSE;
        } else if (psVolHdr->uiVerID != 1) {
            psVolHdr->bNewpredEnable = GET_ONE_BIT(pStreamReader);
        }

        if (psVolHdr->bNewpredEnable) {
            ASSERT(psVolHdr->fAUsage == RECTANGLE);
            ASSERT(psVopHdr->bInterlace == 0);
            ASSERT(psVolHdr->uiSprite == 0);
            psVolHdr->iRequestedBackwardMessegeType = BR_fnGetBits(2, pStreamReader);
            psVolHdr->bNewpredSegmentType = GET_ONE_BIT(pStreamReader);

#if 0
            // generate NEWPRED object
            g_pNewPredDec = new CNewPredDecoder();
#else
            eRetVal = E_MPG4_NOT_SUPPORT_131;
            return eRetVal;
#endif
        }

        if (psVolHdr->uiVerID != 1) {
            psVolHdr->breduced_resolution_vop_enable = GET_ONE_BIT(pStreamReader);
        } else {
            psVolHdr->breduced_resolution_vop_enable = 0;
        }
        psVopHdr->RRVmode.iOnOff = psVolHdr->breduced_resolution_vop_enable;

        psVolHdr->volType = (GET_ONE_BIT(pStreamReader) == 0) ? BASE_LAYER : ENHN_LAYER;

        if (psVolHdr->bNewpredEnable) {
            ASSERT(psVolHdr->volType == BASE_LAYER);
        }

        if (psVolHdr->uiSprite == 2) {
            ASSERT(psVolHdr->volType == BASE_LAYER);
        }

        psVolHdr->ihor_sampling_factor_n = 1;
        psVolHdr->ihor_sampling_factor_m = 1;
        psVolHdr->iver_sampling_factor_n = 1;
        psVolHdr->iver_sampling_factor_m = 1;
        psVolHdr->ihor_sampling_factor_n_shape = 1;
        psVolHdr->ihor_sampling_factor_m_shape = 1;
        psVolHdr->iver_sampling_factor_n_shape = 1;
        psVolHdr->iver_sampling_factor_m_shape = 1;

        if (psVolHdr->volType == ENHN_LAYER) {
            psVolHdr->iHierarchyType = GET_ONE_BIT(pStreamReader);  //
#if 0
            if(psVolHdr->iHierarchyType == 0)
            {
                fprintf(stderr,"Hierarchy_Type == 0 (Spatial scalability)\n");
            }
            else if(psVolHdr->iHierarchyType == 1)
            {
                fprintf(stderr,"Hierarchy_type == 1 (Temporal scalability)\n");
            }
#endif
            BR_fnGetBits(4, pStreamReader); /* ref_layer_id */
            GET_ONE_BIT(pStreamReader);     /* ref_layer_samping_director */
            psVolHdr->ihor_sampling_factor_n = BR_fnGetBits(5, pStreamReader);
            psVolHdr->ihor_sampling_factor_m = BR_fnGetBits(5, pStreamReader);
            psVolHdr->iver_sampling_factor_n = BR_fnGetBits(5, pStreamReader);
            psVolHdr->iver_sampling_factor_m = BR_fnGetBits(5, pStreamReader);
            psVolHdr->iEnhnType = GET_ONE_BIT(pStreamReader); /*enhancement_type */

            if (psVolHdr->fAUsage == ONE_BIT && psVolHdr->iHierarchyType == 0) {
                psVolHdr->iuseRefShape = GET_ONE_BIT(pStreamReader);   /* use_ref_shape */
                psVolHdr->iuseRefTexture = GET_ONE_BIT(pStreamReader); /* use_ref_texture */
                psVolHdr->ihor_sampling_factor_n_shape = BR_fnGetBits(5, pStreamReader);
                psVolHdr->ihor_sampling_factor_m_shape = BR_fnGetBits(5, pStreamReader);
                psVolHdr->iver_sampling_factor_n_shape = BR_fnGetBits(5, pStreamReader);
                psVolHdr->iver_sampling_factor_m_shape = BR_fnGetBits(5, pStreamReader);
            }
        }

        psVolHdr->bDeblockFilterDisable = TRUE; /*no deblocking filter */
#if 0
        if (psVolHdr->bDeblockFilterDisable == FALSE)
            printf("Enable Deblocking Filter\n");
#endif
    }

    return 0;
}

/********************************************************************
 *
 * MPG4VP_fnGetGovHdr - parse GOV header from MPEG-4 bistream
 *
 * Description:
 *   This function parses GOV header from MPEG-4 bistream
 *
 * Arguments:
 *   pMPG4Obj               [IN/OUT]MPEG-4 object structure pointer
 *
 * Return:
 *   E_MPG4_EOF_127         reach the end of file
 *   E_MPG4_OK_0            parse GOV header successfully
 *   E_MPG4_BROKEN_HDR_129  the GOV header is broken
 *
 ********************************************************************/
static int MPG4VP_fnGetGovHdr(MPG4_OBJECT* _pMPG4Obj) {
    MPG4_OBJECT* pMPG4Obj = _pMPG4Obj;
    VOID* pStreamReader = NULL;
    MPG4_GOV_HEADER* psGovHdr = NULL;
    MPG4_VOP_HEADER* psVopHdr = NULL;

    MPG4_RETURN eRetVal = E_MPG4_OK_0;

    ASSERT(_pMPG4Obj != NULL);
    ASSERT((_pMPG4Obj->pStreamReader != NULL) && (_pMPG4Obj->psGovHdr != NULL) &&
           (_pMPG4Obj->psVopHdr != NULL));
    pStreamReader = pMPG4Obj->pStreamReader;
    psGovHdr = pMPG4Obj->psGovHdr;
    psVopHdr = pMPG4Obj->psVopHdr;

    psGovHdr->iTimeCode = BR_fnGetBits(5, pStreamReader) * 3600;
    psGovHdr->iTimeCode += BR_fnGetBits(6, pStreamReader) * 60;
    (void)GET_ONE_BIT(pStreamReader);
    psGovHdr->iTimeCode += BR_fnGetBits(6, pStreamReader);

    psVopHdr->iModuloBaseDecd = psGovHdr->iTimeCode;
    psVopHdr->iModuloBaseDisp = psGovHdr->iTimeCode;

    psGovHdr->iClosedGov = GET_ONE_BIT(pStreamReader);
    psGovHdr->iBrokenLink = GET_ONE_BIT(pStreamReader);

    return eRetVal;
}

static int MPG4VP_fnGetVopHdr(MPG4_OBJECT* _pMPG4Obj) {
    MPG4_OBJECT* pMPG4Obj = _pMPG4Obj;
    VOID* pStreamReader = NULL;
    MPG4_VDO_HEADER* psVdoHdr = NULL;
    MPG4_VOL_HEADER* psVolHdr = NULL;
    MPG4_VOP_HEADER* psVopHdr = NULL;
    I32 iModuloInc = 0, iVopIncr = 0, iAuxComp;
    U32 uiMarker;
    MPG4_RETURN eRetVal = E_MPG4_OK_0;

    ASSERT(_pMPG4Obj != NULL);
    ASSERT((_pMPG4Obj->pStreamReader != NULL) && (_pMPG4Obj->psVdoHdr != NULL) &&
           (_pMPG4Obj->psVolHdr != NULL) && (_pMPG4Obj->psVopHdr != NULL));
    pStreamReader = pMPG4Obj->pStreamReader;
    psVdoHdr = pMPG4Obj->psVdoHdr;
    psVolHdr = pMPG4Obj->psVolHdr;
    psVopHdr = pMPG4Obj->psVopHdr;

    psVopHdr->vopPredType = (VOP_PRED_TYPE)BR_fnGetBits(NUMBITS_VOP_PRED_TYPE, pStreamReader);

    ASSERT((!psVolHdr->bTemporalFGS) || (psVopHdr->vopPredType == 0) ||
           (psVopHdr->vopPredType == 1) || (psVopHdr->vopPredType == 2));
#if 0
    if (psVolHdr->bTemporalFGS)
    {
        switch (psVopHdr->vopPredType)
        {
        case 0: 
            fgs_vop_coding_type = FGS_VOP_CODING_TYPE_I;
            ASSERT(fgs_vop_coding_type != FGS_VOP_CODING_TYPE_I);    // FPDAM4 by INTEL: not a right entry.
            break;
        case 1:
            fgs_vop_coding_type = FGS_VOP_CODING_TYPE_P;
            break;
        case 2:
            fgs_vop_coding_type = FGS_VOP_CODING_TYPE_B;
            break;
        default:
            ASSERT(0);
        }
    }
#endif

    if (psVolHdr->bNewpredEnable) {
        ASSERT(psVopHdr->vopPredType != BVOP);
    }

    while (GET_ONE_BIT(pStreamReader) != 0) {
        iModuloInc++;
    }
    psVopHdr->iCurrSec =
            iModuloInc + ((psVopHdr->vopPredType != BVOP ||
                           (psVopHdr->vopPredType == BVOP && psVolHdr->volType == ENHN_LAYER))
                                  ? psVopHdr->iModuloBaseDecd
                                  : psVopHdr->iModuloBaseDisp);

    uiMarker = GET_ONE_BIT(pStreamReader);
    ASSERT(uiMarker == 1);

    if (psVolHdr->iNumBitsTimeIncr != 0) {
        iVopIncr = BR_fnGetBits(psVolHdr->iNumBitsTimeIncr, pStreamReader);
    }
    uiMarker = GET_ONE_BIT(pStreamReader); /* marker bit */
    ASSERT(uiMarker == 1);
    psVopHdr->iOldModuloBaseDecd = psVopHdr->iModuloBaseDecd;
    psVopHdr->iOldModuloBaseDisp = psVopHdr->iModuloBaseDisp;
    if (psVopHdr->vopPredType != BVOP ||
        (psVopHdr->vopPredType == BVOP && psVolHdr->volType == ENHN_LAYER))

    {
        psVopHdr->iModuloBaseDisp =
                psVopHdr->iModuloBaseDecd; /*update most recently displayed time base */
        psVopHdr->iModuloBaseDecd = psVopHdr->iCurrSec;
    }

    psVopHdr->iTimeStamp =
            (I32)(psVopHdr->iCurrSec * psVolHdr->iClockRate * psVolHdr->dClockRateScale +
                  iVopIncr * psVolHdr->dClockRateScale);

    psVopHdr->iTimeSecond = psVopHdr->iCurrSec;
    psVopHdr->iTimeIncrem = iVopIncr;

    if (psVopHdr->iFrameInterval == 0 && psVopHdr->bInterlace && psVopHdr->vopPredType == BVOP) {
        psVopHdr->iFrameInterval = psVopHdr->iTimeStamp - psVopHdr->iPastRef;
    }

    if (psVolHdr->bTemporalFGS) {
#if 0
        fgs_vop_max_level_y                         = (I32) BR_fnGetBits( 5);        
        fgs_vop_max_level_u                        = (I32) BR_fnGetBits( 5);
        fgs_vop_max_level_v                        = (I32) BR_fnGetBits( 5);
        uiMarker = GET_ONE_BIT(pStreamReader); /* marker bit */
        fgs_vop_number_of_vop_bp_coded            = (I32) BR_fnGetBits( 5);
        fgs_vop_mc_bit_plane_not_used            = (I32) BR_fnGetBits( 5);
        fgs_vop_selective_enhancement_enable    = (I32) GET_ONE_BIT(pStreamReader);

        // INTERLACE, Added by Hong Jiang, INTEL, 8/14/00
        psVopHdr->bAlternateScan = FALSE;
        if (psVopHdr->bInterlace) {
            psVopHdr->bTopFieldFirst = GET_ONE_BIT(pStreamReader);
        } 

        if (fgs_vop_coding_type != FGS_VOP_CODING_TYPE_I)    /* This is always TRUE */
        {
            psVopHdr->mvInfoForward.uiFCode       = BR_fnGetBits( NUMBITS_VOP_FCODE);
            psVopHdr->mvInfoForward.uiScaleFactor = 1 << (psVopHdr->mvInfoForward.uiFCode - 1);
            psVopHdr->mvInfoForward.uiRange       = 16 << psVopHdr->mvInfoForward.uiFCode;
            if (fgs_vop_coding_type == FGS_VOP_CODING_TYPE_B)
            {
                psVopHdr->mvInfoBackward.uiFCode       = BR_fnGetBits( NUMBITS_VOP_FCODE);
                psVopHdr->mvInfoBackward.uiScaleFactor = 1 << (psVopHdr->mvInfoBackward.uiFCode - 1);
                psVopHdr->mvInfoBackward.uiRange       = 16 << psVopHdr->mvInfoBackward.uiFCode;
            }
            psVopHdr->iRefSelectCode = m_pbitstrmIn ->getBits (2) ;
            // refSelCode must be '11' for bvop
            // refSelCode must be '01' or '10' for pvop
            //            ASSERT( (!psVopHdr->bTemporalFGS) || // either not fgs-ts
            ASSERT(    ((psVopHdr->vopPredType == BVOP) && (psVopHdr->iRefSelectCode == 3)) ||    
                ((psVopHdr->vopPredType == PVOP) && ((psVopHdr->iRefSelectCode == 1) || (psVopHdr->iRefSelectCode == 2))) );
        }

        // added by Hong Jiang, INTEL, 8/14/00
        psVopHdr->iRoundingControl = 0;

#ifdef _GENERATE_INDEX_FILE_
        if (g_LoadOrg) {
            m_pFGSTMeta->WriteVopMetaData(VopNo, g_vop_start, (VOPpredType)fgs_vop_coding_type);
            m_pFGSTMeta->WriteToMetaFile("\n", 1);
        }
#endif

        return TRUE;
#else
        eRetVal = E_MPG4_NOT_SUPPORT_131;
        return eRetVal;
#endif
    }

    psVopHdr->bVopCoded = GET_ONE_BIT(pStreamReader);
    if (psVopHdr->bVopCoded == 0) {  // vop_coded == FALSE
        return eRetVal;
    }

    if (psVolHdr->bNewpredEnable) {
        psVopHdr->m_iVopID = BR_fnGetBits(psVopHdr->m_iNumBitsVopID, pStreamReader);
        psVopHdr->m_iVopID4Prediction_Indication =
                BR_fnGetBits(NUMBITS_VOP_ID_FOR_PRED, pStreamReader);
        if (psVopHdr->m_iVopID4Prediction_Indication) {
            psVopHdr->m_iVopID4Prediction = BR_fnGetBits(psVopHdr->m_iNumBitsVopID, pStreamReader);
        }
        BR_fnGetBits(NUMBITS_MARKERBIT, pStreamReader);
#if 0 /* Larry : can be removed? */
        g_pNewPredDec->GetRef(
            NP_VOP_HEADER,
            psVopHdr->vopPredType,
            psVopHdr->m_iVopID,    
            psVopHdr->m_iVopID4Prediction_Indication,
            psVopHdr->m_iVopID4Prediction
            );
#endif
    }

#if 0 /* Larry : can be removed? */ /* Original code : #ifdef _VOP_MC_SWITCHING_ */
    // frame_level_qpel
    if (psVopHdr->vopPredType == PVOP) {
        if (psVopHdr->bSeqQuarterSample) {
            psVopHdr->bQuarterSample = GET_ONE_BIT(pStreamReader);
        }
    }
    // ~frame_level_qpel
#endif

    if ((psVopHdr->vopPredType == PVOP ||
         (psVolHdr->uiSprite == 2 && psVopHdr->vopPredType == SPRITE)) &&
        psVolHdr->bShapeOnly == FALSE) /* GMC */
    {
        psVopHdr->iRoundingControl = GET_ONE_BIT(pStreamReader); /*"VOP_Rounding_Type" */
    } else {
        psVopHdr->iRoundingControl = 0;
    }

    if ((psVolHdr->breduced_resolution_vop_enable == 1) && (psVolHdr->fAUsage == RECTANGLE) &&
        ((psVopHdr->vopPredType == PVOP) || (psVopHdr->vopPredType == IVOP))) {
        psVopHdr->RRVmode.iRRVOnOff = GET_ONE_BIT(pStreamReader);
    } else {
        psVopHdr->RRVmode.iRRVOnOff = 0;
    }

    if (psVolHdr->fAUsage != RECTANGLE) {
        if (!(psVolHdr->uiSprite == 1 && psVopHdr->vopPredType == IVOP)) {
            psVopHdr->iVopWidth = BR_fnGetBits(NUMBITS_VOP_WIDTH, pStreamReader);
            uiMarker = GET_ONE_BIT(pStreamReader); /* marker bit */
            ASSERT(uiMarker == 1);
            psVopHdr->iVopHeight = BR_fnGetBits(NUMBITS_VOP_HEIGHT, pStreamReader);
            uiMarker = GET_ONE_BIT(pStreamReader); /* marker bit */
            ASSERT(uiMarker == 1);
            psVopHdr->iVopHoriMcSpaRef =
                    (GET_ONE_BIT(pStreamReader) == 0)
                            ? BR_fnGetBits(NUMBITS_VOP_HORI_SPA_REF - 1, pStreamReader)
                            : ((I32)BR_fnGetBits(NUMBITS_VOP_HORI_SPA_REF - 1, pStreamReader) -
                               (1 << (NUMBITS_VOP_HORI_SPA_REF - 1)));
            uiMarker = GET_ONE_BIT(pStreamReader); /* marker bit */
            ASSERT(uiMarker == 1);
            psVopHdr->iVopVertMcSpaRef =
                    (GET_ONE_BIT(pStreamReader) == 0)
                            ? BR_fnGetBits(NUMBITS_VOP_VERT_SPA_REF - 1, pStreamReader)
                            : ((I32)BR_fnGetBits(NUMBITS_VOP_VERT_SPA_REF - 1, pStreamReader) -
                               (1 << (NUMBITS_VOP_VERT_SPA_REF - 1)));
            ASSERT(((psVopHdr->iVopHoriMcSpaRef | psVopHdr->iVopVertMcSpaRef) & 1) ==
                   0);                             /* must be even pix unit */
            uiMarker = GET_ONE_BIT(pStreamReader); /* marker bit */
            ASSERT(uiMarker == 1);
        }

        if (psVolHdr->volType == ENHN_LAYER && psVolHdr->iEnhnType != 0) {
            psVopHdr->bBGComposition = GET_ONE_BIT(pStreamReader);
        }

        psVolHdr->bNoCrChange = GET_ONE_BIT(pStreamReader); /*VOP_CR_Change_Disable */
        psVopHdr->iVopConstantAlpha = GET_ONE_BIT(pStreamReader);
        if (psVopHdr->iVopConstantAlpha == 1) {
            psVopHdr->iVopConstantAlphaValue = GET_ONE_BYTE(pStreamReader);
        } else {
            psVopHdr->iVopConstantAlphaValue = 255;
        }
        psVopHdr->bShapeCodingType = (psVopHdr->vopPredType == IVOP) ? 0 : 1;
    }

    if (!psVolHdr->bComplexityEstimationDisable) {
        if ((psVolHdr->iEstimationMethod == 0) || (psVolHdr->iEstimationMethod == 1)) {
            if (psVopHdr->vopPredType == IVOP || psVopHdr->vopPredType == PVOP ||
                psVopHdr->vopPredType == BVOP) {
                if (psVolHdr->bOpaque) {
                    psVopHdr->iOpaque = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iOpaque == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bTransparent) {
                    psVopHdr->iTransparent = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iTransparent == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bIntraCAE) {
                    psVopHdr->iIntraCAE = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iIntraCAE == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bInterCAE) {
                    psVopHdr->iInterCAE = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iInterCAE == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bNoUpdate) {
                    psVopHdr->iNoUpdate = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iNoUpdate == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bUpsampling) {
                    psVopHdr->iUpsampling = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iUpsampling == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
            }

            if (psVolHdr->bIntraBlocks) {
                psVopHdr->iIntraBlocks = GET_ONE_BYTE(pStreamReader);
                if (psVopHdr->iIntraBlocks == 0) {
                    return E_MPG4_INVALID_STREAM;
                }
            }
            if (psVolHdr->bNotCodedBlocks) {
                psVopHdr->iNotCodedBlocks = GET_ONE_BYTE(pStreamReader);
                if (psVopHdr->iNotCodedBlocks == 0) {
                    return E_MPG4_INVALID_STREAM;
                }
            }
            if (psVolHdr->bDCTCoefs) {
                psVopHdr->iDCTCoefs = GET_ONE_BYTE(pStreamReader);
                if (psVopHdr->iDCTCoefs == 0) {
                    return E_MPG4_INVALID_STREAM;
                }
            }
            if (psVolHdr->bDCTLines) {
                psVopHdr->iDCTLines = GET_ONE_BYTE(pStreamReader);
                if (psVopHdr->iDCTLines == 0) {
                    return E_MPG4_INVALID_STREAM;
                }
            }
            if (psVolHdr->bVLCSymbols) {
                psVopHdr->iVLCSymbols = GET_ONE_BYTE(pStreamReader);
                if (psVopHdr->iVLCSymbols == 0) {
                    return E_MPG4_INVALID_STREAM;
                }
            }
            if (psVolHdr->bVLCBits) {
                psVopHdr->iVLCBits = BR_fnGetBits(4, pStreamReader);
                if (psVopHdr->iVLCBits == 0) {
                    return E_MPG4_INVALID_STREAM;
                }
            }

            if (psVopHdr->vopPredType == PVOP || psVopHdr->vopPredType == BVOP ||
                /* START: Complexity Estimation syntax support */
                ((psVopHdr->vopPredType) == SPRITE && (psVolHdr->uiSprite == 1))) {
                /* END: Complexity Estimation syntax support */
                if (psVolHdr->bInterBlocks) {
                    psVopHdr->iInterBlocks = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iInterBlocks == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bInter4vBlocks) {
                    psVopHdr->iInter4vBlocks = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iInter4vBlocks == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bAPM) {
                    psVopHdr->iAPM = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iAPM == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bNPM) {
                    psVopHdr->iNPM = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iNPM == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bForwBackMCQ) {
                    psVopHdr->iForwBackMCQ = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iForwBackMCQ == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bHalfpel2) {
                    psVopHdr->iHalfpel2 = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iHalfpel2 == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
                if (psVolHdr->bHalfpel4) {
                    psVopHdr->iHalfpel4 = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iHalfpel4 == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
            }
            if (psVopHdr->vopPredType == BVOP ||
                /* START: Complexity Estimation syntax support */
                ((psVopHdr->vopPredType) == SPRITE && (psVolHdr->uiSprite == 1))) {
                /* END: Complexity Estimation syntax support */
                if (psVolHdr->bInterpolateMCQ) {
                    psVopHdr->iInterpolateMCQ = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iInterpolateMCQ == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
            }
            /* START: Complexity Estimation syntax support */
            if (psVopHdr->vopPredType == IVOP || psVopHdr->vopPredType == PVOP ||
                psVopHdr->vopPredType == BVOP) {
                if (psVolHdr->bSadct) {
                    psVopHdr->iSadct = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iSadct == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
            }

            if (psVopHdr->vopPredType == PVOP || psVopHdr->vopPredType == BVOP ||
                /* START: Complexity Estimation syntax support */
                ((psVopHdr->vopPredType) == SPRITE && (psVolHdr->uiSprite == 1))) {
                /* END: Complexity Estimation syntax support */
                if (psVolHdr->bQuarterpel) {
                    psVopHdr->iQuarterpel = GET_ONE_BYTE(pStreamReader);
                    if (psVopHdr->iQuarterpel == 0) {
                        return E_MPG4_INVALID_STREAM;
                    }
                }
            }
            /* END: Complexity Estimation syntax support */
        }
    }
    /* END: Complexity Estimation syntax support */

    if (psVolHdr->bShapeOnly == TRUE) {
        psVopHdr->intStep = 10; /* probably not needed  */
        psVopHdr->intStepI = 10;
        psVopHdr->intStepB = 10;
        psVopHdr->mvInfoForward.uiFCode = psVopHdr->mvInfoBackward.uiFCode = 1;
        if (psVolHdr->volType == ENHN_LAYER && psVopHdr->vopPredType == PVOP) {
            psVopHdr->iRefSelectCode = 3;
        } else if (psVolHdr->volType == ENHN_LAYER && psVopHdr->vopPredType == BVOP) {
            psVopHdr->iRefSelectCode = 0;
        }
        return eRetVal;
    }
    psVopHdr->iIntraDcSwitchThr = BR_fnGetBits(3, pStreamReader);

    /* INTERLACE */
    if (psVopHdr->bInterlace) {
        psVopHdr->bTopFieldFirst = GET_ONE_BIT(pStreamReader);
        psVopHdr->bAlternateScan = GET_ONE_BIT(pStreamReader);
        if (psVdoHdr->uiVOId <= 2) {
            ASSERT(psVolHdr->volType == BASE_LAYER);
        }
    } else /* ~INTERLACE */
    {
        psVopHdr->bAlternateScan = FALSE;
    }

    /* GMC */
    if (psVolHdr->uiSprite == 2 && psVopHdr->vopPredType == SPRITE) {
        if (psVolHdr->iNumOfPnts > 0) {
            /* 1decodeWarpPoints(); */
        }
    }
    /* ~GMC */

    if (psVopHdr->vopPredType == IVOP) {
        psVopHdr->intStepI = psVopHdr->intStep = BR_fnGetBits(
                psVolHdr->uiQuantPrecision, pStreamReader); /* also assign intStep to be safe */
        psVopHdr->mvInfoForward.uiFCode = psVopHdr->mvInfoBackward.uiFCode = 1;
        if (psVolHdr->fAUsage == EIGHT_BIT) {
            for (iAuxComp = 0; iAuxComp < psVolHdr->iAuxCompCount; iAuxComp++) {
                psVopHdr->intStepIAlpha[iAuxComp] =
                        BR_fnGetBits(NUMBITS_VOP_ALPHA_QUANTIZER, pStreamReader);
            }
        }
    } else if (psVopHdr->vopPredType == PVOP ||
               (psVolHdr->uiSprite == 2 && psVopHdr->vopPredType == SPRITE)) { /* GMC */
        psVopHdr->intStep = BR_fnGetBits(psVolHdr->uiQuantPrecision, pStreamReader);
        if (psVolHdr->fAUsage == EIGHT_BIT) {
            for (iAuxComp = 0; iAuxComp < psVolHdr->iAuxCompCount; iAuxComp++) {
                psVopHdr->intStepPAlpha[iAuxComp] =
                        BR_fnGetBits(NUMBITS_VOP_ALPHA_QUANTIZER, pStreamReader);
            }
        }

        psVopHdr->mvInfoForward.uiFCode = BR_fnGetBits(NUMBITS_VOP_FCODE, pStreamReader);
        if ((int)psVopHdr->mvInfoForward.uiFCode > 0) {
            psVopHdr->mvInfoForward.uiScaleFactor = 1 << (psVopHdr->mvInfoForward.uiFCode - 1);
            psVopHdr->mvInfoForward.uiRange = 16 << psVopHdr->mvInfoForward.uiFCode;
        }
        psVopHdr->mvInfoBackward.uiFCode = 1;
    } else if (psVopHdr->vopPredType == BVOP) {
        psVopHdr->intStepB = psVopHdr->intStep = BR_fnGetBits(
                psVolHdr->uiQuantPrecision, pStreamReader); /* also assign intStep to be safe */

        if (psVolHdr->fAUsage == EIGHT_BIT) {
            for (iAuxComp = 0; iAuxComp < psVolHdr->iAuxCompCount; iAuxComp++) {
                psVopHdr->intStepBAlpha[iAuxComp] =
                        BR_fnGetBits(NUMBITS_VOP_ALPHA_QUANTIZER, pStreamReader);
            }
        }

        psVopHdr->mvInfoForward.uiFCode = BR_fnGetBits(NUMBITS_VOP_FCODE, pStreamReader);
        if ((int)psVopHdr->mvInfoForward.uiFCode > 0) {
            psVopHdr->mvInfoForward.uiScaleFactor = 1 << (psVopHdr->mvInfoForward.uiFCode - 1);
            psVopHdr->mvInfoForward.uiRange = 16 << psVopHdr->mvInfoForward.uiFCode;
        }
        psVopHdr->mvInfoBackward.uiFCode = BR_fnGetBits(NUMBITS_VOP_FCODE, pStreamReader);
        if ((int)psVopHdr->mvInfoBackward.uiFCode > 0) {
            psVopHdr->mvInfoBackward.uiScaleFactor = 1 << (psVopHdr->mvInfoBackward.uiFCode - 1);
            psVopHdr->mvInfoBackward.uiRange = 16 << psVopHdr->mvInfoBackward.uiFCode;
        }
    }

    if (psVolHdr->volType == BASE_LAYER) {
        if (psVolHdr->fAUsage != RECTANGLE && psVopHdr->vopPredType != IVOP &&
            psVolHdr->uiSprite != 1) {
            psVopHdr->bShapeCodingType = GET_ONE_BIT(pStreamReader);
        }
    }

    if (psVolHdr->volType == ENHN_LAYER) {
        psVopHdr->bShapeCodingType = (psVopHdr->vopPredType == IVOP ? 0 : 1);
        if (psVolHdr->iEnhnType != 0) {
            psVopHdr->iLoadBakShape = GET_ONE_BIT(pStreamReader); /* load_backward_shape */
            if (psVopHdr->iLoadBakShape) {
#if 0
                CVOPU8YUVBA* pvopcCurr = new CVOPU8YUVBA (*(rgpbfShape [0]->pvopcReconCurr()));
                copyVOPU8YUVBA(rgpbfShape [1]->m_pvopcRefQ1, pvopcCurr);
                /* previous backward shape is saved to current forward shape */
                rgpbfShape [1]->m_rctCurrVOPY.left   = rgpbfShape [0]->m_rctCurrVOPY.left;
                rgpbfShape [1]->m_rctCurrVOPY.right  = rgpbfShape [0]->m_rctCurrVOPY.right;
                rgpbfShape [1]->m_rctCurrVOPY.top    = rgpbfShape [0]->m_rctCurrVOPY.top;
                rgpbfShape [1]->m_rctCurrVOPY.bottom = rgpbfShape [0]->m_rctCurrVOPY.bottom;

                I32 width = BR_fnGetBits( NUMBITS_VOP_WIDTH); ASSERT (width % MB_SIZE == 0); /* has to be multiples of MB_SIZE */
                U32 uiMarker = GET_ONE_BIT(pStreamReader);
                ASSERT (uiMarker == 1);
                I32 height = BR_fnGetBits( NUMBITS_VOP_HEIGHT); ASSERT (height % MB_SIZE == 0); /* has to be multiples of MB_SIZE */
                uiMarker = GET_ONE_BIT(pStreamReader);
                ASSERT (uiMarker == 1);
                width = ((width+15)>>4)<<4; /* not needed if the ASSERTs are present */
                height = ((height+15)>>4)<<4;
                I32 iSign = (GET_ONE_BIT(pStreamReader) == 1)? -1 : 1;
                I32 left = iSign * BR_fnGetBits( NUMBITS_VOP_HORIZONTAL_SPA_REF - 1);
                I32 uiMarker = GET_ONE_BIT(pStreamReader); /* marker bit
                                                           iSign = (GET_ONE_BIT(pStreamReader) == 1)? -1 : 1;
                                                           I32 top = iSign * BR_fnGetBits( NUMBITS_VOP_VERTICAL_SPA_REF - 1);

                                                           rgpbfShape[0]->m_rctCurrVOPY = CRct (left, top, left + width, top + height);
                                                           rgpbfShape[0]->m_rctCurrVOPUV = rgpbfShape[0]->m_rctCurrVOPY.downSampleBy2 ();

                                                           // decode backward shape
                                                           rgpbfShape[0]->setRefStartingPointers ();
                                                           rgpbfShape[0]->compute_bfShapeMembers (); // clear m_pvopcRefQ1 */
                rgpbfShape[0]->resetBYPlane ();/* clear BY of RefQ1 */
                rgpbfShape[0]->psVopHdr->bShapeOnly = TRUE;
                rgpbfShape[0]->psVopHdr->bNoCrChange = psVopHdr->bNoCrChange; /* set CR change disable */
                rgpbfShape[0]->psVopHdr->bInterlace = FALSE;
                rgpbfShape[0]->decodeIVOP_WithShape ();

                psVopHdr->iLoadForShape = GET_ONE_BIT(pStreamReader); /* load_forward_shape */
                if(psVopHdr->iLoadForShape)
                {
                    width = BR_fnGetBits( NUMBITS_VOP_WIDTH); ASSERT (width % MB_SIZE == 0); /* has to be multiples of MB_SIZE */
                    uiMarker = GET_ONE_BIT(pStreamReader);
                    ASSERT (uiMarker == 1);
                    height = BR_fnGetBits( NUMBITS_VOP_HEIGHT); ASSERT (height % MB_SIZE == 0); /* has to be multiples of MB_SIZE */
                    uiMarker = GET_ONE_BIT(pStreamReader);
                    ASSERT (uiMarker == 1);
                    width = ((width+15)>>4)<<4; /* not needed if the ASSERTs are present */
                    height = ((height+15)>>4)<<4;
                    iSign = (GET_ONE_BIT(pStreamReader) == 1)? -1 : 1;
                    left = iSign * BR_fnGetBits( NUMBITS_VOP_HORIZONTAL_SPA_REF - 1);
                    uiMarker = GET_ONE_BIT(pStreamReader); /* marker bit */
                    iSign = (GET_ONE_BIT(pStreamReader) == 1)? -1 : 1;
                    top = iSign * BR_fnGetBits( NUMBITS_VOP_VERTICAL_SPA_REF - 1);

                    rgpbfShape[1]->m_rctCurrVOPY = CRct (left, top, left + width, top + height);
                    rgpbfShape[1]->m_rctCurrVOPUV = rgpbfShape[1]->m_rctCurrVOPY.downSampleBy2 ();

                    /* decode forward shape */
                    rgpbfShape[1]->setRefStartingPointers ();
                    rgpbfShape[1]->compute_bfShapeMembers (); /* clear m_pvopcRefQ1 */
                    rgpbfShape[1]->resetBYPlane ();/* clear BY of RefQ1 */
                    rgpbfShape[1]->psVopHdr->bShapeOnly = TRUE;
                    rgpbfShape[1]->psVopHdr->bNoCrChange = psVopHdr->bNoCrChange; /* set CR change disable */
                    rgpbfShape[1]->psVopHdr->bInterlace = FALSE;
                    rgpbfShape[1]->decodeIVOP_WithShape ();
                }
#else
                eRetVal = E_MPG4_NOT_SUPPORT_131;
                return eRetVal;
#endif
            } /* end of "if(psVopHdr->iLoadBakShape)" */
            else {
                psVopHdr->iLoadForShape =
                        0; /* no forward shape when backward shape is not decoded */
            }
        } else {
            psVopHdr->iLoadForShape = 0;
            psVopHdr->iLoadBakShape = 0;
        }
        psVopHdr->iRefSelectCode = BR_fnGetBits(2, pStreamReader);
        ASSERT((!psVolHdr->bTemporalFGS) || /* either not fgs-ts */
               ((psVopHdr->vopPredType == BVOP) &&
                (psVopHdr->iRefSelectCode == 3)) || /* refSelCode must be '11' for bvop */
               /* refSelCode must be '01' or '10' for pvop */
               ((psVopHdr->vopPredType == PVOP) &&
                ((psVopHdr->iRefSelectCode == 1) || (psVopHdr->iRefSelectCode == 2))));
    }

    _pMPG4Obj->vopParsed = 1;
    return eRetVal;
}

int MPG4VP_fnParseVideoHeader(MPG4_OBJECT* _pMPG4Obj, U32 _uiStartCode) {
    MPG4_OBJECT* pMPG4Obj = _pMPG4Obj;
    U32 uiStartCode = _uiStartCode;
    MPG4_RETURN eRetVal = E_MPG4_OK_0;

    ASSERT(pMPG4Obj != NULL);

#ifdef MPEG4_SHORT_HEADER
    if ((uiStartCode == SHORT_VIDEO_START_MARKER)) {
        eRetVal = MPG4VP_fnGetShortHdr(pMPG4Obj);
        return eRetVal;
    }
#endif
    if (uiStartCode == VOP_START_CODE) {
        eRetVal = MPG4VP_fnGetVopHdr(pMPG4Obj);
    } else if (uiStartCode == GOV_START_CODE) {
        eRetVal = MPG4VP_fnGetGovHdr(pMPG4Obj);
    } else if ((uiStartCode >= VOL_START_CODE_MIN) && (uiStartCode <= VOL_START_CODE_MAX)) {
        eRetVal = MPG4VP_fnGetVolHdr(pMPG4Obj);
    } else if (/*( uiStartCode >= VDO_START_CODE_MIN ) && */
               (uiStartCode <= VDO_START_CODE_MAX)) {
        eRetVal = MPG4VP_fnGetVdoHdr(pMPG4Obj, uiStartCode);
    } else if (uiStartCode == VSO_START_CODE) {
        eRetVal = MPG4VP_fnGetVsoHdr(pMPG4Obj);
    } else if (uiStartCode == VOS_START_CODE) {
        eRetVal = MPG4VP_fnGetVosHdr(pMPG4Obj);
    }

    return eRetVal;
}
