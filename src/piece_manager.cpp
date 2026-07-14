#include "piece_manager.h"
#include <iostream>
#include <algorithm>

namespace P2P {

    PieceManager::PieceManager() : m_total_file_size(0), m_piece_size(0), m_total_pieces(0) {}

    PieceManager::~PieceManager() {}

    void PieceManager::initialize_file_layout(const std::string& file_path, uint64_t file_size, uint32_t piece_size) {
        m_file_path = file_path;
        m_total_file_size = file_size;
        m_piece_size = piece_size;

        if (m_piece_size == 0 || m_total_file_size == 0) {
            m_total_pieces = 0;
            m_pieces.clear();
            return;
        }

        // Systems Safe Division: Accounts for uneven final blocks (tail pieces)
        m_total_pieces = static_cast<uint32_t>((m_total_file_size + m_piece_size - 1) / m_piece_size);
        m_pieces.resize(m_total_pieces);

        std::cout << "[PieceManager] Slicing target asset: " << m_file_path << std::endl;
        std::cout << " -> Total File Size: " << m_total_file_size << " bytes." << std::endl;
        std::cout << " -> Standard Chunk Size: " << m_piece_size << " bytes." << std::endl;
        std::cout << " -> Calculated Total Pieces: " << m_total_pieces << std::endl;

        // Initialize every individual block metadata structure
        for (uint32_t i = 0; i < m_total_pieces; ++i) {
            m_pieces[i].index = i;
            m_pieces[i].is_downloaded = false;

            // Catch the final leftover block size mismatch explicitly
            if (i == m_total_pieces - 1) {
                uint64_t remainder = m_total_file_size % m_piece_size;
                m_pieces[i].size = (remainder == 0) ? m_piece_size : static_cast<uint32_t>(remainder);
            }
            else {
                m_pieces[i].size = m_piece_size;
            }
        }

        std::cout << " -> Tail piece [" << m_total_pieces - 1 << "] size budgeted at: "
            << m_pieces[m_total_pieces - 1].size << " bytes." << std::endl;
    }

    void PieceManager::mark_piece_complete(uint32_t index) {
        if (index >= m_total_pieces) {
            std::cerr << "[PieceManager Error] Attempted to mark invalid index out of bounds: " << index << std::endl;
            return;
        }
        m_pieces[index].is_downloaded = true;
        std::cout << "[PieceManager] Piece #" << index << " verified and marked COMPLETE." << std::endl;
    }

    std::vector<bool> PieceManager::get_bitfield() const {
        std::vector<bool> bitfield(m_total_pieces, false);
        for (uint32_t i = 0; i < m_total_pieces; ++i) {
            bitfield[i] = m_pieces[i].is_downloaded;
        }
        return bitfield;
    }

    bool PieceManager::is_file_complete() const {
        if (m_total_pieces == 0) return false;

        // Return true only if all blocks have been toggled to true
        return std::all_of(m_pieces.begin(), m_pieces.end(), [](const PieceInfo& info) {
            return info.is_downloaded;
            });
    }

    uint32_t PieceManager::get_missing_pieces_count() const {
        uint32_t missing_count = 0;
        for (const auto& piece : m_pieces) {
            if (!piece.is_downloaded) {
                missing_count++;
            }
        }
        return missing_count;
    }

} // namespace P2P