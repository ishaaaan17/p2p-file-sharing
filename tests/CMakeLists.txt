add_executable(p2p_tests
    test_main.cpp
    ../src/packet.cpp
    ../src/piece_manager.cpp
    ../src/peer_session_manager.cpp
    ../src/handshake_manager.cpp
    ../src/sha256.cpp
)
target_include_directories(p2p_tests PRIVATE ../include)

add_test(NAME p2p_tests COMMAND p2p_tests)
