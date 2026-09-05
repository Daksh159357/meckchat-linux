# MeckChat Linux

Native Linux desktop application for **MeckChat** — Global Encrypted Peer-to-Peer Communication Platform (C++20 / Qt 6).

---

## 1. Overview & Architecture

MeckChat Linux is a fully native, zero-cloud-data-plane desktop application designed for Linux. It communicates with Android and Windows native clients over the **Universal MeckChat Protocol v1.0**.

The communication pipeline comprises three distinct operational layers:
1. **Signaling & Discovery Layer (Phase 1)**: Native TLS MQTT 3.1.1 (`broker.hivemq.com:8883`) for dynamic peer presence, discovery announcements, and LWT offline detection. Zero message or file payloads ever pass through MQTT.
2. **Cryptographic Pairing & Key Exchange (Phase 2)**: Mutual authenticated key exchange using ephemeral Curve25519 / X25519 key agreements, Argon2id key derivation from user-shared passphrases, HMAC-SHA256 authentication proofs, and 300-second replay protection caches.
3. **Encrypted WireGuard Mesh & P2P Data Channels (Phases 3–6)**:
   - **Kernel WireGuard Layer (Phase 3)**: Linux Generic Netlink interface manager (`netlink_wireguard`) configuring `/32` AllowedIPs within the virtual `10.77.0.0/16` subnet.
   - **Binary Framing Transport (Phase 4)**: Asynchronous TCP socket (`P2PSocket`, Port 7788) utilizing big-endian binary frames with IEEE 802.3 CRC32 verification.
   - **Chat Messaging & ACKs (Phase 5)**: Structured JSON messaging with delivery/read receipts, typing indicators, and duplicate suppression.
   - **P2P File Transfer Protocol (Phase 6)**: 64 KiB chunk streaming, progressive SHA-256 integrity verification, path traversal defenses, cancellation support, and non-blocking Qt GUI file transfers.

---

## 2. Technology Stack

- **Language**: C++20 standard
- **UI Framework**: Qt 6.x (Core, Gui, Widgets, Network)
- **Cryptography**: OpenSSL 3.x (EVP X25519, Argon2id / PBKDF2 fallback, HMAC-SHA256, SHA-256, RAND)
- **Linux Subsystems**: Linux Generic Netlink (`genetlink`, `wireguard`), POSIX Sockets
- **Build System**: CMake 3.16+ (Ninja / Make)
- **Testing**: CTest automated test suite (8 test suites)

---

## 3. Project Structure

```text
meckchat-linux/
├── CMakeLists.txt
├── README.md
├── docs/
│   └── PROTOCOL_SPEC.md
├── include/
│   └── meckchat/
│       ├── core/
│       │   ├── config.h
│       │   └── logger.h
│       ├── crypto/
│       │   ├── crypto_provider.h
│       │   └── pairing_session.h
│       ├── network/
│       │   ├── mqtt_signaling.h
│       │   ├── netlink_wireguard.h
│       │   ├── p2p_socket.h
│       │   └── wireguard_service.h
│       └── protocol/
│           ├── chat_controller.h
│           ├── file_transfer_controller.h
│           ├── framing.h
│           └── models.h
├── src/
│   ├── core/
│   ├── crypto/
│   ├── network/
│   ├── protocol/
│   ├── ui/
│   │   ├── mainwindow.h
│   │   └── mainwindow.cpp
│   └── main.cpp
└── tests/
    ├── test_protocol.cpp
    ├── test_framing.cpp
    ├── test_mqtt.cpp
    ├── test_pairing.cpp
    ├── test_wireguard.cpp
    ├── test_p2p.cpp
    ├── test_chat.cpp
    └── test_file_transfer.cpp
```

---

## 4. Prerequisites & Permissions

### System Dependencies
- Ubuntu/Debian: `sudo apt-get install -y cmake ninja-build g++ qt6-base-dev libssl-dev wireguard-tools`
- Fedora: `sudo dnf install -y cmake ninja-build gcc-c++ qt6-qtbase-devel openssl-devel wireguard-tools`
- Arch Linux: `sudo pacman -S cmake ninja gcc qt6-base openssl wireguard-tools`

### Linux Capabilities
To manage kernel WireGuard network interfaces (`wg0`), the process requires `CAP_NET_ADMIN` privileges or standard root execution:
```bash
sudo setcap cap_net_admin+ep ./build/meckchat-linux
```
*(When executed without `CAP_NET_ADMIN`, unprivileged P2P localhost transport operates normally while kernel interface creation reports blocked privileges).*

---

## 5. Building & Testing

### Compile
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Run CTest Automated Suite
```bash
ctest --test-dir build --output-on-failure -V
```

All 8 test targets:
- `ProtocolTest`: Models serialization, presence, and chat/file framing JSON codecs.
- `FramingTest`: Binary packet layout, magic bytes, length bounds, CRC32 verification.
- `MqttSignalingTest`: Real TLS connection to HiveMQ, subscriptions, LWT, discovery.
- `PairingTest`: X25519 key generation, ECDH agreement, KDF, HMAC proofs, replay cache.
- `WireGuardTest`: Netlink parsing, subnet validation, AllowedIPs, kernel support detection.
- `P2PTest`: Asynchronous TCP stream parsing, chunk reassembly, backpressure.
- `ChatTest`: End-to-end chat message dispatch, delivery ACK, typing notification.
- `FileTransferTest`: 64 KiB streaming, SHA-256 validation, path sanitization, cancellation.

---

## 6. License
MIT License
