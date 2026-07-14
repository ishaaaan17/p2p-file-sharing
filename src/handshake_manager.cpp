#include "handshake_manager.h"
#include <cstring>
#include <iostream>

namespace P2P {

    const std::string HandshakeManager::PROTOCOL_IDENTIFIER = "P2P-CORE-PROT-V1";

    HandshakeManager::HandshakeManager(uint32_t local_id)
        : m_local_peer_id(local_id), m_current_state(HandshakeState::UNINITIALIZED) {
    }

    HandshakeManager::~HandshakeManager() {}

    Packet HandshakeManager::prepare_handshake_packet() const {
        Packet packet;
        packet.type = PacketType::SYN;
        packet.seq_num = 0;

        uint16_t current_offset = 0;
        std::memcpy(packet.payload + current_offset, PROTOCOL_IDENTIFIER.c_str(), PROTOCOL_IDENTIFIER.length());
        current_offset += static_cast<uint16_t>(PROTOCOL_IDENTIFIER.length());

        std::memcpy(packet.payload + current_offset, &m_local_peer_id, sizeof(m_local_peer_id));
        current_offset += sizeof(m_local_peer_id);

        packet.data_len = current_offset;
        packet.checksum = packet.calculate_checksum();

        return packet;
    }

    bool HandshakeManager::process_incoming_handshake(const Packet& packet, uint32_t& out_remote_peer_id) {
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

        m_current_state = HandshakeState::HANDSHAKE_VERIFIED;
        std::cout << "[Handshake Success] Authenticated connection loop! Remote Peer ID: "
            << out_remote_peer_id << std::endl;

        return true;
    }

    Packet HandshakeManager::prepare_syn_ack_packet(const std::vector<bool>& local_bitfield) const {
        Packet packet;
        packet.type = PacketType::SYN_ACK; // Outbound SYN_ACK type flag
        packet.seq_num = 0;

        // Calculate how many raw payload bytes we need to hold these bits
        // (total_bits + 7) / 8 handles rounding up perfectly
        uint16_t byte_count = static_cast<uint16_t>((local_bitfield.size() + 7) / 8);

        if (byte_count > MAX_PAYLOAD_SIZE) {
            std::cerr << "[Handshake Error] Bitfield size exceeds max network payload capacity!" << std::endl;
            packet.data_len = 0;
            return packet;
        }

        // Initialize payload memory block to 0
        std::memset(packet.payload, 0, MAX_PAYLOAD_SIZE);

        // Bitfield Serialization Loop: Pack boolean indices into raw payload byte bits
        for (size_t i = 0; i < local_bitfield.size(); ++i) {
            if (local_bitfield[i]) {
                size_t byte_idx = i / 8;
                size_t bit_idx = 7 - (i % 8); // Left-to-right bit packaging scheme
                packet.payload[byte_idx] |= (1 << bit_idx);
            }
        }

        packet.data_len = byte_count;
        packet.checksum = packet.calculate_checksum();
        return packet;
    }

    bool HandshakeManager::process_incoming_syn_ack(const Packet& packet, std::vector<bool>& out_remote_bitfield, uint32_t total_pieces) {
        if (packet.type != PacketType::SYN_ACK) {
            std::cerr << "[Handshake Error] Expected SYN_ACK framework response." << std::endl;
            return false;
        }

        uint16_t expected_bytes = static_cast<uint16_t>((total_pieces + 7) / 8);
        if (packet.data_len < expected_bytes) {
            std::cerr << "[Handshake Error] SYN_ACK payload size underflow mismatch." << std::endl;
            return false;
        }

        out_remote_bitfield.assign(total_pieces, false);

        // Bitfield Deserialization Loop: Extract boolean bits back out into a readable vector
        for (uint32_t i = 0; i < total_pieces; ++i) {
            size_t byte_idx = i / 8;
            size_t bit_idx = 7 - (i % 8);

            if ((packet.payload[byte_idx] & (1 << bit_idx)) != 0) {
                out_remote_bitfield[i] = true;
            }
        }

        std::cout << "[Handshake Subsystem] SYN_ACK bitfield payload parsed successfully." << std::endl;
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