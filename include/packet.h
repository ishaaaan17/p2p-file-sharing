#pragma once
#include <cstdint>
#include <vector>

namespace P2P {

    // These values identify what type of message is being sent.
    enum class PacketType : uint8_t {
        SYN = 1,
        SYN_ACK = 2,
        DATA = 3,
        ACK = 4,
        FIN = 5,
        CHOKE = 6,   // <-- Add this
        UNCHOKE = 7  // <-- Add this
    };
    // Protocol capacity boundaries
    constexpr size_t MAX_PAYLOAD_SIZE = 1400;
    constexpr size_t HEADER_SIZE = 9; // 1 byte (Type) + 4 bytes (Seq) + 2 bytes (Len) + 2 bytes (Checksum)

    // The layout of our custom reliable UDP frame
    struct Packet {
        PacketType type;
        uint32_t seq_num;
        uint16_t data_len;
        uint16_t checksum;
        uint8_t payload[MAX_PAYLOAD_SIZE];

        // Transforms our C++ structure variables into a flat, raw byte sequence
        std::vector<uint8_t> serialize() const;

        // Reconstructs a structured C++ object from raw incoming network data
        static Packet deserialize(const uint8_t* buffer, size_t buffer_len);

        // Mathematical validation formula to detect packet corruption over the wire
        uint16_t calculate_checksum() const;
    };

} // namespace P2P