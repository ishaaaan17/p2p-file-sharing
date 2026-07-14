#include "peer_session_manager.h"
#include <cstring>
#include <iostream>
#include <limits>

namespace P2P {

    PeerSessionManager::PeerSessionManager(uint32_t remote_id, const std::vector<bool>& remote_bitfield)
        : m_remote_peer_id(remote_id), m_remote_bitfield(remote_bitfield) {
    }

    PeerSessionManager::~PeerSessionManager() {}

    int32_t PeerSessionManager::select_rarest_piece_to_request(
        const PieceManager& local_piece_mgr,
        const std::vector<std::vector<bool>>& global_swarm_bitfields
    ) const {
        std::vector<bool> local_bitfield = local_piece_mgr.get_bitfield();
        size_t num_pieces = local_bitfield.size();

        std::vector<uint32_t> piece_frequencies(num_pieces, 0);

        // Calculate the availability frequency of each piece within the active swarm
        for (const auto& bitfield : global_swarm_bitfields) {
            for (size_t i = 0; i < num_pieces && i < bitfield.size(); ++i) {
                if (bitfield[i]) {
                    piece_frequencies[i]++;
                }
            }
        }

        int32_t targeted_index = -1;
        uint32_t lowest_frequency = std::numeric_limits<uint32_t>::max();

        // Rarest-First Strategy Core Selection Loop
        for (size_t i = 0; i < num_pieces; ++i) {
            // We only care about pieces we are missing, but this specific peer owns
            if (!local_bitfield[i] && i < m_remote_bitfield.size() && m_remote_bitfield[i]) {
                // Find the piece with the absolute minimum availability in the swarm
                if (piece_frequencies[i] < lowest_frequency) {
                    lowest_frequency = piece_frequencies[i];
                    targeted_index = static_cast<int32_t>(i);
                }
            }
        }

        return targeted_index;
    }

    Packet PeerSessionManager::create_piece_request(uint32_t piece_index) const {
        Packet packet;
        packet.type = PacketType::DATA;
        packet.seq_num = piece_index;
        packet.data_len = 0;
        packet.checksum = packet.calculate_checksum();
        return packet;
    }

    Packet PeerSessionManager::fulfill_piece_request_from_disk(const Packet& request_packet, PieceManager& local_piece_mgr) const {
        Packet response_pkt;
        response_pkt.type = PacketType::DATA;
        response_pkt.seq_num = request_packet.seq_num;

        uint32_t piece_index = request_packet.seq_num;
        uint32_t bytes_read = 0;

        // Production Layer: Stream straight out of physical storage disk via PieceManager
        if (local_piece_mgr.read_piece_from_disk(piece_index, response_pkt.payload, bytes_read)) {
            response_pkt.data_len = static_cast<uint16_t>(bytes_read);
        }
        else {
            response_pkt.data_len = 0;
        }

        response_pkt.checksum = response_pkt.calculate_checksum();
        return response_pkt;
    }

    bool PeerSessionManager::verify_and_process_incoming_block(const Packet& data_packet, uint32_t expected_index) const {
        if (data_packet.type != PacketType::DATA) return false;
        if (data_packet.seq_num != expected_index) return false;
        if (data_packet.checksum != data_packet.calculate_checksum()) return false;
        return true;
    }

    Packet PeerSessionManager::create_acknowledgement(uint32_t piece_index) const {
        Packet packet;
        packet.type = PacketType::ACK;
        packet.seq_num = piece_index;
        packet.data_len = 0;
        packet.checksum = packet.calculate_checksum();
        return packet;
    }

    Packet PeerSessionManager::create_teardown_notification() const {
        Packet packet;
        packet.type = PacketType::FIN; // Explicit grace close flag mapping
        packet.seq_num = 0;
        packet.data_len = 0;
        packet.checksum = packet.calculate_checksum();
        return packet;
    }

} // namespace P2P