# Project Roadmap: Multi-Server VR Streaming with UDT

## Project Summary

Building a **single-connection, multi-server** transport protocol for VR streaming where:
- Client opens **ONE** UDT session to a primary server
- **Multiple secondary servers** cooperatively send data chunks
- Client sees it as a single flow, but load is distributed across multiple CDN nodes

**Target:** ~20ms motion-to-photon latency for VR streaming

---

## Feasibility Assessment

### Good News
- UDT already has: reliability (ACK/NAK), sequencing, UDP transport, congestion control
- User-space implementation → easy to modify
- Team of 5 → can parallelize work

### Challenges
- UDT assumes **single sender** → need **coordinated multi-sender**
- No frame-awareness → need to add frame metadata
- No chunk coordination → need primary-secondary protocol
- Packet sequencing conflicts → multiple servers can't use same sequence numbers naively

---

## Incremental Development Roadmap

### Phase 1: Understand & Baseline (Week 1-2)

**Goals:**
- Get vanilla UDT working (appserver/appclient)
- Understand codebase deeply
- Set up Kathará test environment
- Establish baseline metrics

**Tasks:**
1. Build and test vanilla UDT with simple client/server
2. Measure baseline UDT performance (latency, throughput)
3. Set up Kathará network topology with 1 client + 3 servers
4. Understand UDT packet format and sequence number handling

**Key Files to Study:**
- `udt4/src/core.cpp` - packet send/receive flow
- `udt4/src/packet.h` - packet structure
- `udt4/src/ccc.cpp` - congestion control

**Commands:**
```bash
# Build UDT
cd udt4/
make

# Build applications
cd app/
make

# Run simple test
./appserver 9000                    # Terminal 1
./appclient <server_ip> 9000        # Terminal 2

# Capture packets
sudo tcpdump -i any -w udt_test.pcap udp port 9000
```

**Deliverable:** Working UDT client-server with packet captures showing ACK/NAK flow

---

### Phase 2: Add Frame Awareness (Week 3-4)

**Goal:** Make UDT understand "frames" instead of just byte streams

**Modification: Extend packet headers with frame metadata**

```cpp
// In udt4/src/packet.h - add frame metadata methods
class CPacket {
public:
    // Existing methods...

    // VR Frame extensions
    int32_t getFrameID();       // Which frame this chunk belongs to
    int32_t getChunkID();       // Which chunk within the frame
    int32_t getTotalChunks();   // Total chunks for this frame
    int64_t getFrameDeadline(); // When frame must be delivered

    void setFrameID(int32_t fid);
    void setChunkID(int32_t cid);
    void setTotalChunks(int32_t total);
    void setFrameDeadline(int64_t deadline);
};

// Use unused bits in packet header for frame metadata
// See ONBOARDING.md section on packet format for details
```

**Changes Needed:**

1. **Client Side:**
   - Send `FRAME_REQ` with frame_id
   - Add frame reassembly logic (collect all chunks for a frame)

2. **Server Side:**
   - Split frame into chunks
   - Tag each UDT packet with frame/chunk metadata

3. **Receive Buffer:**
   - Modify `udt4/src/buffer.cpp` to reassemble by frame, not just bytes
   - Track which chunks of each frame have arrived

**Implementation Steps:**
```cpp
// 1. Extend packet header (packet.h)
inline int32_t CPacket::getFrameID() {
    return (m_nHeader[1] >> 20) & 0xFFF;  // 12 bits for frame ID
}

// 2. Server: chunk a frame
void sendFrame(int frame_id, char* frame_data, int frame_size) {
    int chunk_size = 1400;  // MTU-safe
    int num_chunks = (frame_size + chunk_size - 1) / chunk_size;

    for (int i = 0; i < num_chunks; i++) {
        CPacket pkt;
        pkt.setFrameID(frame_id);
        pkt.setChunkID(i);
        pkt.setTotalChunks(num_chunks);
        // ... send via UDT
    }
}

// 3. Client: reassemble frame
class FrameAssembler {
    std::map<int, std::vector<char*>> frames;  // frame_id -> chunks

    void addChunk(CPacket& pkt) {
        int fid = pkt.getFrameID();
        int cid = pkt.getChunkID();
        frames[fid][cid] = pkt.getData();

        if (isFrameComplete(fid)) {
            reconstructAndRender(fid);
        }
    }
};
```

**Deliverable:** Single server sends frame-chunked data that client reassembles

---

### Phase 3: Primary-Secondary Coordination (Week 5-7)

**This is the HARD part** - coordinating multiple servers to send parts of the same UDT session.

#### Challenge: Sequence Number Conflicts
Multiple servers can't use the same sequence numbers simultaneously.

#### Solution A: Sequence Number Partitioning (Recommended)

**Approach:** Primary assigns **disjoint sequence number ranges** per server per frame.

```
Frame 1 (1000 chunks):
  Server 1: seq 0-249    (chunks 0-249)
  Server 2: seq 250-499  (chunks 250-499)
  Server 3: seq 500-749  (chunks 500-749)
  Server 4: seq 750-999  (chunks 750-999)
```

**Implementation:**

1. **Extend UDT handshake** to establish "server_id"
   ```cpp
   // In core.cpp - add server_id to connection metadata
   int m_iServerID;  // 0 = primary, 1-N = secondaries
   int32_t m_iSeqBase;  // Starting sequence number for this server
   ```

2. **Primary sends CHUNK_ASSIGNMENT** to secondaries
   ```cpp
   struct ChunkAssignment {
       int32_t frame_id;
       int32_t chunk_start;
       int32_t chunk_end;
       int32_t seq_start;  // Starting sequence number
       int32_t server_id;
   };
   ```

3. **Each secondary sends with assigned sequence range**
   ```cpp
   // Secondary adjusts sequence numbers
   void CUDT::packData(CPacket& pkt, uint64_t& ts) {
       // Normal UDT logic...
       int32_t seq = m_iSeqBase + local_chunk_offset;
       pkt.m_iSeqNo = seq;
   }
   ```

4. **Client receive buffer** handles out-of-order chunks (UDT already does this!)

#### Solution B: Separate Subflows (Simpler Alternative)

Each secondary creates a **separate UDT connection** to client.

**Tradeoff:**
- ✅ Easier to implement (less UDT modification)
- ❌ Client sees N connections instead of 1
- ❌ N separate congestion control contexts

**Implementation:**
```cpp
// Client opens N UDT sockets
for (int i = 0; i < num_servers; i++) {
    UDTSOCKET sock = UDT::socket(AF_INET, SOCK_STREAM, 0);
    UDT::connect(sock, server_addrs[i]);
    server_sockets.push_back(sock);
}

// Receive from all sockets, reassemble by frame_id
```

**Recommendation:** Start with **Solution B** to prove concept, migrate to **Solution A** if time permits.

---

### Phase 4: Shared Bus/Multicast Coordination (Week 6-8)

**Goal:** Primary distributes chunk assignments to secondaries efficiently.

#### Option 1: UDP Multicast

```bash
# Primary sends to multicast group
Primary → 224.0.0.1:5000: "CHUNK_ASSIGN frame=1 chunks=0-249 server=1"

# All secondaries listen on multicast group
Secondaries listen on 224.0.0.1:5000, filter by server_id
```

**Implementation:**
```cpp
// Primary multicasts assignments
int multicast_sock = socket(AF_INET, SOCK_DGRAM, 0);
struct sockaddr_in addr;
addr.sin_addr.s_addr = inet_addr("224.0.0.1");
addr.sin_port = htons(5000);

ChunkAssignment assignment = {frame_id, 0, 249, 1000, 1};
sendto(multicast_sock, &assignment, sizeof(assignment), 0,
       (struct sockaddr*)&addr, sizeof(addr));

// Secondary receives
int sock = socket(AF_INET, SOCK_DGRAM, 0);
bind(sock, multicast_addr);
struct ip_mreq mreq;
mreq.imr_multiaddr.s_addr = inet_addr("224.0.0.1");
setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

while (true) {
    ChunkAssignment assignment;
    recvfrom(sock, &assignment, sizeof(assignment), 0, NULL, NULL);
    if (assignment.server_id == my_server_id) {
        processAssignment(assignment);
    }
}
```

#### Option 2: Redis Pub/Sub (If servers share network)

```python
# Primary publishes
import redis
r = redis.Redis()
assignment = {'frame_id': 1, 'chunks': '0-249', 'server_id': 1}
r.publish('chunk_assignments', json.dumps(assignment))

# Secondary subscribes
pubsub = r.pubsub()
pubsub.subscribe('chunk_assignments')
for message in pubsub.listen():
    assignment = json.loads(message['data'])
    if assignment['server_id'] == my_id:
        process_assignment(assignment)
```

#### Option 3: Direct TCP Control Channel

```cpp
// Simplest: Primary has TCP connection to each secondary
for (auto& secondary : secondaries) {
    send(secondary.control_socket, &assignment, sizeof(assignment), 0);
}
```

**Recommendation:** Start with **Option 3** (TCP control channel), add multicast optimization later.

---

### Phase 5: Reliability & Retransmission (Week 8-9)

**Challenge:** Which server retransmits lost chunks?

#### Solution 1: Primary Handles All Retransmits (Simpler)

```cpp
// Client sends NAK → Primary
void CUDT::processNAK(CPacket& nak) {
    int32_t* losslist = nak.getLossList();

    // Primary retransmits all lost packets
    for (int i = 0; i < loss_count; i++) {
        int32_t seq = losslist[i];
        CPacket retrans_pkt = m_pSndBuffer->readData(seq);
        sendto(client_addr, retrans_pkt);
    }
}
```

**Pros:** Simple, no coordination needed
**Cons:** Primary becomes bottleneck

#### Solution 2: Secondaries Handle Their Own Retransmits (Optimal)

```cpp
// Primary maintains chunk → server mapping
std::map<int32_t, int> chunk_to_server;  // seq_no → server_id

// On NAK, primary forwards to responsible secondary
void CUDT::processNAK(CPacket& nak) {
    int32_t* losslist = nak.getLossList();

    for (int i = 0; i < loss_count; i++) {
        int32_t seq = losslist[i];
        int server_id = chunk_to_server[seq];

        // Forward NAK to that secondary
        forwardNAK(server_id, seq);
    }
}

// Secondary retransmits
void Secondary::handleRetransRequest(int32_t seq) {
    CPacket pkt = m_pSndBuffer->readData(seq);
    sendto(client_addr, pkt);
}
```

**Recommendation:** Start with **Solution 1**, optimize to **Solution 2** if performance requires.

---

### Phase 6: VR-Specific Optimizations (Week 10-12)

Now add VR-specific features:

#### 1. Frame Deadline Dropping
```cpp
// In processData (core.cpp)
int CUDT::processData(CUnit* unit) {
    CPacket& pkt = unit->m_Packet;

    int64_t now = CTimer::getTime();
    int64_t deadline = pkt.getFrameDeadline();

    if (now > deadline) {
        // Frame missed deadline - drop entire frame
        int frame_id = pkt.getFrameID();
        m_pRcvBuffer->dropFrame(frame_id);
        return -1;  // Discard packet
    }

    // Normal processing...
}
```

#### 2. Forward Error Correction (FEC)
```cpp
// Add parity chunks (e.g., Reed-Solomon)
void sendFrameWithFEC(int frame_id, char* data, int size) {
    int k = 100;  // Data chunks
    int n = 120;  // Total chunks (100 data + 20 parity)

    // Encode with FEC
    char** chunks = rs_encode(data, size, k, n);

    // Send all n chunks
    for (int i = 0; i < n; i++) {
        sendChunk(frame_id, i, chunks[i]);
    }

    // Client can reconstruct with any k out of n chunks
}
```

#### 3. Adaptive Chunk Sizing
```cpp
// Adjust chunk size based on RTT
void adaptChunkSize() {
    if (m_iRTT < 10000) {  // < 10ms
        m_iChunkSize = 1400;  // Larger chunks
    } else if (m_iRTT < 50000) {  // 10-50ms
        m_iChunkSize = 700;   // Medium chunks
    } else {
        m_iChunkSize = 350;   // Smaller chunks for high latency
    }
}
```

#### 4. Priority Scheduling
```cpp
// In queue.cpp - prioritize I-frames
void CSndUList::update(const CUDT* u, bool reschedule) {
    int priority = u->getFramePriority();

    if (priority == PRIORITY_IFRAME) {
        // Insert at front of queue
        insertFront(u);
    } else {
        // Normal timestamp-based scheduling
        insert(u);
    }
}
```

---

## Critical Design Decisions

### Decision 1: Single UDT Session vs Multiple Subflows?

| Approach | Pros | Cons | Complexity |
|----------|------|------|------------|
| **Single Session** | True "single flow" abstraction, matches proposal exactly | Requires custom sequence coordination | ⭐⭐⭐⭐⭐ |
| **Multiple Subflows** | Less UDT modification, easier to implement | Client sees N connections, N congestion contexts | ⭐⭐⭐ |

**Recommendation:** Start with **Multiple Subflows**, migrate to **Single Session** if time permits.

---

### Decision 2: Where Does Coordination Happen?

| Approach | Pros | Cons | Complexity |
|----------|------|------|------------|
| **Inside UDT** | Cleaner abstraction, tighter integration | Requires deep UDT modification | ⭐⭐⭐⭐⭐ |
| **Outside UDT** | Faster to implement, UDT stays vanilla | Extra coordination layer | ⭐⭐⭐ |

**Recommendation:** **Outside UDT** - build coordinator as separate service.

---

### Decision 3: Coordination Protocol?

| Approach | Use Case | Complexity |
|----------|----------|------------|
| **TCP Control Channels** | All scenarios, most reliable | ⭐⭐ |
| **UDP Multicast** | Servers on same LAN | ⭐⭐⭐ |
| **Redis Pub/Sub** | Servers share backend network | ⭐⭐ |

**Recommendation:** **TCP Control Channels** for simplicity and reliability.

---

## Suggested First Steps (Week 1)

### Step 1: Build Vanilla UDT
```bash
cd udt4/
make clean && make
cd app/
make

# Test it works
./appserver 9000
./appclient localhost 9000
```

### Step 2: Packet Capture Analysis
```bash
# Terminal 1: Start capture
sudo tcpdump -i lo -w udt_baseline.pcap udp port 9000

# Terminal 2: Run server
cd udt4/app/
./appserver 9000

# Terminal 3: Run client
./appclient localhost 9000

# Analyze in Wireshark
wireshark udt_baseline.pcap
```

**Look for:**
- UDT handshake packets
- Data packet sequence numbers
- ACK/NAK control packets
- Retransmission behavior

### Step 3: Set Up Kathará Topology

Create `topology/lab.conf`:
```bash
# Client node
client[0]="net0"

# Primary server
primary[0]="net0"
primary[1]="backend"

# Secondary servers
secondary1[0]="backend"
secondary2[0]="backend"
secondary3[0]="backend"
```

Create `topology/client.startup`:
```bash
ifconfig eth0 10.0.0.10 netmask 255.255.255.0 up
route add default gw 10.0.0.1
```

Create `topology/primary.startup`:
```bash
ifconfig eth0 10.0.0.1 netmask 255.255.255.0 up
ifconfig eth1 192.168.1.1 netmask 255.255.255.0 up
```

### Step 4: Modify appserver.cpp for Frame Chunks

```cpp
// In udt4/app/appserver.cpp
// Instead of sending random data, send frame-structured data

struct Frame {
    int32_t frame_id;
    int32_t chunk_id;
    int32_t total_chunks;
    char data[1400];
};

void sendFrames(UDTSOCKET serv) {
    for (int frame_id = 0; frame_id < 1000; frame_id++) {
        int chunks_per_frame = 100;

        for (int chunk_id = 0; chunk_id < chunks_per_frame; chunk_id++) {
            Frame frame;
            frame.frame_id = frame_id;
            frame.chunk_id = chunk_id;
            frame.total_chunks = chunks_per_frame;
            memset(frame.data, chunk_id, sizeof(frame.data));

            UDT::send(serv, (char*)&frame, sizeof(frame), 0);
        }

        // Print progress
        if (frame_id % 100 == 0) {
            cout << "Sent frame " << frame_id << endl;
        }
    }
}
```

Modify `appclient.cpp` to reassemble:
```cpp
struct Frame {
    int32_t frame_id;
    int32_t chunk_id;
    int32_t total_chunks;
    char data[1400];
};

map<int, set<int>> received_frames;  // frame_id -> set of chunk_ids

while (true) {
    Frame frame;
    int rs = UDT::recv(client, (char*)&frame, sizeof(frame), 0);

    received_frames[frame.frame_id].insert(frame.chunk_id);

    // Check if frame complete
    if (received_frames[frame.frame_id].size() == frame.total_chunks) {
        cout << "Frame " << frame.frame_id << " complete!" << endl;
    }
}
```

---

## Success Metrics

### Phase 1-2 Success Criteria
- ✅ Vanilla UDT achieves >100 Mbps on Kathará
- ✅ Client can reassemble frames from chunks
- ✅ Packet captures show expected UDT behavior

### Phase 3-4 Success Criteria
- ✅ 2+ servers send chunks for same frame
- ✅ Client receives chunks from multiple servers
- ✅ No duplicate sequence numbers

### Phase 5-6 Success Criteria
- ✅ Lost chunks are retransmitted correctly
- ✅ Frame deadline violations are detected
- ✅ Latency < 50ms for 90% of frames (stretch: <20ms)

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| Sequence number conflicts | Start with multiple subflows instead of single session |
| Coordination complexity | Use simple TCP control channels instead of multicast |
| UDT modification too deep | Build coordinator outside UDT initially |
| Kathará performance issues | Fall back to CloudLab VMs with manual network config |
| ROS Bag integration delays | Use synthetic frame data (random/video file) initially |

---

## Resources

### Code References
- **UDT Source:** `udt4/src/`
- **Example Apps:** `udt4/app/`
- **Documentation:** `udt4/doc/`, `docs/ONBOARDING.md`

### Key Papers
- UDT: UDP-based Data Transfer for High-Speed Networks (SC '04)
- Multi-Path TCP: Design, Implementation and Evaluation (IEEE '11)

### Tools
- **Network Emulation:** Kathará
- **Packet Analysis:** tcpdump, Wireshark
- **Testing:** CloudLab VMs

---

## Timeline Summary

| Week | Phase | Deliverable |
|------|-------|-------------|
| 1-2 | Baseline & Understanding | Working vanilla UDT, Kathará setup |
| 3-4 | Frame Awareness | Single-server frame chunking |
| 5-7 | Multi-Server Coordination | Multiple servers sending same frame |
| 8-9 | Reliability | Retransmission working correctly |
| 10-12 | VR Optimizations | Deadline dropping, FEC, priority |

---

*Last Updated: 2025-10-06*
