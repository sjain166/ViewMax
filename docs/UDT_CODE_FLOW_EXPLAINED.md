# UDT Code Flow Detailed Explanation

This document explains the core UDT implementation focusing on three critical files:
1. `packet.h/cpp` - Packet structure and format
2. `ccc.cpp` - Congestion control
3. `core.cpp` - Packet send/receive flow

---

## 1. Packet Structure (`packet.h` & `packet.cpp`)

### 1.1 The CPacket Class

**Purpose:** Represents a single UDT packet (data or control)

#### Key Members

```cpp
class CPacket {
    uint32_t m_nHeader[4];        // 128-bit (16 bytes) header
    iovec m_PacketVector[2];      // [0] = header, [1] = payload

    // Aliases (references to header fields)
    int32_t& m_iSeqNo;            // = m_nHeader[0]
    int32_t& m_iMsgNo;            // = m_nHeader[1]
    int32_t& m_iTimeStamp;        // = m_nHeader[2]
    int32_t& m_iID;               // = m_nHeader[3] (socket ID)
    char*& m_pcData;              // = m_PacketVector[1].iov_base
};
```

**Why aliases?** Provides convenient named access to header fields without copying data.

---

### 1.2 Packet Header Format (128 bits = 16 bytes)

#### Data Packet Header

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|0|                    Sequence Number (31 bits)                |  ← m_nHeader[0]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|ff |o|                 Message Number (29 bits)                |  ← m_nHeader[1]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Time Stamp (32 bits)                   |  ← m_nHeader[2]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Destination Socket ID (32 bits)             |  ← m_nHeader[3]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Bit 0: 0 = Data Packet
Bit ff (30-31): Message boundary flags
    11 = Solo message (complete message in one packet)
    10 = First packet of multi-packet message
    01 = Last packet of message
    00 = Middle packet
Bit o (29): In-order delivery flag
```

#### Control Packet Header

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|1|     Type (15 bits)          |         Reserved              |  ← m_nHeader[0]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Additional Info (32 bits)                  |  ← m_nHeader[1]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Time Stamp (32 bits)                   |  ← m_nHeader[2]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Destination Socket ID (32 bits)             |  ← m_nHeader[3]
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

Bit 0: 1 = Control Packet
Type field values:
    0 = Handshake
    1 = Keep-alive
    2 = ACK
    3 = NAK (Loss Report)
    4 = Congestion Warning
    5 = Shutdown
    6 = ACK² (Acknowledgement of Acknowledgement)
    7 = Message Drop Request
    8 = Error Signal
```

---

### 1.3 Key Packet Methods

#### Constructor - Sets up aliases
```cpp
CPacket::CPacket():
m_iSeqNo((int32_t&)(m_nHeader[0])),      // Alias to header[0]
m_iMsgNo((int32_t&)(m_nHeader[1])),      // Alias to header[1]
m_iTimeStamp((int32_t&)(m_nHeader[2])),  // Alias to header[2]
m_iID((int32_t&)(m_nHeader[3]))          // Alias to header[3]
{
    m_PacketVector[0].iov_base = (char*)m_nHeader;    // Header
    m_PacketVector[0].iov_len = 16;                    // Always 16 bytes
    m_PacketVector[1].iov_base = NULL;                 // Payload (set later)
    m_PacketVector[1].iov_len = 0;
}
```

**Insight:** Using `iovec` allows zero-copy I/O with `sendmsg()`/`recvmsg()` - header and payload sent in one system call.

---

#### pack() - Create control packets
```cpp
void CPacket::pack(int pkttype, void* lparam, void* rparam, int size)
{
    // Set bit 0 = 1 (control), bits 1-15 = type
    m_nHeader[0] = 0x80000000 | (pkttype << 16);

    switch (pkttype) {
        case 2: // ACK
            m_nHeader[1] = *(int32_t*)lparam;  // ACK seq number
            m_PacketVector[1].iov_base = (char*)rparam;  // ACK data
            m_PacketVector[1].iov_len = size;
            break;

        case 3: // NAK
            // rparam = array of lost sequence numbers
            m_PacketVector[1].iov_base = (char*)rparam;
            m_PacketVector[1].iov_len = size;
            break;

        case 6: // ACK2
            m_nHeader[1] = *(int32_t*)lparam;  // ACK seq being acknowledged
            break;
    }
}
```

---

#### NAK Loss List Encoding (lines 127-143 in packet.cpp)

**Format:**
```
Single lost packet:
  0XXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX   (bit 31 = 0)

Range of lost packets:
  1XXXXXXX XXXXXXXX XXXXXXXX XXXXXXXX   (first seq, bit 31 = 1)
  0YYYYYYY YYYYYYYY YYYYYYYY YYYYYYYY   (last seq, bit 31 = 0)
```

**Example:**
```
Lost packets: 100, 102, 103, 104, 105, 200

Encoded as:
  [100]           // Single loss
  [102 | 0x80000000, 105]  // Range 102-105
  [200]           // Single loss
```

**Why?** Compresses consecutive losses from N entries to 2 entries (50-90% size reduction for burst losses).

---

### 1.4 Handshake Packet

**Purpose:** Connection establishment

```cpp
class CHandShake {
    int32_t m_iVersion;         // UDT version (4)
    int32_t m_iType;            // Socket type (STREAM/DGRAM)
    int32_t m_iISN;             // Initial Sequence Number (random)
    int32_t m_iMSS;             // Maximum Segment Size
    int32_t m_iFlightFlagSize;  // Flow control window
    int32_t m_iReqType;         // Request type (1=request, -1=response)
    int32_t m_iID;              // Socket ID
    int32_t m_iCookie;          // SYN cookie for security
    uint32_t m_piPeerIP[4];     // Peer IP address
};
```

**Size:** 48 bytes (9 × 32-bit + 4 × 32-bit = 13 × 4 = 52? Actually 48 per code)

---

## 2. Congestion Control (`ccc.cpp`)

### 2.1 CCC Base Class

**Purpose:** Abstract base class for pluggable congestion control algorithms

#### Key State Variables

```cpp
class CCC {
protected:
    // Control parameters (OUTPUT - algorithm sets these)
    double m_dPktSndPeriod;     // Inter-packet interval (microseconds)
                                // Rate = 1,000,000 / m_dPktSndPeriod packets/sec
    double m_dCWndSize;         // Congestion window (packets)

    // Network state (INPUT - core provides these)
    int m_iBandwidth;           // Estimated bandwidth (packets/sec)
    int m_iMSS;                 // Maximum segment size (bytes)
    int32_t m_iSndCurrSeqNo;    // Current send sequence number
    int m_iRcvRate;             // Receive rate (packets/sec)
    int m_iRTT;                 // Round-trip time (microseconds)
    double m_dMaxCWndSize;      // Maximum congestion window
};
```

#### Virtual Callbacks (Subclasses override these)

```cpp
virtual void init();                              // Initialize
virtual void onACK(int32_t ack);                  // ACK received
virtual void onLoss(const int32_t* losslist, int size); // Loss detected
virtual void onTimeout();                         // Timeout occurred
virtual void onPktSent(const CPacket* pkt);       // Packet sent
virtual void onPktReceived(const CPacket* pkt);   // Packet received
```

---

### 2.2 CUDTCC - Native UDT Congestion Control

**Algorithm:** AIMD (Additive Increase Multiplicative Decrease) with rate-based pacing

#### Key Variables

```cpp
class CUDTCC : public CCC {
    int m_iRCInterval;          // Rate control interval (10ms)
    uint64_t m_LastRCTime;      // Last rate control time

    bool m_bSlowStart;          // In slow start phase?
    int32_t m_iLastAck;         // Last acknowledged seq

    bool m_bLoss;               // Loss occurred this period?
    int32_t m_iLastDecSeq;      // Seq number of last decrease
    double m_dLastDecPeriod;    // Packet send period at last decrease

    int m_iNAKCount;            // NAK count this period
    int m_iAvgNAKNum;           // Average NAKs per period
    int m_iDecRandom;           // Random decrease threshold
};
```

---

#### init() - Initial state (lines 170-187)

```cpp
void CUDTCC::init()
{
    m_iRCInterval = 10000;      // 10ms rate control interval
    m_bSlowStart = true;
    m_dCWndSize = 16;           // Start with 16 packets
    m_dPktSndPeriod = 1;        // Initially very fast (regulated by slow start)
}
```

---

#### onACK() - Process acknowledgement (lines 189-249)

**Flow:**

1. **Rate limiting:** Only update every `m_iRCInterval` (10ms)
   ```cpp
   if (currtime - m_LastRCTime < m_iRCInterval)
       return;  // Too soon, skip
   ```

2. **Slow Start Phase:**
   ```cpp
   if (m_bSlowStart) {
       // Increase window by number of ACKed packets
       m_dCWndSize += CSeqNo::seqlen(m_iLastAck, ack);

       if (m_dCWndSize > m_dMaxCWndSize) {
           // Exit slow start
           m_bSlowStart = false;

           // Switch to rate-based control
           if (m_iRcvRate > 0)
               m_dPktSndPeriod = 1000000.0 / m_iRcvRate;
           else
               m_dPktSndPeriod = (m_iRTT + m_iRCInterval) / m_dCWndSize;
       }
   }
   ```

3. **Congestion Avoidance (after slow start):**
   ```cpp
   // Set window based on bandwidth-delay product
   m_dCWndSize = m_iRcvRate / 1000000.0 * (m_iRTT + m_iRCInterval) + 16;
   ```

4. **Rate Increase (AIMD - Additive Increase):**
   ```cpp
   // Calculate spare bandwidth
   B = m_iBandwidth - 1000000.0 / m_dPktSndPeriod;

   // Increase rate proportional to spare bandwidth
   inc = pow(10.0, ceil(log10(B * m_iMSS * 8.0))) * 0.0000015 / m_iMSS;

   // Apply increase
   m_dPktSndPeriod = (m_dPktSndPeriod * m_iRCInterval) /
                     (m_dPktSndPeriod * inc + m_iRCInterval);
   ```

**Formula explanation:**
- `B` = spare bandwidth (packets/sec)
- `inc` = increase step (adaptive based on `B`)
- Smaller period = faster sending

---

#### onLoss() - Handle packet loss (lines 251-294)

**Flow:**

1. **Exit slow start immediately:**
   ```cpp
   if (m_bSlowStart) {
       m_bSlowStart = false;
       m_dPktSndPeriod = 1000000.0 / m_iRcvRate;  // Match receive rate
       return;
   }
   ```

2. **AIMD - Multiplicative Decrease:**
   ```cpp
   // Check if this is a new loss event
   if (CSeqNo::seqcmp(losslist[0] & 0x7FFFFFFF, m_iLastDecSeq) > 0) {
       // New loss - decrease rate by 12.5% (multiply period by 1.125)
       m_dLastDecPeriod = m_dPktSndPeriod;
       m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);  // ← KEY: reduce rate

       m_iLastDecSeq = m_iSndCurrSeqNo;

       // Update NAK statistics
       m_iAvgNAKNum = (int)ceil(m_iAvgNAKNum * 0.875 + m_iNAKCount * 0.125);
       m_iNAKCount = 1;
   }
   else if ((m_iDecCount++ < 5) && (0 == (++m_iNAKCount % m_iDecRandom))) {
       // Additional decrease within same congestion period (max 5 times)
       // Note: 0.875^5 ≈ 0.51, so rate won't drop below 50% in one period
       m_dPktSndPeriod = ceil(m_dPktSndPeriod * 1.125);
   }
   ```

**Why 1.125?**
- TCP uses 0.5 (50% decrease) → aggressive
- UDT uses 1.125 (12.5% increase in period = ~11% rate decrease) → gentler
- Better for high-BDP networks where aggressive backoff causes underutilization

---

#### onTimeout() - Handle timeout (lines 296-314)

```cpp
void CUDTCC::onTimeout()
{
    if (m_bSlowStart) {
        m_bSlowStart = false;
        m_dPktSndPeriod = 1000000.0 / m_iRcvRate;
    }
    // Note: No rate decrease on timeout (commented out in code)
    // UDT relies more on NAKs than timeouts
}
```

---

### 2.3 Congestion Control Summary

| Phase | Window Behavior | Rate Behavior | Trigger |
|-------|----------------|---------------|---------|
| **Slow Start** | Exponential increase (doubles per RTT) | N/A (window-limited) | Start, until cwnd > max |
| **Congestion Avoidance** | BDP-based (`rate × RTT + 16`) | Additive increase | After slow start |
| **Loss Response** | N/A | Multiplicative decrease (÷ 1.125) | NAK received |

**Key Insight:** UDT uses **hybrid rate + window control**:
- **Window** (`m_dCWndSize`): Limits outstanding packets
- **Rate** (`m_dPktSndPeriod`): Paces packet transmission

This differs from TCP which is purely window-based.

---

## 3. Core Packet Flow (`core.cpp`)

### 3.1 Sending Path

#### send() → packData() → UDP transmission

**Entry Point:** `CUDT::send()` (user calls `UDT::send()`)
1. App writes data to send buffer (`m_pSndBuffer->addBuffer()`)
2. Sending thread calls `packData()` to create packets
3. Packets queued in `CSndQueue`
4. Background thread sends via UDP

---

### 3.2 packData() - Create outgoing packet (lines 2263-2383)

**Purpose:** Pack data from send buffer into UDT packet

#### Flow Diagram

```
packData()
    ↓
┌───────────────────────────────────────┐
│ Check loss list for retransmissions  │
└───────────────────────────────────────┘
    │
    ↓ if loss list not empty
┌───────────────────────────────────────┐
│ RETRANSMISSION PATH                   │
│  1. Get lost seq from m_pSndLossList │
│  2. Read data from buffer (offset)    │
│  3. Set packet.m_iSeqNo = lost seq    │
│  4. Update retransmit stats           │
└───────────────────────────────────────┘
    │
    ↓ if loss list empty
┌───────────────────────────────────────┐
│ NEW DATA PATH                         │
│  1. Check congestion window           │
│  2. Check flow window                 │
│  3. Read new data from buffer         │
│  4. Increment m_iSndCurrSeqNo         │
│  5. Set packet.m_iSeqNo = new seq     │
└───────────────────────────────────────┘
    │
    ↓
┌───────────────────────────────────────┐
│ FINALIZE PACKET                       │
│  - Set timestamp                      │
│  - Set dest socket ID                 │
│  - Call m_pCC->onPktSent()            │
│  - Calculate next send time (ts)      │
└───────────────────────────────────────┘
```

#### Code Walkthrough

**Step 1: Priority - Retransmissions first**
```cpp
int CUDT::packData(CPacket& packet, uint64_t& ts)
{
    // Check if any packets need retransmission
    if ((packet.m_iSeqNo = m_pSndLossList->getLostSeq()) >= 0)
    {
        // RETRANSMISSION PATH
        int offset = CSeqNo::seqoff(m_iSndLastDataAck, packet.m_iSeqNo);

        // Read old data from buffer at offset
        payload = m_pSndBuffer->readData(&(packet.m_pcData), offset,
                                         packet.m_iMsgNo, msglen);

        if (payload == -1) {
            // Data expired/dropped - send drop request
            sendCtrl(7, &packet.m_iMsgNo, seqpair, 8);
            return 0;
        }

        ++m_iRetransTotal;  // Stats
    }
```

**Why retransmit first?** Minimizes delay for lost packets → improves tail latency.

**Step 2: New data (if no retransmissions)**
```cpp
    else {
        // NEW DATA PATH

        // Check window limits
        int cwnd = min(m_iFlowWindowSize, (int)m_dCongestionWindow);

        if (cwnd >= CSeqNo::seqlen(m_iSndLastAck, m_iSndCurrSeqNo + 1)) {
            // Window has space - read new data
            if (0 != (payload = m_pSndBuffer->readData(&(packet.m_pcData),
                                                         packet.m_iMsgNo))) {
                // Got data
                m_iSndCurrSeqNo = CSeqNo::incseq(m_iSndCurrSeqNo);
                packet.m_iSeqNo = m_iSndCurrSeqNo;

                // Every 16th packet is a probe (for bandwidth estimation)
                if (0 == (packet.m_iSeqNo & 0xF))
                    probe = true;
            }
            else {
                // No data - return 0
                return 0;
            }
        }
        else {
            // Window full - cannot send
            return 0;
        }
    }
```

**Window check:** `outstanding = seqlen(m_iSndLastAck, m_iSndCurrSeqNo + 1)`
- If `outstanding >= cwnd`, window is full → stop sending
- If `outstanding < cwnd`, can send → proceed

**Step 3: Finalize packet**
```cpp
    // Set timestamp (relative to connection start)
    packet.m_iTimeStamp = int(CTimer::getTime() - m_StartTime);

    // Set destination socket ID
    packet.m_iID = m_PeerID;

    // Set payload length
    packet.setLength(payload);

    // Notify congestion control
    m_pCC->onPktSent(&packet);

    // Update stats
    ++m_llSentTotal;

    // Calculate next send time
    if (probe) {
        // Probe pairs sent back-to-back
        ts = entertime;
    }
    else {
        // Normal pacing: next_time = now + interval
        ts = entertime + m_ullInterval;
    }

    return payload;
}
```

**`m_ullInterval`** is set by congestion control (`m_dPktSndPeriod`).

---

### 3.3 Receiving Path

#### UDP → processData() → recv()

**Entry Point:** Background thread receives UDP packet
1. `CRcvQueue` receives packet from UDP
2. Identifies socket by `packet.m_iID`
3. Calls `processData()` for data packets or `processCtrl()` for control packets
4. Data stored in receive buffer
5. App calls `UDT::recv()` to read from buffer

---

### 3.4 processData() - Handle incoming data packet (lines 2385-2448)

**Purpose:** Process received data packet, detect loss, update state

#### Flow Diagram

```
processData(unit)
    ↓
┌───────────────────────────────────────┐
│ Reset timeout (just heard from peer) │
│ Notify CC: m_pCC->onPktReceived()    │
└───────────────────────────────────────┘
    ↓
┌───────────────────────────────────────┐
│ Add packet to receive buffer          │
│  - Check if in valid range            │
│  - m_pRcvBuffer->addData(unit, offset)│
└───────────────────────────────────────┘
    ↓
┌───────────────────────────────────────┐
│ LOSS DETECTION                        │
│  if (seq > m_iRcvCurrSeqNo + 1):     │
│    → Gap detected!                    │
│    → Insert to loss list              │
│    → Send NAK immediately             │
└───────────────────────────────────────┘
    ↓
┌───────────────────────────────────────┐
│ Update m_iRcvCurrSeqNo                │
│  OR remove from loss list (retrans)   │
└───────────────────────────────────────┘
```

#### Code Walkthrough

**Step 1: Housekeeping**
```cpp
int CUDT::processData(CUnit* unit)
{
    CPacket& packet = unit->m_Packet;

    // Reset expiration (peer is alive)
    m_iEXPCount = 1;
    m_ullLastRspTime = CTimer::getTime();

    // Notify congestion control
    m_pCC->onPktReceived(&packet);

    // Update bandwidth estimation
    m_pRcvTimeWindow->onPktArrival();

    // Probe packet detection (for bandwidth measurement)
    if (0 == (packet.m_iSeqNo & 0xF))
        m_pRcvTimeWindow->probe1Arrival();
    else if (1 == (packet.m_iSeqNo & 0xF))
        m_pRcvTimeWindow->probe2Arrival();
```

**Step 2: Add to receive buffer**
```cpp
    // Calculate offset in buffer
    int32_t offset = CSeqNo::seqoff(m_iRcvLastAck, packet.m_iSeqNo);

    // Check if packet is in valid range
    if ((offset < 0) || (offset >= m_pRcvBuffer->getAvailBufSize()))
        return -1;  // Out of range - discard

    // Add to receive buffer
    if (m_pRcvBuffer->addData(unit, offset) < 0)
        return -1;  // Buffer full or duplicate
```

**Step 3: Loss Detection** (🔥 **KEY LOGIC** 🔥)
```cpp
    // Check if seq > expected
    if (CSeqNo::seqcmp(packet.m_iSeqNo, CSeqNo::incseq(m_iRcvCurrSeqNo)) > 0)
    {
        // GAP DETECTED! Packets are missing.

        // Insert missing range to loss list
        m_pRcvLossList->insert(CSeqNo::incseq(m_iRcvCurrSeqNo),   // First lost
                               CSeqNo::decseq(packet.m_iSeqNo));   // Last lost

        // Encode loss list for NAK
        int32_t lossdata[2];
        lossdata[0] = CSeqNo::incseq(m_iRcvCurrSeqNo) | 0x80000000;  // Mark as range
        lossdata[1] = CSeqNo::decseq(packet.m_iSeqNo);

        // SEND NAK IMMEDIATELY (type 3 = NAK)
        sendCtrl(3, NULL, lossdata, (loss_is_single_packet ? 1 : 2));

        // Update loss stats
        int loss = CSeqNo::seqlen(m_iRcvCurrSeqNo, packet.m_iSeqNo) - 2;
        m_iRcvLossTotal += loss;
    }
```

**Example:**
```
Received seq: 100, 101, 102, 105
              ↑              ↑
              m_iRcvCurrSeqNo = 102
              New packet seq = 105

Expected: 103
Gap: 103, 104 are missing

→ Insert [103, 104] to loss list
→ Send NAK with [103 | 0x80000000, 104]
```

**Step 4: Update state**
```cpp
    // Update current largest received sequence
    if (CSeqNo::seqcmp(packet.m_iSeqNo, m_iRcvCurrSeqNo) > 0) {
        // New packet - advance current seq
        m_iRcvCurrSeqNo = packet.m_iSeqNo;
    }
    else {
        // Retransmitted packet - remove from loss list
        m_pRcvLossList->remove(packet.m_iSeqNo);
    }

    return 0;
}
```

---

### 3.5 processCtrl() - Handle control packets (lines 1954-2200)

**Purpose:** Process ACK, NAK, ACK2, and other control messages

#### Control Packet Types

| Type | Name | Purpose |
|------|------|---------|
| 0 | Handshake | Connection establishment |
| 1 | Keep-alive | Maintain connection |
| 2 | ACK | Acknowledge received data |
| 3 | NAK | Report lost packets |
| 4 | Congestion Warning | Notify congestion |
| 5 | Shutdown | Close connection |
| 6 | ACK2 | Acknowledge ACK (for RTT) |
| 7 | Message Drop | Drop expired message |
| 8 | Error | Report error |

---

#### Case 2: ACK Processing (lines 1964-2082)

**Flow:**

```
ACK received
    ↓
┌─────────────────────────────────┐
│ Extract ACK seq number          │
└─────────────────────────────────┘
    ↓
┌─────────────────────────────────┐
│ Send ACK2 (acknowledge the ACK) │
│  → Used for RTT calculation     │
└─────────────────────────────────┘
    ↓
┌─────────────────────────────────┐
│ Update send window              │
│  - m_iSndLastAck = ack          │
│  - m_iFlowWindowSize from ACK   │
└─────────────────────────────────┘
    ↓
┌─────────────────────────────────┐
│ Remove ACKed data from buffer   │
│  - m_pSndBuffer->ackData()      │
│  - m_pSndLossList->remove()     │
└─────────────────────────────────┘
    ↓
┌─────────────────────────────────┐
│ Update RTT                      │
│  - m_iRTT = (7×old + new) / 8   │
│  - m_iRTTVar = variance         │
└─────────────────────────────────┘
    ↓
┌─────────────────────────────────┐
│ Update bandwidth estimate       │
│  - From ACK optional fields     │
└─────────────────────────────────┘
    ↓
┌─────────────────────────────────┐
│ Notify congestion control       │
│  - m_pCC->onACK(ack)            │
│  - m_pCC->setRTT()              │
│  - m_pCC->setBandwidth()        │
└─────────────────────────────────┘
```

**Code:**
```cpp
case 2: // ACK
{
    int32_t ack = *(int32_t*)ctrlpkt.m_pcData;

    // Send ACK2 (for RTT measurement)
    if (time_for_ack2) {
        sendCtrl(6, &ack);
        m_ullSndLastAck2Time = now;
    }

    // Validate ACK
    if (CSeqNo::seqcmp(ack, CSeqNo::incseq(m_iSndCurrSeqNo)) > 0) {
        m_bBroken = true;  // Invalid ACK - attack or bug
        break;
    }

    // Update flow window (from ACK payload)
    m_iFlowWindowSize = *((int32_t*)ctrlpkt.m_pcData + 3);
    m_iSndLastAck = ack;

    // Remove ACKed data from buffer
    int offset = CSeqNo::seqoff(m_iSndLastDataAck, ack);
    m_pSndBuffer->ackData(offset);
    m_pSndLossList->remove(CSeqNo::decseq(ack));
    m_iSndLastDataAck = ack;

    // Update RTT (EWMA: Exponentially Weighted Moving Average)
    int rtt = *((int32_t*)ctrlpkt.m_pcData + 1);
    m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;  // Variance
    m_iRTT = (m_iRTT * 7 + rtt) >> 3;                       // Mean

    m_pCC->setRTT(m_iRTT);

    // Update bandwidth estimate (if present in ACK)
    if (ctrlpkt.getLength() > 16) {
        m_iDeliveryRate = *((int32_t*)ctrlpkt.m_pcData + 4);
        m_iBandwidth = *((int32_t*)ctrlpkt.m_pcData + 5);

        m_pCC->setRcvRate(m_iDeliveryRate);
        m_pCC->setBandwidth(m_iBandwidth);
    }

    // Notify congestion control
    m_pCC->onACK(ack);
    CCUpdate();  // Update rate/window based on CC algorithm

    break;
}
```

**ACK Payload Format:**
```
Offset | Field
-------|------------------
0      | ACK seq number (what's being ACKed)
4      | RTT (microseconds)
8      | RTT Variance
12     | Available buffer size
16     | Packet receive rate (optional)
20     | Estimated bandwidth (optional)
```

---

#### Case 6: ACK2 Processing (lines 2085-2109)

**Purpose:** Calculate RTT from ACK-ACK2 round trip

```cpp
case 6: // ACK2
{
    int32_t ack;
    int rtt = m_pACKWindow->acknowledge(ctrlpkt.getAckSeqNo(), ack);

    if (rtt <= 0)
        break;  // Invalid or duplicate ACK2

    // Update RTT (EWMA)
    m_iRTTVar = (m_iRTTVar * 3 + abs(rtt - m_iRTT)) >> 2;
    m_iRTT = (m_iRTT * 7 + rtt) >> 3;

    m_pCC->setRTT(m_iRTT);

    // Update last ACKed ACK
    if (CSeqNo::seqcmp(ack, m_iRcvLastAckAck) > 0)
        m_iRcvLastAckAck = ack;

    break;
}
```

**RTT Calculation:**
```
Receiver:
  t1: Send ACK (seq=N)
  t3: Receive ACK2 (seq=N)

Sender:
  t2: Receive ACK (seq=N)
  t2: Send ACK2 (seq=N)

RTT = t3 - t1
```

The `CACKWindow` stores sent ACK timestamps to match with ACK2.

---

#### Case 3: NAK Processing (lines 2111-2170)

**Purpose:** Handle loss reports and schedule retransmissions

```cpp
case 3: // NAK (Loss Report)
{
    int32_t* losslist = (int32_t*)(ctrlpkt.m_pcData);

    // Notify congestion control FIRST
    m_pCC->onLoss(losslist, ctrlpkt.getLength() / 4);
    CCUpdate();

    // Decode loss list and insert into sender loss list
    for (int i = 0, n = ctrlpkt.getLength() / 4; i < n; ++i) {
        if (0 != (losslist[i] & 0x80000000)) {
            // Range: [losslist[i] & 0x7FFFFFFF, losslist[i+1]]

            // Security check
            if (seqcmp(losslist[i] & 0x7FFFFFFF, losslist[i+1]) > 0) {
                secure = false;  // Invalid range
                break;
            }

            // Insert range to loss list
            int num = m_pSndLossList->insert(losslist[i] & 0x7FFFFFFF,
                                              losslist[i+1]);
            m_iSndLossTotal += num;

            ++i;  // Skip next entry (end of range)
        }
        else {
            // Single packet loss
            m_pSndLossList->insert(losslist[i], losslist[i]);
            m_iSndLossTotal += 1;
        }
    }

    // Security: drop malformed NAKs
    if (!secure) {
        m_bBroken = true;
        m_iBrokenCounter = 0;
    }

    break;
}
```

**After NAK processing:**
- Loss list populated with packets to retransmit
- Next call to `packData()` will retransmit from loss list (highest priority)

---

### 3.6 sendCtrl() - Send control packets (lines 1737-1950)

**Purpose:** Create and send control packets (ACK, NAK, etc.)

#### Simplified Flow

```cpp
void CUDT::sendCtrl(int pkttype, void* lparam, void* rparam, int size)
{
    CPacket ctrlpkt;

    switch (pkttype) {
        case 2: // ACK
            // Determine ACK sequence number
            if (no_loss)
                ack = m_iRcvCurrSeqNo + 1;
            else
                ack = first_lost_seq;

            // Pack ACK data
            int32_t data[6];
            data[0] = ack;
            data[1] = m_iRTT;
            data[2] = m_iRTTVar;
            data[3] = m_pRcvBuffer->getAvailBufSize();
            data[4] = recv_rate;
            data[5] = bandwidth;

            ctrlpkt.pack(2, &m_iAckSeqNo, data, 24);
            break;

        case 3: // NAK
            ctrlpkt.pack(3, NULL, rparam, size);
            break;

        case 6: // ACK2
            ctrlpkt.pack(6, lparam);
            break;
    }

    ctrlpkt.m_iTimeStamp = timestamp;
    ctrlpkt.m_iID = m_PeerID;

    // Send via UDP
    m_pSndQueue->sendto(m_pPeerAddr, ctrlpkt);
}
```

---

## 4. Complete Data Flow Example

### Scenario: Client sends 100KB, one packet lost

```
CLIENT                          SERVER
======                          ======

1. UDT::send(buf, 100000)
    ↓
   [Store in m_pSndBuffer]
    ↓
   packData() called repeatedly:
    ↓
   Packet seq=100 (1400 bytes) ───────────→ processData()
                                             ├─ Add to buffer
                                             └─ m_iRcvCurrSeqNo = 100

   Packet seq=101 (1400 bytes) ───────────→ processData()
                                             └─ m_iRcvCurrSeqNo = 101

   Packet seq=102 (1400 bytes) ─────X       [LOST IN TRANSIT]

   Packet seq=103 (1400 bytes) ───────────→ processData()
                                             ├─ seq > expected! (103 > 102)
                                             ├─ Insert 102 to loss list
                                             └─ sendCtrl(NAK, [102])

   processCtrl(NAK [102]) ←───────────────  NAK packet
   ├─ m_pCC->onLoss([102])
   ├─ m_dPktSndPeriod *= 1.125 (slow down)
   └─ m_pSndLossList->insert(102)

   packData() [retransmit path]
   ├─ Get seq 102 from loss list
   └─ Read old data from buffer

   Packet seq=102 (RETRANS) ──────────────→ processData()
                                             ├─ seq <= m_iRcvCurrSeqNo
                                             └─ Remove 102 from loss list

   [Every ~10ms]:
   sendCtrl(ACK) ←──────────────────────── [ACK Timer fires]
   ├─ ACK seq = 104 (all received up to 103)
   └─ Includes RTT, buffer, bandwidth

   processCtrl(ACK seq=104)
   ├─ m_pSndBuffer->ackData(offset)
   ├─ Update RTT
   ├─ m_pCC->onACK(104)
   └─ sendCtrl(ACK2) ──────────────────────→ processCtrl(ACK2)
                                              └─ Update RTT

2. UDT::recv(buf, 100000) ←────────────── [All data received]
```

---

## 5. Key Insights for Your Project

### For Multi-Server Coordination

1. **Sequence Number Partitioning:**
   - Modify `packData()` to set `packet.m_iSeqNo` based on assigned range
   - Primary assigns ranges: Server 1 gets [0-249], Server 2 gets [250-499]

2. **NAK Routing:**
   - `processCtrl(NAK)` needs to forward NAKs to the correct server
   - Add server_id to packet header (use unused bits in `m_nHeader[1]`)
   - Primary maintains mapping: `seq → server_id`

3. **Congestion Control:**
   - Each secondary needs its own `CCC` instance
   - OR primary aggregates feedback and coordinates rate
   - Need to sync `m_dPktSndPeriod` across servers

### For VR Optimizations

1. **Frame Metadata (extend `CPacket`):**
   ```cpp
   int32_t getFrameID();     // Use bits in m_nHeader[1]
   int32_t getChunkID();     // Use bits in m_nHeader[1]
   int64_t getDeadline();    // Extend header or use message number
   ```

2. **Deadline-Aware Loss Handling (`processData`):**
   ```cpp
   if (CTimer::getTime() > packet.getDeadline()) {
       // Drop entire frame, don't send NAK
       m_pRcvBuffer->dropFrame(frame_id);
       return -1;
   }
   ```

3. **Priority Retransmission (`packData`):**
   ```cpp
   // Check if lost packet is I-frame
   if (is_iframe(lost_seq)) {
       // Retransmit immediately
   } else {
       // Delay retransmission or skip
   }
   ```

---

## 6. Summary Table

| File | Key Classes | Purpose | Lines to Modify for VR |
|------|-------------|---------|------------------------|
| `packet.h/cpp` | `CPacket`, `CHandShake` | Packet format, serialization | Add frame metadata fields (bits in `m_nHeader[1]`) |
| `ccc.cpp` | `CCC`, `CUDTCC` | Congestion control algorithms | Create `CVRCC` subclass with latency-aware control |
| `core.cpp` | `CUDT` | Main protocol logic, send/receive | `processData()`: deadline checking<br>`packData()`: priority retransmission<br>`processCtrl()`: NAK routing |

---

*This document provides a deep dive into UDT internals. Use it as a reference when implementing your multi-server VR streaming protocol.*
