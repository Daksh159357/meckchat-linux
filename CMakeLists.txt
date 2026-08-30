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

# Core Library Target (include headers explicitly for AUTOMOC)
add_library(meckchat_core STATIC
    include/meckchat/core/logger.h
    include/meckchat/core/config.h
    include/meckchat/protocol/models.h
    include/meckchat/protocol/framing.h
    include/meckchat/network/mqtt_signaling.h
    include/meckchat/network/p2p_socket.h
    include/meckchat/network/wireguard_service.h
    src/core/logger.cpp
    src/core/config.cpp
    src/protocol/models.cpp
    src/protocol/framing.cpp
    src/network/mqtt_signaling.cpp
    src/network/p2p_socket.cpp
    src/network/wireguard_service.cpp
)

target_include_directories(meckchat_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(meckchat_core PUBLIC
    Qt6::Core
    Qt6::Network
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

install(TARGETS meckchat-linux
    RUNTIME DESTINATION bin
)
