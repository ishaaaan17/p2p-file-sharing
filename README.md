# P2P Core — A Decentralized File Sharing Engine in C++17

A from-scratch peer-to-peer file distribution engine built on raw UDP sockets, with its own wire protocol, its own reliability layer, and its own threading model. No third-party networking library, no central tracker, no central server.

---

## 1. Project Overview

Most file transfers people interact with day to day — a browser download, an app update, a video stream — are client-server. One machine holds the file, everyone else requests a copy from it, and that one machine pays the bandwidth bill for every single request. It works fine until the file is popular or the server goes down, at which point it stops working for everyone at once.

This project takes the opposite approach. Every node in the network — call it a peer — is simultaneously a **Seeder** (it can supply data it already has) and a **Leecher** (it can request data it's missing). There's no dedicated server node and no single point of failure. A file spreads through the swarm the same way a rumor spreads through a room: not from one source to everyone, but from whoever already knows it to whoever's standing nearby.

To make that work, a file can't be treated as one solid object. It's sliced into uniform, fixed-size **pieces** — 1000 bytes each in the current implementation — and a `PieceManager` subsystem tracks, piece by piece, which ones a given node already has and which ones it's still missing. That per-piece bookkeeping is what lets two peers with only partial copies of a file still trade the parts they lack, rather than one having to wait for the other to finish downloading first.

| | Client-Server | P2P Mesh (this project) |
|---|---|---|
| Data source | One server, always | Any peer holding the piece |
| Bandwidth load | Concentrated on the server | Spread across the swarm |
| Failure mode | Server down → nobody downloads | One peer down → others still serve |
| Unit of transfer | Whole file (or byte range) | Fixed-size piece |
| Scaling behavior | Gets worse as demand grows | Gets *better* as more peers join |

---

## 2. Technical Stack Deep-Dive

### Why raw UDP, and what has to be built on top of it

TCP already solves reliability — ordering, retransmission, congestion control — but it solves it in a way this project deliberately avoids, because the point of the exercise is to build that reliability layer by hand and understand exactly what it's doing. So the engine talks over raw UDP sockets instead, which hand you a datagram and offer no guarantee it arrived, arrived once, or arrived in order. Everything above that line is homegrown.

That homegrown layer is really two things stacked together:

1. **A framed wire protocol** (Section 3) that gives every packet a type, a sequence number, a length, and a checksum, so the receiving side can tell what a packet is for and whether it's intact.
2. **An application-layer reliability engine** built from that framing: checksums to catch corruption, sequence numbers to identify which piece a packet refers to, and an ACK/retry pattern (Section 5) so a request for a missing piece doesn't just vanish into the network if the response is silently dropped.

Neither of these exist at the OS socket level. UDP hands the application a chunk of bytes; it's on `P2P Core` to decide whether those bytes are trustworthy and what to do about it if they aren't.

### Why the engine is multi-threaded

A raw UDP read (`recvfrom()`) blocks until a packet shows up. If that call sat on the same thread driving the rest of the application, the entire program would freeze every time it was waiting on the network — which, in practice, is most of the time. `SocketManager` solves this by splitting the work across two threads (full breakdown in Section 4.3): one thread does nothing but block on `recvfrom()` and hand off finished packets, and a second thread polls a shared queue at a steady 10ms interval and processes whatever's arrived. Neither thread waits on the other's slow work.

---

## 3. The Wire Protocol — `Packet` Layout

Every message in this system, regardless of purpose, goes out on the wire in the same fixed frame. A receiver doesn't need to guess the format — it reads the same nine bytes of header off every incoming datagram and knows immediately how to interpret what follows.

```
+----------------+----------------+----------------+----------------+
|  Type (1 byte) |  Sequence (4 bytes)             | Length (2 B)   |
+----------------+----------------------------------+----------------+
| Checksum (2 B) |  Payload (variable, up to `Length` bytes)          |
+----------------+-----------------------------------------------------+
```

| Field | Type | Size | Purpose |
|---|---|---|---|
| Type | `uint8_t` | 1 byte | Identifies the message kind — see `PacketType` table below |
| Sequence | `uint32_t` | 4 bytes | The numeric index of the file piece this packet concerns |
| Length | `uint16_t` | 2 bytes | Exact byte size of the payload that follows |
| Checksum | `uint16_t` | 2 bytes | Integrity signature computed over the frame before it's sent |

### `PacketType` values

| Value | Name | Meaning |
|---|---|---|
| `1` | `SYN` | Opening greeting from a connecting peer |
| `2` | `SYN_ACK` | Handshake reply, carries the responder's compressed bitfield |
| `3` | `DATA` | Either a request for a piece (empty payload) or the piece itself (full payload) |
| `4` | `ACK` | Confirms a piece was received and validated |
| `5` | `FIN` | Signals a clean, intentional session close |

### Checksums and corruption detection

Before a `Packet` goes out, the sender runs a checksum calculation across the frame and stores the result in the checksum field. When the packet arrives, the receiver runs the identical calculation on the bytes it actually received and compares the two values. A match means the data is intact. A mismatch means something changed in transit — a dropped bit, a truncated payload, whatever the cause — and the frame is discarded on the spot rather than trusted. This is the first line of defense in the reliability engine: bad data never even reaches the piece-tracking logic upstream.

---

## 4. Handshake Layer & Session Setup — `HandshakeManager`

Two peers don't just start exchanging file data the moment a socket opens. They run a short handshake first, both to confirm the other side actually speaks this protocol and to exchange the information needed to figure out what to trade.

### 4.1 Step one — the `SYN`

The initiating peer sends a `SYN` packet whose payload contains a hardcoded magic string: `"P2P-CORE-PROT-V1"`. This is a cheap but effective filter. If the connection came from a port scanner, a misconfigured client, or anything else that isn't running this exact protocol, the string won't match, and the responder drops the connection immediately rather than wasting cycles on it.

### 4.2 Step two — the `SYN_ACK` and the bitfield

Once the magic string checks out, the responder answers with a `SYN_ACK` packet. Its payload isn't just an acknowledgment — it's the responder's entire file map: which pieces it has, encoded as a bitfield (see Section 4.4 below for exactly how). This is the moment where a connecting peer learns what its new neighbor can actually offer.

### 4.3 The threading behind packet delivery — `SocketManager`

Handshake packets, like every other packet, flow through the same two-thread pipeline:

- **Background Listener Thread** — sits in a loop on a blocking `recvfrom()` call against the raw UDP socket. The moment a datagram lands, it's parsed back into a structured `Packet` object and pushed onto a thread-safe queue. This thread does nothing else; it never touches application logic.
- **Main Application Thread** — polls that queue on a 10ms interval. Whenever a `Packet` is waiting, it's pulled off and handed to the appropriate state machine (handshake, piece exchange, etc.). Because this thread never blocks on the network directly, the rest of the application — UI, local logic, anything else running — keeps moving even while a `recvfrom()` call is stalled on the listener thread.

The two threads only ever communicate through that one queue, which keeps the concurrency model simple: no shared piece-tracking state gets touched from more than one thread at a time.

### 4.4 Bitwise Serialization Compression

A file map is, at its core, a list of booleans — one per piece, true if the peer has it, false if it doesn't. Sending that as a raw vector of booleans is wasteful; most language runtimes store a `bool` in a full byte even though it only carries one bit of real information.

Instead, `HandshakeManager` packs eight piece statuses into a single `uint8_t`, using bit shifts to place piece 0 at the highest bit and piece 7 at the lowest:

```cpp
// Packing: 8 piece-completion flags into one byte
uint8_t packed_byte = 0;
for (int i = 0; i < 8; ++i) {
    if (piece_status[i]) {
        packed_byte |= (1 << (7 - i)); // piece 0 -> bit 7, piece 7 -> bit 0
    }
}
```

```cpp
// Unpacking: reading the flags back out with a bitmask
for (int i = 0; i < 8; ++i) {
    bool has_piece = (packed_byte & (1 << (7 - i))) != 0;
}
```

The effect compounds fast. A 12-piece file, which would take 12 bytes as a naive boolean array, needs two bytes packed — one for pieces 0 through 7, a second (partially used) for pieces 8 through 11. The receiver runs the unpacking loop against each byte and rebuilds the full piece-status vector it needs for the exchange logic in Section 5.

---

## 5. The Data Exchange Pipeline — `PeerSessionManager`

Once two peers have shaken hands and swapped bitfields, `PeerSessionManager` takes over. Its job is a straightforward loop: compare what this node is missing against what the connected neighbor's decoded bitfield says they have, and go get whatever overlaps.

The exchange, step by step:

1. **Gap detection.** The local node scans its own piece-completion vector against the neighbor's decoded bitfield, looking for any index where the local node is missing a piece the neighbor holds.
2. **Request.** For each gap found, the node sends a `DATA` packet with an empty payload and the `Sequence` field set to the missing piece's index. The empty payload is what signals "this is a request," not a delivery — the same `PacketType` covers both directions.
3. **Lookup and read.** The remote peer receives the request, computes the byte offset directly from the index (`index * piece_size`), and reads that exact slice out of its local data buffer.
4. **Delivery.** That data gets packed into a new `DATA` packet — same type, now with the payload actually populated — and sent back to the requester.
5. **Validation.** The requesting node checks the checksum on the returned packet. If it's clean, the piece is marked complete in the local `PieceManager`, the local bitfield is updated to reflect it, and an `ACK` packet goes back to the sender as a receipt.

That five-step loop, repeated against every connected peer, is the entire mechanism by which a file assembles itself across the swarm — no single peer needs to hold the complete file for any other peer to eventually get one.

---

## 6. Build Log — Phases 1 Through 10

The project was built in the following order. Each phase is a working, testable increment on top of the last.

**Phase 1 — Problem definition and architecture.** Decided on a symmetrical mesh model over a client-server model, and settled on raw UDP as the transport, accepting that reliability would need to be built by hand in exchange for full control over the wire format.

**Phase 2 — `Packet` structure design.** Defined the fixed nine-byte header (`Type`, `Sequence`, `Length`, `Checksum`) described in Section 3, along with the five `PacketType` values the rest of the system builds on.

**Phase 3 — Checksum and integrity validation.** Implemented the checksum calculation run on every outbound frame, and the corresponding re-check and discard-on-mismatch logic on the receiving side.

**Phase 4 — Raw socket transport.** Wired up the actual UDP socket calls — binding, sending, and the blocking `recvfrom()` — and built the serialization/deserialization functions that convert between a `Packet` struct and the raw bytes that go over the wire.

**Phase 5 — `SocketManager` and the threading split.** Split networking into the Background Listener Thread and Main Application Thread described in Section 4.3, connected by a thread-safe frame queue, so the socket read no longer blocks the rest of the application.

**Phase 6 — `PieceManager` and file chunking.** Built the subsystem that slices a target file into uniform 1000-byte pieces and tracks, per piece, whether the local node has it yet.

**Phase 7 — `HandshakeManager` and the `SYN` / `SYN_ACK` sequence.** Implemented the magic-string handshake and the connection-drop behavior for anything that fails it, as described in Section 4.1 and 4.2.

**Phase 8 — Bitwise bitfield compression.** Added the packing and unpacking logic from Section 4.4, so file maps travel as packed bytes instead of raw boolean vectors.

**Phase 9 — `PeerSessionManager` and the exchange loop.** Built the gap-detection, request, lookup, delivery, and validation cycle from Section 5, which is what actually moves piece data between two connected peers.

**Phase 10 — End-to-end integration testing.** Ran two local instances of the engine against each other over loopback UDP, confirmed a full handshake, bitfield exchange, and complete piece-by-piece file reconstruction from one peer to the other.

---

## 7. Architectural Next Steps

Four critical upgrades are next in line to scale the core engine into an industrial-grade, swarm-resilient file distribution ecosystem:

### A. Multi-Peer Swarm Scaling Engine
We are upgrading the node architecture from a simple 1-on-1 pipeline to a concurrent, multi-connected mesh network. Instead of managing a single active connection, the core will track an array of multiple active peer sessions simultaneously. The network engine will route incoming data packets by their unique IP and port combinations, allowing a single leecher to pull different file pieces from 3, 4, or more neighbors at the exact same time.

### B. Swarm-Aware Rarest-First Piece Selection
Right now, our core engine can prioritize pieces based on local gaps, but its true power unlocks in a multi-peer swarm. By collecting the compressed file maps (bitfields) of all connected neighbors simultaneously, the selector will calculate the absolute rarest pieces across the entire active network. It will force the download queue to fetch those low-frequency pieces first, ensuring data doesn't get bottlenecked if a rare seeder suddenly goes offline.

### C. Game-Theoretic Choke & Unchoke (Tit-for-Tat)
To prevent "free-riders" (nodes that download data but refuse to upload any of their own), we are building a network throttling matrix. Every peer will actively track its upload-to-download ratio with its neighbors. If a neighbor stops sharing data back, our engine will send a `CHOKE` packet to temporarily halt their downloads, dynamically releasing the choke with an `UNCHOKE` flag only when cooperative balance is restored.

### D. Real Physical Asset Serialization
We are transitioning the engine from transferring simulated byte patterns to synchronizing actual files on your hard drive (like images, documents, or ZIP files). A small utility will read any physical asset, chop it into actual binary chunks, distribute it raw over the multi-threaded UDP pipeline, and use our disk persistence layer to perfectly reconstruct the original file on the receiving side.

### Symmetrical Session Teardown — the `FIN` State Machine

`FIN` already exists as a defined `PacketType`, but nothing currently sends or handles it — sessions just end when a socket goes idle. That's a problem: a peer that vanishes without notice leaves its neighbors holding an open connection that never gets cleaned up, tying up sockets and memory for a session that's already over. The teardown state machine gives `FIN` an actual job: when a peer intends to disconnect, it sends `FIN` to every connected neighbor first, those neighbors acknowledge and release their side of the session cleanly, and only then does the connection actually close. It's a small addition, but it's the difference between a swarm that degrades gracefully as peers come and go and one that slowly accumulates dead connections.

---

## 8. Requirements

- C++17-compatible compiler (tested with GCC and Clang)
- POSIX sockets (Linux/macOS) — Windows support would require swapping in Winsock
- No external dependencies beyond the standard library

## 9. License

MIT License — see [`LICENSE`](./LICENSE) for the full text. In short: use it, modify it, ship it, just keep the copyright notice attached.
