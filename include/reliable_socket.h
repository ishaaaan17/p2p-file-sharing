#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include "packet.h"

// Tell the MSVC Linker to explicitly bind the Windows Socket binary library file
#pragma comment(lib, "ws2_32.lib")

namespace P2P {

    class ReliableSocket {
    private:
        SOCKET m_socket; // Native Windows Socket handle
        bool m_is_bound;

    public:
        ReliableSocket();
        ~ReliableSocket();

        // Initializes Winsock subsystem and creates a raw UDP socket
        bool initialize();

        // Binds the socket to a specific local port (Used by servers or receivers)
        bool bind_to_port(uint16_t port);

        // Sends a custom protocol packet to a target IP and Port
        bool send_packet(const Packet& packet, const std::string& target_ip, uint16_t target_port);

        // Receives an incoming packet and fills out the sender's IP and Port details
        bool receive_packet(Packet& out_packet, std::string& out_sender_ip, uint16_t& out_sender_port);

        // Closes the active socket cleanly
        void close_socket();
    };

} // namespace P2P