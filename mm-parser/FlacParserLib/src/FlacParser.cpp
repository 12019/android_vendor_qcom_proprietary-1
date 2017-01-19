/* =======================================================================
                              FlacParser.cpp
DESCRIPTION
  Meaningful description of the definitions contained in this file.
  Description must specify if the module is portable specific, mobile
  specific, or common to both, and it should alert the reader if the
  module contains any conditional definitions which tailors the module to
  different targets.

  Copyright(c) 2009-2013 by Qualcomm Technologies, Inc. All Rights Reserved
  Qualcomm Technologies Proprietary and Confidential.
========================================================================== */

/* =======================================================================
                             PERFORCE HEADER
$Header: //source/qcom/qct/multimedia2/Video/Source/FileDemux/FlacParserLib/main/latest/src/FlacParser.cpp#22 $
========================================================================== */
#include "parserdatadef.h"
#include "FlacParser.h"
#include "string.h"
#include "MMDebugMsg.h"
#include "MMMemory.h"
#include "MMMalloc.h"
#include "math.h"
#include <stdio.h>
//#define FLAC_PARSER_DEBUG 1
#ifndef __qdsp6__
//q6 compiler gives error for long long constant!
static const unsigned long long SS_MASK_CONST = (unsigned long long)0x0FFFFFFFFFULL;
#else
static const unsigned long long SS_MASK_CONST = ~((uint64)((uint64)0xFFFFFFF << 36));
#endif
    /* CRC8 table */
uint8 const FlacCParser_CRC8_table[256] = {
    0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15,
    0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
    0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65,
    0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
    0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5,
    0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
    0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85,
    0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
    0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2,
    0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
    0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2,
    0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
    0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32,
    0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
    0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42,
    0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
    0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C,
    0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
    0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC,
    0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
    0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C,
    0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
    0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C,
    0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
    0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B,
    0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
    0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B,
    0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
    0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB,
    0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
    0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB,
    0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
};


inline void CONVERT_TO_UPPER(uint8* pdata,int length)
{
  if(pdata)
  {
    for(int i =0; i < length;i++)
    {
      if( (pdata[i] >= 'a') && (pdata[i] <= 'z') )
      {
        pdata[i] = 'A'+pdata[i]-'a';
      }
    }
  }
}
/*
000 : get from STREAMINFO metadata block
001 : 8 bits per sample
010 : 12 bits per sample
011 : reserved
100 : 16 bits per sample
101 : 20 bits per sample
110 : 24 bits per sample
111 : reserved
*/
static const int32 nSampleSizes[8] =
{
   0,
   8,
   12,
   -1,
   16,
   20,
   24,
   -1
};
/*
0000 : get from STREAMINFO metadata block
0001 : 88.2kHz
0010 : 176.4kHz
0011 : 192kHz
0100 : 8kHz
0101 : 16kHz
0110 : 22.05kHz
0111 : 24kHz
1000 : 32kHz
1001 : 44.1kHz
1010 : 48kHz
1011 : 96kHz
1100 : get 8 bit sample rate (in kHz) from end of header
1101 : get 16 bit sample rate (in Hz) from end of header
1110 : get 16 bit sample rate (in tens of Hz) from end of header
1111 : invalid, to prevent sync-fooling string of 1s
*/
static const int32 nSampleRates[16] =
{
      0,
  88200,
 176400,
 192000,
   8000,
  16000,
  22050,
  24000,
  32000,
  44100,
  48000,
  96000,
      0,
      0,
      0,
     -1,
};

/*
0000-0111 : (number of independent channels)-1. Where defined, the channel order follows SMPTE/ITU-R recommendations. The assignments are as follows:
1 channel: mono
2 channels: left, right
3 channels: left, right, center
4 channels: left, right, back left, back right
5 channels: left, right, center, back/surround left, back/surround right
6 channels: left, right, center, LFE, back/surround left, back/surround right
7 channels: not defined
8 channels: not defined
1000 : left/side stereo: channel 0 is the left channel, channel 1 is the side(difference) channel
1001 : right/side stereo: channel 0 is the side(difference) channel, channel 1 is the right channel
1010 : mid/side stereo: channel 0 is the mid(average) channel, channel 1 is the side(difference) channel
1011-1111 : reserved
*/
static const int32 nChannels[16] =
{
    1,2,3,4,5,6,7,8,2,2,2,-1,-1,-1,-1, -1
};

#ifdef _ANDROID_
int std_CopyBE
(
  void *pvDest,      int nDestSize,
  const void *pvSrc, int nSrcSize,
  const char *pszFields
 )
{
  int index;
  uint8 *pSource = (uint8 *)pvSrc;
  uint8 *pDest   = (uint8 *)pvDest;
  int VarSize = 2;
  int nCount = 0;
  if (pSource == NULL || pDest == NULL ||
      nDestSize > nSrcSize)
  {
    return 0;
  }
  if('L' == *pszFields)
  {
    VarSize = 4;
  }
  else if('Q' == *pszFields)
  {
    VarSize= 8;
  }
  for (index = 0; index < nDestSize; ++index)
  {
    if((index) % VarSize == 0)
    {
      nCount++;
    }
    pDest [index] = pSource[VarSize*nCount - index - 1];
  }

  return nDestSize;
}
#endif

#ifdef FEATURE_FILESOURCE_FLAC_PARSER
/*! ======================================================================
@brief    FlacParser constructor

@detail  Instantiates FlacParser to parse flac stream.

@param[in]  pUData    APP's UserData.This will be passed back when this parser invokes the callbacks.
@return    N/A
@note      None
========================================================================== */
FlacParser::FlacParser(void* pUData,uint64 fsize,DataReadCallBack callback)
{
#ifdef FLAC_PARSER_DEBUG
  MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,"FlacParser");
#endif
  m_pUserData = pUData;
  m_nCurrOffset = 0;
  m_nFileSize = fsize;
  m_nFlacDataBufferSize = 0;
  m_nAudioStreams = 0;
  m_nClipDuration = 0;
  m_pDataBuffer = NULL;
  m_bFlacMetaDataParsed = false;
  m_pReadCallback = callback;
  m_eParserStatus = FLACPARSER_IDLE;
  m_pStreamInfoMetaBlock = NULL;
  m_pSeekTableMetaBlock= NULL;
  m_pPictureMetaBlock= NULL;
  m_pCurrentFrameHdr= NULL;
  m_pCodecHeader = NULL;
  m_nCodecHeaderSize = 0;
  m_nCurrentTimeStamp = 0;
  m_nSeekInfoArraySize = 0;
  m_nSeekInfoValidEntries = 0;
  m_pSeekInfoArray = NULL;
  m_pMetaData = NULL;
  m_nMetaData = 0;
}
/*! ======================================================================
@brief    FlacParser destructor

@detail  FlacParser destructor

@param[in] N/A
@return    N/A
@note      None
========================================================================== */
FlacParser::~FlacParser()
{
  if(m_pDataBuffer)
  {
    MM_Free(m_pDataBuffer);
  }
  if(m_pStreamInfoMetaBlock)
  {
    MM_Free(m_pStreamInfoMetaBlock);
  }
  if(m_pSeekTableMetaBlock)
  {
    if(m_pSeekTableMetaBlock->pSeekPoints)
    {
      MM_Free(m_pSeekTableMetaBlock->pSeekPoints);
    }
    MM_Free(m_pSeekTableMetaBlock);
  }
  if(m_pPictureMetaBlock)
  {
    MM_Free(m_pPictureMetaBlock);
  }
  if(m_pCurrentFrameHdr)
  {
    MM_Free(m_pCurrentFrameHdr);
  }
  if(m_pCodecHeader)
  {
    MM_Free(m_pCodecHeader);
  }
  if(m_pSeekInfoArray)
  {
      MM_Free(m_pSeekInfoArray);
  }
  if(m_pMetaData)
  {
    for(uint32 i = 0 ; i < m_nMetaData; i++)
    {
      if(m_pMetaData[i].pMetaData)
      {
        MM_Free(m_pMetaData[i].pMetaData);
      }
    }
    MM_Free(m_pMetaData);
  }
}

/*! ======================================================================
@brief  Starts Parsing the flac stream.

@detail  This function starts parsing the OGG stream.
        Upon successful parsing, user can retrieve total number of streams and
        stream specific information.

@param[in] N/A
@return    FLACPARSER_SUCCESS if parsing is successful otherwise returns appropriate error.
@note      StartParsing needs to be called before retrieving any stream specific information.
========================================================================== */
FlacParserStatus FlacParser::StartParsing(uint64& Offset, bool bForceSeek)
{
   FlacParserStatus nStatus = StartParsing(Offset);

   if(nStatus != FLACPARSER_SUCCESS)
   {
       return nStatus;
   }

   if(bForceSeek && m_eParserStatus == FLACPARSER_READY)
   {
       if( (m_pSeekTableMetaBlock)              &&
           (m_pSeekTableMetaBlock->nSeekPoints) &&
           (m_pSeekTableMetaBlock->pSeekPoints)
         )
       {
           return FLACPARSER_SUCCESS;
       }
#ifndef FLAC_FAST_STARTUP
       else
       {
           GenerateSeekTable();
       }
#endif
   }
   return FLACPARSER_SUCCESS;

}

/*! ======================================================================
@brief  generate the seek table.

@detail  This function starts parsing the OGG stream.
        Upon successful parsing, user can retrieve total number of streams and
        stream specific information.

@param[in] N/A
@return    FLACPARSER_SUCCESS if parsing is successful otherwise returns appropriate error.
@note      StartParsing needs to be called before retrieving any stream specific information.
========================================================================== */

void FlacParser::GenerateSeekTable(void )
{
  //uint8* dataBuffer = (uint8*)MM_Malloc(FLAC_PARSER_BUFFER_SIZE);
  uint64 nLocalOffset = m_nCurrOffset;
  uint32 nBytesNeed = FLAC_PARSER_BUFFER_SIZE;
  uint32 i = 0;
  uint32 nBytes = 0;
  uint32 nOffset = 0;
  uint32 nTempOffset = 0;


  if(!m_pSeekInfoArray)
  {
     m_nSeekInfoArraySize = (uint32)(m_nClipDuration /1000 + 10);
     m_pSeekInfoArray = (uint64*) MM_Malloc(m_nSeekInfoArraySize * 8);
     if(!m_pSeekInfoArray)
     {
         return;
     }
  }

  while(nLocalOffset <= m_nFileSize)
  {
      if( (nLocalOffset + nBytesNeed) > m_nFileSize)
      {
        nBytesNeed = (uint32)(m_nFileSize - m_nCurrOffset);
      }

      nBytes = m_pReadCallback(nLocalOffset,nBytesNeed,m_pDataBuffer,
                    FLAC_PARSER_BUFFER_SIZE,(uint32)m_pUserData);
      nOffset = 0;

      while(nBytes >= FLAC_MAX_HEADER_BYTES)
      {
         if(DecodeFrameHeader (m_pDataBuffer + nOffset,nBytes)
              == FLACPARSER_SUCCESS)
         {
            if((m_nCurrentTimeStamp <= i * 1000) &&
                m_pCurrentFrameHdr->nTimeStampMs >= i*1000)
            {
                if(i < m_nSeekInfoArraySize)
                {
                   m_pSeekInfoArray[i++] =  nLocalOffset + nOffset;
                }
            }
            m_nCurrentTimeStamp = m_pCurrentFrameHdr->nTimeStampMs;

         }
         nOffset++;
         nBytes --;
         if( FindNextFrameOffset(m_pDataBuffer + nOffset, nBytes, &nTempOffset)
                != FLACPARSER_SUCCESS)
         {
             nBytes = 0;
             continue;
         }
         nOffset += nTempOffset;
         nBytes -= nTempOffset;
      }
      nLocalOffset += nBytesNeed - nBytes;

  }
  m_nSeekInfoValidEntries = i;
  return;

}


/*! ======================================================================
@brief  Starts Parsing the flac stream.

@detail  This function starts parsing the OGG stream.
        Upon successful parsing, user can retrieve total number of streams and
        stream specific information.

@param[in] N/A
@return    FLACPARSER_SUCCESS if parsing is successful otherwise returns appropriate error.
@note      StartParsing needs to be called before retrieving any stream specific information.
========================================================================== */
FlacParserStatus FlacParser::StartParsing(uint64& Offset)
{
  FlacParserStatus retError = FLACPARSER_CORRUPT_DATA;
  uint64 localOffset = 0;
#ifdef FLAC_PARSER_DEBUG
  MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,"StartParsing");
#endif
  if(!m_pDataBuffer)
  {
    m_pDataBuffer = (uint8*)MM_Malloc(FLAC_PARSER_BUFFER_SIZE);
  }
  if(!m_pCurrentFrameHdr)
  {
    m_pCurrentFrameHdr = (flac_frame_header*)MM_Malloc(sizeof(flac_frame_header));
  }
  if((m_eParserStatus == FLACPARSER_READY) && m_pReadCallback)
  {
    //we have already parsed meta-data so nothing to parse
    retError = FLACPARSER_SUCCESS;
  }
  if( ((m_eParserStatus == FLACPARSER_INIT) ||
      (m_eParserStatus == FLACPARSER_IDLE) )    &&
      (m_pReadCallback)                         &&
      (m_pDataBuffer) )
  {
    memset(m_pDataBuffer,0,FLAC_PARSER_BUFFER_SIZE);
    uint32 nReadBytes = FLAC_PARSER_BUFFER_SIZE;

    if((Offset + nReadBytes) > m_nFileSize)
    {
       if(Offset >= m_nFileSize)
       {
           return FLACPARSER_INVALID_PARAM;
       }
       nReadBytes = (uint32)(m_nFileSize - Offset);
    }
    //We have not yet finished parsing the meta-data/meta-blocks
    if( !(m_pReadCallback(Offset,nReadBytes,
                          m_pDataBuffer,
                          FLAC_PARSER_BUFFER_SIZE,
                          (uint32)m_pUserData) ) )
    {
      retError = FLACPARSER_READ_ERROR;
      MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_FATAL,"StartParsing Read Failed...");
    }
    else
    {
      if(m_eParserStatus == FLACPARSER_IDLE)
      {
        //Make sure there is FLAC SIGNATURE
        if(!memcmp(m_pDataBuffer+localOffset,FLAC_SIGNATURE_BYTES,FLAC_SIGNATURE_SIZE))
        {
          uint64 nStartOffset = localOffset;
          MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,"StartParsing Located FLAC signature");
          retError = FLACPARSER_SUCCESS;
          localOffset += FLAC_SIGNATURE_SIZE;
          uint8 metadata_block_type = m_pDataBuffer[localOffset]& 0xEF;
          uint8 islastblock = (m_pDataBuffer[localOffset]& 0x80)>>7;
          localOffset++;
          uint32 metadatasize =
          (((uint32)m_pDataBuffer[localOffset])>>16)  |
          (((uint32)m_pDataBuffer[localOffset+1])>>8) |
          (((uint32)m_pDataBuffer[localOffset+2]));
          localOffset += (3*sizeof(uint8));
          bool bRet = true;
          m_eParserStatus = FLACPARSER_CORRUPT_DATA;
          MM_MSG_PRIO3(MM_FILE_OPS, MM_PRIO_HIGH,
        "StartParsing metadata_block_type %d islastblock %d metadatasize %lu",
        metadata_block_type, islastblock, metadatasize);
          switch(metadata_block_type)
          {
            case METADATA_BLOCK_STREAMINFO_TYPE:
            {
              MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,
                          "StartParsing METADATA_BLOCK_STREAMINFO_TYPE");
              if(m_pCodecHeader)
              {
                MM_Free(m_pCodecHeader);
                m_pCodecHeader = NULL;
                MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_FATAL,
                            "StartParsing m_pCodecHeader is not NULL!!!");
              }
              uint32 sizeNeeded = FLAC_SIGNATURE_SIZE +
                                  FLAC_METADATA_BLOCK_TYPE_BYTES+
                                  FLAC_METADATA_BLOCK_SIZE_BYTES+
                                  metadatasize;

              m_pCodecHeader = (uint8*)MM_Malloc(sizeNeeded);
              if(m_pCodecHeader)
              {
                memcpy(m_pCodecHeader,m_pDataBuffer+nStartOffset,sizeNeeded);
                m_nCodecHeaderSize = sizeNeeded;
              }
              bRet = ParseStreamInfoMetaBlock(localOffset,metadatasize);
              m_nAudioStreams++;
            }
            break;
            default:
            {
              MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_FATAL,"UNKNOWN METADATA_BLOCK in FLACPARSER_IDLE state..");
              bRet = SkipMetaBlock(localOffset,metadatasize);
            }
            break;
          }//switch(metadata_block_type)
          if(bRet)
          {
            m_eParserStatus = FLACPARSER_INIT;
            retError = FLACPARSER_SUCCESS;
          }
        }//if(!memcmp(m_pDataBuffer+localOffset,FLAC_SIGNATURE_BYTES,FLAC_SIGNATURE_SIZE))
      }//if(m_eParserStatus == FLACPARSER_IDLE)
      else if(m_eParserStatus == FLACPARSER_INIT)
      {
        uint64 nStartOffset = localOffset;
        uint8 metadata_block_type = m_pDataBuffer[localOffset]& 0x7F;
        uint8 islastblock = (m_pDataBuffer[localOffset]& 0x80)>>7;
        localOffset++;
        uint32 metadatasize =
          (((uint32)m_pDataBuffer[localOffset])<<16)  |
          (((uint32)m_pDataBuffer[localOffset+1])<<8) |
          (((uint32)m_pDataBuffer[localOffset+2]));
        localOffset += (3*sizeof(uint8));
        bool bRet = true;
        if(m_pCodecHeader)
        {
          uint32 newSizeNeeded = metadatasize +
            FLAC_METADATA_BLOCK_SIZE_BYTES + FLAC_METADATA_BLOCK_TYPE_BYTES;
          #ifdef FLAC_PARSER_DEBUG
          MM_MSG_PRIO1(MM_FILE_OPS, MM_PRIO_HIGH,
                       "Reallocating m_pCodecHeader new total size %lu",
                       (newSizeNeeded+m_nCodecHeaderSize) );
          #endif
          uint8* pTemp = (uint8*)MM_Realloc(m_pCodecHeader,(newSizeNeeded+m_nCodecHeaderSize) );
          if(pTemp)
          {
            m_pCodecHeader = pTemp;
            if(newSizeNeeded >= FLAC_PARSER_BUFFER_SIZE -  nStartOffset)
            {
              //If meta data size is huge read from file directly.
              if( !(m_pReadCallback(Offset + nStartOffset ,newSizeNeeded,
                   m_pCodecHeader+m_nCodecHeaderSize,
                    newSizeNeeded+m_nCodecHeaderSize,
                          (uint32)m_pUserData) ) )
              {
                   //QTV_MSG_PRIO(QTVDIAG_FILE_OPS, QTVDIAG_PRIO_FATAL,"ReadMetaData Failed...");
              }
            }
            else
            {
               memcpy(m_pCodecHeader+m_nCodecHeaderSize,
                   m_pDataBuffer+nStartOffset,newSizeNeeded);
            }
            m_nCodecHeaderSize += newSizeNeeded;
          }
        }
        #ifdef FLAC_PARSER_DEBUG
        MM_MSG_PRIO3(MM_FILE_OPS, MM_PRIO_HIGH,
         "StartParsing metadata_block_type %d islastblock %d metadatasize %lu",
         metadata_block_type, islastblock, metadatasize);
        #endif
        switch(metadata_block_type)
        {
          case METADATA_BLOCK_APPLICATION_TYPE:
          {
            #ifdef FLAC_PARSER_DEBUG
            MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,
                        "StartParsing METADATA_BLOCK_APPLICATION_TYPE");
            #endif
            bRet = SkipMetaBlock(localOffset,metadatasize);
          }
          break;
          case METADATA_BLOCK_SEEKTABLE_TYPE:
          {
            #ifdef FLAC_PARSER_DEBUG
            MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,
                        "StartParsing METADATA_BLOCK_SEEKTABLE_TYPE");
            #endif
            bRet = ParseSeekTableMetaBlock(localOffset,metadatasize);
          }
          break;
          case METADATA_BLOCK_VORBIS_COMMENT_TYPE:
          {
            #ifdef FLAC_PARSER_DEBUG
            MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,
                        "StartParsing METADATA_BLOCK_VORBIS_COMMENT_TYPE");
            #endif
            ParseCommentHdr((uint32)localOffset,metadatasize);
            bRet = SkipMetaBlock(localOffset,metadatasize);
          }
          break;
          case METADATA_BLOCK_CUESHEET_TYPE:
          {
            #ifdef FLAC_PARSER_DEBUG
            MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,
                        "StartParsing METADATA_BLOCK_CUESHEET_TYPE");
            #endif
            bRet = SkipMetaBlock(localOffset,metadatasize);
          }
          break;
          case METADATA_BLOCK_PICTURE_TYPE:
          {
            #ifdef FLAC_PARSER_DEBUG
            MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,
                        "StartParsing METADATA_BLOCK_PICTURE_TYPE");
            #endif
            bRet = SkipMetaBlock(localOffset,metadatasize);
          }
          break;
          default:
          {
            #ifdef FLAC_PARSER_DEBUG
            MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,
                        "StartParsing UNKNOWN METADATA_BLOCK");
            #endif
            bRet = SkipMetaBlock(localOffset,metadatasize);
          }
        }//switch(metadata_block_type)
        if(!bRet)
        {
          MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,
                      "StartParsing Parsing failed for Meta Data");
        }
        if(islastblock)
        {
          m_eParserStatus = FLACPARSER_READY;
          m_nCurrOffset = Offset + localOffset;
          MM_MSG_PRIO1(MM_FILE_OPS, MM_PRIO_HIGH,
                       "StartParsing FLACPARSER_READY m_nCurrOffset %llu",
                       m_nCurrOffset);
        }
        retError = FLACPARSER_SUCCESS;
      }
    }//end of else of if( !(*m_pReadCallback(localOffset,...
  }//end of if( ((m_eParserStatus == FLACPARSER_INIT) ||...
  Offset += localOffset;
  return retError;
}
/*! ======================================================================
@brief  Parses STREAMINFO metablock

@detail  This function parses STREAMINFO metablock.
        Upon successful parsing, user can retrieve stream specific information.

@param[in/out] localOffset Offset where given metablock Starts
@param[in]     size        Size of the given metablock
@return    true if parsing is successful otherwise returns false
@note
========================================================================== */
bool FlacParser::ParseStreamInfoMetaBlock(uint64& localOffset,uint32 size)
{
  bool bRet = false;
#ifdef FLAC_PARSER_DEBUG
  MM_MSG_PRIO3(MM_FILE_OPS, MM_PRIO_HIGH,
    "ParseStreamInfoMetaBlock Offset %llu or MetaDataBlock Size %lu FileSize %llu",
    localOffset, size, m_nFileSize);
#endif
  m_pStreamInfoMetaBlock = (flac_metadata_streaminfo*)
    MM_Malloc(sizeof(flac_metadata_streaminfo));
  if(m_pStreamInfoMetaBlock)
  {
    memset(m_pStreamInfoMetaBlock,0,sizeof(flac_metadata_streaminfo));

    m_pStreamInfoMetaBlock->nMinBlockSize =
          ( ((uint16)m_pDataBuffer[localOffset]) << 8)  |
          (((uint16)m_pDataBuffer[localOffset+1]));

    localOffset += sizeof(uint16);

    m_pStreamInfoMetaBlock->nMaxBlockSize =
          (((uint16)m_pDataBuffer[localOffset])<<8)  |
          (((uint16)m_pDataBuffer[localOffset+1]));
    localOffset += sizeof(uint16);

    m_pStreamInfoMetaBlock->nMinFrameSize =
        ( ((uint32)m_pDataBuffer[localOffset])<<16  )  |
        ( ((uint32)m_pDataBuffer[localOffset+1])<<8 )  |
        ( (uint32)m_pDataBuffer[localOffset+2]);
    localOffset += (3*sizeof(uint8));

    m_pStreamInfoMetaBlock->nMaxFrameSize =
         ( ((uint32)m_pDataBuffer[localOffset])<<16  )  |
        ( ((uint32)m_pDataBuffer[localOffset+1])<<8 )  |
        ( (uint32)m_pDataBuffer[localOffset+2]);
    localOffset += (3*sizeof(uint8));

    m_pStreamInfoMetaBlock->nSamplingRate =
        ( ((uint32)m_pDataBuffer[localOffset])<<16  )  |
        ( ((uint32)m_pDataBuffer[localOffset+1])<<8 )  |
        ( (uint32)m_pDataBuffer[localOffset+2]);

    m_pStreamInfoMetaBlock->nSamplingRate =
      ((m_pStreamInfoMetaBlock->nSamplingRate & 0xFFFFF0)>>4);

    m_pStreamInfoMetaBlock->nChannels =
      ((m_pDataBuffer[localOffset+2] & 0x0E) >> 1) + 1;

  //  memcpy(&(m_pStreamInfoMetaBlock->nBitsPerSample),m_pDataBuffer+ localOffset + 2,2);
    m_pStreamInfoMetaBlock->nBitsPerSample =
          (((m_pDataBuffer[localOffset + 2] & 0x1) << 4) |
          ((m_pDataBuffer[localOffset + 3] & 0xf0) >> 4)) + 1;
  //    ((m_pStreamInfoMetaBlock->nBitsPerSample & 0x01F0)>>4) + 1;
    localOffset += (3*sizeof(uint8));

    m_pStreamInfoMetaBlock->nTotalSamplesInStream =
    ( ((uint64)(m_pDataBuffer[localOffset]))  <<32 )  |
    ( ((uint64)(m_pDataBuffer[localOffset+1]))<<24 )  |
    ( ((uint64)(m_pDataBuffer[localOffset+2]))<<16 )  |
    ( ((uint64)(m_pDataBuffer[localOffset+3]))<<8  )  |
    ( (uint64)(m_pDataBuffer[localOffset+4]));

    m_pStreamInfoMetaBlock->nTotalSamplesInStream =
      (m_pStreamInfoMetaBlock->nTotalSamplesInStream & SS_MASK_CONST);

    localOffset += (5*sizeof(uint8));

    memcpy(m_pStreamInfoMetaBlock->MD5Signature,m_pDataBuffer + localOffset,MD5_SIG_SIZE);

    localOffset += MD5_SIG_SIZE;
    bRet = true;
    if(m_pStreamInfoMetaBlock->nSamplingRate)
    {
      m_nClipDuration = (uint64)
        ( ((float)m_pStreamInfoMetaBlock->nTotalSamplesInStream /
                    (float)m_pStreamInfoMetaBlock->nSamplingRate)*1000 );
    }
#ifdef FLAC_PARSER_DEBUG
    MM_MSG_PRIO4(MM_FILE_OPS, MM_PRIO_HIGH,
                 "ParseStreamInfoMetaBlock nMinBlockSize %d nMaxBlockSize %d \
                 nMinFrameSize %lu nMaxFrameSize %lu",
                    m_pStreamInfoMetaBlock->nMinBlockSize,
                    m_pStreamInfoMetaBlock->nMaxBlockSize,
                    m_pStreamInfoMetaBlock->nMinFrameSize,
                    m_pStreamInfoMetaBlock->nMaxFrameSize);
    MM_MSG_PRIO5(MM_FILE_OPS, MM_PRIO_HIGH,
                 "ParseStreamInfoMetaBlock SamplingRate %lu #Channels %d \
                 Bits/Sample %d Total#Samples %llu m_nClipDuration %llu",
                    m_pStreamInfoMetaBlock->nSamplingRate,
                    m_pStreamInfoMetaBlock->nChannels,
                    m_pStreamInfoMetaBlock->nBitsPerSample,
                    m_pStreamInfoMetaBlock->nTotalSamplesInStream,
                    m_nClipDuration);
#endif
  }
  return bRet;
}
/*! ======================================================================
@brief  Parses SEEKTABLE metablock

@detail  This function parses SEEKTABLE metablock.

@param[in/out] localOffset Offset where given metablock Starts
@param[in]     size        Size of the given metablock
@return    true if parsing is successful otherwise returns false
@note
========================================================================== */
bool FlacParser::ParseSeekTableMetaBlock(uint64& localOffset,uint32 size)
{
  bool bRet = false;
#ifdef FLAC_PARSER_DEBUG
  MM_MSG_PRIO3(MM_FILE_OPS, MM_PRIO_HIGH,
  "ParseSeekTableMetaBlock localOffset %llu MetaDataBlock size %lu FileSize %llu",
  localOffset, size, m_nFileSize);
#endif
  int nEntries = size /SEEK_POINT_SIZE_IN_BYTES;

  if(nEntries)
  {
    if(m_pSeekTableMetaBlock)
    {
      MM_Free(m_pSeekTableMetaBlock);
      m_pSeekTableMetaBlock = NULL;
      MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,
                    "Encountered > 1 SEEK TABLE, FREEING THE EXISTING ONE...");
    }
    m_pSeekTableMetaBlock = (flac_metadata_seektable*)MM_Malloc(sizeof(flac_metadata_seektable));
    if(m_pSeekTableMetaBlock)
    {
      memset(m_pSeekTableMetaBlock,0,sizeof(flac_metadata_seektable));
      m_pSeekTableMetaBlock->pSeekPoints = (flac_seek_point*)MM_Malloc(sizeof(flac_seek_point)*nEntries);
      if(m_pSeekTableMetaBlock->pSeekPoints)
      {
        m_pSeekTableMetaBlock->nSeekPoints = nEntries;
        memset(m_pSeekTableMetaBlock->pSeekPoints,0,sizeof(flac_seek_point)*nEntries);
        bRet = true;
      }
      for(int i = 0; i < nEntries && (m_pSeekTableMetaBlock->pSeekPoints); i++)
      {
        std_CopyBE(&m_pSeekTableMetaBlock->pSeekPoints[i].nSampleNoTargetFrame,
                   sizeof(uint64),
                   m_pDataBuffer+localOffset,
                   sizeof(uint64),
                   "Q");
        localOffset+= sizeof(uint64);

        std_CopyBE(&m_pSeekTableMetaBlock->pSeekPoints[i].nOffSetTargetFrameHeader,
                   sizeof(uint64),
                   m_pDataBuffer+localOffset,
                   sizeof(uint64),
                   "Q");
        localOffset+= sizeof(uint64);

        std_CopyBE(&m_pSeekTableMetaBlock->pSeekPoints[i].nNumberOfSampleInTargetFrame,
                   sizeof(uint16),
                   m_pDataBuffer+localOffset,
                   sizeof(uint16),
                   "S");
        localOffset+= sizeof(uint16);

        if(m_pStreamInfoMetaBlock && m_pStreamInfoMetaBlock->nSamplingRate)
        {
          m_pSeekTableMetaBlock->pSeekPoints[i].nTS =
            (uint32)((float)m_pSeekTableMetaBlock->pSeekPoints[i].nSampleNoTargetFrame /
            (float)m_pStreamInfoMetaBlock->nSamplingRate * 1000);
          m_pSeekTableMetaBlock->pSeekPoints[i].nDuration =
            (uint32)((float)m_pSeekTableMetaBlock->pSeekPoints[i].nNumberOfSampleInTargetFrame /
            (float)m_pStreamInfoMetaBlock->nSamplingRate * 1000);
        }
#ifdef FLAC_PARSER_DEBUG
        MM_MSG_PRIO3(MM_FILE_OPS, MM_PRIO_HIGH,
               "ParseSeekTableMetaBlock SeekPoint %d TS %llu Duration %llu",
               i, m_pSeekTableMetaBlock->pSeekPoints[i].nTS,
               m_pSeekTableMetaBlock->pSeekPoints[i].nDuration);
#endif
      }
    }
  }
  return bRet;
}
/*! ======================================================================
@brief  Parses PICTURE metablock

@detail  This function parses PICTURE metablock.

@param[in/out] localOffset Offset where given metablock Starts
@param[in]     size        Size of the given metablock
@return    true if parsing is successful otherwise returns false
@note
========================================================================== */
bool FlacParser::ParsePictureMetaBlock(uint64& localOffset,uint32 size)
{
  bool bRet = false;
#ifdef FLAC_PARSER_DEBUG
  MM_MSG_PRIO3(MM_FILE_OPS, MM_PRIO_HIGH,
               "ParsePictureMetaBlock Offset %llu or Size %lu FileSize %llu",
               localOffset, size, m_nFileSize);
#endif
  return bRet;
}
/*! ======================================================================
@brief  Skips the metablock

@detail  This function skips the metablock starting at given offset

@param[in/out] localOffset Offset where given metablock Starts
@param[in]     size        Size of the given metablock
@return    true if skipping is successful otherwise returns false
@note
========================================================================== */
bool FlacParser::SkipMetaBlock(uint64& localOffset,uint32 size)
{
  bool bRet = true;
#ifdef FLAC_PARSER_DEBUG
  MM_MSG_PRIO3(MM_FILE_OPS, MM_PRIO_HIGH,
      "SkipMetaBlock localOffset %llu or MetaDataBlock Size %lu FileSize %llu",
      localOffset, size, m_nFileSize);
#endif
  if( (localOffset + size)> m_nFileSize)
  {
    bRet = false;
    MM_MSG_PRIO(MM_FILE_OPS,MM_PRIO_FATAL,"SkipMetaBlock Invalid Offset/Size");
  }
  else
  {
    localOffset += size;
  }
  return bRet;
}
/* =============================================================================
FUNCTION:
 FlacParser::GetTrackWholeIDList

DESCRIPTION:
Returns trackId list for all the tracks in given clip.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
uint32 FlacParser::GetTrackWholeIDList(uint32* idList)
{
  MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_HIGH,"GetTrackWholeIDList");
  if(!idList)
  {
    return 0;
  }
  for (uint32 i = 0; i < m_nAudioStreams; i++)
  {
    (*idList) = 0;
    idList++;
  }
  return m_nAudioStreams;
}
/* =============================================================================
FUNCTION:
  FlacParser::GetClipDurationInMsec

DESCRIPTION:
 Returns clip duration from the current parsed data.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
uint64 FlacParser::GetClipDurationInMsec()
{
  return m_nClipDuration;
}


/* =============================================================================
FUNCTION:
 FlacParser::GetCurrentSample

DESCRIPTION:
Returns current sample for the given trackId.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
FlacParserStatus FlacParser::GetCurrentSample(uint32 trackId,uint8* dataBuffer,
                                              uint32 nMaxBufSize,
                                              int32* nBytesRead,
                                              bool bGetFrameBoundary)
{

  uint32          nBytesNeed  = GetFlacMaxBufferSize(0);
  uint32          nBytes      = 0;
  boolean         bFound      = false;
  uint32 nOffset = 0;
  uint32 nTempOffset = 0;

  if(!bGetFrameBoundary)
  {
      return GetCurrentSample(trackId,dataBuffer,
                              nMaxBufSize,
                              nBytesRead);
  }


  nBytesNeed = (nBytesNeed > nMaxBufSize)?nMaxBufSize:nBytesNeed;

  if(m_nCurrOffset >= m_nFileSize)
  {
      return FLACPARSER_EOF;
  }

  while(!bFound &&
        m_nCurrOffset <= m_nFileSize)
  {
      if( (m_nCurrOffset + nBytesNeed) > m_nFileSize)
      {
        nBytesNeed = (uint32)(m_nFileSize - m_nCurrOffset);
      }

      nBytes = m_pReadCallback(m_nCurrOffset,nBytesNeed,dataBuffer,
                    nMaxBufSize,(uint32)m_pUserData);
      if(nBytes < FLAC_MAX_HEADER_BYTES)
      {
       if(nBytes == 0)
       {
            return FLACPARSER_READ_ERROR;
         }
         *nBytesRead = nBytes;
         m_nCurrOffset += nBytes;
         return FLACPARSER_SUCCESS;
      }
      nOffset = 0;

      while(nBytes >= FLAC_MAX_HEADER_BYTES)
      {
         if( FindNextFrameOffset(dataBuffer + nOffset, nBytes, &nTempOffset)
                != FLACPARSER_SUCCESS)
         {
             m_nCurrOffset += nOffset + nBytes;
             nBytes = 0;
             continue;
         }
         nOffset += nTempOffset;
         nBytes -= nTempOffset;
         if(DecodeFrameHeader (dataBuffer + nOffset,nBytes)
              == FLACPARSER_SUCCESS)
         {

            if(m_nCurrentTimeStamp && (m_nCurrentTimeStamp >  m_pCurrentFrameHdr->nTimeStampMs ||
              (m_pCurrentFrameHdr->nTimeStampMs - m_nCurrentTimeStamp) > FLAC_VALID_FRAME_DETECT_THRESHOLD_MS))
            {
                //Flac framing is not robust, even we have checked everything for sync to valid
                // frame we will have some bad matches. Now how to eliminate a bad match.
                bFound = false;
              //  QTV_MSG_PRIO(QTVDIAG_FILE_OPS, QTVDIAG_PRIO_FATAL,"Potential Bad sync word");
            }
            else
            {
                m_nCurrOffset += nOffset;
                bFound = true;
                m_nCurrentTimeStamp = m_pCurrentFrameHdr->nTimeStampMs;
                break;
            }

         }
         nOffset++;
         nBytes --;
         if((int32)nBytes < FLAC_MAX_HEADER_BYTES)
         {
            m_nCurrOffset += nOffset;
         }
      }

  }


  if (nOffset != 0)
  {
      if( (m_nCurrOffset + nBytesNeed) > m_nFileSize)
      {
        nBytesNeed = (uint32)(m_nFileSize - m_nCurrOffset);
      }

      nBytes = m_pReadCallback(m_nCurrOffset,nBytesNeed,dataBuffer,
                    nMaxBufSize,(uint32)m_pUserData);

      if(nBytes < FLAC_MAX_HEADER_BYTES)
      {
         if(nBytes == 0)
         {
           *nBytesRead = 0;
            return FLACPARSER_READ_ERROR;
         }
        *nBytesRead = nBytes;
         m_nCurrOffset += nBytes;
         return FLACPARSER_SUCCESS;
      }
      nOffset = 0;
  }
  else if(nBytes <= FLAC_MAX_HEADER_BYTES)
  {
      *nBytesRead = nBytesNeed;
       m_nCurrOffset += nBytesNeed;
      return FLACPARSER_SUCCESS;
  }

  nOffset ++;
  nBytes --;
  while(nBytes >= FLAC_MAX_HEADER_BYTES)
  {
     nTempOffset = 0;
     if( FindNextFrameOffset(dataBuffer + nOffset, nBytes, &nTempOffset)
            != FLACPARSER_SUCCESS)
     {
         nBytes = 0;
         *nBytesRead = nBytesNeed;
         m_nCurrOffset += nBytesNeed;
         return FLACPARSER_SUCCESS;
     }

     nOffset += nTempOffset;
     nBytes -= nTempOffset;

     if(DecodeFrameHeader (dataBuffer + nOffset,nBytes)
          == FLACPARSER_SUCCESS)
     {
        if(!(m_nCurrentTimeStamp >  m_pCurrentFrameHdr->nTimeStampMs ||
            (m_pCurrentFrameHdr->nTimeStampMs - m_nCurrentTimeStamp) > FLAC_VALID_FRAME_DETECT_THRESHOLD_MS))
        {
           //Flac framing is not robust, even we have checked everything for sync to valid
           // frame we will have some bad matched. Now how to eliminate a bad match.
           m_nCurrOffset += nOffset;
          *nBytesRead = nOffset;
           return FLACPARSER_SUCCESS;
        }
     }

     nOffset++;
     nBytes --;
     if((int32)nBytes < FLAC_MAX_HEADER_BYTES)
     {
        *nBytesRead = nBytesNeed;
        m_nCurrOffset += nBytesNeed;
        return FLACPARSER_SUCCESS;
     }
  }
  return FLACPARSER_FAIL;
}
/* =============================================================================
FUNCTION:
 FlacParser::GetCurrentSample

DESCRIPTION:
Returns current sample for the given trackId.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
FlacParserStatus FlacParser::GetCurrentSample(uint32 trackId,uint8* dataBuffer,
                                              uint32 nMaxBufSize,
                                              int32* nBytesRead)
{
  FlacParserStatus retError = FLACPARSER_DEFAULT_ERROR;
  uint32 nBytesNeed = GetFlacMaxBufferSize(0);
  if( (m_nCurrOffset + nBytesNeed) > m_nFileSize)
  {
    nBytesNeed = (uint32)(m_nFileSize - m_nCurrOffset);
  }
  nBytesNeed = (nBytesNeed > nMaxBufSize)?nMaxBufSize:nBytesNeed;
#ifdef FLAC_PARSER_DEBUG
  MM_MSG_PRIO2(MM_FILE_OPS, MM_PRIO_HIGH,
               "GetCurrentSample m_nCurrOffset %llu nBytesNeed %lu",
               m_nCurrOffset, nBytesNeed);
#endif

  *nBytesRead =
    m_pReadCallback(m_nCurrOffset,nBytesNeed,dataBuffer,
                    nMaxBufSize,(uint32)m_pUserData);
  if(!(*nBytesRead))
  {
    retError = FLACPARSER_READ_ERROR;
    MM_MSG_PRIO(MM_FILE_OPS, MM_PRIO_FATAL,"StartParsing Read Failed...");
  }
  else
  {
    m_nCurrOffset+= *nBytesRead;
    retError = FLACPARSER_SUCCESS;
  }
  #ifdef FLAC_PARSER_DEBUG
  MM_MSG_PRIO1(MM_FILE_OPS, MM_PRIO_HIGH,
               "GetCurrentSample returning nBytesRead %ld",(*nBytesRead));
  #endif
  return retError;
}
/* =============================================================================
FUNCTION:
 FlacParser::GetCurrentSampleTimeStamp

DESCRIPTION:
Returns current sample timestamp for the given trackId.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
FlacParserStatus FlacParser::GetCurrentSampleTimeStamp(uint32 id,
                                                       uint64* ts)
{
  FlacParserStatus status = FLACPARSER_SUCCESS;

  *ts = m_nCurrentTimeStamp;

  return status;
}

/* =============================================================================
FUNCTION:
 FlacParser::DecodeFrameHeader

DESCRIPTION:
Returns current sample timestamp for the given trackId.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
FlacParserStatus  FlacParser::DecodeFrameHeader(uint8* pBuffer, uint32 nSize)
{
    uint8 *pTempBuffer = pBuffer;
    uint8 cCRC;
    uint8 nByte = 0;
    uint8 nOffset = 0;
    uint8 nBlockSize = 0;
    uint8 nSampleRate = 0;
    uint8 nSampleSize = 0;
    uint8 nChannel = 0;

    uint8 nBytes = 0;
    uint64 nPositionInfo = 0;
    uint32 nFixedBlockSize = m_pStreamInfoMetaBlock->nMaxBlockSize;

    if(!pBuffer || nSize < FLAC_MAX_HEADER_BYTES)
    {
        return FLACPARSER_DATA_UNDER_RUN;
    }

     //<14>  Sync code '11111111111110'


    m_pCurrentFrameHdr->nSyncCode = pTempBuffer[nOffset ++];
    nByte = pTempBuffer[nOffset++];
    // without bracket it is 0 always. nByte & 0xfc << 8
    m_pCurrentFrameHdr->nSyncCode |= ((nByte & 0xfc) << 8);

    // <1> Reserved
    /*<1>  Blocking strategy:

     �0 : fixed-blocksize stream; frame header encodes the frame number
     �1 : variable-blocksize stream; frame header encodes the sample number
    */
     m_pCurrentFrameHdr->nBlockingStrategy = nByte & 1; //0-fixed, 1-variable


    nByte = pTempBuffer[nOffset ++];

    // 4: BlockSize

    nBlockSize = (nByte & 0xf0) >> 4;

    // 4: Sample Rate
    nSampleRate = nByte & 0xf;

    if(nSampleRate == 0xf ||
        (nSampleRates[nSampleRate] != 0 &&
        nSampleRates[nSampleRate] != (int32)m_pStreamInfoMetaBlock->nSamplingRate))
    {
        return FLACPARSER_PARSE_ERROR;
    }

    nByte = pTempBuffer[nOffset ++];
    // No of Channels and Channel Assignment - 4bts


    nChannel = (nByte >> 4) & 0xf;

    if(nChannels[nChannel] == -1 ||
        (nChannels[nChannel] < 8 &&
        nChannels[nChannel] != m_pStreamInfoMetaBlock->nChannels))
    {
        return FLACPARSER_PARSE_ERROR;
    }

     //<3>Sample Size*/


    nSampleSize = (nByte >> 1) & 0x7;

    if(nSampleSizes[nSampleSize] == -1 ||
        (nSampleSizes[nSampleSize] &&
        nSampleSizes[nSampleSize] != m_pStreamInfoMetaBlock->nBitsPerSample))
    {
        return FLACPARSER_PARSE_ERROR;
    }
     // 1 Bit reserved */
    if(nByte & 1)
    {
        return FLACPARSER_PARSE_ERROR;
    }

  //If blocking strategy variable read sample number otherwise numblocks
    if(m_pCurrentFrameHdr->nBlockingStrategy ||
        (m_pStreamInfoMetaBlock->nMaxBlockSize !=
        m_pStreamInfoMetaBlock->nMinBlockSize))
    {
        uint64 nTempPos;
        ReadUTF8_uint64(pTempBuffer+nOffset, &nTempPos, &nBytes);
        if(nTempPos == (uint64)-1)
        {
            return FLACPARSER_PARSE_ERROR;
        }
        nPositionInfo = nTempPos;
    }
    else
    {
        uint32 nTempPos;
        ReadUTF8_uint32(pTempBuffer+nOffset, &nTempPos, &nBytes);
        if(nTempPos == (uint32)-1)
        {
            return FLACPARSER_PARSE_ERROR;
        }
        nPositionInfo = (uint64)nTempPos;
    }


    nOffset += nBytes;


    /*
     if(blocksize bits == 011x)
   8/16 bit (blocksize-1)
    */
    if(((nBlockSize & 0xE) == 0x6))
    {
        //nFixedBlockSize = pTempBuffer[nOffset++]; //Do we need to do anything with block size
                                              //if header gives it
        nOffset++;
        if(nBlockSize & 0x1)
        {
            //nFixedBlockSize = (nFixedBlockSize << 8) | pTempBuffer[nOffset++];
            nOffset++;
        }
      //  nFixedBlockSize += 1;
    }


    //if(sample rate bits == 11xx)
    //8/16 bit sample rate
    if((nSampleRate < 0xF) && (nSampleRate >= 0xC))
    {
        nOffset++;
        if(nSampleRate > 0xC)
        {
            nOffset++;//Do we need this TODO
        }
    }
    //Read CRC
    cCRC = pTempBuffer[nOffset];

    if(cCRC != Calculate_CRC8(pBuffer, nOffset))
    {
        return FLACPARSER_PARSE_ERROR;
    }

    if(m_pCurrentFrameHdr->nBlockingStrategy)
    {
        m_pCurrentFrameHdr->nTimeStampMs = nPositionInfo * 1000 /
                              m_pStreamInfoMetaBlock->nSamplingRate;
    }
    else
    {
        m_pCurrentFrameHdr->nTimeStampMs = nPositionInfo *
                              nFixedBlockSize * 1000 /
                              m_pStreamInfoMetaBlock->nSamplingRate;
    }
    return FLACPARSER_SUCCESS;
}

/* =============================================================================
FUNCTION:
 FlacParser::ReadUTF8_uint64

DESCRIPTION:
Returns current sample timestamp for the given trackId.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
void FlacParser::ReadUTF8_uint64(uint8 *pBuf,
                                           uint64 *nOutVal,
                                           uint8 *nBytes)
{
    uint64 nTempVal= (uint64)-1;
    uint8 nParsebyte = 0;
    int32 nIter=0;

    if(NULL == nOutVal)
    {
        return;
    } /* End if */

    nParsebyte = *pBuf++;


    /* No Bits */
    if(0 == (nParsebyte & 0x80))
    {
        /* 0xxxxxxx */
        nTempVal  = nParsebyte;
        nIter = 0;
    }
    else if(0xC0 == (nParsebyte & 0xE0))
    {
        /* 110xxxxx */
        nTempVal  = nParsebyte & 0x1F;
        nIter = 1;
    }
    else if(0xE0 == (nParsebyte & 0xF0))
    {
        /* 1110xxxx */
        nTempVal  = nParsebyte & 0x0F;
        nIter = 2;
    }
    else if(0xF0 == (nParsebyte & 0xF8))
    {
        /* 11110xxx */
        nTempVal  = nParsebyte & 0x07;
        nIter = 3;
    }
    else if(0xF8 == (nParsebyte & 0xFC))
    {
        /* 111110xx */
        nTempVal  = nParsebyte & 0x03;
        nIter  = 4;
    }
    else if(0xFC == (nParsebyte & 0xFE))
    {
        /* 1111110x */
        nTempVal  = nParsebyte & 0x01;
        nIter  = 5;
    }
    else if(nParsebyte & 0xFE && !(nParsebyte & 0x01))
    {
        /* 11111110 */
        nTempVal = 0;
        nIter = 6;
    } /* End if */


    nParsebyte = (uint8)0;
    *nBytes = (uint8)(nIter + 1);
    while(nIter > 0)
    {
        nParsebyte = *pBuf++;
        nTempVal <<= 6;
        nTempVal |=  (nParsebyte & 0x3F);
        nIter--;
    } /* End while */

    *nOutVal = nTempVal;

}/* End of FLACDec_ReadUTF8_uint64 */

/* =============================================================================
FUNCTION:
 FlacParser::ReadUTF8_uint32

DESCRIPTION:
Returns current sample timestamp for the given trackId.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
void FlacParser::ReadUTF8_uint32(uint8 *pBuf,
                                           uint32 *nOutVal,
                                           uint8 *nBytes)
{
    uint32 nTempVal= (uint32)-1;
    uint8 nParsebyte = 0;
    int32 nIter=0;

    if(NULL == nOutVal)
    {
        return;
    } /* End if */

    nParsebyte = *pBuf++;


    /* No Bits */
    if(0 == (nParsebyte & 0x80))
    {
        /* 0xxxxxxx */
        nTempVal  = nParsebyte;
        nIter = 0;
    }
    else if(0xC0 == (nParsebyte & 0xE0))
    {
        /* 110xxxxx */
        nTempVal  = nParsebyte & 0x1F;
        nIter = 1;
    }
    else if(0xE0 == (nParsebyte & 0xF0))
    {
        /* 1110xxxx */
        nTempVal  = nParsebyte & 0x0F;
        nIter = 2;
    }
    else if(0xF0 == (nParsebyte & 0xF8))
    {
        /* 11110xxx */
        nTempVal  = nParsebyte & 0x07;
        nIter = 3;
    }
    else if(0xF8 == (nParsebyte & 0xFC))
    {
        /* 111110xx */
        nTempVal  = nParsebyte & 0x03;
        nIter  = 4;
    }
    else if(0xFC == (nParsebyte & 0xFE))
    {
        /* 1111110x */
        nTempVal  = nParsebyte & 0x01;
        nIter  = 5;
    }

    nParsebyte = (uint8)0;
    *nBytes = (uint8)(nIter + 1);
    while(nIter > 0)
    {
        nParsebyte = *pBuf++;
        nTempVal <<= 6;
        nTempVal |=  (nParsebyte & 0x3F);
        nIter--;
    } /* End while */

    *nOutVal = nTempVal;

}/* End of FLACDec_ReadUTF8_uint64 */



/* =============================================================================
FUNCTION:
 FlacParser::Calculate_CRC8

DESCRIPTION:
Finds the offset inbytes to next Frame.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
uint8  FlacParser::Calculate_CRC8(uint8 *pData,
                                 uint32 nLen)
{
    uint8 Crc = 0;
    uint8 Crc1 = 0;

    if(NULL == pData || 0 == nLen )
    {
        return 0;
    } /* End if */


    while(nLen > 0)
    {
        Crc1 = Crc;
        Crc = FlacCParser_CRC8_table[Crc1 ^ *pData++];
        --nLen;
    } /* End while */
    return Crc;
} /* End of FLACDec_CRC8 */



/* =============================================================================
FUNCTION:
 FlacParser::FindNextFrameOffset

DESCRIPTION:
Finds the offset inbytes to next Frame.

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:

SIDE EFFECTS:
  None.
=============================================================================*/
FlacParserStatus  FlacParser::FindNextFrameOffset(uint8* pBuffer, uint32 nSize, uint32* nOffset)
{

    uint32 len = nSize;
    uint8* pTempBuf = pBuffer;
    uint8  nByte1 = 0;
    uint8  nByte2 = 0;
    if(!pBuffer || !nSize)
    {
        return FLACPARSER_INVALID_PARAM;
    }

    while(len--)
    {
        nByte1 = *pTempBuf++;
        while((0xFF == nByte1) && len)
        {
            nByte2 = *pTempBuf++;
            len--;

            if(0x3E == (nByte2>>2))
            {
                //found
                *nOffset = pTempBuf - pBuffer  - 2;
                return FLACPARSER_SUCCESS;
            }
            nByte1 = nByte2;
    }

    }

    return FLACPARSER_FAIL;
}



/*! ======================================================================
@brief  Repositions given track to specified time

@detail  Seeks given track in forward/backward direction to specified time

@param[in]
 trackid: Identifies the track to be repositioned.
 nReposTime:Target time to seek to
 nCurrPlayTime: Current playback time
 sample_info: Sample Info to be filled in if seek is successful

@return    FLACPARSER_SUCCESS if successful other wise returns FLACPARSER_FAIL
@note      None.
========================================================================== */
FlacParserStatus FlacParser::Seek(uint32, uint64 nReposTime, uint64 nCurrPlayTime,
                                  flac_stream_sample_info* sample_info, bool bForward)
{
  FlacParserStatus retError = FLACPARSER_FAIL;
  bool bSeekOK = false;
  if(sample_info)
  {
    memset(sample_info,0,sizeof(flac_stream_sample_info));
  }
  else
  {
    return FLACPARSER_INVALID_PARAM;
  }
  if(nReposTime == 0)
  {
    //Start from the begining, reset the offset and sampleinfo and return
    sample_info->ntime = 0;
    m_nCurrOffset = m_nCodecHeaderSize;
    m_nCurrentTimeStamp = sample_info->ntime;
    bSeekOK = true;
  }
  else if(nReposTime == m_nCurrentTimeStamp)
  {
    sample_info->ntime = m_nCurrentTimeStamp;
    bSeekOK = true;
  }
  else if( (m_pSeekTableMetaBlock)              &&
           (m_pSeekTableMetaBlock->nSeekPoints) &&
           (m_pSeekTableMetaBlock->pSeekPoints)
         )
  {
    if(bForward)
    {
      for( int i = m_pSeekTableMetaBlock->nSeekPoints-1; i >= 0; i--)
      {
        //If last indexed TS <= requested seek time but > current playback time,
        //we need to seek to such point
        if(i == int32(m_pSeekTableMetaBlock->nSeekPoints-1))
        {
          if(
              (m_pSeekTableMetaBlock->pSeekPoints[i].nTS <= nReposTime)  &&
              (m_pSeekTableMetaBlock->pSeekPoints[i].nTS > nCurrPlayTime)
            )
          {
             sample_info->ntime = m_pSeekTableMetaBlock->pSeekPoints[i].nTS;
             m_nCurrOffset =
               m_pSeekTableMetaBlock->pSeekPoints[i].nOffSetTargetFrameHeader +
               m_nCodecHeaderSize;
             bSeekOK = true;
             m_nCurrentTimeStamp = sample_info->ntime;
             break;
          }
        }//if(i == m_pSeekTableMetaBlock->nSeekPoints-1)
        else
        {
          //We want an indexed entry whose TS >= requested seek position
          //but it's previous indexed entry TS < requested seek position
          if(
              (i > 0)                                                           &&
              ((uint64)m_pSeekTableMetaBlock->pSeekPoints[i].nTS >= nReposTime)&&
              ((uint64)m_pSeekTableMetaBlock->pSeekPoints[i-1].nTS < nReposTime)
            )
          {
             sample_info->ntime = m_pSeekTableMetaBlock->pSeekPoints[i].nTS;
             m_nCurrOffset =
               m_pSeekTableMetaBlock->pSeekPoints[i].nOffSetTargetFrameHeader +
               m_nCodecHeaderSize;
             m_nCurrentTimeStamp = sample_info->ntime;
             bSeekOK = true;
             break;
          }
        }
      }//for( int i = m_pSeekTableMetaBlock->nSeekPoints-1; i >= 0; i--)
    }//if(bForward)
    else
    {
      for( uint32 i = 0; i < m_pSeekTableMetaBlock->nSeekPoints; i++)
      {
        //If last indexed TS <= requested seek time and  < current playback time,
        //we need to seek to such point
        if(i == m_pSeekTableMetaBlock->nSeekPoints-1)
        {
          if(
              (m_pSeekTableMetaBlock->pSeekPoints[i].nTS <= nReposTime)  &&
              (m_pSeekTableMetaBlock->pSeekPoints[i].nTS < nCurrPlayTime)
            )
          {
             sample_info->ntime = m_pSeekTableMetaBlock->pSeekPoints[i].nTS;
             m_nCurrOffset =
               m_pSeekTableMetaBlock->pSeekPoints[i].nOffSetTargetFrameHeader +
               m_nCodecHeaderSize;
             bSeekOK = true;
             m_nCurrentTimeStamp = sample_info->ntime;
             break;
          }
        }//if(i == m_pSeekTableMetaBlock->nSeekPoints-1)
        else
        {
          //We want an indexed entry whose TS <= requested seek position
          //but it's next indexed entry TS > requested seek position
          if(
              ((uint64)m_pSeekTableMetaBlock->pSeekPoints[i].nTS <= nReposTime)&&
              ((uint64)m_pSeekTableMetaBlock->pSeekPoints[i+1].nTS > nReposTime)
            )
          {
             sample_info->ntime = m_pSeekTableMetaBlock->pSeekPoints[i].nTS;
             m_nCurrOffset =
               m_pSeekTableMetaBlock->pSeekPoints[i].nOffSetTargetFrameHeader +
               m_nCodecHeaderSize;
             bSeekOK = true;
             m_nCurrentTimeStamp = sample_info->ntime;
             break;
          }
        }
      }//for( int i = 0; i < m_pSeekTableMetaBlock->nSeekPoints; i++)
    }
  }
#ifndef FLAC_FAST_STARTUP
  else if(m_pSeekInfoArray &&
          m_nSeekInfoArraySize &&
          m_nSeekInfoValidEntries)
  {
      uint64 nSeekTimeSec = nReposTime /1000;

      if(nSeekTimeSec < m_nSeekInfoValidEntries)
      {
          uint8* pTempBuf = (uint8*)MM_Malloc(FLAC_MAX_HEADER_BYTES);
          uint32 nBytes;
          if(!pTempBuf)
          {
              bSeekOK = false;
              return retError;;
          }
          m_nCurrOffset = m_pSeekInfoArray[nReposTime/1000];


          nBytes = m_pReadCallback(m_nCurrOffset,
                                   FLAC_MAX_HEADER_BYTES,pTempBuf,
                                   FLAC_MAX_HEADER_BYTES,
                                   (uint32)m_pUserData);
          if(DecodeFrameHeader (pTempBuf,FLAC_MAX_HEADER_BYTES)
              == FLACPARSER_SUCCESS)
          {
              bSeekOK = true;
              sample_info->ntime = m_pCurrentFrameHdr->nTimeStampMs;
              m_nCurrentTimeStamp = m_pCurrentFrameHdr->nTimeStampMs;

          }
          if(pTempBuf)
          {
              MM_Free(pTempBuf);
          }

      }

  }
#endif
#ifdef FLAC_APPROX_SEEK_MULTIPASS
  else
  {
      //Do an appoximate Seek here.
      if(nReposTime < m_nClipDuration)
      {
          uint32 nBytesNeed = FLAC_PARSER_BUFFER_SIZE;
          bool bFound = false;
          uint32 nBytes;
          uint32 nOffset = 0;
          uint32 nTempOffset = 0;
          int32  nOffsetAdjust = 0;
          float nSlope = 0;
          uint32 nLocalOffset;
          uint32 nPrevOffset = 0;
          uint64 nPrevTime = 0;
          uint32 nMaxIter = FLAC_APPROX_SEEK_MAX_PASS;

          nLocalOffset = (uint32)((float)nReposTime *
                 ((float)m_nFileSize / ((float)m_nClipDuration + 1)));

          nPrevOffset = 0;
          nPrevTime = 0;

          while(!bFound &&
                nLocalOffset < m_nFileSize)
          {
              if( (nLocalOffset + nBytesNeed) > m_nFileSize)
              {
                nBytesNeed = (uint32)(m_nFileSize - nLocalOffset);
              }
              //Fill our Buffer here and set out for a search for sync word.
              nBytes = m_pReadCallback(nLocalOffset,nBytesNeed,m_pDataBuffer,
                            FLAC_PARSER_BUFFER_SIZE,(uint32)m_pUserData);
              nOffset = 0;

              //Search sync word in the buffer.
              while(nBytes >= FLAC_MAX_HEADER_BYTES)
              {

                 //Searchfor next match for sync word
                 if( FindNextFrameOffset(m_pDataBuffer + nOffset, nBytes, &nTempOffset)
                        != FLACPARSER_SUCCESS)
                 {
                     nLocalOffset+=nBytes;
                     nBytes = 0;
                     continue;
                 }
                 nOffset += nTempOffset;
                 nBytes -= nTempOffset;

                 if(nBytes < FLAC_MAX_HEADER_BYTES)
                 {
                     nLocalOffset+=nOffset;
                     nBytes = 0;
                     continue;
                 }

                 //Check if current offset has a valid header.
                 if(DecodeFrameHeader (m_pDataBuffer + nOffset,nBytes)
                      == FLACPARSER_SUCCESS)
                 {
                    nLocalOffset += nOffset;
                    nMaxIter--;
                    break;
                 }
                 nOffset++;
                 nBytes --;

              }
              if(nBytes == 0)
              {
                  continue;
              }
              if(!nMaxIter ||
                  (FILESOURCE_ABS32((int64)m_pCurrentFrameHdr->nTimeStampMs - (int64)nReposTime)
                  <= FLAC_APPROX_SEEK_MAX_DEVIATION_MS))
              {
                 bFound = true;
                 bSeekOK = true;
                 if(((bForward) && (m_pCurrentFrameHdr->nTimeStampMs > m_nCurrentTimeStamp)) ||
                     (!bForward && (m_pCurrentFrameHdr->nTimeStampMs < m_nCurrentTimeStamp)))
                 {
                   m_nCurrOffset = nLocalOffset;
                   sample_info->ntime = m_pCurrentFrameHdr->nTimeStampMs;
                   m_nCurrentTimeStamp = m_pCurrentFrameHdr->nTimeStampMs;
                 }
                 break;
              }

              nSlope = (float)((int32)(nLocalOffset - nPrevOffset))/
                       (((float)( (uint64)(m_pCurrentFrameHdr->nTimeStampMs -
                                             nPrevTime)) /1000) + 1);

              nPrevOffset = nLocalOffset;
              nPrevTime = m_pCurrentFrameHdr->nTimeStampMs;

              nOffsetAdjust =  (int32)(( ((float) ((int32)(nReposTime -
                                m_pCurrentFrameHdr->nTimeStampMs) ) ) / 1000)
                                                                      *nSlope);

              nLocalOffset += nOffsetAdjust;
          }

      }

  }
#else
  else
  {
      //Do an appoximate Seek here.
      if(nReposTime < m_nClipDuration)
      {
          uint32 nBytesNeed = FLAC_PARSER_BUFFER_SIZE;
          bool bFound = false;
          uint32 nBytes;
          uint32 nOffset = 0;
          uint32 nTempOffset = 0;

          m_nCurrOffset = (uint32)(((float)m_nFileSize * nReposTime) / ((float)m_nClipDuration));

          while(!bFound &&
                m_nCurrOffset < m_nFileSize)
          {
              if( (m_nCurrOffset + nBytesNeed) > m_nFileSize)
              {
                nBytesNeed = (uint32)(m_nFileSize - m_nCurrOffset);
              }
              //Fill our Buffer here and set out for a search for sync word.
              nBytes = m_pReadCallback(m_nCurrOffset,nBytesNeed,m_pDataBuffer,
                            FLAC_PARSER_BUFFER_SIZE,(uint32)m_pUserData);
              nOffset = 0;

              //Search sync word in the buffer.
              while(nBytes >= FLAC_MAX_HEADER_BYTES)
              {
                 //Check if current offset has a valid header.
                 if(DecodeFrameHeader (m_pDataBuffer + nOffset,nBytes)
                      == FLACPARSER_SUCCESS)
                 {
                    m_nCurrOffset += nOffset;
                    bFound = true;
                    bSeekOK = true;
                    sample_info->ntime = m_pCurrentFrameHdr->nTimeStampMs;
                    m_nCurrentTimeStamp = m_pCurrentFrameHdr->nTimeStampMs;
                    break;
                 }
                 nOffset++;
                 nBytes --;
                 //Searchfor next match for sync word
                 if( FindNextFrameOffset(m_pDataBuffer + nOffset, nBytes, &nTempOffset)
                        != FLACPARSER_SUCCESS)
                 {
                     nBytes = 0;
                     continue;
                 }
                 nOffset += nTempOffset;
                 nBytes -= nTempOffset;
              }

          }

      }

  }
#endif
  if(bSeekOK)
  {
    retError = FLACPARSER_SUCCESS;
    MM_MSG_PRIO3(MM_FILE_OPS, MM_PRIO_HIGH,
                 "SEEK TO TS %llu nReposTime %lluoffset %llu ",
                 sample_info->ntime, nReposTime, m_nCurrOffset);
  }
  return retError;
}


/* ============================================================================
  @brief  Returns number of bits used for eacha audio sample

  @details    This function is used to return no. of bits used for each
              audio sample.

  @param[in]      trackId       Track Id number.

  @return     MKAV_API_SUCCESS indicating sample read successfully.
              Else, it will report corresponding error.
  @note       BufferSize should be more than maximum frame size value.
============================================================================ */
uint32 FlacParser::GetAudioBitsPerSample(uint32 trackId)
{
  uint32 ulBitWidth = 0;
  if(m_pStreamInfoMetaBlock)
  {
    ulBitWidth = m_pStreamInfoMetaBlock->nBitsPerSample;
  }
  return ulBitWidth;
}

/* =============================================================================
FUNCTION:
 FlacParser::GetAudioSamplingFrequency

DESCRIPTION:
Returns sampling frequency for given id
INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
 Sampling frequency
SIDE EFFECTS:
  None.
=============================================================================*/
uint32 FlacParser::GetAudioSamplingFrequency(uint32 id)
{
  uint32 sfreq = 0;
  if(m_pStreamInfoMetaBlock)
  {
    sfreq = m_pStreamInfoMetaBlock->nSamplingRate;
  }
  return sfreq;
}
/* =============================================================================
FUNCTION:
 FlacParser::GetNumberOfAudioChannels

DESCRIPTION:
Returns number of channels for given id
INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
 Number of channels
SIDE EFFECTS:
  None.
=============================================================================*/
uint8  FlacParser::GetNumberOfAudioChannels(uint32 id)
{
  uint8 noChannels = 0;
  if(m_pStreamInfoMetaBlock)
  {
    noChannels = m_pStreamInfoMetaBlock->nChannels;
  }
  return noChannels;
}
FlacParserStatus  FlacParser::GetFlacStreamInfo(uint32 id,flac_metadata_streaminfo* pinfo)
{
  FlacParserStatus eRet = FLACPARSER_INVALID_PARAM;
  if(pinfo)
  {
    if(m_pStreamInfoMetaBlock)
    {
      memcpy(pinfo,m_pStreamInfoMetaBlock,sizeof(flac_metadata_streaminfo));
      eRet = FLACPARSER_SUCCESS;
    }
  }
  return eRet;
}
/* =============================================================================
FUNCTION:
 FlacParser::GetTrackAverageBitRate

DESCRIPTION:
Returns avg. bit-rate for given id
INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
 Returns bit-rate for given track id
SIDE EFFECTS:
  None.
=============================================================================*/
uint32 FlacParser::GetTrackAverageBitRate(uint32 id)
{
  uint32 rate = 0;
  return rate;
}

/* =============================================================================
FUNCTION:
 FlacParser::GetFlacMaxBufferSize

DESCRIPTION:
Returns max buffer size for given id
INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
 Returns bit-rate for given track id
SIDE EFFECTS:
  None.
=============================================================================*/
uint32 FlacParser::GetFlacMaxBufferSize(uint32 id)
{
   if(m_pStreamInfoMetaBlock &&
       m_pStreamInfoMetaBlock->nMaxBlockSize &&
       m_pStreamInfoMetaBlock->nMaxBlockSize <
        FLAC_PARSER_BLOCK_LEN_BUFFER_THRESHOLD)
   {
      return FLAC_PARSER_BUFFER_SIZE;
   }
   else
   {
      return FLAC_PARSER_SUPERSET_BUFFER_SIZE;
   }
}

/* =============================================================================
FUNCTION:
 FlacParser::GetCodecHeader

DESCRIPTION:
Returns codec specific header for given trackid
INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
 Returns codec specific header for given trackid
SIDE EFFECTS:
  None.
=============================================================================*/
uint8* FlacParser::GetCodecHeader(uint32)
{
  return m_pCodecHeader;
}
/* =============================================================================
FUNCTION:
 FlacParser::GetCodecHeaderSize

DESCRIPTION:
Returns codec specific header size for given trackid
INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
 Returns codec specific header size for given trackid
SIDE EFFECTS:
  None.
=============================================================================*/
uint32 FlacParser::GetCodecHeaderSize(uint32)
{
  return m_nCodecHeaderSize;
}
/* =============================================================================
FUNCTION:
 FlacParser::IsMetaDataParsingDone

DESCRIPTION:
Returns true if all the required meta data is being parsed
INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
 Returns true if all the required meta data is being parsed otherwise, returns false
SIDE EFFECTS:
  None.
=============================================================================*/
bool FlacParser::IsMetaDataParsingDone()
{
  bool bRet = false;
  if(m_eParserStatus == FLACPARSER_READY)
  {
    bRet = true;
  }
  return bRet;
}
/* =============================================================================
FUNCTION:
 FlacParser::ParseCommentHdr

DESCRIPTION:
Parses and stores comment header

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
 None

SIDE EFFECTS:
  None.
=============================================================================*/
void FlacParser::ParseCommentHdr(uint32 localOffset, uint32 nCommentSize)
{
#ifdef FLAC_PARSER_DEBUG
MM_MSG_PRIO1(MM_FILE_OPS, MM_PRIO_HIGH,"ParseCommentHdr @ offset %lu",
               localOffset);
#endif
  uint32 vendor_length = 0;
  uint32 vendor_length_offset = localOffset;
  memcpy(&vendor_length, m_pDataBuffer+vendor_length_offset, sizeof(uint32) );
  uint32 user_comment_list_length = 0;
  uint32 user_comment_list_length_offset =
  vendor_length_offset+ vendor_length+sizeof(uint32);

  memcpy(&user_comment_list_length,
         m_pDataBuffer+user_comment_list_length_offset,
         sizeof(uint32) );

  if( (user_comment_list_length) && (!m_pMetaData) )
  {
    m_pMetaData = (flac_meta_data*)
      MM_Malloc( user_comment_list_length * sizeof(flac_meta_data) );

    if(m_pMetaData)
    {
      memset(m_pMetaData,0,user_comment_list_length * sizeof(flac_meta_data) );
      m_nMetaData = user_comment_list_length;
    }
  }
  uint32 user_comment_offset =
  user_comment_list_length_offset + sizeof(uint32) ;
  uint32  user_comment_length = 0;
  uint32  total_user_comment_length = (user_comment_list_length * sizeof(uint32));

  for(uint32 i = 0 ; i< user_comment_list_length;i++)
  {
    memcpy(&user_comment_length,
           m_pDataBuffer+user_comment_offset, sizeof(uint32) );
    if( (user_comment_length)&& (m_pMetaData) )
    {
      uint8* tempptr = (uint8*)MM_Malloc(user_comment_length+1);
      if(tempptr)
      {
        memcpy(tempptr,m_pDataBuffer+user_comment_offset+sizeof(uint32),user_comment_length);
        tempptr[user_comment_length]='\0';
        uint8* index1 = (uint8*)std_strstr((const char*)tempptr,"=");
        if(index1)
        {
          int fieldnamelength = index1-tempptr;
          for(int j = 0; j < FLAC_MAX_FIELDNAMES_SUPPORTED;j++)
          {
            CONVERT_TO_UPPER(tempptr,(user_comment_length-fieldnamelength));
            uint8* fname = (uint8*)&FLACFieldNames[j][0];
            if(
                (std_strlen((const char*)fname) == fieldnamelength)&&
                (!std_strncmp((const char*)fname,(const char*)tempptr,fieldnamelength) ) )
            {
              //We don't want '=' from index1, which makes room for \0
              int nfieldvallen= user_comment_length - fieldnamelength;
              if( (nfieldvallen > 0)&& (!m_pMetaData[i].bAvailable) )
              {
                m_pMetaData[i].pMetaData = (uint8*)MM_Malloc( nfieldvallen );
                if(m_pMetaData[i].pMetaData)
                {
                  memcpy(m_pMetaData[i].pMetaData,
                         m_pDataBuffer+user_comment_offset+sizeof(uint32)+fieldnamelength+1,
                         (nfieldvallen-1));
                  m_pMetaData[i].pMetaData[nfieldvallen-1]='\0';
                  m_pMetaData[i].nMetaDataFieldIndex = (uint16)i;
                  m_pMetaData[i].nMetaDataLength = (nfieldvallen-1);
                  m_pMetaData[i].bAvailable = true;
                  break;
                }//if(m_pOggMetaData[i].pMetaData)
              }//if(nfieldvallen > 0)
            }//if(strlen((const char*)fname) == fieldnamelength)
          }//for(int i = 0; i < FLAC_MAX_FIELDNAMES_SUPPORTED;i++)
        }//if(index1)
        MM_Free(tempptr);
      }//if(tempptr)
    }//if(user_comment_length)
    total_user_comment_length+= user_comment_length ;
    user_comment_offset += user_comment_length;
    user_comment_offset+= sizeof(uint32);
  }
}
/* =============================================================================
FUNCTION:
 OGGStreamParser::GetClipMetaData

DESCRIPTION:
Returns the clip meta data identified via nIndex

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
OGGSTREAM_SUCCESS if successful in parsing the header, otherwise, returns appropriate error.

SIDE EFFECTS:
  None.
=============================================================================*/
void FlacParser::GetClipMetaData(int nIndex,
                                 uint8* pMetadataValue,
                                 uint32* pMetadataLength)
{
  if(pMetadataLength)
  {
    //Count how many items are matching given index and total length
    //for all of such matching index values
    uint32 nCount = 0;
    int nMatched = 0;
    for(uint32 i =0; i< m_nMetaData; i++)
    {
      if( (m_pMetaData[i].nMetaDataFieldIndex == nIndex) &&
          (m_pMetaData[i].pMetaData) )
      {
        nCount += m_pMetaData[i].nMetaDataLength;
        nMatched++;
      }
    }
    //In case of multiple match, we separate them by ~
    //Add one for '\0'
    if(nMatched > 1)
    {
      nCount = nCount + (nMatched * sizeof("~"))+1;
    }
    else
    {
      nCount++;
    }
    if(!pMetadataValue)
    {
      *pMetadataLength = nCount;
    }
    else
    {
      int nToCopy = nMatched;
      //We have sufficient buffer, start copying
      if(*pMetadataLength >= nCount)
      {
        int writeindex=0;
        for(uint32 i =0; i< m_nMetaData; i++)
        {
          if( (m_pMetaData[i].nMetaDataFieldIndex == nIndex) &&
              (m_pMetaData[i].pMetaData) )
          {
            memcpy(pMetadataValue+writeindex,
                   m_pMetaData[i].pMetaData,
                   m_pMetaData[i].nMetaDataLength);
            writeindex+=m_pMetaData[i].nMetaDataLength;
            if(nToCopy >1)
            {
              memcpy(pMetadataValue+writeindex,"~",1);
              writeindex++;
              nToCopy--;
            }
          }
        }
        pMetadataValue[writeindex] ='\0';
      }//if(*pMetadataLength >= nCount)
    }//end of else of if(!pMetadataValue)
  }//if(pMetadataLength)
}
/* =============================================================================
FUNCTION:
 OGGStreamParser::GetClipMetaDataIndex

DESCRIPTION:
Retrieves the clip meta data index for the given pFieldname

INPUT/OUTPUT PARAMETERS:

RETURN VALUE:
OGGSTREAM_SUCCESS if successful in parsing the header, otherwise, returns appropriate error.

SIDE EFFECTS:
  None.
=============================================================================*/
void  FlacParser::GetClipMetaDataIndex(uint8* pFieldname,
                                       int nLengthFieldName,
                                       int* pFieldIndex)
{
  if(pFieldname && pFieldIndex && nLengthFieldName)
  {
    for(int i = 0; i < FLAC_MAX_FIELDNAMES_SUPPORTED;i++)
    {
      CONVERT_TO_UPPER(pFieldname,nLengthFieldName);
      uint8* fname = (uint8*)&FLACFieldNames[i][0];
      if(
          (std_strlen((const char*)fname) == nLengthFieldName)&&
          (!std_strncmp((const char*)fname,(const char*)pFieldname,nLengthFieldName) )
        )
      {
        *pFieldIndex = i;
        break;
      }
    }
  }
}
#endif //#define FEATURE_FILESOURCE_FLAC_PARSER

