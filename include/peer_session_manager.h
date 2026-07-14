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

        // Rarest-First Optimization: Scans overall swarm frequency maps to select the best piece index
        int32_t select_rarest_piece_to_request(
            const PieceManager& local_piece_mgr,
            const std::vector<std::vector<bool>>& global_swarm_bitfields
        ) const;

        // Creates a data request frame targeting a specific chunk index
        Packet create_piece_request(uint32_t piece_index) const;

        // Fulfills a piece request by pulling real data directly from the PieceManager disk layer
        Packet fulfill_piece_request_from_disk(const Packet& request_packet, PieceManager& local_piece_mgr) const;

        // Validates an incoming data block payload before marking it complete
        bool verify_and_process_incoming_block(const Packet& data_packet, uint32_t expected_index) const;

        // Creates an acknowledgement packet to confirm a successful chunk write
        Packet create_acknowledgement(uint32_t piece_index) const;

        // Creates a graceful connection teardown notification frame
        Packet create_teardown_notification() const;

        uint32_t get_remote_peer_id() const { return m_remote_peer_id; }
        std::vector<bool> get_remote_bitfield() const { return m_remote_bitfield; }
    };

} // namespace P2P