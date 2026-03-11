
/*
 ***********************************************************************
 * Copyright (c) 2010-2014, Freescale Semiconductor Inc.
 * Copyright 2026 NXP
 * SPDX-License-Identifier: BSD-3-Clause
 ***********************************************************************
 */

#ifndef INCLUDED_AVI_H
#define INCLUDED_AVI_H

/********************* DEBUG SWITCH ******************************/
// #define DEBUG_SHOW_ATOM_INHERITANCE
// #define DEBUG_SHOW_ATOM_CONTENT

/********************* config ******************************/
/* Whether to drop the zero size samples in setting up index and data reading?
   No by default because they shall occupy a time slot. */
// #define DROP_ZERO_SIZE_SAMPLES

#ifdef SUPPORT_AVI_DRM
#include "drm_common.h"
#endif

/********************* data types for 64-bit extension ******************************/

#ifdef SUPPORT_LARGE_AVI_FILE
typedef uint64 TIME_STAMP;
typedef uint64 OFFSET;
#else
typedef uint32 TIME_STAMP;
typedef uint32 OFFSET;
#endif

/*
 * AVI private error codes.
 */

enum {
    AVI_ERR_UNKNOWN_STREAM_FORMAT = -121,
    AVI_ERR_WRONG_MEDIA_TYPE = -122, /* An API is called on a track of wrong media type. */
    AVI_ERR_WRONG_VIDEO_FORMAT = -123,
    AVI_ERR_WRONG_AUDIO_FORMAT = -124,
    AVI_ERR_INVALID_ATOM = -125,

    AVI_ERR_CORRUPTED_INDEX = -130,
    AVI_ERR_WRONG_INDEX_SAMPLE_SIZE = -131,   /* the index give wrong sample size */
    AVI_ERR_WRONG_INDEX_SAMPLE_OFFSET = -132, /* the index give wrong sample offset */
    AVI_ERR_WRONG_IDX1_LIST_SIZE = -133,
    AVI_ERR_WRONG_AVI2_INDEX_SIZE = -134,
    AVI_ERR_WRONG_AVI2_INDEX_ENTRY_SIZE = -135,
    AVI_ERR_INDEX_TYPE_NOT_SUPPORTED = -136,
    AVI_ERR_SUPER_INDEX_ENTRY_NOT_FOUND = -137,
    AVI_ERR_EMPTY_INDEX = -138,

    AVI_ERR_DRM_NOT_PROTECTED = -142,   /* call DRM APIs for a not protected clip */
    AVI_ERR_DRM_INVALID_CONTEXT = -143, /* DRM context is invalid */
    AVI_ERR_DRM_PREV_PLAY_NOT_CLEAERED = -144,
    AVI_ERR_DRM_INVALID_CALLBACK = -145,
    AVI_ERR_DRM_OTHERS = -146, /* Reserved for other DRM errors.*/
    AVI_ERR_WRONG_DRM_INFO_SIZE = -148,

    AVI_ERR_ZERO_STREAM_RATE = -150, /* The rate of a track is ZERO. Can not calculate time stamp.*/

    AVI_ERR_NO_INDEX = -152,             /* There is no index, but not affect normal playback */
    AVI_ERR_INDEX_ALREADY_LOADED = -153, /* the index table is already loaded or imported */

    AVI_ERR_WRONG_CHUNK = -160,      /* bad chunk id found */
    AVI_ERR_WRONG_CHUNK_SIZE = -161, /*Invalid chunk size, greater than the tatal movie size or
                                        maximum sample size of the track.*/
    AVI_ERR_WRONG_CHUNK_TAG = -162,

    AVI_ERR_WRONG_TRACK_NUM = -163,

    AVI_ERR_WRONG_MOVIE_LIST_SIZE = -170,
    AVI_ERR_NO_MOVIE_LIST = -171,

    AVI_ERR_NO_PRIMARY_TRACK = -180, /* Neither video nor audio available as the primary track */

    AVI_ERR_INVALID_STREAM_NAME = -190,

    AVI_ERR_NO_MP3_FRAME_FOUND = -200

}; /* AVI parser specifc error codes */

/********************* CONSTANTS ******************************/
#define MAX_AV_INTERLEAVE_DEPTH (8 * 1000 * 1000)

#define DRM_FRAME_KEY_SIZE 10

#define MIN_AVI_FILE_SIZE 128
#define RIFF_HEADER_SIZE 8 /* TAG + SIZE */

#define INVALID_MEDIA_SAMPLE_SIZE (-1)
#define INVALID_TRACK_NUM (-1)

/* error concealment */
#define TRACK_CACHE_SIZE (2 * 1024) /* speed up searching */
#define TRACK_SEARCH_SCOPE                                                               \
    (20 * 1024 * 1024) /* Scope for one scanning, in bytes.                              \
                       Large scope is unlikely to inccur large A/V unsync                \
                       unless A/V tracks corruption has big difference.                  \
                       Don't set it too large,                                           \
                       1. For above reason.                                              \
                       2. It will give a slow response if next valid sample is far away, \
                       Seeking can be a better method */

/* How many samples in an audio frame (to calculate pts for VBR audio streams) */
#define MP3_FRAME_SIZE 1152
#define AC3_FRAME_SIZE 1536
#define AAC_FRAME_SIZE 1024
#define MS_ADPCM_FRAME_SIZE 1012

#define DEFAULT_AUDIO_INDEX_INTERVAL 1 /* interval in seconds */

#define ONE_SECOND_IN_US (1000 * 1000)

/* stream name */
#define MAX_AVI_STREAM_NAME_SIZE 1024
#define STREAM_NAME_AUDIO_PREFIX_SIZE 8     /* "Audio - " */
#define STREAM_NAME_SUBTITLE_PREFIX_SIZE 11 /* "Subtitle - " */

/* DRM configuration  */
#define AVI_DRM_INVALID_VERSION 0
#define AVI_DRM_V1 1
#define AVI_DRM_V2 2
#define MAX_AVI_DRM_INFO_SIZE (4 * 1024)

/* duration error scope in us */
#define DURATION_ERROR_SCOPE (2 * 1000 * 1000)

/* minimum index size */
#define MIN_INDEX_SIZE 40

/* audio wave format tag */
enum {
    WAVE_TAG_MP2 = 0X50, /* MP2 */
    WAVE_TAG_MP3 = 0X55, /*MP3 */

    WAVE_TAG_AC3 = 0X2000,
    WAVE_TAG_DTS = 0X2001,

    WAVE_TAG_PCM = 0X01, /* s16le, u8, s24le or s32le, need to get bits per sample */
    WAVE_TAG_PCM_ALAW = 0X06,
    WAVE_TAG_PCM_MULAW = 0X07,

    WAVE_TAG_ADPCM_MS = 0X02, /* Microsoft ADPCM audio */
    WAVE_TAG_ADPCM_IMA_WAV = 0X11,

    WAVE_TAG_ADPCM_YAMAHA = 0X20,
    WAVE_TAG_ADPCM_G726 = 0x45,
    WAVE_TAG_ADPCM_IMA_DK4 = 0x61,
    WAVE_TAG_ADPCM_IMA_DK3 = 0x62,
    WAVE_TAG_ADPCM_CT = 0x200, /* sonic & sonic ls */
    WAVE_TAG_ADPCM_SWF = ('S' << 8) + 'F',

    WAVE_TAG_AAC = 0x00FF,
    WAVE_TAG_FAAD_AAC = 0x706d,

    WAVE_TAG_WMA1 = 0x160,
    WAVE_TAG_WMA2 = 0x0161,
    WAVE_TAG_WMA3 = 0x0162,
    WAVE_TAG_WMALL = 0x0163,

    WAVE_TAG_VORBIS = ('V' << 8) + 'o',
    WAVE_TAG_TRUESPEECH = 0x22,
    WAVE_TAG_FLAC = 0xF1AC,
    WAVE_TAG_IMC = 0x401,

    WAVE_TAG_GSM_MS = 0x31,
    WAVE_TAG_ATRAC3 = 0x270,
    WAVE_TAG_VOXWARE = 0x75,

    WAVE_FORMAT_EXTENSIBLE = 0xfffe,
};

/*********************************************************************
 * Language code.
 *********************************************************************/
typedef enum {
    NONE_LANGUAGE = 0, /* Language is ignored */
    ARABIC = 1,
    BULGARIAN = 2,
    CATALAN = 3,
    CHINESE = 4,
    CZECH = 5,
    DANISH = 6,
    GERMAN = 7,
    GREEK = 8,
    ENGLISH = 9,
    SPANISH = 10,
    FINNISH = 11,
    FRENCH = 12,
    HEBREW = 13,
    HUNGARIAN = 14,
    ICELANDIC = 15,
    ITALIAN = 16,
    JAPANESE = 17,
    KOREAN = 18,
    DUTCH = 19,
    NORWEGIAN = 20,
    POLISH = 21,
    PORTUGUESE = 22,
    RHAETO_ROMANIC = 23,
    ROMANIAN = 24,
    RUSSIAN = 25,
    SERBO_CROATIAN = 26,
    SLOVAK = 27,
    ALBANIAN = 28,
    SWEDISH = 29,
    THAI = 30,
    TURKISH = 31,
    URDU = 32,
    BAHASA = 33
} LanguageCode;

#define fourcc(a, b, c, d) (((d) << 24) | ((c) << 16) | ((b) << 8) | (a))

enum {
    FileTag = fourcc('F', 'I', 'L', 'E'),
    RIFFTag = fourcc('R', 'I', 'F', 'F'),
    AVITag = fourcc('A', 'V', 'I', ' '),
    AVIExtensionTag = fourcc('A', 'V', 'I', 'X'),

    ListTag = fourcc('L', 'I', 'S', 'T'),
    /* list type tag */
    HeaderListTag = fourcc('h', 'd', 'r', 'l'),       /* main header list */
    StreamHeaderListTag = fourcc('s', 't', 'r', 'l'), /* stream header list */
    MovieListTag = fourcc('m', 'o', 'v', 'i'),        /* movie list */
    RecordListTag =
            fourcc('r', 'e', 'c', ' '), /* The data chunks can reside directly in the 'movi' list,
                                           or they might be grouped within 'rec ' lists.*/
    OdmlTag = fourcc('d', 'm', 'l', 'h'),
    InfoListTag = fourcc('I', 'N', 'F', 'O'),

    /* chunk tag */
    AmvHeaderTag = fourcc('a', 'm', 'v', 'h'),
    AviHeaderTag = fourcc('a', 'v', 'i', 'h'), /* main avi header */

    Idx1Tag = fourcc('i', 'd', 'x', '1'),      /* AVI 1.0 index */
    Avi2IndexTag = fourcc('i', 'n', 'd', 'x'), /* AVI 2.0 index, super index */

    StreamHeaderTag = fourcc('s', 't', 'r', 'h'),
    StreamFormatTag = fourcc('s', 't', 'r', 'f'),
    StreamHeaderDataTag = fourcc('s', 't', 'r', 'd'),
    StreamNameTag = fourcc('s', 't', 'r', 'n'),

    AudioTag = fourcc('a', 'u', 'd', 's'),
    VideoTag = fourcc('v', 'i', 'd', 's'),
    MidiTag = fourcc('m', 'i', 'd', 's'),
    TextTag = fourcc('t', 'x', 't', 's'),

    /* user data tags */
    LanguageTag = fourcc('I', 'L', 'N', 'G'),
    RatingTag = fourcc('I', 'R', 'T', 'D'),
    NameTag = fourcc('I', 'N', 'A', 'M'),
    ArtistTag = fourcc('I', 'A', 'R', 'T'),
    CreationDateTag = fourcc('I', 'C', 'R', 'D'),
    GenreTag = fourcc('I', 'G', 'N', 'R'),
    CopyrightTag = fourcc('I', 'C', 'O', 'P'),
    CommentsTag = fourcc('I', 'C', 'M', 'T'),
    LocationTag = fourcc('I', 'A', 'R', 'L'),
    KeywordsTag = fourcc('I', 'K', 'E', 'Y'),

    /* DivX Media Format INFO*/
    IDFVTag = fourcc('I', 'D', 'F', 'V'),
    IDPNTag = fourcc('I', 'D', 'P', 'N'),
    ISFTTag = fourcc('I', 'S', 'F', 'T'),

    JunkTag = fourcc('J', 'U', 'N', 'K')
};

/********************* AVI base atom definitions  BEGIN ******************************/
#define BASE_ATOM_SIZE 16

/* base atom fields, list atom need extra fields than chunk item: type, destroy() funtion to release
child atoms NOTE: the "size" of atom is the even-rounded size, for bytes reading. AVI inserts a \0
at the end of an odd-sized atom. "realSize" is the actual data size of an atom. */
#define AVI_BASE_ATOM        \
    uint32 tag;              \
    uint32 size;             \
    uint32 realSize;         \
    uint32 type;             \
    uint32 bytesRead;        \
    bool isLive;             \
    uint32 au_flag;          \
    struct BaseAtom* parent; \
    void (*destroy)(struct BaseAtom * self);

typedef struct BaseAtom {
    AVI_BASE_ATOM

} BaseAtom, *BaseAtomPtr; /* to describe a chunk or a list, type field is for list only */

#define INIT_ATOM(fourccTag, size1, realSize1, fourccType, parentAtom, live, flag) \
    self->tag = (fourccTag);                                                       \
    self->size = (size1);                                                          \
    self->realSize = (realSize1);                                                  \
    self->type = (fourccType);                                                     \
    self->bytesRead = 0;                                                           \
    self->isLive = live;                                                           \
    self->au_flag = flag;                                                          \
    self->parent = parentAtom;                                                     \
    self->destroy = destroyBaseAtom;

#define COPY_ATOM(des, src)          \
    des->tag = src->tag;             \
    des->size = src->size;           \
    des->realSize = src->realSize;   \
    des->type = src->type;           \
    des->bytesRead = src->bytesRead; \
    des->isLive = src->isLive;       \
    des->au_flag = src->au_flag;     \
    des->parent = src->parent;       \
    des->destroy = src->destroy;

/* create an atom */
#define CREATE_ATOM(atom)                                                    \
    {                                                                        \
        atom = NULL;                                                         \
        err = createAtom(&atom, (BaseAtomPtr)self, inputStream, appContext); \
        if (err)                                                             \
            goto bail;                                                       \
        self->bytesRead += (atom->size + 8);                                 \
    }

/* get a child atom */
#define GET_ATOM(membername)                                                             \
    {                                                                                    \
        err = createAtom(&self->membername, (BaseAtomPtr)self, inputStream, appContext); \
        if (err)                                                                         \
            goto bail;                                                                   \
        self->bytesRead += (self->membername->size + 8);                                 \
    }

#define SKIP_ATOM                                                                \
    {                                                                            \
        uint64 available = self->size - self->bytesRead;                         \
        if (available)                                                           \
            LocalFileSeek(inputStream, (uint32)available, SEEK_CUR, appContext); \
        self->bytesRead = self->size;                                            \
    }

/* safely destroy a child atom */
#define DESTROY_ATOM(membername)                     \
    if (self->membername) {                          \
        self->membername->destroy(self->membername); \
        self->membername = NULL;                     \
    }

/* show atom's inheritance */
#ifdef DEBUG_SHOW_ATOM_INHERITANCE
#define PRINT_INHERITANCE               \
    if (ListTag == self->tag) {         \
        AVIMSG("\t self: LIST ");       \
        PrintTag(self->type);           \
    } else {                            \
        AVIMSG("\t self: ");            \
        PrintTag(self->tag);            \
    }                                   \
    if (ListTag == self->parent->tag) { \
        AVIMSG("\t\t parent: LIST ");   \
        PrintTag(self->parent->type);   \
    } else {                            \
        AVIMSG("\t\t parent: ");        \
        PrintTag(self->parent->tag);    \
    }
#else
#define PRINT_INHERITANCE
#endif

/********************* AVI base atom definitions END ******************************/

typedef struct {
    AVI_BASE_ATOM

    BaseAtomPtr hdrl; /* hdrl, header list, type HeaderListPtr*/
    BaseAtomPtr movi;
    BaseAtomPtr idx1;
    BaseAtomPtr info; /* user data info list , LIST INFO */

} RiffTitle, *RiffTitlePtr; /* RIFF title*/

typedef struct {
    AVI_BASE_ATOM

    BaseAtomPtr avih;                 /* main avi header, type MainAviHeaderPtr */
    BaseAtomPtr strl[MAX_AVI_TRACKS]; /* type StreamHeaderListPtr */
    uint32 numStreams;                /* get this value by check the number of stream headers */

} HeaderList, *HeaderListPtr; /* hdrl LIST */

typedef struct {
    AVI_BASE_ATOM

    /* file level info from the main avi header */
    uint32 msPerFrame;     /* micro seconds per video frame */
    uint32 maxBytesPerSec; /* often 0 */
    uint32 reserved;
    uint32 flags; /* must use index? the physical ordering of chunks is not the presentation order
                   */
    uint32 totalFrames;
    uint32 initialFrames;
    uint32 numStreams;
    uint32 suggestedBufferSize;
    uint32 width;
    uint32 height;
    uint32 scale;                   /* often 0, will make start & lenght fields invalid */
    uint32 rate;                    /* often 0 */
    uint32 start;                   /* start delay */
    uint32 length;                  /* presentation length in time scale, often 0 */
} MainAviHeader, *MainAviHeaderPtr; /* avih */

typedef struct {
    AVI_BASE_ATOM

    BaseAtomPtr strh; /* StreamHeader */
    BaseAtomPtr strf; /* StreamFormat */
    BaseAtomPtr strd; /* SteamHeaderData, optional.
                                  'strd' chunk of a video stream should exist only for DRM-protected
                         video */
    BaseAtomPtr strn; /* stream name informaiton, optional */

    BaseAtomPtr indx; /* super index */

} StreamHeaderList, *StreamHeaderListPtr; /* strl LIST */

typedef struct {
    AVI_BASE_ATOM
    uint32 fccType; /* vids, auds, txts, midi */
    uint32 fccHandler;
    uint32 flags;
    uint16 priority; /* reseved: priority and language */
    uint16 language;
    uint32 initialFrames;
    uint32 scale;
    uint32 rate;
    uint32 start;
    uint32 length;
    uint32 suggestedBufferSize;
    uint32 quality;
    uint32 sampleSize;

    struct {
        uint16 left;
        uint16 top;
        uint16 right;
        uint16 bottom;
    } frame; /* the value is unused */

} StreamHeader, *StreamHeaderPtr; /* strh */

#define WAVEFORMATEX_SIZE 18 /* size of WAVEFORMATEX, including "cbSize" */
#define WAVEFORMATEXTENSIBLE_SIZE \
    22 /* size of WAVEFORMATEXTENSIBLE structure, not including WAVEFORMATEX size*/

#define MPEG_LAYER1 1
#define MPEG_LAYER2 2
#define MPEG_LAYER3 3

#define AVI_STRF_MAX_EXTRA 22

typedef struct {
    uint16 formatTag; /* wave format tag */
    uint16 channels;  /* 1 for mono, 2 for stereo, more for multi-channels */
    uint32 samplesPerSec;
    uint32 avgBytesPerSec;
    uint16 blockAlgn;
    uint16 bitsPerSample;
    uint16 extraSize;
    uint8* extraData; /* wave extra data, point to somewhere in decoder specific info buffer */
    uint16 layer;     /* get the layer outside "extraData" for convenience */
    /* for extraData, only care about the first 2 bytes: layer */

} WaveFormatEx; /* 'strf' for audio steam*/

#define BITMAPINFO_SIZE 40 /* size of BITMAPINFO */

typedef struct {
    uint32 size;
    uint32 width;
    uint32 height;
    uint16 planes;
    uint16 bitCount; /* bits per pixel */
    uint32 compression;
    uint32 sizeImage;
    uint32 xPelsPerMeter; /* often 0 */
    uint32 yPelsPerMeter;
    uint32 clrUsed;
    uint32 clrImportant;
} BitmapInfo;

typedef struct {
    AVI_BASE_ATOM

    union {
        WaveFormatEx waveFomatEx;
        BitmapInfo bitmapInfo;
    };

    uint8* decoderSpecificInfo; /* for WINCE, we put the whole data of strf into this decoder
                                   specific info */
    uint32 decoderSpecificInfoSize;

    uint32 h264NAL_HeaderSize;
    bool waveformat_extensible;   /* indicate if the WAVEFORMATEXTENSIBLE information is exist  */
} StreamFormat, *StreamFormatPtr; /* strf */

typedef struct {
    AVI_BASE_ATOM

    uint32 version; /* default is 1 */
    uint32 drmInfoSize;
    uint8* drmInfo;

} StreamHeaderData, *StreamHeaderDataPtr; /* strd, optional atom. DRM info hidden inside it */

typedef struct {
    AVI_BASE_ATOM

    uint8* nameString;
    uint32 nameSize;

} StreamName, *StreamNamePtr; /* strn, stream name infomation */

typedef struct {
    AVI_BASE_ATOM

    int64 moviList; /* where 'movi' list begins
                                  --- file offset of list type field 'movi',  so 4 bytes ahead of
                       where data chunks begin*/
    int64 moviEnd;  /* where 'movi' list (last chunk) ends */

    bool cutShort; /* TRUE if the movie data is cut short, the untouched part shall still be able to
                      be played! */

} MovieList, *MovieListPtr;

typedef struct {
    AVI_BASE_ATOM

    int64 idx1Start; /* where 1st idx1 entry begins
                                  --- file offset after list type field 'idx1'*/
    int64 idxEnd;    /* where 'idx' list (last chunk) ends */

    int32 numEntries;

    bool useAbsoluteOffset; /* whether index entries use absolute offset,
                               not relative offset from movi list by defualt. */

} Idx1, *Idx1Ptr;

typedef struct {
    AVI_BASE_ATOM

    BaseAtomPtr ilng; /* language. Conflicting with CEST? */
    BaseAtomPtr irtd; /* rating */
    BaseAtomPtr inam; /* name, title  */
    BaseAtomPtr iart; /* artist, author */
    BaseAtomPtr icrd; /* creation date */
    BaseAtomPtr ignr; /* genre */
    BaseAtomPtr icop; /* copyrignt */
    BaseAtomPtr icmt; /* comment */

    BaseAtomPtr isft; /* ISFT name */
    BaseAtomPtr idfv; /* DivX Format Version */
    BaseAtomPtr idpn; /* DivX Profile Name */
    BaseAtomPtr ikey; /* Keywords */
    BaseAtomPtr iarl; /* Archival Location */

} InfoList, *InfoListPtr; /* INFO LIST */

typedef struct {
    AVI_BASE_ATOM

    uint32 dataSize;
    uint8* data;

    uint16* unicodeString;
    uint32 stringLength; /* in characters */

} UserDataAtom, *UserDataAtomPtr; /* user data atom */

/********************* AVI 2.0 index definitions START ******************************/
/* index types */
#define AVI_INDEX_OF_INDEXES 0x00
#define AVI_INDEX_OF_CHUNKS 0x01
#define AVI_INDEX_IS_DATA 0X80 /* Not supported yet*/

/* index subtypes for INDEX_OF_CHUNKS */
#define AVI_INDEX_2FIELD 0X01

#define AVI2_INDEX_BASE_FIELDS                                         \
    uint16 longsPerEntry; /* size of each entry in the aIndex array */ \
    uint8 indexSubType;                                                \
    uint8 indexType;                                                   \
    uint32 entriesInUse;                                               \
    uint32 chunkId;

typedef struct {
    AVI_BASE_ATOM

    AVI2_INDEX_BASE_FIELDS

    uint32 reserved[3];

} BaseIndex, *BaseIndexPtr; /* AVI 2.0 base index, same size as subtyes */

typedef struct {
    AVI_BASE_ATOM

    AVI2_INDEX_BASE_FIELDS

    uint64 baseOffset;
    uint32 reserved3;

    int64 entriesFileOffet; /* file offset of the entries field*/

} StdIndex, *StdIndexPtr; /* standard index */

typedef struct {
    int64 offset;
    uint32 size;
    uint32 duration;
} SuperIndexEntry, *SuperIndexEntryPtr;

typedef struct {
    AVI_BASE_ATOM

    AVI2_INDEX_BASE_FIELDS

    uint32 reserved[3];

    int64 entriesFileOffet; /* file offset of the entries field*/
    SuperIndexEntryPtr entries;

} SuperIndex, *SuperIndexPtr; /* super index */

#define COPY_BASE_INDEX(des, src)            \
    COPY_ATOM(des, src)                      \
    des->longsPerEntry = src->longsPerEntry; \
    des->indexSubType = src->indexSubType;   \
    des->indexType = src->indexType;         \
    des->entriesInUse = src->entriesInUse;   \
    des->chunkId = src->chunkId;

/*********************AVI 2.0 index definitions END ******************************/

/* avi core parser general index */
typedef struct {
    TIME_STAMP pts; /* for VBR stream, this is the 0-based sample index;
                                for CBR stream, this is the accumulated bytes before this sample
                       entry */
    OFFSET offset;  /* changed from 64 to 32 bits long to decrease memory. absolute file offset of
                       the data chunk (chunk ID field) */

} indexEntry, *indexEntryPtr; /* avi parser index entry for both AVI 1.0 & 2.0 */

#define MAX_IDX_ENTRY_READ_PER_TIME 1024
#define IDX_TBL_SIZE ((32 * 1024) / sizeof(indexEntry) + 1)
#define TXT_TRACK_IDX_TBL_SIZE_STEP \
    ((8 * 1024) / sizeof(indexEntry) + 1) /* smaller than video step */

typedef struct AVStream {
    uint32 streamIndex; /* 0-based stream index. Not assume stream 0 is video ! */

    AviInputStream* inputStream;

    MediaType mediaType;
    uint32 tag;                  /* fourcc of the stream's data chunk */
    uint32 drmTag;               /* for video stream only, 'xxdd' */
    uint32 uncompressedVideoTag; /* for video stream only, 'xxdb'*/
    uint32 decoderType;          /* video/audio codec type */
    uint32 decoderSubtype;

    /* video properties */

    uint32 rate; /* to give a pts */
    uint32 scale;
    uint32 startDelay; /* start delay of the stream in scale. NON-zero means the stream does not
                          start concurrently with the file */
    uint64 usDuration; /*duration in us */

    uint64 usFixedSampleDuration; /* 0 if sample duration of this track is not a constant.
                                    It is for PTS calculation.*/

    uint32 suggestedBufferSize; /* Even-rounded suggested buffer size from 'strh',
                                it may be equal or larger than max sample size, or be 0. */

    uint32 maxSampleSize; /* max sample size, it's from the index table if index available.
                            At first, it's equal to the suggested buffer size from 'strh'.
                            And then it will be updated after scanning the index table if index
                            table give a larger value. Index table may be half missing or corrupted.
                            if this value is ODD, round it to EVEN.

                            If no suggested buffer size is given and no index exists,
                            this value is set to INVALID_MEDIA_SAMPLE_SIZE (-1).
                            But the user will get 0.

                            Used for error check and help user to allocate a sample buffer big
                            enough.*/

    uint32 codecSpecificInfoSize;
    uint8* codecSpecificInfo;

    bool isCbr;
    uint32 bytesPerSec;    /* only for cbr stream */
    uint32 audioFrameSize; /* to calculate pts for VBR MP3/AC3 stream */
    uint32 sampleRate;     /* to calculate pts for VBR audio stream */

    /* reading control ---- START */
    uint32 readMode;
    bool enabled;

    uint64 sampleOffset; /* index of next frame to read, 0-based */
    uint64 fileOffset;
    uint64 indexOffset;
    uint32 prevSampleSize; /*size of the sample just read, for CBR stream trick mode */

    bool chunkHeaderRead; /* whether the chunk header is read already before sample data is read.
                            The data output of this sample is not started yet. */

    /* partial output of large samples */
    uint32 sampleBytesLeft; /* bytes of current sample data not output yet, with the possible
                            padding byte. If this value is non-zero, current sample output is not
                            finished, and need backup its flag, size, start time & end time*/
    /* backup of current sample info*/
    uint32 sampleSize;        /* size backup. real size of the entire sample, not even- rounded */
    uint32 sampleFlag;        /* flag backup */
    uint64 usSampleStartTime; /* start time backup */
    uint64 usSampleDuration;  /* current sample duration backup */

    /* for file-based trick mode */
    bool bos; /* whether meet BOS when rewinding. seeking will clear this flag */
    bool isOneSampleGotAfterSkip; /* Whether just read a sample after skipping to a sync sample.
                                    to avoid sample counter decrease without sample reading when
                                    rewinding */

    /* reading control ---- END */

    /* index table */
    indexEntryPtr indexTab; /* index table of the stream.
                                        For video stream, only key frames are indexed.
                                        For audio stream, only peer of the video key frames are
                               indexed. */
    uint32 indexTabSize;    /* in entries */
    uint64 numIndexEntries; /* number of index entries in the table */

    /* used for index table scanning, if index table is not available, these values are ZERO! */
    uint64 numSamples;

    uint64 cumLength; /* "last time stamp":
                                    accumulated length for index, temporary storage (used during
                         seek), same as frame_offset for video/VBR audio, frame/chunk index for cbr
                         audio, accumulated_bytes/sampe_size When frame offset=cum_len, EOS reached!
                                    NEED also export with index table */
    uint64 firstSampleFileOffset;

    uint64 lastSampleFileOffset;

    uint64 numZeroSizeSamples;

    /* temperary flags for indexing audio track */
    uint64 usPeerEntryPTS; /* pts of peer index entry of base stream, in ms, only for indexing audio
                              streams */
    bool indexDone;        /* whether the index table of this stream is established because all peer
                           entries are found.        Only secondary audio streams will set this flag.*/
    indexEntry prevEntry;  /* previous audio sample entry index found in file index table */

    uint64 defaultAudioIndexInterval; /* scaled pts interval of index, for primary audio stream */
    uint64 prevIndexedEntryPTS;       /* for primary audio stream */

    /* temperary flags for indexing text track */
    uint64 peerEntryNum; /* number of peer index entry of base stream, 0-based,only for indexing
                            text streams */

    /* for error concealment */
    uint8* cache;             /* cashe buffer for error concealment, to speed up searching */
    uint32 errConcealedCount; /* how many times error concealment is applied for this track */
    int64 errBytesScanned; /* how many bytes scanned in error concealments before seeking to a sync
                              sample */

    uint32 blockAlign; /* stream blockAlign for block size */

} AVStream, *AVStreamPtr;

typedef struct {
    /* to support multi-riff avi file, abstract the file as an atom, but no "destroy" function */
    AVI_BASE_ATOM

    AviInputStream* inputStream;

    void* appContext;
    bool fileHeaderParsed; /* whether the file header is parsed */

    BaseAtomPtr riff; /* riff title 1 */
    bool isAvi2;      /* avi2 format, may has multiple channels */

    uint64 usLongestTrackDuration;

    bool seekable;    /* depends on whether index table exist & loading/importing succeeds */
    bool indexLoaded; /* whether index is loaded from file or imported from outside database */
    uint32 indexSizeToExport;
    bool isDeepInterleaving; /* for deep interleaving, file-based reading mode is not suitable.
                                Only set if both a/v are present and interleaving is too large.*/

    bool bCorruptedIdx;

    uint32 numStreams;                   /* number of streams by check number of stream headers */
    AVStreamPtr streams[MAX_AVI_TRACKS]; /* stream array */
    uint32 primaryStreamNum; /* primary stream(track) is the 1st video or 1st audio if video is not
                                available. It will help to setup index for all other secondary
                                tracks.*/

    /* actual selected stream tag */
    uint32 videoTag;
    uint32 drmTag;

    /* drm */
    bool protected;      /* whether the content is protected */
    uint8* drmContext;   /* core parser allocate this memory and free it on finalizePlayback() */
    uint8* drmHeader;    /* never free this memory, point to the strd's drmInfo field */
    uint32 drmHeadeSize; /* size in bytes of the drm_raw_info */
    uint8 frameKey[DRM_FRAME_KEY_SIZE]; /* shared by all the video streams */

#ifdef SUPPORT_AVI_DRM
    bool bHasDrmLib;
    void* hDRMLib;
    drmAPI_s sDrmAPI;
#endif

    /* */
    uint64 numChunks; /*total number of data chunks, including drm chunks */

    uint64 fileSize; /* actual file size in bytes */

    uint64 moviList; /* where 'movi' begins
                               --- start from list type 'movi', right after the movi list size
                        field, and before type 'movi' field so 4 bytes ahead of where data chunks
                        begin  ;-) */

    uint64 moviEnd;  /* For AVI 1.0, where list 'movi' ends.
                                 For AVI 2.0, it's the file end because there can be multiple RIFFs &
                        multiple movie lists. */
    uint64 moviSize; /* size of the movie data, END-START */

    uint32 readMode;

    /* only for file-based mode */
    uint64 fileOffset;
    bool isNewSegment;      /* after seeking, a new segment started */
    AVStreamPtr nextStream; /* next stream to read, for partial output.
                               NULL means need to select a track with min file offset. */

} AviObject, *AviObjectPtr;

int32 createAtom(BaseAtomPtr* outAtom, BaseAtomPtr parentAtom, AviInputStream* inputStream,
                 void* appContext);
void destroyBaseAtom(BaseAtomPtr atom);

int32 parseRIFF(RiffTitlePtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                void* appContext);

int32 parseHeaderList(HeaderListPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                      void* appContext);
int32 parseAviHeader(MainAviHeaderPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                     void* appContext);

int32 parseStreamHeaderList(StreamHeaderListPtr* outAtom, BaseAtomPtr proto,
                            AviInputStream* inputStream, void* appContext);
int32 parseStreamHeader(StreamHeaderPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                        void* appContext);
int32 parseStreamFormat(StreamFormatPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                        void* appContext);
int32 parseStreamHeaderData(StreamHeaderDataPtr* outAtom, BaseAtomPtr proto,
                            AviInputStream* inputStream, void* appContext);
int32 parseStreamName(StreamNamePtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                      void* appContext);

int32 parseMovieList(MovieListPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                     void* appContext);

int32 parseIdx1(Idx1Ptr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream, void* appContext);
int32 loadIdx1(AviObjectPtr aviObj);

int32 parseInfoList(InfoListPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                    void* appContext);
int32 parseUserDataAtom(UserDataAtomPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                        void* appContext);

bool isAvi2IndexTag(uint32 tag);

int32 parseAvi2BaseIndex(BaseIndexPtr* outAtom, BaseAtomPtr proto, AviInputStream* inputStream,
                         void* appContext);
int32 parseSuperIndex(SuperIndexPtr* outAtom, BaseIndexPtr proto, AviInputStream* inputStream,
                      void* appContext);
int32 parseStandardIndex(StdIndexPtr* outAtom, BaseIndexPtr proto, AviInputStream* inputStream,
                         void* appContext);

int32 loadSuperIndex(AviObjectPtr aviObj, uint32 trackNum, SuperIndexPtr indx,
                     AVStreamPtr videoStream);
int32 loadStandardIndex(AviObjectPtr aviObj, uint32 trackNum, StdIndexPtr indx,
                        AVStreamPtr videoStream);

/* seeking */
int32 seekTrack(AviObjectPtr self, AVStreamPtr stream, uint64* usTime, uint32 flag);
int32 seek2SyncSample(AviObjectPtr self, AVStreamPtr stream, uint32 flag);

void seek2TrackStart(AviObjectPtr self, AVStreamPtr stream);
void seek2TrackEnd(AviObjectPtr self, AVStreamPtr stream);
void seek2Entry(AVStreamPtr stream, indexEntryPtr entry);

int32 verifySampleIndex(uint32 sampleSize, int64 sampleFileOffset, AVStreamPtr stream,
                        uint32 streamTag, AviInputStream* inputStream, void* appContext);

bool tryIndexAudioEntry(AVStreamPtr stream, AVStreamPtr baseStream, uint64 sampleOffset);
bool tryIndexTextEntry(AVStreamPtr stream, AVStreamPtr baseStream, uint64 sampleOffset);

void checkInterleavingDepth(AviObjectPtr self);

int32 importIndex(AviObjectPtr aviObj, uint8* buffer, uint32 size);
int32 exportIndex(AviObjectPtr aviObj, uint8* buffer, uint32* size);

/* time conversion */
void getSampleTime(AVStreamPtr stream, uint64 scaledTime, uint64* usTime);

void getScaledTime(AVStreamPtr stream, uint64 usTime, uint64* scaledTime);
void getTimestamp(AVStreamPtr stream, uint32 sampleSize, uint64* usStartTime, uint64* usDuration);

/* error concealment */
int32 AviSearchValidSample(AviObjectPtr aviObj, AVStreamPtr stream, bool* suggestSeek);

int32 AviSearchNextSample(AviObjectPtr aviObj, AVStreamPtr stream, uint32 scope);

bool isValidTag(uint32 tag);

/* sample output */
void resetTrackReadingStatus(AVStreamPtr stream);

int32 getNextChunkHead(FslFileHandle parserHandle, uint32 trackNum);

int32 getFileNextChunkHead(FslFileHandle parserHandle, uint32* trackNum);

void findMinFileOffset(AviObjectPtr self);
void findMaxFileOffset(AviObjectPtr self);

int32 getNextSample(FslFileHandle parserHandle, uint32 trackNum, uint8** sampleBuffer,
                    void** bufferContext, uint32* dataSize, uint64* usStartTime, uint64* usDuration,
                    uint32* flag);

int32 getSampleRemainingBytes(FslFileHandle parserHandle, uint32 trackNum, uint8** sampleBuffer,
                              void** bufferContext, uint32* dataSize, bool* finished);

#endif /* INCLUDED_AVI_H */
