/**
 *  Copyright 2005-2016, Freescale Semiconductor Inc.,
 *  Copyright 2017-2024, 2026 NXP
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef MATROSKA_H
#define MATROSKA_H

#include "mkv_parser_api.h"
#include "streambuf.h"
#ifdef SUPPORT_MKV_DRM
#include "drm_common.h"
#endif
#include "amphion_startcode.h"
#include "bit_reader.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef uint64 vint;
typedef int64 svint;

#define EBML_DOCTYPE_LEN 64
#define MKV_DOCTYPE "matroska"
#define WEBM_DOCTYPE "webm"

#define INIT_READ_HEADER_LEN 8192

#define FSL_EBML_VERSION 1

// EBML header
#define FSL_EBML_ID_HEADER 0x1A45DFA3

#define FSL_EBML_ID_EBMLVERSION 0x4286
#define FSL_EBML_ID_EBMLREADVERSION 0x42F7
#define FSL_EBML_ID_EBMLMAXIDLENGTH 0x42F2
#define FSL_EBML_ID_EBMLMAXSIZELENGTH 0x42F3
#define FSL_EBML_ID_DOCTYPE 0x4282
#define FSL_EBML_ID_DOCTYPEVERSION 0x4287
#define FSL_EBML_ID_DOCTYPEREADVERSION 0x4285

// global elements
#define FSL_EBML_ID_VOID 0xEC
#define FSL_EBML_ID_CRC32 0xBF

// segment
#define MATROSKA_ID_SEGMENT 0x18538067

// segment information
#define MATROSKA_ID_INFO 0x1549A966
#define MATROSKA_ID_SEGMENTUID 0x73A4
#define MATROSKA_ID_TIMECODESCALE 0x2AD7B1
#define MATROSKA_ID_DURATION 0x4489
#define MATROSKA_ID_DATEUTC 0x4461
#define MATROSKA_ID_TITLE 0x7BA9
#define MATROSKA_ID_MUXINGAPP 0x4D80
#define MATROSKA_ID_WRITINGAPP 0x5741

// cluster
#define MATROSKA_ID_CLUSTER 0x1F43B675
#define MATROSKA_ID_CLUSTERTIMECODE 0xE7
#define MATROSKA_ID_CLUSTERPOSITION 0xA7
#define MATROSKA_ID_CLUSTERPREVSIZE 0xAB

#define MATROSKA_ID_SIMPLEBLOCK 0xA3
#define MATROSKA_ID_BLOCKGROUP 0xA0
#define MATROSKA_ID_BLOCK 0xA1
#define MATROSKA_ID_BLOCKDURATION 0x9B
#define MATROSKA_ID_BLOCKREFERENCE 0xFB

// track
#define MATROSKA_ID_TRACKS 0x1654AE6B
#define MATROSKA_ID_TRACKENTRY 0xAE
#define MATROSKA_ID_TRACKNUMBER 0xD7
#define MATROSKA_ID_TRACKUID 0x73C5
#define MATROSKA_ID_TRACKTYPE 0x83
#define MATROSKA_ID_TRACKFLAGENABLED 0xB9
#define MATROSKA_ID_TRACKFLAGDEFAULT 0x88
#define MATROSKA_ID_TRACKFLAGFORCED 0x55AA
#define MATROSKA_ID_TRACKFLAGLACING 0x9C
#define MATROSKA_ID_TRACKMINCACHE 0x6DE7
#define MATROSKA_ID_TRACKMAXCACHE 0x6DF8
#define MATROSKA_ID_TRACKDEFAULTDURATION 0x23E383
#define MATROSKA_ID_TRACKTIMECODESCALE 0x23314F
#define MATROSKA_ID_TRACKMAXBLKADDID 0x55EE
#define MATROSKA_ID_TRACKNAME 0x536E
#define MATROSKA_ID_TRACKLANGUAGE 0x22B59C

#define MATROSKA_ID_CODECID 0x86
#define MATROSKA_ID_CODECPRIVATE 0x63A2
#define MATROSKA_ID_CODECNAME 0x258688
#define MATROSKA_ID_ATTACHLINK 0x7446
#define MATROSKA_ID_CODECINFOURL 0x3B4040
#define MATROSKA_ID_CODECDOWNLOADURL 0x26B240
#define MATROSKA_ID_CODECDECODEALL 0xAA
#define MATROSKA_ID_CODECDELAY 0x56AA
#define MATROSKA_ID_SEEKPREROLL 0x56BB

// video
#define MATROSKA_ID_TRACKVIDEO 0xE0
#define MATROSKA_ID_VIDEOFLAGINTERLACED 0x9A
#define MATROSKA_ID_VIDEOSTEREOMODE 0x53B9
#define MATROSKA_ID_VIDEOPIXELWIDTH 0xB0
#define MATROSKA_ID_VIDEOPIXELHEIGHT 0xBA
#define MATROSKA_ID_VIDEOPIXELCROPB 0x54AA
#define MATROSKA_ID_VIDEOPIXELCROPT 0x54BB
#define MATROSKA_ID_VIDEOPIXELCROPL 0x54CC
#define MATROSKA_ID_VIDEOPIXELCROPR 0x54DD
#define MATROSKA_ID_VIDEODISPLAYWIDTH 0x54B0
#define MATROSKA_ID_VIDEODISPLAYHEIGHT 0x54BA
#define MATROSKA_ID_VIDEODISPLAYUNIT 0x54B2
#define MATROSKA_ID_VIDEOASPECTRATIO 0x54B3
#define MATROSKA_ID_VIDEOCOLORSPACE 0x2EB524
#define MATROSKA_ID_VIDEOFRAMERATE 0x2383E3

#define MATROSKA_ID_VIDEOCOLOUR 0x55B0
#define MATROSKA_ID_VIDEOMATRIXCOEFFICIENTS 0x55B1
#define MATROSKA_ID_VIDEOBITSPERCHANNEL 0x55B2
#define MATROSKA_ID_VIDEOCHROMASUBSAMPLINGHORZ 0x55B3
#define MATROSKA_ID_VIDEOCHROMASUBSAMPLINGVERT 0x55B4
#define MATROSKA_ID_VIDEOCBSUBSAMPLINGHORZ 0x55B5
#define MATROSKA_ID_VIDEOCBSUBSAMPLINGVERT 0x55B6
#define MATROSKA_ID_VIDEOCHROMASITINGHORZ 0x55B7
#define MATROSKA_ID_VIDEOCHROMASITINGVERT 0x55B8
#define MATROSKA_ID_VIDEORANGE 0x55B9
#define MATROSKA_ID_VIDEOTRANSFERCHARACTERISTICS 0x55BA
#define MATROSKA_ID_VIDEOPRIMARIES 0x55BB
#define MATROSKA_ID_VIDEOMAXCLL 0x55BC
#define MATROSKA_ID_VIDEOMAXFALL 0x55BD

#define MATROSKA_ID_VIDEOMASTERINGMETADATA 0x55D0
#define MATROSKA_ID_VIDEOPRIMARYRX 0x55D1
#define MATROSKA_ID_VIDEOPRIMARYRY 0x55D2
#define MATROSKA_ID_VIDEOPRIMARYGX 0x55D3
#define MATROSKA_ID_VIDEOPRIMARYGY 0x55D4
#define MATROSKA_ID_VIDEOPRIMARYBX 0x55D5
#define MATROSKA_ID_VIDEOPRIMARYBY 0x55D6
#define MATROSKA_ID_VIDEOWHITEPOINTX 0x55D7
#define MATROSKA_ID_VIDEOWHITEPOINTY 0x55D8
#define MATROSKA_ID_VIDEOLUMINANCEMAX 0x55D9
#define MATROSKA_ID_VIDEOLUMINANCEMIN 0x55DA

// audio
#define MATROSKA_ID_TRACKAUDIO 0xE1
#define MATROSKA_ID_AUDIOSAMPLINGFREQ 0xB5
#define MATROSKA_ID_AUDIOOUTSAMPLINGFREQ 0x78B5
#define MATROSKA_ID_AUDIOCHANNELS 0x9F
#define MATROSKA_ID_AUDIOBITDEPTH 0x6264

#define MATROSKA_ID_TRACKCONTENTENCODINGS 0x6D80
#define MATROSKA_ID_TRACKCONTENTENCODING 0x6240
#define MATROSKA_ID_ENCODINGORDER 0x5031
#define MATROSKA_ID_ENCODINGSCOPE 0x5032
#define MATROSKA_ID_ENCODINGTYPE 0x5033
#define MATROSKA_ID_ENCODINGCOMPRESSION 0x5034
#define MATROSKA_ID_ENCODINGCOMPALGO 0x4254
#define MATROSKA_ID_ENCODINGCOMPSETTINGS 0x4255
#define MATROSKA_ID_ENCODINGCONTENTENCRYPTION 0x5035
#define MATROSKA_ID_ENCODINGCONTENTENCALGO 0x47E1
#define MATROSKA_ID_ENCODINGCONTENTENCKEYID 0x47E2
#define MATROSKA_ID_ENCODINGCONTENTSIGNATURE 0x47E3
#define MATROSKA_ID_ENCODINGCONTENTSIGKEYID 0x47E4
#define MATROSKA_ID_ENCODINGCONTENTSIGALGO 0x47E5
#define MATROSKA_ID_ENCODINGCONTENTSIGHASHALGO 0x47E6
// get from http://www.webmproject.org/docs/webm-encryption/
#define MATROSKA_ID_ENCODINGCONTENTENCAESSETTINGS 0x47E7
#define MATROSKA_ID_ENCODINGAESSETTINGSCIPHERMODE 0x47E8

// cueing data
#define MATROSKA_ID_CUES 0x1C53BB6B
#define MATROSKA_ID_POINTENTRY 0xBB
#define MATROSKA_ID_CUETIME 0xB3
#define MATROSKA_ID_CUETRACKPOSITION 0xB7
#define MATROSKA_ID_CUETRACK 0xF7
#define MATROSKA_ID_CUECLUSTERPOSITION 0xF1
#define MATROSKA_ID_CUEBLOCKNUMBER 0x5378

// attachment
#define MATROSKA_ID_ATTACHMENTS 0x1941A469
#define MATROSKA_ID_ATTACHEDFILE 0x61A7
#define MATROSKA_ID_FILEDESC 0x467E
#define MATROSKA_ID_FILENAME 0x466E
#define MATROSKA_ID_FILEMIMETYPE 0x4660
#define MATROSKA_ID_FILEDATA 0x465C
#define MATROSKA_ID_FILEUID 0x46AE

// chapters
#define MATROSKA_ID_CHAPTERS 0x1043A770
#define MATROSKA_ID_EDITIONENTRY 0x45B9
#define MATROSKA_ID_EDITIONUID 0x45BC
#define MATROSKA_ID_EDITIONFLAGHIDDEN 0x45BD
#define MATROSKA_ID_EDITIONFLAGDEFAULT 0x45DB
#define MATROSKA_ID_EDITIONFLAGORDERED 0x45DD

#define MATROSKA_ID_CHAPTERATOM 0xB6
#define MATROSKA_ID_CHAPTERUID 0x73C4
#define MATROSKA_ID_CHAPTERTIMESTART 0x91
#define MATROSKA_ID_CHAPTERTIMEEND 0x92
#define MATROSKA_ID_CHAPTERFLAGHIDDEN 0x98
#define MATROSKA_ID_CHAPTERFLAGENABLED 0x4598
#define MATROSKA_ID_CHAPTERPSEGMETUID 0x6E67
#define MATROSKA_ID_CHAPTERPSEGMENTEDITIONUID 0x6EBC
#define MATROSKA_ID_CHAPTERPHYSEQUIV 0x63C3
#define MATROSKA_ID_CHAPTERPTRACKS 0x8F
#define MATROSKA_ID_CHAPTERDISPLAY 0x80
#define MATROSKA_ID_CHAPTRACKNUMBER 0x89
#define MATROSKA_ID_CHAPSTRING 0x85
#define MATROSKA_ID_CHAPLANG 0x437C
#define MATROSKA_ID_CHAPCOUNTRY 0x437E

// tagging
#define MATROSKA_ID_TAGS 0x1254C367
#define MATROSKA_ID_TAG 0x7373
#define MATROSKA_ID_TAGTARGETS 0x63C0
#define MATROSKA_ID_TAGTARGETS_TYPEVALUE 0x68CA
#define MATROSKA_ID_TAGTARGETS_TYPE 0x63CA
#define MATROSKA_ID_TAGTARGETS_TRACKUID 0x63C5
#define MATROSKA_ID_TAGTARGETS_EDITIONUID 0x63C9
#define MATROSKA_ID_TAGTARGETS_CHAPTERUID 0x63C4
#define MATROSKA_ID_TAGTARGETS_ATTACHUID 0x63C6

#define MATROSKA_ID_SIMPLETAG 0x67C8
#define MATROSKA_ID_TAGNAME 0x45A3
#define MATROSKA_ID_TAGLANG 0x447A
#define MATROSKA_ID_TAGDEFAULT 0x44B4
#define MATROSKA_ID_TAGORIGINAL 0x4484
#define MATROSKA_ID_TAGBINARY 0x4485
#define MATROSKA_ID_TAGSTRING 0x4487

// seek head
#define MATROSKA_ID_SEEKHEAD 0x114D9B74
#define MATROSKA_ID_SEEKENTRY 0x4DBB
#define MATROSKA_ID_SEEKID 0x53AB
#define MATROSKA_ID_SEEKPOSITION 0x53AC

// tags
#define MATROSKA_TAG_ID_TITLE "TITLE"
#define MATROSKA_TAG_ID_AUTHOR "AUTHOR"
#define MATROSKA_TAG_ID_ARTIST "ARTIST"
#define MATROSKA_TAG_ID_ALBUM "ALBUM"
#define MATROSKA_TAG_ID_COMMENTS "COMMENTS"
#define MATROSKA_TAG_ID_COMMENT "COMMENT"
#define MATROSKA_TAG_ID_RATING "RATING"
#define MATROSKA_TAG_ID_BITSPS "BITSPS"
#define MATROSKA_TAG_ID_BPS "BPS"
#define MATROSKA_TAG_ID_BPM "BPM"
#define MATROSKA_TAG_ID_ENCODER "ENCODER"
#define MATROSKA_TAG_ID_ISRC "ISRC"
#define MATROSKA_TAG_ID_COPYRIGHT "COPYRIGHT"
#define MATROSKA_TAG_ID_COMPOSER "COMPOSER"
#define MATROSKA_TAG_ID_DIRECTOR "DIRECTOR"

#define MATROSKA_TAG_ID_TERMS_OF_USE "TERMS_OF_USE"
#define MATROSKA_TAG_ID_DATE "DATE"

#define MATROSKA_TAG_ID_LEAD_PERFORMER "LEAD_PERFORMER"
#define MATROSKA_TAG_ID_GENRE "GENRE"
#define MATROSKA_TAG_ID_TOTAL_PARTS "TOTAL_PARTS"
#define MATROSKA_TAG_ID_PART_NUMBER "PART_NUMBER"
#define MATROSKA_TAG_ID_PUBLISHER "PUBLISHER"

#define MATROSKA_TAG_ID_SUBTITLE "SUBTITLE"
#define MATROSKA_TAG_ID_ACCOMPANIMENT "ACCOMPANIMENT"
#define MATROSKA_TAG_ID_LYRICS "LYRICS"
#define MATROSKA_TAG_ID_CONDUCTOR "CONDUCTOR"
#define MATROSKA_TAG_ID_WRITTEN_BY "WRITTEN_BY"

#define MATROSKA_TAG_ID_ENCODED_BY "ENCODED_BY"
#define MATROSKA_TAG_ID_PRODUCER "PRODUCER"

#define MATROSKA_TAG_ID_DESCRIPTION "DESCRIPTION"
#define MATROSKA_TAG_ID_KEYWORDS "KEYWORDS"
#define MATROSKA_TAG_ID_DATE_RELEASED "DATE_RELEASED"
#define MATROSKA_TAG_ID_DATE_RECORDED "DATE_RECORDED"
#define MATROSKA_TAG_ID_DATE_ENCODED "DATE_ENCODED"
#define MATROSKA_TAG_ID_DATE_TAGGED "DATE_TAGGED"
#define MATROSKA_TAG_ID_DATE_DIGITIZED "DATE_DIGITIZED"
#define MATROSKA_TAG_ID_DATE_WRITTEN "DATE_WRITTEN"
#define MATROSKA_TAG_ID_DATE_PURCHASED "DATE_PURCHASED"
#define MATROSKA_TAG_ID_RECORDING_LOCATION "RECORDING_LOCATION"
#define MATROSKA_TAG_ID_PRODUCTION_COPYRIGHT "PRODUCTION_COPYRIGHT"

#ifdef SUPPORT_MKV_DRM
/* IDs in TracksData */
#define MATROSKA_ID_TRACKSDATA 0xDA

#define MATROSKA_ID_TRACKSDATAVER 0xDB
#define MATROSKA_ID_TRACKSDATASIZE 0xDC
#define MATROSKA_ID_TRACKSDATABUF 0xDE

/* IDs for DRMInfo */
#define MATROSKA_ID_DRMINFO 0xDD

#define DRM_FRAME_KEY_SIZE (10)  // DRMInfo size
#endif

#define MKV_NOPTS_VALUE (uint64)(0x8000000000000000ULL)
#define MKV_INVALID_SEEK_POS (uint64)(0xFFFFFFFFFFFFFFFFULL)

typedef enum {
    MATROSKA_TRACK_TYPE_NONE = 0x0,
    MATROSKA_TRACK_TYPE_VIDEO,
    MATROSKA_TRACK_TYPE_AUDIO,
    MATROSKA_TRACK_TYPE_COMPLEX,
    MATROSKA_TRACK_TYPE_LOGO = 0x10,
    MATROSKA_TRACK_TYPE_SUBTITLE,
    MATROSKA_TRACK_TYPE_CONTROL = 0x20,
} TrackType;

typedef enum {
    MATROSKA_TRACK_ENCODING_COMP_ALGO_ZLIB = 0,
    MATROSKA_TRACK_ENCODING_COMP_ALGO_BZLIB,
    MATROSKA_TRACK_ENCODING_COMP_ALGO_LZO,
    MATROSKA_TRACK_ENCODING_COMP_ALGO_HEADERSTRIP,
} TrackEncodingCompAlgo;

// EBML, Extensible Binary Meta Language
typedef struct {
    vint s_id;
    vint s_size;
    int64 s_offset;
    char* s_data;
} ebml_info;

typedef struct {
    int binary_size;
    char* binary_ptr;
    uint64 binary_offset;
} BinaryData;

typedef struct {
    char ebml_version;
    char ebml_reader_version;
    char ebml_id_maxlen;
    char ebml_size_maxlen;
    char ebml_doctype[EBML_DOCTYPE_LEN];
    char ebml_doctype_version;
    char ebml_doctype_reader_version;
} EBML_HeaderContext;

#define MAX_STRING_LEN 512

typedef struct {
    uint32 strlen;
    char str[MAX_STRING_LEN];
} StringOrUtf8;

typedef struct {
    uint16 str[MAX_STRING_LEN];
    uint32 strlen;
} UnicodeString;

typedef struct {
    char uid[16];
    StringOrUtf8 filename;
    char prev_uid[16];
    StringOrUtf8 prev_filename;
    char next_uid[16];
    StringOrUtf8 next_filename;
    unsigned int time_code_scale;
    float duration;
    StringOrUtf8 title;
    StringOrUtf8 muxing_app;
    StringOrUtf8 writing_app;
    uint64 date_utc;
    StringOrUtf8 strdate;
} SegmentInfo;

#define SEEK_ENTRY_INC_NUM 128

typedef struct {
    unsigned long seekid;
    uint64 seekpos;
} SeekEntry;

typedef struct {
    unsigned int MatrixCoefficients;
    unsigned int BitsPerChannel;
    unsigned int ChromaSubsamplingHorz;
    unsigned int ChromaSubsamplingVert;
    unsigned int CbSubsamplingHorz;
    unsigned int CbSubsamplingVert;
    unsigned int ChromaSitingHorz;
    unsigned int ChromaSitingVert;
    unsigned int Range;
    unsigned int TransferCharacteristics;
    unsigned int Primaries;
    unsigned int MaxCLL;
    unsigned int MaxFALL;
} VideoColour;

typedef struct {
    float PrimaryRChromaticityX;
    float PrimaryRChromaticityY;
    float PrimaryGChromaticityX;
    float PrimaryGChromaticityY;
    float PrimaryBChromaticityX;
    float PrimaryBChromaticityY;
    float WhitePointChromaticityX;
    float WhitePointChromaticityY;
    float LuminanceMax;
    float LuminanceMin;
} VideoColourMasteringMetadata;

typedef struct {
    unsigned int has_this_info;
    unsigned int pixel_width;
    unsigned int pixel_height;
    unsigned int flag_interlaced;
    unsigned int pixel_crop_bottom;
    unsigned int pixel_crop_top;
    unsigned int pixel_crop_left;
    unsigned int pixel_crop_right;
    unsigned int display_width;
    unsigned int display_height;
    unsigned int display_unit;
    float frame_rate;
} VideoSpecInfo;

typedef struct {
    unsigned int has_this_info;
    unsigned int sampling_frequency;
    unsigned int output_sampling_freq;
    unsigned int channels;
    unsigned int bitdepth;
    unsigned int bitrate;
} AudioSpecInfo;

typedef struct {
    unsigned int content_compression_algo;
    BinaryData content_compression_settings;
} ContentCompression;

typedef struct {
    unsigned int enc_algo;
    BinaryData enc_key_id;
    unsigned int aes_settings_cipher_mode;
} ContentEncryption;

typedef struct {
    unsigned int content_encoding_order;
    unsigned int content_encoding_scope;
    unsigned int content_encoding_type;
    ContentCompression compression;
    ContentEncryption encryption;
} ContentEncodingInfo;

#define TRACK_ENTRY_INC_NUM 8
#define TRACK_CONTECT_INC_NUM 2
#define ATTACH_LINK_INC_NUM 16

typedef struct {
    int size;
    uint8* data;
    uint64 pos;
    uint64 pts;
    uint32 flags;
    uint64 duration;
    int stream_index;
} mkv_packet;

#ifdef SUPPORT_MKV_DRM
typedef struct {
    uint32 version;
    uint32 drmHdrSize;
    uint8* pdrmHdr;
} Tracks_Data_t;
#endif

typedef struct {
    unsigned int track_num;
    unsigned int track_uid;
    unsigned int track_type;
    unsigned int flag_enabled;
    unsigned int flag_default;
    unsigned int flag_forced;
    unsigned int flag_lacing;
    unsigned int min_cache;
    unsigned int max_cache;
    unsigned int default_duration;
    float track_time_code_scale;
    StringOrUtf8 track_name;
    StringOrUtf8 track_language;
    StringOrUtf8 track_codecid;
    BinaryData codec_priv;
    StringOrUtf8 codec_name;
    int attach_link_count;
    unsigned int* attach_link_list;
    VideoSpecInfo vinfo;
    AudioSpecInfo ainfo;
    int content_encoding_count;
    ContentEncodingInfo* content_encoding_list;
    uint64 end_timecode;
    void* stream;
    bool is_sample_data_remained;
    unsigned int data_offset;
    mkv_packet packet;
    uint64 last_ts;
    bool seek_to_EOS;
    bool seek_to_BOS;
    uint64 curr_pos;
    bool is_not_new_segment;
    unsigned int codecDelay;
    unsigned int seekPreRoll;
    VideoColour* colorPtr;
    VideoColourMasteringMetadata* colorMetadataPtr;
} TrackEntry;

#define REFERENCE_BLOCK_INC_NUM 16

typedef struct {
    BinaryData block;
    int reference_block_count;
    int* reference_block_list;
    uint64 block_duration;
} BlockGroup;

#define CLUSTER_ENTRY_INC_NUM 16
#define BLOCK_GROUP_INC_NUM 256
#define SIMPLE_BLOCK_INC_NUM 256

#define CLUSTER_INDEX_INC_NUM 1024

typedef struct {
    uint64 timecode;
    unsigned int position;
    unsigned int prevsize;
    int group_count;
    BlockGroup* group_list;
    int simple_block_count;
    BinaryData* simple_block_list;
    // #ifdef SUPPORT_PART_CLUSTER
    int cluster_parser_group_not_finished;
    int cluster_parser_simple_not_finished;
    int group_count_next;
    int simple_block_count_next;
    int64 cur_cluster_off;
    int64 next_cluster_off;
    int64 cur_file_pos;  // check if seek/... operations occur between cluster
    int64 cur_time_off_ms;
    int total_part_num;  // for debug
    // #endif

    int cluster_not_finished;
    int64 cluster_left_length;
    int64 cluster_next_blk_offset;
    int64 cluster_time_off_ms;
    int64 cluster_s_off;
    int64 cluster_s_size;
    int64 cluster_offset;
    int64 cluster_cur_file_pos;
} ClusterEntry;

// #ifdef SUPPORT_PRE_SCAN_CLUSTER
typedef struct {
    int64 offset;    // location for current cluster
    int64 s_offset;  // data offset
    int64 s_size;    // data size
    uint64 timecode;
    unsigned int position;
    unsigned int prevsize;
} ClusterIndexNode;

// #endif

#define CUE_TRACK_POS_INC_NUM 16

typedef struct {
    unsigned int cue_track;
    unsigned int cue_block_number;
    uint64 cue_cluster_position;
} CueTrackPosition;

#define CUE_POINT_INC_NUM 128
#define CUE_TRACK_INC_NUM 8

typedef struct {
    unsigned int cue_time;
    int cue_track_count;
    CueTrackPosition* cue_track_list;
} CuePointEntry;

#define CHAPTER_TRACK_INC_NUM 16

typedef struct {
    int chapter_track_count;
    unsigned int* chapter_track_number;
} ChapterTracks;

typedef struct {
    StringOrUtf8 chapter_string;
    StringOrUtf8 chapter_language;
    StringOrUtf8 chapter_country;
} ChapterDisplay;

#define CHAPTER_EDITION_INC_NUM 16
#define CHAPTER_ATOM_INC_NUM 16
#define CHAPTER_DISPLAY_INC_NUM 16

typedef struct {
    unsigned int chapter_uid;
    unsigned int chapter_time_start;
    unsigned int chapter_time_end;
    unsigned int chapter_flag_hidden;
    unsigned int chapter_flag_enabled;
    unsigned int chapter_segment_uid;
    unsigned int chapter_segment_edition_uid;
    ChapterTracks chapter_track;
    int chapter_display_count;
    ChapterDisplay* chapter_display_list;
} ChapterAtom;

typedef struct {
    unsigned int edition_uid;
    unsigned int edition_flag_hidden;
    unsigned int edition_flag_default;
    unsigned int edition_flag_ordered;
    int chapter_atom_count;
    ChapterAtom* chapter_atom_list;
} ChapterEdition;

#define ATTACH_FILE_INC_NUM 16

typedef struct {
    StringOrUtf8 file_discription;
    StringOrUtf8 file_name;
    StringOrUtf8 file_minetype;
    BinaryData file_data;
    unsigned int file_uid;
} Attachments;

#define TAG_UID_INC_NUM 32

typedef struct {
    unsigned int target_type_value;
    StringOrUtf8 target_type;
    int track_uid_count;
    unsigned int* track_uid;
    int edition_uid_count;
    unsigned int* edition_uid;
    int chapter_uid_count;
    unsigned int* chapter_uid;
    int attach_uid_count;
    unsigned int* attach_uid;
} TagTarget;

typedef struct {
    int tag_name_count;
    StringOrUtf8* tag_name;
    StringOrUtf8 tag_language;
    unsigned int tag_original;
    unsigned int tag_default;
    StringOrUtf8 tag_string;
    BinaryData tag_binary;
} SimpleTag;

#define TAG_LIST_INC_NUM 16

typedef struct {
    TagTarget tag_target;
    int simple_tag_count;
    SimpleTag* simple_tag_list;
} TagEntry;

typedef struct {
    int seek_count;
    SeekEntry* seek_list;
    bool is_seek_head_read;

    int track_count;
    TrackEntry* track_list;
    bool is_tracks_read;
    bool has_video_track;

#ifdef SUPPORT_MKV_DRM
    bool bHasDRMHdr;
    Tracks_Data_t stDRM_Hdr;
#endif

    int cluster_count;
    ClusterEntry* cluster_list;
    // #ifdef SUPPORT_CORRUPT_CLUSTER
    int continuous_bad_cluster_cnt;
    // #endif

    int cue_count;
    CuePointEntry* cue_list;
    bool has_cue;
    bool cue_parsed;

    int chapter_count;
    ChapterEdition* chapter_list;
    ChapterMenu mkv_chapterMenu;  // for GetMetaData
    bool is_chapters_read;

    int attach_count;
    Attachments* attach_list;
    bool is_attatchments_read;

    int tag_count;
    TagEntry* tag_list;
    bool is_tags_read;

    SegmentInfo info;
    uint64 first_cluster_pos;
    bool is_seg_info_read;

    ClusterIndexNode* prescan_cluster_index_list;
    int prescan_cluster_index_count;  // current index
    int64 prescan_segment_offset;
    int64 prescan_segment_size;
    int prescan_cluster_interval;  // read one cluster every "interval" clusters
    int prescan_cluster_maxcnt;    // avoid heavy overhead MAX_CLUSTER_CNT
    int prescan_cluster_totalcnt;  // cluster list count
    bool prescan_cluster_done;
    int prescan_fuzzy_seek_enable;
} SEGM_HeaderContext;

typedef struct {
    uint32 buf_a;
    uint32 buf_b;
    uint32 bitcnt;
    uint32 buf_idx;
    uint8* buf_ptr;
    uint32 bufsize;
} BitsCtx;

typedef struct {
    uint64 pts;
    uint64 pos;
    uint32 flags;
    uint32 block_num;
} CueIndex;

typedef struct {
    uint32 coded_framesize;
    uint16 sub_packet_h;
    uint16 sub_packet_cnt;
    uint16 frame_size;
    uint16 sub_packet_size;
    uint16 block_align;
    uint16 pkt_cnt;
    uint8* audio_buf;
} RealAudioFormat;

#define STREAM_LIST_INC_NUM 16

typedef struct {
    uint8 force_enable;
    uint8 track_enable;
    uint8 stream_index;
    uint8 track_index;
    uint8 codec_type;
    TrackType track_type;
    int index_count;
    CueIndex* index_list;
    uint64 duration;
    uint64 position;
    uint64 last_seek_pos;
    uint8* codec_extra_data;
    uint32 codec_extra_size;

    uint64 last_pts;
    int last_pts_pos;
    int check_pts_pos;

    int num_packets;
    int packets_size;
    mkv_packet** packets;

    int has_ra_info;
    int has_wave_info;
    WaveFormatEx waveinfo;
    RealAudioFormat rainfo;
    RV_INFO rvinfo;

    int h264_mark_size;
    TrackExtTagList* ext_tag_list;
    uint8 get_codecdata_from_frame;
} mkvStream;

#define MKV_PKT_FLAG_KEY 0x0001  //(FLAG_SYNC_SAMPLE)
#define PKT_FLAG_KEY MKV_PKT_FLAG_KEY
#define PKT_FLAG_CODEC_DATA 0x40  //(FLAG_SAMPLE_CODEC_DATA)

#define PACKET_PTR_INC_NUM 16

typedef struct {
    ByteStream bs;
    StringOrUtf8 filename;

    uint64 ebml_master_size;
    uint64 ebml_header_size;

    uint64 segment_start;
    uint64 segm_master_size;
    uint64 segm_header_size;

    uint64 duration_us;

    int stream_count;
    mkvStream* stream_list;

    int done;
    mkv_packet* prev_pkt;

    uint64 skip_to_timecode;
    int skip_to_keyframe;

    EBML_HeaderContext ectx;
    SEGM_HeaderContext sctx;
    bool is_in_trick_mode;

    bool isLive;
    uint32 readMode;
#ifdef SUPPORT_MKV_DRM
    bool bHasDrmLib;
    void* hDRMLib;
    drmAPI_s sDrmAPI;

    uint8* drmContext; /* core parser allocate this memory and free it on finalizePlayback() */
    //    uint8 *drmHeader;     /* never free this memory, point to the strd's drmInfo field */
    //    uint32 drmHeadeSize;    /* size in bytes of the drm_raw_info */
    uint8 frameKey[DRM_FRAME_KEY_SIZE]; /* DRM INFO, used for  */
#endif
    int direction;
    uint32 flags;
    bool remaining_sample;
    bool switchTrack;
} MKVReaderContext;

int is_matroska_file_type(char* ptr, int size);
int read_matroska_file_header(MKVReaderContext* pctx);
int matroska_initialize_index(MKVReaderContext* pctx);
int matroska_import_index(MKVReaderContext* pctx, uint32 tracknum, uint8* buffer, uint32 size);
int matroska_export_index(MKVReaderContext* pctx, uint32 tracknum, uint8* buffer, uint32* size);
int matroska_is_seekable(MKVReaderContext* pctx);
int matroska_get_trackcount(MKVReaderContext* pctx);
int matroska_get_userdata(MKVReaderContext* pctx, uint32 id, uint8** buffer, uint32* size);
int matroska_get_artwork(MKVReaderContext* pctx, UserDataFormat* format, uint8** buffer,
                         uint32* size);
int matroska_get_chapter_menu(MKVReaderContext* pctx, uint8** pBuffer, uint32* dwLen);
int matroska_get_movie_duration(MKVReaderContext* pctx, uint64* duration);
int matroska_get_track_duration(MKVReaderContext* pctx, uint32 tracknum, uint64* duration);
int matroska_get_track_position(MKVReaderContext* pctx, uint32 tracknum, uint64* timestamp);
int matroska_get_track_type(MKVReaderContext* pctx, uint32 tracknum, uint32* mediaType,
                            uint32* decoderType, uint32* decoderSubtype);
int matroska_get_language(MKVReaderContext* pctx, uint32 tracknum, char* langcode);
int matroska_get_max_samplesize(MKVReaderContext* pctx, uint32 tracknum, uint32* size);
int matroska_get_bitrate(MKVReaderContext* pctx, uint32 tracknum, uint32* bitrate);
int matroska_get_track_crypto_key(MKVReaderContext* pctx, TrackEntry* track, uint32* len,
                                  uint8** data);
int matroska_get_sample_duration(MKVReaderContext* pctx, uint32 tracknum, uint64* duration);
int matroska_get_video_frame_width(MKVReaderContext* pctx, uint32 tracknum, uint32* width);
int matroska_get_video_frame_height(MKVReaderContext* pctx, uint32 tracknum, uint32* height);
int matroska_get_video_pixelbits(MKVReaderContext* pctx, uint32 tracknum, uint32* bitcount);
int matroska_get_audio_channels(MKVReaderContext* pctx, uint32 tracknum, uint32* channels);
int matroska_get_audio_samplerate(MKVReaderContext* pctx, uint32 tracknum, uint32* samplerate);
int matroska_get_audio_samplebits(MKVReaderContext* pctx, uint32 tracknum, uint32* samplebits);
int matroska_get_extra_data(MKVReaderContext* pctx, uint32 tracknum, uint8** data, uint32* size);
int matroska_get_wave_format(MKVReaderContext* pctx, uint32 tracknum, WaveFormatEx** waveinfo);
int matroska_track_seek(MKVReaderContext* pctx, uint32 tracknum, uint64 utime, uint32 flags);
int matroska_get_text_width(MKVReaderContext* pctx, uint32 tracknum, uint32* width);
int matroska_get_text_height(MKVReaderContext* pctx, uint32 tracknum, uint32* height);
int matroska_get_packet(MKVReaderContext* pctx, mkv_packet* packet, uint32 tracknum);
int matroska_file_seek(MKVReaderContext* pctx, uint64 utime, uint32 flags, uint32 trackType);
int matroska_get_packet_size(MKVReaderContext* pctx, uint32 tracknum);
int matroska_free_packet(MKVReaderContext* pctx, mkv_packet* packet);
int close_matroska_file_header(MKVReaderContext* pctx);
TrackEntry* matroska_find_track_by_num(MKVReaderContext* pctx, int track_num);

int matroska_get_next_packet_from_cluster(MKVReaderContext* pctx, uint32* tracknum);
TrackExtTagList* matroska_create_track_ext_taglist(ByteStream* pbs);
int matroska_add_track_ext_tag(ByteStream* pbs, TrackExtTagList* list, uint32 index, uint32 type,
                               uint32 size, uint8* data);
int matroska_delete_track_ext_taglist(ByteStream* pbs, TrackExtTagList* list);
int matroska_get_track_ext_taglist(MKVReaderContext* pctx, uint32 tracknum, TrackExtTagList** list);
int32 matroska_get_video_color_info(MKVReaderContext* pctx, uint32 tracknum, int32* primaries,
                                    int32* transfer, int32* coeff, int32* fullRange);
int32 matroska_get_video_hdr_color_info(MKVReaderContext* pctx, uint32 tracknum,
                                        VideoHDRColorInfo* pInfo);
int matroska_find_cluster(MKVReaderContext* pctx, uint64 target_ts);
int matroska_parse_cluster(MKVReaderContext* pctx, bool fileMode);
int close_segment_track_entry(MKVReaderContext* pctx, TrackEntry* track_entry);
int close_segment_cuepoint_entry(MKVReaderContext* pctx, CuePointEntry* cue_entry);
int close_segment_chapter_edition(MKVReaderContext* pctx, ChapterEdition* chapter_entry);
int close_segment_tags_entry(MKVReaderContext* pctx, TagEntry* tag_entry);
int close_segment_attachment(MKVReaderContext* pctx, Attachments* attach_entry);
int matroska_file_update_track(MKVReaderContext* pctx);
int clear_matroska_stream_list(MKVReaderContext* pctx);
void matroska_clear_queue(MKVReaderContext* pctx, uint32 tracknum);
void clear_matroska_cue_list(MKVReaderContext* pctx);
int matroska_parser_flush_track(MKVReaderContext* pctx);
void matroska_check_codec_data(MKVReaderContext* pctx, mkvStream* stream);
int matroska_get_codec_data_from_frame(MKVReaderContext* pctx);
int matroska_parse_codec_data_from_frame(MKVReaderContext* pctx, mkv_packet* packet,
                                         mkvStream* stream);
int matroska_add_VP9_codec_data(MKVReaderContext* pctx, mkv_packet* packet, mkvStream* stream);
int matroska_get_VP9_colorconfig(BitReader* bits, uint32 profile, uint32* bit_depth,
                                 int32* subsampling);
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  // MATROSKA_H
