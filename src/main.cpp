#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include "packet.h"
#include "socket_manager.h"
#include "piece_manager.h"
#include "handshake_manager.h"
#include "peer_session_manager.h"

// Forward declaration to inform the compiler about our helper function
bool getkey_check_async(char key);

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "        P2P DECENTRALIZED LIVE NETWORK ELEMENT          " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    // 1. Core setup configuration metrics
    uint32_t local_peer_id = 0;
    uint16_t local_port = 0;
    uint16_t target_port = 0;
    std::string target_ip = "127.0.0.1";

    std::cout << "Enter Local Peer ID (e.g., 1001 or 2002): ";
    std::cin >> local_peer_id;
    std::cout << "Enter Local Port to BIND and listen on: ";
    std::cin >> local_port;
    std::cout << "Enter Target Port to CONNECT to: ";
    std::cin >> target_port;

    // 2. Initialize Core Subsystems
    P2P::SocketManager socket_mgr;
    P2P::HandshakeManager handshake_mgr(local_peer_id);
    P2P::PieceManager piece_mgr;

    // Initialize a mock file asset mapping of 6 pieces
    piece_mgr.initialize_file_layout("live_transfer.dat", 6000, 1000);

    // If we are peer 2002, let's pretend we have all pieces to seed [111111]
    if (local_peer_id == 2002) {
        for (uint32_t i = 0; i < 6; ++i) {
            piece_mgr.mark_piece_complete(i);
        }
    }

    std::cout << "\n[Local Status] Current Bitfield Array: [";
    for (bool b : piece_mgr.get_bitfield()) std::cout << (b ? "1" : "0");
    std::cout << "]" << std::endl;

    // Launch background asynchronous thread listener
    if (!socket_mgr.start(local_port)) {
        std::cerr << "CRITICAL: Listening bind layer setup failure." << std::endl;
        return -1;
    }

    // 3. Construct a pseudo disk data block buffer for seeding
    std::vector<uint8_t> local_disk_buffer(6000);
    for (size_t i = 0; i < local_disk_buffer.size(); ++i) {
        local_disk_buffer[i] = static_cast<uint8_t>('A' + (i / 1000));
    }

    std::unique_ptr<P2P::PeerSessionManager> live_session = nullptr;
    bool running = true;

    std::cout << "\n -> System online! Press 'h' to send Handshake SYN, or 'q' to quit." << std::endl;

    P2P::Packet inbound_pkt;
    std::string sender_ip;
    uint16_t sender_port = 0;

    // 4. Central Real-Time Execution Loop
    while (running) {
        // Non-blocking intercept from the async background queue thread
        if (socket_mgr.pop_incoming_frame(inbound_pkt, sender_ip, sender_port)) {

            if (inbound_pkt.type == P2P::PacketType::SYN) {
                uint32_t remote_id = 0;
                if (handshake_mgr.process_incoming_handshake(inbound_pkt, remote_id)) {
                    std::cout << "\n[NETWORK EVENT] Valid SYN received from Peer #" << remote_id << std::endl;

                    // Reply back instantly with our SYN_ACK bitfield payload map
                    P2P::Packet syn_ack = handshake_mgr.prepare_syn_ack_packet(piece_mgr.get_bitfield());
                    socket_mgr.send_data_to(syn_ack, sender_ip, sender_port);
                    std::cout << " -> Dispatched compressed SYN_ACK bitfield down the wire." << std::endl;
                }
            }
            else if (inbound_pkt.type == P2P::PacketType::SYN_ACK) {
                std::vector<bool> remote_bitfield;
                if (handshake_mgr.process_incoming_syn_ack(inbound_pkt, remote_bitfield, 6)) {
                    std::cout << "\n[NETWORK EVENT] SYN_ACK captured from " << sender_ip << ":" << sender_port << std::endl;

                    // Instantly spin up our active exchange tracking session
                    live_session = std::make_unique<P2P::PeerSessionManager>(local_peer_id, remote_bitfield);
                    std::cout << " -> Live Session established with active remote bitfield tracking!" << std::endl;

                    // Trigger an immediate file piece download request strategy check
                    int32_t target_chunk = live_session->select_next_piece_to_request(piece_mgr);
                    if (target_chunk != -1) {
                        P2P::Packet req = live_session->create_piece_request(static_cast<uint32_t>(target_chunk));
                        socket_mgr.send_data_to(req, target_ip, target_port);
                        std::cout << " -> Issued live DATA request frame for missing Piece #" << target_chunk << std::endl;
                    }
                }
            }
            else if (inbound_pkt.type == P2P::PacketType::DATA) {
                // Scenario A: It's a request from a peer asking us for data (empty payload size)
                if (inbound_pkt.data_len == 0 && live_session) {
                    std::cout << "\n[NETWORK EVENT] Peer requested Data Piece #" << inbound_pkt.seq_num << std::endl;
                    P2P::Packet response = live_session->fulfill_piece_request(inbound_pkt, local_disk_buffer, 1000);
                    socket_mgr.send_data_to(response, sender_ip, sender_port);
                    std::cout << " -> Dispatched packed block data chunk (" << response.data_len << " bytes)." << std::endl;
                }
                // Scenario B: It's the full data payload coming back to us
                else if (inbound_pkt.data_len > 0 && live_session) {
                    std::cout << "\n[NETWORK EVENT] Inbound full data block payload arrived for Piece #" << inbound_pkt.seq_num << std::endl;
                    if (live_session->verify_and_process_incoming_block(inbound_pkt, inbound_pkt.seq_num)) {
                        piece_mgr.mark_piece_complete(inbound_pkt.seq_num);
                        std::cout << " -> Checksum validation verified! Local bitfield updated." << std::endl;

                        // Fire back the transactional receipt validation ACK frame
                        P2P::Packet ack = live_session->create_acknowledgement(inbound_pkt.seq_num);
                        socket_mgr.send_data_to(ack, sender_ip, sender_port);

                        // Loop Strategy: Check if there's another missing chunk to request
                        int32_t next_chunk = live_session->select_next_piece_to_request(piece_mgr);
                        if (next_chunk != -1) {
                            P2P::Packet req = live_session->create_piece_request(static_cast<uint32_t>(next_chunk));
                            socket_mgr.send_data_to(req, target_ip, target_port);
                            std::cout << " -> Issued loop DATA request frame for missing Piece #" << next_chunk << std::endl;
                        }
                        else {
                            std::cout << "\n========================================================" << std::endl;
                            std::cout << "      SUCCESS: ALL MISSING PIECES RETRIEVED SYNCED!     " << std::endl;
                            std::cout << "========================================================" << std::endl;
                        }
                    }
                }
            }
            else if (inbound_pkt.type == P2P::PacketType::ACK) {
                std::cout << "\n[NETWORK EVENT] Reliable transmission receipt ACK confirmed for Piece #" << inbound_pkt.seq_num << std::endl;
            }
        }

        // Non-blocking keyboard prompt handling loop logic check
#ifdef _WIN32
        if (getkey_check_async('h')) {
            std::cout << "\n[Action] Firing initial Handshake SYN frame to target node..." << std::endl;
            P2P::Packet syn_pkt = handshake_mgr.prepare_handshake_packet();
            socket_mgr.send_data_to(syn_pkt, target_ip, target_port);
            // Small sleep to prevent accidental key-repeat double trigger
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (getkey_check_async('q')) {
            running = false;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    socket_mgr.stop();
    std::cout << "\n[Shutdown Complete] Core network node offline." << std::endl;
    return 0;
}

// Windows Async Key Helper Function Implementation
#ifdef _WIN32
#include <windows.h>
bool getkey_check_async(char key) {
    return (GetAsyncKeyState(static_cast<int>(toupper(key))) & 0x8000) != 0;
}
#else
bool getkey_check_async(char key) { return false; }
#endif