#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <cstdint>

namespace P2P {

    class PieceManager {
    private:
        std::string m_file_path;
        uint32_t m_total_file_size;
        uint32_t m_piece_size;
        uint32_t m_total_pieces;
        std::vector<bool> m_bitfield;

        // Thread safety mechanism to prevent multiple threads from writing to the disk simultaneously
        mutable std::mutex m_disk_mutex;

    public:
        PieceManager();
        ~PieceManager();

        // Initializes file tracking arrays and allocates target empty binary files on disk
        bool initialize_file_layout(const std::string& file_path, uint32_t total_size, uint32_t piece_size);

        // High-performance disk writer targeting direct byte positions
        bool write_piece_to_disk(uint32_t piece_index, const uint8_t* payload_buffer, uint32_t payload_size);

        // High-performance disk reader targeting direct byte positions for seeding
        bool read_piece_from_disk(uint32_t piece_index, uint8_t* out_buffer, uint32_t& out_size) const;

        void mark_piece_complete(uint32_t piece_index);

        std::vector<bool> get_bitfield() const;
        uint32_t get_missing_pieces_count() const;
        uint32_t get_total_pieces() const { return m_total_pieces; }
        uint32_t get_piece_size() const { return m_piece_size; }
    };

} // namespace P2P