#include <iostream>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include "packet.h"
#include "socket_manager.h"

int main() {
    std::cout << "[P2P Concurrency Engine] Starting Asynchronous Thread Verification..." << std::endl;

    // 1. Initialize the multi-threaded manager on local port 8888
    P2P::SocketManager manager;
    uint16_t port = 8888;
    if (!manager.start(port)) {
        std::cerr << "CRITICAL: Failed to launch threaded Socket Manager node." << std::endl;
        return -1;
    }

    // 2. Construct our test transmission frame
    P2P::Packet outbound_pkt;
    outbound_pkt.type = P2P::PacketType::DATA;
    outbound_pkt.seq_num = 99; // Unique Identifier

    const char* thread_msg = "Multi-Threaded Asynchronous Queue Pipeline: Verified Active.";
    outbound_pkt.data_len = static_cast<uint16_t>(std::strlen(thread_msg));
    std::memcpy(outbound_pkt.payload, thread_msg, outbound_pkt.data_len);
    outbound_pkt.checksum = outbound_pkt.calculate_checksum();

    // 3. Dispatch the transmission packet to the loopback interface
    std::string loopback_ip = "127.0.0.1";
    std::cout << " -> Main thread firing packet down to OS network queue..." << std::endl;
    if (!manager.send_data_to(outbound_pkt, loopback_ip, port)) {
        std::cerr << "Manager asynchronous dispatch failure!" << std::endl;
        return -1;
    }

    // 4. Run a live non-blocking polling loop on the main execution thread
    std::cout << " -> Main thread entering fluid monitoring loop..." << std::endl;
    P2P::Packet inbound_pkt;
    std::string sender_ip;
    uint16_t sender_port = 0;

    bool frame_captured = false;
    int loop_iterations = 0;

    // We'll spin for a maximum of 5 iterations, sleeping 20ms each time.
    // This proves the main thread is NOT stuck blocking on network input!
    while (loop_iterations < 5) {
        loop_iterations++;
        std::cout << "    [Main Thread Loop #" << loop_iterations << "] Processing application cycles..." << std::endl;

        // Non-blocking query check down into the manager's locked FIFO queue
        if (manager.pop_incoming_frame(inbound_pkt, sender_ip, sender_port)) {
            frame_captured = true;
            break; // Frame successfully extracted from background queue!
        }

        // Simulating other application work (e.g., rendering, processing files)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // 5. Evaluate execution results
    if (frame_captured) {
        std::cout << "\n[ASYNC SUCCESS] Packet captured cleanly via Background Thread Worker!" << std::endl;
        std::cout << "   Origin Network End: " << sender_ip << ":" << sender_port << std::endl;
        std::cout << "   Extracted Seq ID:   " << inbound_pkt.seq_num << std::endl;

        char clean_string[P2P::MAX_PAYLOAD_SIZE + 1] = { 0 };
        std::memcpy(clean_string, inbound_pkt.payload, inbound_pkt.data_len);
        std::cout << "   Payload String:     \"" << clean_string << "\"" << std::endl;
    }
    else {
        std::cout << "\n[ASYNC TIMEOUT ERROR] Background thread failed to capture packet." << std::endl;
    }

    // 6. Gracefully shut down the background thread context
    manager.stop();
    std::cout << "\n[Engine Test Closed] Asynchronous subsystem elements cleanly recycled." << std::endl;

    return 0;
}