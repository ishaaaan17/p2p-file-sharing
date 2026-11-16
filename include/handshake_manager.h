#pragma once
#include <array>
#include <string>
#include <cstdint>
#include <vector>
#include "packet.h"

namespace P2P {

    enum class HandshakeState {
        UNINITIALIZED,
        HANDSHAKE_SENT,
        HANDSHAKE_VERIFIED,
        FAILED_PROT_MISMATCH,
        FAILED_TIMEOUT
    };

    // File description negotiated during the handshake. Before this existed,
    // file size (6000), piece size (1000) and piece count (6) were hardcoded
    // in main.cpp, so only that one fixed demo file could ever be shared.
    // The defaults remain the demo values, so a peer that sends no metadata
    // (older build) still interoperates exactly as before.
    struct FileMetadata {
        std::string file_name = "peer_storage.bin";
        uint32_t file_size = 6000;
        uint32_t piece_size = 1000;
        uint32_t total_pieces = 6;
    };

    class HandshakeManager {
    private:
        static const std::string PROTOCOL_IDENTIFIER;
        uint32_t m_local_peer_id;
        HandshakeState m_current_state;
        FileMetadata m_file_metadata;

    public:
        HandshakeManager(uint32_t local_id);
        ~HandshakeManager();

        // The local node's file description, sent in SYN and SYN_ACK.
        void set_file_metadata(const FileMetadata& metadata) { m_file_metadata = metadata; }
        FileMetadata get_file_metadata() const { return m_file_metadata; }

        Packet prepare_handshake_packet() const;
        // Parses a SYN. If the peer sent a metadata block it is returned via
        // out_metadata (when non-null); peers without one leave it untouched.
        bool process_incoming_handshake(const Packet& packet, uint32_t& out_remote_peer_id,
                                        FileMetadata* out_metadata = nullptr);

        // Creates a SYN_ACK frame: [metadata block][packed bitfield]
        // [optional SHA-256 manifest: total_pieces x 32 bytes, only for
        // pieces listed in piece_hashes; pass an empty vector to omit it].
        Packet prepare_syn_ack_packet(const std::vector<bool>& local_bitfield,
                                      const std::vector<std::array<uint8_t, 32>>& piece_hashes = {}) const;

        // Parses a SYN_ACK into the peer's bitfield, its file metadata and
        // its SHA-256 piece manifest (empty when the peer sent none).
        bool process_incoming_syn_ack(const Packet& packet, std::vector<bool>& out_remote_bitfield,
                                      FileMetadata& out_metadata,
                                      std::vector<std::array<uint8_t, 32>>& out_piece_hashes);

        HandshakeState get_state() const { return m_current_state; }
        void set_state(HandshakeState new_state) { m_current_state = new_state; }
        std::string get_state_string() const;
    };

} // namespace P2P
