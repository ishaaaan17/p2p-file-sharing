#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include <memory>
#include "packet.h"
#include "socket_manager.h"
#include "piece_manager.h"
#include "handshake_manager.h"
#include "peer_session_manager.h"

bool getkey_check_async(char key);

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "    PRODUCTION P2P COOPERATIVE DISK EXCHANGE ENGINE    " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    uint32_t local_peer_id = 0;
    uint16_t local_port = 0;
    uint16_t target_port = 0;
    std::string target_ip = "127.0.0.1";

    std::cout << "Enter Local Peer ID: ";
    std::cin >> local_peer_id;
    std::cout << "Enter Local Port to BIND: ";
    std::cin >> local_port;
    std::cout << "Enter Target Port to CONNECT: ";
    std::cin >> target_port;

    P2P::SocketManager socket_mgr;
    P2P::HandshakeManager handshake_mgr(local_peer_id);
    P2P::PieceManager piece_mgr;

    // Allocate physical binary workspace file on hard drive layout
    std::string targeted_filename = "peer_" + std::to_string(local_peer_id) + "_storage.bin";
    piece_mgr.initialize_file_layout(targeted_filename, 6000, 1000);

    // Seeder configuration node simulation parameters logic
    if (local_peer_id == 2002) {
        std::cout << "[Seeding Initializer] Loading signature data to binary file blocks..." << std::endl;
        uint8_t dummy_payload[1000];
        for (uint32_t i = 0; i < 6; ++i) {
            std::memset(dummy_payload, 'A' + i, 1000); // Unique letters per block
            piece_mgr.write_piece_to_disk(i, dummy_payload, 1000);
        }
    }

    if (!socket_mgr.start(local_port)) {
        std::cerr << "CRITICAL: Listening bind error." << std::endl;
        return -1;
    }

    std::unique_ptr<P2P::PeerSessionManager> live_session = nullptr;
    std::vector<std::vector<bool>> global_swarm_bitfields;
    bool running = true;

    std::cout << "\n -> System Live on disk storage! Press 'h' to Handshake, 't' to Teardown (FIN), 'q' to quit." << std::endl;

    P2P::Packet inbound_pkt;
    std::string sender_ip;
    uint16_t sender_port = 0;

    while (running) {
        if (socket_mgr.pop_incoming_frame(inbound_pkt, sender_ip, sender_port)) {

            if (inbound_pkt.type == P2P::PacketType::SYN) {
                uint32_t remote_id = 0;
                if (handshake_mgr.process_incoming_handshake(inbound_pkt, remote_id)) {
                    std::cout << "\n[HANDSHAKE] Valid SYN received from Peer #" << remote_id << std::endl;
                    P2P::Packet syn_ack = handshake_mgr.prepare_syn_ack_packet(piece_mgr.get_bitfield());
                    socket_mgr.send_data_to(syn_ack, sender_ip, sender_port);
                }
            }
            else if (inbound_pkt.type == P2P::PacketType::SYN_ACK) {
                std::vector<bool> remote_bitfield;
                if (handshake_mgr.process_incoming_syn_ack(inbound_pkt, remote_bitfield, 6)) {
                    std::cout << "\n[HANDSHAKE] SYN_ACK verified! Establishing tracking vectors..." << std::endl;

                    live_session = std::make_unique<P2P::PeerSessionManager>(local_peer_id, remote_bitfield);

                    // Register bitfield map into our swarm analyzer engine tracking matrices
                    global_swarm_bitfields.clear();
                    global_swarm_bitfields.push_back(remote_bitfield);

                    // Execute advanced Rarest-First calculation choice logic routines
                    int32_t target_chunk = live_session->select_rarest_piece_to_request(piece_mgr, global_swarm_bitfields);
                    if (target_chunk != -1) {
                        P2P::Packet req = live_session->create_piece_request(static_cast<uint32_t>(target_chunk));
                        socket_mgr.send_data_to(req, target_ip, target_port);
                        std::cout << " -> [Rarest-First] Requesting Piece #" << target_chunk << " from storage." << std::endl;
                    }
                }
            }
            else if (inbound_pkt.type == P2P::PacketType::DATA) {
                if (inbound_pkt.data_len == 0 && live_session) {
                    // Pull block directly from storage disk out to the wire pipeline frame
                    P2P::Packet response = live_session->fulfill_piece_request_from_disk(inbound_pkt, piece_mgr);
                    socket_mgr.send_data_to(response, sender_ip, sender_port);
                    std::cout << " -> [Disk Read Out] Dispatched Piece #" << inbound_pkt.seq_num << " payload over UDP." << std::endl;
                }
                else if (inbound_pkt.data_len > 0 && live_session) {
                    std::cout << "\n[NETWORK EVENT] Inbound block landed for Piece #" << inbound_pkt.seq_num << std::endl;
                    if (live_session->verify_and_process_incoming_block(inbound_pkt, inbound_pkt.seq_num)) {

                        // Production Phase: Flash payload bytes straight to sectors on physical storage disk
                        piece_mgr.write_piece_to_disk(inbound_pkt.seq_num, inbound_pkt.payload, inbound_pkt.data_len);
                        std::cout << " -> [Disk Write Success] Piece #" << inbound_pkt.seq_num << " safely committed to storage." << std::endl;

                        P2P::Packet ack = live_session->create_acknowledgement(inbound_pkt.seq_num);
                        socket_mgr.send_data_to(ack, sender_ip, sender_port);

                        int32_t next_chunk = live_session->select_rarest_piece_to_request(piece_mgr, global_swarm_bitfields);
                        if (next_chunk != -1) {
                            P2P::Packet req = live_session->create_piece_request(static_cast<uint32_t>(next_chunk));
                            socket_mgr.send_data_to(req, target_ip, target_port);
                        }
                        else {
                            std::cout << "\n========================================================" << std::endl;
                            std::cout << "    SUCCESS: ALL STORAGE CHUNKS SYNCHRONIZED TO DRIVE   " << std::endl;
                            std::cout << "========================================================" << std::endl;
                        }
                    }
                }
            }
            else if (inbound_pkt.type == P2P::PacketType::ACK) {
                std::cout << " -> [Receipt Confirmed] Peer acknowledged receipt of piece #" << inbound_pkt.seq_num << std::endl;
            }
            else if (inbound_pkt.type == P2P::PacketType::FIN) {
                std::cout << "\n[TEARDOWN STATE] Received grace FIN from remote neighbor node. Cleaning allocations..." << std::endl;
                live_session.reset();
                std::cout << " -> Session state destroyed smoothly. Safe to exit." << std::endl;
            }
        }

#ifdef _WIN32
        if (getkey_check_async('h')) {
            std::cout << "\n[Action] Firing initial Handshake SYN frame..." << std::endl;
            P2P::Packet syn_pkt = handshake_mgr.prepare_handshake_packet();
            socket_mgr.send_data_to(syn_pkt, target_ip, target_port);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (getkey_check_async('t') && live_session) {
            std::cout << "\n[Action] Initializing symmetrical session teardown close sequence..." << std::endl;
            P2P::Packet fin_pkt = live_session->create_teardown_notification();
            socket_mgr.send_data_to(fin_pkt, target_ip, target_port);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (getkey_check_async('q')) {
            running = false;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    socket_mgr.stop();
    std::cout << "\n[Shutdown] System Offline safely." << std::endl;
    return 0;
}

#ifdef _WIN32
#include <windows.h>
bool getkey_check_async(char key) {
    return (GetAsyncKeyState(static_cast<int>(toupper(key))) & 0x8000) != 0;
}
#else
bool getkey_check_async(char key) { return false; }
#endif