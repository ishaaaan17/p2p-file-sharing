#include "reliable_socket.h"
#include <iostream>
#include <stdexcept>

namespace P2P {

    // Constructor sets our native windows socket handler to an invalid state safely
    ReliableSocket::ReliableSocket() : m_socket(INVALID_SOCKET), m_is_bound(false) {}

    ReliableSocket::~ReliableSocket() {
        close_socket();
    }

    bool ReliableSocket::initialize() {
        // 1. Initialize the Winsock Subsystem DLL (WSAStartup)
        WSADATA wsaData;
        // Request Winsock version 2.2 explicitly
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            std::cerr << "[Winsock Error] WSAStartup failed. Error Code: " << result << std::endl;
            return false;
        }

        // 2. Open a native Windows socket descriptor
        // AF_INET = IPv4 Address Family
        // SOCK_DGRAM = Datagram service (UDP protocol)
        // IPPROTO_UDP = Explicitly bind socket behavior to UDP standards
        m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (m_socket == INVALID_SOCKET) {
            std::cerr << "[Winsock Error] Socket creation failed. Error Code: " << WSAGetLastError() << std::endl;
            WSACleanup();
            return false;
        }

        return true;
    }

    bool ReliableSocket::bind_to_port(uint16_t port) {
        if (m_socket == INVALID_SOCKET) return false;

        sockaddr_in local_addr{};
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(port);          // Convert host byte order to Network Byte Order (Big-Endian)
        local_addr.sin_addr.s_addr = INADDR_ANY;    // Bind to any network interface card active on this machine

        int result = bind(m_socket, reinterpret_cast<sockaddr*>(&local_addr), sizeof(local_addr));
        if (result == SOCKET_ERROR) {
            std::cerr << "[Winsock Error] Bind failed on port " << port << ". Error Code: " << WSAGetLastError() << std::endl;
            return false;
        }

        m_is_bound = true;
        std::cout << " -> Socket bound successfully to port: " << port << std::endl;
        return true;
    }

    bool ReliableSocket::send_packet(const Packet& packet, const std::string& target_ip, uint16_t target_port) {
        if (m_socket == INVALID_SOCKET) return false;

        // Flatten our structural C++ packet data into a raw sequential wire array
        std::vector<uint8_t> buffer = packet.serialize();

        // Configure the address destination structural envelope
        sockaddr_in target_addr{};
        target_addr.sin_family = AF_INET;
        target_addr.sin_port = htons(target_port);

        // Convert string representation of IP ("127.0.0.1") to raw network binary format
        if (inet_pton(AF_INET, target_ip.c_str(), &target_addr.sin_addr) <= 0) {
            std::cerr << "[Winsock Error] Invalid target IP address formatting string." << std::endl;
            return false;
        }

        // Pass the serialized payload onto the native network adapter queue
        int bytes_sent = sendto(m_socket, reinterpret_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()),
            0, reinterpret_cast<const sockaddr*>(&target_addr), sizeof(target_addr));

        if (bytes_sent == SOCKET_ERROR) {
            std::cerr << "[Winsock Error] Packet transmission failed. Error Code: " << WSAGetLastError() << std::endl;
            return false;
        }

        return true;
    }

    bool ReliableSocket::receive_packet(Packet& out_packet, std::string& out_sender_ip, uint16_t& out_sender_port) {
        if (m_socket == INVALID_SOCKET) return false;

        // Allocate a buffer massive enough to handle our absolute maximum theoretical packet ceiling
        uint8_t incoming_buffer[HEADER_SIZE + MAX_PAYLOAD_SIZE];

        sockaddr_in sender_addr{};
        int sender_addr_len = sizeof(sender_addr);

        // Blocking call: Waits indefinitely until a UDP frame lands on our bound port
        int bytes_received = recvfrom(m_socket, reinterpret_cast<char*>(incoming_buffer), sizeof(incoming_buffer),
            0, reinterpret_cast<sockaddr*>(&sender_addr), &sender_addr_len);

        if (bytes_received == SOCKET_ERROR) {
            std::cerr << "[Winsock Error] Packet capture failed. Error Code: " << WSAGetLastError() << std::endl;
            return false;
        }

        // Unpack raw incoming wire buffer into our structured C++ format out_packet object
        try {
            out_packet = Packet::deserialize(incoming_buffer, static_cast<size_t>(bytes_received));
        }
        catch (const std::exception& ex) {
            std::cerr << "[Protocol Mismatch Error] Dropping malformed incoming packet frame: " << ex.what() << std::endl;
            return false;
        }

        // Extract and populate sender metadata back to the manager layer
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        out_sender_ip = std::string(ip_str);
        out_sender_port = ntohs(sender_addr.sin_port); // Convert Big-Endian back to native computer layout

        return true;
    }

    void ReliableSocket::close_socket() {
        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }
        if (m_is_bound) {
            // Balance the lifecycle by shutting down the active subsystem allocations
            WSACleanup();
            m_is_bound = false;
        }
    }

} // namespace P2P