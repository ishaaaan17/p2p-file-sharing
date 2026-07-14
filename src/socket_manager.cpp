#include "socket_manager.h"
#include <iostream>

namespace P2P {

    SocketManager::SocketManager() : m_current_port(0), m_is_running(false) {}

    SocketManager::~SocketManager() {
        stop();
    }

    bool SocketManager::start(uint16_t port) {
        if (m_is_running.load()) {
            std::cout << "[SocketManager] Warning: Background thread is already active." << std::endl;
            return true;
        }

        m_listening_socket = std::make_unique<ReliableSocket>();

        if (!m_listening_socket->initialize()) {
            std::cerr << "[SocketManager] Critical initialization fault." << std::endl;
            m_listening_socket.reset();
            return false;
        }

        if (!m_listening_socket->bind_to_port(port)) {
            std::cerr << "[SocketManager] Critical bind fault on port: " << port << std::endl;
            m_listening_socket->close_socket();
            m_listening_socket.reset();
            return false;
        }

        m_current_port = port;
        m_is_running.store(true);

        // Spawn our dedicated worker thread to handle input loops in the background
        m_worker_thread = std::thread(&SocketManager::network_worker_loop, this);

        std::cout << "[SocketManager] Threaded background receiver running on port: " << m_current_port << std::endl;
        return true;
    }

    bool SocketManager::send_data_to(const Packet& packet, const std::string& ip, uint16_t port) {
        if (!m_is_running.load() || !m_listening_socket) {
            std::cerr << "[SocketManager] Error: Cannot transmit while thread is dead." << std::endl;
            return false;
        }
        return m_listening_socket->send_packet(packet, ip, port);
    }

    // High Performance Non-Blocking Interface: Instantly returns true if a frame exists
    bool SocketManager::pop_incoming_frame(Packet& out_packet, std::string& out_ip, uint16_t& out_port) {
        std::unique_lock<std::mutex> lock(m_queue_mutex); // Lock access to the storage queue

        if (m_inbound_queue.empty()) {
            return false; // Instant fallback - does not block the main application loop!
        }

        // Pull the front item from our thread-safe buffer
        InboundNetworkFrame frame = m_inbound_queue.front();
        m_inbound_queue.pop();

        out_packet = frame.packet;
        out_ip = frame.sender_ip;
        out_port = frame.sender_port;

        return true;
    }

    void SocketManager::network_worker_loop() {
        while (m_is_running.load()) {
            Packet captured_packet;
            std::string source_ip;
            uint16_t source_port = 0;

            // Wait on the low-level Winsock block (this blocks the worker thread, NOT the main app)
            if (m_listening_socket->receive_packet(captured_packet, source_ip, source_port)) {

                // Critical Section: Acquire lock before modifying shared memory space
                {
                    std::unique_lock<std::mutex> lock(m_queue_mutex);
                    m_inbound_queue.push({ captured_packet, source_ip, source_port });
                }
            }
        }
    }

    void SocketManager::stop() {
        if (!m_is_running.load()) return;

        std::cout << "[SocketManager] Signalling background thread shutdown sequence..." << std::endl;

        // 1. Flip our atomic execution state flag
        m_is_running.store(false);

        // 2. Tear down the underlying Winsock descriptor block.
        // This instantly causes any blocking receive_packet() loop on the worker thread to fail,
        // preventing the worker thread from getting stuck forever.
        if (m_listening_socket) {
            m_listening_socket->close_socket();
        }

        // 3. Gracefully wait for the OS to spin down the background thread safely
        if (m_worker_thread.joinable()) {
            m_worker_thread.join();
        }

        // 4. Free remaining heap resources safely
        if (m_listening_socket) {
            m_listening_socket.reset();
        }

        // Clear out any residual packet junk left in the memory queue
        std::unique_lock<std::mutex> lock(m_queue_mutex);
        std::queue<InboundNetworkFrame> empty_queue;
        std::swap(m_inbound_queue, empty_queue);

        std::cout << "[SocketManager] Thread context recycled. Subsystem offline." << std::endl;
    }

} // namespace P2P