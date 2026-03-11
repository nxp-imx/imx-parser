/*
***********************************************************************
* Copyright 2018, 2025-2026 NXP
* SPDX-License-Identifier: BSD-3-Clause
***********************************************************************
*/
/*****************************************************************************
* DsfParser.c
*
* Description:
* Implement for DSF parser.
*
****************************************************************************/
#ifdef WIN32
#include   <windows.h>
#endif

#include <string.h>

#include "fsl_types.h"
#include "file_stream.h"
#include "fsl_media_types.h"
#include "fsl_parser.h"
#include "DSFParserInner.h"

#define DSF_BLOCK_SIZE 4096

#define SEPARATOR " "
#define BASELINE_SHORT_NAME "BLN_MAD-MMLAYER_DSFPARSER_02.00.00"

static uint8 BitReverseTable[256];

#define SND_PCM_FORMAT_DSD_U32_LE 1
#define SND_PCM_FORMAT_DSD_U16_LE 2
#define SND_PCM_FORMAT_DSD_U8 3

#define OS_NAME "_LINUX"

/* user define suffix */
#define VERSION_STR_SUFFIX ""

#define CODEC_VERSION_STR \
    (BASELINE_SHORT_NAME OS_NAME \
    SEPARATOR VERSION_STR_SUFFIX \
    SEPARATOR "build on" \
    SEPARATOR __DATE__ SEPARATOR __TIME__)

const char * DSFVersionInfo()
{
    return (const char *)CODEC_VERSION_STR;
}

#define READ16BIT_FILE(val)  \
do \
{ \
    if(streamOps->Read(fileHandle, &(val), 2, context) < 2) \
        BAILWITHERROR(PARSER_READ_ERROR) \
}while (0)


#define READ32BIT_FILE(val)  \
do \
{ \
    if(streamOps->Read(fileHandle, &(val), 4, context) < 4) \
        BAILWITHERROR(PARSER_READ_ERROR) \
}while (0)


// little endian
#define FOURCC(ch0, ch1, ch2, ch3)  ((uint32)(uint8)(ch0)|((uint32)(uint8)(ch1)<<8)|((uint32)(uint8)(ch2)<<16)|((uint32)(uint8)(ch3)<<24))

#define FOURCC_DSD    FOURCC('D', 'S', 'D', ' ')
#define FOURCC_fmt     FOURCC('f', 'm', 't', ' ')
#define FOURCC_data    FOURCC('d', 'a', 't', 'a')

#define UINT32(buf) (uint32)*(buf)|((uint32)*(buf+1)<<8)|((uint32)*(buf+2)<<16)|((uint32)*(buf+3)<<24)
#define UINT64(buf) (uint64)*(buf)|((uint64)*(buf+1)<<8)|((uint64)*(buf+2)<<16)|((uint64)*(buf+3)<<24)| \
	((uint64)*(buf+4)<<32)|((uint64)*(buf+5)<<40)|((uint64)*(buf+6)<<48)|((uint64)*(buf+7)<<56)

#define CHUNK_ID_SIZE 4

/*
 * search chunkID, return -1 for not found, return chunkID offset if found
 */
int32 searchChunkID(uint8* buf, uint32 bufSize, uint32 chunkID)
{
    uint32 i = 0;

    if(!buf || bufSize < CHUNK_ID_SIZE)
        return -1;

    while(i + CHUNK_ID_SIZE - 1 < bufSize)
    {
        if(chunkID == FOURCC(buf[i], buf[i + 1], buf[i + 2], buf[i + 3]))
            break;
        i++;
    }
    if(i + CHUNK_ID_SIZE - 1 < bufSize)
        return i + CHUNK_ID_SIZE;
    else
        return -1;
}

void initBitReverseTable(){
    int i=0, j=0;
    memset(BitReverseTable, 0, 256);
    for(i=0; i<256; i++){
        for(j=0; j<8; j++)
            BitReverseTable[i] |= ( (i&(1<<j)) >> j) * (1 << (7-j));
    }
    return;
}

void interleaveDsfBlock(uint8 *dest, const uint8 *src, unsigned channels, unsigned format)
{
    unsigned i, c, j, bytes;

    if (format == SND_PCM_FORMAT_DSD_U32_LE)
        bytes = 4;
    else if (format == SND_PCM_FORMAT_DSD_U16_LE)
        bytes = 2;
    else
        bytes = 1;

    for (i = 0; i < DSF_BLOCK_SIZE / bytes; i++) {
        for (c = 0; c < channels; c++) {
                int dst_off = channels*bytes*i + c*bytes;
                int src_off = c*DSF_BLOCK_SIZE + i*bytes;
                for (j = 0; j < bytes; j++)
                    dest[dst_off + j] = src[src_off + bytes - j - 1];
        }
    }
}

int32  DSFCreateParser2(uint32 flags,
                       FslFileStream * streamOps,
                       ParserMemoryOps * memOps,
                       ParserOutputBufferOps * outputBufferOps,
                       void * context,
                       FslParserHandle * parserHandle)
{
    int32 err = PARSER_SUCCESS;
    DSFObjPtr self = NULL;
    FslFileHandle fileHandle = NULL;
    uint32 dwFormatChunkSize = 0;
    uint64 qwDataChunkSize = 0;

    bool bGotDSD = FALSE;
    bool bGotfmt = FALSE;
    bool bGotdata = FALSE;

	const uint32 BUF_SIZE = 128;
    uint8 buf[128];
    uint32 readLen = 0;

    if((NULL == streamOps) ||(NULL == memOps) ||(NULL == parserHandle)||(NULL == outputBufferOps))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    self = memOps->Calloc(1, sizeof(DSFObj));
    TESTMALLOC(self);

    self->m_context = context;
    self->m_tBufferOps = *outputBufferOps;
    self->m_tMemOps = *memOps;
    self->m_tStreamOps = *streamOps;
    self->hID3 = NULL;
    self->m_pBlockBuffer = NULL;

    if(flags & FLAG_ID3_FORMAT_NON_UTF8)
        self->bEnableConvert = FALSE;
    else
        self->bEnableConvert = TRUE;

    /* open file */
    fileHandle = streamOps->Open(NULL, (const uint8 *)"rb", context);
    if(fileHandle == NULL)
    {
        DSFMSG("DSFCreateParser: error: can not open source stream.\n");
        BAILWITHERROR(PARSER_FILE_OPEN_ERROR)
    }
    self->m_fileHandle = fileHandle;
    self->m_qwFileSize = (uint64) streamOps->Size(fileHandle,context);
    self->m_DSFTrack.m_dwCodecType = AUDIO_DSD;

    while(1)
    {
        uint64 qwSavePos = 0;
        uint32 dwMagic = 0;

        memset(buf, 0, sizeof(buf));

        if(bGotDSD && bGotfmt && bGotdata)
        {
            err = PARSER_SUCCESS;
            break;
        }

        qwSavePos = streamOps->Tell(fileHandle, context);
        if((readLen = streamOps->Read(fileHandle, buf, BUF_SIZE, context)) < CHUNK_ID_SIZE)
        {
            err = PARSER_EOS;
            break;
        }
		else
        {
            uint32 chunkID = (bGotDSD ? (bGotfmt ? FOURCC_data : FOURCC_fmt) : FOURCC_DSD);
            int32 matchOffset = searchChunkID(buf, readLen, chunkID);
            if(matchOffset >= 0)
                streamOps->Seek(fileHandle, matchOffset + qwSavePos, SEEK_SET, context);
            else {
                streamOps->Seek(fileHandle, qwSavePos + readLen - CHUNK_ID_SIZE + 1, SEEK_SET, context); // backward CHUNK_ID_SIZE - 1 bytes in case of chunk id is seperated by buf.
                continue;
            }

            dwMagic = chunkID;
        }

        if(dwMagic == FOURCC_DSD)
        {
            bGotDSD = TRUE;
			if((readLen = streamOps->Read(fileHandle, buf, 24, context)) < 24)
			{
				err = PARSER_EOS;
				break;
			}
			if(self->m_qwFileSize == 0)
				self->m_qwFileSize = UINT64(buf+4);
			self->m_qwMetaDataBeginOffset = UINT64(buf+16);

			// check m_qwMetaDataOffset valid
			if(self->m_qwFileSize > 0 && self->m_qwMetaDataBeginOffset > self->m_qwFileSize)
				self->m_qwMetaDataBeginOffset = 0;

        }
        else if(dwMagic == FOURCC_fmt)
        {
            uint64 sampleCount;
            bGotfmt = TRUE;

			// read chunk size
            if((readLen = streamOps->Read(fileHandle, buf, 8, context)) < 8)
            {
                err = PARSER_EOS;
                break;
            }
            dwFormatChunkSize = UINT64(buf);
            if(dwFormatChunkSize>BUF_SIZE)
            {
                DSFMSG("DsfParser: incorrect fmt chunk size \r\n", dwFormatChunkSize);
            }

            if((readLen = streamOps->Read(fileHandle, buf, dwFormatChunkSize - 12, context)) < (dwFormatChunkSize - 12))
            {
                err = PARSER_EOS;
                break;
            }

            self->m_DSFTrack.m_dwChnNum = UINT32(buf+12);
            if(self->m_DSFTrack.m_dwChnNum < 1 || self->m_DSFTrack.m_dwChnNum > 6)
            {
                err = PARSER_ERR_UNKNOWN;
                DSFMSG("DsfParser: incorrect channel num \r\n", self->m_DSFTrack.m_dwChnNum);
                break;
            }

            self->m_pBlockBuffer = (uint8*)self->m_tMemOps.Malloc(self->m_DSFTrack.m_dwChnNum * DSF_BLOCK_SIZE);
            TESTMALLOC(self->m_pBlockBuffer);

            self->m_DSFTrack.m_dwSampleRate = UINT32(buf+16);
            self->m_DSFTrack.m_dwSampleBits = UINT32(buf+20);
            if(self->m_DSFTrack.m_dwSampleBits == 1)
                initBitReverseTable();
            sampleCount = UINT64(buf+24);
            if(self->m_DSFTrack.m_dwSampleRate > 0)
                self->m_DSFTrack.m_qwDuration = sampleCount * 1000000 / self->m_DSFTrack.m_dwSampleRate;

            self->m_DSFTrack.m_dwBitRate = self->m_DSFTrack.m_dwSampleRate * self->m_DSFTrack.m_dwSampleBits * self->m_DSFTrack.m_dwChnNum;

        }
        else if (dwMagic == FOURCC_data)
        {
            bGotdata = TRUE;
            if((readLen = streamOps->Read(fileHandle, buf, 8, context)) < 8)
            {
                err = PARSER_EOS;
                break;
            }

            qwDataChunkSize = UINT64(buf);
            self->m_qwDataBeginOffset = (uint32)streamOps->Tell(fileHandle, context);
            if(qwDataChunkSize <= 0 || qwDataChunkSize + self->m_qwDataBeginOffset > self->m_qwFileSize)
            {
                DSFMSG("DSFCreateParser, warning, set org data chun size %lld to %lld\n",
                    qwDataChunkSize, self->m_qwFileSize - self->m_qwDataBeginOffset);
                qwDataChunkSize = self->m_qwFileSize - self->m_qwDataBeginOffset;
            }

            //some clip may have meta data in the end, so need record data end offset.
            //otherwise will push out meta data as media sample.
            self->m_qwDataEndOffset = self->m_qwDataBeginOffset + qwDataChunkSize - 12;
        }
        else
        {
            DSFMSG("DsfParser: unknown chunk type! \r\n", );
        }
    }

    if(PARSER_SUCCESS == err && self->m_qwMetaDataBeginOffset > 0){
        self->hID3 = NULL;
        err = ID3ParserCreate2(&(self->m_tStreamOps), &(self->m_tMemOps), (self->m_fileHandle),
            (self->m_context), &(self->hID3), self->bEnableConvert, self->m_qwMetaDataBeginOffset);
        if (PARSER_SUCCESS != err)
        {
            PARSERMSG("Failed to create ID3 Parser\n");
        }
        else
        {
            uint32 nID3V2_size = 0;
            err = ID3ParserGetID3V2Size(self->hID3, &(nID3V2_size));
            if (PARSER_SUCCESS != err)
            {
                DSFMSG("Failed to get ID3 V2 size\n");
            }
            DSFMSG("ID3 V2 size is %d Bytes\n", pParserBase->nBeginPoint);
        }
    }


bail:
    if( PARSER_SUCCESS != err)
    {
        if(fileHandle)
        {
            streamOps->Close(fileHandle, context);
        }
        SAFE_DELETE(self->m_pBlockBuffer);
        SAFE_DELETE(self);
    }
    else
    {
        streamOps->Seek(fileHandle, self->m_qwDataBeginOffset, SEEK_SET, context);
        *parserHandle = (FslParserHandle) self;

        DSFMSG("DSFCreateParser:parser created successfully\n");
    }

    return err;

}

int32  DSFCreateParser( bool    isLive,
                        FslFileStream * streamOps,
                        ParserMemoryOps * memOps,
                        ParserOutputBufferOps * outputBufferOps,
                        void * context,
                        FslParserHandle * parserHandle)
{
    uint32 flags = 0;
    if (isLive) {
        flags |= FILE_FLAG_NON_SEEKABLE;
        flags |= FILE_FLAG_READ_IN_SEQUENCE;
    }
    return DSFCreateParser2( flags, streamOps, memOps, outputBufferOps, context, parserHandle);

}

int32 DSFDeleteParser(FslParserHandle parserHandle)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if(parserHandle == NULL)
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if(self->m_fileHandle)
    {
        self->m_tStreamOps.Close(self->m_fileHandle, self->m_context);
        self->m_fileHandle = NULL;
    }

    if (self->hID3)
    {
        ID3ParserDelete(self->hID3);
    }

    SAFE_DELETE(self->m_pBlockBuffer);
    SAFE_DELETE(self);

    return PARSER_SUCCESS;
}


int32 DSFGetNumTracks(FslParserHandle parserHandle, uint32 * numTracks) /* single program interface */
{
    if((parserHandle == NULL) || (numTracks == NULL))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *numTracks = MAX_TRACK_NUM;

    return PARSER_SUCCESS;
}

int32 DSFEnableTrack(FslParserHandle parserHandle,
                     uint32 trackNum,
                     bool enable)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if((parserHandle == NULL) || (trackNum >= MAX_TRACK_NUM))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    self->m_DSFTrack.m_bEnable = enable;

    return PARSER_SUCCESS;
}


int32 DSFIsSeekable(FslParserHandle parserHandle, bool * seekable)
{

    if((parserHandle == NULL) || (seekable == NULL))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *seekable = TRUE;

    return PARSER_SUCCESS;
}

int32 DSFGetMovieDuration(FslParserHandle parserHandle,  uint64 * usDuration)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if((parserHandle == NULL) || (usDuration == NULL))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *usDuration = self->m_DSFTrack.m_qwDuration;

    return PARSER_SUCCESS;
}

int32 DSFGetTrackType(FslParserHandle parserHandle,
                      uint32 trackNum,
                      uint32 * mediaType,
                      uint32 * decoderType,
                      uint32 * decoderSubtype)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if( (parserHandle == NULL) || (trackNum >= MAX_TRACK_NUM) || (mediaType == NULL) ||
        (decoderType == NULL) || (decoderSubtype == NULL) )
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *mediaType = MEDIA_AUDIO;
    *decoderType = self->m_DSFTrack.m_dwCodecType;
    *decoderSubtype = 0;

    return PARSER_SUCCESS;
}

int32 DSFGetTrackDuration(FslParserHandle parserHandle,
                          uint32 trackNum,
                          uint64 * usDuration)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if((self == NULL) || (usDuration == NULL) || (trackNum >= MAX_TRACK_NUM))
    {
        DSFMSG("DSFGetTrackDuration, trackNum %d err\n", (int)trackNum);
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *usDuration = self->m_DSFTrack.m_qwDuration;

    return PARSER_SUCCESS;
}

int32 DSFGetBitRate(FslParserHandle parserHandle,
                    uint32 trackNum,
                    uint32 * bitrate)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if((parserHandle == NULL) || (bitrate == NULL) || (trackNum >= MAX_TRACK_NUM))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *bitrate = self->m_DSFTrack.m_dwBitRate;

    return PARSER_SUCCESS;
}


int32 DSFGetAudioSampleRate(FslParserHandle parserHandle,
                            uint32 trackNum,
                            uint32 * sampleRate)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if((parserHandle == NULL) || (sampleRate == NULL) || (trackNum >= MAX_TRACK_NUM))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *sampleRate = self->m_DSFTrack.m_dwSampleRate;

    return PARSER_SUCCESS;
}

int32 DSFGetAudioNumChannels(FslParserHandle parserHandle,
                             uint32 trackNum,
                             uint32 * numchannels)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if((parserHandle == NULL) || (numchannels == NULL) || (trackNum >= MAX_TRACK_NUM))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *numchannels = self->m_DSFTrack.m_dwChnNum;

    return PARSER_SUCCESS;
}

int32 DSFGetAudioBitsPerSample(FslParserHandle parserHandle,
                               uint32 trackNum,
                               uint32 * bitsPerSample)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if((parserHandle == NULL) || (bitsPerSample == NULL) || (trackNum >= MAX_TRACK_NUM))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *bitsPerSample = self->m_DSFTrack.m_dwSampleBits;

    return PARSER_SUCCESS;
}


int32 DSFGetReadMode(FslParserHandle parserHandle, uint32 * readMode)
{
    if((parserHandle == NULL) || (readMode == NULL))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *readMode = PARSER_READ_MODE_FILE_BASED;

    return PARSER_SUCCESS;
}

int32 DSFSetReadMode(FslParserHandle parserHandle, uint32 readMode)
{
    if((parserHandle == NULL) || (readMode != PARSER_READ_MODE_FILE_BASED))
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    return PARSER_SUCCESS;
}


int32 DSFGetFileNextSample(FslParserHandle parserHandle,
                           uint32 * trackNum,
                           uint8 ** sampleBuffer,
                           void  ** bufferContext,
                           uint32 * dataSize,
                           uint64 * usStartTime,
                           uint64 * usDuration,
                           uint32 * sampleFlags)
{
    DSFObjPtr self = (DSFObjPtr)parserHandle;
    uint32 dwActualReadSize = 0;
    uint8 *pbyBuf = 0;
    uint32 dwAllocSize = 0;
    uint64 qwCurPos = 0;
    uint32 dwDsfBlockSize = 0;
    uint32 i=0;

    if( (NULL == parserHandle) || (NULL==trackNum) || (NULL==sampleBuffer) || (NULL==bufferContext) ||
        (NULL==dataSize) || (NULL==usStartTime) || (NULL==usDuration) || (NULL==sampleFlags) )
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    if(self->m_DSFTrack.m_bEnable == FALSE)
    {
        return PARSER_ERR_TRACK_DISABLED;
    }

    if(self->m_bBeyondDataChunk)
    {
        return PARSER_EOS;
    }

    if (self->m_qwFileSize == (uint64)self->m_tStreamOps.Tell(self->m_fileHandle, self->m_context)) {
        return PARSER_EOS;
    }

    //alloc buffer
    dwDsfBlockSize = DSF_BLOCK_SIZE * self->m_DSFTrack.m_dwChnNum;
    dwAllocSize = dwDsfBlockSize;

    //read block
    dwActualReadSize = self->m_tStreamOps.Read(self->m_fileHandle, self->m_pBlockBuffer, dwDsfBlockSize, self->m_context);
    if((int)dwActualReadSize < 0)
    {
        DSFMSG("DSFGetFileNextSample, read frame err\n");
        return PARSER_READ_ERROR;
    }

    if(dwActualReadSize == 0)
    {
        return PARSER_EOS;
    }

    pbyBuf = self->m_tBufferOps.RequestBuffer(0, &dwAllocSize, bufferContext, self->m_context);
    if((pbyBuf == NULL) || (dwAllocSize < dwDsfBlockSize))
    {
        DSFMSG("RequestBuffer failed, pbyBuf %p, allocSize %d\n", pbyBuf, (int)dwAllocSize);
        if(dwAllocSize < dwDsfBlockSize)
            self->m_tBufferOps.ReleaseBuffer(0, pbyBuf, bufferContext, self->m_context);
        return PARSER_ERR_NO_OUTPUT_BUFFER;
    }

    qwCurPos = (uint64)self->m_tStreamOps.Tell(self->m_fileHandle, self->m_context);

    //some meta data is read, remove from buf
    if(qwCurPos > self->m_qwDataEndOffset)
    {
        uint32 dwOverlap = (uint32)(qwCurPos - self->m_qwDataEndOffset);
        if(dwActualReadSize > dwOverlap)
        {
            dwActualReadSize -= dwOverlap;
        }

        self->m_bBeyondDataChunk = TRUE;
    }

    if(dwActualReadSize < dwDsfBlockSize)
        memset(self->m_pBlockBuffer + dwActualReadSize, 0, dwDsfBlockSize - dwActualReadSize);

    if(self->m_DSFTrack.m_dwSampleBits == 1)
    {
        for(i=0; i<dwActualReadSize; i++)
        {
            self->m_pBlockBuffer[i] = BitReverseTable[self->m_pBlockBuffer[i]];
        }
    }

    interleaveDsfBlock(pbyBuf, self->m_pBlockBuffer, self->m_DSFTrack.m_dwChnNum, SND_PCM_FORMAT_DSD_U32_LE);// using U32_LE is from linux-test/test/mxc_alsa_dsd_player

    *trackNum = 0;
    *sampleBuffer = pbyBuf;
    *dataSize = dwActualReadSize;
    *sampleFlags = FLAG_SYNC_SAMPLE;
    *usStartTime = self->m_DSFTrack.m_qwCurStamp;

    if(self->m_DSFTrack.m_dwBitRate)
        *usDuration = (uint64)dwActualReadSize * 1000000 * 8 / self->m_DSFTrack.m_dwBitRate;

    self->m_DSFTrack.m_qwCurStamp += *usDuration;

    return PARSER_SUCCESS;
}


int32 DSFSeek(FslParserHandle parserHandle,
              uint32 trackNum,
              uint64 *usTime,
              uint32 flag)
{
    uint32 dwSecond = 0;
    uint32 dwBlockSize = 0;
    uint64 qwOffset = 0;
    DSFObjPtr self = (DSFObjPtr)parserHandle;
    (void)flag;

    if( (NULL == parserHandle) || (trackNum >= MAX_TRACK_NUM) || (usTime == NULL) )
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    dwSecond = (uint32)(*usTime/1000000);

    dwBlockSize = self->m_DSFTrack.m_dwChnNum * DSF_BLOCK_SIZE;
    qwOffset = self->m_qwDataBeginOffset + (dwSecond * self->m_DSFTrack.m_dwBitRate / 8)/dwBlockSize * dwBlockSize;

    self->m_bBeyondDataChunk = FALSE;

    //beyond
    if(qwOffset > self->m_qwFileSize)
    {
        qwOffset = self->m_qwFileSize;
        *usTime = self->m_DSFTrack.m_qwDuration;
        self->m_bBeyondDataChunk = TRUE;
    }

    self->m_tStreamOps.Seek(self->m_fileHandle, qwOffset, SEEK_SET, self->m_context);
    self->m_DSFTrack.m_qwCurStamp = (*usTime) / 1000000 * 1000000;

    DSFMSG("DSFSeek to %lld us, %lld\n", *usTime, qwOffset);

    return PARSER_SUCCESS;
}

int32 DSFGetMetaData(FslParserHandle parserHandle, UserDataID userDataId,
                                  UserDataFormat * userDataFormat, uint8 ** userData,
                                  uint32 * userDataLength )
{
    int32 ret = PARSER_SUCCESS;
    DSFObjPtr self = (DSFObjPtr)parserHandle;

    if( (parserHandle == NULL) || (userDataFormat == NULL) ||
        (userData == NULL) || (userDataLength == NULL) )
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    *userDataLength = 0;
    *userData = NULL;

    if(self->hID3 == NULL)
    {
        return PARSER_ERR_INVALID_PARAMETER;
    }

    ret = ID3ParserGetMetaData(self->hID3, userDataId, userDataFormat, userData, userDataLength);

    return ret;

}

int32 FslParserQueryInterface(uint32 id, void ** func)
{
    if(NULL==func)
        return PARSER_ERR_INVALID_PARAMETER;

    switch(id)
    {
    case PARSER_API_GET_VERSION_INFO:
        *func =(void*)DSFVersionInfo;
        break;

    case PARSER_API_CREATE_PARSER2:
        *func = (void*)DSFCreateParser2;
        break;

    case PARSER_API_CREATE_PARSER:
        *func = (void*)DSFCreateParser;
        break;

    case PARSER_API_DELETE_PARSER:
        *func =(void*)DSFDeleteParser;
        break;

    case PARSER_API_IS_MOVIE_SEEKABLE:
        *func =(void*)DSFIsSeekable;
        break;

    case PARSER_API_GET_MOVIE_DURATION:
        *func =(void*)DSFGetMovieDuration;
        break;

    case PARSER_API_GET_NUM_TRACKS:
        *func =(void*)DSFGetNumTracks;
        break;

    case PARSER_API_GET_TRACK_TYPE:
        *func = (void*)DSFGetTrackType;
        break;

    case PARSER_API_GET_TRACK_DURATION:
        *func = (void*)DSFGetTrackDuration;
        break;

    case PARSER_API_GET_BITRATE:
        *func = (void*)DSFGetBitRate;
        break;

    case PARSER_API_GET_AUDIO_NUM_CHANNELS:
        *func =(void*)DSFGetAudioNumChannels;
        break;

    case PARSER_API_GET_AUDIO_SAMPLE_RATE:
        *func = (void*)DSFGetAudioSampleRate;
        break;

    case PARSER_API_GET_AUDIO_BITS_PER_SAMPLE:
        *func = (void*)DSFGetAudioBitsPerSample;
        break;

    case PARSER_API_GET_READ_MODE:
        *func = (void*)DSFGetReadMode;
        break;

    case PARSER_API_SET_READ_MODE:
        *func = (void*)DSFSetReadMode;
        break;

    case PARSER_API_ENABLE_TRACK:
        *func = (void*)DSFEnableTrack;
        break;

    case PARSER_API_GET_FILE_NEXT_SAMPLE:
        *func = (void*)DSFGetFileNextSample;
        break;

    case PARSER_API_SEEK:
        *func = (void*)DSFSeek;
        break;

    case PARSER_API_GET_META_DATA:
        *func = DSFGetMetaData;
        break;

    default:
        *func=NULL;
        break;
    }

    return PARSER_SUCCESS;
}

