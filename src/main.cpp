#include <iostream>
#include <cstring>
#include <string>
#include "packet.h"
#include "socket_manager.h"

int main() {
    std::cout << "[P2P Architecture Engine] Testing SocketManager Orchestration Layer..." << std::endl;

    // 1. Instantiate the high-level manager
    P2P::SocketManager manager;

    // 2. Start the network node on our test port 8888
    uint16_t port = 8888;
    if (!manager.start(port)) {
        std::cerr << "CRITICAL: Failed to start the Socket Manager node instance." << std::endl;
        return -1;
    }

    // 3. Construct an abstraction test packet
    P2P::Packet outbound_pkt;
    outbound_pkt.type = P2P::PacketType::DATA;
    outbound_pkt.seq_num = 42; // Testing a new sequence state

    const char* manager_message = "Socket Manager Architecture: Fully Operational.";
    outbound_pkt.data_len = static_cast<uint16_t>(std::strlen(manager_message));
    std::memcpy(outbound_pkt.payload, manager_message, outbound_pkt.data_len);
    outbound_pkt.checksum = outbound_pkt.calculate_checksum();

    // 4. Send the data to ourselves via the loopback address through the manager interface
    std::string loopback_ip = "127.0.0.1";
    std::cout << " -> Routing transmission out through Manager interface..." << std::endl;
    if (!manager.send_data_to(outbound_pkt, loopback_ip, port)) {
        std::cerr << "Manager transmission dispatch failure!" << std::endl;
        return -1;
    }

    // 5. Poll for the incoming loopback packet rebound
    std::cout << " -> Polling manager for inbound network frames..." << std::endl;
    P2P::Packet inbound_pkt;
    std::string sender_ip;
    uint16_t sender_port = 0;

    if (manager.poll_incoming_traffic(inbound_pkt, sender_ip, sender_port)) {
        std::cout << "\n[ORCHESTRATION SUCCESS] Frame retrieved from SocketManager pipeline!" << std::endl;
        std::cout << "   Peer Address: " << sender_ip << ":" << sender_port << std::endl;
        std::cout << "   Sequence ID:  " << inbound_pkt.seq_num << std::endl;

        // Securely unpack and display string payload bytes
        char clean_string[P2P::MAX_PAYLOAD_SIZE + 1] = { 0 };
        std::memcpy(clean_string, inbound_pkt.payload, inbound_pkt.data_len);
        std::cout << "   Payload:      \"" << clean_string << "\"" << std::endl;

        // Perform mathematical safety integrity validation
        if (inbound_pkt.calculate_checksum() == inbound_pkt.checksum) {
            std::cout << "   Data Status:  PASSED (No bit corruption)" << std::endl;
        }
        else {
            std::cout << "   Data Status:  FAILED (Bit corruption detected)" << std::endl;
        }
    }

    // 6. Tear down the subsystem using the manager control path
    manager.stop();
    std::cout << "\n[Engine Test Closed] Manager shut down cleanly. Stack components recycled." << std::endl;

    return 0;
}