#include "handshake_manager.h"
#include <cstring>
#include <iostream>

namespace P2P {

    const std::string HandshakeManager::PROTOCOL_IDENTIFIER = "P2P-CORE-PROT-V1";

    HandshakeManager::HandshakeManager(uint32_t local_id)
        : m_local_peer_id(local_id), m_current_state(HandshakeState::UNINITIALIZED) {
    }

    HandshakeManager::~HandshakeManager() {}

    // --- FileMetadata wire encoding (big-endian) ---------------------------
    // Layout: file_size u32 | piece_size u32 | total_pieces u32 |
    //         name_length u16 | name bytes
    namespace {
        void append_u32(uint8_t* payload, uint16_t& offset, uint32_t value) {
            payload[offset++] = static_cast<uint8_t>((value >> 24) & 0xFF);
            payload[offset++] = static_cast<uint8_t>((value >> 16) & 0xFF);
            payload[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);
            payload[offset++] = static_cast<uint8_t>(value & 0xFF);
        }
        void append_u16(uint8_t* payload, uint16_t& offset, uint16_t value) {
            payload[offset++] = static_cast<uint8_t>((value >> 8) & 0xFF);
            payload[offset++] = static_cast<uint8_t>(value & 0xFF);
        }
        uint32_t read_u32(const uint8_t* payload, uint16_t& offset) {
            uint32_t value = (static_cast<uint32_t>(payload[offset]) << 24) |
                (static_cast<uint32_t>(payload[offset + 1]) << 16) |
                (static_cast<uint32_t>(payload[offset + 2]) << 8) |
                static_cast<uint32_t>(payload[offset + 3]);
            offset += 4;
            return value;
        }
        uint16_t read_u16(const uint8_t* payload, uint16_t& offset) {
            uint16_t value = static_cast<uint16_t>(
                (static_cast<uint16_t>(payload[offset]) << 8) | payload[offset + 1]);
            offset += 2;
            return value;
        }
        constexpr uint16_t METADATA_FIXED_SIZE = 4 + 4 + 4 + 2;

        bool append_metadata(uint8_t* payload, uint16_t& offset, const FileMetadata& meta, uint16_t payload_capacity) {
            uint16_t name_len = static_cast<uint16_t>(meta.file_name.size());
            if (offset + METADATA_FIXED_SIZE + name_len > payload_capacity) return false;
            append_u32(payload, offset, meta.file_size);
            append_u32(payload, offset, meta.piece_size);
            append_u32(payload, offset, meta.total_pieces);
            append_u16(payload, offset, name_len);
            if (name_len > 0) {
                std::memcpy(payload + offset, meta.file_name.c_str(), name_len);
                offset += name_len;
            }
            return true;
        }

        bool parse_metadata(const uint8_t* payload, uint16_t data_len, uint16_t& offset, FileMetadata& out_meta) {
            if (offset + METADATA_FIXED_SIZE > data_len) return false;
            FileMetadata meta;
            meta.file_size = read_u32(payload, offset);
            meta.piece_size = read_u32(payload, offset);
            meta.total_pieces = read_u32(payload, offset);
            uint16_t name_len = read_u16(payload, offset);
            if (offset + name_len > data_len) return false;
            meta.file_name.assign(reinterpret_cast<const char*>(payload + offset), name_len);
            offset += name_len;
            if (meta.file_size == 0 || meta.piece_size == 0 || meta.total_pieces == 0) return false;
            out_meta = meta;
            return true;
        }
    }

    Packet HandshakeManager::prepare_handshake_packet() const {
        Packet packet;
        packet.type = PacketType::SYN;
        packet.seq_num = 0;

        uint16_t current_offset = 0;
        std::memcpy(packet.payload + current_offset, PROTOCOL_IDENTIFIER.c_str(), PROTOCOL_IDENTIFIER.length());
        current_offset += static_cast<uint16_t>(PROTOCOL_IDENTIFIER.length());

        std::memcpy(packet.payload + current_offset, &m_local_peer_id, sizeof(m_local_peer_id));
        current_offset += sizeof(m_local_peer_id);

        // File metadata block: what we are sharing / want to receive.
        append_metadata(packet.payload, current_offset, m_file_metadata, MAX_PAYLOAD_SIZE);

        packet.data_len = current_offset;
        packet.checksum = packet.calculate_checksum();

        return packet;
    }

    bool HandshakeManager::process_incoming_handshake(const Packet& packet, uint32_t& out_remote_peer_id,
                                                      FileMetadata* out_metadata) {
        if (packet.type != PacketType::SYN) {
            std::cerr << "[Handshake Error] Mismatched initialization type flag." << std::endl;
            return false;
        }

        uint16_t expected_size = static_cast<uint16_t>(PROTOCOL_IDENTIFIER.length() + sizeof(uint32_t));
        if (packet.data_len < expected_size) {
            std::cerr << "[Handshake Error] Malformed frame dimensions." << std::endl;
            m_current_state = HandshakeState::FAILED_PROT_MISMATCH;
            return false;
        }

        char incoming_id[17] = { 0 };
        std::memcpy(incoming_id, packet.payload, PROTOCOL_IDENTIFIER.length());

        if (PROTOCOL_IDENTIFIER != incoming_id) {
            std::cerr << "[Handshake Refused] Magic ID mismatch! Got: \"" << incoming_id << "\"" << std::endl;
            m_current_state = HandshakeState::FAILED_PROT_MISMATCH;
            return false;
        }

        std::memcpy(&out_remote_peer_id, packet.payload + PROTOCOL_IDENTIFIER.length(), sizeof(uint32_t));

        // Optional metadata block (absent in older builds -> defaults apply).
        if (out_metadata != nullptr && packet.data_len > expected_size) {
            uint16_t offset = expected_size;
            FileMetadata remote_meta;
            if (parse_metadata(packet.payload, packet.data_len, offset, remote_meta)) {
                *out_metadata = remote_meta;
                std::cout << "[Handshake] Remote file metadata: \"" << remote_meta.file_name << "\" ("
                    << remote_meta.file_size << " bytes, " << remote_meta.total_pieces
                    << " pieces x " << remote_meta.piece_size << ")" << std::endl;
            }
        }

        m_current_state = HandshakeState::HANDSHAKE_VERIFIED;
        std::cout << "[Handshake Success] Authenticated connection loop! Remote Peer ID: "
            << out_remote_peer_id << std::endl;

        return true;
    }

    Packet HandshakeManager::prepare_syn_ack_packet(const std::vector<bool>& local_bitfield,
                                                    const std::vector<std::array<uint8_t, 32>>& piece_hashes) const {
        Packet packet;
        packet.type = PacketType::SYN_ACK; // Outbound SYN_ACK type flag
        packet.seq_num = 0;

        // Payload layout: [metadata block][packed bitfield][SHA-256 manifest]
        uint16_t offset = 0;
        FileMetadata meta = m_file_metadata;
        meta.total_pieces = static_cast<uint32_t>(local_bitfield.size());
        if (!append_metadata(packet.payload, offset, meta, MAX_PAYLOAD_SIZE)) {
            std::cerr << "[Handshake Error] Metadata exceeds max network payload capacity!" << std::endl;
            packet.data_len = 0;
            return packet;
        }

        // Bitfield Serialization Loop: Pack boolean indices into raw payload byte bits
        uint16_t byte_count = static_cast<uint16_t>((local_bitfield.size() + 7) / 8);
        if (offset + byte_count > MAX_PAYLOAD_SIZE) {
            std::cerr << "[Handshake Error] Bitfield size exceeds max network payload capacity!" << std::endl;
            packet.data_len = 0;
            return packet;
        }
        for (size_t i = 0; i < local_bitfield.size(); ++i) {
            if (local_bitfield[i]) {
                size_t byte_idx = offset + i / 8;
                size_t bit_idx = 7 - (i % 8); // Left-to-right bit packaging scheme
                packet.payload[byte_idx] |= (1 << bit_idx);
            }
        }
        offset += byte_count;

        // Optional SHA-256 manifest: one 32-byte hash per piece, in index order.
        if (!piece_hashes.empty()) {
            if (piece_hashes.size() != local_bitfield.size() ||
                offset + piece_hashes.size() * 32 > MAX_PAYLOAD_SIZE) {
                std::cerr << "[Handshake Error] Hash manifest exceeds max network payload capacity!" << std::endl;
                packet.data_len = 0;
                return packet;
            }
            for (const auto& hash : piece_hashes) {
                std::memcpy(packet.payload + offset, hash.data(), 32);
                offset += 32;
            }
        }

        packet.data_len = offset;
        packet.checksum = packet.calculate_checksum();
        return packet;
    }

    bool HandshakeManager::process_incoming_syn_ack(const Packet& packet, std::vector<bool>& out_remote_bitfield,
                                                    FileMetadata& out_metadata,
                                                    std::vector<std::array<uint8_t, 32>>& out_piece_hashes) {
        if (packet.type != PacketType::SYN_ACK) {
            std::cerr << "[Handshake Error] Expected SYN_ACK framework response." << std::endl;
            return false;
        }

        uint16_t offset = 0;
        FileMetadata remote_meta;
        if (!parse_metadata(packet.payload, packet.data_len, offset, remote_meta)) {
            std::cerr << "[Handshake Error] SYN_ACK metadata block malformed." << std::endl;
            return false;
        }
        out_metadata = remote_meta;

        uint32_t total_pieces = remote_meta.total_pieces;
        uint16_t expected_bytes = static_cast<uint16_t>((total_pieces + 7) / 8);
        if (offset + expected_bytes > packet.data_len) {
            std::cerr << "[Handshake Error] SYN_ACK bitfield payload size underflow mismatch." << std::endl;
            return false;
        }

        out_remote_bitfield.assign(total_pieces, false);

        // Bitfield Deserialization Loop: Extract boolean bits back out into a readable vector
        for (uint32_t i = 0; i < total_pieces; ++i) {
            size_t byte_idx = offset + i / 8;
            size_t bit_idx = 7 - (i % 8);

            if ((packet.payload[byte_idx] & (1 << bit_idx)) != 0) {
                out_remote_bitfield[i] = true;
            }
        }
        offset += expected_bytes;

        // Optional SHA-256 manifest trailing the bitfield.
        out_piece_hashes.clear();
        if (packet.data_len - offset >= static_cast<uint16_t>(total_pieces * 32) && total_pieces > 0) {
            out_piece_hashes.resize(total_pieces);
            for (uint32_t i = 0; i < total_pieces; ++i) {
                std::memcpy(out_piece_hashes[i].data(), packet.payload + offset + i * 32, 32);
            }
        }

        std::cout << "[Handshake Subsystem] SYN_ACK metadata + bitfield payload parsed successfully." << std::endl;
        return true;
    }

    std::string HandshakeManager::get_state_string() const {
        switch (m_current_state) {
        case HandshakeState::UNINITIALIZED:       return "UNINITIALIZED";
        case HandshakeState::HANDSHAKE_SENT:      return "HANDSHAKE_SENT";
        case HandshakeState::HANDSHAKE_VERIFIED:  return "HANDSHAKE_VERIFIED";
        case HandshakeState::FAILED_PROT_MISMATCH: return "FAILED_PROT_MISMATCH";
        case HandshakeState::FAILED_TIMEOUT:      return "FAILED_TIMEOUT";
        default:                                  return "UNKNOWN";
        }
    }

} // namespace P2P
