cmake_minimum_required(VERSION 3.16)

project(meckchat-linux
    VERSION 0.1.0
    DESCRIPTION "MeckChat Native Linux Application"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(meckchat-linux
    src/main.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
    src/network/wireguard_manager.h
    src/network/wireguard_manager.cpp
    resources/resources.qrc
)

target_include_directories(meckchat-linux PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(meckchat-linux PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

install(TARGETS meckchat-linux
    RUNTIME DESTINATION bin
)
