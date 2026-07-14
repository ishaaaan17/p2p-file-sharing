# P2P Swarm Core — Decentralized File Sharing Engine (C++17) + Live Telemetry Dashboard (Python/Streamlit)

## System Overview

This project is a peer-to-peer file distribution engine written in C++17, running directly over raw UDP sockets with no external networking libraries. Every node in the swarm is simultaneously a data source and a data consumer — there is no central server, no tracker, and no single point of failure. Beyond the base transport, the engine layers in the mechanics that make real swarms work: pieces are written straight to disk instead of held in memory, a rarest-first scheduler decides what to request next, and a Tit-for-Tat choke system decides who gets served.

Sitting alongside the C++ core is a Python dashboard built with Streamlit. It doesn't participate in the swarm — it has no piece data, no bitfield, no role in the protocol. It's a passive observer: the C++ binary fires small JSON status packets over a loopback UDP socket every time something swarm-relevant happens (a peer connects, a piece finishes downloading, someone gets choked), and the dashboard renders those events live in a browser tab.

---

## 🚀 Core Architectural Features

### Custom Framed Wire Protocol
Every message on the wire — regardless of purpose — travels inside the same `Packet` structure: a type flag, a sequence number identifying which piece it concerns, a length field, and a payload buffer. Two control types were added in this phase on top of the original five:

| Type | Role |
|---|---|
| `SYN` | Opening handshake greeting |
| `SYN_ACK` | Handshake reply, carries the responder's bitfield |
| `DATA` | Piece request (empty payload) or piece delivery (full payload) |
| `ACK` | Confirms a piece was received intact |
| `FIN` | Clean, intentional session teardown |
| `CHOKE` | Sent by a node refusing to serve another peer further data |
| `UNCHOKE` | Sent by a node resuming service to a previously choked peer |

`CHOKE` and `UNCHOKE` govern a new swarm state that sits on top of the base request/response cycle: a node can still be "connected" to a peer (post-handshake, bitfield exchanged) while being actively refused service at the piece level.

### Multi-Threaded Socket Pipeline (`SocketManager`)
Unchanged in principle from the base engine: a background thread blocks on `recvfrom()` so the rest of the application never stalls waiting on the network, and hands finished frames off through a thread-safe queue to the main thread, which polls it on a steady interval and drives all the state machines described below. Every telemetry packet sent to the Python dashboard also goes out through this same socket — there is no second, dedicated telemetry socket. The C++ binary simply directs an extra `send_data_to()` call at `127.0.0.1:6000` whenever a swarm event happens, using the exact same `SocketManager` instance it uses for peer traffic.

### High-Performance Disk Persistence (`PieceManager`)
Piece data no longer lives in memory. `initialize_file_layout()` pre-allocates the full output file on disk up front — it writes `file_size` zero bytes once, so every later write is a fixed-offset overwrite rather than an append. `write_piece_to_disk()` opens the file in `std::ios::binary | std::ios::in | std::ios::out` (deliberately *not* truncating), calls `seekp(piece_idx * piece_size)` to jump to the piece's exact byte offset, and writes the payload directly there. Reads work the same way in reverse with `seekg()`, and correctly shrink the read length for the final piece if the file size isn't an exact multiple of the piece size. A `std::mutex` guards every disk operation, since both the listener thread's incoming writes and the main thread's outgoing reads can touch the same file.

One consequence worth knowing: every `write_piece_to_disk()` call opens and closes the file fresh. That's simple and safe under a mutex, but it's not the fastest possible approach — a version that kept the file handle open for the life of the session would cut down on repeated open/close overhead under heavy piece traffic.

### Swarm Intelligence & Rarest-First Scheduler
`select_rarest_piece_to_request()` takes a snapshot of every connected peer's bitfield, tallies how many peers currently hold each piece index, and picks — among the pieces the *local* node is missing that *this specific remote peer* has — whichever one has the lowest count across the swarm. That's a correct implementation of rarest-first in spirit: it's actively trying to keep the least-available pieces from disappearing if a lone holder drops offline.

The scheduling is bounded by what this build's data actually is, though. `remote_bitfield` is set once, at the moment a `SYN_ACK` is processed, and never updated afterward — there's no `HAVE`-style message telling a node "I just finished piece 4," so the rarity picture used for scheduling is a snapshot from handshake time, not a live view of the swarm.

### Game-Theoretic Anti-Leech Protection (`ChokeManager`)
The design intent is textbook Tit-for-Tat: track bytes given and bytes received per peer, and stop serving anyone who takes without giving back. The implementation tracks two maps — `uploaded_bytes_map` and `downloaded_bytes_map` — updated through `record_upload()` and `record_download()`, with `should_choke_peer()` reading them to decide.

**Known issue:** the calls into this class from `main.cpp` are swapped relative to what the method names describe. When the local node *serves* a piece to a peer (an actual upload), the code calls `choke_mgr.record_download(...)`. When the local node *receives* a piece from a peer (an actual download), it calls `choke_mgr.record_upload(...)`. The practical effect: `should_choke_peer()` ends up choking a peer that has been sending you data generously before you've sent anything back — the opposite of the intended leech protection. The same swap exists in the plain `peer_uploaded_bytes` / `peer_downloaded_bytes` maps used for telemetry, so the numbers shown on the dashboard for "Downloaded" vs "Uploaded" per peer are similarly reversed from the local node's actual perspective. This doesn't crash anything or break the swarm mechanically — pieces still transfer correctly — but the choke decisions and the dashboard's byte counters do not currently mean what their labels say. Swapping which method is called at the two call sites in `main.cpp` (upload-path and download-path) resolves it.

There is also no optimistic unchoke in this build — real BitTorrent occasionally unchokes a random peer regardless of score, to give new or slow peers a chance to prove themselves. This implementation is strict reciprocity only, evaluated per incoming piece request rather than on a fixed timer, and it only ever sends `CHOKE` — nothing in this code path currently emits an `UNCHOKE` packet back out.

### Python Live Telemetry Dashboard
`dashboard.py` runs as its own process. On first load it spins up a background Python thread that binds a UDP socket to `127.0.0.1:6000` and sits in a loop parsing whatever JSON frames arrive, updating `st.session_state`. The Streamlit frontend redraws on a roughly one-second loop (`time.sleep(1)` followed by `st.rerun()`), showing a per-piece status grid (Missing / Downloading / Completed), a connected-peer table with byte counters and choke state, and two buttons that don't actually trigger anything in the C++ process — they're currently visual only, since there's no reverse channel from the browser back to the swarm.

Because every C++ node instance sends its telemetry to the same `127.0.0.1:6000` destination, a single running dashboard aggregates events from however many local nodes are active. Since the dashboard keys its peer table by the *remote* port each event refers to, running all three demo nodes against one dashboard gives a reasonable combined picture of swarm activity, though it doesn't distinguish which local node originated which event.

---

## 🛠️ System Architecture Diagram

```
                     ┌─────────────────────┐         ┌─────────────────────┐
                     │   Seeder Node A      │         │   Seeder Node B      │
                     │   (Peer ID 2002)     │         │   (Peer ID 2003)     │
                     │   UDP :9999          │         │   UDP :8888          │
                     │   Holds pieces 0-2    │         │   Holds pieces 3-5   │
                     └──────────┬───────────┘         └──────────┬───────────┘
                                │  SYN / SYN_ACK / DATA / ACK / CHOKE / FIN
                                │  (raw UDP datagrams)
                                └───────────────┬───────────────┘
                                                ▼
                              ┌───────────────────────────────────┐
                              │      Downloader / Leecher Node      │
                              │      (Peer ID 1001)  UDP :7777       │
                              │                                       │
                              │  ┌─────────────────────────────────┐ │
                              │  │  Listener Thread                  │ │
                              │  │  blocking recvfrom() loop         │ │
                              │  │  → parses raw bytes into Packet   │ │
                              │  │  → pushes onto thread-safe queue  │ │
                              │  └───────────────┬───────────────────┘ │
                              │                  │ (10ms poll)          │
                              │  ┌───────────────▼───────────────────┐ │
                              │  │  Main Thread                       │ │
                              │  │  HandshakeManager                  │ │
                              │  │  PeerSessionManager (rarest-first) │ │
                              │  │  ChokeManager                      │ │
                              │  │  PieceManager (disk seekp/seekg)   │ │
                              │  └───────────────┬───────────────────┘ │
                              └──────────────────┼──────────────────────┘
                                                 │
                                                 │  JSON telemetry frames
                                                 │  (UDP loopback, same socket)
                                                 ▼
                              ┌───────────────────────────────────┐
                              │  127.0.0.1 : 6000                    │
                              │  Python Background Listener Thread   │
                              │  → parses JSON → st.session_state    │
                              └───────────────┬───────────────────┘
                                              ▼
                              ┌───────────────────────────────────┐
                              │  Streamlit Browser UI                │
                              │  Piece grid · Peer table · Progress  │
                              └───────────────────────────────────┘
```

---

## 📦 Project Directory Structure

```
p2p-swarm-core/
├── include/
│   ├── packet.h                 # Packet struct + PacketType enum (SYN..UNCHOKE)
│   ├── socket_manager.h         # Two-thread UDP transport, thread-safe frame queue
│   ├── handshake_manager.h      # SYN / SYN_ACK exchange, bitfield compression
│   ├── piece_manager.h          # Disk-backed piece storage, seekp/seekg I/O
│   ├── peer_session_manager.h   # Rarest-first scheduling, request/response logic
│   └── choke_manager.h          # Tit-for-Tat upload/download tracking
│
├── src/
│   ├── socket_manager.cpp       # SocketManager implementation
│   ├── handshake_manager.cpp    # HandshakeManager implementation
│   ├── piece_manager.cpp        # Header-only class; translation unit only includes piece_manager.h
│   ├── choke_manager.cpp        # Header-only class; translation unit only includes choke_manager.h
│   └── main.cpp                 # Entry point: CLI setup, main event loop, telemetry dispatch
│
├── dashboard/
│   ├── dashboard.py             # Streamlit UI + background UDP telemetry listener
│   └── requirements.txt         # streamlit, pandas
│
├── LICENSE
└── README.md
```

---

## ⚡ Full Execution & Deployment Guide — 3-Node Cooperative Swarm Test

This walks through running two seeders and one leecher locally, with the dashboard watching all three.

### 1. Compile the C++ binary (Visual Studio)

1. Open the project folder in Visual Studio as a CMake or `.sln` project (whichever your workspace uses).
2. Set the solution configuration to **x64** and either **Debug** or **Release**.
3. Build the solution (`Ctrl+Shift+B`). Confirm `p2p_core.exe` (or your configured output name) lands in `x64/Debug/` or `x64/Release/`.
4. `main.cpp` reads from `source_asset.txt` if it exists in the working directory (falls back to synthetic placeholder data if the file is missing) — drop any 6000-byte-or-smaller test file named `source_asset.txt` next to the executable if you want to test with real content.

### 2. Set up the Python dashboard

```bash
cd dashboard
pip install streamlit pandas
streamlit run dashboard.py
```

This opens a browser tab (default `http://localhost:8501`). Leave it running for the rest of the test — it's the shared observer for all three nodes.

### 3. Launch the three swarm nodes

Open three separate terminals, each running the compiled binary.

**Terminal 1 — Seeder A**
```
Enter Local Peer ID: 2002
Enter Local Port to BIND: 9999
How many neighbors are you connecting to? 1
 -> Enter target neighbor port #1: 7777
```

**Terminal 2 — Seeder B**
```
Enter Local Peer ID: 2003
Enter Local Port to BIND: 8888
How many neighbors are you connecting to? 1
 -> Enter target neighbor port #1: 7777
```

**Terminal 3 — Leecher**
```
Enter Local Peer ID: 1001
Enter Local Port to BIND: 7777
How many neighbors are you connecting to? 2
 -> Enter target neighbor port #1: 9999
 -> Enter target neighbor port #2: 8888
```

Peer IDs `2002` and `2003` are hardcoded in `main.cpp` as the seeding condition — those two nodes preload pieces `0–2` and `3–5` respectively from `source_asset.txt` before the swarm starts. Any other Peer ID starts empty, which is why the leecher above uses `1001`.

### 4. Trigger the handshake

Each terminal is running a live keypress loop. With the **Leecher terminal focused**, press:

- **`h`** — broadcasts a `SYN` handshake to every neighbor port entered at startup. Do this on the leecher first so it discovers both seeders.
- **`t`** — sends a `FIN` teardown notice to every currently connected peer, cleanly closing those sessions.
- **`q`** — stops the node and exits.

### 5. What to expect

- Console output on the seeders will show incoming `SYN` packets and outgoing `SYN_ACK` replies.
- The leecher's console will show `DATA` requests going out, `ACK` confirmations coming back, and pieces filling in.
- In the browser: the six-piece grid under **Distributed Bitfield Progress Map** should move from `⬛ Empty` to `⏳ Fetching` to `🟩 Active` as pieces land, and the **Connected Active Peer Node Matrix** table should populate with both seeder ports once the handshake completes. Remember the byte-count columns in that table are currently mislabeled per the `ChokeManager` note above — the numbers are real, but "Downloaded" and "Uploaded" are swapped from the leecher's actual perspective.
- Once all six pieces show `Completed`, check the leecher's working directory for `peer_1001_storage.bin` — it should be byte-identical to `source_asset.txt` (or the synthetic fallback pattern if no source file was provided).

---

## License

MIT License — see [`LICENSE`](./LICENSE) for the full text.
