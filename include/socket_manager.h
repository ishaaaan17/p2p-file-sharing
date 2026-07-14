#pragma once
#include <memory>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include "reliable_socket.h"
#include "packet.h"

namespace P2P {

    // A simple struct to wrap a packet along with its source networking metadata
    struct InboundNetworkFrame {
        Packet packet;
        std::string sender_ip;
        uint16_t sender_port;
    };

    class SocketManager {
    private:
        std::unique_ptr<ReliableSocket> m_listening_socket;
        uint16_t m_current_port;

        // Thread Safety and Asynchronous Mechanics
        std::atomic<bool> m_is_running;
        std::thread m_worker_thread;
        std::mutex m_queue_mutex;

        // Internal storage thread-safe FIFO queue for packets waiting to be processed
        std::queue<InboundNetworkFrame> m_inbound_queue;

        // The background loop executed by our worker thread
        void network_worker_loop();

    public:
        SocketManager();
        ~SocketManager();

        // Starts up the centralized network node and spawns the background worker thread
        bool start(uint16_t port);

        // Dispatches a packet to a specified endpoint asynchronously
        bool send_data_to(const Packet& packet, const std::string& ip, uint16_t port);

        // Checks our thread-safe queue for incoming traffic without blocking the main execution path
        bool pop_incoming_frame(Packet& out_packet, std::string& out_ip, uint16_t& out_port);

        // Safely stops the background worker thread and terminates handles cleanly
        void stop();

        uint16_t get_port() const { return m_current_port; }
        bool is_active() const { return m_is_running.load(); }
    };

} // namespace P2P