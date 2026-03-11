/***********************************************************************
 * Copyright (c) 2012, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************/

#ifndef _FSL_PARSER_DRM_API_H
#define _FSL_PARSER_DRM_API_H

enum /* API Return Code*/
{
    /* DRM */
    DRM_ERR_NOT_AUTHORIZED_USER = -80, /* Not an authorized user to play a DRM-protected file */
    DRM_ERR_RENTAL_EXPIRED =
            -81 /* The protected rental file is expired (reaching its view limit) */
};

/************************************************************************************************************
 *
 *               DLL entry point (mandatory) - to query parser interface
 *
 ************************************************************************************************************/
enum /* API function ID */
{
    PARSER_API_IS_DRM_PROTECTED = 130,
    PARSER_API_QUERY_CONTENT_USAGE = 131,
    PARSER_API_QUERY_OUTPUT_PROTECTION_FLAG = 132,
    PARSER_API_COMMIT_PLAYBACK = 133,
    PARSER_API_FINAL_PLAYBACK = 134
};

/*********************************************************************************************************
 *                  API Function Prototypes List
 *
 * There are mandatory and optional APIs.
 * A core parser must implement the mandatory APIs while need not implement the optional one.
 * And in its DLL entry point "FslParserInit", it shall set the not-implemented function pointers to
 *NULL.
 *
 *********************************************************************************************************/

/***************************************************************************************
 *                  DRM APIs Begin
 ***************************************************************************************/
/**
 * DRM interface.function to see whether file is protected by DRM.
 * The wrapper shall call the DRM interface right after the file header is parsed for a quick
 * decision. before doing the time-consuming task such as initialize index table.
 *
 * @param parserHandle [in] Handle of the core parser.
 * @param isProtected [out]True for protected file.
 */
typedef int32 (*FslParserIsDRMProtected)(FslFileHandle parserHandle, bool* isProtected);

/**
 * DRM interface.function to see whether file is a rental or purchased movie.
 * This API shall be called once before playing a protected clip.
 *
 * @param parserHandle[in] - Handle of the core parser.
 * @param isRental[out]  - True for a rental file and False for a purchase file. Rental file has a
 * view limit.
 * @param viewLimit[out] - View limit if a rental file.
 * @param viewCount[out] - Count of views played already.
 * @return
 */
typedef int32 (*FslParserQueryDRMContentUsage)(FslFileHandle parserHandle, bool* isRental,
                                               uint32* viewLimit, uint32* viewCount);

/**
 * DRM interface. function to check the video output protection flag.
 *
 * @param parserHandle[in] - Handle of the core parser.
 * @param cgmsaSignal[out] - 0, 1, 2, or 3 based on standard CGMSA signaling.
 * @param acptbSignal[out] - 0, 1, 2, or 3 based on standard trigger bit signaling.
 *                                      acptb values:
 *                                      0 = Off.
 *                                      1 = Auto gain control / pseudo sync pulse.
 *                                      2 = Two line color burst.
 *                                      3 = Four line color burst.
 * @param[out] digitalProtectionSignal - 0=off, 1=on.
 * @param[out] ictSignal   - 0=off, 1=on, Image Constraint token flag
 * @return PARSER_SUCCESS - success. Others - failure.
 */
typedef int32 (*FslParserQueryDRMOutputProtectionFlag)(FslFileHandle parserHandle,
                                                       uint8* cgmsaSignal, uint8* acptbSignal,
                                                       uint8* digitalProtectionSignal,
                                                       uint8* ictSignal);

/**
 * DRM interface.function to commit playing the protected file.
 * The wrapper shall call it before playback is started.
 *
 * @param parserHandle[in] - Handle of the core parser.
 * @return
 */
typedef int32 (*FslParserDRMCommitPlayback)(FslFileHandle parserHandle);

/**
 * DRM interface.function to end playing the protected file.
 * The wrapper shall call it after playback is stopped.
 * Otherwise error "DRM_ERR_PREV_PLAY_NOT_CLEAERED" on next playback.
 *
 * @param parserHandle [in] - Handle of the core parser.
 * @return
 */
typedef int32 (*FslParserDRMFinalizePlayback)(FslFileHandle parserHandle);

/***************************************************************************************
 *                  DRM APIs End
 ***************************************************************************************/

#endif /* _FSL_PARSER_DRM_API_H */
