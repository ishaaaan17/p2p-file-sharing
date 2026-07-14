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

        Packet prepare_handshake_packet() const;
        bool process_incoming_handshake(const Packet& packet, uint32_t& out_remote_peer_id);

        // NEW Phase 8: Creates a SYN_ACK frame containing the packed bitfield map
        Packet prepare_syn_ack_packet(const std::vector<bool>& local_bitfield) const;

        // NEW Phase 8: Parses an incoming SYN_ACK packet to extract the peer's bitfield map
        bool process_incoming_syn_ack(const Packet& packet, std::vector<bool>& out_remote_bitfield, uint32_t total_pieces);

        HandshakeState get_state() const { return m_current_state; }
        void set_state(HandshakeState new_state) { m_current_state = new_state; }
        std::string get_state_string() const;
    };

} // namespace P2P