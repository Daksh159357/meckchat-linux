cmake_minimum_required(VERSION 3.16)

project(meckchat-linux
    VERSION 0.1.0
    DESCRIPTION "MeckChat Native Linux Application (C++20 / Qt 6)"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# Core Library Target (include headers explicitly for AUTOMOC)
add_library(meckchat_core STATIC
    include/meckchat/core/logger.h
    include/meckchat/core/config.h
    include/meckchat/protocol/models.h
    include/meckchat/protocol/framing.h
    include/meckchat/protocol/chat_controller.h
    include/meckchat/protocol/file_transfer_controller.h
    include/meckchat/crypto/crypto_provider.h
    include/meckchat/crypto/pairing_session.h
    include/meckchat/network/mqtt_signaling.h
    include/meckchat/network/p2p_socket.h
    include/meckchat/network/netlink_wireguard.h
    include/meckchat/network/wireguard_service.h
    src/core/logger.cpp
    src/core/config.cpp
    src/protocol/models.cpp
    src/protocol/framing.cpp
    src/protocol/chat_controller.cpp
    src/protocol/file_transfer_controller.cpp
    src/crypto/crypto_provider.cpp
    src/crypto/pairing_session.cpp
    src/network/mqtt_signaling.cpp
    src/network/p2p_socket.cpp
    src/network/netlink_wireguard.cpp
    src/network/wireguard_service.cpp
)

target_include_directories(meckchat_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(meckchat_core PUBLIC
    Qt6::Core
    Qt6::Network
    OpenSSL::Crypto
)

# Application Executable Target
add_executable(meckchat-linux
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
    src/main.cpp
    resources/resources.qrc
)

target_include_directories(meckchat-linux PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(meckchat-linux PRIVATE
    meckchat_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    OpenSSL::Crypto
)

# CTest Testing Targets
enable_testing()

add_executable(test_protocol tests/test_protocol.cpp)
target_include_directories(test_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_protocol PRIVATE meckchat_core Qt6::Core)
add_test(NAME ProtocolTest COMMAND test_protocol)

add_executable(test_framing tests/test_framing.cpp)
target_include_directories(test_framing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_framing PRIVATE meckchat_core Qt6::Core)
add_test(NAME FramingTest COMMAND test_framing)

add_executable(test_mqtt tests/test_mqtt.cpp)
target_include_directories(test_mqtt PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_mqtt PRIVATE meckchat_core Qt6::Core Qt6::Network)
add_test(NAME MqttSignalingTest COMMAND test_mqtt)

add_executable(test_pairing tests/test_pairing.cpp)
target_include_directories(test_pairing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_pairing PRIVATE meckchat_core Qt6::Core OpenSSL::Crypto)
add_test(NAME PairingTest COMMAND test_pairing)

add_executable(test_wireguard tests/test_wireguard.cpp)
target_include_directories(test_wireguard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_wireguard PRIVATE meckchat_core Qt6::Core OpenSSL::Crypto)
add_test(NAME WireGuardTest COMMAND test_wireguard)

add_executable(test_p2p tests/test_p2p.cpp)
target_include_directories(test_p2p PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_p2p PRIVATE meckchat_core Qt6::Core Qt6::Network OpenSSL::Crypto)
add_test(NAME P2PTest COMMAND test_p2p)

add_executable(test_chat tests/test_chat.cpp)
target_include_directories(test_chat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_chat PRIVATE meckchat_core Qt6::Core Qt6::Network OpenSSL::Crypto)
add_test(NAME ChatTest COMMAND test_chat)

add_executable(test_file_transfer tests/test_file_transfer.cpp)
target_include_directories(test_file_transfer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(test_file_transfer PRIVATE meckchat_core Qt6::Core Qt6::Network OpenSSL::Crypto)
add_test(NAME FileTransferTest COMMAND test_file_transfer)

install(TARGETS meckchat-linux
    RUNTIME DESTINATION bin
)
