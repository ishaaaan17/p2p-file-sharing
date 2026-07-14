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
        packet.type = PacketType::SYN; // Using your SYN descriptor flag
        packet.seq_num = 0;

        uint16_t current_offset = 0;

        // Copy identifier text into the unsigned byte payload array
        std::memcpy(packet.payload + current_offset, PROTOCOL_IDENTIFIER.c_str(), PROTOCOL_IDENTIFIER.length());
        current_offset += static_cast<uint16_t>(PROTOCOL_IDENTIFIER.length());

        // Append the local Peer ID
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