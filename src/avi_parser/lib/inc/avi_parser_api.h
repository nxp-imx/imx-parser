/*
 ***********************************************************************
 * Copyright (c) 2009-2012, 2014, Freescale Semiconductor, Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

/**
 * @file avi_parser_api.h
 * @AVI file parser header files
 */

#ifndef FSL_AVI_PARSER_API_HEADER_INCLUDED
#define FSL_AVI_PARSER_API_HEADER_INCLUDED

/* common files of mmlayer */
#include "common/fsl_media_types.h"
#include "common/fsl_parser.h"
#include "common/fsl_types.h"

/***************************************************************************************
 *                  Data Structures & Constants
 ***************************************************************************************/
#define MAX_AVI_TRACKS                                                                          \
    24 /* maximum media tracks to support for an AVI movie.                                     \
                             If there are more tracks, those with a larger track number will be \
          overlooked */

/***************************************************************************************
 *                  API Functions
 * For calling sequence, please refer to the end of this file.
 ***************************************************************************************/
#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN
#endif

/**
 * function to get the AVI core parser version.
 *
 * @return Version string.
 */
EXTERN const char* AviParserVersionInfo();

/**
 * function to create the AVI core parser.
 *
 * @param stream [in]   Source stream of the AVI file.
 *                      It implements functions to open, close, tell, read and seek a file.
 *
 * @param memOps [in]   Memory operation callback table.
 *                      It implements the functions to malloc, calloc, realloc and free memory.
 *
 * @param drmOps [in]   DRM callback function table.
 *                      It implements the functions to read and write local DRM memory.
 *
 * @param context [in]  Wrapper context for the callback functions. Avi parser never modify it.
 * @param parserHandle [out] Handle of the AVI core parser if succeeds. NULL for failure.
 * @return
 */
EXTERN int32 AviCreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                             ParserOutputBufferOps* outputBufferOps, void* context,
                             FslParserHandle* parserHandle);

EXTERN int32 AviCreateParser2(uint32 flags, FslFileStream* streamOps, ParserMemoryOps* memOps,
                              ParserOutputBufferOps* outputBufferOps, void* context,
                              FslParserHandle* parserHandle);

/**
 * function to delete the AVI core parser.
 *
 * @param parserHandle Handle of the AVI core parser.
 * @return
 */
EXTERN int32 AviDeleteParser(FslFileHandle parserHandle);

/***************************************************************************************
 *                  DRM APIs Begin
 ***************************************************************************************/

/**
 * DRM interface.function to see whether file is protected by DRM.
 * The wrapper shall call the DRM interface right after the file header is parsed for a quick
 * decision. before doing the time-consuming task such as initialize index table.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param isProtected [out]True for protected file.
 */
EXTERN int32 AviIsProtected(FslFileHandle parserHandle, bool* isProtected);

/**
 * DRM interface.function to see whether file is a rental or purchased movie.
 * This API shall be called once before playing a protected clip.
 *
 * @param parserHandle[in] Handle of the avi core parser.
 * @param isRental[out] True for a rental file and False for a purchase file. Rental file has a view
 * limit.
 * @param viewLimit[out] View limit if a rental file.
 * @param viewCount [out]Count of views played already.
 * @return
 */
EXTERN int32 AviQueryContentUsage(FslFileHandle parserHandle, bool* isRental, uint32* viewLimit,
                                  uint32* viewCount);

/**
 * DRM interface. function to check the video output protection flag.
 *
 * @param parserHandle[in] - Handle of the AVI core parser.
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

EXTERN int32 AviQueryOutputProtectionFlag(FslFileHandle parserHandle, uint8* cgmsaSignal,
                                          uint8* acptbSignal, uint8* digitalProtectionSignal,
                                          uint8* ictSignal);

/**
 * DRM interface.function to commit playing the protected file.The wrapper shall call it before
 * playback is started.
 *
 * @param parserHandle[in] Handle of the AVI core parser.
 * @return
 */
EXTERN int32 AviCommitPlayback(FslFileHandle parserHandle);

/**
 * DRM interface.function to end playing the protected file.
 * The wrapper shall call it after playback is stopped.
 * Otherwise error "AVI_ERR_DRM_PREV_PLAY_NOT_CLEAERED" on next playback.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @return
 */
EXTERN int32 AviFinalizePlayback(FslFileHandle parserHandle);

/***************************************************************************************
 *                  DRM APIs End
 ***************************************************************************************/

/**
 * Function to intialize the index table by scanning the index in the file.
 * For a movie played for the 1st time, the index table has to be loaded from the file.
 * The index table increases with the movie duration. So the longer the movie is,
 * the more time it takes to load the index.
 *
 * Seeking and trick mode can be performed on a movie only if it has a index.
 * But normal playback does not depend on the index. So even if this function fails,
 * normal playback can still work as long as movie data are right.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @return
 */
EXTERN int32 AviInitializeIndex(FslFileHandle parserHandle);

/**
 * function to import the index table of a track from outside, instead of scanning the file.
 * This can reduce time to open a 2nd-play movie if its index table has been exported on 1st play.
 * To save the memory used, import/export a track's index per time.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param buffer [in] Buffer containing the index data.
 * @param size [in] Size of the index data in the buffer, in bytes.
 * @return
 */
EXTERN int32 AviImportIndex(FslFileHandle parserHandle, uint8* buffer, uint32 size);

/**
 * function to export the index table of track from outside, after the index table is loaded by
 * scanning the file at 1st play. This function is usually used on a 1st play movie. This can reduce
 * time to open a 2nd-play movie if its index table has been exported on 1st play. To save the
 * memory used, import/export a track's index per time.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param buffer [in] Buffer to export the index data.
 *                             If this parameter is NULL, just return the size of buffer needed
 * without exporting the index data.
 *
 * @param size [in/out] Size of the the buffer as input, in bytes.
 *                                 Size of the index data in the buffer as output, in bytes.
 * @return
 */
EXTERN int32 AviExportIndex(FslFileHandle parserHandle, uint8* buffer, uint32* size);

/**
 * function to tell whether the movie is seekable. A seekable AVI movie must have the index table.
 * If the file's index table is loaded from file or imported successfully, it's seekable.
 * Seeking and trick mode can be performed on a seekable file.
 * If the index table is corrupted, the file is NOT seekable. This function will fail and return
 * value can tell the error type.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param seekable [out] true for seekable and false for non-seekable
 * @return
 */
EXTERN int32 AviIsSeekable(FslFileHandle parserHandle, bool* seekable);

/**
 * function to tell how many tracks in the movie.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param numTracks [out] Number of tracks.
 * @return
 */
EXTERN int32 AviGetNumTracks(FslFileHandle parserHandle, uint32* numTracks);

/**
 * function to tell the user data information (title, artist, genre etc) of the movie. User data
 * API.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param id [in] User data ID, type of the user data.
 * @param buffer [out] Buffer containing the information. The core parser manages this buffer and
 * the user shall NOT free it. If no such info is availabe, this value will be set to NULL.
 * @param size [out] Length of the information in bytes. The informaiton is usually a
 * null-terminated ASCII string. If no such info is available, this value will be set to 0.
 * @return
 */
EXTERN int32 AviGetUserData(FslFileHandle parserHandle, uint32 id, uint16** unicodeString,
                            uint32* stringLength);

/**
 * function to tell the meta data information (title, artist, genre etc) of the movie. User data
 * API.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param userDataId [in] User data ID. Type of the user data. Value can be from enum
 * FSL_PARSER_USER_DATA_TYPE
 * @param userDataFormat [in/out] format of user data. Value can be from enum
 * FSL_PARSER_USER_DATA_FORMAT
 * @param userData [out] Buffer containing the information. The core parser manages this buffer and
 * the user shall NOT free it. If no such info is availabe, this value will be set to NULL.
 * @param userDataLength [out] Length of the information in bytes. The informaiton is usually a
 * null-terminated ASCII string. If no such info is available, this value will be set to 0.
 * @return
 */
EXTERN int32 AviGetMetaData(FslFileHandle parserHandle, UserDataID userDataId,
                            UserDataFormat* userDataFormat, uint8** userData,
                            uint32* userDataLength);

/**
 * function to tell the movie duration.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param usDuration [out] Duration in us.
 * @return
 */
EXTERN int32 AviGetMovieDuration(FslFileHandle parserHandle, uint64* usDuration);

/**
 * function to tell a track's duration.
 * The tracks may have different durations.
 * And the movie's duration is usually the video track's duration (maybe not the longest one).
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param usDuration [out] Duration in us.
 * @return
 */
EXTERN int32 AviGetTrackDuration(FslFileHandle parserHandle, uint32 trackNum, uint64* usDuration);

/**
 * function to tell the type of a track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 *
 * @param mediaType [out] Media type of the track. (video, audio, subtitle...)
 *                        "MEDIA_TYPE_UNKNOWN" means the media type is unknown.
 *
 * @param decoderType [out] Decoder type of the track if available. (eg. MPEG-4, H264, AAC, MP3, AMR
 * ...) "UNKNOWN_CODEC_TYPE" means the decoder type is unknown.
 *
 * @param decoderSubtype [out] Decoder Subtype type of the track if available. (eg. AMR-NB, AMR-WB
 * ...) "UNKNOWN_CODEC_SUBTYPE" means the decoder subtype is unknown.
 * @return
 */
EXTERN int32 AviGetTrackType(FslFileHandle parserHandle, uint32 trackNum, uint32* mediaType,
                             uint32* decoderType, uint32* decoderSubtype);

/**
 * function to tell the codec specific information of a track.
 * It's the data of stream format atom (strf).
 * It's a Windows bitmap header for video track (at least 40 bytes)
 * and a Windows Waveform audio header for audio (at least 16 or 18 bytes).
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param data [out] Buffer holding the codec specific information. The user shall never free this
 * buffer.
 * @param size [out] Size of the codec specific information, in bytes.
 * @return
 */
EXTERN int32 AviGetCodecSpecificInfo(FslFileHandle parserHandle, uint32 trackNum, uint8** data,
                                     uint32* size);

/**
 * function to tell the maximum sample size of a track.
 * Avi parser read A/V tracks sample by sample. The max sample size can help the user to prepare a
 * big enough buffer. Warning!The "max sample size" can be zero if the file header information is
 * not complete or index table is not available.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param size [out] Max sample size of the track. Warning!It can be zero if index table does not
 * exist.
 * @return
 */
EXTERN int32 AviGetMaxSampleSize(FslFileHandle parserHandle, uint32 trackNum, uint32* size);

/**
 * function to tell the language of a track used.
 * This is helpful to select an video/audio/subtitle track or menu pages.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param threeCharCode [out] Three or two character language code.
 *                  See ISO 639-2/T for the set of three character codes.Eg. 'eng' for English.
 *                  Four special case:
 *                  mis- "uncoded languages"
 *                  mul- "multiple languages"
 *                  und- "undetermined language"
 *                  zxx- "no linguistic content"
 *
 *                  See ISO 639 for the set of two character codes. Eg. 'en' for English and'zh'for
 * chinese. If ISO 639 is used, the 3rd character will be '\0'.
 * @return
 */
EXTERN int32 AviGetLanguage(FslFileHandle parserHandle, uint32 trackNum, uint8* threeCharCode);

/**
 * function to tell the bitrate of a track.
 * For CBR stream, the real bitrate is given.
 * For VBR stream, 0 is given since the bitrate varies during the playback and AVI parser does not
 * calculate the peak or average bit rate.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param bitrate [out] Bitrate. For CBR stream, this is the real bitrate.
 *                                            For VBR stream, the bitrate is 0 since max bitrate is
 * usually not available.
 * @return
 */
EXTERN int32 AviGetBitRate(FslFileHandle parserHandle, uint32 trackNum, uint32* bitrate);

/**
 * function to tell the sample duration in us of a track.
 * If the sample duration is not a constant (eg. some audio, subtilte), 0 is given.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param usDuration [out] Sample duration in us. If sample duration is not a constant, this value
 * is 0.
 * @return
 */
EXTERN int32 AviGetSampleDuration(FslFileHandle parserHandle, uint32 trackNum, uint64* usDuration);

/**
 * function to tell the width in pixels of a video track.
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a video track.
 * @param width [out] Width in pixels.
 * @return
 */
EXTERN int32 AviGetVideoFrameWidth(FslFileHandle parserHandle, uint32 trackNum, uint32* width);

/**
 * function to tell the height in pixels of a video track.
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a video track.
 * @param height [out] Height in pixels.
 * @return
 */
EXTERN int32 AviGetVideoFrameHeight(FslFileHandle parserHandle, uint32 trackNum, uint32* height);

EXTERN int32 AviGetVideoFrameRate(FslParserHandle parserHandle, uint32 trackNum, uint32* rate,
                                  uint32* scale);

/**
 * function to tell how many channels in an audio track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to an audio track.
 * @param numchannels [out] Number of the channels. 1 mono, 2 stereo, or more for multiple channels.
 * @return
 */
EXTERN int32 AviGetAudioNumChannels(FslFileHandle parserHandle, uint32 trackNum,
                                    uint32* numchannels);

/**
 * function to tell the audio sample rate (sampling frequency) of an audio track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to an audio track.
 * @param sampleRate [out] Audio integer sample rate (sampling frequency).
 * @return
 */
EXTERN int32 AviGetAudioSampleRate(FslFileHandle parserHandle, uint32 trackNum, uint32* sampleRate);

/**
 * function to tell the bits per sample for a PCM audio track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a PCM audio track.
 * @param bitsPerSample [out] Bits per PCM sample.
 * @return
 */
EXTERN int32 AviGetAudioBitsPerSample(FslFileHandle parserHandle, uint32 trackNum,
                                      uint32* bitsPerSample);

/**
 * function to tell the block alignment for an audio track.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a audio track.
 * @param blockAlign [out] Block alignment in bytes.
 * @return
 */
EXTERN int32 AviGetAudioBlockAlign(FslFileHandle parserHandle, uint32 trackNum, uint32* blockAlign);

/**
 * Function to tell the width of a text track.
 * The text track defines a window to display the subtitles.
 * This window shall be positioned in the middle of the screen.
 * And the sample is displayed in the window.How to position the sample within the window is defined
 * by the sample data. The origin of window is always (0, 0).
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param width [out] Width of the text track, in pixels.
 * @return
 */
EXTERN int32 AviGetTextTrackWidth(FslFileHandle parserHandle, uint32 trackNum, uint32* width);

/**
 * Function to tell the height of a text track.
 * The text track defines a window to display the subtitles.
 * This window shall be positioned in the middle of the screen.
 * And the sample is displayed in the window.How to position the sample within the window is defined
 * by the sample data. The origin of window is always (0, 0).
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param height [out] Height of the window, in pixels.
 * @return
 */
EXTERN int32 AviGetTextTrackHeight(FslFileHandle parserHandle, uint32 trackNum, uint32* height);

EXTERN int32 AviSetReadMode(FslParserHandle parserHandle, uint32 readMode);

EXTERN int32 AviGetReadMode(FslParserHandle parserHandle, uint32* readMode);

EXTERN int32 AviEnableTrack(FslParserHandle parserHandle, uint32 trackNum, bool enable);

/**
 * function to read the next sample from a track.
 *
 * For clips not protected by DRM, it supports partial output of large samples:
 * If the entire sample can not be output by calling this function once, its remaining data
 * can be got by repetitive calling the same function.
 *
 * For DRM-protected clips, can only output entire samples for decryption need.
 *
 * For A/V tracks, the time stamp of samples are continuous. If a sample is output, its start time
 * and end time are also output. But for subtitle text tracks, the time stamp & duration are
 * discontinuous and encoded in the sample data.So the parser gives an "estimated" time stamp. The
 * decoder shall decode the accurate time stamp.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track to read, 0-based.
 * @param sampleData [in]   Buffer to hold the sample data.
 *                          For DRM-protected clips, it must be large enough to hold the entire
 * sample for decryption need. Otherwise this function will fail. For non-protected clips, if the
 * buffer is not big enough, only the 1st part of sample is output.
 *
 * @param dataSize [in/out]  Size of the buffer as input, in bytes.
 *                             As output:
 *
 *                              If a sample or part of sample is output successfully (return value
 * is PARSER_SUCCESS ), it's the size of the data actually got.
 *
 *                              If the sample can not be output at all because buffer is too small
 *                              (the return value is PARSER_INSUFFICIENT_MEMORY), it's the buffer
 * size needed. Only for DRM-protected files.
 *
 * @param usStartTime [out] Start time of the sample in us (timestamp)
 * @param usEndTime [out] End time of the sample in us.
 *
 * @param flag [out] Flags of this sample if a sample is got successfully.
 *
 *                            FLAG_SYNC_SAMPLE
 *                                  Whether this sample is a sync sample (key frame).
 *                                  For non-video media, the wrapper shall take every sample as sync
 * sample.
 *
 *                            FLAG_UNCOMPRESSED_SAMPLE
 *                                  Whethter this sample is a uncompressed one. Uncompressed samples
 * shall bypass the decoder. Warning: Video track may have both compressed & uncompressed samples.
 *                                                But some AVI clips seem to abuse this flag, sync
 * samples are mark as uncompressed, although they are actually compressed ones.
 *
 *                            FLAG_SAMPLE_ERR_CONCEALED
 *                                  There is error in bitstream but a sample is still got by error
 * concealment.
 *
 *                            FLAG_SAMPLE_SUGGEST_SEEK
 *                                  A seeking on ALL tracks is suggested although samples can be got
 * by error concealment. Because there are many corrupts, and A/V sync is likely impacted by simple
 * concealment(scanning bitstream).
 *
 *                            FLAG_SAMPLE_NOT_FINISHED
 *                                  Sample data output is not finished because the buffer is not big
 * enough. Need to get the remaining data by repetitive calling this func. This feature is only for
 * non-protected clips.
 *
 *
 * @return  PARSER_SUCCESS     An entire sample or part of it is got successfully.
 *          PARSER_EOS     No sample is got because of end of the track.
 *          PARSER_INSUFFICIENT_MEMORY Buffer is not big enough to hold the entire sample.
 *                                  The user can allocate a larger buffer and call this API again.
 * (Only for DRM-protected clips). PARSER_READ_ERROR  File reading error. No need for further error
 * concealment. PARSER_ERR_CONCEAL_FAIL  There is error in bitstream, and no sample can be found by
 * error concealment. A seeking is helpful. Others ...
 */
EXTERN int32 AviGetNextSample(FslParserHandle parserHandle, uint32 trackNum, uint8** sampleBuffer,
                              void** bufferContext, uint32* dataSize, uint64* usStartTime,
                              uint64* usDuration, uint32* sampleFlags);

EXTERN int32 AviGetFileNextSample(FslParserHandle parserHandle, uint32* trackNum,
                                  uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                                  uint64* usStartTime, uint64* usDuration, uint32* sampleFlags);

/**
 * Function to seek a track to a target time. It will seek to a sync sample of the time stamp
 * matching the target time. Due to the scarcity of the video sync samples (key frames),
 * there can be a gap between the target time and the timestamp of the matched sync sample.
 * So this time stamp will be output to as the accurate start time of the following playback
 * segment. NOTE: Seeking to the beginning of the movie (target time is 0 us) does not require the
 * index table.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track to seek, 0-based.
 * @param usTime [in/out]  Target time to seek as input, in us.
 *                         Actual seeking time, timestamp of the matched sync sample, as output.
 *
 * @param flag [in]  Control flags to seek.
 *
 *                      SEEK_FLAG_NEAREST
 *                      Default flag.The matched time stamp shall be the nearest one to the target
 * time (may be later or earlier).
 *
 *                      SEEK_FLAG_NO_LATER
 *                      The matched time stamp shall be no later than the target time.
 *
 *                      SEEK_FLAG_NO_EARLIER
 *                      The matched time stamp shall be no earlier than the target time.
 *
 * @return  PARSER_SUCCESS    Seeking succeeds.
 *          PARSER_ERR_NOT_SEEKABLE    Seeking fails because he movie is not seekable (index not
 * available) Others      Seeking fails for other reason.
 */
EXTERN int32 AviSeek(FslFileHandle parserHandle, uint32 trackNum, uint64* usTime, uint32 flag);

/**
 * function to get the next or previous sync sample (key frame) from current reading position of a
 * track. For trick mode FF/RW. Also support partial output of large samples for clips not protected
 * by DRM. If not the entire sample is got, its remaining data can be got by repetitive calling the
 * same function.
 *
 * @param parserHandle [in] Handle of the AVI core parser.
 * @param trackNum [in] Number of the track to read, 0-based.
 * @param direction [in]  Direction to get the sync sample.
 *                           FLAG_FORWARD   Read the next sync sample from current reading position.
 *                           FLAG_BACKWARD  Read the previous sync sample from current reading
 * position.
 *
 * @param sampleData [in]   Buffer to hold the sample data.
 *                          For DRM-protected clips, it must be large enough to hold the entire
 * sample for decryption need. Otherwise this function will fail. For non-protected clips, if the
 * buffer is not big enough, only the 1st part of sample is output.
 *
 * @param dataSize [in/out]  Size of the buffer as input, in bytes.
 *                             As output:
 *
 *                              If a sample or part of sample is output successfully (return value
 * is PARSER_SUCCESS ), it's the size of the data actually got.
 *
 *                              If the sample can not be output at all because buffer is too small
 *                              (the return value is PARSER_INSUFFICIENT_MEMORY), it's the buffer
 * size needed. Only for DRM-protected files.
 *
 * @param usStartTime [out] Start time of the sample in us (timestamp)
 * @param usEndTime [out] End time of the sample in us.
 * @param flag [out] Flags of this sample.
 *                            FLAG_SYNC_SAMPLE
 *                                  Whether this sample is a sync sample (key frame).
 *                                  For non-video media, the wrapper shall take every sample as sync
 * sample. This flag shall always be SET for this API.
 *
 *                            FLAG_UNCOMPRESSED_SAMPLE
 *                                  Whethter this sample is a uncompressed one. Uncompressed samples
 * shall bypass the decoder. Warning: Video track may have both compressed & uncompressed samples.
 *                                                But some AVI clips seem to abuse this flag, sync
 * samples are mark as uncompressed, although they are actually compressed ones.
 *
 *                            FLAG_SAMPLE_ERR_CONCEALED
 *                                  There is error in bitstream but a sample is still got by error
 * concealment.
 *
 *                            FLAG_SAMPLE_SUGGEST_SEEK
 *                                  A seeking on ALL tracks is suggested although samples can be got
 * by error concealment. Because there are many corrupts, and A/V sync is likely impacted by simple
 * concealment(scanning bitstream).
 *
 *                            FLAG_SAMPLE_NOT_FINISHED
 *                                  Sample data output is not finished because the buffer is not big
 * enough. Need to get the remaining data by repetitive calling this func. This feature is only for
 * non-protected clips.
 *
 * @return  PARSER_SUCCESS     An entire sync sample or part of it is got successfully.
 *          PARSER_ERR_NOT_SEEKABLE    No sync sample is got  because the movie is not seekable
 * (index not available) * PARSER_EOS      Reaching the end of the track, no sync sample is got.
 *          PARSER_BOS      Reaching the beginning of the track, no sync sample is got.
 *          PARSER_INSUFFICIENT_MEMORY Buffer is too small to hold the sample.
 *                                  The user can allocate a larger buffer and call this API again
 * (Only for DRM-protected clips). PARSER_READ_ERROR  File reading error. No need for further error
 * concealment. PARSER_ERR_CONCEAL_FAIL  There is error in bitstream, and no sample can be found by
 * error concealment. A seeking is helpful. Others ... Reading fails for other reason.
 */
EXTERN int32 AviGetNextSyncSample(FslFileHandle parserHandle, uint32 direction, uint32 trackNum,
                                  uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                                  uint64* usStartTime, uint64* usDuration, uint32* flag);

EXTERN int32 AviGetFileNextSyncSample(FslParserHandle parserHandle, uint32 direction,
                                      uint32* trackNum, uint8** sampleBuffer, void** bufferContext,
                                      uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                                      uint32* sampleFlags);

#endif  // FSL_AVI_PARSER_API_HEADER_INCLUDED
