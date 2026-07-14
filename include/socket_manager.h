#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <cstdint>
#include "packet.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace P2P {

    // A structural representation of a remote network endpoint
    struct PeerEndpoint {
        std::string ip;
        uint16_t port;

        bool operator==(const PeerEndpoint& other) const {
            return ip == other.ip && port == other.port;
        }
    };

    // A combined container that moves across our multi-threaded queue pipeline
    struct InboundNetworkFrame {
        Packet packet;
        PeerEndpoint endpoint;
    };

    class SocketManager {
    private:
#ifdef _WIN32
        SOCKET m_socket;
#else
        int m_socket;
#endif
        std::atomic<bool> m_is_running;
        std::thread m_listener_thread;

        // Thread-safe pipeline components updated for multi-peer frames
        std::queue<InboundNetworkFrame> m_frame_queue;
        std::mutex m_queue_mutex;

        void listen_loop();

    public:
        SocketManager();
        ~SocketManager();

        bool start(uint16_t port);
        void stop();

        // Dispatches a packet out to a explicit remote network address
        bool send_data_to(const Packet& packet, const std::string& target_ip, uint16_t target_port);

        // Pops a tracking frame containing the data packet and the endpoint that sent it
        bool pop_incoming_frame(Packet& out_packet, PeerEndpoint& out_endpoint);
    };

} // namespace P2P