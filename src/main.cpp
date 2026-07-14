#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <chrono>
#include <thread>
#include <memory>
#include <map>
#include <fstream>
#include "packet.h"
#include "socket_manager.h"
#include "piece_manager.h"
#include "handshake_manager.h"
#include "peer_session_manager.h"
#include "choke_manager.h"

bool getkey_check_async(char key);

struct SwarmPeerNode {
    P2P::PeerEndpoint endpoint;
    std::unique_ptr<P2P::PeerSessionManager> session;
};

// --- TELEMETRY HELPER TO FEED THE PYTHON DASHBOARD ---
void send_telemetry_to_dashboard(P2P::SocketManager& socket_mgr, const std::string& type, uint16_t port, uint64_t downloaded, uint64_t uploaded, const std::string& status, int32_t piece_idx = -1) {
    std::string json_str;
    if (type == "PEER_UPDATE") {
        json_str = "{\"type\":\"PEER_UPDATE\",\"port\":" + std::to_string(port) +
            ",\"downloaded\":" + std::to_string(downloaded) +
            ",\"uploaded\":" + std::to_string(uploaded) +
            ",\"status\":\"" + status + "\"}";
    }
    else if (type == "PIECE_UPDATE") {
        json_str = "{\"type\":\"PIECE_UPDATE\",\"index\":" + std::to_string(piece_idx) +
            ",\"status\":\"" + status + "\"}";
    }

    // Create a dummy packet payload to carry the text frame over our raw socket manager to localhost port 6000
    P2P::Packet telemetry_pkt;
    telemetry_pkt.type = P2P::PacketType::DATA;
    telemetry_pkt.data_len = static_cast<uint16_t>(json_str.length());
    std::memcpy(telemetry_pkt.payload, json_str.c_str(), json_str.length());

    socket_mgr.send_data_to(telemetry_pkt, "127.0.0.1", 6000);
}

int main() {
    std::cout << "========================================================" << std::endl;
    std::cout << "     FULL SWARM CORE ENGINE WITH PYTHON VISUAL TELEMETRY " << std::endl;
    std::cout << "========================================================\n" << std::endl;

    uint32_t local_peer_id = 0;
    uint16_t local_port = 0;

    std::cout << "Enter Local Peer ID: ";
    std::cin >> local_peer_id;
    std::cout << "Enter Local Port to BIND: ";
    std::cin >> local_port;

    std::vector<uint16_t> swarm_targets;
    int choice_count = 0;
    std::cout << "How many neighbors are you connecting to? ";
    std::cin >> choice_count;
    for (int i = 0; i < choice_count; ++i) {
        uint16_t port = 0;
        std::cout << " -> Enter target neighbor port #" << (i + 1) << ": ";
        std::cin >> port;
        swarm_targets.push_back(port);
    }

    P2P::SocketManager socket_mgr;
    P2P::HandshakeManager handshake_mgr(local_peer_id);
    P2P::PieceManager piece_mgr;
    P2P::ChokeManager choke_mgr;

    std::string filename = "peer_" + std::to_string(local_peer_id) + "_storage.bin";
    piece_mgr.initialize_file_layout(filename, 6000, 1000);

    // Track state metrics locally for telemetry aggregation
    std::map<uint16_t, uint64_t> peer_uploaded_bytes;
    std::map<uint16_t, uint64_t> peer_downloaded_bytes;

    // --- REAL ASSET SERIALIZATION ENGINE ---
    if (local_peer_id == 2002 || local_peer_id == 2003) {
        std::cout << "[Seeding Mode] Loading real file data streams into mesh memory..." << std::endl;

        std::ifstream source_file("source_asset.txt", std::ios::binary);
        uint8_t chunk[1000];

        uint32_t start_piece = (local_peer_id == 2002) ? 0 : 3;
        uint32_t end_piece = (local_peer_id == 2002) ? 3 : 6;

        if (source_file.is_open()) {
            for (uint32_t i = 0; i < 6; ++i) {
                source_file.seekg(i * 1000);
                source_file.read(reinterpret_cast<char*>(chunk), 1000);

                std::streamsize bytes_read = source_file.gcount();
                if (bytes_read < 1000) {
                    std::memset(chunk + bytes_read, ' ', 1000 - bytes_read);
                }

                if (i >= start_piece && i < end_piece) {
                    piece_mgr.write_piece_to_disk(i, chunk, 1000);
                }
            }
            source_file.close();
            std::cout << " -> Successfully parsed 'source_asset.txt' into P2P pieces!" << std::endl;
        }
        else {
            std::cout << " -> [Notice] 'source_asset.txt' not found. Falling back to structured string patterns." << std::endl;
            for (uint32_t i = start_piece; i < end_piece; ++i) {
                std::memset(chunk, 'A' + i, 1000);
                piece_mgr.write_piece_to_disk(i, chunk, 1000);
            }
        }
    }

    if (!socket_mgr.start(local_port)) {
        std::cerr << "CRITICAL: Listening bind error." << std::endl;
        return -1;
    }

    std::map<uint16_t, SwarmPeerNode> active_swarm_nodes;
    std::vector<std::vector<bool>> global_swarm_bitfields;
    bool running = true;

    std::cout << "\n -> Complete Swarm Active! Press 'h' to Handshake Swarm, 't' to exit via FIN, 'q' to quit." << std::endl;

    P2P::Packet inbound_pkt;
    P2P::PeerEndpoint sender_ep;

    while (running) {
        if (socket_mgr.pop_incoming_frame(inbound_pkt, sender_ep)) {
            uint16_t origin_port = sender_ep.port;

            if (inbound_pkt.type == P2P::PacketType::CHOKE) {
                std::cout << "[GAME THEORY] Peer on Port " << origin_port << " has CHOKED us!" << std::endl;
                send_telemetry_to_dashboard(socket_mgr, "PEER_UPDATE", origin_port, peer_downloaded_bytes[origin_port], peer_uploaded_bytes[origin_port], "CHOKED");
                continue;
            }
            else if (inbound_pkt.type == P2P::PacketType::UNCHOKE) {
                std::cout << "[GAME THEORY] Peer on Port " << origin_port << " has UNCHOKED us!" << std::endl;
                send_telemetry_to_dashboard(socket_mgr, "PEER_UPDATE", origin_port, peer_downloaded_bytes[origin_port], peer_uploaded_bytes[origin_port], "UNCHOKED");
            }

            if (inbound_pkt.type == P2P::PacketType::SYN) {
                uint32_t remote_id = 0;
                if (handshake_mgr.process_incoming_handshake(inbound_pkt, remote_id)) {
                    std::cout << "[SWARM EVENT] Accepted valid SYN handshake from Port: " << origin_port << std::endl;
                    P2P::Packet syn_ack = handshake_mgr.prepare_syn_ack_packet(piece_mgr.get_bitfield());
                    socket_mgr.send_data_to(syn_ack, sender_ep.ip, origin_port);

                    send_telemetry_to_dashboard(socket_mgr, "PEER_UPDATE", origin_port, 0, 0, "UNCHOKED");
                }
            }
            else if (inbound_pkt.type == P2P::PacketType::SYN_ACK) {
                std::vector<bool> remote_bitfield;
                if (handshake_mgr.process_incoming_syn_ack(inbound_pkt, remote_bitfield, 6)) {
                    std::cout << "[SWARM EVENT] Decoded bitfield map from neighbor port: " << origin_port << std::endl;

                    SwarmPeerNode node;
                    node.endpoint = sender_ep;
                    node.session = std::make_unique<P2P::PeerSessionManager>(origin_port, remote_bitfield);

                    active_swarm_nodes[origin_port] = std::move(node);

                    global_swarm_bitfields.clear();
                    for (const auto& pair : active_swarm_nodes) {
                        global_swarm_bitfields.push_back(pair.second.session->get_remote_bitfield());
                    }

                    send_telemetry_to_dashboard(socket_mgr, "PEER_UPDATE", origin_port, 0, 0, "UNCHOKED");

                    auto& current_node = active_swarm_nodes[origin_port];
                    int32_t rare_chunk = current_node.session->select_rarest_piece_to_request(piece_mgr, global_swarm_bitfields);
                    if (rare_chunk != -1) {
                        P2P::Packet req = current_node.session->create_piece_request(static_cast<uint32_t>(rare_chunk));
                        socket_mgr.send_data_to(req, sender_ep.ip, origin_port);

                        send_telemetry_to_dashboard(socket_mgr, "PIECE_UPDATE", 0, 0, 0, "Downloading", rare_chunk);
                    }
                }
            }
            else if (inbound_pkt.type == P2P::PacketType::DATA) {
                if (inbound_pkt.data_len == 0 && active_swarm_nodes.count(origin_port)) {
                    if (choke_mgr.should_choke_peer(origin_port)) {
                        if (!choke_mgr.get_choke_status(origin_port)) {
                            P2P::Packet choke_pkt;
                            choke_pkt.type = P2P::PacketType::CHOKE;
                            socket_mgr.send_data_to(choke_pkt, sender_ep.ip, origin_port);
                            choke_mgr.set_choke_status(origin_port, true);
                            send_telemetry_to_dashboard(socket_mgr, "PEER_UPDATE", origin_port, peer_downloaded_bytes[origin_port], peer_uploaded_bytes[origin_port], "CHOKED");
                        }
                        continue;
                    }

                    P2P::Packet resp = active_swarm_nodes[origin_port].session->fulfill_piece_request_from_disk(inbound_pkt, piece_mgr);
                    socket_mgr.send_data_to(resp, sender_ep.ip, origin_port);

                    choke_mgr.record_download(origin_port, resp.data_len);
                    peer_downloaded_bytes[origin_port] += resp.data_len;

                    send_telemetry_to_dashboard(socket_mgr, "PEER_UPDATE", origin_port, peer_downloaded_bytes[origin_port], peer_uploaded_bytes[origin_port], "UNCHOKED");
                }
                else if (inbound_pkt.data_len > 0 && active_swarm_nodes.count(origin_port)) {
                    auto& current_node = active_swarm_nodes[origin_port];
                    if (current_node.session->verify_and_process_incoming_block(inbound_pkt, inbound_pkt.seq_num)) {

                        piece_mgr.write_piece_to_disk(inbound_pkt.seq_num, inbound_pkt.payload, inbound_pkt.data_len);
                        choke_mgr.record_upload(origin_port, inbound_pkt.data_len);
                        peer_uploaded_bytes[origin_port] += inbound_pkt.data_len;

                        std::cout << "[DISK WRITE] Successfully saved Piece #" << inbound_pkt.seq_num << std::endl;

                        send_telemetry_to_dashboard(socket_mgr, "PIECE_UPDATE", 0, 0, 0, "Completed", inbound_pkt.seq_num);
                        send_telemetry_to_dashboard(socket_mgr, "PEER_UPDATE", origin_port, peer_downloaded_bytes[origin_port], peer_uploaded_bytes[origin_port], "UNCHOKED");

                        P2P::Packet ack = current_node.session->create_acknowledgement(inbound_pkt.seq_num);
                        socket_mgr.send_data_to(ack, sender_ep.ip, origin_port);

                        int32_t next_chunk = -1;
                        uint16_t chosen_port = origin_port;

                        for (auto& pair : active_swarm_nodes) {
                            int32_t chunk_idx = pair.second.session->select_rarest_piece_to_request(piece_mgr, global_swarm_bitfields);
                            if (chunk_idx != -1) {
                                next_chunk = chunk_idx;
                                chosen_port = pair.first;
                                break;
                            }
                        }

                        if (next_chunk != -1) {
                            P2P::Packet req = active_swarm_nodes[chosen_port].session->create_piece_request(static_cast<uint32_t>(next_chunk));
                            socket_mgr.send_data_to(req, active_swarm_nodes[chosen_port].endpoint.ip, chosen_port);
                            send_telemetry_to_dashboard(socket_mgr, "PIECE_UPDATE", 0, 0, 0, "Downloading", next_chunk);
                        }
                        else {
                            std::cout << "\n========================================================" << std::endl;
                            std::cout << "   SWARM COMPLETED: ALL PIECES DOCK-SYNCHRONIZED TO DRIVE " << std::endl;
                            std::cout << "========================================================" << std::endl;
                        }
                    }
                }
            }
            else if (inbound_pkt.type == P2P::PacketType::FIN) {
                std::cout << "[SWARM TEARDOWN] Graceful FIN received from Port: " << origin_port << std::endl;
                active_swarm_nodes.erase(origin_port);
            }
        }

#ifdef _WIN32
        if (getkey_check_async('h')) {
            std::cout << "\n[Action] Broadcasting SYN handshakes..." << std::endl;
            for (uint16_t target_port : swarm_targets) {
                P2P::Packet syn_pkt = handshake_mgr.prepare_handshake_packet();
                socket_mgr.send_data_to(syn_pkt, "127.0.0.1", target_port);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (getkey_check_async('t')) {
            std::cout << "\n[Action] Initializing global swarm disconnection..." << std::endl;
            for (auto& pair : active_swarm_nodes) {
                P2P::Packet fin_pkt = pair.second.session->create_teardown_notification();
                socket_mgr.send_data_to(fin_pkt, pair.second.endpoint.ip, pair.first);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (getkey_check_async('q')) {
            running = false;
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    socket_mgr.stop();
    std::cout << "\n[Shutdown] Swarm Engine Offline." << std::endl;
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