
/*
 ***********************************************************************
 * Copyright (c) 2005-2012, Freescale Semiconductor, Inc.
 *
 * Copyright 2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

/**
 * @file mpeg2_parser_api.h
 * @MPEG2 file parser header files
 */

#ifndef FSL_MPEG2_PARSER_API_HEADER_INCLUDED
#define FSL_MPEG2_PARSER_API_HEADER_INCLUDED

/* common files of mmalyer */
#include "fsl_types.h"
#include "file_stream.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"

/***************************************************************************************
 *                  Data Structures & Constants
 ***************************************************************************************/
#define MAX_MPEG2_STREAMS                                                                        \
    128 /* maximum media streams to support for an MPEG2 movie.                                  \
                                    If there are more streams, those with a larger stream number \
           will be overlooked */

/*
 * error code of the MPEG2 parser API.

 */
typedef enum {
    MPEG2_ERR_WRONG_FILE_SIZE = -111,

    MPEG2_ERR_NOT_MPEG2_FILE = -120, /* This is not an MPEG2 file. No RIFF MPEG2 header found */
    MPEG2_ERR_UNKNOWN_STREAM_FORMAT = -121,
    MPEG2_ERR_WRONG_MEDIA_TYPE = -122, /* An API is called on a stream of wrong media type. */
    MPEG2_ERR_WRONG_VIDEO_FORMAT = -123,
    MPEG2_ERR_WRONG_AUDIO_FORMAT = -124,

    MPEG2_ERR_CORRUPTED_INDEX = -130,
    MPEG2_ERR_WRONG_INDEX_SAMPLE_SIZE = -131,   /* the index give wrong sample size */
    MPEG2_ERR_WRONG_INDEX_SAMPLE_OFFSET = -132, /* the index give wrong sample offset */
    MPEG2_ERR_WRONG_IDX1_LIST_SIZE = -133,
    MPEG2_ERR_WRONG_MPEG22_INDEX_SIZE = -134,
    MPEG2_ERR_WRONG_MPEG22_INDEX_ENTRY_SIZE = -135,
    MPEG2_ERR_INDEX_TYPE_NOT_SUPPORTED = -136,
    MPEG2_ERR_SUPER_INDEX_ENTRY_NOT_FOUND = -137,
    MPEG2_ERR_EMPTY_INDEX = -138,

    MPEG2_ERR_ZERO_STREAM_RATE =
            -150,                  /* The rate of a stream is ZERO. Can not calculate time stamp.*/
    MPEG2_ERR_NO_USER_DATA = -155, /* the user data is not available */
    MPEG2_ERR_WRONG_STREAM_NUM = -162,
    MPEG2_ERR_WRONG_MOVIE_LIST_SIZE = -170,
    MPEG2_ERR_NO_MOVIE_LIST = -171,
    MPEG2_ERR_NOTALLOCED_MEMORY = -180,

    MPEG2_ERR_NO_DURATION = -200,

    MPEG2_ERR_NO_MORE_ARRAY_PACKETS = -211,

    MPEG2_ERR_FILE_READ_POS = -250,
    MPEG2_ERR_INDEX_OUTOFRANGE = -260
} MPEG2_PARSER_ERROR_CODE_ENUM;

typedef int32 MPEG2_PARSER_ERROR_CODE;

/*
typedef enum{
    MPEG1_SS = 0,
    MPEG2_PS = 1,
    MPEG2_TS = 2
}MPEG2_PARSER_STREAM_TYPE;
*/
/*
 * this structure could be invisible for Application
 */

/***************************************************************************************
 *                  API Funtions
 * For calling sequence, please refer to the end of this file.
 ***************************************************************************************/
#ifdef __cplusplus
#define EXTERN extern "C"
#else
#define EXTERN
#endif

/**
 * function to get the MPEG2 core parser version.
 *
 * @return Version string.
 */
EXTERN const char* Mpeg2ParserVersionInfo();

/**
 * function to create the MPEG2 core parser.
 *
 * @param stream [in]   Source stream of the MPEG2 file.
 *                      It implements functions to open, close, tell, read and seek a file.
 *
 * @param memOps [in]   Memory operation callback table.
 *                      It implements the functions to malloc, calloc, realloc and free memory.
 *
 * @param context [in]  Wrapper context for the callback functions. Mpeg2 parser never modify it.
 * @param parserHandle [out] Handle of the MPEG2 core parser if succeeds. NULL for failure.
 * @return
 */

EXTERN int32 Mpeg2CreateParser(bool isLive, FslFileStream* streamOps, ParserMemoryOps* memOps,
                               ParserOutputBufferOps* outputBufferOps, void* context,
                               FslParserHandle* parserHandle);

EXTERN int32 Mpeg2CreateParser2(uint32 flags, FslFileStream* streamOps, ParserMemoryOps* memOps,
                                ParserOutputBufferOps* outputBufferOps, void* context,
                                FslParserHandle* parserHandle);

/**
 * Function to delete the Mpeg2 core parser.
 *
 * @param parserHandle Handle of the Mpeg2 core parser.
 * @return
 */
EXTERN int32 Mpeg2DeleteParser(FslParserHandle parserHandle);

/**
 * Function to tell whether the movie is seekable.
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param seekable [out] true for seekable and false for non-seekable
 * @return
 */
EXTERN int32 Mpeg2IsSeekable(FslParserHandle parserHandle, bool* seekable);

EXTERN int32
Mpeg2ParserInitializeIndex(FslParserHandle parserHandle); /*Loading index from the movie file */

EXTERN int32 Mpeg2ParserImportIndex(FslParserHandle parserHandle, /* Import index from outside */
                                    uint8* buffer, uint32 size);
EXTERN int32 Mpeg2ParserExportIndex(FslParserHandle parserHandle, uint8* buffer, uint32* size);

/**
 * Function to tell how many programs in the movie.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param numPrograms [out] Number of programs.
 * @return
 */
EXTERN int32 Mpeg2GetNumPrograms(FslParserHandle parserHandle, uint32* numPrograms);

/**
 * Function to tell how many programs in the movie.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param programNum [in] Index of programs.
 * @param numTracks [out] Track num in the given program
 * @param ppTrackNumList [out] Track index array
 * @param
 * @return
 */
EXTERN int32 Mpeg2GetProgramTracks(FslParserHandle parserHandle, uint32 programNum,
                                   uint32* numTracks, uint32** ppTrackNumList);

/**
 * Function to tell how many tracks in the movie.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param numTracks [out] Number of tracks.
 * @return
 */
EXTERN int32 Mpeg2GetNumTracks(FslParserHandle parserHandle, uint32* numTracks);

/**
 * Function to tell the user data information (title, artist, genre etc) of the movie. User data
 * API. The information is usually a null-terminated ASCII string.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param userDataId [in] User data ID. Type of the user data.
 * @param buffer [out] Buffer containing the information. The core parser manages this buffer and
 * the user shall NOT free it. If no such info is availabe, this value will be set to NULL.
 * @param size [out] Length of the information in bytes.
 *                               If no such info is available, this value will be set to 0.
 * @return
 */
EXTERN int32 Mpeg2GetUserData(FslParserHandle parserHandle, uint32 userDataId, uint8** buffer,
                              uint32* size);

/**
 * Function to tell the user data information (title, artist, genre etc) of the movie. User data
 * API. The information is usually a null-terminated ASCII string.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param userDataId [in] User data ID. Type of the user data.
 * @param userDataFormat [in/out] User data format. Format of the user data.
 * @param userData [out] Buffer containing the information. The core parser manages this buffer and
 * the user shall NOT free it. If no such info is availabe, this value will be set to NULL.
 * @param userDataLength [out] Length of the information in bytes.
 *                               If no such info is available, this value will be set to 0.
 * @return
 */
EXTERN int32 Mpeg2GetMetaData(FslParserHandle parserHandle, UserDataID userDataId,
                              UserDataFormat* userDataFormat, uint8** userData,
                              uint32* userDataLength);

/**
 * Function to tell the movie duration.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param usDuration [out] Duration in us.
 * @return
 */
EXTERN int32 Mpeg2GetMovieDuration(FslParserHandle parserHandle, uint64* usDuration);

/**
 * Function to tell a track's duration.
 * The tracks may have different durations.
 * And the movie's duration is usually the video track's duration (maybe not the longest one).
 *
 * If a track's duration is 0, this track is empty! Application can read nothing from an empty
 * track.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param usDuration [out] Duration in us.
 * @return
 */
EXTERN int32 Mpeg2GetTrackDuration(FslParserHandle parserHandle, uint32 trackNum,
                                   uint64* usDuration);

/**
 * Function to tell the type of a track.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
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
EXTERN int32 Mpeg2GetTrackType(FslParserHandle parserHandle, uint32 trackNum, uint32* mediaType,
                               uint32* decoderType, uint32* decoderSubtype);

/**
 * Function to tell the decoder specific information of a track.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param data [out] Buffer holding the decoder specific information. The user shall never free this
 * buffer.
 * @param size [out] Size of the codec specific information, in bytes.
 * @return
 */
EXTERN int32 Mpeg2GetDecoderSpecificInfo(FslParserHandle parserHandle, uint32 trackNum,
                                         uint8** data, uint32* size);

/**
 * Optional API. Function to tell the maximum sample size of a track.
 * Mpeg2 parser read A/V tracks sample by sample. The max sample size can help the user to prepare a
 * big enough buffer. Warning: [1]The "max sample size" is not available if the index table is
 * invalid. And for Mpeg2 file, if the index table is invalid, playback will be affected. [2]Core
 * parser need to scan the whole index table to get the max sample size. So this API is
 *    time-consuming for long movies that has big index tables. For application that requests a
 * quick movie loading, it's better not call this API. Anyway, the parser can output a sample piece
 * by piece if output buffer is not big enough.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param size [out] Max sample size of the track, in bytes.
 *
 * @return:
 * PARSER_SUCCESS
 * PARSER_ERR_INVALID_MEDIA  Index table is invalid so max sample size is not available.
 */
EXTERN int32 Mpeg2GetMaxSampleSize(FslParserHandle parserHandle, uint32 trackNum, uint32* size);

/**
 * Function to tell the language of a track used.
 * This is helpful to select a video/audio/subtitle track or menu pages.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param threeCharCode [out] Three character language code.
 *                  See ISO 639-2/T for the set of three character codes.Eg. 'eng' for English.
 *                  Four special cases:
 *                  mis- "uncoded languages"
 *                  mul- "multiple languages"
 *                  und- "undetermined language"
 *                  zxx- "no linguistic content"
 * @return
 */
EXTERN int32 Mpeg2GetLanguage(FslParserHandle parserHandle, uint32 trackNum, uint8* threeCharCode);

/**
 * Function to tell the average bitrate of a track.
 * If the average bitrate is not available in the file header, 0 is given.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param bitrate [out] Average bitrate, in bits per second.
 * @return
 */
EXTERN int32 Mpeg2GetBitRate(FslParserHandle parserHandle, uint32 trackNum, uint32* bitrate);

/**
 * Function to tell the sample duration in us of a track.
 * If the sample duration is not a constant (eg. audio, subtitle), 0 is given.
 * For a video track, the frame rate can be calculated from this information.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based.
 * @param usDuration [out] Sample duration in us. If sample duration is not a constant, this value
 * is 0.
 * @return
 */

EXTERN int32 Mpeg2GetVideoFrameRate(FslParserHandle parserHandle, uint32 trackNum, uint32* rate,
                                    uint32* scale);

// EXTERN int32 Mpeg2GetVideoFrameDuration(FslParserHandle parserHandle, uint32 trackNum, uint64
// *usDuration);

/**
 * Function to tell the width in pixels of a video track.
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a video track.
 * @param width [out] Width in pixels.
 * @return
 */
EXTERN int32 Mpeg2GetVideoFrameWidth(FslParserHandle parserHandle, uint32 trackNum, uint32* width);

/**
 * Function to tell the height in pixels of a video track.
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a video track.
 * @param height [out] Height in pixels.
 * @return
 */
EXTERN int32 Mpeg2GetVideoFrameHeight(FslParserHandle parserHandle, uint32 trackNum,
                                      uint32* height);

/**
 * Function to tell the scan type of video stream
 * @param parserHandle [in] Handle of the MPEG2 core parser.
 * @param streamNum [in] ID of the stream. It must point to a video stream.
 * @param scanType [out] scan type, either VIDEO_SCAN_PROGRESSIVE or VIDEO_SCAN_INTERLACED.
 * @return
 */
EXTERN int32 Mpeg2GetVideoScanType(FslParserHandle parserHandle, uint32 streamNum,
                                   uint32* scanType);

/**
 * Function to tell how many channels in an audio track.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to an audio track.
 * @param numchannels [out] Number of the channels. 1 mono, 2 stereo, or more for multiple channels.
 * @return
 */
EXTERN int32 Mpeg2GetAudioNumChannels(FslParserHandle parserHandle, uint32 trackNum,
                                      uint32* numChannels);

/**
 * Function to tell the audio sample rate (sampling frequency) of an audio track.
 * Warning:
 * The audio sample rate from the file header may be wrong. And if the audio decoder specific
 * information is available (for AAC), the decoder can double check the audio sample rate from this
 * information.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to an audio track.
 * @param sampleRate [out] Audio integer sample rate (sampling frequency).
 * @return
 */
EXTERN int32 Mpeg2GetAudioSampleRate(FslParserHandle parserHandle, uint32 trackNum,
                                     uint32* sampleRate);

/**
 * Function to tell the bits per sample for an audio track.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to an audio track.
 * @param bitsPerSample [out] Bits per PCM sample.
 * @return
 */
EXTERN int32 Mpeg2GetAudioBitsPerSample(FslParserHandle parserHandle, uint32 trackNum,
                                        uint32* bitsPerSample);

/**
 * Function to tell audio presentation num for an audio track.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to an audio track.
 * @param presentationNum [out] audio presentation num.
 * @return
 */
EXTERN int32 Mpeg2GetAudioPresentationNum(FslParserHandle parserHandle, uint32 streamNum,
                                          int32* presentationNum);

/**
 * Function to tell audio presentation info for an audio track.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to an audio track.
 * @param presentationNum [in] audio presentation num.
 * @return
 */
EXTERN int32 Mpeg2GetAudioPresentationInfo(FslParserHandle parserHandle, uint32 streamNum,
                                           int32 presentationNum, int32* presentationId,
                                           char** language, uint32* masteringIndication,
                                           uint32* audioDescriptionAvailable,
                                           uint32* spokenSubtitlesAvailable,
                                           uint32* dialogueEnhancementAvailable);

/**
 * Function to tell the width of a text track.
 * The text track defines a window to display the subtitles.
 * This window shall be positioned in the middle of the screen.
 * And the sample is displayed in the window.How to position the sample within the window is defined
 * by the sample data. The origin of window is always (0, 0).
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param width [out] Width of the text track, in pixels.
 * @return
 */
EXTERN int32 Mpeg2GetTextTrackWidth(FslParserHandle parserHandle, uint32 trackNum, uint32* width);

/**
 * Function to tell the height of a text track.
 * The text track defines a window to display the subtitles.
 * This window shall be positioned in the middle of the screen.
 * And the sample is displayed in the window.How to position the sample within the window is defined
 * by the sample data. The origin of window is always (0, 0).
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param height [out] Height of the window, in pixels.
 * @return
 */
EXTERN int32 Mpeg2GetTextTrackHeight(FslParserHandle parserHandle, uint32 trackNum, uint32* height);

/**
 * Function to set the mode to read media samples, file-based or track-based.
 *  a. File-based sample reading.
 *      The reading order is same as that of track interleaving in the file.
 *      Mainly for streaming application.
 *
 *  b. Track-based sample reading.
 *      Each track can be read and seeked independently from each other.
 *
 * Warning:
 *  - The parser may support only one reading mode.Setting a not-supported reading mode will fail.
 *  - Once selected, the reading mode can no longer change.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param readMode [in] Sample reading mode.
 *
 *                      READ_MODE_FILE_BASED
 *                      Default mode.Linear sample reading. The reading order is same as that of
 *                      track interleaving in the file.
 *
 *                      READ_MODE_TRACK_BASED
 *                      Track-based sample reading. Each track can be read independently from each
 * other.
 * @return
 * PARSER_SUCCESS   The reading mode is set successfully.
 * PARSER_NOT_IMPLEMENTED   The reading mode is not supported.
 *
 */
EXTERN int32 Mpeg2SetReadMode(FslParserHandle parserHandle, uint32 readMode);

/**
 * Function to get the mode to read media samples, file-based or track-based. *
 * And the parser has a default read mode.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param readMode [out] Current Sample reading mode.
 *
 *                      READ_MODE_FILE_BASED
 *                      Default mode.Linear sample reading. The reading order is same as that of
 *                      track interleaving in the file.
 *
 *                      READ_MODE_TRACK_BASED
 *                      Track-based sample reading. Each track can be read independently from each
 * other.
 * @return
 */
EXTERN int32 Mpeg2GetReadMode(FslParserHandle parserHandle, uint32* readMode);

/**
 * Function to enable or disable track.
 * The parser can only output samples from enabled tracks.
 * To avoid unexpected memory cost or data output from a track, the application can disable the
 * track.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @param enable [in] TRUE to enable the track and FALSE to disable it.
 * @return
 */
EXTERN int32 Mpeg2EnableTrack(FslParserHandle parserHandle, uint32 trackNum, bool enable);

/**
 * Function to read the next sample from a track, only for track-based reading mode.
 *
 * In this function, the parser may use callback to request buffers to output the sample.
 * And it will tell which buffer is output on returning. But if the buffer is not available or too
 * small, this function will fail.
 *
 * It supports partial output of large samples:
 * If the entire sample can not be output by calling this function once, its remaining data
 * can be got by repetitive calling the same function.
 *
 * BSAC audio track is somewhat special:
 * Parser does not only read this BSAC track. But it will read one sample from each BSAC track
 * in the proper order and make up a "big sample" for the user.
 * Partial output is still supported and the sample flags describe this "big sample".
 * So the user shall take all BSAC tracks as one track and use any BSAC track number to call this
 * function.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track to read, 0-based.
 * @param sampleBuffer [out]   Buffer to hold the sample data.
 *                             If the buffer is not big enough, only part of sample is output.
 * @param bufferContext [out] Buffer context from application, got on requesting the buffer.
 *
 * @param dataSize [out]    If a sample or part of sample is output successfully (return value is
 * PARSER_SUCCESS ), it's the size of the data actually got.
 *
 * @param usStartTime [out] Start time of the sample in us (timestamp)
 * @param usDuration [out] Duration of the sample in us. PARSER_UNKNOWN_DURATION for unknown
 * duration.
 *
 * @param sampleFlags [out] Flags of this sample, if a sample or part of it is got successfully.
 *
 *                            FLAG_SYNC_SAMPLE
 *                                  Whether this sample is a sync sample (video key frame, random
 * access point). For non-video media, the application can take every sample as sync sample.
 *
 *                            FLAG_SAMPLE_NOT_FINISHED
 *                                  Sample data output is not finished because the buffer is not big
 * enough. Need to get the remaining data by repetitive calling this func.
 *
 *
 * @return  PARSER_SUCCESS     An entire sample or part of it is got successfully.
 *          PARSER_EOS     No sample is got because of end of the track.
 *          PARSER_INSUFFICIENT_MEMORY Buffer is not availble, or too small to output any data at
 * all. PARSER_READ_ERROR  File reading error. No need for further error concealment. Others ...
 */
EXTERN int32 Mpeg2GetNextSample(FslParserHandle parserHandle, uint32 trackNum, uint8** sampleBuffer,
                                void** bufferContext, uint32* dataSize, uint64* usStartTime,
                                uint64* usDuration, uint32* sampleFlags);

/**
 * Function to read the next sample from the file, only for file-based reading mode.
 * The parser will tell which track is outputting now and disabled track will be skipped.
 *
 * In this function, the parser may use callback to request buffers to output the sample.
 * And it will tell which buffer is output on returning. But if the buffer is not available or too
 * small, this function will fail.
 *
 * It supports partial output of large samples:
 * If the entire sample can not be output by calling this function once, its remaining data
 * can be got by repetitive calling the same function.
 *
 * BSAC audio track is somewhat special:
 * Parser does not only read this BSAC track. But it will read one sample from each BSAC track
 * in the proper order and make up a "big sample" for the user.
 * Partial output is still supported and the sample flags describe this "big sample".
 * So the user shall take all BSAC tracks as one track and use any BSAC track number to call this
 * function.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [out] Number of the track to read, 0-based.
 * @param sampleBuffer [out]   Buffer to hold the sample data.
 *                             If the buffer is not big enough, only part of sample is output.
 * @param bufferContext [out] Buffer context from application, got on requesting the buffer.
 *
 * @param dataSize [out]    If a sample or part of sample is output successfully (return value is
 * PARSER_SUCCESS ), it's the size of the data actually got.
 *
 * @param usStartTime [out] Start time of the sample in us (timestamp)
 * @param usDuration [out] Duration of the sample in us. PARSER_UNKNOWN_DURATION for unknown
 * duration.
 *
 * @param sampleFlags [out] Flags of this sample, if a sample or part of it is got successfully.
 *
 *                            FLAG_SYNC_SAMPLE
 *                                  Whether this sample is a sync sample (video key frame, random
 * access point). For non-video media, the application can take every sample as sync sample.
 *
 *                            FLAG_SAMPLE_NOT_FINISHED
 *                                  Sample data output is not finished because the buffer is not big
 * enough. Need to get the remaining data by repetitive calling this func.
 *
 *
 * @return  PARSER_SUCCESS     An entire sample or part of it is got successfully.
 *          PARSER_EOS     No sample is got because of end of the track.
 *          PARSER_INSUFFICIENT_MEMORY Buffer is not availble, or too small to output any data at
 * all. PARSER_READ_ERROR  File reading error. No need for further error concealment. Others ...
 */

EXTERN int32 Mpeg2GetFileNextSample(FslParserHandle parserHandle, uint32* trackNum,
                                    uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                                    uint64* usStartTime, uint64* usDuration, uint32* sampleFlags);

/**
 * Function to seek a track to a target time, for both track-based mode and file-based mode.
 * The parser handles the mode difference internally.
 *
 * It will seek to a sync sample of the time stamp
 * matching the target time. Due to the scarcity of the video sync samples (key frames),
 * there can be a gap between the target time and the timestamp of the matched sync sample.
 * So this time stamp will be output to as the accurate start time of the following playback
 * segment.
 *
 * BSAC audio track is somewhat special:
 * Parser does not only seek this BSAC track. But it will seek all BSAC tracks to the target time
 * and align their reading position.
 * So the user shall take all BSAC tracks as one track and use any BSAC track number to call this
 * function.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
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
 *          PARSER_EOS  Can NOT to seek to the target time because of end of stream.
 *          PARSER_ERR_NOT_SEEKABLE    Seeking fails because the movie is not seekable (index not
 * available or no sync samples) Others      Seeking fails for other reason.
 */
EXTERN int32 Mpeg2Seek(FslParserHandle parserHandle, uint32 trackNum, uint64* usTime, uint32 flag);

/**
 * Function to get the next or previous sync sample (key frame) from current reading position of a
 * track, only for track-based reading mode.
 *
 * For trick mode FF/RW.
 * Also support partial output of large samples.
 * If not the entire sample is got, its remaining data can be got by repetitive calling the same
 * function.
 *
 * Warning: This function does not support BSAC audio tracks because there is dependency between
 * these tracks.
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track to read, 0-based.
 * @param direction [in]  Direction to get the sync sample.
 *                           FLAG_FORWARD   Read the next sync sample from current reading position.
 *                           FLAG_BACKWARD  Read the previous sync sample from current reading
 * position.
 *
 * @param sampleBuffer [out]   Buffer to hold the sample data.
 *                             If the buffer is not big enough, only part of sample is output.
 * @param bufferContext [out] Buffer context from application, got on requesting the buffer.
 *
 * @param dataSize [out]  If a sample or part of sample is output successfully (return value is
 * PARSER_SUCCESS ), it's the size of the data actually got.
 *
 * @param usStartTime [out] Start time of the sample in us (timestamp)
 * @param usDuration [out] Duration of the sample in us. PARSER_UNKNOWN_DURATION for unknown
 * duration.
 *
 * @param flag [out] Flags of this sample, if a sample or part of it is got successfully.
 *                            FLAG_SYNC_SAMPLE
 *                                  Whether this sample is a sync sample (key frame).
 *                                  For non-video media, the application can take every sample as a
 * sync sample. This flag shall always be SET for this API.
 *
 *                            FLAG_SAMPLE_NOT_FINISHED
 *                                  Sample data output is not finished because the buffer is not big
 * enough. Need to get the remaining data by repetitive calling this func.
 *
 *
 * @return  PARSER_SUCCESS     An entire sync sample or part of it is got successfully.
 *          PARSER_ERR_NOT_SEEKABLE    No sync sample is got  because the movie is not seekable
 * (index not available or no sync samples) PARSER_EOS      Reaching the end of the track, no sync
 * sample is got. PARSER_BOS      Reaching the beginning of the track, no sync sample is got.
 *          PARSER_INSUFFICIENT_MEMORY Buffer is too small to hold the sample data.
 *                                  The user can allocate a larger buffer and call this API again.
 *          PARSER_READ_ERROR  File reading error. No need for further error concealment.
 *          PARSER_ERR_CONCEAL_FAIL  There is error in bitstream, and no sample can be found by
 * error concealment. A seeking is helpful. Others ... Reading fails for other reason.
 */
EXTERN int32 Mpeg2GetNextSyncSample(FslParserHandle parserHandle, uint32 direction, uint32 trackNum,
                                    uint8** sampleBuffer, void** bufferContext, uint32* dataSize,
                                    uint64* usStartTime, uint64* usDuration, uint32* flags);

EXTERN int32 Mpeg2GetFileNextSyncSample(FslParserHandle parserHandle, uint32 direction,
                                        uint32* pStreamNum, uint8** sampleData, void** pAppContext,
                                        uint32* dataSize, uint64* usPresTime, uint64* usDuration,
                                        uint32* flag);

/**
 * Function to get the program PCR data
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param programNum [in] program number
 * @param PCR [out] the value of PCR.
 * @return
 */
EXTERN int32 Mpeg2GetPCR(FslParserHandle parserHandle, uint32 programNum, uint64* PCR);

/**
 * Function to flush track. This function will reset output buffers and stream info
 *
 * @param parserHandle [in] Handle of the Mpeg2 core parser.
 * @param trackNum [in] Number of the track, 0-based. It must point to a text track.
 * @return
 */
EXTERN int32 Mpeg2FlushTrack(FslParserHandle parserHandle, uint32 trackNum);

#endif  // FSL_MPEG2_PARSER_API_HEADER_INCLUDED
