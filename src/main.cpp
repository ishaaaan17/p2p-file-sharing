#include <iostream>
#include <cstring>
#include "packet.h"

int main() {
    std::cout << "[P2P Testing Engine] Initializing Packet Binary Test..." << std::endl;

    // 1. Construct a mock DATA packet containing a test message
    P2P::Packet original_pkt;
    original_pkt.type = P2P::PacketType::DATA;
    original_pkt.seq_num = 105; // Mock sequence number

    const char* sample_chunk = "Systems Engineering: Verifying raw byte states early.";
    original_pkt.data_len = static_cast<uint16_t>(std::strlen(sample_chunk));
    std::memcpy(original_pkt.payload, sample_chunk, original_pkt.data_len);

    // Assign calculated checksum
    original_pkt.checksum = original_pkt.calculate_checksum();

    // 2. Serialize: Flatten struct variables into a raw byte vector
    std::vector<uint8_t> wire_data = original_pkt.serialize();
    std::cout << " -> Total serialized packet size: " << wire_data.size() << " bytes." << std::endl;

    // 3. Deserialize: Simulate receiving this raw stream back from a peer
    try {
        P2P::Packet unpacked_pkt = P2P::Packet::deserialize(wire_data.data(), wire_data.size());

        std::cout << " -> Unpacked Sequence Number: " << unpacked_pkt.seq_num << std::endl;

        // Print and null-terminate the extracted payload string cleanly
        char verification_string[P2P::MAX_PAYLOAD_SIZE + 1] = { 0 };
        std::memcpy(verification_string, unpacked_pkt.payload, unpacked_pkt.data_len);
        std::cout << " -> Unpacked Payload Data: \"" << verification_string << "\"" << std::endl;

        // Verify the mathematical fingerprint
        if (unpacked_pkt.calculate_checksum() == unpacked_pkt.checksum) {
            std::cout << "\n[SUCCESS] Custom Protocol Binary Layer verified error-free!" << std::endl;
        }
        else {
            std::cout << "\n[CRITICAL FAILURE] Checksum verification mismatch detected." << std::endl;
        }

    }
    catch (const std::exception& ex) {
        std::cerr << "Parsing Exception triggered: " << ex.what() << std::endl;
    }

    return 0;
}