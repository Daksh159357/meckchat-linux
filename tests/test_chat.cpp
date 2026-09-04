#include <iostream>
#include <cassert>
#include <QCoreApplication>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHostAddress>

#include "meckchat/protocol/models.h"
#include "meckchat/protocol/framing.h"
#include "meckchat/protocol/chat_controller.h"
#include "meckchat/network/p2p_socket.h"
#include "meckchat/core/logger.h"
#include "meckchat/core/config.h"

using namespace MeckChat::Protocol;
using namespace MeckChat::Network;

#define TEST_CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "TEST ASSERTION FAILED: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void testChatMessageSerialization() {
    std::cout << "[RUN] testChatMessageSerialization" << std::endl;

    // 1. Valid roundtrip with Unicode & special characters
    ChatMessage msg;
    msg.messageId = "msg_12345678-abcd-ef01-2345-6789abcdef01";
    msg.senderDeviceId = "mc_alice_linux";
    msg.recipientDeviceId = "mc_bob_windows";
    msg.content = "Hello 🚀 from Linux C++20 Qt6! Special characters: \"quotes\", \\slashes\\, \nnewlines, and 🐧 emojis.";
    msg.timestamp = 1725000000;
    msg.replyToMessageId = "msg_previous_001";

    QJsonObject json = msg.toJson();
    auto decodedOpt = ChatMessage::fromJson(json);
    TEST_CHECK(decodedOpt.has_value());
    TEST_CHECK(decodedOpt->messageId == msg.messageId);
    TEST_CHECK(decodedOpt->senderDeviceId == msg.senderDeviceId);
    TEST_CHECK(decodedOpt->recipientDeviceId == msg.recipientDeviceId);
    TEST_CHECK(decodedOpt->content == msg.content);
    TEST_CHECK(decodedOpt->timestamp == msg.timestamp);
    TEST_CHECK(decodedOpt->replyToMessageId == msg.replyToMessageId);

    // 2. Missing required fields
    QJsonObject invalidJson = json;
    invalidJson.remove("message_id");
    TEST_CHECK(!ChatMessage::fromJson(invalidJson).has_value());

    invalidJson = json;
    invalidJson.remove("sender_device_id");
    TEST_CHECK(!ChatMessage::fromJson(invalidJson).has_value());

    invalidJson = json;
    invalidJson.remove("recipient_device_id");
    TEST_CHECK(!ChatMessage::fromJson(invalidJson).has_value());

    // 3. Oversized content rejection (> 64 KiB)
    QString hugeContent(70000, 'A');
    QJsonObject hugeJson = json;
    hugeJson["content"] = hugeContent;
    TEST_CHECK(!ChatMessage::fromJson(hugeJson).has_value());

    std::cout << "[PASS] testChatMessageSerialization" << std::endl;
}

void testMessageAckSerialization() {
    std::cout << "[RUN] testMessageAckSerialization" << std::endl;

    // 1. Delivered ACK
    MessageAck ackDelivered;
    ackDelivered.messageId = "msg_test_ack_001";
    ackDelivered.status = "delivered";
    ackDelivered.timestamp = 1725000005;

    QJsonObject json1 = ackDelivered.toJson();
    auto decoded1 = MessageAck::fromJson(json1);
    TEST_CHECK(decoded1.has_value());
    TEST_CHECK(decoded1->messageId == ackDelivered.messageId);
    TEST_CHECK(decoded1->status == "delivered");
    TEST_CHECK(decoded1->timestamp == 1725000005);

    // 2. Read Receipt ACK
    MessageAck ackRead;
    ackRead.messageId = "msg_test_ack_002";
    ackRead.status = "read";
    ackRead.timestamp = 1725000010;

    QJsonObject json2 = ackRead.toJson();
    auto decoded2 = MessageAck::fromJson(json2);
    TEST_CHECK(decoded2.has_value());
    TEST_CHECK(decoded2->messageId == ackRead.messageId);
    TEST_CHECK(decoded2->status == "read");

    // 3. Missing message_id
    QJsonObject invalidJson;
    invalidJson["status"] = "delivered";
    TEST_CHECK(!MessageAck::fromJson(invalidJson).has_value());

    std::cout << "[PASS] testMessageAckSerialization" << std::endl;
}

void testTypingIndicatorSerialization() {
    std::cout << "[RUN] testTypingIndicatorSerialization" << std::endl;

    // 1. Typing true
    TypingIndicator typingTrue;
    typingTrue.senderDeviceId = "mc_alice";
    typingTrue.recipientDeviceId = "mc_bob";
    typingTrue.isTyping = true;
    typingTrue.timestamp = 1725000020;

    QJsonObject json1 = typingTrue.toJson();
    auto decoded1 = TypingIndicator::fromJson(json1);
    TEST_CHECK(decoded1.has_value());
    TEST_CHECK(decoded1->senderDeviceId == typingTrue.senderDeviceId);
    TEST_CHECK(decoded1->recipientDeviceId == typingTrue.recipientDeviceId);
    TEST_CHECK(decoded1->isTyping == true);
    TEST_CHECK(decoded1->timestamp == 1725000020);

    // 2. Typing false
    TypingIndicator typingFalse;
    typingFalse.senderDeviceId = "mc_alice";
    typingFalse.recipientDeviceId = "mc_bob";
    typingFalse.isTyping = false;
    typingFalse.timestamp = 1725000025;

    QJsonObject json2 = typingFalse.toJson();
    auto decoded2 = TypingIndicator::fromJson(json2);
    TEST_CHECK(decoded2.has_value());
    TEST_CHECK(decoded2->isTyping == false);

    // 3. Missing sender
    QJsonObject invalidJson;
    invalidJson["recipient_device_id"] = "mc_bob";
    invalidJson["is_typing"] = true;
    TEST_CHECK(!TypingIndicator::fromJson(invalidJson).has_value());

    std::cout << "[PASS] testTypingIndicatorSerialization" << std::endl;
}

void testMessageIdGeneration() {
    std::cout << "[RUN] testMessageIdGeneration" << std::endl;

    QString id1 = generateMessageId();
    QString id2 = generateMessageId();

    TEST_CHECK(id1.startsWith("msg_"));
    TEST_CHECK(id2.startsWith("msg_"));
    TEST_CHECK(id1 != id2);
    TEST_CHECK(id1.length() > 20);

    std::cout << "[PASS] testMessageIdGeneration" << std::endl;
}

void testEndToEndP2PChatTransmission() {
    std::cout << "[RUN] testEndToEndP2PChatTransmission (Localhost Port 17789)" << std::endl;

    quint16 testPort = 17789;

    P2PSocket serverSocket;
    P2PSocket clientSocket;

    ChatController serverChat(&serverSocket);
    ChatController clientChat(&clientSocket);

    bool serverReceivedTypingTrue = false;
    bool serverReceivedChatMessage = false;
    bool clientReceivedAck = false;
    bool serverReceivedTypingFalse = false;

    // Start Server Socket
    bool serverStarted = serverSocket.startServer(testPort, QHostAddress("127.0.0.1"));
    TEST_CHECK(serverStarted);
    TEST_CHECK(serverSocket.isServerListening());

    // Connect Client Socket
    clientSocket.connectToPeer("127.0.0.1", testPort);

    auto checkCompletion = [&]() {
        if (serverReceivedTypingTrue && serverReceivedChatMessage && clientReceivedAck && serverReceivedTypingFalse) {
            std::cout << "  -> Chat transmission sequence successfully verified!" << std::endl;
            clientSocket.disconnectFromPeer();
            serverSocket.stopServer();
            QTimer::singleShot(20, []() {
                QCoreApplication::quit();
            });
        }
    };

    // Setup Server Chat signals
    QObject::connect(&serverChat, &ChatController::peerTypingChanged, [&](const QString &peerId, bool isTyping) {
        if (isTyping) {
            serverReceivedTypingTrue = true;
            std::cout << "  -> Server received TypingIndicator: true from " << peerId.toStdString() << std::endl;
        } else {
            serverReceivedTypingFalse = true;
            std::cout << "  -> Server received TypingIndicator: false from " << peerId.toStdString() << std::endl;
            checkCompletion();
        }
    });

    QObject::connect(&serverChat, &ChatController::messageReceived, [&](const ChatMessage &msg) {
        serverReceivedChatMessage = true;
        std::cout << "  -> Server received ChatMessage: " << msg.content.toStdString() << " (ID: " << msg.messageId.toStdString() << ")" << std::endl;
        TEST_CHECK(msg.content == "Hello P2P Chat Protocol v1.0!");
    });

    // Setup Client Chat signals
    QObject::connect(&clientChat, &ChatController::messageStatusChanged, [&](const QString &msgId, MessageState state) {
        if (state == MessageState::Delivered) {
            clientReceivedAck = true;
            std::cout << "  -> Client received Delivery ACK for message: " << msgId.toStdString() << std::endl;

            // Step 3: Send Typing false after ACK
            QTimer::singleShot(20, [&]() {
                clientChat.sendTypingNotification("mc_server_device", false);
                checkCompletion();
            });
        }
    });

    // Client begins communication once connected
    QObject::connect(&clientSocket, &P2PSocket::connected, [&]() {
        std::cout << "  -> Client connected. Initiating chat sequence..." << std::endl;

        // Step 1: Send Typing true
        clientChat.sendTypingNotification("mc_server_device", true);

        // Step 2: Send Message after 50ms
        QTimer::singleShot(50, [&]() {
            clientChat.sendMessage("mc_server_device", "Hello P2P Chat Protocol v1.0!");
        });
    });

    // Timeout safety guard (5s)
    QTimer::singleShot(5000, [&]() {
        if (!clientReceivedAck || !serverReceivedTypingFalse) {
            std::cerr << "  -> End-to-end Chat test timed out!" << std::endl;
            QCoreApplication::exit(1);
        }
    });

    int ret = QCoreApplication::exec();
    TEST_CHECK(ret == 0);
    TEST_CHECK(serverReceivedTypingTrue);
    TEST_CHECK(serverReceivedChatMessage);
    TEST_CHECK(clientReceivedAck);
    TEST_CHECK(serverReceivedTypingFalse);

    std::cout << "[PASS] testEndToEndP2PChatTransmission" << std::endl;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    MeckChat::Core::Logger::init();
    MeckChat::Core::AppConfig::instance().setDeviceId("mc_client_unit_test");

    testChatMessageSerialization();
    testMessageAckSerialization();
    testTypingIndicatorSerialization();
    testMessageIdGeneration();
    testEndToEndP2PChatTransmission();

    std::cout << "\n==============================================" << std::endl;
    std::cout << "  All Chat Messaging & ACK Protocol Tests Passed! " << std::endl;
    std::cout << "==============================================" << std::endl;
    return 0;
}
