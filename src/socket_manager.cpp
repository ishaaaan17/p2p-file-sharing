#include "socket_manager.h"
#include <iostream>
#include <vector>

namespace P2P {

#ifdef _WIN32
    static int wsa_references = 0;
    static std::mutex wsa_mutex;
#endif

    SocketManager::SocketManager() : m_socket(-1), m_is_running(false) {
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(wsa_mutex);
        if (wsa_references == 0) {
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
        }
        wsa_references++;
#endif
    }

    SocketManager::~SocketManager() {
        stop();
#ifdef _WIN32
        std::lock_guard<std::mutex> lock(wsa_mutex);
        wsa_references--;
        if (wsa_references == 0) {
            WSACleanup();
        }
#endif
    }

    bool SocketManager::start(uint16_t port) {
        if (m_is_running) return false;

        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == -1) return false;

        sockaddr_in local_addr{};
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = INADDR_ANY;
        local_addr.sin_port = htons(port);

        if (bind(m_socket, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
            stop();
            return false;
        }

        m_is_running = true;
        m_listener_thread = std::thread(&SocketManager::listen_loop, this);
        std::cout << " -> Socket bound successfully to port: " << port << std::endl;
        return true;
    }

    void SocketManager::stop() {
        m_is_running = false;
        if (m_socket != -1) {
#ifdef _WIN32
            closesocket(m_socket);
#else
            close(m_socket);
#endif
            m_socket = -1;
        }
        if (m_listener_thread.joinable()) {
            m_listener_thread.join();
        }
    }

    bool SocketManager::send_data_to(const Packet& packet, const std::string& target_ip, uint16_t target_port) {
        if (m_socket == -1) return false;

        std::vector<uint8_t> buffer = packet.serialize();
        sockaddr_in target_addr{};
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(target_port);
        inet_pton(AF_INET, target_ip.c_str(), &target_addr.sin_addr);

        int sent_bytes = sendto(m_socket, reinterpret_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
            reinterpret_cast<sockaddr*>(&target_addr), sizeof(target_addr));
        return sent_bytes > 0;
    }

    void SocketManager::listen_loop() {
        std::vector<uint8_t> raw_buffer(2048);
        sockaddr_in remote_addr{};

#ifdef _WIN32
        int addr_len = sizeof(remote_addr);
#else
        socklen_t addr_len = sizeof(remote_addr);
#endif

        std::cout << "[SocketManager] Threaded background receiver running..." << std::endl;

        while (m_is_running) {
            int received_bytes = recvfrom(m_socket, reinterpret_cast<char*>(raw_buffer.data()), static_cast<int>(raw_buffer.size()), 0,
                reinterpret_cast<sockaddr*>(&remote_addr), &addr_len);

            if (received_bytes > 0 && m_is_running) {
                std::vector<uint8_t> active_data(raw_buffer.begin(), raw_buffer.begin() + received_bytes);
                Packet parsed_packet = Packet::deserialize(active_data.data(), active_data.size());

                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &remote_addr.sin_addr, ip_str, INET_ADDRSTRLEN);

                InboundNetworkFrame frame;
                frame.packet = parsed_packet;
                frame.endpoint.ip = std::string(ip_str);
                frame.endpoint.port = ntohs(remote_addr.sin_port);

                std::lock_guard<std::mutex> lock(m_queue_mutex);
                m_frame_queue.push(frame);
            }
        }
    }

    bool SocketManager::pop_incoming_frame(Packet& out_packet, PeerEndpoint& out_endpoint) {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        if (m_frame_queue.empty()) return false;

        InboundNetworkFrame front_frame = m_frame_queue.front();
        out_packet = front_frame.packet;
        out_endpoint = front_frame.endpoint;
        m_frame_queue.pop();
        return true;
    }

} // namespace P2P