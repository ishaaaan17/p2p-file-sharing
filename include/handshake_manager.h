#pragma once
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

    class HandshakeManager {
    private:
        static const std::string PROTOCOL_IDENTIFIER;
        uint32_t m_local_peer_id;
        HandshakeState m_current_state;

    public:
        HandshakeManager(uint32_t local_id);
        ~HandshakeManager();

        // Generates an initialization frame utilizing PacketType::SYN
        Packet prepare_handshake_packet() const;

        // Validates the protocol identity credentials
        bool process_incoming_handshake(const Packet& packet, uint32_t& out_remote_peer_id);

        HandshakeState get_state() const { return m_current_state; }
        void set_state(HandshakeState new_state) { m_current_state = new_state; }
        std::string get_state_string() const;
    };

} // namespace P2P