cmake_minimum_required(VERSION 3.20)
project(P2PFileSharing LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Binding all structural implementations together into our output target binary.
# NOTE: ReliableSocket (reliable_socket.h/.cpp) was removed — it was dead code,
# never instantiated anywhere; SocketManager superseded it, and its unconditional
# winsock2.h include was the single file breaking the Linux build.
add_executable(p2p_app
    src/main.cpp
    src/packet.cpp
    src/socket_manager.cpp
    src/piece_manager.cpp
    src/handshake_manager.cpp
    src/peer_session_manager.cpp
)

if(WIN32)
    # Winsock lives in ws2_32; link it explicitly instead of relying on the
    # MSVC-only #pragma comment(lib) inside the headers.
    target_link_libraries(p2p_app PRIVATE ws2_32)
endif()
