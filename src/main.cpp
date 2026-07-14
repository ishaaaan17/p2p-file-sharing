#include <iostream>
#include <vector>
#include "handshake_manager.h"
#include "packet.h"

void print_bitfield_vector(const std::string& label, const std::vector<bool>& bitfield) {
    std::cout << "   " << label << ": [";
    for (bool bit : bitfield) {
        std::cout << (bit ? "1" : "0");
    }
    std::cout << "]" << std::endl;
}

int main() {
    std::cout << "[P2P Protocol Engine] Simulating Full SYN -> SYN_ACK Bitfield Exchange Pipeline...\n" << std::endl;

    // 1. Setup local and remote managers
    P2P::HandshakeManager local_node(1001);
    P2P::HandshakeManager remote_node(2002);

    // 2. Define a mock file map layout (12 pieces)
    // Let's assume the remote peer has pieces: 0, 1, 3, 4, 7, 9, 11
    std::vector<bool> remote_mock_bitfield = { true, true, false, true, true, false, false, true, false, true, false, true };
    uint32_t total_pieces = static_cast<uint32_t>(remote_mock_bitfield.size());

    print_bitfield_vector("Original Remote Bitfield Map", remote_mock_bitfield);
    std::cout << "   Total system bits to transmit: " << total_pieces << " bits." << std::endl;

    // 3. Remote Node serializes its bitfield into a SYN_ACK packet
    std::cout << "\n--- Step 1: Remote Node Compressing Bitfield into Payload ---" << std::endl;
    P2P::Packet syn_ack_pkt = remote_node.prepare_syn_ack_packet(remote_mock_bitfield);
    std::cout << " -> SYN_ACK Frame Prepared. Type ID: " << static_cast<int>(syn_ack_pkt.type) << std::endl;
    std::cout << " -> Compressed Data Payload Size: " << syn_ack_pkt.data_len << " bytes." << std::endl;

    // 4. Local Node receives the SYN_ACK over the network wire and deserializes it
    std::cout << "\n--- Step 2: Local Node Unpacking Payload Bitwise Stream ---" << std::endl;
    std::vector<bool> extracted_local_bitfield;

    if (local_node.process_incoming_syn_ack(syn_ack_pkt, extracted_local_bitfield, total_pieces)) {
        print_bitfield_vector("Decoded Extracted Bitfield Map", extracted_local_bitfield);

        // Integrity Verification Check
        bool integrity_pass = true;
        for (size_t i = 0; i < total_pieces; ++i) {
            if (remote_mock_bitfield[i] != extracted_local_bitfield[i]) {
                integrity_pass = false;
                break;
            }
        }

        if (integrity_pass) {
            std::cout << "\n[SUCCESS] Bitfield packed, transmitted, and decompressed with 100% integrity!" << std::endl;
        }
        else {
            std::cout << "\n[CRITICAL ERROR] Bitfield tracking array payload data corruption detected!" << std::endl;
        }
    }
    else {
        std::cerr << "Failed to parse incoming SYN_ACK packet framework." << std::endl;
    }

    return 0;
}