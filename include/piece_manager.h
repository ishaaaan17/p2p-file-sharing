#pragma once
#include <vector>
#include <string>
#include <cstdint>

namespace P2P {

    struct PieceInfo {
        uint32_t index;
        uint32_t size;
        bool is_downloaded;
        // In a later phase, we will add: std::vector<uint8_t> expected_hash;
    };

    class PieceManager {
    private:
        std::string m_file_path;
        uint64_t m_total_file_size;
        uint32_t m_piece_size;
        uint32_t m_total_pieces;
        std::vector<PieceInfo> m_pieces;

    public:
        PieceManager();
        ~PieceManager();

        // Configures the manager for a new tracking layout
        void initialize_file_layout(const std::string& file_path, uint64_t file_size, uint32_t piece_size);

        // Marks a specific piece chunk as verified and written to disk
        void mark_piece_complete(uint32_t index);

        // Generates a boolean bitfield array representation for peer exchange updates
        std::vector<bool> get_bitfield() const;

        // Validates whether the entire file transfer architecture is complete
        bool is_file_complete() const;

        // Accessor metrics for our download tracking layers
        uint32_t get_total_pieces() const { return m_total_pieces; }
        uint32_t get_missing_pieces_count() const;
    };

} // namespace P2P