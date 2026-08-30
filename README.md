# MeckChat Linux

Native Linux desktop application for **MeckChat** — Global Encrypted Peer-to-Peer Communication Platform.

## Overview

MeckChat Linux is a fully native Linux application written in modern C++20 and Qt 6. It interfaces directly with Linux kernel networking subsystems and userspace WireGuard tooling to provide high-performance, secure P2P communication.

## Technology Stack

- **Language**: C++20
- **UI Framework**: Qt 6 (Widgets / Core / Gui / Network)
- **Build System**: Modern CMake (3.16+)
- **Target OS**: Linux (Ubuntu, Debian, Fedora, Arch, etc.)
- **Networking**: Linux Socket APIs, Netlink, and WireGuard kernel/userspace interfaces
- **Architecture**: Modular Qt Model-View-Controller with asynchronous event loops

## Repository Ecosystem

MeckChat consists of three dedicated native platform clients designed to operate over the unified MeckChat Protocol:

| Platform | Repository | Core Stack |
| :--- | :--- | :--- |
| **Android** | [meckchat-android](https://github.com/Daksh159357/meckchat-android) | Kotlin + Jetpack Compose |
| **Linux** | [meckchat-linux](https://github.com/Daksh159357/meckchat-linux) | C++20 + Qt 6 |
| **Windows** | [meckchat-windows](https://github.com/Daksh159357/meckchat-windows) | C# + WinUI 3 / .NET 8 |

## Protocol Compatibility

All three clients implement the shared MeckChat specification:
- Device Identity & Cryptographic Keys
- Presence Discovery & Pairing Handshake
- WireGuard Public Key & Virtual IP Exchange
- End-to-End Encrypted Messaging & P2P Data Channels

## Project Structure

```
meckchat-linux/
├── CMakeLists.txt
├── README.md
├── .gitignore
├── resources/
│   └── resources.qrc
└── src/
    ├── main.cpp
    ├── ui/
    │   ├── mainwindow.h
    │   └── mainwindow.cpp
    └── network/
        ├── wireguard_manager.h
        └── wireguard_manager.cpp
```

## Getting Started

### Prerequisites
- GCC 11+ or Clang 13+ with C++20 support
- CMake 3.16+
- Qt 6 (qt6-base-dev)
- pkg-config

### Building
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Running
```bash
./build/meckchat-linux
```

## License
MIT License
