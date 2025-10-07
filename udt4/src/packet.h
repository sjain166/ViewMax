/*****************************************************************************
Copyright (c) 2001 - 2011, The Board of Trustees of the University of Illinois.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.

* Redistributions in binary form must reproduce the
  above copyright notice, this list of conditions
  and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the University of Illinois
  nor the names of its contributors may be used to
  endorse or promote products derived from this
  software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*****************************************************************************/

/*****************************************************************************
written by
   Yunhong Gu, last updated 01/02/2011
*****************************************************************************/

#ifndef __UDT_PACKET_H__
#define __UDT_PACKET_H__


#include "udt.h"

#ifdef WIN32
   struct iovec
   {
      int iov_len;
      char* iov_base;
   };
#endif

class CChannel;

class CPacket
{
friend class CChannel;
friend class CSndQueue;
friend class CRcvQueue;

public:
   int32_t& m_iSeqNo;                   // alias: sequence number
   int32_t& m_iMsgNo;                   // alias: message number
   int32_t& m_iTimeStamp;               // alias: timestamp
   int32_t& m_iID;			// alias: socket ID
   char*& m_pcData;                     // alias: data/control information

   static const int m_iPktHdrSize;	// packet header size

public:
   CPacket();
   ~CPacket();

      // Functionality:
      //    Get the payload or the control information field length.
      // Parameters:
      //    None.
      // Returned value:
      //    the payload or the control information field length.

   int getLength() const;

      // Functionality:
      //    Set the payload or the control information field length.
      // Parameters:
      //    0) [in] len: the payload or the control information field length.
      // Returned value:
      //    None.

   void setLength(int len);

      // Functionality:
      //    Pack a Control packet.
      // Parameters:
      //    0) [in] pkttype: packet type filed.
      //    1) [in] lparam: pointer to the first data structure, explained by the packet type.
      //    2) [in] rparam: pointer to the second data structure, explained by the packet type.
      //    3) [in] size: size of rparam, in number of bytes;
      // Returned value:
      //    None.

   void pack(int pkttype, void* lparam = NULL, void* rparam = NULL, int size = 0);

      // Functionality:
      //    Read the packet vector.
      // Parameters:
      //    None.
      // Returned value:
      //    Pointer to the packet vector.

   iovec* getPacketVector();

      // Functionality:
      //    Read the packet flag.
      // Parameters:
      //    None.
      // Returned value:
      //    packet flag (0 or 1).

   int getFlag() const;

      // Functionality:
      //    Read the packet type.
      // Parameters:
      //    None.
      // Returned value:
      //    packet type filed (000 ~ 111).

   int getType() const;

      // Functionality:
      //    Read the extended packet type.
      // Parameters:
      //    None.
      // Returned value:
      //    extended packet type filed (0x000 ~ 0xFFF).

   int getExtendedType() const;

      // Functionality:
      //    Read the ACK-2 seq. no.
      // Parameters:
      //    None.
      // Returned value:
      //    packet header field (bit 16~31).

   int32_t getAckSeqNo() const;

      // Functionality:
      //    Read the message boundary flag bit.
      // Parameters:
      //    None.
      // Returned value:
      //    packet header field [1] (bit 0~1).

   int getMsgBoundary() const;

      // Functionality:
      //    Read the message inorder delivery flag bit.
      // Parameters:
      //    None.
      // Returned value:
      //    packet header field [1] (bit 2).

   bool getMsgOrderFlag() const;

      // Functionality:
      //    Read the message sequence number.
      // Parameters:
      //    None.
      // Returned value:
      //    packet header field [1] (bit 3~31).

   int32_t getMsgSeq() const;

      // Functionality:
      //    Read the frame ID (for VR streaming).
      // Parameters:
      //    None.
      // Returned value:
      //    Frame ID (16 bits: 0-65535).

   int32_t getFrameID() const;

      // Functionality:
      //    Set the frame ID (for VR streaming).
      // Parameters:
      //    0) [in] frame_id: Frame ID to set (0-65535).
      // Returned value:
      //    None.

   void setFrameID(int32_t frame_id);

      // Functionality:
      //    Read the chunk ID within a frame (for VR streaming).
      // Parameters:
      //    None.
      // Returned value:
      //    Chunk ID (8 bits: 0-255).

   int32_t getChunkID() const;

      // Functionality:
      //    Set the chunk ID (for VR streaming).
      // Parameters:
      //    0) [in] chunk_id: Chunk ID to set (0-255).
      // Returned value:
      //    None.

   void setChunkID(int32_t chunk_id);

      // Functionality:
      //    Read the total number of chunks in the frame (for VR streaming).
      // Parameters:
      //    None.
      // Returned value:
      //    Total chunks (8 bits: 0-255).

   int32_t getTotalChunks() const;

      // Functionality:
      //    Set the total number of chunks in the frame (for VR streaming).
      // Parameters:
      //    0) [in] total_chunks: Total chunks to set (0-255).
      // Returned value:
      //    None.

   void setTotalChunks(int32_t total_chunks);

      // Functionality:
      //    Read the frame deadline timestamp (for VR streaming).
      //    This reuses the timestamp field (m_nHeader[2]) which UDT doesn't actually use.
      // Parameters:
      //    None.
      // Returned value:
      //    Frame deadline in microseconds (32 bits).

   int64_t getFrameDeadline() const;

      // Functionality:
      //    Set the frame deadline timestamp (for VR streaming).
      //    This reuses the timestamp field (m_nHeader[2]).
      // Parameters:
      //    0) [in] deadline_us: Deadline timestamp in microseconds.
      // Returned value:
      //    None.

   void setFrameDeadline(int64_t deadline_us);

      // Functionality:
      //    Clone this packet.
      // Parameters:
      //    None.
      // Returned value:
      //    Pointer to the new packet.

   CPacket* clone() const;

protected:
   uint32_t m_nHeader[5];               // The 160-bit header field (extended for VR frame metadata)
   iovec m_PacketVector[2];             // The 2-demension vector of UDT packet [header, data]

   int32_t __pad;

protected:
   CPacket& operator=(const CPacket&);
};

////////////////////////////////////////////////////////////////////////////////

class CHandShake
{
public:
   CHandShake();

   int serialize(char* buf, int& size);
   int deserialize(const char* buf, int size);

public:
   static const int m_iContentSize;	// Size of hand shake data

public:
   int32_t m_iVersion;          // UDT version
   int32_t m_iType;             // UDT socket type
   int32_t m_iISN;              // random initial sequence number
   int32_t m_iMSS;              // maximum segment size
   int32_t m_iFlightFlagSize;   // flow control window size
   int32_t m_iReqType;          // connection request type: 1: regular connection request, 0: rendezvous connection request, -1/-2: response
   int32_t m_iID;		// socket ID
   int32_t m_iCookie;		// cookie
   uint32_t m_piPeerIP[4];	// The IP address that the peer's UDP port is bound to
};


#endif
