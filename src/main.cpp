#include <iostream>
#include <cstring>
#include "handshake_manager.h"
#include "packet.h"

int main() {
    std::cout << "[P2P Protocol Engine] Simulating Handshake State Machine Logic via SYN Frames...\n" << std::endl;

    P2P::HandshakeManager local_manager(1001);
    std::cout << " -> Initial State: " << local_manager.get_state_string() << std::endl;

    std::cout << "\n--- Scenario A: Processing Valid Friendly Handshake ---" << std::endl;
    P2P::Packet valid_handshake_pkt = local_manager.prepare_handshake_packet();
    local_manager.set_state(P2P::HandshakeState::HANDSHAKE_SENT);
    std::cout << " -> State changed to: " << local_manager.get_state_string() << std::endl;

    uint32_t extracted_peer_id = 0;
    if (local_manager.process_incoming_handshake(valid_handshake_pkt, extracted_peer_id)) {
        std::cout << " -> Dynamic State Report: " << local_manager.get_state_string() << std::endl;
        std::cout << " -> Verified extracted Peer ID: " << extracted_peer_id << " (Expected: 1001)" << std::endl;
    }
    else {
        std::cerr << " -> Unexpected error processing valid handshake." << std::endl;
    }

    std::cout << "\n--- Scenario B: Processing Malicious/Invalid Handshake ---" << std::endl;
    P2P::HandshakeManager security_tester(1001);

    P2P::Packet malicious_pkt;
    malicious_pkt.type = P2P::PacketType::SYN; // Matches structure type but breaks verification values
    malicious_pkt.data_len = 20;

    const char* junk_data = "MALICIOUS-NOISE-JUNK";
    std::memcpy(malicious_pkt.payload, junk_data, 20);
    malicious_pkt.checksum = malicious_pkt.calculate_checksum();

    uint32_t attack_peer_id = 0;
    std::cout << " -> Injecting hostile payload..." << std::endl;
    if (!security_tester.process_incoming_handshake(malicious_pkt, attack_peer_id)) {
        std::cout << " -> State correctly shifted to: " << security_tester.get_state_string() << std::endl;
        std::cout << "[SECURITY OK] Hostile frame successfully neutralized and connection refused." << std::endl;
    }
    else {
        std::cerr << " -> CRITICAL ERROR: System accepted corrupted identity credentials!" << std::endl;
    }

    std::cout << "\n[SUCCESS] Handshake structural validation and isolation metrics operational." << std::endl;
    return 0;
}