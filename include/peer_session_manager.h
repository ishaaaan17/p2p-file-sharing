#pragma once
#include <cstdint>
#include <vector>
#include "packet.h"
#include "piece_manager.h"

namespace P2P {

    class PeerSessionManager {
    private:
        uint32_t m_remote_peer_id;
        std::vector<bool> m_remote_bitfield;

    public:
        PeerSessionManager(uint32_t remote_id, const std::vector<bool>& remote_bitfield);
        ~PeerSessionManager();

        // Strategy Engine: Evaluates our missing slots against peer availability to find the next target
        int32_t select_next_piece_to_request(const PieceManager& local_piece_mgr) const;

        // Creates a data request frame targeting a specific chunk index
        Packet create_piece_request(uint32_t piece_index) const;

        // Handles an incoming request frame, filling it with simulated file disk data
        Packet fulfill_piece_request(const Packet& request_packet, const std::vector<uint8_t>& mock_disk_buffer, uint32_t piece_size) const;

        // Validates an incoming data block payload before marking it complete
        bool verify_and_process_incoming_block(const Packet& data_packet, uint32_t expected_index) const;

        // Creates an acknowledgement packet to confirm a successful chunk write
        Packet create_acknowledgement(uint32_t piece_index) const;

        uint32_t get_remote_peer_id() const { return m_remote_peer_id; }
    };

} // namespace P2P