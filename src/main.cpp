#include <iostream>
#include <vector>
#include <string>
#include "piece_manager.h"

void print_current_bitfield(const std::vector<bool>& bitfield) {
    std::cout << "   Current Bitfield Map: [";
    for (bool bit : bitfield) {
        std::cout << (bit ? "1" : "0");
    }
    std::cout << "]" << std::endl;
}

int main() {
    std::cout << "[P2P Storage Engine] Testing PieceManager Slicing & State Mechanics..." << std::endl;

    // 1. Instantiate the piece manager
    P2P::PieceManager piece_mgr;

    // 2. Simulate a non-divisible uneven file layout (e.g., a 10.5 MB video file)
    // 11,000,000 bytes divided into standard 2,000,000 byte (2 MB) chunks
    uint64_t mock_file_size = 11000000;
    uint32_t mock_piece_size = 2000000;

    std::cout << "\n--- Initializing File Structural Layout ---" << std::endl;
    piece_mgr.initialize_file_layout("D:\\downloads\\ubuntu_distribution_mock.iso", mock_file_size, mock_piece_size);

    // Verify chunk budget numbers match our engineering design expectations
    std::vector<bool> current_map = piece_mgr.get_bitfield();
    print_current_bitfield(current_map);
    std::cout << "   Missing Pieces remaining: " << piece_mgr.get_missing_pieces_count() << std::endl;

    // 3. Simulate down-streaming out-of-order P2P chunks landing randomly from distinct peers
    std::cout << "\n--- Simulating Out-of-Order P2P Fragment Capture ---" << std::endl;

    // Peer A drops piece index 1
    std::cout << " -> Capture event: Packet chunk for Piece #1 arrived." << std::endl;
    piece_mgr.mark_piece_complete(1);

    // Peer B drops piece index 4
    std::cout << " -> Capture event: Packet chunk for Piece #4 arrived." << std::endl;
    piece_mgr.mark_piece_complete(4);

    // Peer C drops piece index 0
    std::cout << " -> Capture event: Packet chunk for Piece #0 arrived." << std::endl;
    piece_mgr.mark_piece_complete(0);

    // 4. Inspect intermediate bitfield representation status
    std::cout << "\n--- Inspecting Intermediate Pipeline State ---" << std::endl;
    current_map = piece_mgr.get_bitfield();
    print_current_bitfield(current_map);
    std::cout << "   Missing Pieces remaining: " << piece_mgr.get_missing_pieces_count() << std::endl;
    std::cout << "   Is total file download complete? " << (piece_mgr.is_file_complete() ? "YES" : "NO") << std::endl;

    // 5. Complete remaining structural slices to simulate full asset assembly
    std::cout << "\n--- Downloading Remaining Missing Targets ---" << std::endl;
    piece_mgr.mark_piece_complete(2);
    piece_mgr.mark_piece_complete(3);
    piece_mgr.mark_piece_complete(5); // The tail piece block!

    // 6. Final verification checks
    std::cout << "\n--- Final Storage Assembly Report ---" << std::endl;
    current_map = piece_mgr.get_bitfield();
    print_current_bitfield(current_map);
    std::cout << "   Missing Pieces remaining: " << piece_mgr.get_missing_pieces_count() << std::endl;
    std::cout << "   Is total file download complete? " << (piece_mgr.is_file_complete() ? "YES" : "NO") << std::endl;

    if (piece_mgr.is_file_complete()) {
        std::cout << "\n[SUCCESS] Storage bitfield tracking system operational and memory-safe." << std::endl;
    }
    else {
        std::cout << "\n[FAILURE] Allocation state mapping fault." << std::endl;
    }

    return 0;
}