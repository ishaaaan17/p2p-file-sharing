#include "packet.h"
#include <cstring>
#include <stdexcept>

namespace P2P {

    // Converts our variables into a sequential array of Big-Endian network bytes
    std::vector<uint8_t> Packet::serialize() const {
        std::vector<uint8_t> buffer(HEADER_SIZE + data_len);

        // Byte 0: Type
        buffer[0] = static_cast<uint8_t>(type);

        // Bytes 1-4: Sequence Number (Shifting 32-bit int into 4 individual bytes)
        buffer[1] = static_cast<uint8_t>((seq_num >> 24) & 0xFF);
        buffer[2] = static_cast<uint8_t>((seq_num >> 16) & 0xFF);
        buffer[3] = static_cast<uint8_t>((seq_num >> 8) & 0xFF);
        buffer[4] = static_cast<uint8_t>(seq_num & 0xFF);

        // Bytes 5-6: Data Length (Shifting 16-bit int into 2 bytes)
        buffer[5] = static_cast<uint8_t>((data_len >> 8) & 0xFF);
        buffer[6] = static_cast<uint8_t>(data_len & 0xFF);

        // Bytes 7-8: Checksum
        buffer[7] = static_cast<uint8_t>((checksum >> 8) & 0xFF);
        buffer[8] = static_cast<uint8_t>(checksum & 0xFF);

        // Copy actual payload data right behind the 9-byte header
        if (data_len > 0) {
            std::memcpy(&buffer[HEADER_SIZE], payload, data_len);
        }

        return buffer;
    }

    // Unpacks an incoming raw buffer back into our C++ structure variables
    Packet Packet::deserialize(const uint8_t* buffer, size_t buffer_len) {
        if (buffer_len < HEADER_SIZE) {
            throw std::runtime_error("Network fragment drop: data is smaller than our 9-byte header.");
        }

        Packet packet;
        packet.type = static_cast<PacketType>(buffer[0]);

        // Rebuild sequence number from 4 individual big-endian network bytes
        packet.seq_num = (static_cast<uint32_t>(buffer[1]) << 24) |
            (static_cast<uint32_t>(buffer[2]) << 16) |
            (static_cast<uint32_t>(buffer[3]) << 8) |
            static_cast<uint32_t>(buffer[4]);

        // Rebuild data length
        packet.data_len = (static_cast<uint16_t>(buffer[5]) << 8) |
            static_cast<uint16_t>(buffer[6]);

        if (packet.data_len > MAX_PAYLOAD_SIZE || HEADER_SIZE + packet.data_len > buffer_len) {
            throw std::runtime_error("Malformed packet payload length matches corrupted values.");
        }

        packet.checksum = (static_cast<uint16_t>(buffer[7]) << 8) |
            static_cast<uint16_t>(buffer[8]);

        if (packet.data_len > 0) {
            std::memcpy(packet.payload, &buffer[HEADER_SIZE], packet.data_len);
        }

        return packet;
    }

    // Classic Internet Checksum calculation loop (1's complement sum)
    uint16_t Packet::calculate_checksum() const {
        uint32_t sum = 0;

        sum += static_cast<uint8_t>(type);
        sum += (seq_num >> 16) & 0xFFFF;
        sum += seq_num & 0xFFFF;
        sum += data_len;

        for (size_t i = 0; i < data_len; i += 2) {
            if (i + 1 < data_len) {
                uint16_t word = (static_cast<uint16_t>(payload[i]) << 8) | payload[i + 1];
                sum += word;
            }
            else {
                uint16_t word = static_cast<uint16_t>(payload[i]) << 8;
                sum += word;
            }
        }

        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }

        return static_cast<uint16_t>(~sum);
    }

} // namespace P2P