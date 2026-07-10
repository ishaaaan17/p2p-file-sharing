cmake_minimum_required(VERSION 3.20)
project(P2PFileSharing LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

# Binding all structural implementations together
add_executable(p2p_app 
    src/main.cpp 
    src/packet.cpp
    src/reliable_socket.cpp
)