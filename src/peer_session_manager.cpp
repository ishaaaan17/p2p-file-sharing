#include "peer_session_manager.h"
#include <cstring>
#include <iostream>

namespace P2P {

    PeerSessionManager::PeerSessionManager(uint32_t remote_id, const std::vector<bool>& remote_bitfield)
        : m_remote_peer_id(remote_id), m_remote_bitfield(remote_bitfield) {
    }

    PeerSessionManager::~PeerSessionManager() {}

    int32_t PeerSessionManager::select_next_piece_to_request(const PieceManager& local_piece_mgr) const {
        std::vector<bool> local_bitfield = local_piece_mgr.get_bitfield();

        // Strategy: Iterate through our missing chunks and match them against what the peer owns
        for (size_t i = 0; i < local_bitfield.size(); ++i) {
            // If the local node lacks the piece, but the remote node possesses it, select it!
            if (!local_bitfield[i] && i < m_remote_bitfield.size() && m_remote_bitfield[i]) {
                return static_cast<int32_t>(i);
            }
        }

        return -1; // No matching pieces available to request from this specific peer
    }

    Packet PeerSessionManager::create_piece_request(uint32_t piece_index) const {
        Packet packet;
        packet.type = PacketType::DATA; // Utilizing DATA flag to initiate the request pipeline
        packet.seq_num = piece_index;   // Sequence number stores the requested piece index
        packet.data_len = 0;            // Data length is 0 because there is no payload data yet
        packet.checksum = packet.calculate_checksum();
        return packet;
    }

    Packet PeerSessionManager::fulfill_piece_request(const Packet& request_packet, const std::vector<uint8_t>& mock_disk_buffer, uint32_t piece_size) const {
        Packet response_pkt;
        response_pkt.type = PacketType::DATA;
        response_pkt.seq_num = request_packet.seq_num;

        uint32_t piece_index = request_packet.seq_num;
        size_t buffer_offset = static_cast<size_t>(piece_index) * piece_size;

        // Ensure we do not read past the boundaries of our data buffer
        if (buffer_offset + piece_size <= mock_disk_buffer.size() && piece_size <= MAX_PAYLOAD_SIZE) {
            std::memcpy(response_pkt.payload, mock_disk_buffer.data() + buffer_offset, piece_size);
            response_pkt.data_len = static_cast<uint16_t>(piece_size);
        }
        else {
            // Handle tail piece scenarios where the block size is shorter than standard pieces
            size_t remaining_bytes = mock_disk_buffer.size() - buffer_offset;
            if (remaining_bytes <= MAX_PAYLOAD_SIZE) {
                std::memcpy(response_pkt.payload, mock_disk_buffer.data() + buffer_offset, remaining_bytes);
                response_pkt.data_len = static_cast<uint16_t>(remaining_bytes);
            }
            else {
                response_pkt.data_len = 0;
            }
        }

        response_pkt.checksum = response_pkt.calculate_checksum();
        return response_pkt;
    }

    bool PeerSessionManager::verify_and_process_incoming_block(const Packet& data_packet, uint32_t expected_index) const {
        if (data_packet.type != PacketType::DATA) {
            std::cerr << "[Session Error] Non-DATA frame dropped during collection loops." << std::endl;
            return false;
        }

        if (data_packet.seq_num != expected_index) {
            std::cerr << "[Session Error] Out of sync sequence block index matching mismatch." << std::endl;
            return false;
        }

        // Run the mathematical checksum validation to protect against data corruption over the wire
        uint16_t computed_checksum = data_packet.calculate_checksum();
        if (data_packet.checksum != computed_checksum) {
            std::cerr << "[Session Error] Checksum corruption detected! Frame isolated." << std::endl;
            return false;
        }

        return true;
    }

    Packet PeerSessionManager::create_acknowledgement(uint32_t piece_index) const {
        Packet packet;
        packet.type = PacketType::ACK; // Utilizing ACK flag to signal reliable receipt
        packet.seq_num = piece_index;
        packet.data_len = 0;
        packet.checksum = packet.calculate_checksum();
        return packet;
    }

} // namespace P2P