#include <cassert>
#include <iostream>
#include <QCoreApplication>
#include <QTimer>
#include <QHostAddress>

#include "meckchat/network/p2p_socket.h"
#include "meckchat/protocol/framing.h"
#include "meckchat/core/logger.h"

using namespace MeckChat::Network;
using namespace MeckChat::Protocol;

#define TEST_CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "TEST ASSERTION FAILED: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void testStreamDecoderChunking() {
    std::cout << "[RUN] testStreamDecoderChunking" << std::endl;

    P2PFrame frame1;
    frame1.type = FrameType::ChatMessage;
    frame1.payload = QByteArray("{\"msg\":\"Hello Stream Frame 1\"}");
    QByteArray encoded1 = frame1.encode();

    P2PFrame frame2;
    frame2.type = FrameType::Heartbeat;
    frame2.payload = QByteArray("ping");
    QByteArray encoded2 = frame2.encode();

    P2PFrame frame3;
    frame3.type = FrameType::TypingIndicator;
    frame3.payload = QByteArray("{\"typing\":true}");
    QByteArray encoded3 = frame3.encode();

    // 1. Single complete frame
    QByteArray buf = encoded1;
    QString err;
    auto frames = P2PSocket::processStreamBuffer(buf, err);
    TEST_CHECK(err.isEmpty());
    TEST_CHECK(frames.size() == 1);
    TEST_CHECK(frames[0].type == FrameType::ChatMessage);
    TEST_CHECK(frames[0].payload == frame1.payload);
    TEST_CHECK(buf.isEmpty());

    // 2. Header split across multiple reads
    buf.clear();
    buf.append(encoded1.left(3)); // only 3 bytes of 12+N
    frames = P2PSocket::processStreamBuffer(buf, err);
    TEST_CHECK(frames.isEmpty());
    TEST_CHECK(buf.size() == 3);

    buf.append(encoded1.mid(3, 5)); // 5 more bytes (total 8 bytes: complete header, no payload yet)
    frames = P2PSocket::processStreamBuffer(buf, err);
    TEST_CHECK(frames.isEmpty());
    TEST_CHECK(buf.size() == 8);

    buf.append(encoded1.mid(8)); // remaining payload + CRC
    frames = P2PSocket::processStreamBuffer(buf, err);
    TEST_CHECK(frames.size() == 1);
    TEST_CHECK(frames[0].type == FrameType::ChatMessage);
    TEST_CHECK(buf.isEmpty());

    // 3. Multiple frames in single read
    buf = encoded1 + encoded2 + encoded3;
    frames = P2PSocket::processStreamBuffer(buf, err);
    TEST_CHECK(frames.size() == 3);
    TEST_CHECK(frames[0].type == FrameType::ChatMessage);
    TEST_CHECK(frames[1].type == FrameType::Heartbeat);
    TEST_CHECK(frames[2].type == FrameType::TypingIndicator);
    TEST_CHECK(buf.isEmpty());

    // 4. Incomplete trailing frame
    buf = encoded1 + encoded2.left(6);
    frames = P2PSocket::processStreamBuffer(buf, err);
    TEST_CHECK(frames.size() == 1);
    TEST_CHECK(frames[0].type == FrameType::ChatMessage);
    TEST_CHECK(buf.size() == 6); // 6 trailing bytes preserved

    buf.append(encoded2.mid(6));
    frames = P2PSocket::processStreamBuffer(buf, err);
    TEST_CHECK(frames.size() == 1);
    TEST_CHECK(frames[0].type == FrameType::Heartbeat);
    TEST_CHECK(buf.isEmpty());

    std::cout << "[PASS] testStreamDecoderChunking" << std::endl;
}

void testStreamDecoderMaliciousInput() {
    std::cout << "[RUN] testStreamDecoderMaliciousInput" << std::endl;

    QString err;
    QByteArray buf;

    // 1. Invalid Magic
    buf = QByteArray::fromHex("1234") + QByteArray(20, 0x00);
    auto frames = P2PSocket::processStreamBuffer(buf, err);
    TEST_CHECK(frames.isEmpty());
    TEST_CHECK(!err.isEmpty());
    TEST_CHECK(buf.isEmpty());

    // 2. Oversized payload length (10 MB > 4 MB limit)
    buf = QByteArray::fromHex("4D43000200A00000"); // Magic=4D43, Type=0002, Len=10485760 (10MB)
    err.clear();
    frames = P2PSocket::processStreamBuffer(buf, err, 4 * 1024 * 1024);
    TEST_CHECK(frames.isEmpty());
    TEST_CHECK(!err.isEmpty());
    TEST_CHECK(buf.isEmpty());

    // 3. Corrupted CRC
    P2PFrame frame;
    frame.type = FrameType::Heartbeat;
    frame.payload = "data";
    QByteArray encoded = frame.encode();
    encoded[encoded.size() - 1] = static_cast<char>(encoded[encoded.size() - 1] ^ 0xFF); // Corrupt CRC

    buf = encoded;
    err.clear();
    frames = P2PSocket::processStreamBuffer(buf, err);
    TEST_CHECK(frames.isEmpty());
    TEST_CHECK(!err.isEmpty());

    std::cout << "[PASS] testStreamDecoderMaliciousInput" << std::endl;
}

void testTcpServerClientLocalTransmission() {
    std::cout << "[RUN] testTcpServerClientLocalTransmission (Localhost Port 17788)" << std::endl;

    quint16 testPort = 17788;

    P2PSocket server;
    P2PSocket client;

    bool serverAccepted = false;
    bool clientConnected = false;
    bool serverReceivedChatMessage = false;
    bool clientReceivedHeartbeat = false;
    bool serverReceivedLargeChunk = false;

    // 1. Start Server on IPv4 localhost
    bool serverStarted = server.startServer(testPort, QHostAddress("127.0.0.1"));
    TEST_CHECK(serverStarted);
    TEST_CHECK(server.isServerListening());

    // Server signals
    QObject::connect(&server, &P2PSocket::peerConnected, [&](const QString &peerIp) {
        serverAccepted = true;
        std::cout << "  -> Server accepted peer: " << peerIp.toStdString() << std::endl;
    });

    QObject::connect(&server, &P2PSocket::frameReceived, [&](const P2PFrame &frame) {
        if (frame.type == FrameType::ChatMessage) {
            serverReceivedChatMessage = true;
            std::cout << "  -> Server received ChatMessage: " << frame.payload.toStdString() << std::endl;

            // Reply with Heartbeat from server
            P2PFrame reply;
            reply.type = FrameType::Heartbeat;
            reply.payload = "pong";
            // Client will send 64 KB chunk next
        } else if (frame.type == FrameType::FileChunk) {
            if (frame.payload.size() == 65536) {
                serverReceivedLargeChunk = true;
                std::cout << "  -> Server received 64 KB FileChunk verified by CRC!" << std::endl;

                // Test complete! Disconnect and quit
                client.disconnectFromPeer();
                server.stopServer();
                QTimer::singleShot(20, []() {
                    QCoreApplication::quit();
                });
            }
        }
    });

    // Client signals
    QObject::connect(&client, &P2PSocket::connected, [&]() {
        clientConnected = true;
        std::cout << "  -> Client connected successfully!" << std::endl;

        // Send ChatMessage
        P2PFrame chat;
        chat.type = FrameType::ChatMessage;
        chat.payload = "Hello P2P WireGuard Mesh";
        client.sendFrame(chat);

        // Send 64 KB File Chunk
        P2PFrame chunk;
        chunk.type = FrameType::FileChunk;
        chunk.payload = QByteArray(65536, 'X');
        client.sendFrame(chunk);
    });

    QObject::connect(&client, &P2PSocket::frameReceived, [&](const P2PFrame &frame) {
        if (frame.type == FrameType::Heartbeat) {
            clientReceivedHeartbeat = true;
            std::cout << "  -> Client received Heartbeat from server." << std::endl;
        }
    });

    // Timeout safety guard (5s)
    QTimer::singleShot(5000, [&]() {
        if (!serverReceivedLargeChunk) {
            std::cerr << "  -> Local P2P test timed out!" << std::endl;
            QCoreApplication::exit(1);
        }
    });

    // 2. Client Connects once event loop is active
    QTimer::singleShot(50, [&]() {
        client.connectToPeer("127.0.0.1", testPort);
    });

    int ret = QCoreApplication::exec();
    TEST_CHECK(ret == 0);
    TEST_CHECK(serverAccepted);
    TEST_CHECK(clientConnected);
    TEST_CHECK(serverReceivedChatMessage);
    TEST_CHECK(serverReceivedLargeChunk);

    std::cout << "[PASS] testTcpServerClientLocalTransmission" << std::endl;
}

void testSubnetAddressValidation() {
    std::cout << "[RUN] testSubnetAddressValidation" << std::endl;

    TEST_CHECK(P2PSocket::isValidPeerAddress(QHostAddress("10.77.0.2"), true));
    TEST_CHECK(P2PSocket::isValidPeerAddress(QHostAddress("10.77.100.5"), true));

    TEST_CHECK(!P2PSocket::isValidPeerAddress(QHostAddress("192.168.1.1"), true));
    TEST_CHECK(!P2PSocket::isValidPeerAddress(QHostAddress("10.78.0.1"), true));
    TEST_CHECK(!P2PSocket::isValidPeerAddress(QHostAddress("10.77.0.0"), true));
    TEST_CHECK(!P2PSocket::isValidPeerAddress(QHostAddress("10.77.255.255"), true));

    std::cout << "[PASS] testSubnetAddressValidation" << std::endl;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    MeckChat::Core::Logger::init();

    testStreamDecoderChunking();
    testStreamDecoderMaliciousInput();
    testSubnetAddressValidation();
    testTcpServerClientLocalTransmission();

    std::cout << "\n==============================================" << std::endl;
    std::cout << "  All P2P Socket & Framing Tests Passed! " << std::endl;
    std::cout << "==============================================" << std::endl;
    return 0;
}
