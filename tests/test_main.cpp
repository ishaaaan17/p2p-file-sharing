// Plain-assert test suite (no external framework needed):
// packet framing, checksum, SHA-256 vectors, piece bounds, rarest-first,
// choke decisions, handshake metadata/manifest, piece-hash verification.
#include <cstring>
#include <iostream>
#include <cstdio>
#include <vector>
#include "packet.h"
#include "sha256.h"
#include "piece_manager.h"
#include "peer_session_manager.h"
#include "choke_manager.h"
#include "handshake_manager.h"

static int checks = 0;
static int failures = 0;
#define CHECK(cond) do { ++checks; if (!(cond)) { ++failures; \
    std::cerr << "FAIL line " << __LINE__ << ": " << #cond << "\n"; } } while (0)

using namespace P2P;

static void test_packet_defaults() {
    Packet p;
    CHECK(p.seq_num == 0);
    CHECK(p.data_len == 0);
    CHECK(p.checksum == 0);
    bool payload_zero = true;
    for (size_t i = 0; i < MAX_PAYLOAD_SIZE; ++i) if (p.payload[i] != 0) payload_zero = false;
    CHECK(payload_zero);
}

static void test_packet_roundtrip_and_checksum() {
    Packet p;
    p.type = PacketType::DATA;
    p.seq_num = 42;
    const char* msg = "hello pieces";
    p.data_len = static_cast<uint16_t>(std::strlen(msg));
    std::memcpy(p.payload, msg, p.data_len);
    p.checksum = p.calculate_checksum();

    std::vector<uint8_t> wire = p.serialize();
    Packet back = Packet::deserialize(wire.data(), wire.size());
    CHECK(back.type == PacketType::DATA);
    CHECK(back.seq_num == 42);
    CHECK(back.data_len == p.data_len);
    CHECK(std::memcmp(back.payload, msg, p.data_len) == 0);
    CHECK(back.checksum == back.calculate_checksum());

    // Corrupt one payload byte on the wire: checksum must no longer validate.
    wire[HEADER_SIZE] ^= 0xFF;
    Packet corrupt = Packet::deserialize(wire.data(), wire.size());
    CHECK(corrupt.checksum != corrupt.calculate_checksum());

    // Malformed (truncated) input must throw, not crash.
    bool threw = false;
    try { Packet::deserialize(wire.data(), 4); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

static void test_sha256_vectors() {
    CHECK(sha256_hex(reinterpret_cast<const uint8_t*>(""), 0) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex(reinterpret_cast<const uint8_t*>("abc"), 3) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* multi = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    CHECK(sha256_hex(reinterpret_cast<const uint8_t*>(multi), std::strlen(multi)) ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void test_piece_manager_bounds_and_tail() {
    const char* path = "p2p_test_asset.bin";
    PieceManager pm;
    CHECK(pm.initialize_file_layout(path, 6500, 1000));
    CHECK(pm.get_total_pieces() == 7);

    // Tail piece: 6500 = 6*1000 + 500.
    std::vector<uint8_t> tail(500, 0xAB);
    CHECK(pm.write_piece_to_disk(6, tail.data(), 500));
    std::vector<uint8_t> out(1000, 0);
    uint32_t out_size = 0;
    CHECK(pm.read_piece_from_disk(6, out.data(), out_size));
    CHECK(out_size == 500);
    CHECK(std::memcmp(out.data(), tail.data(), 500) == 0);

    // Out-of-range and oversized writes/reads are rejected.
    CHECK(!pm.write_piece_to_disk(7, tail.data(), 100));
    CHECK(!pm.read_piece_from_disk(7, out.data(), out_size));
    std::vector<uint8_t> big(1001, 0x01);
    CHECK(!pm.write_piece_to_disk(0, big.data(), 1001));
    std::remove(path);
}

static void test_rarest_first() {
    const char* path = "p2p_test_rarest.bin";
    PieceManager pm;
    CHECK(pm.initialize_file_layout(path, 6000, 1000)); // owns nothing

    std::vector<bool> remote = { true, true, true, false, false, false };
    PeerSessionManager session(9999, remote);
    std::vector<std::vector<bool>> swarm = {
        { true, true, true, false, false, false },
        { true, false, true, false, false, false }
    };
    // Frequencies: piece0=2, piece1=1, piece2=2 -> rarest we lack & peer has = 1
    CHECK(session.select_rarest_piece_to_request(pm, swarm) == 1);
    std::remove(path);
}

static void test_choke_decisions() {
    ChokeManager cm;
    // New peers start unchoked.
    CHECK(cm.get_choke_status(5000) == false);
    CHECK(cm.should_choke_peer(5000) == false); // cannot reciprocate by default

    // Peer that CAN reciprocate: grace for the first piece, then choked
    // when it has taken a lot and given nothing.
    cm.set_can_reciprocate(5001, true);
    cm.record_upload(5001, 1000);
    CHECK(cm.should_choke_peer(5001) == false); // grace
    cm.record_upload(5001, 3000);
    CHECK(cm.should_choke_peer(5001) == true);  // took 4000, gave 0

    // Peer that gives back at least half of what it takes stays unchoked.
    cm.set_can_reciprocate(5002, true);
    cm.record_upload(5002, 4000);
    cm.record_download(5002, 3000);
    CHECK(cm.should_choke_peer(5002) == false);

    // Peer that can never reciprocate (e.g. leecher vs complete seeder)
    // is never choked, however much it takes.
    cm.set_can_reciprocate(5003, false);
    cm.record_upload(5003, 100000);
    CHECK(cm.should_choke_peer(5003) == false);
}

static void test_handshake_metadata_and_manifest() {
    FileMetadata meta;
    meta.file_name = "movie.bin";
    meta.file_size = 12345;
    meta.piece_size = 1000;
    meta.total_pieces = 13;

    HandshakeManager local(1001);
    local.set_file_metadata(meta);
    Packet syn = local.prepare_handshake_packet();

    HandshakeManager remote(2002);
    uint32_t remote_id = 0;
    FileMetadata parsed;
    CHECK(remote.process_incoming_handshake(syn, remote_id, &parsed));
    CHECK(remote_id == 1001);
    CHECK(parsed.file_name == "movie.bin");
    CHECK(parsed.file_size == 12345);
    CHECK(parsed.piece_size == 1000);
    CHECK(parsed.total_pieces == 13);

    // SYN_ACK: metadata + bitfield + hash manifest round-trip.
    std::vector<bool> bf(13, false);
    bf[0] = true; bf[2] = true;
    std::vector<std::array<uint8_t, 32>> hashes(13);
    for (auto& h : hashes) h.fill(0);
    const char* piece0 = "piece0data";
    hashes[0] = sha256(reinterpret_cast<const uint8_t*>(piece0), std::strlen(piece0));

    Packet syn_ack = local.prepare_syn_ack_packet(bf, hashes);
    std::vector<bool> out_bf;
    FileMetadata out_meta;
    std::vector<std::array<uint8_t, 32>> out_hashes;
    CHECK(remote.process_incoming_syn_ack(syn_ack, out_bf, out_meta, out_hashes));
    CHECK(out_bf.size() == 13 && out_bf[0] && !out_bf[1] && out_bf[2]);
    CHECK(out_meta.total_pieces == 13);
    CHECK(out_hashes.size() == 13);
    CHECK(out_hashes[0] == hashes[0]);

    // Piece-hash verification against that manifest.
    PeerSessionManager session(1001, out_bf);
    session.set_piece_hashes(out_hashes);
    Packet data;
    data.type = PacketType::DATA;
    data.seq_num = 0;
    data.data_len = static_cast<uint16_t>(std::strlen(piece0));
    std::memcpy(data.payload, piece0, data.data_len);
    CHECK(session.verify_piece_hash(data));
    data.payload[0] ^= 0xFF; // tamper
    CHECK(!session.verify_piece_hash(data));
    data.seq_num = 5; // zero-hash entry = unknown -> accepted (checksum-only)
    CHECK(session.verify_piece_hash(data));
}

int main() {
    test_packet_defaults();
    test_packet_roundtrip_and_checksum();
    test_sha256_vectors();
    test_piece_manager_bounds_and_tail();
    test_rarest_first();
    test_choke_decisions();
    test_handshake_metadata_and_manifest();

    std::cout << "p2p_tests: " << (checks - failures) << "/" << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
