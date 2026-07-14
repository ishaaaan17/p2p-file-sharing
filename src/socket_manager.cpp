#include "socket_manager.h"
#include <iostream>

namespace P2P {

    SocketManager::SocketManager() : m_current_port(0), m_is_running(false) {}

    SocketManager::~SocketManager() {
        stop();
    }

    bool SocketManager::start(uint16_t port) {
        if (m_is_running) {
            std::cout << "[SocketManager] Warning: Node instance is already active." << std::endl;
            return true;
        }

        // 1. Allocate a fresh socket instance utilizing our RAII tracking handle
        m_listening_socket = std::make_unique<ReliableSocket>();

        // 2. Wake up Winsock and build the kernel descriptor structures
        if (!m_listening_socket->initialize()) {
            std::cerr << "[SocketManager] Critical initialization fault triggered in ReliableSocket." << std::endl;
            m_listening_socket.reset();
            return false;
        }

        // 3. Bind to the local network architecture interface port
        if (!m_listening_socket->bind_to_port(port)) {
            std::cerr << "[SocketManager] Critical bind fault. Target port " << port << " might be in use." << std::endl;
            m_listening_socket->close_socket();
            m_listening_socket.reset();
            return false;
        }

        m_current_port = port;
        m_is_running = true;
        std::cout << "[SocketManager] Node successfully operational on network port: " << m_current_port << std::endl;
        return true;
    }

    bool SocketManager::send_data_to(const Packet& packet, const std::string& ip, uint16_t port) {
        if (!m_is_running || !m_listening_socket) {
            std::cerr << "[SocketManager] Error: Cannot dispatch packets while state is inactive." << std::endl;
            return false;
        }

        // Forward the message target down to the Winsock pipeline layer
        return m_listening_socket->send_packet(packet, ip, port);
    }

    bool SocketManager::poll_incoming_traffic(Packet& out_packet, std::string& out_ip, uint16_t& out_port) {
        if (!m_is_running || !m_listening_socket) return false;

        // At this specific phase, this will safely execute a blocking intercept wrapper.
        // In the next phase of architecture, we will integrate non-blocking state polling 
        // using Winsock select() or worker threads so the main execution path remains fluid.
        return m_listening_socket->receive_packet(out_packet, out_ip, out_port);
    }

    void SocketManager::stop() {
        if (!m_is_running) return;

        std::cout << "[SocketManager] Tearing down active socket allocations cleanly..." << std::endl;

        if (m_listening_socket) {
            m_listening_socket->close_socket();

            // Explicitly force unique_ptr to delete the memory resource back to the OS heap
            m_listening_socket.reset();
        }

        m_is_running = false;
        m_current_port = 0;
    }

} // namespace P2P