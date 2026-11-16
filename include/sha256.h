#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace P2P {

    // Compact, dependency-free SHA-256 (FIPS 180-4), used to verify piece
    // integrity against the hash manifest exchanged during the handshake.
    // Verified against the standard test vectors in tests/test_main.cpp.
    class Sha256 {
    public:
        Sha256();
        void update(const uint8_t* data, size_t length);
        std::array<uint8_t, 32> final_digest();

    private:
        void transform(const uint8_t block[64]);

        uint32_t m_state[8];
        uint64_t m_bit_length;
        uint8_t m_buffer[64];
        size_t m_buffer_size;
        bool m_finalised;
        std::array<uint8_t, 32> m_digest;
    };

    // One-shot helpers.
    std::array<uint8_t, 32> sha256(const uint8_t* data, size_t length);
    std::string sha256_hex(const uint8_t* data, size_t length);
    std::string to_hex(const std::array<uint8_t, 32>& digest);

} // namespace P2P
