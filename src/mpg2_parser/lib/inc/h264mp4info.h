/**
 *  Copyright 2025-2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef H264MP4_INFO

#define H264MP4_INFO

#include "fsl_datatype.h"
#include "fsl_types.h"

#ifndef I32
typedef int I32;
#endif

#ifndef FLOAT
typedef float FLOAT;
#endif

#ifndef DOUBLE
typedef double DOUBLE;
#endif

#ifndef VOID
typedef void VOID;
#endif

typedef int MPG4_RETURN;

typedef struct {
    uint8* pBuffer;
    uint32 bitsOffset;
    uint32 bytesOffset;
    uint32 bufLen;
} BUF_CONTEXT_T;

// types for parsing H264 header

typedef struct syntaxelement {
    int type;                 //!< type of syntax element for data part.
    int value1;               //!< numerical value of syntax element
    int value2;               //!< for blocked symbols, e.g. run/level
    int len;                  //!< length of code
    int inf;                  //!< info part of UVLC code
    unsigned int bitpattern;  //!< UVLC bitpattern
    int context;              //!< CABAC context
    int k;                    //!< CABAC context for coeff_count,uv

    //! for mapping of UVLC to syntaxElement
    void (*mapping)(int len, int info, int* value1, int* value2);
    //! used for CABAC: refers to actual coding method of each individual syntax element type
} SyntaxElement;

/*
   Parser H.264 sequence header
*/
#define MAXnum_ref_frames_in_pic_order_cnt_cycle 256

#define MB_BLOCK_SIZE 16

#define FREXT_HP 100       //!< YUV 4:2:0/8 "High"
#define FREXT_Hi10P 110    //!< YUV 4:2:0/10 "High 10"
#define FREXT_Hi422 122    //!< YUV 4:2:2/10 "High 4:2:2"
#define FREXT_Hi444 244    //!< YUV 4:4:4/14 "High 4:4:4"
#define FREXT_CAVLC444 44  //!< YUV 4:4:4/14 "CAVLC 4:4:4"

#define YUV400 0
#define YUV420 1
#define YUV422 2
#define YUV444 3

typedef struct {
    int Valid;  // indicates the parameter set is valid
    int updated;

    unsigned int profile_idc;   // u(8)
    int constrained_set0_flag;  // u(1)
    int constrained_set1_flag;  // u(1)
    int constrained_set2_flag;  // u(1)
    int constrained_set3_flag;  // u(1)
    int constrained_set4_flag;
    int constrained_set5_flag;
    unsigned int level_idc;             // u(8)
    unsigned int seq_parameter_set_id;  // ue(v)
    unsigned int chroma_format_idc;     // ue(v)

    int seq_scaling_matrix_present_flag;    // u(1)
    int seq_scaling_list_present_flag[12];  // u(1)
    int ScalingList4x4[6][16];              // se(v)
    int ScalingList8x8[6][64];              // se(v)
    int UseDefaultScalingMatrix4x4Flag[6];
    int UseDefaultScalingMatrix8x8Flag[6];

    unsigned int bit_depth_luma_minus8;    // ue(v)
    unsigned int bit_depth_chroma_minus8;  // ue(v)

    unsigned int log2_max_frame_num_minus4;  // ue(v)
    unsigned int pic_order_cnt_type;
    unsigned log2_max_pic_order_cnt_lsb_minus4;  // ue(v)
    // else if( pic_order_cnt_type == 1 )
    int delta_pic_order_always_zero_flag;                                // u(1)
    int offset_for_non_ref_pic;                                          // se(v)
    int offset_for_top_to_bottom_field;                                  // se(v)
    unsigned int num_ref_frames_in_pic_order_cnt_cycle;                  // ue(v)
    int offset_for_ref_frame[MAXnum_ref_frames_in_pic_order_cnt_cycle];  // se(v)
    unsigned int num_ref_frames;                                         // ue(v)
    int gaps_in_frame_num_value_allowed_flag;                            // u(1)
    unsigned int pic_width_in_mbs_minus1;                                // ue(v)
    unsigned int pic_height_in_map_units_minus1;                         // ue(v)
    int frame_mbs_only_flag;                                             // u(1)
    int mb_adaptive_frame_field_flag;                                    // u(1)
    int direct_8x8_inference_flag;                                       // u(1)
    int frame_cropping_flag;                                             // u(1)
    unsigned int frame_cropping_rect_left_offset;                        // ue(v)
    unsigned int frame_cropping_rect_right_offset;                       // ue(v)
    unsigned int frame_cropping_rect_top_offset;                         // ue(v)
    unsigned int frame_cropping_rect_bottom_offset;                      // ue(v)
    int vui_parameters_present_flag;                                     // u(1)
    unsigned separate_colour_plane_flag;                                 // u(1)
    int time_scale;
    int num_units_in_tick;
} seq_parameter_set_rbsp_t;

// comment out below functions because they are unused
// end types for parsing H264 header.
int Buf_initContext(BUF_CONTEXT_T* pContext, uint8* pBuffer, uint32 bufLen);
#if 0
int Buf_fnGetBits(BUF_CONTEXT_T* pContext, uint32 nBits, int* pValue);
int GetVLCSymbol (int *info, BUF_CONTEXT_T * pContext, int* pValue);
int readSyntaxElement_VLC(SyntaxElement *sym, BUF_CONTEXT_T * pContext, int* pValue);
void linfo_ue(int len, int info, int *value1, int *dummy);
void linfo_se(int len,  int info, int *value1, int *dummy);
int ue_v ( BUF_CONTEXT_T * pContext, int* pValue);
int se_v ( BUF_CONTEXT_T * pContext, int* pValue);
int H264R_fnScaling_List(int *scalingList, int sizeOfScalingList, int *UseDefaultScalingMatrix, BUF_CONTEXT_T * pContext);
#endif

// types for parsing Mpeg4 header
#define BLOCK_SQUARE_SIZE 64
/*
** ...
*/
#define MAX_MAC 10

/*****************************************************************************
 * <Typedefs>
 *****************************************************************************/
/*
** return values of MPEG-4 video parser
*/
// typedef enum
#define E_MPG4_OK_0 0              /* parse MPEG-4 bitstream successfully */
#define E_MPG4_START_CODE_1 1      /* find start code */
#define E_MPG4_SHORT_HDR_2 2       /* find short header */
#define E_MPG4_EOF_127 127         /* reach the end of file */
#define E_MPG4_ERROR_128 128       /* unknown error */
#define E_MPG4_BROKEN_HDR_129 129  /* broken header */
#define E_MPG4_MALLOC_ERR_130 130  /* allocating memory failure */
#define E_MPG4_NOT_SUPPORT_131 131 /* un-supported feature in the bitstream */
#define E_MPG4_INVALID_STREAM 132

/*
** VOL type
*/
typedef enum {
    BASE_LAYER, /* base layer */
    ENHN_LAYER  /* enhance layer */
} VOL_TYPE;

/*
** VOL shape type
*/
typedef enum {
    RECTANGLE, /* rectangular */
    ONE_BIT,   /* binary */
    EIGHT_BIT, /* binary only */
    GRAYSCALE  /* grayscale */
} ALPHA_USAGE;

/*
** quantization type
*/
typedef enum {
    Q_H263, /* H.264 quantization */
    Q_MPEG  /* MPEG-4 quantization */
} QUANTIZER;

/*
** entropy code type
*/
typedef enum {
    HUFFMAN,   /* huffman coding */
    ARITHMETIC /* arithmatic coding */
} ENTROPY_CODE_TYPE;

/*
** VOP prediction type
*/
typedef enum {
    IVOP,          /* I VOP */
    PVOP,          /* P VOP */
    BVOP,          /* B VOP */
    SPRITE,        /* S VOP */
    PBCHUNK,       /* PB chunk */
    UNKNOWNVOPTYPE /* unknown prediction type */
} VOP_PRED_TYPE;

/*
** shape prediction directory(B VOP)
*/
typedef enum {
    B_FORWARD, /* forward prediction */
    B_BACKWARD /* backward prediction */
} SHAPE_B_PRED_DIR;

/*
** motion vector information
*/
typedef struct  // for motion vector coding
{
    U32 uiRange;       /* search range */
    U32 uiFCode;       /* f-code  */
    U32 uiScaleFactor; /* scale factor */
} MV_INFO;

/*
** ...
*/
typedef enum {
    STOP,
    PIECE,
    UPDATE,
    PAUSE,
    NEXT
} SPT_XMIT_MODE;

/*
** RRV insertion
*/

typedef struct {
    I32 iOnOff;
    I32 iOnOffForI;
    FLOAT fC1;
    FLOAT fC2;
    FLOAT fFR1;
    FLOAT fFR2;
    I32 iQP1;
    I32 iQP2;
    I32 iCycle;
    I32 iRRVOnOff;   // 1==ON,0==OFF
    I32 iQcount;     // Quantizer counter
    I32 iQave;       // average of Q
    I32 iPrevOnOff;  // OnOff of previous VOP
    I32 iNumBits;    // number of previous bits
    I32 iCutoffThr;  // for encoder
} RRV_MODE_STR;

/*
** VOS header context
*/
typedef struct {
    U32 uiProfile;
} MPG4_VOS_HEADER;

/*
** VSO header context
*/
typedef struct {
    U32 uiIsVisualObjectIdent;
    U32 uiVSOVerID;
    U32 uiVSOPriority;
    U32 uiVSOType;
} MPG4_VSO_HEADER;

/*
** VDO header context
*/
typedef struct {
    U32 uiVOId;
} MPG4_VDO_HEADER;

/*
** VOL header context
*/
typedef struct {
    /* video plane with short header */
    bool bFGSScalability;

    U32 uiVerID;  // Version identification // GMC
    // tung
    U32 uiVoInd;  // video_object_type_indication
    // ~tung
    // type of VOL
    U32 uiVOLId;
    bool bRandom;
    U32 uiOLType;
    VOL_TYPE volType;  // what type of VOL
    U32 uiOLI;         // VOL_Is_Object_Layer_Identifier

    U32 uiVolPrio;  // video_oject_layer_priority
    U32 uiAspect;
    U32 uiParWidth;
    U32 uiParHeight;
    U32 uiCTP;  // VOL_Control_Parameter

    /* VBV */
    U32 uiFirstHalfBitRate;
    U32 uiLatterHalfBitRate;
    U32 uiFirstHalfVbvBufferSize;
    U32 uiLatterHalfVbvBufferSize;
    U32 uiFirstHalfVbvBufferOccupany;
    U32 uiLatterHalfVbvBufferOccupany;

    /* SPRITE */
    U32 uiSprite;
    I32 iSpriteWidth;
    I32 iSpriteHeight;
    I32 iSpriteLeftEdge;
    I32 iSpriteTopEdge;
    bool bLightChange;
    I32 iLightChangeFactor;
    I32 iNumOfPnts;   // for sprite warping
    bool bSpriteMode; /* Low_latency_sprite_enable */

    // NBIT: nbit information
    bool bNot8Bit;
    U32 uiQuantPrecision;
    U32 nBits;

    // time info
    I32 iClockRate;   // rate of clock used to count vops in Hz
    DOUBLE dFrameHz;  // Frame frequency (Hz), (floating point in case of 29.97 Hz)
    bool bFixFrameRate;
    U32 uiFixedVOPTimeIncrement;
    I32 iNumBitsTimeIncr;
    DOUBLE dClockRateScale; /* FOR BASE LAYER, it is 1.0; for ENHANCE LAYER ... */

    U32 uiResyncMarkerDisable;

    I32 iVolWidth;
    I32 iVolHeight;

    // shape info
    ALPHA_USAGE fAUsage;  // binary or gray level alpha; or no alpha (rectangle VO)
    I32 iAlphaShapeExtension;
    I32 iAuxCompCount;
    bool bShapeOnly;     // code only the shape
    I32 iBinaryAlphaTH;  // binary shaperounding parameter
    I32 iBinaryAlphaRR;  // binary shaperounding refresh rate: for Added error resilient mode by
                         // Toshiba(1997-11-14)
    I32 iGrayToBinaryTH;
    bool bNoCrChange;  // nobinary shape size conversion

    // motion info
    bool bOriginalForME;    // flag indicating whether use the original previous VOP for ME
    U32 uiWarpingAccuracy;  // indicates the quantization accuracy of motion vector for sprite
                            // warping
    bool bAdvPredDisable;   // No OBMC, (8x8 in the future).
    bool bQuarterSample;    // Quarter sample
#ifdef _VOP_MC_SWITCHING_
    // frame_level_qpel
    bool bSeqQuarterSample;
    I32 iHPelCount;
    I32 iQPelCount;
    // ~frame_level_qpel
#endif
    bool bRoundingControlDisable;
    I32 iInitialRoundingType;

    // NEWPRED
    bool bNewpredEnable;
    int iRequestedBackwardMessegeType;
    bool bNewpredSegmentType;
    char* cNewpredRefName;
    char* cNewpredSlicePoint;
    // ~NEWPRED
    /* EIGHT_BIT */
    U32 uiCompMethod;
    U32 uiLinearComp;
    /* ~EIGHT_BIT */
    // RESYNC_MARKER_FIX
    bool bResyncMarkerDisable;  // resync marker Disable
                                //~RESYNC_MARKER_FIX
    bool bVPBitTh;              // Bit threshold for video packet spacing control
    bool bDataPartitioning;     // data partitioning
    bool bReversibleVlc;        // reversible VLC

    bool bCodeSequenceHead;
    U32 uiProfileAndLevel;

    // texture coding info
    QUANTIZER fQuantizer;   // either H.263 or MPEG
    bool bLoadIntraMatrix;  // flag indicating whether to load intra Q-matrix
    I32 rgiIntraQuantizerMatrix[BLOCK_SQUARE_SIZE];  // Intra Q-Matrix
    bool bLoadInterMatrix;  // flag indicating whether to load inter Q-matrix
    I32 rgiInterQuantizerMatrix[BLOCK_SQUARE_SIZE];  // Inter Q-Matrix
    bool bLoadIntraMatrixAlpha;  // flag indicating whether to load intra Q-matrix
    I32 rgiIntraQuantizerMatrixAlpha[MAX_MAC][BLOCK_SQUARE_SIZE];
    bool bLoadInterMatrixAlpha;  // flag indicating whether to load inter Q-matrix
    I32 rgiInterQuantizerMatrixAlpha[MAX_MAC][BLOCK_SQUARE_SIZE];
    bool bDeblockFilterDisable;      // apply deblocking filter or not.
    bool bNoGrayQuantUpdate;         // decouple gray quant and dquant
    ENTROPY_CODE_TYPE fEntropyType;  // Entropy code type

    // HHI Klaas Schueuer sadct flag
    bool bSadctDisable;
    // end HHI

    // START: Complexity Estimation syntax support - Marc Mongenet (EPFL) - 15 Jun 1998
    bool bComplexityEstimationDisable;
    I32 iEstimationMethod;
    bool bShapeComplexityEstimationDisable;
    bool bOpaque;
    bool bTransparent;
    bool bIntraCAE;
    bool bInterCAE;
    bool bNoUpdate;
    bool bUpsampling;
    bool bTextureComplexityEstimationSet1Disable;
    bool bIntraBlocks;
    bool bInterBlocks;
    bool bInter4vBlocks;
    bool bNotCodedBlocks;
    bool bTextureComplexityEstimationSet2Disable;
    bool bDCTCoefs;
    bool bDCTLines;
    bool bVLCSymbols;
    bool bVLCBits;
    bool bMotionCompensationComplexityDisable;
    bool bAPM;
    bool bNPM;
    bool bInterpolateMCQ;
    bool bForwBackMCQ;
    bool bHalfpel2;
    bool bHalfpel4;
    // START: Complexity Estimation syntax support - Update version 2 - Massimo Ravasi (EPFL) - 5
    // Nov 1999
    bool bVersion2ComplexityEstimationDisable;
    bool bSadct;
    bool bQuarterpel;
    // END: Complexity Estimation syntax support - Update version 2

    // START: Vol Control Parameters
    U32 uiVolControlParameters;
    U32 uiChromaFormat;
    U32 uiLowDelay;
    U32 uiVBVParams;
    U32 uiBitRate;
    U32 uiVbvBufferSize;
    U32 uiVbvBufferOccupany;
    // END: Vol Control Parameters

    // frame rate info
    I32 iTemporalRate;  // no. of input frames between two encoded VOP's assuming 30Hz input
    I32 iPbetweenI;
    I32 iBbetweenP;
    bool bPutLastResAsP;
    I32 iGOVperiod;  // number of VOP from GOV header to next GOV header
                     // added by SONY 980212

    bool bAllowSkippedPMBs;

    // scalability info
    // #ifdef _Scalable_SONY_
    I32 iHierarchyType;
    // #endif //_Scalable_SONY_
    I32 iEnhnType;  // enhancement type

    I32 iEnhnTypeSpatial;  // for OBSS partial enhancement mode        //OBSS_SAIT_991015

    I32 iSpatialOption;
    I32 ihor_sampling_factor_m;
    I32 ihor_sampling_factor_n;
    I32 iver_sampling_factor_m;
    I32 iver_sampling_factor_n;

    // OBSS_SAIT_991015
    //  object based spatial scalability
    I32 iFrmWidth_SS;
    I32 iFrmHeight_SS;
    I32 iuseRefShape;
    I32 iuseRefTexture;
    I32 ihor_sampling_factor_m_shape;
    I32 ihor_sampling_factor_n_shape;
    I32 iver_sampling_factor_m_shape;
    I32 iver_sampling_factor_n_shape;
    bool bSpatialScalability;
    //~OBSS_SAIT_991015

    U32 uiFgsLayerType;
    // FGS : start
    //  FGS Error Resilience, MSRCN, 2000/8/20
    bool bFGSErrorResilienceDisable;
    I32 iFGSVideoPacketLength;

    // FGS Weight Matrix, Intel, 10/25/99
    bool bFGSFreqWeightingEnable;
    bool bLoadWeightMatrix;
    I32 rgiWeightMatrix[BLOCK_SQUARE_SIZE];

    // FGST Weight Matrix, Intel, 06/22/2000
    bool bFGSTFreqWeightingEnable;
    bool bLoadFGSTWeightMatrix;
    I32 rgiFGSTWeightMatrix[BLOCK_SQUARE_SIZE];
    // FGS : end

    // temporal scalability  // added by Sharp (98/2/10)
    bool bTemporalScalability;
    // FGS : start
    bool bTemporalFGS;  // added by Intel 2/22/00
    bool bFGS;
    // FGS : end

    // statistics dumping options
    bool bDumpMB;  // dump statitstics at MB level
    bool bTrace;   // dumping trace file

    I32 iMVRadiusPerFrameAwayFromRef;  // MV serach radius per frame away from reference VOP
                                       // RRV insertion
    bool breduced_resolution_vop_enable;
    // ~RRV
} MPG4_VOL_HEADER;

/*
** GOV header context
*/
typedef struct {
    I32 iTimeCode;
    I32 iClosedGov;
    I32 iBrokenLink;
} MPG4_GOV_HEADER;

/*
** VOP header context
*/
typedef struct {
    /* video plane with short header */
    U32 uiNumGobsInVop;
    U32 uiNumMacroblocksInGob;
    U32 uiVopQuant;
    U32 uiPei;
    U32 uiPrevTemporalRef;
    U32 uiCurrTemporalRef;
    U32 uiNum256;

    // user specify, per VOP
    I32 intStepI;  // I-VOP stepsize for DCT
    I32 intStep;   // P-VOP stepsize for DCT
    I32 intStepB;  // B-VOP stepsize for DCT

    I32 intStepIAlpha[MAX_MAC];  // I-VOP stepsize for DCT alpha
    I32 intStepPAlpha[MAX_MAC];  // P-VOP stepsize for DCT alpha
    I32 intStepBAlpha[MAX_MAC];  // B-VOP stepsize for DCT alpha

    I32 intStepDiff;            // stepsize difference for updating for DCT (DQUANT)
    VOP_PRED_TYPE vopPredType;  // whether IVOP, PVOP, BVOP, or Sprite
    I32 iIntraDcSwitchThr;      // threshold to code intraDC as with AC VLCs
    I32 iRoundingControl;       // rounding control
    I32 iRoundingControlEncSwitch;
    SHAPE_B_PRED_DIR fShapeBPredDir;  // shape prediction direction BVOP

    /* TIME */
    I32 iCurrSec;
    I32 iModuloBaseDecd;
    I32 iModuloBaseDisp;
    I32 iOldModuloBaseDecd;
    I32 iOldModuloBaseDisp;
    I32 iTimeSecond;
    I32 iTimeIncrem;
    I32 iTimeStamp;
    I32 iFrameInterval;
    I32 iPastRef;

    bool bVopCoded;
    I32 iVopWidth;
    I32 iVopHeight;
    I32 iVopHoriMcSpaRef;
    I32 iVopVertMcSpaRef;

    I32 iVopConstantAlpha;
    I32 iVopConstantAlphaValue;  // for binary or grayscale shape pk val

    // OBSSFIX_MODE3
    bool bBGComposition;
    //~OBSSFIX_MODE3

    // Complexity Estimation syntax support - Marc Mongenet (EPFL) - 15 Jun 1998
    I32 iOpaque;
    I32 iTransparent;
    I32 iIntraCAE;
    I32 iInterCAE;
    I32 iNoUpdate;
    I32 iUpsampling;
    I32 iIntraBlocks;
    I32 iInterBlocks;
    I32 iInter4vBlocks;
    I32 iNotCodedBlocks;
    I32 iDCTCoefs;
    I32 iDCTLines;
    I32 iVLCSymbols;
    I32 iVLCBits;
    I32 iAPM;
    I32 iNPM;
    I32 iInterpolateMCQ;
    I32 iForwBackMCQ;
    I32 iHalfpel2;
    I32 iHalfpel4;
    // START: Complexity Estimation syntax support - Update version 2 - Massimo Ravasi (EPFL) - 5
    // Nov 1999
    I32 iSadct;
    I32 iQuarterpel;
    // END: Complexity Estimation syntax support - Update version 2

    // motion search info
    MV_INFO mvInfoForward;     // motion search info
    MV_INFO mvInfoBackward;    // motion search info
    I32 iSearchRangeForward;   // maximum search range for motion estimation
    I32 iSearchRangeBackward;  // maximum search range for motion estimation

    bool bInterlace;        // interlace coding flag
    bool bTopFieldFirst;    // Top field first
    bool bAlternateScan;    // Alternate Scan
    I32 iDirectModeRadius;  // Direct mode search radius (half luma pels)

    // for scalability
    I32 iRefSelectCode;
    I32 iLoadForShape;      // load_forward_shape
    I32 iLoadBakShape;      // load_backward_shape
    bool bShapeCodingType;  // vop_shape_coding_type (0:intra, 1:inter): Added for error resilient
                            // mode by Toshiba(1997-11-14)
    SPT_XMIT_MODE SpriteXmitMode;  // sprite transmit mode

    // NEWPRED
    int m_iVopID;
    int m_iNumBitsVopID;
    int m_iVopID4Prediction_Indication;
    int m_iVopID4Prediction;
    // ~NEWPRED

    // RRV insertion
    RRV_MODE_STR RRVmode;
    // ~RRV
} MPG4_VOP_HEADER;

/*
** MPEG-4 video object
*/
typedef struct {
    BUF_CONTEXT_T* pStreamReader;
    MPG4_VOS_HEADER* psVosHdr;
    MPG4_VSO_HEADER* psVsoHdr;
    MPG4_VDO_HEADER* psVdoHdr;
    MPG4_VOL_HEADER* psVolHdr;
    MPG4_GOV_HEADER* psGovHdr;
    MPG4_VOP_HEADER* psVopHdr;
    VOP_PRED_TYPE ePrevVopType;
    U32* puiFrmIdxList;
    U32 uiFrameCnt;
    U32 vopParsed;
} MPG4_OBJECT;

int MPG4VP_fnParseVideoHeader(MPG4_OBJECT* _pMPG4Obj, U32 _uiStartCode);
#endif
