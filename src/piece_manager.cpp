#include "piece_manager.h"
#include <iostream>
#include <algorithm>

namespace P2P {

    PieceManager::PieceManager() : m_total_file_size(0), m_piece_size(0), m_total_pieces(0) {}
    PieceManager::~PieceManager() {}

    bool PieceManager::initialize_file_layout(const std::string& file_path, uint32_t total_size, uint32_t piece_size) {
        std::lock_guard<std::mutex> lock(m_disk_mutex);
        m_file_path = file_path;
        m_total_file_size = total_size;
        m_piece_size = piece_size;

        // Calculate total structural blocks required
        m_total_pieces = (total_size + piece_size - 1) / piece_size;
        m_bitfield.assign(m_total_pieces, false);

        // Production Trick: Pre-allocate an empty dummy file on the drive matching the complete size instantly
        std::ofstream out_file(m_file_path, std::ios::binary | std::ios::out);
        if (!out_file) {
            std::cerr << "[Disk Error] Could not allocate target file space: " << m_file_path << std::endl;
            return false;
        }

        // Fill file with dummy zeros to claim physical blocks from the operating system
        std::vector<char> zero_buffer(64 * 1024, 0); // 64KB block chunks
        uint32_t bytes_written = 0;
        while (bytes_written < m_total_file_size) {
            uint32_t to_write = std::min(static_cast<uint32_t>(zero_buffer.size()), m_total_file_size - bytes_written);
            out_file.write(zero_buffer.data(), to_write);
            bytes_written += to_write;
        }
        out_file.close();

        std::cout << "[PieceManager] Created binary asset placeholder: " << m_file_path << " (" << m_total_file_size << " bytes Allocated)" << std::endl;
        return true;
    }

    bool PieceManager::write_piece_to_disk(uint32_t piece_index, const uint8_t* payload_buffer, uint32_t payload_size) {
        std::lock_guard<std::mutex> lock(m_disk_mutex);

        // Open file stream in "in/out" mode so we overwrite specific regions without cleaning the rest
        std::fstream disk_file(m_file_path, std::ios::binary | std::ios::in | std::ios::out);
        if (!disk_file) {
            std::cerr << "[Disk Error] Access failure during piece write loop." << std::endl;
            return false;
        }

        // Calculate precise mathematical seek position
        uint64_t byte_offset = static_cast<uint64_t>(piece_index) * m_piece_size;
        disk_file.seekp(byte_offset); // Jump directly to position
        disk_file.write(reinterpret_cast<const char*>(payload_buffer), payload_size);
        disk_file.close();

        m_bitfield[piece_index] = true; // Set piece complete
        return true;
    }

    bool PieceManager::read_piece_from_disk(uint32_t piece_index, uint8_t* out_buffer, uint32_t& out_size) const {
        std::lock_guard<std::mutex> lock(m_disk_mutex);

        std::ifstream disk_file(m_file_path, std::ios::binary);
        if (!disk_file) return false;

        uint64_t byte_offset = static_cast<uint64_t>(piece_index) * m_piece_size;
        disk_file.seekg(byte_offset); // Seek input pointer

        // Calculate expected piece size (accounting for tail piece variations)
        uint32_t expected_size = m_piece_size;
        if (piece_index == m_total_pieces - 1) {
            expected_size = m_total_file_size - static_cast<uint32_t>(byte_offset);
        }

        disk_file.read(reinterpret_cast<char*>(out_buffer), expected_size);
        out_size = static_cast<uint32_t>(disk_file.gcount()); // Set exact byte read metric
        disk_file.close();

        return out_size > 0;
    }

    void PieceManager::mark_piece_complete(uint32_t piece_index) {
        std::lock_guard<std::mutex> lock(m_disk_mutex);
        if (piece_index < m_bitfield.size()) {
            m_bitfield[piece_index] = true;
        }
    }

    std::vector<bool> PieceManager::get_bitfield() const {
        std::lock_guard<std::mutex> lock(m_disk_mutex);
        return m_bitfield;
    }

    uint32_t PieceManager::get_missing_pieces_count() const {
        std::lock_guard<std::mutex> lock(m_disk_mutex);
        uint32_t missing = 0;
        for (bool b : m_bitfield) {
            if (!b) missing++;
        }
        return missing;
    }

} // namespace P2P