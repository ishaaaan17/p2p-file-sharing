#include <iostream>
#include <vector>
#include <string>
#include <cstring>
#include "piece_manager.h"
#include "peer_session_manager.h"
#include "packet.h"

void print_bool_vector(const std::string& label, const std::vector<bool>& vec) {
    std::cout << "   " << label << ": [";
    for (bool bit : vec) {
        std::cout << (bit ? "1" : "0");
    }
    std::cout << "]" << std::endl;
}

int main() {
    std::cout << "[P2P Exchange Engine] Simulating Request-Response Block Data Loop...\n" << std::endl;

    // 1. Setup our local PieceManager 
    // Passing a mock file path string first, followed by total size (6000) and piece size (1000)
    P2P::PieceManager local_piece_mgr;
    local_piece_mgr.initialize_file_layout("mock_download.dat", 6000, 1000);

    // 2. Setup the remote peer's decoded map from the handshake phase
    std::vector<bool> remote_peer_map = { true, true, true, true, true, true };
    P2P::PeerSessionManager session(2002, remote_peer_map);

    // 3. Generate a simulated 6,000-byte disk data buffer for the remote peer
    std::vector<uint8_t> remote_mock_disk(6000);
    for (size_t i = 0; i < remote_mock_disk.size(); ++i) {
        remote_mock_disk[i] = static_cast<uint8_t>('A' + (i / 1000));
    }

    std::cout << " -> Local tracking initialization complete. Missing pieces: "
        << local_piece_mgr.get_missing_pieces_count() << std::endl;

    // 4. Run the selection strategy engine to target the next block
    int32_t target_piece = session.select_next_piece_to_request(local_piece_mgr);
    std::cout << " -> Strategy engine selected target piece index: " << target_piece << std::endl;

    if (target_piece != -1) {
        // 5. Local node generates the outbound piece request frame
        std::cout << "\n--- Step 1: Local Node Issuing DATA Request Frame ---" << std::endl;
        P2P::Packet request_pkt = session.create_piece_request(static_cast<uint32_t>(target_piece));
        std::cout << " -> Request dispatched for Piece #" << request_pkt.seq_num
            << " (Payload Size: " << request_pkt.data_len << " bytes)" << std::endl;

        // 6. Remote peer intercepts request and pulls data from disk buffer
        std::cout << "\n--- Step 2: Remote Peer Fulfilling Request From Disk Buffer ---" << std::endl;
        uint32_t standard_piece_size = 1000;
        P2P::Packet response_pkt = session.fulfill_piece_request(request_pkt, remote_mock_disk, standard_piece_size);
        std::cout << " -> Response packed. Piece ID: " << response_pkt.seq_num
            << " | Transmitting " << response_pkt.data_len << " bytes." << std::endl;
        std::cout << " -> Data block preview character signature: '" << (char)response_pkt.payload[0] << "'" << std::endl;

        // 7. Local node intercepts response frame and runs validation checks
        std::cout << "\n--- Step 3: Local Node Validating and Processing Inbound Block ---" << std::endl;
        if (session.verify_and_process_incoming_block(response_pkt, static_cast<uint32_t>(target_piece))) {
            std::cout << " -> Checksum validation PASSED." << std::endl;

            // Mark the piece complete inside our manager layout
            local_piece_mgr.mark_piece_complete(static_cast<uint32_t>(target_piece));

            // Manually print the bitfield vector since get_bitfield_string doesn't exist
            print_bool_vector(" -> Piece Manager bitfield updated", local_piece_mgr.get_bitfield());

            // 8. Generate the confirmation ACK to complete the transaction loop
            P2P::Packet ack_pkt = session.create_acknowledgement(static_cast<uint32_t>(target_piece));
            std::cout << "\n--- Step 4: Dispatching Reliable Transaction ACK ---" << std::endl;
            std::cout << " -> ACK frame built for sequence slot: " << ack_pkt.seq_num
                << " | Type ID: " << static_cast<int>(ack_pkt.type) << std::endl;

            std::cout << "\n[SUCCESS] End-to-end data pipeline exchange sequence fully operational!" << std::endl;
        }
        else {
            std::cerr << " -> Verification check failed. Data block discarded." << std::endl;
        }
    }
    else {
        std::cout << "No missing blocks match peer capability." << std::endl;
    }

    return 0;
}