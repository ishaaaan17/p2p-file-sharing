#pragma once
#include <memory>
#include <vector>
#include <string>
#include "reliable_socket.h"

namespace P2P {

    class SocketManager {
    private:
        std::unique_ptr<ReliableSocket> m_listening_socket;
        uint16_t m_current_port;
        bool m_is_running;

    public:
        SocketManager();
        ~SocketManager();

        // Starts up our centralized network node on a designated port
        bool start(uint16_t port);

        // Dispatches a packet to a specified endpoint
        bool send_data_to(const Packet& packet, const std::string& ip, uint16_t port);

        // Checks for incoming traffic without locking up the application state permanently
        bool poll_incoming_traffic(Packet& out_packet, std::string& out_ip, uint16_t& out_port);

        // Safely terminates all network operational dependencies
        void stop();

        uint16_t get_port() const { return m_current_port; }
        bool is_active() const { return m_is_running; }
    };

} // namespace P2P