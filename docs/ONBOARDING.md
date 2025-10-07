# Optimized VR Streaming - Onboarding Guide

## 📋 Table of Contents
1. [Project Overview](#project-overview)
2. [Repository Structure](#repository-structure)
3. [UDT Protocol Architecture](#udt-protocol-architecture)
4. [Key Files for Protocol Modification](#key-files-for-protocol-modification)
5. [Build System](#build-system)
6. [Application Examples](#application-examples)
7. [Docker Deployment](#docker-deployment)
8. [How to Modify the Protocol](#how-to-modify-the-protocol)

---

## 🎯 Project Overview

This is a CS 538 research project at UIUC focused on **optimizing VR streaming** using the **UDT (UDP-based Data Transfer) protocol**.

**What is UDT?**
- A reliable UDP-based transport protocol designed for high bandwidth-delay product networks
- Features configurable congestion control (CCC framework)
- Supports both reliable streaming and partial reliable messaging
- Originally designed for bulk data transfer, being adapted here for low-latency VR streaming

**Project Goal:** Modify UDT's congestion control and packet handling to optimize for VR streaming requirements:
- Ultra-low latency (minimize motion-to-photon delay)
- Frame-aware delivery (prioritize recent frames)
- Graceful degradation under network congestion
- Adaptive bitrate based on network conditions

---

## 📁 Repository Structure

```
optimized_vr_streaming/
├── udt4/                      # UDT protocol implementation (main focus)
│   ├── src/                   # Core protocol source files ⭐
│   ├── app/                   # Example applications
│   ├── doc/                   # HTML documentation
│   ├── win/                   # Windows Visual Studio projects
│   ├── draft-gg-udt-xx.txt   # IETF protocol specification
│   ├── Makefile               # Build system
│   └── README.txt             # UDT library documentation
│
├── images/                    # Docker containers for deployment
│   ├── backend/               # VR backend server container
│   ├── cdn/                   # CDN node container
│   └── client/                # VR client container
│
├── scripts/                   # Setup automation
│   └── cloudlab_setup.sh     # CloudLab VM setup script
│
├── topology/                  # Network topology configuration
│   └── lab.conf              # Kathara lab configuration
│
└── docs/                      # Project documentation
```

---

## 🏗️ UDT Protocol Architecture

### Layer Overview

```
┌─────────────────────────────────────┐
│      Application Layer (app/)       │  ← Your VR streaming app
├─────────────────────────────────────┤
│        UDT API (api.cpp)            │  ← Socket interface
├─────────────────────────────────────┤
│    Core Protocol (core.cpp)         │  ← Main UDT logic
│  ┌──────────────────────────────┐   │
│  │ Congestion Control (ccc.cpp) │   │  ← ⭐ Key modification point
│  └──────────────────────────────┘   │
├─────────────────────────────────────┤
│  Buffer Management (buffer.cpp)     │
│  Loss Tracking (list.cpp)           │
│  Packet Queue (queue.cpp)           │
├─────────────────────────────────────┤
│    UDP Channel (channel.cpp)        │  ← Raw UDP I/O
└─────────────────────────────────────┘
```

### Packet Flow

**Sending Path:**
1. App calls `UDT::send()` → **api.cpp**
2. Data stored in → **buffer.cpp** (CSndBuffer)
3. Core schedules packets → **core.cpp** (packData)
4. Congestion control determines rate → **ccc.cpp** (CCC)
5. Packets queued → **queue.cpp** (CSndQueue)
6. Sent via UDP → **channel.cpp** (CChannel)

**Receiving Path:**
1. UDP packet arrives → **channel.cpp**
2. Queued → **queue.cpp** (CRcvQueue)
3. Core processes → **core.cpp** (processData/processCtrl)
4. Stored in buffer → **buffer.cpp** (CRcvBuffer)
5. Loss detection → **list.cpp** (CRcvLossList)
6. Delivered to app → **api.cpp** (`UDT::recv()`)

### Control Loop

The UDT protocol uses **timer-based events** (all in `core.cpp`):

```cpp
// ACK Timer (triggered every ~10ms by default)
if (current_time >= m_ullNextACKTime) {
    sendCtrl(ACK);  // Send acknowledgement
    updateACKInterval();
}

// NAK Timer (triggered when loss detected)
if (current_time >= m_ullNextNAKTime) {
    sendCtrl(NAK);  // Request retransmission
    updateNAKInterval();
}

// SYN Timer (keepalive, ~10s)
if (current_time >= m_ullNextSYNTime) {
    sendCtrl(KEEPALIVE);
}
```

---

## ⭐ Key Files for Protocol Modification

### 1. **`udt4/src/ccc.cpp` & `udt4/src/ccc.h`** - Congestion Control
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/ccc.cpp`

**What it does:**
- Defines the **CCC (Custom Congestion Control) framework**
- Implements **CUDTCC** - the native AIMD-based algorithm
- Provides virtual callbacks for congestion events

**Key callbacks to override:**
```cpp
virtual void onACK(int32_t ack);              // ACK received
virtual void onLoss(const int32_t* losslist, int size);  // Loss detected
virtual void onTimeout();                      // Timeout occurred
virtual void onPktSent(const CPacket* pkt);   // Packet sent
```

**Key parameters to control:**
```cpp
double m_dPktSndPeriod;    // Inter-packet interval (microseconds)
double m_dCWndSize;        // Congestion window size (packets)
```

**🎯 For VR:** This is your **PRIMARY modification point**. Create a custom CCC subclass that:
- Adjusts rate based on frame deadlines
- Implements latency-aware backoff
- Prioritizes recent frames over old frames
- Adapts quickly to bandwidth changes

**Example custom CCC:** See `udt4/app/cc.h` for CTCP (TCP-like) and CUDPBlast (fixed-rate) examples

---

### 2. **`udt4/src/core.cpp` & `udt4/src/core.h`** - Main Protocol Logic
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/core.cpp:1-3500`

**What it does:**
- Implements the **CUDT class** - the heart of UDT
- Manages connection state, timers, flow control
- Handles packet processing and loss detection

**Key methods:**
- `packData()` - Creates data packets from send buffer
- `processData()` - Processes received data packets
- `processCtrl()` - Processes control packets (ACK/NAK)
- `checkTimers()` - Timer event handling
- `CCUpdate()` - Updates congestion control state

**Key state variables:**
```cpp
// Sending
int32_t m_iSndCurrSeqNo;        // Current send sequence number
int m_iFlowWindowSize;          // Flow control window
double m_dCongestionWindow;     // Congestion window
uint64_t m_ullInterval;         // Packet send interval

// Receiving
int32_t m_iRcvCurrSeqNo;        // Current receive sequence
CRcvLossList* m_pRcvLossList;   // Lost packets

// Timers
uint64_t m_ullNextACKTime;      // Next ACK time
uint64_t m_ullNextNAKTime;      // Next NAK time
```

**🎯 For VR:** Modify here for:
- Timer intervals tuned to frame rate (60/90/120 FPS)
- Fast loss detection for low latency
- Selective retransmission based on frame importance

---

### 3. **`udt4/src/packet.cpp` & `udt4/src/packet.h`** - Packet Format
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/packet.h:1-200`

**What it does:**
- Defines packet structure (128-bit header + payload)
- Handles packet serialization/deserialization

**Current packet header:**
```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|F|        Sequence Number / Message Type                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|O|O|        Message Number / Additional Info                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                           Timestamp                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Destination Socket ID                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

F = 0: Data packet, F = 1: Control packet
O = Message boundary flags
```

**🎯 For VR:** Extend header to include:
- Frame type (I-frame, P-frame, B-frame)
- Frame timestamp/deadline
- Priority level
- FEC information
- Viewport/region information

---

### 4. **`udt4/src/buffer.cpp` & `udt4/src/buffer.h`** - Buffer Management
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/buffer.h:1-300`

**What it does:**
- **CSndBuffer**: Manages send buffer (stores unacknowledged data)
- **CRcvBuffer**: Manages receive buffer (reassembles packets)

**Key methods:**
```cpp
// Send buffer
void addBuffer(const char* data, int len);     // Add data
int readData(char** data, int32_t& msgno);     // Get new packet
int readData(char** data, int offset);         // Get retransmission
void ackData(int offset);                      // Remove ACKed data

// Receive buffer
int addData(CUnit* unit, int offset);          // Add received packet
int readBuffer(char* data, int len);           // Read to application
```

**🎯 For VR:** Modify for:
- Minimal buffering to reduce latency
- Frame-boundary aware buffer management
- Priority-based buffer space allocation
- Automatic dropping of outdated frames

---

### 5. **`udt4/src/list.cpp` & `udt4/src/list.h`** - Loss Tracking
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/list.h:1-200`

**What it does:**
- **CSndLossList**: Tracks packets to retransmit (sender side)
- **CRcvLossList**: Tracks missing packets (receiver side)

**Key methods:**
```cpp
// Sender loss list
int insert(int32_t seqno1, int32_t seqno2);  // Mark as lost
int32_t getLostSeq();                        // Get next to retransmit

// Receiver loss list
void insert(int32_t seqno1, int32_t seqno2); // Detected loss
void getLossArray(int32_t* array, int& len); // Generate NAK packet
```

**🎯 For VR:** Modify for:
- Selective retransmission based on frame deadlines
- FEC-aware loss handling
- Prioritize retransmission of critical packets (I-frames)

---

### 6. **`udt4/src/queue.cpp` & `udt4/src/queue.h`** - Packet Scheduling
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/queue.h:1-400`

**What it does:**
- **CSndQueue**: Background thread for sending packets
- **CRcvQueue**: Background thread for receiving packets
- **CSndUList**: Scheduling list (heap-based priority queue)

**Key scheduling:**
```cpp
// Packets scheduled by timestamp
void update(const CUDT* u, bool reschedule, bool erase = false);
```

**🎯 For VR:** Modify for:
- Priority-based scheduling (not just timestamp)
- Frame-type aware queue management
- Prevent head-of-line blocking for low-priority frames

---

### 7. **`udt4/src/window.cpp` & `udt4/src/window.h`** - Window Management
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/window.h:1-200`

**What it does:**
- **CACKWindow**: Stores ACK history for RTT calculation
- **CPktTimeWindow**: Tracks packet timing for bandwidth estimation

**Key methods:**
```cpp
int getPktRcvSpeed();      // Packet arrival rate (pps)
int getBandwidth();        // Estimated bandwidth (pps)
```

**🎯 For VR:** Modify for:
- Faster bandwidth estimation convergence
- Frame-rate aware measurements
- Integration with link quality indicators

---

### 8. **`udt4/src/api.cpp` & `udt4/src/api.h`** - Public API
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/api.h:1-500`

**What it does:**
- Implements the **UDT namespace** (public API)
- Socket management (CUDTUnited singleton)
- Socket options (UDT_MSS, UDT_CC, UDT_MAXBW, etc.)

**Key API functions:**
```cpp
UDTSOCKET socket(int af, int type, int protocol);
int bind(UDTSOCKET u, const sockaddr* name, int namelen);
int connect(UDTSOCKET u, const sockaddr* name, int namelen);
int send(UDTSOCKET u, const char* buf, int len, int flags);
int recv(UDTSOCKET u, char* buf, int len, int flags);
```

**Socket options:**
```cpp
UDT_MSS           // Maximum Segment Size
UDT_CC            // Congestion Control algorithm
UDT_FC            // Flow control window size
UDT_SNDBUF        // Send buffer size
UDT_RCVBUF        // Receive buffer size
UDT_MAXBW         // Maximum bandwidth limit
```

**🎯 For VR:** Add new socket options:
- `UDT_FRAME_DEADLINE` - Frame delivery deadline
- `UDT_FRAME_PRIORITY` - Frame priority levels
- `UDT_VR_MODE` - Enable VR-specific optimizations

---

### 9. **`udt4/src/channel.cpp` & `udt4/src/channel.h`** - UDP Layer
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/channel.h:1-150`

**What it does:**
- Wraps raw UDP socket operations
- Handles platform-specific socket APIs

**Key methods:**
```cpp
void open(const sockaddr* addr);              // Create UDP socket
int sendto(const sockaddr* addr, CPacket& pkt); // Send packet
int recvfrom(sockaddr* addr, CPacket& pkt);    // Receive packet
```

**🎯 For VR:** Usually no modifications needed, unless:
- Implementing DSCP/TOS for QoS
- Using kernel bypass (DPDK, eBPF)
- Multi-path support

---

### 10. **`udt4/src/common.cpp` & `udt4/src/common.h`** - Utilities
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/common.h:1-400`

**What it does:**
- **CTimer**: High-resolution timing (microsecond precision)
- **CSeqNo**: Sequence number arithmetic with wrapping
- **CMutex/CGuard**: Thread synchronization

**Key utilities:**
```cpp
// Timer
static uint64_t getTime();              // Current time (microseconds)
void sleep(uint64_t interval);          // Sleep for interval

// Sequence numbers (handle wrapping at 2^31)
static int seqcmp(int32_t seq1, int32_t seq2);    // Compare
static int seqlen(int32_t seq1, int32_t seq2);    // Distance
static int32_t incseq(int32_t seq, int32_t inc);  // Increment
```

---

### 11. **`udt4/src/cache.cpp` & `udt4/src/cache.h`** - Network Info Cache
**Location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/src/cache.h:1-100`

**What it does:**
- Caches network characteristics per destination
- Reuses learned parameters for new connections

**Cached info:**
```cpp
int m_iRTT;                 // Round-trip time
int m_iBandwidth;           // Estimated bandwidth
int m_iLossRate;            // Average loss rate
double m_dCWnd;             // Last congestion window
```

**🎯 For VR:** Extend to cache:
- Optimal frame rate for path
- VR-specific QoE metrics
- Historical jitter statistics

---

## 🔨 Build System

### Prerequisites
```bash
# Linux/Mac
gcc/g++ compiler
pthread library
make
```

### Building UDT Library

```bash
cd udt4/
make

# Outputs:
# - src/libudt.so (Linux) or src/libudt.dylib (Mac)
# - src/libudt.a (static library)
```

### Building Applications

```bash
cd udt4/app/
make

# Outputs:
# - appserver, appclient    (simple test client/server)
# - sendfile, recvfile      (file transfer)
# - test                     (test suite)
```

### Compiler Flags (udt4/src/Makefile)
```makefile
CCFLAGS = -fPIC -Wall -Wextra -D$(os) -finline-functions -O3 \
          -fno-strict-aliasing -fvisibility=hidden
```

### Clean Build
```bash
cd udt4/
make clean  # Remove all build artifacts
make        # Rebuild from scratch
```

---

## 🧪 Application Examples

### 1. Simple Client/Server (`appclient.cpp`, `appserver.cpp`)

**Purpose:** Basic throughput test

**Server:**
```bash
cd udt4/app/
./appserver 9000
```

**Client:**
```bash
cd udt4/app/
./appclient <server_ip> 9000
```

**What it does:**
- Client sends 1 million packets of 100KB each
- Displays real-time statistics:
  - Send rate (Mb/s)
  - RTT (milliseconds)
  - Congestion window size
  - Packet send period (microseconds)

**Code location:**
- Server: `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/app/appserver.cpp`
- Client: `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/app/appclient.cpp`

---

### 2. File Transfer (`sendfile.cpp`, `recvfile.cpp`)

**Purpose:** Bulk file transfer over UDT

**Server (sender):**
```bash
cd udt4/app/
./sendfile 9000
```

**Client (receiver):**
```bash
cd udt4/app/
./recvfile <server_ip> 9000 <remote_file> <local_file>
```

**Code location:**
- Sender: `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/app/sendfile.cpp`
- Receiver: `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/app/recvfile.cpp`

---

### 3. Test Suite (`test.cpp`)

**Purpose:** Comprehensive protocol testing

```bash
cd udt4/app/
./test
```

**Test cases:**
1. Simple data transfer (10,000 integers)
2. Parallel UDT (200) and TCP (10) connections
3. Rendezvous connections (50 concurrent)
4. Multi-threaded UDT (1000 connections across 40 threads)

**Code location:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/app/test.cpp`

---

### 4. Custom Congestion Control Examples (`cc.h`)

**File:** `/Users/sidpro/Desktop/WorkPlace/UIUC/Fall 2025/CS 538/optimized_vr_streaming/udt4/app/cc.h`

**Two examples provided:**

**a) CTCP - TCP-like congestion control**
```cpp
class CTCP: public CCC {
  // Slow start + congestion avoidance
  // Fast retransmit on duplicate ACKs
  // Multiplicative decrease on loss
};
```

**b) CUDPBlast - Fixed-rate sending**
```cpp
class CUDPBlast: public CCC {
  // No congestion control
  // Fixed sending rate (configurable)
  // Good for controlled environments
};
```

**How to use:**
```cpp
// In your application
CTCP* cc = new CTCP;
UDT::setsockopt(sock, 0, UDT_CC, new CCCVirtualFactory<CTCP>, sizeof(CCCVirtualFactory<CTCP>));
```

---

## 🐳 Docker Deployment

### Directory Structure
```
images/
├── backend/Dockerfile    # VR streaming backend server
├── cdn/Dockerfile        # CDN caching node
└── client/Dockerfile     # VR client receiver
```

**Current status:** Dockerfiles are minimal stubs (based on `kathara/base:latest`)

**To develop:**
1. Add UDT library build steps
2. Copy application binaries
3. Configure network parameters
4. Add startup scripts

### Example Dockerfile (to be implemented):
```dockerfile
FROM kathara/base:latest

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    g++ \
    libpthread-stubs0-dev

# Copy UDT source
COPY udt4/ /opt/udt4/

# Build UDT
WORKDIR /opt/udt4
RUN make

# Copy VR streaming application
COPY vr_server /opt/vr_server

CMD ["/opt/vr_server"]
```

---

## 🛠️ How to Modify the Protocol

### Step-by-Step Guide

#### **Option 1: Modify Existing Congestion Control (Quick Start)**

1. **Edit `udt4/src/ccc.cpp`:**
```cpp
void CUDTCC::onACK(int32_t ack) {
    // ORIGINAL: Increase rate based on bandwidth estimation
    // MODIFIED: VR-aware rate increase

    if (vr_frame_deadline_approaching) {
        // Boost rate temporarily
        m_dPktSndPeriod *= 0.9;  // 10% faster
    } else {
        // Normal AIMD increase
        m_dPktSndPeriod = (m_dPktSndPeriod * m_ullSYNInterval) /
                          (m_dPktSndPeriod * m_iRcvRate + m_ullSYNInterval);
    }
}

void CUDTCC::onLoss(const int32_t* losslist, int size) {
    // ORIGINAL: Multiplicative decrease
    // MODIFIED: Latency-aware decrease

    if (m_iRTT > VR_LATENCY_THRESHOLD) {
        // Aggressive decrease if latency too high
        m_dPktSndPeriod *= 1.5;  // 50% slower
    } else {
        // Normal decrease
        m_dPktSndPeriod *= 1.125;  // 12.5% slower
    }
}
```

2. **Rebuild:**
```bash
cd udt4/src/
make clean && make
cd ../app/
make clean && make
```

3. **Test:**
```bash
./appserver 9000
./appclient <ip> 9000
```

---

#### **Option 2: Create Custom Congestion Control (Recommended for VR)**

1. **Create `vr_cc.h` in `udt4/app/`:**

```cpp
#ifndef __UDT_VR_CC_H__
#define __UDT_VR_CC_H__

#include <ccc.h>

class CVRCC: public CCC {
public:
    CVRCC();

    virtual void init();
    virtual void onACK(int32_t ack);
    virtual void onLoss(const int32_t* losslist, int size);
    virtual void onTimeout();

private:
    // VR-specific parameters
    int m_iFrameRate;           // Target frame rate (60/90/120 FPS)
    int64_t m_llFrameDeadline;  // Frame deadline (microseconds)
    bool m_bLowLatencyMode;     // Latency-critical mode

    // Statistics
    int m_iAvgRTT;              // Smoothed RTT
    int m_iRTTVar;              // RTT variance (jitter)
    double m_dMaxRateForLatency; // Max rate that keeps latency low

    // Helper methods
    void adjustRateForLatency();
    void adjustRateForBandwidth();
    bool isFrameDeadlineApproaching();
};

// Implementation
CVRCC::CVRCC() {
    m_iFrameRate = 90;  // 90 FPS VR
    m_llFrameDeadline = 1000000 / m_iFrameRate;  // ~11ms per frame
    m_bLowLatencyMode = true;
}

void CVRCC::init() {
    // Initialize with conservative values
    m_dPktSndPeriod = 10000.0;  // Start at 100 pps
    m_dCWndSize = 16.0;         // Small initial window
}

void CVRCC::onACK(int32_t ack) {
    // Update RTT statistics
    m_iAvgRTT = (7 * m_iAvgRTT + m_iRTT) / 8;

    // VR optimization: prioritize latency over throughput
    if (m_iAvgRTT < m_llFrameDeadline / 2) {
        // RTT well below frame deadline - can increase rate
        adjustRateForBandwidth();
    } else {
        // RTT approaching deadline - maintain current rate
        adjustRateForLatency();
    }
}

void CVRCC::onLoss(const int32_t* losslist, int size) {
    // VR optimization: gradual decrease to minimize jitter
    if (m_bLowLatencyMode) {
        // Small decrease to keep latency stable
        m_dPktSndPeriod *= 1.1;  // 10% slower
        m_dCWndSize = std::max(4.0, m_dCWndSize - 1);
    } else {
        // Standard multiplicative decrease
        m_dPktSndPeriod *= 1.25;
        m_dCWndSize = std::max(4.0, m_dCWndSize * 0.8);
    }
}

void CVRCC::adjustRateForLatency() {
    // Keep rate stable if latency is acceptable
    setACKInterval(10000);  // 10ms ACK interval
    setRTO(m_iAvgRTT * 4);  // Aggressive timeout
}

void CVRCC::adjustRateForBandwidth() {
    // Increase rate based on available bandwidth
    if (m_iBandwidth > 0) {
        double target_period = 1000000.0 / m_iBandwidth;
        m_dPktSndPeriod = 0.9 * m_dPktSndPeriod + 0.1 * target_period;
    }
}

#endif
```

2. **Use in application:**

```cpp
#include "vr_cc.h"

int main() {
    UDT::startup();

    UDTSOCKET client = UDT::socket(AF_INET, SOCK_STREAM, 0);

    // Set custom VR congestion control
    CCCVirtualFactory<CVRCC>* vr_cc_factory = new CCCVirtualFactory<CVRCC>;
    UDT::setsockopt(client, 0, UDT_CC, vr_cc_factory, sizeof(CCCVirtualFactory<CVRCC>));

    // Other VR-specific options
    int mss = 1500;
    UDT::setsockopt(client, 0, UDT_MSS, &mss, sizeof(int));

    int fc = 32;  // Small flow window for low latency
    UDT::setsockopt(client, 0, UDT_FC, &fc, sizeof(int));

    // Connect and stream VR frames
    // ...
}
```

3. **Build and test:**
```bash
cd udt4/src/
make
cd ../app/
g++ -o vr_client vr_client.cpp -I../src -L../src -ludt -lpthread -lm
./vr_client <server_ip> 9000
```

---

#### **Option 3: Extend Packet Format for VR Metadata**

1. **Modify `udt4/src/packet.h`:**

```cpp
// Add VR-specific flags to message number field
class CPacket {
public:
    // Existing methods...

    // VR extensions
    int getFrameType();          // I/P/B frame
    void setFrameType(int type);

    int getFramePriority();      // 0-7 priority levels
    void setFramePriority(int pri);

    int64_t getFrameDeadline();  // Timestamp deadline
    void setFrameDeadline(int64_t deadline);
};

// Use unused bits in m_iMsgNo for VR metadata
inline int CPacket::getFrameType() {
    return (m_nHeader[1] >> 29) & 0x3;  // 2 bits for frame type
}

inline void CPacket::setFrameType(int type) {
    m_nHeader[1] = (m_nHeader[1] & 0x9FFFFFFF) | ((type & 0x3) << 29);
}

inline int CPacket::getFramePriority() {
    return (m_nHeader[1] >> 26) & 0x7;  // 3 bits for priority
}

inline void CPacket::setFramePriority(int pri) {
    m_nHeader[1] = (m_nHeader[1] & 0xE3FFFFFF) | ((pri & 0x7) << 26);
}
```

2. **Modify sender to set VR metadata (`core.cpp`):**

```cpp
int CUDT::packData(CPacket& packet, uint64_t& ts) {
    // Existing code to read from buffer...

    // VR: Set frame metadata
    if (vr_frame_is_iframe) {
        packet.setFrameType(VR_FRAME_I);
        packet.setFramePriority(7);  // Highest priority
    } else {
        packet.setFrameType(VR_FRAME_P);
        packet.setFramePriority(5);  // Medium priority
    }

    packet.setFrameDeadline(vr_frame_deadline);

    return packet.getLength();
}
```

3. **Modify receiver to use VR metadata (`core.cpp`):**

```cpp
int CUDT::processData(CUnit* unit) {
    CPacket& packet = unit->m_Packet;

    // VR: Check frame deadline
    int64_t now = CTimer::getTime();
    int64_t deadline = packet.getFrameDeadline();

    if (now > deadline) {
        // Frame missed deadline - drop it
        m_pRcvBuffer->dropMsg(packet.getMsgSeq());
        return -1;
    }

    // VR: Prioritize I-frames for retransmission
    if (packet.getFrameType() == VR_FRAME_I) {
        // Request immediate retransmission if lost
        sendCtrl(NAK);
    }

    // Normal processing...
}
```

---

### Testing Your Modifications

#### 1. **Unit Testing**
```bash
cd udt4/app/
./test  # Run comprehensive test suite
```

#### 2. **Performance Testing**
```bash
# Terminal 1 (server)
./appserver 9000

# Terminal 2 (client)
./appclient <server_ip> 9000
# Monitor: send rate, RTT, cwnd, packet send period
```

#### 3. **Network Simulation**
```bash
# Use tc (traffic control) to simulate VR network conditions
sudo tc qdisc add dev eth0 root netem delay 20ms rate 50mbit loss 0.1%

# Run tests
./appclient <server_ip> 9000

# Clean up
sudo tc qdisc del dev eth0 root
```

#### 4. **Packet Capture**
```bash
# Capture UDT traffic
sudo tcpdump -i any -w udt_capture.pcap udp port 9000

# Analyze in Wireshark
wireshark udt_capture.pcap
```

---

## 📚 Additional Resources

### Protocol Specification
- **IETF Draft:** `udt4/draft-gg-udt-xx.txt`
- **HTML Docs:** `udt4/doc/index.htm` (open in browser)

### Key Papers (search online)
- "UDT: UDP-based Data Transfer for High-Speed Wide Area Networks" (SC '04)
- "Experiences in Designing and Implementing High-Performance Transport Protocols" (SC '07)

### Build Variations

**Debug build:**
```bash
cd udt4/src/
make CCFLAGS="-g -DDEBUG"
```

**Architecture-specific:**
```bash
make arch=IA32    # 32-bit x86
make arch=IA64    # Itanium
make arch=POWERPC # PowerPC
make arch=ARM     # ARM
```

**OS-specific:**
```bash
make os=LINUX
make os=BSD
make os=OSX
```

---

## 🎯 Quick Start Checklist

- [ ] Clone repository
- [ ] Build UDT library: `cd udt4 && make`
- [ ] Build apps: `cd udt4/app && make`
- [ ] Run simple test: `./test`
- [ ] Run client/server: `./appserver 9000` and `./appclient localhost 9000`
- [ ] Read protocol spec: `udt4/draft-gg-udt-xx.txt`
- [ ] Study congestion control: `udt4/src/ccc.cpp` and `udt4/app/cc.h`
- [ ] Plan VR modifications (latency-aware CC, frame prioritization, etc.)
- [ ] Implement custom CCC for VR
- [ ] Test with network simulation
- [ ] Deploy in Docker containers

---

## 🚀 Next Steps for VR Optimization

### Phase 1: Analysis
1. Profile current UDT performance for VR traffic patterns
2. Identify bottlenecks (latency sources, jitter causes)
3. Benchmark against TCP/QUIC/WebRTC

### Phase 2: Design
1. Define VR-specific metrics (motion-to-photon latency, frame loss, jitter)
2. Design frame-aware congestion control algorithm
3. Design packet prioritization scheme

### Phase 3: Implementation
1. Implement VR congestion control (CVRCC)
2. Add frame metadata to packet headers
3. Implement deadline-aware retransmission
4. Add frame dropping logic for outdated frames

### Phase 4: Evaluation
1. Test with real VR traffic traces
2. Measure latency, throughput, frame loss
3. Compare with baseline UDT and other protocols
4. Iterate based on results

---

## 📞 Support

- **UDT Homepage:** http://udt.sourceforge.net/
- **GitHub Issues:** (create issues in your fork)
- **Course Staff:** (for CS 538 specific questions)

---

*Last Updated: October 2025*
*Generated for CS 538 - Optimized VR Streaming Project*
