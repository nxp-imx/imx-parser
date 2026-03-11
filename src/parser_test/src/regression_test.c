/************************************************************************
 * Copyright (c) 2005-2015, Freescale Semiconductor, Inc.
 * Copyright 2017-2020, 2024, 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ************************************************************************/

/*=============================================================================

  Module Name:  regression_test.c

  General Description:  Regression test for all FSL core parser libraries.

  =============================================================================*/

/* To support files larger than 2GB under WINCE,
 * non-buffered IO is used. Two reasons:
 * (1)_fseeki64 & _ftelli64 are not defined in C run-time library under WINCE.
 *    And they are not declared in stdlib.h.
 * (2) Even if using 32-bit parameter, fseek/ftell/fread will fail when the
 *    absolute file offset exceeds 2GB.
 *
 */

// #define AUDIO_PARSER
// #define SCAN_THREAD

#ifdef SUPPORT_LARGE_FILE
#if defined(__WINCE) || defined(WIN32)
#define WINCE_MAX_POSITIVE_SEEK_OFFSET 0X7FFFFFFF
#define WINCE_MAX_NEGATIVE_SEEK_OFFSET 0XFFFFFFFF10000000ULL
#define MAX_OFFSET_32BIT 0XFFFFFFFF  //(0 - 0X7FFFFFFF, 0X10000000 - 0XFFFFFFFF)
#else
#define _FILE_OFFSET_BITS 64 /* required by LINUX, must define it before all header files */
#endif
#endif

// #define ID3_PARSER_TEST

/*===============================================================================
  INCLUDE FILES
  =============================================================================*/
#include <ctype.h>
#include "fsl_osal.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "ID3Parser.h"
#include "aac.h"
#include "subtitle.h"

#include "memory_mgr.h"
#include "queue_mgr.h"
#include "fsl_parser_test.h"
#include "err_check.h"

#if defined PLATFORM_ARM11_ELINUX || defined PLATFORM_X86_UNIX || PLATFORM_ARM_ELINUX
typedef uint32 DWORD;
typedef uint8 BYTE;
#endif

#define MULTI_PROGRAM_TEST

// #define DBG_CHECK_APP_CONTEXT

/*===============================================================================
  global control variables
  =============================================================================*/

static FILE* g_fpLog = NULL;

static bool g_dump_track_data = TRUE;
bool g_dump_track_pts = FALSE;  // TRUE;
static bool g_export_index_table = TRUE;
static bool g_import_available_index_table = TRUE;


static uint32 g_first_clip_number = 1; /* the 1st clip number in the list to test */
static uint32 g_last_clip_number =
        (-1); /* the last clip number in the list to test, no limit by defualt */

static bool g_isLiveSource = TRUE;

static char g_parser_lib_path_a[128] = "";
static char g_parser_lib_path_b[128] = "";
FILE* fpStreamInfo = NULL;
FILE* fpRegressionTestLog = NULL;
static uint32 g_test_total = 0;
static uint32 g_test_fail = 0;

static char index_file_name[256];
static uint8 appContext[16] = "fslparser";

static Track tracks[MAX_MEDIA_TRACKS] = {};
static bool g_keepData = FALSE;

static PlayState playState = {0};
static uint64 g_usReferenceClock = 0;

uint32 g_current_clip_number = 0;
uint32 g_failed_clip_count = 0;

int32 g_test_state = TEST_STATE_NULL;

uint64 g_usMaxPTSGitter = 0;
FILE* fpErrLog = NULL;

/* global variables for file I/O callbacks */
static FslFileHandle g_sourceFileHandle = NULL;
static uint32 g_sourceFileRefCount = 0;

/* current file offset - only to test huge files for wince */
#define HUGE_FILE_SIZE_THESHOLD                \
    0X7FFFFFFF /* 2GB,                         \
A file is a huge file if it's larger than 2GB. \
fseek, fread & ftell will fail if absolute file offset exceeds 2GB even if paramenter is valid */
static char* g_testFilePath = NULL;
int64 g_testFileSize = 0;                /* file size */
static int64 g_curTestFileOffset = 0;    /* current file offset, seen by the app. Real offset!*/
static int64 g_targetTestFileOffset = 0; /* current file offset, seen by the core parser */

static int64 g_totalDataSizeAvailable = 0; /* to simulate streaming */

#define MAX_USER_DATA_STRING_LENGTH 512
static char asciiString[MAX_USER_DATA_STRING_LENGTH + 1];

static HANDLE g_hParserMutex;
static bool g_abTrackDisabledByCommand[64];

FslFileStream g_fileOps;
ParserMemoryOps g_memOps;
ParserOutputBufferOps g_outputBufferOps;

typedef struct _USER_DATA {
    UserDataID id;
    UserDataFormat format;
    char* printString;
} UserData;

static UserData g_userDataTable[] = {
        {USER_DATA_TITLE, USER_DATA_FORMAT_UTF8, "Title               : %s\n"},
        {USER_DATA_LANGUAGE, USER_DATA_FORMAT_UTF8, "Language            : %s\n"},
        {USER_DATA_GENRE, USER_DATA_FORMAT_UTF8, "Genre               : %s\n"},
        {USER_DATA_ARTIST, USER_DATA_FORMAT_UTF8, "Artist              : %s\n"},
        {USER_DATA_COPYRIGHT, USER_DATA_FORMAT_UTF8, "Copy Right          : %s\n"},
        {USER_DATA_COMMENTS, USER_DATA_FORMAT_UTF8, "Comments            : %s\n"},
        {USER_DATA_CREATION_DATE, USER_DATA_FORMAT_UTF8, "Creation Date       : %s\n"},
        {USER_DATA_RATING, USER_DATA_FORMAT_UTF8, "Rating              : %s\n"},
        {USER_DATA_ALBUM, USER_DATA_FORMAT_UTF8, "Album               : %s\n"},
        {USER_DATA_VCODECNAME, USER_DATA_FORMAT_UTF8, "Video Codec Name    : %s\n"},
        {USER_DATA_ACODECNAME, USER_DATA_FORMAT_UTF8, "Audio Codec Name    : %s\n"},
        {USER_DATA_ARTWORK, USER_DATA_FORMAT_JPEG, "Found Artwork       : %s, %d bytes\n"},
        {USER_DATA_COMPOSER, USER_DATA_FORMAT_UTF8, "Composer            : %s\n"},
        {USER_DATA_DIRECTOR, USER_DATA_FORMAT_UTF8, "Director            : %s\n"},
        {USER_DATA_INFORMATION, USER_DATA_FORMAT_UTF8, "Information         : %s\n"},
        {USER_DATA_CREATOR, USER_DATA_FORMAT_UTF8, "Creator             : %s\n"},
        {USER_DATA_PRODUCER, USER_DATA_FORMAT_UTF8, "Producer            : %s\n"},
        {USER_DATA_PERFORMER, USER_DATA_FORMAT_UTF8, "Performer           : %s\n"},
        {USER_DATA_REQUIREMENTS, USER_DATA_FORMAT_UTF8, "Requirements        : %s\n"},
        {USER_DATA_SONGWRITER, USER_DATA_FORMAT_UTF8, "Song Writer         : %s\n"},
        {USER_DATA_MOVIEWRITER, USER_DATA_FORMAT_UTF8, "Movie Writer        : %s\n"},
        {USER_DATA_TOOL, USER_DATA_FORMAT_UTF8, "Writing Application : %s\n"},
        {USER_DATA_DESCRIPTION, USER_DATA_FORMAT_UTF8, "Description         : %s\n"},
        {USER_DATA_TRACKNUMBER, USER_DATA_FORMAT_UTF8, "Track Number        : %s\n"},
        {USER_DATA_TOTALTRACKNUMBER, USER_DATA_FORMAT_UTF8, "Total Track Number  : %s\n"},
        {USER_DATA_LOCATION, USER_DATA_FORMAT_UTF8, "Geographic Location : %s\n"},
        {USER_DATA_AUTHOR, USER_DATA_FORMAT_UTF8, "Author              : %s\n"},
        {USER_DATA_COLLECTION, USER_DATA_FORMAT_UTF8, "Collector           : %s\n"},
        {USER_DATA_PUBLISHER, USER_DATA_FORMAT_UTF8, "publisher           : %s \n"},
        {USER_DATA_SOFTWARE, USER_DATA_FORMAT_UTF8, "Software            : %s \n"},
        {USER_DATA_YEAR, USER_DATA_FORMAT_UTF8, "Year                : %s \n"},
        {USER_DATA_KEYWORDS, USER_DATA_FORMAT_UTF8, "Keywords            : %s \n"},
        {USER_DATA_ALBUMARTIST, USER_DATA_FORMAT_UTF8, "AlbumArtist         : %s \n"},
        {USER_DATA_COMPILATION, USER_DATA_FORMAT_INT_LE, "Compilation         : %s \n"},
        {USER_DATA_PROGRAMINFO, USER_DATA_FORMAT_PROGRAM_INFO, "Program Info : %s\n"},
        {USER_DATA_PMT, USER_DATA_FORMAT_PMT_INFO, "PMT : %s\n"},
        {USER_DATA_FORMATVERSION, USER_DATA_FORMAT_UTF8, "Container version   : %s\n"},
        {USER_DATA_ANDROID_VERSION, USER_DATA_FORMAT_UTF8, "android version: %s \n"},
        {USER_DATA_CAPTURE_FPS, USER_DATA_FORMAT_FLOAT32_BE, "fps       : %f \n"},

        {-1, -1, NULL}};

#ifdef TIME_PROFILE
typedef struct {
    uint64 seekTimeUs;
    uint32 seekCount;
    uint64 playTimeUs;
    uint32 playCount;
    uint64 createTimeUs;
    uint64 indexLoadTimeUs;
    uint64 fileOpenTimeUs;
    uint64 fileReadTimeUs;
} G_TIME_INFO;

G_TIME_INFO g_timeprofile_info;

void reset_GTimeInfo() {
    memset(&g_timeprofile_info, 0, sizeof(G_TIME_INFO));
}

void print_GTimeInfo() {
    printf("Seek Time :%llu\n", g_timeprofile_info.seekTimeUs);
    printf("Seek Count: %lu\n", g_timeprofile_info.seekCount);
    printf("Play Time: %llu\n", g_timeprofile_info.playTimeUs);
    printf("Play Count:%lu\n", g_timeprofile_info.playCount);
    printf("Parser Create Time:%llu\n", g_timeprofile_info.createTimeUs);
    printf("Load Index Time :%llu\n", g_timeprofile_info.indexLoadTimeUs);
    printf("Source File Open Time:%llu\n", g_timeprofile_info.fileOpenTimeUs);
    printf("Source File Read Time:%llu\n", g_timeprofile_info.fileReadTimeUs);
}

#define CALC_DURATION_LINUX                                  \
    ((uint64)end_time.tv_sec * 1000000 + end_time.tv_usec) - \
            ((uint64)start_time.tv_sec * 1000000 + start_time.tv_usec)
#define CALC_DURATION_WINCE (((uint64)end_time - start_time) * 1000)
#endif

static void unicode2AsciiString(char* s_ascii, unsigned short* s_wchar, int length) {
    unsigned short temp;
    int count = 0;

    while (count < length) {
        temp = s_wchar[count];
        s_ascii[count] = (char)temp;
        count++;
    }
    s_ascii[count] = '\0';
}

static void displayUnicodeString(uint16* unicodeString, uint32 stringLength) {
    if (stringLength > MAX_USER_DATA_STRING_LENGTH)
        stringLength = MAX_USER_DATA_STRING_LENGTH;

    unicode2AsciiString(asciiString, unicodeString, stringLength);
    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "%s\n", asciiString);
}

static int64 getFileSize(FILE* fp) {
    if (fp) {
#ifdef SUPPORT_LARGE_FILE

#if defined(__WINCE) || defined(WIN32)
        WCHAR wFilePath[255];
        HANDLE hFile = NULL;
        uint32 fileSizeLow, fileSizeHigh;
        uint32 errno;
        int64 size;

        asciiToUnicodeString(g_testFilePath, wFilePath);
        hFile = CreateFile(wFilePath,  // TEXT("\\SDMemory\\hellboy_2-the_golden_army_h320.mov")
                           GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
        if (INVALID_HANDLE_VALUE == hFile) {
            errno = GetLastError(); /* System Error Codes, 2: file not found */
            PARSER_ERROR("CreateFile failed on %s, handle %d, err %d\n", g_testFilePath, hFile,
                         errno);
            return -1;
        }

        fileSizeLow = GetFileSize(hFile, &fileSizeHigh);

        PARSER_INFO(PARSER_INFO_FILE, "file size low %u, high %u\n", fileSizeLow, fileSizeHigh);
        size = fileSizeHigh;
        size <<= 32;
        size += fileSizeLow;

        if (size > HUGE_FILE_SIZE_THESHOLD) {
            PARSER_INFO(PARSER_INFO_FILE, "this is a huge file (> 2GB)\n");
            g_hugeFileHandle = hFile; /* not close it, will use it for later parsing */
        } else {
            PARSER_INFO(PARSER_INFO_FILE, "this is a normal size file\n");
            CloseHandle(hFile); /* Only use it to get size, use fp for later parsing  */
        }

        return size;

#else /* linux  */
        off_t pos;
        off_t size;

        pos = ftello(fp); /* back position */
        fseeko(fp, 0, SEEK_END);
        size = ftello(fp);
        fseeko(fp, pos, SEEK_SET); /*restore position */
        return (int64)size;

#endif

#else /* SUPPORT_LARGE_FILE */
        int pos;
        int32 size;

        pos = ftell(fp); /* back position */

        fseek(fp, 0, SEEK_END);
        size = ftell(fp); /* on WINCE, ftell() return -1 if current file position exceeds 2GB.*/
        fseek(fp, pos, SEEK_SET); /*restore position */
        return (int64)size;
#endif

    } else
        return 0;
}

#if defined(__WINCE) || defined(WIN32)
/* offset is the target absolute offset */
int seekHugeFileWINCE(int64 offset) {
    int32 ret;
    uint32 seekMethod;
    uint32 newFilePointer;

#if 0
    if( SEEK_CUR == whence)
        seekMethod = FILE_CURRENT;
    else if( SEEK_SET == whence)
        seekMethod = FILE_BEGIN;
    else /* SEEK_END */
        seekMethod = FILE_END;
#endif

    seekMethod = FILE_BEGIN;

    if (((0 <= offset) && (WINCE_MAX_POSITIVE_SEEK_OFFSET >= offset)) ||
        ((0 > offset) && (WINCE_MAX_NEGATIVE_SEEK_OFFSET <= (uint64)offset))) {
        int32 distanceToMoveLow;
        distanceToMoveLow = (int32)offset;

        newFilePointer = SetFilePointer(g_hugeFileHandle, distanceToMoveLow, NULL, seekMethod);
    } else {
        LARGE_INTEGER li;

        li.QuadPart = offset;

        newFilePointer = SetFilePointer(g_hugeFileHandle,
                                        li.LowPart,    // distanceToMoveLow,
                                        &li.HighPart,  //&distanceToMoveHigh,
                                        seekMethod);
    }

    if (INVALID_SET_FILE_POINTER == newFilePointer) {
        printf("huge file seek fail\n");
        ret = -1;
    } else
        ret = 0;

    return ret;
}
#endif /* __WINCE */

/* seek file to an absolute postion
0 for SUCCESS, -1 for failure */
int32 seekFile(FILE* fp, int64 offset) {
    int32 ret;

#ifdef SUPPORT_LARGE_FILE
#if defined(__WINCE) || defined(WIN32)
    if (NULL == g_hugeFileHandle) {
        /* file smaller than 2GB, fseek is enough */
        ret = fseek(fp, (int32)offset, SEEK_SET);

    } else
        ret = seekHugeFileWINCE(offset);

#else
    ret = fseeko(fp, (off_t)offset, SEEK_SET);
#endif

#else
    ret = fseek(fp, (int32)offset, SEEK_SET);
#endif

    return ret;
}

/* no longer used */
void simulateStreamingData() {
    if (TEST_STATE_PLAYING != g_test_state) {
        /* must simulate the streaming cache */
        if ((g_totalDataSizeAvailable + STREAMING_BYTE_RATE) < g_testFileSize) {
            g_totalDataSizeAvailable += STREAMING_BYTE_RATE;
        } else
            g_totalDataSizeAvailable = g_testFileSize;
    } else
        g_totalDataSizeAvailable = g_testFileSize;
}

/* no longer used */
void simulateStreamingDataBySeek(int64 curFileOffset, int64 targetFileOffset) {
    if (TEST_STATE_PLAYING != g_test_state) {
        /* must simulate the streaming cache */
        if ((g_totalDataSizeAvailable + STREAMING_BYTE_RATE) < g_testFileSize) {
            if (((targetFileOffset - curFileOffset) > STREAMING_FAR_SEEK_THRESHOLD) &&
                (targetFileOffset < g_testFileSize)) {
                PARSER_INFO(PARSER_INFO_FILE, "\nFar seek from %lld to %lld\n", curFileOffset,
                            targetFileOffset);
                g_totalDataSizeAvailable = targetFileOffset;
            }

            else
                g_totalDataSizeAvailable += STREAMING_BYTE_RATE;

        } else
            g_totalDataSizeAvailable = g_testFileSize;
    } else
        g_totalDataSizeAvailable = g_testFileSize;
}

// audio parser must use two file handle, one for normal process, one for duration scan/index table
// construction
#if defined(AUDIO_PARSER) || defined(ID3_PARSER_TEST)
FslFileHandle appLocalFileOpen(const uint8* file_path, const uint8* mode, void* context) {
    FILE* fp = NULL;

    fp = fopen(g_testFilePath, mode);

    if (fp) {
        fseek(fp, 0, SEEK_END);
        g_testFileSize = ftell(fp);
        fseek(fp, 0, SEEK_SET);
    }

    return (FslFileHandle)fp;
}

uint32 appLocalReadFile(FslFileHandle file_handle, void* buffer, uint32 nb, void* context) {
    uint32 ret;
    FILE* fp = (FILE*)file_handle;

    ret = fread(buffer, 1, nb, fp);

    return ret;
}

int32 appLocalSeekFile(FslFileHandle file_handle, int64 offset, int32 whence, void* context) {
    FILE* fp = (FILE*)file_handle;

    fseek(fp, offset, whence);

    return 0;
}

int64 appLocalGetCurrentFilePos(FslFileHandle file_handle, void* context) {
    FILE* fp = (FILE*)file_handle;

    return ftell(fp);
}

int64 appLocalFileSize(FslFileHandle file_handle, void* context) {
    return g_testFileSize;
}

int32 appLocalFileClose(FslFileHandle file_handle, void* context) {
    FILE* fp = (FILE*)file_handle;

    fclose(fp);

    return 0;
}

int64 appLocalCheckAvailableBytes(FslFileHandle file_handle, int64 bytesRequested, void* context) {
    FILE* fp = (FILE*)file_handle;

    return g_testFileSize - ftell(fp);
}

#else

/*****************************************************************************
 * Function:    appLocalFileOpen
 *
 * Description: Implements to Open the file.
 *
 * Return:      File Pointer.
 ****************************************************************************/
FslFileHandle appLocalFileOpen(const char* file_path, const char* mode, void* context) {

#ifdef DBG_CHECK_APP_CONTEXT
    if ((NULL == context) || strcmp(appContext, (uint8*)context)) {
        PARSER_ERROR("appLocalFileOpen: Invalid context %p\n", context);
        return NULL;
    }
#else
    (void)context;
#endif
    (void)file_path;

    if ((NULL == g_testFilePath) || (NULL == mode)) {
        PARSER_ERROR("appLocalFileOpen: Invalid parameter\n");
        return NULL;
    }

    if (g_sourceFileHandle) /* already opened */
    {
        int32 result;

        g_sourceFileRefCount++;
#ifdef DBG_DISPLAY_FILE_REF_COUNT
        PARSER_INFO(PARSER_INFO_FILE,
                    "appLocalFileOpen: file already opened on fd %p, ref count increase to %d\n",
                    g_sourceFileHandle, g_sourceFileRefCount);
#endif

        result = seekFile((FILE*)g_sourceFileHandle, 0);
        if (result) {
            PARSER_ERROR("read fail when seeking to position %u\n", g_targetTestFileOffset);
            return NULL; /* seek fails, nothing can be read */
        }

        /* seek to the beginning, but cached data (g_totalDataSizeAvailable) not lost! */
    }

    else {
        FILE* fp = NULL;

        if (NULL == g_testFilePath) {
            PARSER_ERROR("appLocalFileOpen: Invalid file path\n");
            return NULL;
        }

        fp = fopen((char*)g_testFilePath, (char*)mode);
        if (fp) {
            PARSER_ERROR("appLocalFileOpen: file %s opened successfully on fp %p\n", g_testFilePath,
                         fp);
            g_sourceFileHandle = (FslFileHandle)fp;
            g_sourceFileRefCount = 1;
        } else {
            PARSER_ERROR("appLocalFileOpen: Fail to open file %s\n", g_testFilePath);
            return NULL;
        }

        g_testFileSize = getFileSize(fp);
        PARSER_INFO(PARSER_INFO_FILE, "Test file size %lld\n", g_testFileSize);

        g_totalDataSizeAvailable = g_testFileSize;
    }

    g_curTestFileOffset = 0;
    g_targetTestFileOffset = 0;

    return g_sourceFileHandle;
}

/*****************************************************************************
 * Function:    appLocalReadFile
 *
 * Description: Implements to read the stream from the file.
 *
 * Return:      Total number of bytes read.
 *
 ****************************************************************************/
uint32 appLocalReadFile(FslFileHandle file_handle, void* buffer, uint32 nb, void* context) {
    uint32 ret;
    FILE* fp = (FILE*)file_handle;

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    uint32 start_time;
    uint32 end_time;
#else
    struct timeval start_time;
    struct timeval end_time;
    struct timezone time_zone;
#endif
#endif

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    start_time = timeGetTime();
#else
    gettimeofday(&start_time, &time_zone);
#endif
#endif

#ifdef DBG_CHECK_APP_CONTEXT
    if ((NULL == context) || strcmp(appContext, (uint8*)context)) {
        PARSER_ERROR("appLocalReadFile: Invalid context %p\n", context);
        return 0;
    }
#else
    (void)context;
#endif

    if (nb == 1)

        if (g_targetTestFileOffset > g_totalDataSizeAvailable) {
            PARSER_ERROR(
                    "appLocalReadFile: exceeds available bytes, target offset %lld, total bytes "
                    "available %lld\n",
                    g_targetTestFileOffset, g_totalDataSizeAvailable);
            return 0;
        }

    if (g_curTestFileOffset != g_targetTestFileOffset) {
        int32 result;

        if (g_isLiveSource) {
            /*
            (1) After seeking, there may be serveral forward file seeking becuase each track has
            different file offset, which is reasonible. (2) But it's an ERROR to seek backward AFTER
            1st sample is got! There is one backward seeking at most upon user requested seeking !
            */
            checkBackwardFileSeek(g_curTestFileOffset, g_targetTestFileOffset, playState.playRate);
        }

        result = seekFile(fp, g_targetTestFileOffset);

        if (result) {
            PARSER_ERROR("read fail when seeking to position %u\n", g_targetTestFileOffset);
            goto bail; /* seek fails, nothing can be read */
        }
        g_curTestFileOffset = g_targetTestFileOffset;
    }

    if (!nb)
        return 0; /* nothing to read in fact. eg. AVI empty chunk */

#if defined(__WINCE) || defined(WIN32)
    if (g_hugeFileHandle) {
        bool boolRet;
        uint32 numberOfBytesRead;

        boolRet = ReadFile(g_hugeFileHandle, buffer, nb, &numberOfBytesRead, NULL);
        if (boolRet) {
            nb = numberOfBytesRead;
            g_curTestFileOffset += nb;
            g_targetTestFileOffset = g_curTestFileOffset;

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
            end_time = timeGetTime();
            g_timeprofile_info.fileReadTimeUs += CALC_DURATION_WINCE;
#else
            gettimeofday(&end_time, &time_zone);
            g_timeprofile_info.fileReadTimeUs += CALC_DURATION_LINUX;
#endif
#endif

            return nb;
        } else {
            PARSER_ERROR("read fail at position %lld, size to read %u\n", g_targetTestFileOffset,
                         nb);
            goto bail;
        }
    }

    else
#endif
    { /* under LINUX or normal file under WINCE */
        if (nb <= (g_testFileSize - g_curTestFileOffset))
            ret = fread(buffer, nb, 1, fp);
        else {
            PARSER_INFO(PARSER_INFO_FILE, "\nRead %u bytes near EOF,\n", nb);
            ret = fread(buffer, 1, nb, fp);
            if (0 < ret) {
                PARSER_INFO(PARSER_INFO_FILE, "Actully read %u bytes\n", ret);
                nb = ret;
            }
        }

        if (0 < ret) {
            g_curTestFileOffset += nb;
            g_targetTestFileOffset = g_curTestFileOffset;

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
            end_time = timeGetTime();
            g_timeprofile_info.fileReadTimeUs += CALC_DURATION_WINCE;
#else
            gettimeofday(&end_time, &time_zone);
            g_timeprofile_info.fileReadTimeUs += CALC_DURATION_LINUX;
#endif
#endif
            return nb; /* bytes read */
        } else {
            PARSER_ERROR("read fail at position %lld, size to read %u\n", g_targetTestFileOffset,
                         nb);
            goto bail; /* take as nothing read */
        }
    }

bail:
    logFileReadError();
    return 0;
}

/*****************************************************************************
 * Function:    appLocalSeekFile
 *
 * Description: seek the file. whence: SEEK_SET, SEEK_CUR, or SEEK_END
 *
 * Return:      0 on success and -1 on failure.
 *
 ****************************************************************************/

int32 appLocalSeekFile(FslFileHandle file_handle, int64 offset, int32 whence, void* context) {
    int64 targetTestFileOffset;

#ifdef DBG_CHECK_APP_CONTEXT
    if ((NULL == context) || strcmp(appContext, (uint8*)context)) {
        PARSER_ERROR("appLocalSeekFile: Invalid context %p\n", context);
        return -1;
    }
#else
    (void)context;
#endif
    (void)file_handle;

    static int64 preOffset;

    if (offset < preOffset) {
    }

    preOffset = offset;

    switch (whence) {
        case SEEK_SET: {
            targetTestFileOffset = offset;
            break;
        }

        case SEEK_CUR: {
            targetTestFileOffset = g_targetTestFileOffset + offset;
            break;
        }

        default:  // SEEK_END:
        {
            targetTestFileOffset = g_testFileSize + offset;
        }
    }

    if ((0 <= targetTestFileOffset) && (targetTestFileOffset <= g_totalDataSizeAvailable)) {
        g_targetTestFileOffset = targetTestFileOffset;
        return 0;
    } else {
        PARSER_ERROR("seek fail! target offset %lld, data size available %lld\n",
                     targetTestFileOffset, g_totalDataSizeAvailable);
        if ((0 > targetTestFileOffset) || (targetTestFileOffset > g_testFileSize)) {
            logFileSeekError(g_testFilePath, g_testFileSize, targetTestFileOffset);
        }
        return -1;
    }
}

/*****************************************************************************
 * Function:    appLocalGetCurrentFilePos
 *
 * Description: Gets file position.
 *
 * Return:      File Pointer.
 *
 ****************************************************************************/
int64 appLocalGetCurrentFilePos(FslFileHandle file_handle, void* context) {
#ifdef DBG_CHECK_APP_CONTEXT
    if ((NULL == context) || strcmp(appContext, (uint8*)context)) {
        PARSER_ERROR("appLocalGetCurrentFilePos: Invalid context %p\n", context);
        return -1;
    }
#else
    (void)context;
#endif
    (void)file_handle;
    return g_targetTestFileOffset; /* this the offset seen by the parser */
}

/*****************************************************************************
 * Function:    appLocalFileSize. For source file only
 *
 * Description: get the size of the file
 *
 * Return:      File Pointer.
 *
 ****************************************************************************/

int64 appLocalFileSize(FslFileHandle file_handle, void* context) {
#ifdef DBG_CHECK_APP_CONTEXT
    if ((NULL == context) || strcmp(appContext, (uint8*)context)) {
        PARSER_ERROR("appLocalFileSize: Invalid context %p\n", context);
        return -1;
    } else {
        PARSER_ERROR("appLocalFileSize: context is valid\n");
    }
#else
    (void)context;
#endif

    if ((NULL == file_handle) || (NULL == g_sourceFileHandle)) {
        PARSER_ERROR("appLocalFileSize: invalid file handle\n");
        return -1;
    }

    return g_testFileSize;  // 0; //debug
}

/*****************************************************************************
 * Function:    appLocalFileClose
 *
 * Description: Implements to close the file.
 *
 * Return:      Sucess or EOF.
 *
 ****************************************************************************/
int32 appLocalFileClose(FslFileHandle file_handle, void* context) {
    int32 err = 0;

    FILE* fp = (FILE*)file_handle;

#ifdef DBG_CHECK_APP_CONTEXT
    if ((NULL == context) || strcmp(appContext, (uint8*)context)) {
        PARSER_ERROR("appLocalFileClose: Invalid context %p\n", context);
        return -1;
    } else {
        PARSER_ERROR("appLocalFileClose: context is valid\n");
    }
#else
    (void)context;
#endif

    if (fp && g_sourceFileHandle) {
#if 1
        if (1 < g_sourceFileRefCount) {
            g_sourceFileRefCount--;
            return 0; /* not close the file until last reference is closed */
        }
#endif

#ifdef DBG_DISPLAY_FILE_REF_COUNT
        PARSER_INFO(PARSER_INFO_FILE, "appLocalFileClose: src file closed, fd %p\n", fp);
#endif

        g_sourceFileRefCount = 0;
        g_sourceFileHandle = NULL;

        fclose(fp);

#if defined(__WINCE) || defined(WIN32)
        if (g_hugeFileHandle) {
            CloseHandle(g_hugeFileHandle);
            g_hugeFileHandle = NULL;
        }
#endif
    } else {
        PARSER_ERROR("appLocalFileClose: fd %p already closed or invalid\n", fp);
        err = -1;
    }
    return err;
}

int64 appLocalCheckAvailableBytes(FslFileHandle file_handle, int64 bytesRequested, void* context) {
    int64 bytesAvailable;

#ifdef DBG_CHECK_APP_CONTEXT
    if ((NULL == context) || strcmp(appContext, (uint8*)context)) {
        PARSER_ERROR("appLocalCheckAvailableBytes: Invalid context %p\n", context);
        return -1;
    } else {
        PARSER_ERROR("appLocalCheckAvailableBytes: context is valid\n");
    }
#else
    (void)context;
#endif
    (void)file_handle;
    (void)bytesRequested;
    bytesAvailable = g_totalDataSizeAvailable - g_targetTestFileOffset;
    return bytesAvailable;
}

#endif

uint32 appLocalGetFlag(FslFileHandle file_handle, void* context) {
    uint32 flag = 0;
    if (g_isLiveSource) {
        flag |= FILE_FLAG_NON_SEEKABLE;
        flag |= FILE_FLAG_READ_IN_SEQUENCE;
    }

    (void)file_handle;
    (void)context;
    return flag;
}

uint8* appLocalRequestBuffer(uint32 streamNum, uint32* size, void** bufContext,
                             void* parserContext) {
    Track* track = &tracks[streamNum];
    uint8* buffer;
    uint32 sizeRequested, sizeGot;
    static uint32 dwCount;

#ifdef DBG_CHECK_APP_CONTEXT
    if ((NULL == parserContext) || strcmp(appContext, (uint8*)parserContext)) {
        PARSER_ERROR("appLocalFileOpen: Invalid context %p\n", parserContext);
        return NULL;
    }
#else
    (void)parserContext;
#endif

    if (track->isBSAC) {
        track = &tracks[track->firstBSACTrackNum]; /* redirect to 1st BSAC track */
    } else {
    }

    if (!track->hQueue) {
        PARSER_ERROR("ERR, invalid queue for trk %d\n", streamNum);
        return NULL;
    }

    dwCount++;
    sizeRequested = *size;  //(dwCount % 2 == 1) ? (10<<10) : *size; //10<<10; //
    if (!sizeRequested) {
        sizeRequested = 8;
        logError(RISK_REQUEST_ZERO_SIZE_OUTPUT_BUFFER);
    }

    buffer = getBuffer(track->hQueue, sizeRequested, &sizeGot, bufContext);

    *size = sizeGot;

    if (!buffer) {
        PARSER_ERROR(
                "ERR, trk %d, decoder buffer full & dead lock! Requested size %u bytes, at rate "
                "%uX, segment start time %lld us, reference clock %lld us.\n",
                streamNum, sizeRequested, playState.playRate, playState.usMovieSegmentStartTime,
                g_usReferenceClock);
        logQueueOverflow(streamNum, playState.playRate, playState.usMovieSegmentStartTime);

        if (sizeRequested > MAX_OUTPUT_BUFFER_SIZE)
            logHugeOutputBufferRequest(streamNum, sizeRequested, playState.playRate,
                                       playState.usMovieSegmentStartTime);
    }

    return buffer;
}

void appLocalReleaseBuffer(uint32 streamNum, uint8* pBuffer, void* bufContext,
                           void* parserContext) {
    Track* track = &tracks[streamNum];

#ifdef DBG_CHECK_APP_CONTEXT
    if ((NULL == parserContext) || strcmp(appContext, (uint8*)parserContext)) {
        PARSER_ERROR("appLocalFileOpen: Invalid context %p\n", parserContext);
        return NULL;
    }
#else
    (void)parserContext;
#endif

    if (track->isBSAC) {
        track = &tracks[track->firstBSACTrackNum]; /* redirect to 1st BSAC track */
    }

    if (track->hQueue)
        unrefBuffer(track->hQueue, pBuffer, bufContext);

    return;
}

static void displayVideoCodecType(uint32 decoderType, uint32 decoderSubtype) {
    PARSER_INFO(PARSER_INFO_STREAM, "\t ");
    switch (decoderType) /* maybe shall use fccCompression */
    {
        case VIDEO_MPEG4:
            switch (decoderSubtype) {
                case MPEG4_VIDEO_AS_PROFILE:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "MPEG-4 AS profile video\n");
                    break;

                default:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "MPEG-4 video\n");
                    break;
            }
            break;

        case VIDEO_MPEG2:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Mpeg-2 video\n");
            break;

        case VIDEO_H263:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "H.263 video\n");
            break;

        case VIDEO_H264:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "H.264 video\n");
            break;
        case VIDEO_HEVC:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "HEVC video\n");
            break;

        case VIDEO_JPEG:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "JPEG still image\n");
            break;

        case VIDEO_MJPG:
            switch (decoderSubtype) {
                case VIDEO_MJPEG_FORMAT_A:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Motion JPEG format A\n");
                    break;

                case VIDEO_MJPEG_FORMAT_B:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Motion JPEG format B\n");
                    break;

                case VIDEO_MJPEG_2000:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Motion JPEG 2000\n");
                    break;
                default:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Motion JPEG video\n");
            }
            break;

        case VIDEO_XVID:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "XVID video\n");
            break;

        case VIDEO_DIVX:
            switch (decoderSubtype) {
                case VIDEO_DIVX3:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "DIVX video version 3\n");
                    break;

                case VIDEO_DIVX4:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "DIVX video version 4\n");
                    break;

                case VIDEO_DIVX5_6:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "DIVX video version 5/6\n");
                    break;

                default:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "DivX video, unknown version\n");
            }
            break;

        case VIDEO_MS_MPEG4:
            switch (decoderSubtype) {
                case VIDEO_MS_MPEG4_V2:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Microsoft MPEG-4 video V2\n");
                    break;

                case VIDEO_MS_MPEG4_V3:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Microsoft MPEG-4 video V3\n");
                    break;

                default:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                                     "Microsoft MPEG-4 video, unknown version\n");
            }
            break;

        case VIDEO_WMV:
            switch (decoderSubtype) {
                case VIDEO_WMV7:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "WMV7 video\n");
                    break;

                case VIDEO_WMV8:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "WMV8 video\n");
                    break;

                case VIDEO_WMV9:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "WMV9 video\n");
                    break;

                case VIDEO_WMV9A:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "WMV9 Advanced Profile video\n");
                    break;

                case VIDEO_WVC1:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "VC-1 video\n");
                    break;

                default:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "WMV video, unknown version\n");
            }
            break;

        case VIDEO_REAL:
            switch (decoderSubtype) {
                case REAL_VIDEO_RV10:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "REAL VIDEO RV10\n");
                    break;
                case REAL_VIDEO_RV20:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "REAL VIDEO RV20\n");
                    break;
                case REAL_VIDEO_RV30:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "REAL VIDEO RV30\n");
                    break;
                case REAL_VIDEO_RV40:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "REAL VIDEO RV40\n");
                    break;
                default:
                    break;
            }
            break;

        case VIDEO_SORENSON:
            switch (decoderSubtype) {
                case VIDEO_SORENSON_SPARK:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Sorenson Spark\n");
                    break;

                case VIDEO_SVQ1:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "SVQ1 - Sorenson video type 1\n");
                    break;

                case VIDEO_SVQ3:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "SVQ3 - Sorenson video type 3\n");
                    break;

                default:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Sorenson video, unknown version\n");
            }
            break;

        default:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Unknown video codec type\n ");
    }
    return;
}

static void displayAudioCodecType(uint32 decoderType, uint32 decoderSubtype) {
    PARSER_INFO(PARSER_INFO_STREAM, "\t ");
    switch (decoderType) /* maybe shall use fccCompression */
    {
        case AUDIO_MP3:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "MP3 audio\n");
            break;

        case AUDIO_AAC:
            if (AUDIO_ER_BSAC == decoderSubtype)
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "BSAC audio\n");
            else
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "AAC audio\n");
            break;

        case AUDIO_MPEG2_AAC:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "MPEG-2 AAC audio\n");
            break;

        case AUDIO_AC3:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "AC3 audio\n");
            break;

        case AUDIO_WMA:
            switch (decoderSubtype) {
                case AUDIO_WMA1:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "WMA1 audio\n");
                    break;

                case AUDIO_WMA2:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "WMA2 audio\n");
                    break;

                case AUDIO_WMA3:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "WMA3 audio\n");
                    break;

                default:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "WMA audio (version unknown)\n");
                    break;
            }
            break;

        case AUDIO_AMR:
            switch (decoderSubtype) {
                case AUDIO_AMR_NB:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "AMR-NB\n");
                    break;

                case AUDIO_AMR_WB:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "AMR-WB\n");
                    break;

                case AUDIO_AMR_WB_PLUS:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "AMR-WB+\n");
                    break;
            }
            break;

        case AUDIO_PCM:
            switch (decoderSubtype) {
                case AUDIO_PCM_U8:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "PCM, unsigned, 8 bits per sample\n");
                    break;

                case AUDIO_PCM_S16LE:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                                     "PCM, signed little-endian, 16 bits per sample\n");
                    break;

                case AUDIO_PCM_S24LE:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                                     "PCM, signed little-endian, 24 bits per sample\n");
                    break;

                case AUDIO_PCM_S32LE:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                                     "PCM, signed little-endian, 32 bits per sample\n");
                    break;

                case AUDIO_PCM_S16BE:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                                     "PCM, signed big-endian, 16 bits per sample\n");
                    break;

                case AUDIO_PCM_S24BE:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                                     "PCM, signed big-endian, 24 bits per sample\n");
                    break;

                case AUDIO_PCM_S32BE:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                                     "PCM, signed big-endian, 32 bits per sample\n");
                    break;
            }
            break;

        case AUDIO_ADPCM:
            switch (decoderSubtype) {
                case AUDIO_IMA_ADPCM:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "ADPCM, IMA 4:1\n");
                    break;

                case AUDIO_ADPCM_MS:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Microsoft ADPCM\n");
                    break;

                default:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "ADPCM\n");
                    break;
            }
            break;

        case AUDIO_PCM_MULAW:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "PCM, MuLaw\n");
            break;

        case AUDIO_REAL:
            switch (decoderSubtype) {
                case REAL_AUDIO_SIPR:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "REAL Audio SIPR.\n");
                    break;
                case REAL_AUDIO_COOK:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "REAL Audio COOK.\n");
                    break;
                case REAL_AUDIO_ATRC:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "REAL Audio ATRC.\n");
                    break;
                case REAL_AUDIO_RAAC:
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "REAL Audio RAAC.\n");
                    break;
                default:
                    break;
            }
            break;

        case AUDIO_VORBIS:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Vorbis Audio\n");
            break;

        default:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Unknown audio codec type\n ");
    }
    return;
}

static void displayTextCodecType(uint32 decoderType, uint32 decoderSubtype) {
    switch (decoderType) /* maybe shall use fccCompression */
    {
        case TXT_3GP_STREAMING_TEXT:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "3GP Streaming Text\n");
            break;

        case TXT_DIVX_FEATURE_SUBTITLE:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "DivX Feature Subtitle\n");
            break;

        case TXT_DIVX_MENU_SUBTITLE:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "DivX Menu Subtitle\n");
            break;

        case TXT_QT_TEXT:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "QuickTime Text\n");
            break;

        default:
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Unknown text codec type\n ");
    }
    (void)decoderSubtype;
    return;
}

static void dumpRawSampleData(FILE* fp, uint8* sampleBuffer, uint32 sampleSize) {
    fwrite(sampleBuffer, sampleSize, 1, fp);
}

static void dumpAACFrame(FILE* fp, uint8* sampleBuffer, uint32 sampleSize,
                         sAACConfigPtr aacConfig) {
    uint8 ADTSHeader[ADTS_HEADER_SIZE];
    aacConfig->frame_length = sampleSize + ADTS_HEADER_SIZE;
    PutAdtsHeader(aacConfig, ADTSHeader);
    fwrite(ADTSHeader, ADTS_HEADER_SIZE, 1, fp);
    fwrite(sampleBuffer, sampleSize, 1, fp);
    return;
}

static int32 dumpH264Frame(FILE* fp, uint8* sampleBuffer, uint32 sampleSize,
                           uint32 NALLengthFieldSize, uint8* NALWorkingBuffer) {
    uint8* srcBuffer = sampleBuffer;
    uint32 srcDataSize = sampleSize;
    uint8* destBuffer = NALWorkingBuffer;
    uint32 destDataSize = 0;

    uint32 i = 0; /* byte index in source buffer */
    uint32 j = 0; /* byte index in destination buffer */
    uint32 k;     /* byte index to read NAL length field */
    uint32 nal_length = 0;
    uint32 nal_length_size = NALLengthFieldSize;
    uint32 extra_bytes = 0; /* start code size - nal length size */
    uint32 frame_length = srcDataSize;
    uint32 bytes_not_read = srcDataSize; /* data size */
    uint8* NalSrcBuffer = srcBuffer;

    if ((0 == nal_length_size) || ((0 == sampleBuffer[0]) && (0 == sampleBuffer[1]) &&
                                   (0 == sampleBuffer[2]) && (1 == sampleBuffer[3]))) {
        dumpRawSampleData(fp, sampleBuffer, sampleSize);
        return PARSER_SUCCESS;
    }

    for (j = 0; j < frame_length;) {
        /* For H.264 track in MP4 file, the frame data are separated in NALs and there are
        the NAL length field to give the length of each NAL length data.
        Usually, the NAL length field itself is 4 bytes long and can be replaced by NAL start code
        "00000001" for decoding easily. But in fact, the AVC spec 14496-15 allow this field to be 16
        bytes long at most, the proper value is in the decoder specific information.
        */

        if (bytes_not_read <= nal_length_size)
            break;

        nal_length = 0;
        for (k = 0; k < nal_length_size; k++) {
            nal_length = (nal_length << 8) + NalSrcBuffer[i + k];
        }

        bytes_not_read -= nal_length_size;

        destBuffer[j] = 0x0;
        destBuffer[j + 1] = 0x0;
        destBuffer[j + 2] = 0x0;
        destBuffer[j + 3] = 0x1;

        if (nal_length <= bytes_not_read) {
            {
                memmove((&destBuffer[j + 4]), (&NalSrcBuffer[i + nal_length_size]), nal_length);
                extra_bytes +=
                        (4 - nal_length_size); /* no need to update if nal_length_size is 4 */
            }

            i += (nal_length + nal_length_size);
            j += (nal_length + 4);
            bytes_not_read -= nal_length;
        } else {
            PARSER_ERROR("Err! Invaid NAL length found. Frame size %u, size not parsed %u\n",
                         sampleSize, bytes_not_read);
            return PARSER_ERR_INVALID_MEDIA;
        }
    }

    destDataSize = srcDataSize +
                   extra_bytes; /* update sample data size because NAL start code is inserted */
    fwrite(destBuffer, destDataSize, 1, fp);
    return PARSER_SUCCESS;
}

static int32 dumpOneSampleData(Track* track) {
    int32 err = PARSER_SUCCESS;

    if (g_dump_track_data && track->fp_track_dump) {
        if (track->isAAC && !track->isBSAC) { /* dump ADTS header & whole sample */
            dumpAACFrame(track->fp_track_dump, track->sampleBuffer, track->sampleSize,
                         &track->aacConfig);
        } else if (track->isH264Video) {
            err = dumpH264Frame(track->fp_track_dump, track->sampleBuffer, track->sampleSize,
                                track->NALLengthFieldSize, track->NALWorkingBuffer);
        } else
            dumpRawSampleData(track->fp_track_dump, track->sampleBuffer, track->sampleSize);
    }

    if (err)
        PARSER_ERROR("fail to dump track data\n");
    return err;
}

static void MakeIndexDumpFileName(char* media_file_name) {
    char* p = strrchr(media_file_name, '.');
    int suffix_len;
    int len_org;
    size_t cp_len;

    if (p == NULL) {
        return;
    }
    suffix_len = strlen(p);
    len_org = strlen(media_file_name);
    cp_len = len_org - suffix_len;
    if(cp_len > 254)
        cp_len = 254;
    memcpy(index_file_name, media_file_name, cp_len);
    index_file_name[cp_len] = '\0';
    strcat(index_file_name, ".idx");
    PARSER_INFO(PARSER_INFO_FILE, "%s, index file name %s.\n", __FUNCTION__, index_file_name);
    return;
}

static int32 openTrackDataDumpFile(char* media_file_name, Track* track, TEST_SEQUENCE sequence) {
    char* p = strrchr(media_file_name, '.');
    size_t suffix_len;
    size_t len_org;
    size_t cp_len;
    char tmp[8];

    if (p == NULL) {
        return PARSER_ERR_UNKNOWN;
    }

    suffix_len = strlen(p);
    len_org = strlen(media_file_name);
    cp_len = len_org - suffix_len;
    if(cp_len > 254)
        cp_len = 254;
    memcpy(track->dump_file_path, media_file_name, cp_len);
    track->dump_file_path[cp_len] = '\0';

    sprintf(tmp, "_track%d", track->trackNum);
    strcat(track->dump_file_path, tmp);
    if (MEDIA_VIDEO == track->mediaType) {
        switch (track->decoderType) {
            case VIDEO_DIVX:
                strcat(track->dump_file_path, ".divx");
                break;

            case VIDEO_MPEG4:
            case VIDEO_MS_MPEG4:
                strcat(track->dump_file_path, ".m4v");
                break;

            case VIDEO_H263:
                strcat(track->dump_file_path, ".h263");
                break;

            case VIDEO_H264:
                strcat(track->dump_file_path, ".h264");
                break;
            case VIDEO_HEVC:
                strcat(track->dump_file_path, ".hevc");
                break;

            case VIDEO_MJPG:
                strcat(track->dump_file_path, ".mjpg");
                break;

            case VIDEO_XVID:
                strcat(track->dump_file_path, ".xvid");
                break;

            default:
                strcat(track->dump_file_path, ".data");
        }
    } else if (MEDIA_AUDIO == track->mediaType) {
        switch (track->decoderType) /* maybe shall use fccCompression */
        {
            case AUDIO_MP3:
                strcat(track->dump_file_path, ".mp3");
                break;

            case AUDIO_AAC:
            case AUDIO_MPEG2_AAC:
                strcat(track->dump_file_path, ".aac");
                break;

            case AUDIO_AC3:
                strcat(track->dump_file_path, ".ac3");
                break;

            case AUDIO_AMR:
                strcat(track->dump_file_path, ".amr");
                break;

            case AUDIO_PCM:
                strcat(track->dump_file_path, ".pcm");
                break;

            case AUDIO_ADPCM:
                strcat(track->dump_file_path, ".adpcm");
                break;

            case AUDIO_VORBIS:
                strcat(track->dump_file_path, ".vorbis");
                break;

            default:
                strcat(track->dump_file_path, ".data");
        }
    } else
        strcat(track->dump_file_path, ".data");

    printf("track dump file name %s\n", track->dump_file_path);

    if (g_keepData == TRUE) {
        char* pBaseName = strrchr(track->dump_file_path, '/');
        if (pBaseName == NULL) {
            PARSER_INFO(PARSER_INFO_FILE, "can not find / in %s\n", track->dump_file_path);
            return PARSER_ERR_UNKNOWN;
        }

        char BaseName[MAX_FILE_PATH_LENGTH] = {0};
        if (sequence == TEST_SEQUENCE_A)
            strcpy(BaseName, g_parser_lib_path_a);
        else
            strcpy(BaseName, g_parser_lib_path_b);
        strcat(BaseName, pBaseName);
        PARSER_INFO(PARSER_INFO_FILE, "local dump file %s\n", BaseName);
        track->fp_track_dump = fopen((const char*)BaseName, (const char*)"w+b");
        if (NULL == track->fp_track_dump) {
            return PARSER_FILE_OPEN_ERROR;
        }
        strcpy(track->dump_file_path, BaseName);
    } else {
        track->fp_track_dump = fopen((const char*)track->dump_file_path, (const char*)"w+b");
        if (NULL == track->fp_track_dump) {
            PARSER_INFO(PARSER_INFO_FILE,
                        "Fail to open file %s to dump track %d data, try working directory...\n",
                        track->dump_file_path, track->trackNum);
            char* pBaseName = strrchr(track->dump_file_path, '/');
            if (pBaseName == NULL) {
                PARSER_INFO(PARSER_INFO_FILE, "can not find / in %s\n", track->dump_file_path);
                return PARSER_ERR_UNKNOWN;
            }

            char BaseName[MAX_FILE_PATH_LENGTH] = {0};
            strcpy(BaseName, ".");
            strcat(BaseName, pBaseName);
            PARSER_INFO(PARSER_INFO_FILE, "local dump file %s\n", BaseName);
            track->fp_track_dump = fopen((const char*)BaseName, (const char*)"w+b");
            if (NULL == track->fp_track_dump) {
                return PARSER_FILE_OPEN_ERROR;
            }
            strcpy(track->dump_file_path, BaseName);
        }
    }
    return PARSER_SUCCESS;
}

static int32 OpenTrackPtsDumpFile(char* media_file_name, Track* track, TEST_SEQUENCE sequence) {
    char* p = strrchr(media_file_name, '.');
    size_t suffix_len;
    size_t len_org;
    size_t cp_len;
    char tmp[256];

    if (p == NULL) {
        return PARSER_ERR_UNKNOWN;
    }

    suffix_len = strlen(p);
    len_org = strlen(media_file_name);
    cp_len = len_org - suffix_len;
    if(cp_len > 254)
        cp_len = 254;
    memcpy(track->pts_dump_file_path, media_file_name, cp_len);
    track->pts_dump_file_path[cp_len] = '\0';

    sprintf(tmp, "_track%d.pts", track->trackNum);
    strcat(track->pts_dump_file_path, tmp);

    PARSER_INFO(PARSER_INFO_PTS, "track pts dump file name %s\n", track->pts_dump_file_path);

    if (g_keepData == TRUE) {
        char* pBaseName = strrchr(track->pts_dump_file_path, '/');
        if (pBaseName == NULL) {
            PARSER_INFO(PARSER_INFO_FILE, "can not find / in %s\n", track->pts_dump_file_path);
            return PARSER_ERR_UNKNOWN;
        }

        char BaseName[MAX_FILE_PATH_LENGTH] = {0};
        if (sequence == TEST_SEQUENCE_A)
            strcpy(BaseName, g_parser_lib_path_a);
        else
            strcpy(BaseName, g_parser_lib_path_b);
        strcat(BaseName, pBaseName);
        PARSER_INFO(PARSER_INFO_FILE, "local dump file %s\n", BaseName);
        track->fp_track_pts_dump = fopen((const char*)BaseName, (const char*)"w+b");
        if (NULL == track->fp_track_pts_dump) {
            return PARSER_FILE_OPEN_ERROR;
        }
        strcpy(track->pts_dump_file_path, BaseName);
    } else {
        track->fp_track_pts_dump = fopen((const char*)track->pts_dump_file_path, (const char*)"w");
        if (NULL == track->fp_track_pts_dump) {
            PARSER_INFO(PARSER_INFO_FILE,
                        "Fail to open file %s to dump track %d pts, try working directory...\n",
                        track->pts_dump_file_path, track->trackNum);
            char* pBaseName = strrchr(track->pts_dump_file_path, '/');
            if (pBaseName == NULL)
                return PARSER_ERR_UNKNOWN;

            char BaseName[MAX_FILE_PATH_LENGTH] = {0};
            strcpy(BaseName, ".");
            strcat(BaseName, pBaseName);
            PARSER_INFO(PARSER_INFO_FILE, "local dump file %s\n", BaseName);
            track->fp_track_pts_dump = fopen((const char*)BaseName, (const char*)"w+b");
            if (NULL == track->fp_track_pts_dump) {
                return PARSER_FILE_OPEN_ERROR;
            }
            strcpy(track->pts_dump_file_path, BaseName);
        }
    }
    PARSER_INFO(PARSER_INFO_FILE, "pts dump file opened on fp %p , path %s\n",
                track->fp_track_pts_dump, track->pts_dump_file_path);
    return PARSER_SUCCESS;
}

static int32 allocateTrackBuffers(Track* track) {
    int32 err = PARSER_SUCCESS;

    // uint8 * buffer = NULL; /* buffer to read samples */
    // uint32 smallBufferSize = 2; /* not the real buffer size, test buffer size check */

    if (MEDIA_VIDEO == track->mediaType) {
#if 1
        if (!track->queueSettings.maxSizeBytes && !track->queueSettings.maxSizeBuffers)
            track->queueSettings.maxSizeBytes = DEFAULT_VIDEO_QUEUE_DEPTH;
#else
        track->queueSettings.maxSizeBuffers = 10000;
        track->queueSettings.singleBufferSize = 500;
#endif
    } else if (MEDIA_AUDIO == track->mediaType) {
#if 1
        if (!track->queueSettings.maxSizeBytes && !track->queueSettings.maxSizeBuffers) {
            if ((AUDIO_TYPE_UNKNOWN == track->decoderType) || (AUDIO_PCM == track->decoderType) ||
                (AUDIO_ADPCM == track->decoderType) || (AUDIO_DTS == track->decoderType))
                track->queueSettings.maxSizeBytes = DEFAULT_AUDIO_DEEP_QUEUE_DEPTH;
            else
                track->queueSettings.maxSizeBytes = DEFAULT_AUDIO_QUEUE_DEPTH;
        }
#else
        track->queueSettings.maxSizeBuffers = 5000;
        track->queueSettings.singleBufferSize = 200;
#endif
    } else if (MEDIA_TEXT == track->mediaType) {
        if (!track->queueSettings.maxSizeBytes && !track->queueSettings.maxSizeBuffers)
            track->queueSettings.maxSizeBytes = DEFAULT_TEXT_QUEUE_DEPTH;
    }

    if (!track->queueSettings.maxSizeBytes && !track->queueSettings.maxSizeBuffers)
        goto bail; /* not a media track, nothing to do */

    if (track->queueSettings.maxSizeBytes)
        PARSER_INFO(PARSER_INFO_BUFFER, "Trk %d, max queue depth: %d bytes (GStreamer style)\n",
                    track->trackNum, track->queueSettings.maxSizeBytes);
    else
        PARSER_INFO(
                PARSER_INFO_BUFFER,
                "Trk %d, max queue depth: %d buffers, single buffer size %d bytes (DShow style)\n",
                track->trackNum, track->queueSettings.maxSizeBuffers,
                track->queueSettings.singleBufferSize);
    track->hQueue =
            createQueue(track->queueSettings.maxSizeBytes, track->queueSettings.maxSizeBuffers,
                        track->queueSettings.singleBufferSize);
    if (NULL == track->hQueue) {
        PARSER_ERROR("trk %d, fail to create queue\n", track->trackNum);
        err = PARSER_INSUFFICIENT_MEMORY;
        goto bail;
    }

    /* allocate sample buffer */
    if (MEDIA_VIDEO == track->mediaType)
        track->sampleBufferSize = MAX_VIDEO_FRAME_SIZE;
    else
        track->sampleBufferSize = MAX_NON_VIDEO_SAMPLE_SIZE;

    if (track->sampleBufferSize) {
        PARSER_INFO(PARSER_INFO_DATASIZE, "Trk %d, sample buffer size: %d bytes\n", track->trackNum,
                    track->sampleBufferSize);

        track->sampleBuffer = (uint8*)fsl_osal_malloc(track->sampleBufferSize);
        if (NULL == track->sampleBuffer) {
            err = PARSER_INSUFFICIENT_MEMORY;
            goto bail;
        }
    }

    if (track->isH264Video) {
        track->NALWorkingBufferSize = track->sampleBufferSize;  // + H264_NAL_OVERHEADER_SIZE;
        PARSER_INFO(PARSER_INFO_DATASIZE, "Trk %d, NAL buffer size: %d bytes\n", track->trackNum,
                    track->NALWorkingBufferSize);
        track->NALWorkingBuffer = (uint8*)fsl_osal_malloc(track->NALWorkingBufferSize);
        if (NULL == track->NALWorkingBuffer) {
            err = PARSER_INSUFFICIENT_MEMORY;
            goto bail;
        }
    }

bail:
    return err;
}

static void freeTrackBuffers(Track* track) {
    if (track->hQueue) {
        deleteQueue(track->hQueue);
        track->hQueue = NULL;
    }

    if (track->sampleBuffer) {
        fsl_osal_free(track->sampleBuffer);
        track->sampleBuffer = NULL;
    }

    if (track->NALWorkingBuffer) {
        fsl_osal_free(track->NALWorkingBuffer);
        track->NALWorkingBuffer = NULL;
    }

    return;
}

static FslFileHandle OpenSourceFile(char* fileName) {
    FslFileHandle sourceFileHandle = NULL;

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    uint32 start_time;
    uint32 end_time;
#else
    struct timeval start_time;
    struct timeval end_time;
    struct timezone time_zone;
#endif
#endif

    g_testFilePath = fileName;

    /* open the test clip, exclude this time in parser loading time. */
#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    start_time = timeGetTime();
#else
    gettimeofday(&start_time, &time_zone);
#endif
#endif
    sourceFileHandle = appLocalFileOpen(fileName, "rb", appContext);
    if (NULL == sourceFileHandle) {
        PARSER_ERROR("Fail to open the test clip %s\n", g_testFilePath);
        goto bail;
    }

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    end_time = timeGetTime();
    g_timeprofile_info.fileOpenTimeUs = CALC_DURATION_WINCE;
#else
    gettimeofday(&end_time, &time_zone);
    g_timeprofile_info.fileOpenTimeUs = CALC_DURATION_LINUX;
#endif
#endif

    PARSER_INFO(PARSER_INFO_FILE, "\n");

bail:
    return sourceFileHandle;
}

static int32 GetMetaData(FslParserInterface* IParser, FslParserHandle parserHandle) {
    int32 err = PARSER_SUCCESS;
    uint8* userData;
    uint32 userDataSize;
    uint32 i = 0;
    UserDataID id;
    UserDataFormat format;

    printf("\n************************************************************\n");
    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "     FILE META DATA\n");

    while (g_userDataTable[i].id != (uint32)-1) {
        id = g_userDataTable[i].id;
        format = g_userDataTable[i].format;
        err = IParser->getMetaData(parserHandle, id, &format, &userData, &userDataSize);
        /*   if(err)
           {
               i++;
               continue;
           }
   */
        if ((userData != NULL) && (userDataSize > 0)) {
            if (USER_DATA_FORMAT_UTF8 == format) {
                char* string = (char*)malloc(userDataSize + 1);
                if (string) {
                    memcpy(string, userData, userDataSize);
                    string[userDataSize] = '\0';
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, g_userDataTable[i].printString, string);
                    free(string);
                }
            } else if ((USER_DATA_FORMAT_JPEG == format) || (USER_DATA_FORMAT_PNG == format) ||
                       (USER_DATA_FORMAT_BMP == format) || (USER_DATA_FORMAT_GIF == format)) {
                FILE* fp = NULL;
                char* filename = "image.jpg";
                char* imagetype = "JPEG";
                if (USER_DATA_FORMAT_PNG == format) {
                    filename = "image.png";
                    imagetype = "PNG";
                } else if (USER_DATA_FORMAT_BMP == format) {
                    filename = "image.bmp";
                    imagetype = "BMP";
                } else if (USER_DATA_FORMAT_GIF == format) {
                    filename = "image.gif";
                    imagetype = "GIF";
                }

                fp = fopen(filename, "wb");
                if (NULL != fp) {
                    fwrite(userData, userDataSize, 1, fp);
                    fclose(fp);
                }

                printf(g_userDataTable[i].printString, imagetype, userDataSize);
            } else if (USER_DATA_FORMAT_PROGRAM_INFO == format) {
                uint32 dwIdx;
                ProgramInfoMenu* ptProInfoMenu = (ProgramInfoMenu*)userData;
                if (ptProInfoMenu->m_dwProgramNum * sizeof(ProgramInfo) + sizeof(ProgramInfoMenu) !=
                    userDataSize) {
                    printf("USER_DATA_FORMAT_PROGRAM_INFO, size err\n");
                    break;
                }

                PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                                 "USER_DATA_FORMAT_PROGRAM_INFO: program num %d\n",
                                 ptProInfoMenu->m_dwProgramNum);
                for (dwIdx = 0; dwIdx < ptProInfoMenu->m_dwProgramNum; dwIdx++) {
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "channel %d, PID 0x%x\n",
                                     ptProInfoMenu->m_atProgramInfo[dwIdx].m_dwChannel,
                                     ptProInfoMenu->m_atProgramInfo[dwIdx].m_dwPID);
                }
            } else if (USER_DATA_FORMAT_PMT_INFO == format) {
                uint32 dwProgramIdx = 0;
                uint32 dwTrackIdx = 0;
                PMTInfoList* ptPMTList = (PMTInfoList*)userData;
                PMTInfo* ptPMT = NULL;
                TrackInfo* ptTrack = NULL;

                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nPMT\n");

                for (dwProgramIdx = 0; dwProgramIdx < ptPMTList->m_dwProgramNum; dwProgramIdx++) {
                    ptPMT = &ptPMTList->m_ptPMTInfo[dwProgramIdx];
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "PID 0x%x, channel %d, tracknum %d\n",
                                     ptPMT->m_dwPID, ptPMT->m_dwChannel, ptPMT->m_dwTrackNum);

                    for (dwTrackIdx = 0; dwTrackIdx < ptPMT->m_dwTrackNum; dwTrackIdx++) {
                        ptTrack = &ptPMT->m_ptTrackInfo[dwTrackIdx];
                        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "    track PID 0x%x, No %d\n",
                                         ptTrack->m_dwPID, ptTrack->m_dwTrackNo);
                    }
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\n");
                }
            } else if (USER_DATA_FORMAT_FLOAT32_BE == format) {
                uint32 value = 0;
                float data;
                value += *userData << 24;
                value += *(userData + 1) << 16;
                value += *(userData + 2) << 8;
                value += *(userData + 3);
                data = (float)value;
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, g_userDataTable[i].printString, data);
            } else if ((enum FSL_PARSER_USER_DATA_FORMAT) - 1 == format) {
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "size %d, 0x%x, 0x%x, 0x%x, 0x%x\n",
                                 userDataSize, userData[0], userData[1], userData[2], userData[3]);
            }
        }
        i++;
    }

    // bail:
    printf("\n************************************************************\n");
    return err;
}

static int32 GetUserData(FslParserInterface* IParser, FslParserHandle parserHandle) {
    int32 err = PARSER_SUCCESS;
    uint16* userData;
    uint32 userDataSize;

    PARSER_INFO(PARSER_INFO_STREAM,
                "\n************************************************************\n");
    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "     FILE USER DATA\n");

    err = IParser->getUserData(parserHandle, USER_DATA_TITLE, &userData, &userDataSize);
    if ((PARSER_SUCCESS == err) && userData && userDataSize) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nTitle Name = ");
        displayUnicodeString(userData, userDataSize);
    }

    err = IParser->getUserData(parserHandle, USER_DATA_GENRE, &userData, &userDataSize);
    if ((PARSER_SUCCESS == err) && userData && userDataSize) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nGenre = ");
        displayUnicodeString(userData, userDataSize);
    }

    err = IParser->getUserData(parserHandle, USER_DATA_ARTIST, &userData, &userDataSize);
    if ((PARSER_SUCCESS == err) && userData && userDataSize) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nArtist = ");
        displayUnicodeString(userData, userDataSize);
    }

    err = IParser->getUserData(parserHandle, USER_DATA_COPYRIGHT, &userData, &userDataSize);
    if ((PARSER_SUCCESS == err) && userData && userDataSize) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nCopyRight = ");
        displayUnicodeString(userData, userDataSize);
    }

    err = IParser->getUserData(parserHandle, USER_DATA_COMMENTS, &userData, &userDataSize);
    if ((PARSER_SUCCESS == err) && userData && userDataSize) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nComments = ");
        displayUnicodeString(userData, userDataSize);
    }

    err = IParser->getUserData(parserHandle, USER_DATA_CREATION_DATE, &userData, &userDataSize);
    if ((PARSER_SUCCESS == err) && userData && userDataSize) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nCreation Date = ");
        displayUnicodeString(userData, userDataSize);
    }

    err = IParser->getUserData(parserHandle, USER_DATA_ALBUM, &userData, &userDataSize);
    if ((PARSER_SUCCESS == err) && userData && userDataSize) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nAlbum = ");
        displayUnicodeString(userData, userDataSize);
    }

    err = IParser->getUserData(parserHandle, USER_DATA_RATING, &userData, &userDataSize);
    if ((PARSER_SUCCESS == err) && userData && userDataSize) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nRating = ");
        displayUnicodeString(userData, userDataSize);
    }

    err = IParser->getUserData(parserHandle, USER_DATA_LANGUAGE, &userData, &userDataSize);
    if ((PARSER_SUCCESS == err) && userData && userDataSize) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nLanguage = ");
        displayUnicodeString(userData, userDataSize);
    }

    // bail:
    PARSER_INFO(PARSER_INFO_STREAM,
                "\n************************************************************\n");
    return err;
}

static int32 LoadIndex(FslParserInterface* IParser, FslParserHandle parserHandle) {
    int32 err = PARSER_SUCCESS;

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    uint32 start_time;
    uint32 end_time;
#else
    struct timeval start_time;
    struct timeval end_time;
    struct timezone time_zone;
#endif
#endif

    bool supportExternalIndex = FALSE;
    bool useExternalIndex = FALSE; /* whether to load index table from file & export index table */

    FILE* fpIndex = NULL; /* handle of external index table file */
    uint32 indexSize;
    uint8* indexBuffer = NULL;

    /* support index import/export? */
    if (IParser->initializeIndex) {
        supportExternalIndex = TRUE;
        PARSER_INFO(PARSER_INFO_STREAM, "Support export/inport index table\n");
    } else {
        supportExternalIndex = FALSE;
        PARSER_INFO(PARSER_INFO_STREAM,
                    "No support for export/inport index table\n"); /* create API handle the index
                                                                      already */
    }

    if (!supportExternalIndex)
        return err;

    if (supportExternalIndex && g_import_available_index_table) {
        /*to detect if index file already exist*/
        fpIndex = fopen(index_file_name, "rb");
        if (!fpIndex)
            PARSER_INFO(PARSER_INFO_STREAM, "No external index file found\n");
        else {
            /* import index table from outside data base */
            if (0 == fread(&indexSize, sizeof(uint32), 1, fpIndex)) {
                err = PARSER_READ_ERROR;
                goto bail;
            }
            PARSER_INFO(PARSER_INFO_STREAM, "Size of external index: %u\n", indexSize);
            indexBuffer = fsl_osal_malloc(indexSize);
            if (NULL == indexBuffer) {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }

            if (0 == fread(indexBuffer, indexSize, 1, fpIndex)) {
                err = PARSER_READ_ERROR;
                goto bail;
            }
#if 1
            err = IParser->importIndex(parserHandle, indexBuffer, indexSize);
            if (PARSER_SUCCESS != err) {
                PARSER_INFO(PARSER_INFO_STREAM, "fail to Import index\n");
            } else {
                PARSER_INFO(PARSER_INFO_STREAM, "Index imported\n");
                useExternalIndex = TRUE;
            }
#endif
        }
    }

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    start_time = timeGetTime();
#else
    gettimeofday(&start_time, &time_zone);
#endif
#endif

    if (!useExternalIndex) /* no external index table found or import failure */
    {
        /* load index from the movie file */
        err = IParser->initializeIndex(parserHandle);
        if (PARSER_SUCCESS != err) {
            PARSER_INFO(
                    PARSER_INFO_STREAM,
                    "fail to initialize index. Index may be missing or corrupted. Err code: %d\n",
                    err);
            goto bail;
        }

        /* export it for fast openning the same clip later */
        /* export index is moved before close parser*/

        /*
            err = IParser->exportIndex(parserHandle, NULL, &indexSize);
            if(PARSER_SUCCESS != err) goto bail;
            PARSER_INFO(PARSER_INFO_STREAM,"Size of index to export: %u\n", indexSize);

            indexBuffer = fsl_osal_malloc(indexSize);
            if(NULL == indexBuffer)
            {
                err = PARSER_INSUFFICIENT_MEMORY;
                goto bail;
            }

            err = IParser->exportIndex(parserHandle, indexBuffer, &indexSize);
            if(PARSER_SUCCESS != err) goto bail;

            if(g_export_index_table)
            {
                fpIndex = fopen(index_file_name, (const char *)"wb");
                if(NULL == fpIndex)
                {
                    PARSER_ERROR("Fail to open index file to export index: %s\n", index_file_name);
                    err = PARSER_FILE_OPEN_ERROR;
                    goto bail;
                }
                size_written = fwrite(&indexSize, sizeof(uint32), 1, fpIndex);
                if(0 == size_written)
                {
                    err = PARSER_WRITE_ERROR;
                    goto bail;
                }

                size_written = fwrite(indexBuffer, indexSize, 1, fpIndex);
                if(0 == size_written)
                {
                    PARSER_ERROR("Fail to export index table to fp %d\n", fpIndex);
                    err = PARSER_WRITE_ERROR;
                    goto bail;
                }
                PARSER_INFO(PARSER_INFO_STREAM,"Index exported\n");
            }
            */
    }

    if (PARSER_SUCCESS == err) {
#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
        end_time = timeGetTime();
        g_timeprofile_info.indexLoadTimeUs = CALC_DURATION_WINCE;
#else
        gettimeofday(&end_time, &time_zone);
        g_timeprofile_info.indexLoadTimeUs = CALC_DURATION_LINUX;
#endif
#endif
    }

bail:
    if (fpIndex)
        fclose(fpIndex);

    if (indexBuffer)
        fsl_osal_free(indexBuffer);

    return err;
}

static void char_tolower(char* string) {
    while (*string != '\0') {
        *string = tolower(*string);
        string++;
    }
}

char* MediaTypeInspect(const char* source_url) {
    char* role = NULL;
    char* p = NULL;
    char subfix[16];

    p = strrchr((char*)(source_url), '.');
    printf("source_url= %s\n", source_url);
    if (p == NULL || strlen(p) > 15)
        return NULL;

    strcpy(subfix, p);
    char_tolower(subfix);
    if (strcmp(subfix, ".avi") == 0 || strcmp(subfix, ".divx") == 0)
        role = "parser.avi";
    else if (strcmp(subfix, ".mp3") == 0)
        role = "parser.mp3";
    else if (strcmp(subfix, ".amr") == 0)
        role = "parser.amr";
    else if (strcmp(subfix, ".wav") == 0)
        role = "parser.wav";
    else if (strcmp(subfix, ".aac") == 0)
        role = "parser.aac";
    else if (strcmp(subfix, ".bsac") == 0)
        role = "parser.bsac";
    else if (strcmp(subfix, ".ac3") == 0)
        role = "parser.ac3";
    else if (strcmp(subfix, ".wmv") == 0 || strcmp(subfix, ".wma") == 0 ||
             strcmp(subfix, ".asf") == 0)
        role = "parser.asf";
    else if (strcmp(subfix, ".rm") == 0 || strcmp(subfix, ".rmvb") == 0 ||
             strcmp(subfix, ".ra") == 0)
        role = "parser.rmvb";
    else if (strcmp(subfix, ".3gp") == 0 || strcmp(subfix, ".mp4") == 0 ||
             strcmp(subfix, ".mov") == 0 || strcmp(subfix, ".m4v") == 0 ||
             strcmp(subfix, ".m4a") == 0 || strcmp(subfix, ".f4v") == 0)
        role = "parser.mp4";
    else if (strcmp(subfix, ".flac") == 0)
        role = "parser.flac";
    else if (strcmp(subfix, ".wav") == 0)
        role = "parser.wav";

    else if (!strcmp(subfix, ".mpg") || !strcmp(subfix, ".mpeg") || !strcmp(subfix, ".vob") ||
             !strcmp(subfix, ".m2ts") || !strcmp(subfix, ".ts") || !strcmp(subfix, ".m2v"))
        role = "parser.mpg2";

    else if (strcmp(subfix, ".ogg") == 0)
        role = "parser.ogg";

    else if (strcmp(subfix, ".mkv") == 0 || strcmp(subfix, ".webm") == 0)
        role = "parser.mkv";

    else if (strcmp(subfix, ".flv") == 0)
        role = "parser.flv";

    else
        PARSER_INFO(PARSER_INFO_STREAM, "Can't inspect media file type.\n");

    return role;
}

static int32 CreateParserInterface(const char* source_url, FslParserInterface** pIParser,
                                   HANDLE* hParserLib, char* parserRole,
                                   const char* parser_lib_path) {
    int32 err = PARSER_SUCCESS;
    char* role = NULL;
    char parserLibName[255];
    char platform[50] = {0};
    char version[10] = {0};
    char fileNameExtension[10] = {0};

    FslParserInterface* IParser = NULL;
    HANDLE hParserDll = NULL;
    tFslParserQueryInterface myQueryInterface;

    *pIParser = NULL;
    *hParserLib = NULL;

    role = MediaTypeInspect(source_url);
    if (NULL == role)
        BAILWITHERROR(PARSER_ERR_INVALID_MEDIA)

    strcpy(parserRole, role);

    PARSER_DEBUG("\n************************************************************\n");
    PARSER_DEBUG("Platform:\t");

#if defined(ANDORID)
    PARSER_DEBUG("ARM11 eLinux\n");
    strcpy(platform, "arm11_elinux.3.0");
    strcpy(fileNameExtension, ".so");
#else
    PARSER_DEBUG("ARM eLinux\n");
    strcpy(platform, "arm_elinux");
    strcpy(fileNameExtension, ".so");
#endif


    if (strlen(parser_lib_path) > 0) {
        strcpy(parserLibName, parser_lib_path);
        if (parserLibName[strlen(parserLibName) - 1] != '/')
            strcat(parserLibName, "/");
        strcat(parserLibName, "lib_");
    } else
        strcpy(parserLibName, "lib_");

    if (!strncmp(role, "parser.mp4", strlen("parser.mp4")))
        strcat(parserLibName, "mp4");

    else if (!strncmp(role, "parser.mpg2", strlen("parser.mpg2"))) {
        strcat(parserLibName, "mpg2");
    }

    else if (!strncmp(role, "parser.rmvb", strlen("parser.rmvb")))
        strcat(parserLibName, "rm");

    else if (!strncmp(role, "parser.asf", strlen("parser.asf")))
        strcat(parserLibName, "asf");

    else if (!strncmp(role, "parser.avi", strlen("parser.avi")))
        strcat(parserLibName, "avi");

    else if (!strncmp(role, "parser.ogg", strlen("parser.ogg")))
        strcat(parserLibName, "ogg");

    else if (!strncmp(role, "parser.mkv", strlen("parser.mkv")))
        strcat(parserLibName, "mkv");

    else if (!strncmp(role, "parser.flv", strlen("parser.flv")))
        strcat(parserLibName, "flv");

    else if (!strncmp(role, "parser.amr", strlen("parser.amr")))
        strcat(parserLibName, "amr");

    else if (!strncmp(role, "parser.wav", strlen("parser.wav")))
        strcat(parserLibName, "wav");

    else if (!strncmp(role, "parser.aac", strlen("parser.aac")))
        strcat(parserLibName, "aac");
    else if (!strncmp(role, "parser.mp3", strlen("parser.mp3")))
        strcat(parserLibName, "mp3");
    else {
        PARSER_ERROR("Err: unknown media format!\n");
        BAILWITHERROR(PARSER_ERR_INVALID_MEDIA)
    }

    strcat(parserLibName, "_parser_");
    strcat(parserLibName, platform);
    strcat(parserLibName, version);
    strcat(parserLibName, fileNameExtension);

    PARSER_INFO(PARSER_INFO_STREAM, "Parser library name: %s\n", parserLibName);
    PARSER_INFO(PARSER_INFO_STREAM,
                "************************************************************\n");

    hParserDll = fsl_osal_dll_open(parserLibName);
    if (NULL == hParserDll) {
        PARSER_ERROR("Fail to open parser DLL for %s\n", role);
        BAILWITHERROR(PARSER_FILE_OPEN_ERROR)
    }

    /* query interface */
    IParser = fsl_osal_calloc(1, sizeof(FslParserInterface));
    if (!IParser)
        BAILWITHERROR(PARSER_INSUFFICIENT_MEMORY)

    myQueryInterface =
            (tFslParserQueryInterface)fsl_osal_dll_symbol(hParserDll, "FslParserQueryInterface");
    if (NULL == myQueryInterface) {
        PARSER_ERROR("Fail to query parser interface\n");
        BAILWITHERROR(PARSER_ERR_INVALID_API)
    }

    /* create & delete */
    err = myQueryInterface(PARSER_API_GET_VERSION_INFO, (void**)&IParser->getVersionInfo);
    if (err)
        goto bail;
    if (!IParser->getVersionInfo)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    err = myQueryInterface(PARSER_API_CREATE_PARSER, (void**)&IParser->createParser);
    if (err)
        goto bail;
    if (!IParser->createParser)
        BAILWITHERROR(PARSER_ERR_INVALID_API)
    err = myQueryInterface(PARSER_API_CREATE_PARSER2, (void**)&IParser->createParser2);
    if (err)
        IParser->createParser2 = NULL;

    err = myQueryInterface(PARSER_API_DELETE_PARSER, (void**)&IParser->deleteParser);
    if (err)
        goto bail;
    if (!IParser->deleteParser)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    /* index export/import */
    err = myQueryInterface(PARSER_API_INITIALIZE_INDEX, (void**)&IParser->initializeIndex);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_EXPORT_INDEX, (void**)&IParser->exportIndex);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_IMPORT_INDEX, (void**)&IParser->importIndex);
    if (err)
        goto bail;
    if (IParser->initializeIndex || IParser->exportIndex || IParser->importIndex) {
        if ((NULL == IParser->initializeIndex) || (NULL == IParser->exportIndex) ||
            (NULL == IParser->importIndex)) {
            PARSER_ERROR(
                    "Invalid API for index initialize/export/import. Must implement ALL or NONE of "
                    "the three API.\n");
            BAILWITHERROR(PARSER_ERR_INVALID_API)
        }
    }

    /* movie properties */
    err = myQueryInterface(PARSER_API_IS_MOVIE_SEEKABLE, (void**)&IParser->isSeekable);
    if (err)
        goto bail;
    if (!IParser->isSeekable)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    err = myQueryInterface(PARSER_API_GET_MOVIE_DURATION, (void**)&IParser->getMovieDuration);
    if (err)
        goto bail;
    if (!IParser->getMovieDuration)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    err = myQueryInterface(PARSER_API_GET_USER_DATA, (void**)&IParser->getUserData);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_META_DATA, (void**)&IParser->getMetaData);
    if (err)
        IParser->getMetaData = NULL;

    err = myQueryInterface(PARSER_API_GET_NUM_PROGRAMS, (void**)&IParser->getNumPrograms);
    if (err)
        goto bail;

    err = myQueryInterface(PARSER_API_GET_NUM_TRACKS, (void**)&IParser->getNumTracks);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_NUM_PROGRAMS, (void**)&IParser->getNumPrograms);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_PROGRAM_TRACKS, (void**)&IParser->getProgramTracks);
    if (err)
        goto bail;

    /*    if((!IParser->getNumTracks && !IParser->getNumPrograms)
            ||(IParser->getNumPrograms && !IParser->getProgramTracks))
        {
            PARSER_ERROR("Invalid API to get tracks or programs.\n");
            BAILWITHERROR(PARSER_ERR_INVALID_API)
        }
    */
    /* generic track properties */
    err = myQueryInterface(PARSER_API_GET_TRACK_TYPE, (void**)&IParser->getTrackType);
    if (err)
        goto bail;
    if (!IParser->getTrackType)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    err = myQueryInterface(PARSER_API_GET_TRACK_DURATION, (void**)&IParser->getTrackDuration);
    if (err)
        goto bail;
    if (!IParser->getTrackDuration)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    err = myQueryInterface(PARSER_API_GET_LANGUAGE, (void**)&IParser->getLanguage);
    if (err)
        goto bail;

    err = myQueryInterface(PARSER_API_GET_BITRATE, (void**)&IParser->getBitRate);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_DECODER_SPECIFIC_INFO,
                           (void**)&IParser->getDecoderSpecificInfo);
    if (err)
        goto bail;

    /* video properties (not required for audio-only media */
    err = myQueryInterface(PARSER_API_GET_VIDEO_FRAME_WIDTH, (void**)&IParser->getVideoFrameWidth);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_VIDEO_FRAME_HEIGHT,
                           (void**)&IParser->getVideoFrameHeight);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_VIDEO_FRAME_RATE, (void**)&IParser->getVideoFrameRate);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_VIDEO_SCAN_TYPE, (void**)&IParser->getVideoScanType);
    if (err) {
        /* optional api */
        IParser->getVideoScanType = NULL;
        err = 0;
    }

    /* audio properties */
    err = myQueryInterface(PARSER_API_GET_AUDIO_NUM_CHANNELS,
                           (void**)&IParser->getAudioNumChannels);
    if (err)
        goto bail;
    if (!IParser->getAudioNumChannels)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    err = myQueryInterface(PARSER_API_GET_AUDIO_SAMPLE_RATE, (void**)&IParser->getAudioSampleRate);
    if (err)
        goto bail;
    if (!IParser->getAudioSampleRate)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    err = myQueryInterface(PARSER_API_GET_AUDIO_BITS_PER_SAMPLE,
                           (void**)&IParser->getAudioBitsPerSample);
    if (err)
        goto bail;

    err = myQueryInterface(PARSER_API_GET_AUDIO_BLOCK_ALIGN, (void**)&IParser->getAudioBlockAlign);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_AUDIO_CHANNEL_MASK,
                           (void**)&IParser->getAudioChannelMask);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_AUDIO_BITS_PER_FRAME,
                           (void**)&IParser->getAudioBitsPerFrame);
    if (err)
        goto bail;

    /* text/subtitle properties */
    err = myQueryInterface(PARSER_API_GET_TEXT_TRACK_WIDTH, (void**)&IParser->getTextTrackWidth);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_TEXT_TRACK_HEIGHT, (void**)&IParser->getTextTrackHeight);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_TEXT_TRACK_MIME, (void**)&IParser->getTextTrackMime);
    if (err)
        IParser->getTextTrackMime = NULL;

    /* sample reading, seek & trick mode */
    err = myQueryInterface(PARSER_API_GET_READ_MODE, (void**)&IParser->getReadMode);
    if (err)
        goto bail;
    if (!IParser->getReadMode)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    err = myQueryInterface(PARSER_API_SET_READ_MODE, (void**)&IParser->setReadMode);
    if (err)
        goto bail;
    if (!IParser->setReadMode)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    err = myQueryInterface(PARSER_API_ENABLE_TRACK, (void**)&IParser->enableTrack);
    if (err)
        goto bail;

    err = myQueryInterface(PARSER_API_GET_NEXT_SAMPLE, (void**)&IParser->getNextSample);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_NEXT_SYNC_SAMPLE, (void**)&IParser->getNextSyncSample);
    if (err)
        goto bail;

    err = myQueryInterface(PARSER_API_GET_FILE_NEXT_SAMPLE, (void**)&IParser->getFileNextSample);
    if (err)
        goto bail;
    err = myQueryInterface(PARSER_API_GET_FILE_NEXT_SYNC_SAMPLE,
                           (void**)&IParser->getFileNextSyncSample);
    if (err)
        goto bail;

    if (!IParser->getNextSample && !IParser->getFileNextSample) {
        PARSER_ERROR("ERR: Support neither track-based nor file-based reading mode\n");
        BAILWITHERROR(PARSER_ERR_INVALID_API)
    }

    if (IParser->getFileNextSample && !IParser->enableTrack) {
        PARSER_ERROR("ERR: For file-based reading mode, need implement API to enable a track!\n");
        BAILWITHERROR(PARSER_ERR_INVALID_API)
    }

    err = myQueryInterface(PARSER_API_SEEK, (void**)&IParser->seek);
    if (err)
        goto bail;
    if (!IParser->seek)
        BAILWITHERROR(PARSER_ERR_INVALID_API)

    *pIParser = IParser;
    *hParserLib = hParserDll;

bail:

    if (PARSER_SUCCESS != err) {
        if (IParser) {
            PARSER_ERROR("free IParser %p \n", IParser);
            fsl_osal_free(IParser);
            IParser = NULL;
        }

        if (hParserDll) {
            PARSER_ERROR("close parser DLL %p \n", hParserDll);
            fsl_osal_dll_close(hParserDll);
            hParserDll = NULL;
        }
    }

    return err;
}

static int32 DestoryParserInterface(FslParserInterface* IParser, HANDLE hParserLib) {
    int32 err = PARSER_SUCCESS;

    if (IParser)
        fsl_osal_free(IParser);

    if (hParserLib)
        err = fsl_osal_dll_close(hParserLib);

    return err;
}

/* parse the NAL length field size, SPS and PPS info,
wrap SPS &PPS in NALs and rewrite the data buffer.*/
__attribute__((unused))
static int32 parseH264DecoderSpecificInfo(uint8* decoderSpecificInfo,
                                          uint32* decoderSpecificInfoSize,
                                          uint32* NALLengthFieldSize) {
    int32 err = PARSER_SUCCESS;
    uint8* inputData = decoderSpecificInfo;
    uint32 inputDataSize = *decoderSpecificInfoSize;
    /* H264 video, wrap decoder info in NAL units. The parameter NAL length field size
    is always 2 bytes long, different from that of data NAL units (1, 2 or 4 bytes)*/
    uint32 i, j, k;
    uint32 info_size = 0; /* size of SPS&PPS in NALs */
    char* data = NULL;    /* temp buffer */
    uint8 lengthSizeMinusOne;
    uint8 numOfSequenceParameterSets;
    uint8 numOfPictureParameterSets;
    uint16 NALLength;

    lengthSizeMinusOne = inputData[4];
    lengthSizeMinusOne &= 0x03;

    *NALLengthFieldSize =
            (uint32)lengthSizeMinusOne + 1; /* lengthSizeMinusOne = 0x11, 0b1111 1111 */

    numOfSequenceParameterSets = inputData[5] & 0x1f;

    k = 6;
    for (i = 0; i < numOfSequenceParameterSets; i++) {
        if (k >= inputDataSize) {
            PARSER_ERROR("Invalid Sequence parameter NAL length: %d\n", NALLength);
            err = PARSER_ERR_INVALID_MEDIA;
            goto bail;
        }
        NALLength = inputData[k];
        NALLength = (NALLength << 8) + inputData[k + 1];
        k += (NALLength + 2);
        info_size += (NALLength + NAL_START_CODE_SIZE);
    }
    numOfPictureParameterSets = inputData[k];
    k++;

    for (i = 0; i < numOfPictureParameterSets; i++) {
        if (k >= inputDataSize) {
            PARSER_ERROR("Invalid picture parameter NAL length: %d\n", NALLength);
            err = PARSER_ERR_INVALID_MEDIA;
            goto bail;
        }
        NALLength = inputData[k];
        NALLength = (NALLength << 8) + inputData[k + 1];
        k += (NALLength + 2);
        info_size += (NALLength + NAL_START_CODE_SIZE);
    }

    /* wrap SPS + PPS into the temp buffer "data" */
    data = (char*)malloc(info_size);
    if (NULL == data) {
        err = PARSER_INSUFFICIENT_MEMORY;
        goto bail;
    }

    k = 6;
    j = 0;
    for (i = 0; i < numOfSequenceParameterSets; i++) {
        NALLength = inputData[k];
        NALLength = (NALLength << 8) + inputData[k + 1];
        *(data + j) = 0;
        *(data + j + 1) = 0;
        *(data + j + 2) = 0;
        *(data + j + 3) = 1;
        j += 4;

        memmove(data + j, inputData + k + 2, NALLength);
        k += (NALLength + 2);
        j += NALLength;
    }
    k++; /* number of picture parameter sets */
    for (i = 0; i < numOfPictureParameterSets; i++) {
        NALLength = inputData[k];
        NALLength = (NALLength << 8) + inputData[k + 1];
        *(data + j) = 0;
        *(data + j + 1) = 0;
        *(data + j + 2) = 0;
        *(data + j + 3) = 1;
        j += 4;

        memmove(data + j, inputData + k + 2, NALLength);
        k += (NALLength + 2);
        j += NALLength;
    }

    /* write back to the original buffer */
    memcpy(decoderSpecificInfo, data, info_size);
    *decoderSpecificInfoSize = info_size;

bail:
    if (data)
        free(data);
    return err;
}

static int32 scanOneCommand(FILE** fpCommands, char* command) {
    int32 err = 0;
    printf("scan one command ...\n");
    if (NULL == *fpCommands) {
        if (scanf("%s", command) != 1)
            err = EOF;
        // printf(" command %s, err %d (EOF %d)\n", command, err, EOF); /* note: scanf will get a
        // command but return EOF */
    } else {
        err = fscanf(*fpCommands, "%s", command);
        printf("current command: %s\n", command);
        if (EOF == err) { /* switch to UI to get next command */
            *fpCommands = NULL;
            if (scanf("%s", command) != 1)
                err = EOF;
        }
    }

    return err;
}

static void resetTrackCounter(Track* track) {
    track->sampleCount = 0;
    track->byteCount = 0;
    track->lastPts = track->usSegmentStartTime;
    track->lastPtsPure = track->usSegmentStartTime;
    track->ptsRepeatCount = 0;
    track->ptsNoMonoCount = 0;

    track->sampleSize = 0;
    track->sampleBufferOffset = 0;

    track->firstSampleDataGot = FALSE;
    track->isNewSample = TRUE;
    track->eos = FALSE;

    if (track->isBSAC && (track->trackNum != track->firstBSACTrackNum)) {
        track->eos = TRUE; /* for BSAC, only need to read 1st BSAC track */
    }
}

/* select the next track to read, not finished, enabled,
    - minimum pts for normal playback & fast forward
    - maximum pts for rewinding
    TODO: Skip non-1st BSAC audio track */
static uint32 selectNextTrack(uint32 numTracks, Track* tracks, int32 rate) {
    uint32 i;
    uint32 trackNum = PARSER_INVALID_TRACK_NUMBER;
    Track* track;

    if (rate > 0) {
        uint64 minPTS = -1;

        for (i = 0; i < numTracks; i++) {
            track = tracks + i;
            if (track->eos || !track->enabled)
                continue;

            if (minPTS > track->lastPts) {
                trackNum = i;
                minPTS = track->lastPts;
            }
        }
    } else /* rewinding */
    {
        int64 maxPTS = -1;

        for (i = 0; i < numTracks; i++) {
            track = tracks + i;
            if (track->eos || !track->enabled)
                continue;

            if (maxPTS < (int64)track->lastPts) {
                trackNum = i;
                maxPTS = track->lastPts;
            }
        }
    }

    return trackNum;
}

/* calculate the reference clock based on all track's PTS and play direction */
__attribute__((unused))
static uint64 getReferenceClock(uint32 numTracks, Track* tracks, int32 rate) {
    uint32 i;
    uint64 usRefClock = 0;
    uint32 trackNum = PARSER_INVALID_TRACK_NUMBER;
    Track* track;

    if (rate > 0) {
        uint64 minPTS = 0;

        for (i = 0; i < numTracks; i++) {
            track = tracks + i;

            /* text samples are too few and may freeze the reference clock and cause queue overflow
             */
            if (track->eos || !track->enabled || (MEDIA_TEXT == track->mediaType))
                continue;

            if (minPTS > track->lastPts) {
                trackNum = i;
                minPTS = track->lastPts;
            }
        }
        if ((uint32)PARSER_INVALID_TRACK_NUMBER != trackNum) {
            if (minPTS > DEFAULT_DECODING_DELAY_IN_US + track[trackNum].usSegmentStartTime) {
                usRefClock = minPTS - DEFAULT_DECODING_DELAY_IN_US;
            } else {
                usRefClock = track[trackNum].usSegmentStartTime;
            }
        }
    } else /* rewinding */
    {
        int64 maxPTS = -1;

        for (i = 0; i < numTracks; i++) {
            track = tracks + i;
            if (track->eos || !track->enabled)
                continue;

            if (maxPTS < (int64)track->lastPts) {
                trackNum = i;
                maxPTS = track->lastPts;
            }
        }
        usRefClock = maxPTS;
    }

    return usRefClock;
}

static int32 enableMediaTracks(FslParserInterface* IParser, HANDLE parserHandle, uint32 numTracks,
                               Track* tracks) {
    int32 err = PARSER_SUCCESS;
    Track* track;
    uint32 i;
    uint32 mediaType;

    for (i = 0; i < numTracks; i++) {
        track = tracks + i;
        mediaType = track->mediaType;

        if ((MEDIA_VIDEO == mediaType) || (MEDIA_AUDIO == mediaType) || (MEDIA_TEXT == mediaType)) {
            track->enabled = TRUE;
        } else {
            track->enabled = FALSE;
        }

        if (mediaType == MEDIA_TEXT && track->isMKVText)
            track->enabled = FALSE;

        err = IParser->enableTrack(parserHandle, i, track->enabled);
        if (PARSER_SUCCESS != err)
            goto bail;
    }

bail:
    return err;
}

/* when playback change between 1X and trick mode, audio tracks need to be enabled or disabled */
static int32 enableAudioTracks(FslParserInterface* IParser, HANDLE parserHandle, uint32 numTracks,
                               Track* tracks, bool enable) {
    int32 err = PARSER_SUCCESS;
    Track* track;
    uint32 i;

    for (i = 0; i < numTracks; i++) {
        track = tracks + i;

        if ((MEDIA_AUDIO == track->mediaType) && (FALSE == g_abTrackDisabledByCommand[i])) {
            track->enabled = enable;
            err = IParser->enableTrack(parserHandle, i, enable);
        }
    }

    return err;
}

static bool missTrackEOS(uint32 numTracks, Track* tracks) {
    bool missOneTrackEOS = FALSE;
    uint32 i;
    Track* track;

    for (i = 0; i < numTracks; i++) {
        track = tracks + i;
        if (track->eos || !track->enabled)
            continue;

        if ((track->lastPts > track->usDuration) ||
            ((track->lastPts + 200 * 1000) > track->usDuration)) {
            PARSER_INFO(PARSER_INFO_STREAM,
                        "\nTrack %d actually ends! Last pts %lld us, duration %lld us.\n", i,
                        track->lastPts, track->usDuration);

            missOneTrackEOS = TRUE;
            track->eos = TRUE;

            logError(RISK_FAKE_QUEUE_OVERFLOW);
        }
    }

    return missOneTrackEOS;
}

static void flushQueues(uint32 numTracks, Track* tracks) {
    uint32 i;
    Track* track;

    PARSER_INFO(PARSER_INFO_BUFFER, "Flush track queues ...\n\n");
    for (i = 0; i < numTracks; i++) {
        track = tracks + i;
        if (track->hQueue)
            flushQueue(track->hQueue);
    }
}

static int32 backSampleData(Track* track, uint8* buffer, uint32 dataSize) {
    int32 err = PARSER_SUCCESS;

    if (g_dump_track_data && buffer) {
        uint32 trackNum = track->trackNum;

        if ((track->sampleBufferOffset + dataSize) >
            track->sampleBufferSize) { /* huge samples can be present. eg big video frame, PCM audio
                                          etc. */
            uint32 oriSampleBufferSize = track->sampleBufferSize;
            track->sampleBufferSize =
                    (oriSampleBufferSize * 2) > (track->sampleBufferOffset + dataSize)
                            ? (oriSampleBufferSize * 2)
                            : (track->sampleBufferOffset + dataSize);
            PARSER_INFO(PARSER_INFO_DATASIZE,
                        "trk %d, extend sample buffer size from %u to %u bytes\n", trackNum,
                        oriSampleBufferSize, track->sampleBufferSize);
            track->sampleBuffer = fsl_osal_realloc(track->sampleBuffer, track->sampleBufferSize);
            if (!track->sampleBuffer) {
                PARSER_ERROR("trk %d, fail to realloc sample buffer.\n", trackNum);
                BAILWITHERROR(PARSER_INSUFFICIENT_MEMORY)
            }
        }

        memcpy(track->sampleBuffer + track->sampleBufferOffset, buffer, dataSize);
        track->sampleBufferOffset += dataSize;
    }

bail:
    return err;
}

static int32 backSamplePTS(Track* track, uint8* sampleDataBuffer, uint64 sampleTimeStamp,
                           uint64 sampleDuration) {
    int32 err = PARSER_SUCCESS;

    if (track->isNewSample) {
        if ((MEDIA_TEXT == track->mediaType) &&
            (TXT_DIVX_FEATURE_SUBTITLE ==
             track->decoderType)) { /* for divx subtitile, real pts is inside the raw data */
            err = getAviSubtitleTimeStamp(sampleDataBuffer, &sampleTimeStamp, &sampleDuration);
            if (PARSER_SUCCESS != err) {
                PARSER_ERROR("divx subitle trk %d, sample %u, fail to get pts, err %d\n",
                             track->trackNum, track->sampleCount, err);
                goto bail;
            }
        }
        track->usSampleTimeStamp = sampleTimeStamp;
        track->usCurSampleDuration = sampleDuration;
    } else {
        if ((MEDIA_TEXT == track->mediaType) && (TXT_DIVX_FEATURE_SUBTITLE == track->decoderType)) {
            sampleTimeStamp = track->usSampleTimeStamp;
            sampleDuration = track->usCurSampleDuration;
        }

        if (PARSER_SUCCESS != checkPtsWithinSample(track, sampleTimeStamp))
            BAILWITHERROR(PARSER_ERR_UNKNOWN)
    }

bail:
    return err;
}

/*static uint8 abyHead[] = {0x0, 0x0, 0x0, 0x1, 0x67, 0x4d, 0x40, 0x1f, 0x96, 0x56, 0x5, 0xa1, 0x22,
0xc0, 0x54, 0x20, 0x1, 0xb7, 0xe4, 0xa0, 0x66, 0xff, 0x30, 0x1c, 0x48, 0x0, 0x4, 0xa5, 0x70, 0x0,
0xe, 0xde, 0x4c, 0x63, 0x1c, 0x1f, 0x14, 0x29, 0x38, 0x0, 0x0, 0x0, 0x1, 0x68, 0xe9, 0x8, 0x36,
0x20};
*/

// #define CONTROLSIZE
#define SAVEFRM 5         // 0x7fffffff //138 //1890 //300
#define SAVESIZE 7287552  //(uint32)(1<<20)
static FILE* g_fp;

void SaveData(uint8* pbyData, uint32 dwLen) {
    static FILE* fp;
    static uint32 dwSaveNum;
    static uint32 dwSaveSize;
    int ret;

#ifdef CONTROLSIZE
    if (dwSaveSize >= SAVESIZE) {
        return;
    }
#else
    if (dwSaveNum >= SAVEFRM) {
        return;
    }
#endif

    if (fp == NULL) {
        fp = fopen("d:\\raw.data", "wb");
        if (fp == NULL) {
            printf("fopen failed\r\n");
            return;
        }
        g_fp = fp;

        //    fwrite(abyHead, 1, sizeof(abyHead), fp); //!!! add head
    }

    ret = fwrite(pbyData, 1, dwLen, fp);
    if ((uint32)ret != dwLen) {
        fclose(fp);
        printf("fwrite failed, %d, %d\r\n", ret, dwLen);
        return;
    }

    dwSaveNum++;
    dwSaveSize += dwLen;

#ifdef CONTROLSIZE
    if (dwSaveSize >= SAVESIZE) {
        fclose(fp);
        printf("write data finished\r\n");
    }
#else
    if (dwSaveNum >= SAVEFRM) {
        fclose(fp);
        printf("write data finished\r\n");
    }
#endif

    return;
}

static FILE* g_fpLen;
#define SAVELEN_NUM 0x7fffffff

void SaveLen(uint32 dwLen) {
    static FILE* fp;
    static uint32 dwSaveNum;

    if (dwSaveNum >= SAVELEN_NUM) {
        return;
    }

    if (fp == NULL) {
        fp = fopen("d:\\len.txt", "wb");
        if (fp == NULL) {
            printf("fopen failed\r\n");
            return;
        }
        g_fpLen = fp;
    }

    fprintf(fp, "%d\n", dwLen);

    dwSaveNum++;

    if (dwSaveNum >= SAVELEN_NUM) {
        fclose(fp);
        printf("write len finished\r\n");
    }

    return;
}

#define GIVENFRM 1  // 390 //5 //138 //1890 //300

void SaveGivenFrame(uint8* pbyData, uint32 dwLen) {
    FILE* fp;
    static uint32 dwFrmNum;
    int ret;

    dwFrmNum++;

    if (dwFrmNum != GIVENFRM) {
        return;
    }

    fp = fopen("d:\\frame.data", "wb");
    if (fp == NULL) {
        printf("fopen failed\r\n");
        return;
    }

    ret = fwrite(pbyData, 1, dwLen, fp);
    if ((uint32)ret != dwLen) {
        fclose(fp);
        printf("fwrite failed, %d, %d\r\n", ret, dwLen);
        return;
    }

    fclose(fp);
    printf("write data finished\r\n");

    return;
}

static void ParserLock() {
#ifdef MULTI_PROGRAM_TEST
    fsl_osal_mutex_lock(g_hParserMutex);
#endif
}

static void ParserUnLock() {
#ifdef MULTI_PROGRAM_TEST
    fsl_osal_mutex_unlock(g_hParserMutex);
#endif
}

/**************************************************************************************
This is a simulation of parser plug-in task : a loop to read samples under some play rate.

It has an extra feature:
Before reading a new sample, always check if to halt by user command 'u' (play rate = 0)
This task will pause to wait for a new command.
***************************************************************************************/
static void* parserTask(void* arg) {
    int32 err = PARSER_SUCCESS;
    PlayState* playState = (PlayState*)arg;
    FslParserInterface* IParser = playState->IParser;
    HANDLE parserHandle = playState->parserHandle;
    uint32 readMode = playState->readMode;
    uint32 numTracks = playState->numTracks;
    Track* tracks = playState->tracks;

    int32 playRate;

    bool hasReadSomething = FALSE;
    bool eos = FALSE; /* end of FILE */
    bool bSave = FALSE;
    uint64 usMovieSegmentStartTime = 0; /* movie segment start time for 1X/FF, end time for RW */

    uint32 i;
    uint32 trackNum;

    Track* track = NULL;

    uint32 dataSize;
    uint64 sampleTimeStamp, sampleStopTime;
    uint64 sampleDuration;
    uint32 sampleFlag;

    uint8* buffer;
    void* bufferContext;
    static uint8 adwSegCount[32];
    static uint32 dwTotalCount;
#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    uint32 start_time;
    uint32 end_time;
#else
    struct timeval start_time;
    struct timeval end_time;
    struct timezone time_zone;
#endif
#endif

    clearFileSeekError();

    playState->errCode = PARSER_SUCCESS;

    err = fsl_osal_mutex_lock(playState->hPlayMutex);
    if (err)
        goto bail;
    playRate = playState->playRate;
    usMovieSegmentStartTime = playState->usMovieSegmentStartTime;
    sampleStopTime = playState->usMovieStopTime;
    err = fsl_osal_mutex_unlock(playState->hPlayMutex);
    if (err)
        goto bail;

    for (i = 0; i < numTracks; i++) {
        track = tracks + i;
        resetTrackCounter(track);

        if (playRate && track->enabled && track->hQueue) {
            pushEvent(track->hQueue, EVENT_NEW_SEGMENT, track->usSegmentStartTime, playRate);
        }
    }

    while (playState->isRunning && !eos) {
        /* before reading a new sample, always check if to halt by user command 'u' */
        err = fsl_osal_mutex_lock(playState->hPlayMutex);
        if (err)
            goto bail;
        playRate = playState->playRate;
        err = fsl_osal_mutex_unlock(playState->hPlayMutex);
        if (err)
            goto bail;

        if (0 == playRate) {
            M_SLEEP(500);
            continue;
        }

        hasReadSomething = TRUE;

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
        start_time = timeGetTime();
#else
        gettimeofday(&start_time, &time_zone);
#endif
#endif

        if (PARSER_READ_MODE_TRACK_BASED == readMode) /* track-based reading mode */
        {
            /* select the next track to read, on track EOS or a entire sample is got! */
            if (!track || track->eos || track->isNewSample) {
                trackNum = selectNextTrack(numTracks, tracks, playRate);
                if ((uint32)PARSER_INVALID_TRACK_NUMBER == trackNum) {
                    PARSER_ERROR("Can not find next track to read! File EOS!\n");
                    eos = TRUE;
                    playState->isRunning = FALSE;
                    goto bail;
                }
                track = tracks + trackNum;
            }

            if (1 == playRate) {
                ParserLock();
                err = IParser->getNextSample(parserHandle, trackNum, &buffer, &bufferContext,
                                             &dataSize, &sampleTimeStamp, &sampleDuration,
                                             &sampleFlag);
                ParserUnLock();

            } else /* trick mode */
            {
                uint32 direction;
                direction = (playRate > 0) ? FLAG_FORWARD : FLAG_BACKWARD;

                ParserLock();
                err = IParser->getNextSyncSample(parserHandle, direction, trackNum, &buffer,
                                                 &bufferContext, &dataSize, &sampleTimeStamp,
                                                 &sampleDuration, &sampleFlag);
                ParserUnLock();

                printf("getNextSyncSample, err %d, trackNum %d, dataSize %d, stamp %lld\n", err,
                       trackNum, dataSize, sampleTimeStamp);
            }

            if (sampleFlag & FLAG_SAMPLE_NOT_FINISHED) {
            } else {
            }

            if ((PARSER_EOS == err) || (PARSER_BOS == err)) { /* single track ends */
                if (PARSER_EOS == err)
                    PARSER_INFO(PARSER_INFO_STREAM, "track %d, EOS!\n", trackNum);
                else
                    PARSER_INFO(PARSER_INFO_STREAM, "track %d, BOS!\n", trackNum);
                track->eos = TRUE; /* go on reading until EOF */
                continue;
            } else if (PARSER_SUCCESS != err) {
                PARSER_INFO(PARSER_INFO_STREAM, "track %d, read ERROR! err code %d\n", trackNum,
                            err);
                track->eos = TRUE;
                break;
            }

        }

        else /* file-based reading mode */
        {

            if (1 == playRate) {
                ParserLock();
                err = IParser->getFileNextSample(parserHandle, &trackNum, &buffer, &bufferContext,
                                                 &dataSize, &sampleTimeStamp, &sampleDuration,
                                                 &sampleFlag);
                ParserUnLock();
            } else {
                uint32 direction;
                direction = (playRate > 0) ? FLAG_FORWARD : FLAG_BACKWARD;
                ParserLock();
                err = IParser->getFileNextSyncSample(parserHandle, direction, &trackNum, &buffer,
                                                     &bufferContext, &dataSize, &sampleTimeStamp,
                                                     &sampleDuration, &sampleFlag);
                ParserUnLock();
            }

            //     printf("==== err %d, trackNum %d, dataSize %d, stamp %lld, next %lld, duration
            //     %lld, finish %d\n",

            if (err == 0)
                adwSegCount[trackNum]++;

            dwTotalCount++;
            bSave = 0;

            if ((err == 0) && buffer && (bufferContext == NULL)) {
                printf("err, buffer 0x%p valid while bufferContext NULL\n", buffer);
            }

            if ((err == 0) && bSave && (trackNum == 1) && dataSize)  // && (playRate > 1))
            {
                SaveData(buffer, dataSize);
            } else {
            }

            if ((err == 1) && g_fp) {
                fclose(g_fp);
                printf("write data finished on eos\r\n");
            }

            //    printf("========= rate %d, err %d, trackNum %d, dataSize %d, stamp %lld, finish
            //    %d, sync %d\n",

            //   fprintf(g_fpLog, "========= rate %d, err %d, trackNum %d, dataSize %d, stamp %lld,
            //   finish %d, sync %d\n",

            if (sampleFlag & FLAG_SAMPLE_NEWSEG) {
            }

            if (!(sampleFlag & FLAG_SAMPLE_NOT_FINISHED) && (err == 0)) {
            }

            if ((PARSER_EOS == err) || (PARSER_BOS == err)) /* trick mode */
            {
                if (PARSER_EOS == err)
                    PARSER_INFO(PARSER_INFO_STREAM, "movie EOS!\n");
                else
                    PARSER_INFO(PARSER_INFO_STREAM, "movie BOS!\n");

                eos = TRUE;
                break;
            } else if (PARSER_SUCCESS != err) {
                if (PARSER_ERR_NO_OUTPUT_BUFFER == err) {
                    if (missTrackEOS(numTracks, tracks)) {
                        flushQueues(numTracks, tracks);
                        continue;
                    }
                }
                PARSER_ERROR("movie read ERROR! err code %d\n", err);
                eos = TRUE;
                break;
            }
            track = tracks + trackNum;
        }

        if ((trackNum == 0) && (sampleFlag & FLAG_SYNC_SAMPLE)) {
        }

        //   fprintf(g_fpLog, "========= rate %d, err %d, trackNum %d, dataSize %d, stamp %lld,
        //   finish %d, sync %d\n",

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
        end_time = timeGetTime();
        g_timeprofile_info.playCount += 1;
        g_timeprofile_info.playTimeUs += CALC_DURATION_WINCE;
#else
        gettimeofday(&end_time, &time_zone);
        g_timeprofile_info.playCount += 1;
        g_timeprofile_info.playTimeUs += CALC_DURATION_LINUX;
#endif
#endif

        if (sampleTimeStamp != (uint64)PARSER_UNKNOWN_TIME_STAMP) {
            if (sampleStopTime != 0 && ((sampleTimeStamp >= sampleStopTime && playRate > 0) ||
                                        (sampleTimeStamp <= sampleStopTime && playRate < 0))) {
                playState->isRunning = FALSE;
            }
        }

        if (PARSER_SUCCESS != checkFileReadError())
            BAILWITHERROR(PARSER_READ_ERROR)
        if (PARSER_SUCCESS != checkFileSeekError())
            BAILWITHERROR(PARSER_SEEK_ERROR)

        if (PARSER_SUCCESS != backSampleData(track, buffer, dataSize))
            BAILWITHERROR(PARSER_INSUFFICIENT_MEMORY)
        track->sampleSize += dataSize;
        track->byteCount += dataSize;

#if 0
        if(buffer)
        {   /* warning: MPEG-2 parser may output a NULL buffer, only to tell end of a sample */
            g_usReferenceClock = getReferenceClock(numTracks, tracks, playRate);
            ret_queue = pushBuffer(track->hQueue, buffer, bufferContext, dataSize, sampleTimeStamp, g_usReferenceClock, track->lastPts);
            if(ret_queue)
            {
                PARSER_INFO(PARSER_INFO_BUFFER,"trk %d, fail to push buf 0x%p, Task ends.\n", trackNum, buffer);
                err = PARSER_ERR_UNKNOWN;
                playState->isRunning = FALSE; /* quit task at once */
                break;
            }
        }
#endif

        if (buffer) {
            unrefBuffer(track->hQueue, buffer, bufferContext);
        }

        backSamplePTS(
                track, buffer, sampleTimeStamp,
                sampleDuration);  // if(PARSER_SUCCESS != backSamplePTS(track, buffer,
                                  // sampleTimeStamp, sampleDuration))
                                  //   BAILWITHERROR(PARSER_ERR_UNKNOWN) /* note: pts & duration may
                                  //   be translated if it's in the data, eg. for divx subtitle */

        if (sampleFlag & FLAG_SAMPLE_NOT_FINISHED) {
            track->isNewSample = FALSE;
        } else {
            /* an entire sample is output successfully */
            track->isNewSample = TRUE;

            PARSER_INFO(PARSER_INFO_FRAMESIZE, "trk %d, sample %d, size %d\n", trackNum,
                        track->sampleCount, track->sampleSize);
            PARSER_INFO(PARSER_INFO_PTS, "trk %d, sample %d, size %d, pts %lld, duration %lld\n",
                        trackNum, track->sampleCount, track->sampleSize, track->usSampleTimeStamp,
                        track->usCurSampleDuration);

            if ((sampleFlag & FLAG_SYNC_SAMPLE) && (MEDIA_VIDEO == track->mediaType)) {
            }

            if (PARSER_SUCCESS != checkSamplePTS(track, playRate))
                BAILWITHERROR(PARSER_ERR_UNKNOWN)

            if (PARSER_SUCCESS !=
                checkErrorConcealment(track, sampleFlag, playRate, usMovieSegmentStartTime))
                BAILWITHERROR(PARSER_ERR_UNKNOWN)

            if (PARSER_SUCCESS != dumpOneSampleData(track))
                BAILWITHERROR(PARSER_ERR_UNKNOWN)

            /* prepare for next sample */
            track->sampleCount++;

            track->sampleSize = 0;
            track->sampleBufferOffset = 0;
        }
    }

bail:
    if (hasReadSomething) {
        for (i = 0; i < numTracks; i++) {
            if (!tracks[i].enabled)
                continue;

            PARSER_INFO(PARSER_INFO_STREAM,
                        "\ntrack %d, Totally %d samples got, total bytes %lld. Last pts %lld us\n",
                        i, tracks[i].sampleCount, tracks[i].byteCount, tracks[i].lastPts);
            if (tracks[i].hQueue) {
                displayQueueStatistics(tracks[i].hQueue);
                // pushEvent(track[i].hQueue, EVENT_EOS, 0, 0); /* not necessary */
            }

            // tracks[i].usSegmentStartTime = tracks[i].lastPts; /* for new segment, user may not
            // seek but just change rate */
        }
    }

    if (eos) {
        /* seek all tracks to the beginning when playback ends on EOF. This is a defualt behavior of
         * players. */
        for (i = 0; i < numTracks; i++) {
            int32 err1;
            uint64 usSeekTime;

            if (PARSER_READ_MODE_FILE_BASED == readMode) {
                track = tracks + i;
                checkPreroll(track, playRate, usMovieSegmentStartTime);
            }

            /* seek track to its beginning */
            usSeekTime = 0;
            err1 = IParser->seek(parserHandle, i, &usSeekTime, SEEK_FLAG_NO_LATER);
            if (PARSER_SUCCESS != err1) {
                PARSER_ERROR("Fail to seek to BOS at task end\n");
                err = PARSER_SEEK_ERROR;
                goto ParserTaskEnd;
            }
            tracks[i].usSegmentStartTime = 0;
            playState->usMovieSegmentStartTime = 0;
        }

        checkAVSync(numTracks, tracks);
    }

ParserTaskEnd:
    playState->errCode = err;
    return NULL;
}

static HANDLE startTask(PlayState* playState) {
    HANDLE hTask;
    playState->isRunning = TRUE;
    hTask = fsl_osal_create_thread(parserTask, playState);
    return hTask;
}

static int32 pauseTask(HANDLE hTask, PlayState* playState) {
    int32 err = 0;

    playState->isRunning = FALSE;

    if (hTask) /* task may already terminated */
        err = fsl_osal_thread_join(hTask);

    if (err) {
        PARSER_ERROR("fail to pause the parser task, err\n");
    } else {
        err = playState->errCode; /* we want to carry back the parser's error */
        if ((PARSER_BOS == err) || (PARSER_EOS == err))
            err = PARSER_SUCCESS; /* still a success, able to go ahead */
    }

    return err;
}

static int32 SeekMovie(FslParserInterface* IParser, FslParserHandle parserHandle, uint32 numTracks,
                       Track* tracks, uint64 seek_secus, bool isAccurate) {
    int32 err = PARSER_SUCCESS;

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    uint32 start_time;
    uint32 end_time;
#else
    struct timeval start_time;
    struct timeval end_time;
    struct timezone time_zone;
#endif
#endif

    bool seekable;
    uint64 usTimeDesired = seek_secus;
    uint64 usTimeMatched;
    uint32 i;
    uint32 videoTrackNum = PARSER_INVALID_TRACK_NUMBER; /* the 1st enabled video track */
    uint64 usTimeMatchedVideo;

    uint64 usSegmentStartTime;
    uint32 minutes;
    uint32 seconds;

    Track* track;

    err = IParser->isSeekable(parserHandle, &seekable);
    if (PARSER_SUCCESS != err)
        goto bail;
    if (!seekable) {
        PARSER_INFO(PARSER_INFO_STREAM, "Movie is not seekable\n");
        return err;
    }

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    start_time = timeGetTime();
#else
    gettimeofday(&start_time, &time_zone);
#endif
#endif

    if (isAccurate) /* accurate seek, every track to an earlier sync sample */
    {
        usTimeDesired = seek_secus;

        for (i = 0; i < numTracks; i++) {
            track = tracks + i;

            usTimeMatched = usTimeDesired;
            PARSER_INFO(PARSER_INFO_SEEK, "Seek trk %d, flag NO_LATER, desired time %lld us, ", i,
                        usTimeDesired);
            err = IParser->seek(parserHandle, i, &usTimeMatched, SEEK_FLAG_NO_LATER);

            if ((PARSER_SUCCESS != err) && (PARSER_EOS != err)) {
                PARSER_ERROR("Trk %d, accrate seek fail! flag NO_LATER, err %d\n", i, err);
                if (MEDIA_TYPE_UNKNOWN == track->mediaType)
                    continue;
                goto bail;
            }
            if (PARSER_EOS == err) {
                PARSER_INFO(PARSER_INFO_SEEK, "to EOS, assume ");
                if (track->usDuration)
                    usTimeMatched = track->usDuration;
                else
                    usTimeMatched = usTimeDesired;
            }

            printf("matched time %lld us\n", usTimeMatched);
            track->usSegmentStartTime = usTimeMatched;
        }

        usSegmentStartTime = usTimeDesired;
        playState.usMovieSegmentStartTime = usTimeDesired;
    }

    else /* non-accurate seek, to the nearest video key frame */
    {
        /* If video is present, seek video first. Other tracks will seek to the key frame time */
        videoTrackNum = PARSER_INVALID_TRACK_NUMBER;
        for (i = 0; i < numTracks; i++) {
            track = tracks + i;

            if (MEDIA_VIDEO == track->mediaType) {
                videoTrackNum = i;
                break;
            }
        }

        if ((uint32)PARSER_INVALID_TRACK_NUMBER != videoTrackNum) {
            usTimeMatchedVideo = usTimeDesired = seek_secus;
            PARSER_INFO(PARSER_INFO_SEEK, "Seek video trk %d, flag NEAREST, desired time %lld us, ",
                        videoTrackNum, usTimeDesired);

            // for Mpeg2 we can use SEEK_FLAG_FUZZ
            // for other formats for which performance is an issue, SEEK_FLAG_FUZZ can be used
            if (track->isMpeg2Video)
                err = IParser->seek(parserHandle, videoTrackNum, &usTimeMatchedVideo,
                                    SEEK_FLAG_FUZZ);
            else
                err = IParser->seek(parserHandle, videoTrackNum, &usTimeMatchedVideo,
                                    SEEK_FLAG_NO_EARLIER);  // SEEK_FLAG_NEAREST);

            if ((PARSER_SUCCESS != err) && (PARSER_EOS != err)) {
                PARSER_ERROR("Video trk %d, seek fail! flag NEAREST, err %d\n", videoTrackNum, err);
                goto bail;
            }

            if (PARSER_EOS == err) {
                PARSER_INFO(PARSER_INFO_SEEK, "to EOS, assume ");
                if (track->usDuration)
                    usTimeMatchedVideo = track->usDuration;
                else
                    usTimeMatchedVideo = usTimeDesired;
            }

            printf("matched time %lld us\n", usTimeMatchedVideo);
            tracks[videoTrackNum].usSegmentStartTime = usTimeMatchedVideo;
        } else {
            PARSER_INFO(PARSER_INFO_SEEK, "No enabled video trk is present.\n");
            usTimeMatchedVideo = usTimeDesired; /* seek to the user desired time */
        }

        playState.usMovieSegmentStartTime = usTimeMatchedVideo;

        /* seek other tracks */
        for (i = 0; i < numTracks; i++) {
            track = tracks + i;
            if (videoTrackNum == i)
                continue;

            usTimeMatched = usTimeMatchedVideo;
            PARSER_INFO(PARSER_INFO_SEEK, "Seek trk %d, flag NO_LATER, desired time %lld us, ", i,
                        usTimeMatched);
            err = IParser->seek(parserHandle, i, &usTimeMatched, SEEK_FLAG_NO_LATER);

            if ((PARSER_SUCCESS != err) && (PARSER_EOS != err)) {
                PARSER_INFO(PARSER_INFO_SEEK, "Trk %d, seek fail! flag NO_LATER, err %d\n", i, err);
                if (MEDIA_TYPE_UNKNOWN == track->mediaType)
                    continue;
                goto bail;
            }
            if (PARSER_EOS == err) {
                PARSER_INFO(PARSER_INFO_SEEK, "to EOS, assume ");
                if (track->usDuration)
                    usTimeMatched = track->usDuration;
                else
                    usTimeMatched = usTimeMatchedVideo;
            }
            printf("matched time %lld us\n", usTimeMatched);
            track->usSegmentStartTime = usTimeMatched;
        }

        usSegmentStartTime = usTimeMatchedVideo;
    }

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    end_time = timeGetTime();
    g_timeprofile_info.seekCount += 1;
    g_timeprofile_info.seekTimeUs += CALC_DURATION_LINUX;
#else
    gettimeofday(&end_time, &time_zone);
    /*
    if(end_time.tv_usec >= start_time.tv_usec)
    {
        PARSER_INFO(PARSER_INFO_SEEK,"Seek response time: %u sec, %u ms\n", end_time.tv_sec -
    start_time.tv_sec, (end_time.tv_usec - start_time.tv_usec)/1000);
    }
    else
    {
        PARSER_INFO(PARSER_INFO_SEEK,"Seek response time: %u sec, %u ms\n", end_time.tv_sec -
    start_time.tv_sec -1, (end_time.tv_usec + 1000000000 - start_time.tv_usec)/1000);
    }
    */
    g_timeprofile_info.seekCount += 1;
    g_timeprofile_info.seekTimeUs += CALC_DURATION_LINUX;
#endif
#endif

    minutes = (uint32)(usSegmentStartTime / 60000000);
    seconds = (uint32)(usSegmentStartTime / 1000000 - minutes * 60);
    PARSER_INFO(PARSER_INFO_SEEK, "New segment start time: %lld us ", usSegmentStartTime);
    PARSER_INFO(PARSER_INFO_SEEK, "(%d m : %d s)\n\n", minutes, seconds);

bail:
    return err;
}

static int32 SeekTrack(FslParserInterface* IParser, FslParserHandle parserHandle, Track* track,
                       uint64 seek_secus, uint32 seek_flag) {
    int32 err = PARSER_SUCCESS;
    uint64 usSeekTime;

    if (!track->seekable) {
        PARSER_INFO(PARSER_INFO_SEEK, "%s track %d not seekable\n", __FUNCTION__, track->trackNum);
        return err;
    }

    usSeekTime = seek_secus;
    err = IParser->seek(parserHandle, track->trackNum, &usSeekTime, seek_flag);

    PARSER_INFO(PARSER_INFO_SEEK, "%s track %d seek to the %lld.\n", __FUNCTION__, track->trackNum,
                usSeekTime);
    if ((PARSER_SUCCESS != err) && (PARSER_EOS != err)) {
        PARSER_INFO(PARSER_INFO_SEEK, "Warning!track %d can not seek to the %lld. err %d\n",
                    track->trackNum, err, usSeekTime);
    } else {
        if (PARSER_EOS == err)
            usSeekTime = seek_secus;

        track->usSegmentStartTime = usSeekTime;
    }

    // bail:
    return err;
}

static int32 PlayMovie(FILE* fpUserCommandList, tQueueSettings* videoQueueSettings,
                       tQueueSettings* audioQueueSettings, tQueueSettings* textQueueSettings,
                       FslParserInterface* IParser, HANDLE parserHandle, bool isLiveSource,
                       bool isMovieSeekable, uint32 num_tracks, Track* tracks,
                       const char* parserRole, bool isStreaming) {
    int32 err = PARSER_SUCCESS;
    FILE* fpUserCommands = fpUserCommandList;
    uint32 defualtReadMode, readMode;
    uint32 i, j;
    char rep[128];

    HANDLE hTask = NULL;

    uint64 seek_secus;
    uint32 seek_mode;
    uint32 seek_flag;
    uint32 track_num;
    uint64 movie_usduration = 0;

    /***************************************************************************
                set read mode
    ***************************************************************************/
    memset(&playState, 0, sizeof(PlayState));
    for (i = 0; i < num_tracks; i++)
        if (movie_usduration < tracks[i].usDuration)
            movie_usduration = tracks[i].usDuration;

    err = IParser->getReadMode(parserHandle, &defualtReadMode);
    if (PARSER_SUCCESS != err)
        goto bail;
    ;
    if (PARSER_READ_MODE_FILE_BASED == defualtReadMode)
        PARSER_INFO(PARSER_INFO_STREAM, "\nDefault read mode: file-based\n");
    else
        PARSER_INFO(PARSER_INFO_STREAM, "\nDefault read mode: track-based\n");

    readMode = defualtReadMode;

    /*
     if(isLiveSource)
     {
         if(PARSER_READ_MODE_FILE_BASED != defualtReadMode)
             PARSER_WARNNING ("Warning! Default read mode for live source shall be FILE-BASED!\n");
         readMode = PARSER_READ_MODE_FILE_BASED;
     }
     else
         readMode = PARSER_READ_MODE_TRACK_BASED;
     err = IParser->setReadMode(parserHandle, readMode);
     if(PARSER_SUCCESS != err)
     {
         PARSER_INFO(PARSER_INFO_STREAM,"Fail to change read mode\n");
         if(g_isLiveSource)
         {
             PARSER_INFO (PARSER_INFO_STREAM,"Take the live source as local!\n");
             g_isLiveSource = FALSE; // to disable check on backward file seeking
             logError(RISK_DEEP_INTERLEAVING);
         }

         readMode = defualtReadMode;
     }
     if(PARSER_READ_MODE_FILE_BASED == readMode)
         PARSER_INFO(PARSER_INFO_STREAM,"Final read mode: file-based\n");
     else
         PARSER_INFO(PARSER_INFO_STREAM,"Final read mode: track-based\n");
     */

    (void)videoQueueSettings;
    (void)audioQueueSettings;
    (void)textQueueSettings;
    (void)isLiveSource;

    /***************************************************************************
                1. enable tracks & prepare resources
                2. seek every track to the beginning
    ***************************************************************************/
    err = enableMediaTracks(IParser, parserHandle, num_tracks, tracks);
    if (PARSER_SUCCESS != err)
        goto bail;

    for (i = 0; i < num_tracks; i++) {
        uint64 usSeekTime;

        usSeekTime = 0;
        err = IParser->seek(parserHandle, i, &usSeekTime, SEEK_FLAG_NO_LATER);
        if (PARSER_SUCCESS != err) {
            PARSER_ERROR("ERR!Can not seek to the beginning of trk %d\n", i);
            goto bail;
        }
        err = allocateTrackBuffers(&tracks[i]);
        if (PARSER_SUCCESS != err)
            goto bail;
    }

#ifdef MULTI_PROGRAM_TEST
    g_hParserMutex = fsl_osal_mutex_create(fsl_osal_mutex_normal);
    if (NULL == g_hParserMutex) {
        err = PARSER_ERR_UNKNOWN;
        goto bail;
    }
#endif

    /***************************************************************************
               launch task
    ***************************************************************************/
    playState.hPlayMutex = fsl_osal_mutex_create(fsl_osal_mutex_normal);
    if (NULL == playState.hPlayMutex) {
        err = PARSER_ERR_UNKNOWN;
        goto bail;
    }
    playState.readMode = readMode;
    playState.playRate = 0; /* halting */
    playState.IParser = IParser;
    playState.parserHandle = parserHandle;
    playState.numTracks = num_tracks;
    playState.tracks = tracks;

    hTask = startTask(&playState);
    if (NULL == hTask) {
        PARSER_ERROR("Fail to start parser task!\n");
        err = PARSER_ERR_UNKNOWN;
        goto bail;
    }

    printf("\n************************************************************\n");
    printf("     PLAYBACK\n");
    printf("************************************************************\n");

    /***************************************************************************
                run message loop
    ***************************************************************************/
    while (1) {
        printf("Please enter command: \n\
                'p' normal playback from current position.\n\
                'f' fast forward from current position.\n\
                'b' rewind from current position.\n\
                'u' pause, halt the task & wait for next command.\n\
                's' seek to a particular position.\n\
                't' intensive test on seeking & trick mode. \n\
                'r' intensive test on seeking & trick mode for regression. \n\
                'm' set read mode. \n\
                'e' enable track. \n\
                'd' disable track. \n\
                'x' to exit.\n");

        scanOneCommand(&fpUserCommands, &rep[0]);

        if (rep[0] == 'e') {
            if (rep[1] == 0) {
                continue;
            }

            track_num = atoi(&rep[1]);
            ParserLock();
            IParser->enableTrack(parserHandle, track_num, 1);
            ParserUnLock();

            g_abTrackDisabledByCommand[track_num] = FALSE;
        } else if (rep[0] == 'd') {
            if (rep[1] == 0) {
                continue;
            }

            track_num = atoi(&rep[1]);
            ParserLock();
            IParser->enableTrack(parserHandle, track_num, 0);
            ParserUnLock();

            g_abTrackDisabledByCommand[track_num] = TRUE;
        } else if (rep[0] == 'p') {
            printf("\nPlay ******************\n");
            err = pauseTask(hTask, &playState);
            if (err)
                goto bail;
            err = enableAudioTracks(IParser, parserHandle, num_tracks, tracks, TRUE);
            if (err)
                goto bail;
            playState.playRate = 1;

            // set stop us seconds here.

            // first check if it is the percentage
            playState.usMovieStopTime = 0;
            if (strlen(rep) > 1) {
                for (j = 1; j < strlen(rep); j++)
                    if (rep[j] == '%')
                        break;
                // percentage
                if (j < strlen(rep)) {
                    rep[j] = 0;
                    playState.usMovieStopTime = (uint64)(atof(&rep[1]) / 100 * movie_usduration);
                } else if ('!' != rep[1])
                    playState.usMovieStopTime = (uint64)(atof(&rep[1]) * 1000000);
            }

            hTask = startTask(&playState);
            if (1 < strlen(rep))  // && ('!' == rep[1]))
            {
                err = fsl_osal_thread_join(hTask);
                hTask = NULL;
            }

        } else if (rep[0] == 'f') {
            printf("\nFast Forward ******************\n");
            if (!isMovieSeekable) {
                printf("Error:This movie is not seekable!\n");
                continue;
            }

            if (((PARSER_READ_MODE_TRACK_BASED == readMode) && !IParser->getNextSyncSample) ||
                ((PARSER_READ_MODE_FILE_BASED == readMode) && !IParser->getFileNextSyncSample)) {
                printf("Warning:current reading mode (%u) can not support fast forword!\n",
                       readMode);
                continue;
            }
            err = pauseTask(hTask, &playState);
            if (err)
                goto bail;
            err = enableAudioTracks(IParser, parserHandle, num_tracks, tracks, FALSE);
            if (err)
                goto bail;
            playState.playRate = 2;

            playState.usMovieStopTime = 0;
            if (strlen(rep) > 1) {
                for (j = 1; j < strlen(rep); j++)
                    if (rep[j] == '%')
                        break;
                // percentage
                if (j < strlen(rep)) {
                    rep[j] = 0;
                    playState.usMovieStopTime = (uint64)(atof(&rep[1]) / 100 * movie_usduration);
                } else if ('!' != rep[1])
                    playState.usMovieStopTime = (uint64)(atof(&rep[1]) * 1000000);
            }

            hTask = startTask(&playState);

            if (1 < strlen(rep)) {
                err = fsl_osal_thread_join(hTask);
                hTask = NULL;
            }
        }

        else if (rep[0] == 'b') {
            printf("\nRewind ******************\n");
            if (!isMovieSeekable) {
                printf("Error:This movie is not seekable!\n");
                continue;
            }

            if (((PARSER_READ_MODE_TRACK_BASED == readMode) && !IParser->getNextSyncSample) ||
                ((PARSER_READ_MODE_FILE_BASED == readMode) && !IParser->getFileNextSyncSample)) {
                printf("Warning:current reading mode (%u) can not support fast forword!\n",
                       readMode);
                continue;
            }
            err = pauseTask(hTask, &playState);
            if (err)
                goto bail;
            err = enableAudioTracks(IParser, parserHandle, num_tracks, tracks, FALSE);
            if (err)
                goto bail;
            playState.playRate = -1;

            playState.usMovieStopTime = 0;
            if (strlen(rep) > 1) {
                for (j = 1; j < strlen(rep); j++)
                    if (rep[j] == '%')
                        break;
                // percentage
                if (j < strlen(rep)) {
                    rep[j] = 0;
                    playState.usMovieStopTime = (uint64)(atof(&rep[1]) / 100 * movie_usduration);
                } else if ('!' != rep[1])
                    playState.usMovieStopTime = (uint64)(atof(&rep[1]) * 1000000);
            }
            hTask = startTask(&playState);

            if (1 < strlen(rep)) {
                err = fsl_osal_thread_join(hTask);
                hTask = NULL;
            }
        }

        if (rep[0] == 'u') {
            err = fsl_osal_mutex_lock(playState.hPlayMutex);
            if (err)
                goto bail;
            playState.playRate = 0;
            err = fsl_osal_mutex_unlock(playState.hPlayMutex);
            if (err)
                goto bail;
        }

        else if (rep[0] == 's') {
            printf("\nSeek ******************\n");
            if (!isMovieSeekable) {
                printf("Error:This movie is not seekable!\n");
                continue;
            }

            printf("Please select mode:\n\
                    '1' for non-accurate seek \n\
                    '2' for accurate seek \n\
                    '3' for self-defined test\n"); /* 1 & 2 to immitate application behavior, 3 for
                                                      self-test. */
            scanOneCommand(&fpUserCommands, rep);

            seek_mode = atoi(rep);
            if (seek_mode < 1 || seek_mode > 3) {
                printf("Invalid seek mode %d !\n", seek_mode);
                continue;
            }

            err = pauseTask(hTask, &playState);
            if (err)
                goto bail;

            printf("Please input seek position, in seconds or percentage by %%.\n");
            scanOneCommand(&fpUserCommands, rep);

            for (j = 0; j < strlen(rep); j++)
                if (rep[j] == '%')
                    break;
            if (j < strlen(rep))  // percentage
            {
                rep[j] = 0;
                seek_secus = (uint64)(atof(rep) / 100 * movie_usduration);
            } else
                seek_secus = (uint64)(atof(rep) * 1000000);  // debug change s to ms1); //

            if (1 == seek_mode) {
                err = SeekMovie(IParser, parserHandle, num_tracks, tracks, seek_secus, FALSE);
            } else if (2 == seek_mode) {
                err = SeekMovie(IParser, parserHandle, num_tracks, tracks, seek_secus, TRUE);
            } else {
                printf("Please select time request:\n\
                    '1' for SEEK_FLAG_NEAREST \n\
                    '2' for SEEK_FLAG_NO_LATER \n\
                    '3' for SEEK_FLAG_NO_EARLIER\n");
                scanOneCommand(&fpUserCommands, rep);

                seek_flag = atoi(rep);
                if (seek_flag < 1 || seek_flag > 3) {
                    printf("Invalid seek flag %d !\n", seek_flag);
                    continue;
                }
                printf("Please select a track number,'a' for all track simutaneously.\n");
                scanOneCommand(&fpUserCommands, rep);

                if (rep[0] == 'a') {
                    for (i = 0; i < num_tracks; i++)
                        err = SeekTrack(IParser, parserHandle, &tracks[i], seek_secus, seek_flag);
                } else {
                    track_num = atoi(rep);
                    if (track_num > num_tracks) {
                        printf("Please select an available track.\n");
                        continue;
                    } else {
                        err = SeekTrack(IParser, parserHandle, &tracks[track_num], seek_secus,
                                        seek_flag);
                    }
                }
            }

            playState.playRate = 0; /* let the task halt & wait for new command to set a rate */
            hTask = startTask(&playState);
        } else if (rep[0] == 't') {
            uint32 seekingCount;
            uint64 usMovieDuraiton;
            uint64 usSeekingInterval;
            int32 playRate;

            if (!isMovieSeekable) {
                printf("Error:This movie is not seekable!\n");
                continue;
            }

            err = IParser->getMovieDuration(parserHandle, &usMovieDuraiton);
            if (err || !usMovieDuraiton) {
                printf("Error:can not get a valid movie duration!\n");
                continue;
            }

            printf("Please enter the number of seeking points across the movie (1~500):\n");
            scanOneCommand(&fpUserCommands, rep);
            seekingCount = atoi(rep);
            if (!seekingCount || (500 < seekingCount)) {
                printf("Error: Invalid seeking count!\n");
                continue;
            }
            usSeekingInterval = usMovieDuraiton / seekingCount;
            printf("Seeking interval is %lld us (%lld sec)\n", usSeekingInterval,
                   usSeekingInterval / 1000000);

            printf("Please enter the play rate (1 for normal rate, 2 for FF and -1 for "
                   "Rewinding):\n");
            scanOneCommand(&fpUserCommands, rep);
            playRate = atoi(rep);
            if ((1 != playRate) && (-1 != playRate) && (2 != playRate)) {
                printf("Error: Invalid play rate!\n");
                continue;
            }
            usSeekingInterval = usMovieDuraiton / seekingCount;
            printf("Seeking interval is %lld us (%lld sec)\n", usSeekingInterval,
                   usSeekingInterval / 1000000);

            seekingCount += 2; /* make sure exceeding EOS */
            for (i = 0; i < seekingCount; i++) {
                err = pauseTask(hTask, &playState);
                if (err)
                    goto bail;
                seek_secus = (usSeekingInterval * i);
                printf("\n\nSeek count %u, desired time %lld us (%lld sec)\n", i,
                       usSeekingInterval * i, seek_secus);
                err = SeekMovie(IParser, parserHandle, num_tracks, tracks, seek_secus, FALSE);
                if (1 == playRate) {
                    err = enableAudioTracks(IParser, parserHandle, num_tracks, tracks, TRUE);
                    if (err)
                        goto bail;
                } else {
                    err = enableAudioTracks(IParser, parserHandle, num_tracks, tracks, FALSE);
                    if (err)
                        goto bail;
                }
                playState.playRate = playRate;
                hTask = startTask(&playState);
                M_SLEEP(10000);
            }

            printf("\n\nSeeking test ends at play rate %d!\n", playRate);
            err = pauseTask(hTask, &playState);
            if (err)
                goto bail;
            /* seek to the start of movie, haulting */
            err = SeekMovie(IParser, parserHandle, num_tracks, tracks, 0, FALSE);
            if (err)
                goto bail;
            playState.playRate = 0;
            hTask = startTask(&playState);
        } else if (rep[0] == 'm') {
            int defaultReadMode = 0;
            int readMode = 0;
            ParserLock();
            err = IParser->getReadMode(parserHandle, &defualtReadMode);
            ParserUnLock();
            if (PARSER_SUCCESS != err)
                goto bail;

            printf("default mode is %d\n", defaultReadMode);
            if (defaultReadMode == PARSER_READ_MODE_FILE_BASED)
                readMode = PARSER_READ_MODE_TRACK_BASED;
            else
                readMode = PARSER_READ_MODE_FILE_BASED;

            if ((readMode = PARSER_READ_MODE_TRACK_BASED) &&
                (isStreaming || !strcmp(parserRole, "parser.mpg2"))) {
                printf("track_based mode not supported, stop test\n");
                err = pauseTask(hTask, &playState);
                break;
            }

            ParserLock();
            err = IParser->setReadMode(parserHandle, readMode);
            ParserUnLock();
            if (PARSER_SUCCESS != err) {
                PARSER_ERROR("ERR! Can not set readMode %d\n", readMode);
                err = pauseTask(hTask, &playState);
                break;
            }
            printf("set readMode to %d\n", readMode);
            playState.readMode = readMode;

        } else if (rep[0] == 'r') {
            uint32 seekingCount;
            uint64 usMovieDuraiton;
            uint64 usSeekingInterval;
            int32 playRate;

            if (!isMovieSeekable) {
                printf("Error:This movie is not seekable!\n");
                continue;
            }

            err = IParser->getMovieDuration(parserHandle, &usMovieDuraiton);
            if (err || !usMovieDuraiton) {
                printf("Error:can not get a valid movie duration!\n");
                continue;
            }

            printf("Please enter the number of seeking points across the movie (1~500):\n");
            scanOneCommand(&fpUserCommands, rep);
            seekingCount = atoi(rep);
            if (!seekingCount || (500 < seekingCount)) {
                printf("Error: Invalid seeking count!\n");
                continue;
            }
            usSeekingInterval = usMovieDuraiton / seekingCount;
            printf("Seeking interval is %lld us (%lld sec)\n", usSeekingInterval,
                   usSeekingInterval / 1000000);

            printf("Please enter the play rate (1 for normal rate, 2 for FF and -1 for "
                   "Rewinding):\n");
            scanOneCommand(&fpUserCommands, rep);
            playRate = atoi(rep);
            if ((1 != playRate) && (-1 != playRate) && (2 != playRate)) {
                printf("Error: Invalid play rate!\n");
                continue;
            }
            printf("read mode %s\n",
                   playState.readMode == PARSER_READ_MODE_TRACK_BASED ? "track" : "file");

            seekingCount += 2; /* make sure exceeding EOS */
            for (i = 0; i < seekingCount; i++) {
                err = pauseTask(hTask, &playState);
                if (err)
                    goto bail;
                seek_secus = (usSeekingInterval * i);
                printf("\n\nSeek count %u, desired time %lld us (%lld sec)\n", i,
                       usSeekingInterval * i, seek_secus);
                err = SeekMovie(IParser, parserHandle, num_tracks, tracks, seek_secus, FALSE);
                if (1 == playRate) {
                    err = enableAudioTracks(IParser, parserHandle, num_tracks, tracks, TRUE);
                    if (err)
                        goto bail;
                } else {
                    err = enableAudioTracks(IParser, parserHandle, num_tracks, tracks, FALSE);
                    if (err)
                        goto bail;
                }
                playState.playRate = playRate;
                playState.usMovieStopTime =
                        seek_secus + (playRate > 0 ? 5 * 1000000 : -5 * 1000000);
                printf("stop time %lld\n", playState.usMovieStopTime);
                hTask = startTask(&playState);
                err = fsl_osal_thread_join(hTask);
                hTask = NULL;
            }

            printf("\n\nSeeking test ends at play rate %d!\n", playRate);
            err = pauseTask(hTask, &playState);
            if (err)
                goto bail;
            /* seek to the start of movie, haulting */

            printf("seek movie to 0\n");
            err = SeekMovie(IParser, parserHandle, num_tracks, tracks, 0, FALSE);
            if (err)
                goto bail;

            playState.playRate = 0;
            printf("start task\n");
            hTask = startTask(&playState);
        } else if (rep[0] == 'x') {
            err = pauseTask(hTask, &playState);
            break;
        }

        else if (rep[0] == '#') {
            M_SLEEP(2000);
        } else if (rep[0] == '*') {
            M_SLEEP(10000);
        }
    }

bail:

    if (playState.hPlayMutex)
        fsl_osal_mutex_destroy(playState.hPlayMutex);

#ifdef MULTI_PROGRAM_TEST
    if (g_hParserMutex)
        fsl_osal_mutex_destroy(g_hParserMutex);
#endif

    for (i = 0; i < num_tracks; i++) {
        freeTrackBuffers(&tracks[i]);
    }

    return err;
}

static void displayHelp() {
    printf("Usage:\n");
    printf("\t a. Local playback:");
    printf("\t test_parser.exe <test file path>\n");
    printf("\n");
    printf("\t b. Streaming:     ");
    printf("\t test_parser.exe live <test file path>\n");
    printf("\n");
    printf("\t c. Batch test:     ");
    printf("\t test_parser.exe -c=<configuration file path>\n");
    printf("\t\t\t\t eg. test_parser.exe -c=./config_example.txt\n\n");
    printf(HELP_DELIMITER);
    printf("In the configuration file, you can set the following properties:\n");
    printf(HELP_DELIMITER);
    printf("* %s: true to immitate a live source, false for a local file.\n", IS_LIVE_SOURCE);
    printf(HELP_DELIMITER);
    printf("* %s: Test file path, to test a single clip\n", CLIP_NAME);
    printf(HELP_DELIMITER);
    printf("* %s: External clip list file path, for mass test.  You mush either set the %s or %s. "
           "\n",
           CLIP_LIST, CLIP_NAME, CLIP_LIST);
    printf(HELP_DELIMITER);
    printf("* %s: External user commands file path, for mass test. Optional.\n", USER_COMMAND_LIST);

    printf(HELP_DELIMITER);
    printf("* %s: Max size in bytes of the video queue, for GStreamer style. Optional.\n",
           VIDEO_QUEUE_MAX_SIZE_BYTES);
    printf(HELP_DELIMITER);
    printf("* %s: Max size in buffers of the video queue, for DShow style. Optional.\n",
           VIDEO_QUEUE_MAX_SIZE_BUFFERS);
    printf(HELP_DELIMITER);
    printf("* %s: Single buffer size of the video queue, for DShow style. Optional.\n",
           VIDEO_QUEUE_SINGLE_BUFFER_SIZE);

    printf(HELP_DELIMITER);
    printf("* %s: Max size in bytes of the audio queue, for GStreamer style. Optional.\n",
           AUDIO_QUEUE_MAX_SIZE_BYTES);
    printf(HELP_DELIMITER);
    printf("* %s: Max size in buffers of the audio queue, for DShow style. Optional.\n",
           AUDIO_QUEUE_MAX_SIZE_BUFFERS);
    printf(HELP_DELIMITER);
    printf("* %s: Single buffer size of the audio queue, for DShow style. Optional.\n",
           AUDIO_QUEUE_SINGLE_BUFFER_SIZE);

    printf(HELP_DELIMITER);
    printf("* %s: Max size in bytes of the text(subtitle) queue, for GStreamer style. Optional.\n",
           TEXT_QUEUE_MAX_SIZE_BYTES);
    printf(HELP_DELIMITER);
    printf("* %s: Max size in buffers of the text(subtitle) queue, for DShow style. Optional.\n",
           TEXT_QUEUE_MAX_SIZE_BUFFERS);
    printf(HELP_DELIMITER);
    printf("* %s: Single buffer size of the text(subtitle) queue, for DShow style. Optional.\n",
           TEXT_QUEUE_SINGLE_BUFFER_SIZE);
    printf(HELP_DELIMITER);
    printf("                                                                                   "
           "\"\"\"\n");
    printf(HELP_DELIMITER);
    printf("Note: If no queue property is set, default GStreamer style settings will be used "
           "<(^-^)>\n");
}

static int32 parseCommandLine(uint32 argc, char* argv[], char* sourceUrl, FILE** fpSourceList,
                              char* userCommandsFilePath, bool* isLive,
                              tQueueSettings* videoQueueSettings,
                              tQueueSettings* audioQueueSettings,
                              tQueueSettings* textQueueSettings) {
    int32 err = PARSER_SUCCESS;
    uint32 i;
    FILE* fpConfig = NULL;
    FILE* fpClipList = NULL;
    char filePathTemp[255];

    sourceUrl[0] = 0;
    *fpSourceList = NULL;
    userCommandsFilePath[0] = 0;
    *isLive = FALSE;
    memset(videoQueueSettings, 0, sizeof(tQueueSettings));
    memset(audioQueueSettings, 0, sizeof(tQueueSettings));
    memset(textQueueSettings, 0, sizeof(tQueueSettings));

    for (i = 1; i < argc; i++) /* skip the 1st argument, program name */
    {
        if (!memcmp(CONFIG_OPTION, argv[i], sizeof(CONFIG_OPTION) - 1)) {
            char argument[512];
            fpConfig = fopen(argv[i] + sizeof(CONFIG_OPTION), "r");
            if (!fpConfig) {
                PARSER_ERROR("Fail to open config file %s\n", argv[i] + sizeof(CONFIG_OPTION));
                BAILWITHERROR(-1);
            }

            argument[0] = 0;
            while (EOF != fscanf(fpConfig, "%s", argument)) {
                if (!memcmp(IS_LIVE_SOURCE, argument, sizeof(IS_LIVE_SOURCE) - 1)) {
                    if (!strcmp("true", argument + sizeof(IS_LIVE_SOURCE)) ||
                        !strcmp("TRUE", argument + sizeof(IS_LIVE_SOURCE)) ||
                        !strcmp("True", argument + sizeof(IS_LIVE_SOURCE))) {
                        PARSER_INFO(PARSER_INFO_STREAM, "Simulate streaming\n");
                        *isLive = TRUE;
                    } else {
                        printf("Test local playback\n");
                        *isLive = FALSE;
                    }
                }

                else if (!memcmp(DUMP_TRACK_DATA, argument, sizeof(DUMP_TRACK_DATA) - 1)) {
                    if (!strcmp("false", argument + sizeof(DUMP_TRACK_DATA)) ||
                        !strcmp("FALSE", argument + sizeof(DUMP_TRACK_DATA)) ||
                        !strcmp("False", argument + sizeof(DUMP_TRACK_DATA))) {
                        printf("Will not dump track data\n");
                        g_dump_track_data = FALSE;
                    } /* by default is true */
                }

                else if (!memcmp(DUMP_TRACK_PTS, argument, sizeof(DUMP_TRACK_PTS) - 1)) {
                    if (!strcmp("true", argument + sizeof(DUMP_TRACK_PTS)) ||
                        !strcmp("TRUE", argument + sizeof(DUMP_TRACK_PTS)) ||
                        !strcmp("True", argument + sizeof(DUMP_TRACK_PTS))) {
                        printf("Will dump track PTS\n");
                        g_dump_track_pts = TRUE;
                    } /* by default is false */
                }

                else if (!memcmp(EXPORT_INDEX_TABLE, argument, sizeof(EXPORT_INDEX_TABLE) - 1)) {
                    if (!strcmp("false", argument + sizeof(EXPORT_INDEX_TABLE)) ||
                        !strcmp("FALSE", argument + sizeof(EXPORT_INDEX_TABLE)) ||
                        !strcmp("False", argument + sizeof(EXPORT_INDEX_TABLE))) {
                        printf("Will not export index\n");
                        g_export_index_table = FALSE;
                    } /* by default is true */
                }

                else if (!memcmp(IMPORT_AVAILABLE_INDEX_TABLE, argument,
                                 sizeof(IMPORT_AVAILABLE_INDEX_TABLE) - 1)) {
                    if (!strcmp("false", argument + sizeof(IMPORT_AVAILABLE_INDEX_TABLE)) ||
                        !strcmp("FALSE", argument + sizeof(IMPORT_AVAILABLE_INDEX_TABLE)) ||
                        !strcmp("False", argument + sizeof(IMPORT_AVAILABLE_INDEX_TABLE))) {
                        printf("Will not import index\n");
                        g_import_available_index_table = FALSE;
                    } /* by default is true */
                }

                else if (!memcmp(CLIP_NAME, argument, sizeof(CLIP_NAME) - 1)) {
                    if (!fpClipList) /* if not set the clip list yet */
                    {
                        strcpy(sourceUrl, argument + sizeof(CLIP_NAME));
                    }
                } else if (!memcmp(CLIP_LIST, argument, sizeof(CLIP_LIST) - 1)) {
                    if (!sourceUrl[0]) /* if not set the single clip name yet */
                    {
                        strcpy(filePathTemp, argument + sizeof(CLIP_LIST));
                        fpClipList = fopen(filePathTemp, "r");
                        if (!fpClipList) {
                            printf("Invalid Clip list file %s!\n", filePathTemp);
                            BAILWITHERROR(PARSER_ERR_INVALID_PARAMETER)
                        }
                        *fpSourceList = fpClipList;
                    }
                }

                else if (!memcmp(FIRST_CLIP_NUMBER, argument, sizeof(FIRST_CLIP_NUMBER) - 1)) {
                    uint32 firstClipNumber = atoi(argument + sizeof(FIRST_CLIP_NUMBER));
                    printf("first clip number: %u\n", firstClipNumber);
                    g_first_clip_number = firstClipNumber;
                }

                else if (!memcmp(LAST_CLIP_NUMBER, argument, sizeof(LAST_CLIP_NUMBER) - 1)) {
                    uint32 lastClipNumber = atoi(argument + sizeof(LAST_CLIP_NUMBER));
                    printf("last clip number: %u\n", lastClipNumber);
                    g_last_clip_number = lastClipNumber;
                }

                else if (!memcmp(MAX_PTS_GITTER_IN_MS, argument,
                                 sizeof(MAX_PTS_GITTER_IN_MS) - 1)) {
                    uint32 msMaxPtsGitter = atoi(argument + sizeof(MAX_PTS_GITTER_IN_MS));
                    g_usMaxPTSGitter = msMaxPtsGitter * 1000;
                    if (0 > (int64)g_usMaxPTSGitter) {
                        printf("Invalid max PTS gitter: %lld us\n", g_usMaxPTSGitter);
                        g_usMaxPTSGitter = 0;
                    } else
                        printf("Max PTS gitter: %lld us\n", g_usMaxPTSGitter);
                }

                else if (!memcmp(USER_COMMAND_LIST, argument, sizeof(USER_COMMAND_LIST) - 1)) {
                    strcpy(userCommandsFilePath, argument + sizeof(USER_COMMAND_LIST));
                    printf("User command list file: %s\n", userCommandsFilePath);
                }

                /* video queue settings */
                else if (!memcmp(VIDEO_QUEUE_MAX_SIZE_BYTES, argument,
                                 sizeof(VIDEO_QUEUE_MAX_SIZE_BYTES) - 1)) {
                    videoQueueSettings->maxSizeBytes =
                            atoi(argument + sizeof(VIDEO_QUEUE_MAX_SIZE_BYTES));
                    DBGMSG("video queue max size: %u bytes\n", videoQueueSettings->maxSizeBytes);
                } else if (!memcmp(VIDEO_QUEUE_MAX_SIZE_BUFFERS, argument,
                                   sizeof(VIDEO_QUEUE_MAX_SIZE_BUFFERS) - 1)) {
                    videoQueueSettings->maxSizeBuffers =
                            atoi(argument + sizeof(VIDEO_QUEUE_MAX_SIZE_BUFFERS));
                    DBGMSG("video queue max size: %u buffers\n",
                           videoQueueSettings->maxSizeBuffers);
                } else if (!memcmp(VIDEO_QUEUE_SINGLE_BUFFER_SIZE, argument,
                                   sizeof(VIDEO_QUEUE_SINGLE_BUFFER_SIZE) - 1)) {
                    videoQueueSettings->singleBufferSize =
                            atoi(argument + sizeof(VIDEO_QUEUE_SINGLE_BUFFER_SIZE));
                    DBGMSG("video queue single buffer size: %u bytes\n",
                           videoQueueSettings->singleBufferSize);
                }
                /* audio queue settings */
                else if (!memcmp(AUDIO_QUEUE_MAX_SIZE_BYTES, argument,
                                 sizeof(AUDIO_QUEUE_MAX_SIZE_BYTES) - 1)) {
                    audioQueueSettings->maxSizeBytes =
                            atoi(argument + sizeof(AUDIO_QUEUE_MAX_SIZE_BYTES));
                    DBGMSG("audio queue max size: %u bytes\n", audioQueueSettings->maxSizeBytes);
                } else if (!memcmp(AUDIO_QUEUE_MAX_SIZE_BUFFERS, argument,
                                   sizeof(AUDIO_QUEUE_MAX_SIZE_BUFFERS) - 1)) {
                    audioQueueSettings->maxSizeBuffers =
                            atoi(argument + sizeof(AUDIO_QUEUE_MAX_SIZE_BUFFERS));
                    DBGMSG("audio queue max size: %u buffers\n",
                           audioQueueSettings->maxSizeBuffers);
                } else if (!memcmp(AUDIO_QUEUE_SINGLE_BUFFER_SIZE, argument,
                                   sizeof(AUDIO_QUEUE_SINGLE_BUFFER_SIZE) - 1)) {
                    audioQueueSettings->singleBufferSize =
                            atoi(argument + sizeof(AUDIO_QUEUE_SINGLE_BUFFER_SIZE));
                    DBGMSG("audio queue single buffer size: %u bytes\n",
                           audioQueueSettings->singleBufferSize);
                }

                /* text queue settings */
                else if (!memcmp(TEXT_QUEUE_MAX_SIZE_BYTES, argument,
                                 sizeof(TEXT_QUEUE_MAX_SIZE_BYTES) - 1)) {
                    textQueueSettings->maxSizeBytes =
                            atoi(argument + sizeof(TEXT_QUEUE_MAX_SIZE_BYTES));
                    DBGMSG("text queue max size: %u bytes\n", textQueueSettings->maxSizeBytes);
                } else if (!memcmp(TEXT_QUEUE_MAX_SIZE_BUFFERS, argument,
                                   sizeof(TEXT_QUEUE_MAX_SIZE_BUFFERS) - 1)) {
                    textQueueSettings->maxSizeBuffers =
                            atoi(argument + sizeof(TEXT_QUEUE_MAX_SIZE_BUFFERS));
                    DBGMSG("text queue max size: %u buffers\n", textQueueSettings->maxSizeBuffers);
                } else if (!memcmp(TEXT_QUEUE_SINGLE_BUFFER_SIZE, argument,
                                   sizeof(TEXT_QUEUE_SINGLE_BUFFER_SIZE) - 1)) {
                    textQueueSettings->singleBufferSize =
                            atoi(argument + sizeof(TEXT_QUEUE_SINGLE_BUFFER_SIZE));
                    DBGMSG("text queue single buffer size: %u bytes\n",
                           textQueueSettings->singleBufferSize);
                } else if (!memcmp(PARSER_LIB_PATH_A, argument, sizeof(PARSER_LIB_PATH_A) - 1)) {
                    strcpy(g_parser_lib_path_a, argument + sizeof(PARSER_LIB_PATH_A));
                    DBGMSG("parser lib path is %s\n", g_parser_lib_path_a);
                } else if (!memcmp(PARSER_LIB_PATH_B, argument, sizeof(PARSER_LIB_PATH_B) - 1)) {
                    strcpy(g_parser_lib_path_b, argument + sizeof(PARSER_LIB_PATH_B));
                    DBGMSG("parser lib path is %s\n", g_parser_lib_path_b);
                }

                argument[0] = 0;
            }

        } else if (!strcmp("live", argv[i])) {
            printf("This is a live source\n");
            *isLive = TRUE;
        } else if (!strcmp("keep-data", argv[i])) {
            printf("g_keepData is true\n");
            g_keepData = TRUE;
        } else if (!strcmp("-h", argv[i])) {
            displayHelp();
        } else {
            strcpy(sourceUrl, argv[i]);
        }
    }

    if (!sourceUrl[0] && !(*fpSourceList)) {
        printf("Invalid parameter: Neither single clip path or clip list file is specified.\n");
        err = PARSER_ERR_INVALID_PARAMETER;
    }

bail:
    if (fpConfig)
        fclose(fpConfig);

    return err;
}
__attribute__((unused))
static void* scanTask(void* arg) {
    int err;
    int ret;
    FslParserInterface* IParser = (FslParserInterface*)arg;
    FslParserHandle hParser;
    uint32 dwContext = 1;
    uint64 qwDuration;
    uint32 dwSize = 0;
    uint8* pbyBuf = NULL;
    FILE* fp = NULL;

    err = IParser->createParser(g_isLiveSource, &g_fileOps, &g_memOps, &g_outputBufferOps,
                                (void*)&dwContext, &hParser);

    if (PARSER_SUCCESS != err) {
        PARSER_ERROR("scanTask, fail to create the parser, err %d\n", err);
        goto bail;
    }

    err = IParser->getTrackDuration(hParser, 0, &qwDuration);
    if (PARSER_SUCCESS != err) {
        PARSER_ERROR("scanTask, fail to getTrackDuration, err %d\n", err);
        goto bail;
    }

    printf("scanTask, duration %lld\n", qwDuration);

    err = IParser->exportIndex(hParser, NULL, &dwSize);
    if (PARSER_SUCCESS != err) {
        PARSER_ERROR("scanTask, aaa fail to exportIndex, err %d\n", err);
        goto bail;
    }

    pbyBuf = g_memOps.Malloc(dwSize);
    if (pbyBuf == NULL) {
        err = -1;
        PARSER_ERROR("scanTask, fail to Malloc %d bytes\n", dwSize);
        goto bail;
    }
    err = IParser->exportIndex(hParser, pbyBuf, &dwSize);
    if (PARSER_SUCCESS != err) {
        PARSER_ERROR("scanTask, aaa fail to exportIndex, err %d\n", err);
        goto bail;
    }

    fp = fopen(index_file_name, "wb");
    if (fp == NULL) {
        err = -1;
        PARSER_ERROR("scanTask, open %s failed\n", index_file_name);
        goto bail;
    }

    fwrite(&dwSize, 1, sizeof(dwSize), fp);

    ret = fwrite(pbyBuf, 1, dwSize, fp);
    if (ret != (int)dwSize) {
        err = -1;
        PARSER_ERROR("scanTask, fwrite %s failed, %d, %d\n", index_file_name, ret, dwSize);
        goto bail;
    }

    fflush(fp);

    fclose(fp);

    IParser->deleteParser(hParser);

    printf("scanTask ok =================\n");

bail:
    return NULL;
}

static int32 testOneClip(char* source_url, char* userCommandsFilePath,
                         tQueueSettings* videoQueueSettings, tQueueSettings* audioQueueSettings,
                         tQueueSettings* textQueueSettings, TEST_SEQUENCE sequence) {
    int32 err = PARSER_SUCCESS;

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    uint32 start_time;
    uint32 end_time;
#else
    struct timeval start_time;
    struct timeval end_time;
    struct timezone time_zone;
#endif
#endif

    FILE* fpUserCommands = NULL;

    FslParserInterface* IParser = NULL;
    HANDLE parserDllHandle = NULL;
    FslParserHandle parserHandle = NULL;
    char parserRole[32] = {0};

    FslFileHandle sourceFileHandle = NULL;

    FslFileStream fileOps;
    ParserMemoryOps memOps;
    ParserOutputBufferOps outputBufferOps;

    /* movie properties */
    bool seekable;
    uint64 usMovieDuration;
    uint64 usDuration;
    uint32 minutes;
    uint32 seconds;
    uint32 num_tracks = 0;
    uint32 num_programs = 0;

    uint32 trackType; /* track's media type */
    uint32 decoderType;
    uint32 decoderSubtype;

    uint32 videoFrameWidth;
    uint32 videoFrameHeight;
    uint32 videoFrameRateNumerator;
    uint32 videoFrameRateDenorminator;

    uint32 audioNumChannels;
    uint32 audioSampleRate;
    uint32 audioBitsPerSample;
    uint32 audioBlockAlign;
    uint32 audioBitsPerFrame;
    uint32 audioChannelMask;

    uint32 textTrackWidth;
    uint32 textTrackHeight;

    uint32 bitRate = 0;
    uint8 language[4];

    uint8* decoderSpecificInfo;
    uint32 decoderSpecificInfoSize;

    uint32 firstBSACTrackNum = PARSER_INVALID_TRACK_NUMBER;

    uint32 i;

    bool isStreaming = FALSE;

    char md5FileName[32] = {0};

    mm_mm_init();

    resetErrorStatus();

#ifdef TIME_PROFILE
    reset_GTimeInfo();
#endif

    fpErrLog = fopen(ERR_LOG_FILE_NAME, "a");
    if (!fpErrLog) {
        PARSER_ERROR("Fail to open err log file\n");
        goto bail;
    }

    fprintf(fpErrLog, "\n ============ test %s\n", source_url);

    sourceFileHandle = OpenSourceFile(source_url); /* open outside for performance test */
    if (!sourceFileHandle) {
        BAILWITHERROR(PARSER_FILE_OPEN_ERROR)
    }

    if (sequence == TEST_SEQUENCE_A)
        strcpy(md5FileName, MD5_FILE_NAME_A);
    else
        strcpy(md5FileName, MD5_FILE_NAME_B);

    fpStreamInfo = fopen(STREAM_INFO_FILE_NAME, "w");
    if (!fpStreamInfo) {
        PARSER_ERROR("Fail to open stream info file\n");
        BAILWITHERROR(PARSER_FILE_OPEN_ERROR)
    }

    MakeIndexDumpFileName(source_url);

    /* query the parser interface */
    g_test_state = TEST_STATE_CREATING_PARSER;
    if (sequence == TEST_SEQUENCE_A)
        err = CreateParserInterface(source_url, &IParser, &parserDllHandle, parserRole,
                                    g_parser_lib_path_a);
    else
        err = CreateParserInterface(source_url, &IParser, &parserDllHandle, parserRole,
                                    g_parser_lib_path_b);

    if (PARSER_SUCCESS != err) {
        printf("Error: Fail to get the parser interface, err %d\n", err);
        goto bail;
    }
    printf("Parser version: %s \n", IParser->getVersionInfo());

    /* create the parser */
    fileOps.Open = (void * (*)(const unsigned char *, const unsigned char *, void *))appLocalFileOpen;
    fileOps.Read = appLocalReadFile;
    fileOps.Seek = appLocalSeekFile;
    fileOps.Tell = appLocalGetCurrentFilePos;
    fileOps.Size = appLocalFileSize;
    fileOps.Close = appLocalFileClose;
    fileOps.CheckAvailableBytes = appLocalCheckAvailableBytes;
    fileOps.GetFlag = appLocalGetFlag;

    memOps.Calloc = fsl_osal_calloc;
    memOps.Malloc = fsl_osal_malloc;
    memOps.Free = fsl_osal_free;
    memOps.ReAlloc = fsl_osal_realloc;

    outputBufferOps.RequestBuffer = appLocalRequestBuffer;
    outputBufferOps.ReleaseBuffer = appLocalReleaseBuffer;

#ifdef AUDIO_PARSER
#ifdef SCAN_THREAD
    g_memOps = memOps;
    g_fileOps = fileOps;
    g_outputBufferOps = outputBufferOps;
    IScanParser = *IParser;
    hScanTask = fsl_osal_create_thread(scanTask, (void*)&IScanParser);
#endif
#endif

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    start_time = timeGetTime();
#else
    gettimeofday(&start_time, &time_zone);
#endif
#endif

    if (IParser->createParser2) {
        uint32 flags;
        flags = FLAG_H264_NO_CONVERT;
        if (g_isLiveSource)
            flags |= (FILE_FLAG_NON_SEEKABLE | FILE_FLAG_READ_IN_SEQUENCE);
        err = IParser->createParser2(flags, &fileOps, &memOps, &outputBufferOps, (void*)appContext,
                                     &parserHandle);
    } else {
        err = IParser->createParser(g_isLiveSource, &fileOps, &memOps, &outputBufferOps,
                                    (void*)appContext, &parserHandle);
    }

    if (PARSER_SUCCESS != err) {
        PARSER_ERROR("fail to create the parser, err %d\n", err);
        goto bail;
    }

#ifdef AUDIO_PARSER
#ifdef SCAN_THREAD
    fsl_osal_thread_join(hScanTask);
    aitFp = fopen(index_file_name, "rb");
    fseek(aitFp, 0, SEEK_END);
    dwAitSize = ftell(aitFp);

    pAitBuf = malloc(dwAitSize);
    if (pAitBuf == NULL) {
        printf("malloc %d bytes failed\n", dwAitSize);
        err = -1;
        goto bail;
    }

    fseek(aitFp, 0, SEEK_SET);
    readSize = fread(pAitBuf, 1, dwAitSize, aitFp);
    if (readSize != dwAitSize) {
        printf("fread %d bytes, actual %d\n", dwAitSize, readSize);
        err = -1;
        goto bail;
    }
    fclose(aitFp);

    if (dwAitSize != *((uint32*)(pAitBuf)) + 4) {
        PARSER_ERROR("fail to importIndex, size %d, %d mismatch\n", dwAitSize,
                     *((uint32*)(pAitBuf)));
        goto bail;
    }

    err = IParser->importIndex(parserHandle, pAitBuf + 4, dwAitSize - 4);
    if (PARSER_SUCCESS != err) {
        PARSER_ERROR("fail to importIndex, err %d\n", err);
        goto bail;
    }
    free(pAitBuf);
#endif
#endif

    if (IParser->getNumPrograms) {
        uint32 dwProgramIdx;
        uint32 dwTrackNum;
        uint32* pdwTrackIdx;
        uint32 i;
        err = IParser->getNumPrograms(parserHandle, &num_programs);
        if (PARSER_SUCCESS != err)
            goto bail;
        printf("======== Number of programs: %d\n", num_programs);

        for (dwProgramIdx = 0; dwProgramIdx < num_programs; dwProgramIdx++) {
            if (IParser->getProgramTracks) {
                err = IParser->getProgramTracks(parserHandle, dwProgramIdx, &dwTrackNum,
                                                &pdwTrackIdx);
                if (PARSER_SUCCESS != err)
                    goto bail;

                printf("program %d, tracknum %d\n", dwProgramIdx, dwTrackNum);

                for (i = 0; i < dwTrackNum; i++) {
                    printf("%d ", pdwTrackIdx[i]);
                }
                printf("\n");
            }
        }
    }

#ifdef TIME_PROFILE
#if defined(__WINCE) || defined(WIN32)
    end_time = timeGetTime();
    g_timeprofile_info.createTimeUs = CALC_DURATION_WINCE;
#else
    gettimeofday(&end_time, &time_zone);
    g_timeprofile_info.createTimeUs = CALC_DURATION_LINUX;
#endif
#endif

    g_test_state = TEST_STATE_PLAYING;

#ifndef AUDIO_PARSER
    err = LoadIndex(IParser, parserHandle);
    if (PARSER_SUCCESS != err) {
        g_export_index_table = FALSE;
        PARSER_ERROR("fail to load index.err %d\n",
                     err); /* GO ahead!Normal playback shall not be affected */
        logIndexError(err);
    }
#endif

    /***************************************************************************************
     *                 movie properties
     ****************************************************************************************/
    PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                     "\n-----------------------------------------------------------------\n");
    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "%s\n", source_url);

    err = IParser->isSeekable(parserHandle, &seekable);
    if (PARSER_SUCCESS != err)
        goto bail;
    if (seekable)
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "File is seekable\n");
    else {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "File is NOT seekable\n");
        logError(RISK_NOT_SEEKABLE);
    }

    err = IParser->getMovieDuration(parserHandle, &usDuration);
    if (PARSER_SUCCESS != err)
        goto bail;
    minutes = (uint32)(usDuration / 60000000);
    seconds = (uint32)(usDuration / 1000000 - minutes * 60);
    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Duration of movie in us: %lld\t", usDuration);
    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "(%d m : %d s)\n", minutes, seconds);
    usMovieDuration = usDuration;
    checkMovieDuration(usMovieDuration);

    if (IParser->getMetaData) {
        err = GetMetaData(IParser, parserHandle);
    } else if (IParser->getUserData) {
        err = GetUserData(IParser, parserHandle);
    }

    err = IParser->getNumTracks(parserHandle, &num_tracks);
    if (PARSER_SUCCESS != err)
        goto bail;
    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Number of tracks: %d\n", num_tracks);

    /***************************************************************************************
     *                track properties
     ****************************************************************************************/
    PARSER_INFO(PARSER_INFO_STREAM, "\n\n     STREAM INFORMATION\n\n");
    memset(tracks, 0, sizeof(tracks));
    for (i = 0; i < num_tracks; i++) {
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\nTrack %d:\n", i);

        tracks[i].IParser = IParser;
        tracks[i].parserHandle = parserHandle;

        tracks[i].trackNum = i;
        tracks[i].seekable = seekable;

        err = IParser->getTrackType(parserHandle, i, &trackType, &decoderType, &decoderSubtype);
        if (PARSER_SUCCESS != err)
            goto bail;
        tracks[i].mediaType = trackType;
        tracks[i].decoderType = decoderType;
        tracks[i].decoderSubtype = decoderSubtype;

        if (g_dump_track_data)
            if (PARSER_SUCCESS != openTrackDataDumpFile(source_url, &tracks[i], sequence))
                goto bail;

        if (g_dump_track_pts)
            if (PARSER_SUCCESS != OpenTrackPtsDumpFile(source_url, &tracks[i], sequence))
                goto bail;

        switch (trackType) {
            case MEDIA_VIDEO:
                displayVideoCodecType(decoderType, decoderSubtype);
                memcpy(&tracks[i].queueSettings, videoQueueSettings, sizeof(tQueueSettings));
                err = IParser->getVideoFrameWidth(parserHandle, i, &videoFrameWidth);
                if (PARSER_SUCCESS != err)
                    goto bail;
                err = IParser->getVideoFrameHeight(parserHandle, i, &videoFrameHeight);
                if (PARSER_SUCCESS != err)
                    goto bail;
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t width: %u, height: %u\n", videoFrameWidth,
                                 videoFrameHeight);
                if (IParser->getVideoFrameRate) {
                    err = IParser->getVideoFrameRate(parserHandle, i, &videoFrameRateNumerator,
                                                     &videoFrameRateDenorminator);
                    if (PARSER_SUCCESS != err)
                        goto bail;
                    if (videoFrameRateNumerator && videoFrameRateDenorminator)
                        PARSER_INFO_SAVE(
                                PARSER_INFO_STREAM,
                                "\t frame rate: %.2f fps (frame duration %lld us)\n",
                                (double)videoFrameRateNumerator / videoFrameRateDenorminator,
                                (uint64)videoFrameRateDenorminator * 1000 * 1000 /
                                        videoFrameRateNumerator);
                    else
                        PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                                         "\t frame rate is unknown! rate %u, scale %u\n",
                                         videoFrameRateNumerator, videoFrameRateDenorminator);
                }
                if (IParser->getVideoScanType) {
                    uint32 videoScanType;
                    err = IParser->getVideoScanType(parserHandle, i, &videoScanType);
                    if (PARSER_SUCCESS == err)
                        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t video scan type: %s\n",
                                         videoScanType == VIDEO_SCAN_PROGRESSIVE ? "Progressive"
                                                                                 : "Interlaced");
                }

                if (VIDEO_H264 == decoderType)
                    tracks[i].isH264Video = TRUE;
                if (VIDEO_MPEG2 == decoderType)
                    tracks[i].isMpeg2Video = TRUE;

                break;

            case MEDIA_AUDIO:
                displayAudioCodecType(decoderType, decoderSubtype);
                memcpy(&tracks[i].queueSettings, audioQueueSettings, sizeof(tQueueSettings));
                err = IParser->getAudioNumChannels(parserHandle, i, &audioNumChannels);
                if (PARSER_SUCCESS != err)
                    goto bail;
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t number of channels: %u\n",
                                 audioNumChannels);
                err = IParser->getAudioSampleRate(parserHandle, i, &audioSampleRate);
                if (PARSER_SUCCESS != err)
                    goto bail;
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t sampling rate: %u HZ\n", audioSampleRate);
                if (IParser->getAudioBitsPerSample) {
                    err = IParser->getAudioBitsPerSample(parserHandle, i, &audioBitsPerSample);
                    if (PARSER_SUCCESS != err)
                        goto bail;
                    checkAudioBitDepth(audioBitsPerSample);
                } else
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t bits per sample: unknown\n");
                if (IParser->getAudioBlockAlign) {
                    err = IParser->getAudioBlockAlign(parserHandle, i, &audioBlockAlign);
                    if (PARSER_SUCCESS != err)
                        goto bail;
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t block alignment: %u\n",
                                     audioBlockAlign);
                }
                if (IParser->getAudioBitsPerFrame) {
                    err = IParser->getAudioBitsPerFrame(parserHandle, i, &audioBitsPerFrame);
                    if (PARSER_SUCCESS != err)
                        goto bail;
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t bits per frame: %u\n",
                                     audioBitsPerFrame);
                }
                if (IParser->getAudioChannelMask) {
                    err = IParser->getAudioChannelMask(parserHandle, i, &audioChannelMask);
                    if (PARSER_SUCCESS != err)
                        goto bail;
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t channel mask: %u\n", audioChannelMask);
                }

                if ((AUDIO_AAC == decoderType) || (AUDIO_MPEG2_AAC == decoderType))
                    tracks[i].isAAC = TRUE;
                if ((AUDIO_AAC == decoderType) && (AUDIO_ER_BSAC == decoderSubtype)) {
                    tracks[i].isBSAC = TRUE;
                    if ((uint32)PARSER_INVALID_TRACK_NUMBER == firstBSACTrackNum)
                        firstBSACTrackNum = i;
                    tracks[i].firstBSACTrackNum = firstBSACTrackNum;
                }

                break;

            case MEDIA_TEXT:

                displayTextCodecType(decoderType, decoderSubtype);
                memcpy(&tracks[i].queueSettings, textQueueSettings, sizeof(tQueueSettings));

                if (!strncmp(&parserRole[0], "parser.mkv", strlen("parser.mkv")))
                    tracks[i].isMKVText = TRUE;

                if (IParser->getTextTrackWidth && IParser->getTextTrackHeight) {
                    err = IParser->getTextTrackWidth(parserHandle, i, &textTrackWidth);
                    if (PARSER_SUCCESS != err)
                        goto bail;
                    err = IParser->getTextTrackHeight(parserHandle, i, &textTrackHeight);
                    if (PARSER_SUCCESS != err)
                        goto bail;
                    PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t text track window (0, 0, %d, %d)\n",
                                     textTrackWidth, textTrackHeight);
                }
                if (IParser->getTextTrackMime) {
                    uint8* mime = NULL;
                    uint32 mime_size = 0;
                    err = IParser->getTextTrackMime(parserHandle, i, &mime, &mime_size);
                    if (PARSER_SUCCESS != err)
                        goto bail;
                    PARSER_INFO(PARSER_INFO_STREAM, "\t text mime=%s,len=%d\n", mime, mime_size);
                }
                break;

            case MEDIA_MIDI:
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "MIDI\n");
                break;

            default:
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Unknown media type\n");
        }

        if (IParser->getBitRate) {
            err = IParser->getBitRate(parserHandle, i, &bitRate);
            if (PARSER_SUCCESS != err)
                goto bail; /* not necessary API for playback */
        }
        if (bitRate)
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t average bit rate: %d bps\n", bitRate);
        else
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t average bit rate: %d (unknown)\n", bitRate);
        tracks[i].bitrate = bitRate;

        language[0] = 0;
        language[1] = 0;
        language[2] = 0;
        language[3] = 0;
        if (IParser->getLanguage) {
            err = IParser->getLanguage(parserHandle, i, language);
            if (PARSER_SUCCESS != err)
                goto bail;
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t language: %s\n", language);
        }

#ifndef AUDIO_PARSER
        err = IParser->getTrackDuration(parserHandle, i, &usDuration);
        if (PARSER_SUCCESS != err)
            goto bail;
        tracks[i].usDuration = usDuration;
        minutes = (uint32)(usDuration / 60000000);
        seconds = (uint32)(usDuration / 1000000 - minutes * 60);
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t ");
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "duration in us: %lld\t", usDuration);
        PARSER_INFO_SAVE(PARSER_INFO_STREAM, "(%d m : %d s)\n", minutes, seconds);
        if (0 == usDuration)
            PARSER_INFO_SAVE(PARSER_INFO_STREAM,
                             "  --- This is an empty track or track duration is unknown!\n");
        checkTrackDuration(&tracks[i], usMovieDuration);
#endif

        if (IParser->getDecoderSpecificInfo) {
            err = IParser->getDecoderSpecificInfo(parserHandle, i, &decoderSpecificInfo,
                                                  &decoderSpecificInfoSize);
            if (PARSER_SUCCESS != err)
                goto bail;
            tracks[i].decoderSpecificInfo = decoderSpecificInfo;
            tracks[i].decoderSpecificInfoSize = decoderSpecificInfoSize;
            PARSER_INFO_SAVE(PARSER_INFO_STREAM, "\t decoder specific info size : %d\n",
                             decoderSpecificInfoSize);

#if 0
            if(tracks[i].isH264Video && tracks[i].decoderSpecificInfoSize )
            {   /* default behavior, decoder shall parser AVC decoder specific info , and wrap SPS/PPS in NALs if necessary.*/
                err = parseH264DecoderSpecificInfo( tracks[i].decoderSpecificInfo,
                                                    &tracks[i].decoderSpecificInfoSize,
                                                    &tracks[i].NALLengthFieldSize);
                if(PARSER_SUCCESS != err) goto bail;
                printf("\t << NAL length field size in bytes: %d >>\n", tracks[i].NALLengthFieldSize);

            }
#endif

            if (tracks[i].isAAC) {
                if ((0 == decoderSpecificInfoSize) || (NULL == decoderSpecificInfo)) {
                    if (!strcmp(parserRole,
                                "parser.mp4")) { /* not all file formats need AAC codec info */
                        PARSER_ERROR("ERR!Invalid decoder specific info for AAC audio\n");
                        err = PARSER_ERR_INVALID_MEDIA;
                        goto bail;
                    }
                } else {
                    setAACConfig(decoderSpecificInfo, tracks[i].decoderType, &tracks[i].aacConfig);
                }
            }

            if (g_dump_track_data && tracks[i].fp_track_dump && decoderSpecificInfoSize) {
                /* be careful, H.264 may change decoder info size during parsing */
                if (((MEDIA_VIDEO == tracks[i].mediaType) &&
                     ((VIDEO_MPEG4 == tracks[i].decoderType) ||
                      (VIDEO_H264 == tracks[i].decoderType) ||
                      (VIDEO_HEVC == tracks[i].decoderType))) ||
                    ((MEDIA_AUDIO == tracks[i].mediaType) &&
                     ((AUDIO_AMR_NB == tracks[i].decoderType) ||
                      (AUDIO_AMR_WB == tracks[i].decoderType)))) {
                    fwrite(decoderSpecificInfo, tracks[i].decoderSpecificInfoSize, 1,
                           tracks[i].fp_track_dump);
                }
            }
        }
    }

    if (!strncmp(source_url, "http", 4) || !strncmp(source_url, "rtsp", 4))
        isStreaming = TRUE;

    /***************************************************************************************
     *                Playback
     ****************************************************************************************/
    fpUserCommands = fopen(userCommandsFilePath, "r");
    printf("user command file %s opened on fp %p\n", userCommandsFilePath, fpUserCommands);

    err = PlayMovie(fpUserCommands, videoQueueSettings, audioQueueSettings, textQueueSettings,
                    IParser, parserHandle, g_isLiveSource, seekable, num_tracks, tracks, parserRole,
                    isStreaming);

bail:
    g_test_state = TEST_STATE_NULL;

    if (fpUserCommands)
        fclose(fpUserCommands);

#ifdef TIME_PROFILE
    print_GTimeInfo();
#endif

    if (IParser && parserHandle) {
#ifndef AUDIO_PARSER
        if ((IParser->exportIndex != NULL) && g_export_index_table) {
            uint32 size_written = 0, indexSize = 0;
            uint8* indexBuffer = NULL;
            FILE* fpIndex = NULL;

            err = IParser->exportIndex(parserHandle, NULL, &indexSize);
            if (PARSER_SUCCESS != err)  // goto bail;
            {
                indexSize = 0;
            } else {
                PARSER_INFO_SAVE(PARSER_INFO_STREAM, "Size of index to export: %u\n", indexSize);

                indexBuffer = fsl_osal_malloc(indexSize);
                if (NULL == indexBuffer) {
                    err = PARSER_INSUFFICIENT_MEMORY;
                } else {
                    err = IParser->exportIndex(parserHandle, indexBuffer, &indexSize);
                    if (PARSER_SUCCESS != err)
                        indexSize = 0;  // goto bail;
                }
            }

            if ((indexSize > 0) && (NULL != indexBuffer)) {
                fpIndex = fopen(index_file_name, (const char*)"wb");
                if (NULL == fpIndex) {
                    PARSER_ERROR("Fail to open index file to export index: %s\n", index_file_name);
                } else {
                    size_written = fwrite(&indexSize, sizeof(uint32), 1, fpIndex);
                    if (0 == size_written) {
                        err = PARSER_WRITE_ERROR;
                        goto bail;
                    }
                    size_written = fwrite(indexBuffer, indexSize, 1, fpIndex);
                    if (0 == size_written) {
                        PARSER_ERROR("Fail to export index table to fp %d\n", fpIndex);
                        err = PARSER_WRITE_ERROR;
                    }
                    fclose(fpIndex);
                    PARSER_INFO(PARSER_INFO_STREAM, "Index exported\n");
                }
            }

            if (indexBuffer)
                fsl_osal_free(indexBuffer);
        }
#endif

        IParser->deleteParser(parserHandle);
    }

    if (IParser) {
        DestoryParserInterface(IParser, parserDllHandle);
    }

    if (strlen(md5FileName)) {
        char cmd[256];
        sprintf(cmd, "echo \"\n%s\" > %s", source_url, md5FileName);
        if (system(cmd) != 0) {
            PARSER_ERROR("Failed to execute command: %s\n", cmd);
        }
    }

    for (i = 0; i < num_tracks; i++) {
        if (tracks[i].fp_track_dump) {
            fclose(tracks[i].fp_track_dump);
            char cmd[256];
            sprintf(cmd, "md5sum \"%s\" >> %s", tracks[i].dump_file_path, md5FileName);
            if (system(cmd) != 0) {
                PARSER_ERROR("Failed to execute md5sum command: %s\n", cmd);
            }

            if (!g_keepData) {
                sprintf(cmd, "rm \"%s\"", tracks[i].dump_file_path);
                if (system(cmd) != 0) {
                    PARSER_ERROR("Failed to remove file: %s\n", tracks[i].dump_file_path);
                }
            }
        }

        if (tracks[i].fp_track_pts_dump) {
            fclose(tracks[i].fp_track_pts_dump);
            char cmd[256];
            sprintf(cmd, "md5sum \"%s\" >> %s", tracks[i].pts_dump_file_path, md5FileName);
            if (system(cmd) != 0) {
                PARSER_ERROR("Failed to execute md5sum command\n");
            }

            if (!g_keepData) {
                sprintf(cmd, "rm \"%s\"", tracks[i].pts_dump_file_path);
                if (system(cmd) != 0) {
                    PARSER_ERROR("Failed to remove file: %s\n", tracks[i].pts_dump_file_path);
                }
            }
        }
    }

    if (sourceFileHandle)
        appLocalFileClose(sourceFileHandle, appContext);

    if (fpStreamInfo) {
        fclose(fpStreamInfo);
        char cmd[256];
        sprintf(cmd, "md5sum %s >> %s", STREAM_INFO_FILE_NAME, md5FileName);
        if (system(cmd) != 0) {
            PARSER_ERROR("Failed to execute md5sum command\n");
        }
    }

    PARSER_ERROR("ERROR CODE is %d\n\n", err);

    ListErrors(source_url, num_tracks, tracks, err);

    mm_mm_exit();

    return err;
}

/*****************************************************************************
 * Function:    main
 *
 * Description: Intrgrates the three intefaces required to RM Parser Module.
 *              To seek the file and play the parsed file. and to get the
 *              user data information.
 *
 * Return:      The error code.
 *
 * Notes:
 ****************************************************************************/

#ifdef ID3_PARSER_TEST
int main(int32 argc, char* argv[]) {
    int32 ret;
    FslFileStream fileOps;
    ParserMemoryOps memOps;
    FslFileHandle fileHandle;
    void* context = NULL;
    ID3Parser hParser;

    uint16* userData;
    uint32 userDataSize;
    uint32 i = 0;
    UserDataID id;
    UserDataFormat format;
    char* pCh = "";
    char pTest[] = {0x05, 0x6e, 0x0e, 0x66, 0xe8, 0x96, 0x0a, 0x4e, 0x00};
    char pTest2[] = {0xe6, 0xb8, 0x85, 0xe6, 0x98, 0x8e, 0xe9, 0x9b,
                     0xa8, 0xe4, 0xb8, 0x8a, 0x00, 0x00, 0x00, 0x00};

    if (argc < 2) {
        printf("please input file name\n");
        return 0;
    }

    mm_mm_init();

    g_testFilePath = argv[1];

    /* create the parser */
    fileOps.Open = appLocalFileOpen;
    fileOps.Read = appLocalReadFile;
    fileOps.Seek = appLocalSeekFile;
    fileOps.Tell = appLocalGetCurrentFilePos;
    fileOps.Size = appLocalFileSize;
    fileOps.Close = appLocalFileClose;
    fileOps.CheckAvailableBytes = appLocalCheckAvailableBytes;

    memOps.Calloc = fsl_osal_calloc;
    memOps.Malloc = fsl_osal_malloc;
    memOps.Free = fsl_osal_free;
    memOps.ReAlloc = fsl_osal_realloc;

    fileHandle = appLocalFileOpen(g_testFilePath, "rb", context);
    if (fileHandle == NULL) {
        printf("open %s failed\n", g_testFilePath);
        return -1;
    }

    ret = ID3ParserCreate(&fileOps, &memOps, fileHandle, context, &hParser);
    if (ret) {
        printf("ID3ParserCreate failed, ret %d\n", ret);
        return -1;
    }

    printf("\n************************************************************\n");  //, %s, %d\n", pCh,
                                                                                 //strlen(pCh));
    printf("META DATA of %s\n", g_testFilePath);

    while (g_userDataTable[i].id != -1) {
        id = g_userDataTable[i].id;
        format = g_userDataTable[i].format;
        ret = ID3ParserGetMetaData(hParser, id, &format, &userData, &userDataSize);

        if (ret || (userData == NULL) || (userDataSize == 0)) {
            i++;
            continue;
        }

        if (USER_DATA_FORMAT_UTF8 == format) {
            char* string = (char*)malloc(userDataSize + 1);
            if (string) {
                memcpy(string, userData, userDataSize);
                string[userDataSize] = '\0';
                printf(g_userDataTable[i].printString, string);
                free(string);
            }
        }

        if (USER_DATA_ARTWORK == g_userDataTable[i].id) {
            FILE* fp = fopen("./artwork", "wb");
            if (fp) {
                fwrite(userData, 1, userDataSize, fp);
                fclose(fp);
            }

            printf("xxxxxxxxxxxxxxxxx save picture xxxxxxxxxxxxx\n");
        }

        i++;
    }

    printf("************************************************************\n");

    ret = ID3ParserDelete(hParser);
    if (ret) {
        printf("ID3ParserDelete failed, ret %d\n", ret);
        return -1;
    }

    mm_mm_exit();

    return 0;
}

#else
int main(int32 argc, char* argv[]) {
    int32 err = PARSER_SUCCESS;

    char source_url[MAX_FILE_PATH_LENGTH] = {0};

    /* queue configuration */
    tQueueSettings videoQueueSettings = {0};
    tQueueSettings audioQueueSettings = {0};
    tQueueSettings textQueueSettings = {0};

    FILE* fpSourceList = NULL;
    char userCommandsFilePath[MAX_FILE_PATH_LENGTH];
    uint32 testedClipCount = 0;

    g_fpLog = fopen("parselog.txt", "w");
    if (g_fpLog == NULL) {
        printf("open log failed\n");
        return -1;
    }

    parser_initlogstatus();

    printf("%s\n", CODEC_VERSION_STR);
    if (!strcmp("-h", argv[1])) {
        displayHelp();
        goto bail;
    }

    err = parseCommandLine(argc, argv, (char*)source_url, &fpSourceList, userCommandsFilePath,
                           &g_isLiveSource, &videoQueueSettings, &audioQueueSettings,
                           &textQueueSettings);
    if (err)
        goto bail;

    if (source_url[0]) /* only test a single clip */
    {
        g_current_clip_number++;
        err = testOneClip(source_url, userCommandsFilePath, &videoQueueSettings,
                          &audioQueueSettings, &textQueueSettings, TEST_SEQUENCE_A);
    } else if (fpSourceList) {
        char file_path[MAX_FILE_PATH_LENGTH] = {0};
        char file_prefix[MAX_FILE_PATH_LENGTH / 2] = {0};

        while (NULL != fgets(source_url, MAX_FILE_PATH_LENGTH, fpSourceList)) {
            g_current_clip_number++;

            if (g_current_clip_number < g_first_clip_number)
                continue;
            if (g_current_clip_number > g_last_clip_number)
                break;

            /* fgets will read RET at the end of the string */
            if ((0x0d == source_url[strlen(source_url) - 1]) ||
                (0x0a == source_url[strlen(source_url) - 1]))
                source_url[strlen(source_url) - 1] = 0; /* remove the RET */

            if (strncmp(source_url, "prefix", 6) == 0) {
                strcpy(file_prefix, source_url + 7);
                PARSER_INFO(PARSER_INFO_STREAM, "got prefix %s\n", file_prefix);
                continue;
            }

            if (('#' != source_url[0]) && source_url[0]) {
                strcpy(file_path, file_prefix);
                strcpy(file_path + strlen(file_prefix), source_url);
                PARSER_INFO(PARSER_INFO_STREAM, "file path is %s\n", file_path);

                err = testOneClip(file_path, userCommandsFilePath, &videoQueueSettings,
                                  &audioQueueSettings, &textQueueSettings, TEST_SEQUENCE_A);

                if (err != PARSER_FILE_OPEN_ERROR) {  //
                    if (strcmp(g_parser_lib_path_a, g_parser_lib_path_b) == 0) {
                        printf("invalid lib path for regression test!\n");
                        goto bail;
                    }

                    err = testOneClip(file_path, userCommandsFilePath, &videoQueueSettings,
                                      &audioQueueSettings, &textQueueSettings, TEST_SEQUENCE_B);

                    if (fpRegressionTestLog == NULL) {
                        fpRegressionTestLog = fopen(REGRESSION_TEST_LOG, "a");
                        if (fpRegressionTestLog == NULL) {
                            printf("can not open file %s", REGRESSION_TEST_LOG);
                            goto bail;
                        }
                    }
                    g_test_total++;

                    char cmd[256];
                    sprintf(cmd, "diff %s %s > temp.txt", MD5_FILE_NAME_A, MD5_FILE_NAME_B);
                    if (system(cmd) == -1) {
                        PARSER_ERROR("Failed to execute diff command\n");
                    }
                    struct stat f_stat;
                    fprintf(fpRegressionTestLog, "%s:", source_url);
                    if (stat("./temp.txt", &f_stat) == 0 && f_stat.st_size == 0)
                        fprintf(fpRegressionTestLog, "pass\n");
                    else {
                        fprintf(fpRegressionTestLog, "fail\n");
                        g_test_fail++;
                        printf("test fail: %s\n", source_url);
                    }
                    fprintf(fpRegressionTestLog, "total %d, fail %d\n", g_test_total, g_test_fail);
                    fflush(fpRegressionTestLog);
                }

                testedClipCount++;
            }
        }

        g_current_clip_number = (g_current_clip_number < g_last_clip_number) ? g_current_clip_number
                                                                             : g_last_clip_number;
    }

bail:
    if (fpSourceList)
        fclose(fpSourceList);

    if (g_fpLog)
        fclose(g_fpLog);

    if (fpRegressionTestLog)
        fclose(fpRegressionTestLog);

    printf("Totally %d clips are tested and  %d clips failed. Last clip number is %d.\n",
           testedClipCount, g_failed_clip_count, g_current_clip_number);
    printf("Final ERROR CODE is %d\n", err);
    getchar();
    return (int)err;
}
#endif
