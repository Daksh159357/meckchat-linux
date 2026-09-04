#include <QCoreApplication>
#include <QTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <cassert>
#include <iostream>

#include "meckchat/network/mqtt_signaling.h"
#include "meckchat/core/config.h"
#include "meckchat/core/logger.h"

using namespace MeckChat::Network;
using namespace MeckChat::Protocol;

void testDeviceIdValidation() {
    std::cout << "[RUN] testDeviceIdValidation" << std::endl;

    // Valid Device IDs
    assert(MqttSignalingClient::isValidDeviceId("mc_12345678"));
    assert(MqttSignalingClient::isValidDeviceId("mc_f47ac10b-58cc-4372-a567-0e02b2c3d479"));
    assert(MqttSignalingClient::isValidDeviceId("mc_linux_node_01"));
    assert(MqttSignalingClient::isValidDeviceId("mc_test.node-123_abc"));

    // Invalid Device IDs
    assert(!MqttSignalingClient::isValidDeviceId(""));
    assert(!MqttSignalingClient::isValidDeviceId("mc_"));
    assert(!MqttSignalingClient::isValidDeviceId("12345678"));
    assert(!MqttSignalingClient::isValidDeviceId("device_mc_12345"));
    assert(!MqttSignalingClient::isValidDeviceId("mc_bad/slash"));
    assert(!MqttSignalingClient::isValidDeviceId("mc_bad+plus"));
    assert(!MqttSignalingClient::isValidDeviceId("mc_bad#hash"));
    assert(!MqttSignalingClient::isValidDeviceId("mc_bad space"));
    assert(!MqttSignalingClient::isValidDeviceId("mc_" + QString("a").repeated(200)));

    std::cout << "[PASS] testDeviceIdValidation" << std::endl;
}

void testTopicValidation() {
    std::cout << "[RUN] testTopicValidation" << std::endl;

    assert(MqttSignalingClient::isValidTopic("meckchat/v1/discovery"));
    assert(MqttSignalingClient::isValidTopic("meckchat/v1/presence/online/mc_node1"));
    assert(MqttSignalingClient::isValidTopic("meckchat/v1/presence/offline/mc_node1"));
    assert(MqttSignalingClient::isValidTopic("meckchat/v1/presence/online/+"));

    assert(!MqttSignalingClient::isValidTopic(""));
    assert(!MqttSignalingClient::isValidTopic(QString("a").repeated(300)));

    std::cout << "[PASS] testTopicValidation" << std::endl;
}

void testVariableByteIntegerCodec() {
    std::cout << "[RUN] testVariableByteIntegerCodec" << std::endl;

    struct TestCase {
        quint32 value;
        QByteArray expected;
    };

    std::vector<TestCase> cases = {
        {0, QByteArray::fromHex("00")},
        {127, QByteArray::fromHex("7f")},
        {128, QByteArray::fromHex("8001")},
        {16383, QByteArray::fromHex("ff7f")},
        {16384, QByteArray::fromHex("808001")},
        {2097151, QByteArray::fromHex("ffff7f")},
        {2097152, QByteArray::fromHex("80808001")},
        {268435455, QByteArray::fromHex("ffffff7f")}
    };

    for (const auto &tc : cases) {
        QByteArray encoded = MqttSignalingClient::encodeVariableLength(tc.value);
        assert(encoded == tc.expected);

        quint32 decoded = 0;
        int bytesConsumed = 0;
        bool ok = MqttSignalingClient::decodeVariableLength(encoded, 0, decoded, bytesConsumed);
        assert(ok);
        assert(decoded == tc.value);
        assert(bytesConsumed == encoded.size());
    }

    // Incomplete stream test
    quint32 incompleteDecoded = 0;
    int consumed = 0;
    QByteArray incompleteBuf = QByteArray::fromHex("8080"); // Needs 3rd byte
    assert(!MqttSignalingClient::decodeVariableLength(incompleteBuf, 0, incompleteDecoded, consumed));

    std::cout << "[PASS] testVariableByteIntegerCodec" << std::endl;
}

void testUtf8StringCodec() {
    std::cout << "[RUN] testUtf8StringCodec" << std::endl;

    QString testStr = "meckchat/v1/discovery";
    QByteArray encoded = MqttSignalingClient::encodeUtf8String(testStr);

    assert(encoded.size() == testStr.toUtf8().size() + 2);
    assert(static_cast<quint8>(encoded[0]) == 0);
    assert(static_cast<quint8>(encoded[1]) == testStr.toUtf8().size());

    QString decoded;
    int offset = 0;
    bool ok = MqttSignalingClient::decodeUtf8String(encoded, offset, decoded);
    assert(ok);
    assert(decoded == testStr);
    assert(offset == encoded.size());

    // Truncated buffer check
    int badOffset = 0;
    QByteArray truncated = encoded.left(5);
    assert(!MqttSignalingClient::decodeUtf8String(truncated, badOffset, decoded));

    std::cout << "[PASS] testUtf8StringCodec" << std::endl;
}

void testPayloadValidation() {
    std::cout << "[RUN] testPayloadValidation" << std::endl;

    // Valid Online Presence
    Device dev;
    dev.deviceId = "mc_test_unit_1";
    dev.displayName = "Test Device";
    dev.platform = Platform::Linux;
    dev.isOnline = true;
    QJsonObject onlineJson = dev.toPresenceOnlineJson();
    assert(onlineJson["type"].toString() == "presence_online");
    assert(onlineJson["protocol_version"].toInt() == 1);
    assert(onlineJson["device_id"].toString() == "mc_test_unit_1");

    auto parsedDev = Device::fromPresenceJson(onlineJson);
    assert(parsedDev.has_value());
    assert(parsedDev->deviceId == "mc_test_unit_1");
    assert(parsedDev->displayName == "Test Device");
    assert(parsedDev->platform == Platform::Linux);

    // Valid Offline Presence
    QJsonObject offlineJson = dev.toPresenceOfflineJson();
    assert(offlineJson["type"].toString() == "presence_offline");
    assert(offlineJson["device_id"].toString() == "mc_test_unit_1");

    // Valid Discovery Request
    DiscoveryRequest req;
    req.deviceId = "mc_test_unit_1";
    req.timestamp = 1725000000;
    QJsonObject discJson = req.toJson();
    assert(discJson["type"].toString() == "discovery_request");
    assert(discJson["device_id"].toString() == "mc_test_unit_1");

    auto parsedReq = DiscoveryRequest::fromJson(discJson);
    assert(parsedReq.has_value());
    assert(parsedReq->deviceId == "mc_test_unit_1");
    assert(parsedReq->timestamp == 1725000000);

    // Invalid JSON cases
    QJsonObject missingDevId;
    missingDevId["type"] = "discovery_request";
    assert(!DiscoveryRequest::fromJson(missingDevId).has_value());

    QJsonObject wrongType;
    wrongType["type"] = "chat_message";
    wrongType["device_id"] = "mc_test";
    assert(!DiscoveryRequest::fromJson(wrongType).has_value());

    std::cout << "[PASS] testPayloadValidation" << std::endl;
}

void testLiveMqttIntegration(int argc, char **argv) {
    std::cout << "[RUN] testLiveMqttIntegration (Connecting to HiveMQ TLS on broker.hivemq.com:8883)" << std::endl;

    QCoreApplication app(argc, argv);
    MeckChat::Core::Logger::init();

    // Configure unique test device identity
    QString testDeviceId = "mc_test_ci_" + QString::number(QDateTime::currentSecsSinceEpoch());
    MeckChat::Core::AppConfig::instance().setDeviceId(testDeviceId);
    MeckChat::Core::AppConfig::instance().setDisplayName("CI Test Worker");

    MqttSignalingClient client;

    bool connectedFired = false;
    bool stateChangedToReady = false;

    QObject::connect(&client, &MqttSignalingClient::connected, [&]() {
        connectedFired = true;
        std::cout << "  -> Live MQTT signal: connected() received" << std::endl;

        // Test publishing discovery request and online presence
        client.broadcastDiscoveryRequest(testDeviceId);

        Device testDev;
        testDev.deviceId = testDeviceId;
        testDev.displayName = "CI Test Worker";
        testDev.platform = Platform::Linux;
        testDev.isOnline = true;
        client.publishOnlinePresence(testDev);

        // Schedule graceful disconnect after 1.5 seconds
        QTimer::singleShot(1500, [&]() {
            std::cout << "  -> Initiating graceful disconnect..." << std::endl;
            client.disconnectFromBroker();
            assert(client.state() == MqttState::Disconnected);
            assert(!client.isConnected());
            app.quit();
        });
    });

    QObject::connect(&client, &MqttSignalingClient::stateChanged, [&](MqttState st) {
        if (st == MqttState::Ready) {
            stateChangedToReady = true;
            std::cout << "  -> Live MQTT stateChanged: Ready" << std::endl;
        }
    });

    QObject::connect(&client, &MqttSignalingClient::errorOccurred, [&](const QString &err) {
        std::cerr << "  -> Live MQTT Error: " << err.toStdString() << std::endl;
    });

    // Timeout safety guard for test (15 seconds)
    QTimer::singleShot(15000, [&]() {
        if (!connectedFired) {
            std::cerr << "  -> Live MQTT test timed out after 15s!" << std::endl;
            app.exit(1);
        }
    });

    client.connectToBroker("broker.hivemq.com", 8883);

    int ret = app.exec();
    assert(ret == 0);
    assert(connectedFired);
    assert(stateChangedToReady);

    std::cout << "[PASS] testLiveMqttIntegration (REAL MQTT INTEGRATION TEST: PASS)" << std::endl;
}

int main(int argc, char **argv) {
    testDeviceIdValidation();
    testTopicValidation();
    testVariableByteIntegerCodec();
    testUtf8StringCodec();
    testPayloadValidation();
    testLiveMqttIntegration(argc, argv);

    std::cout << "\n==============================================" << std::endl;
    std::cout << "  All MQTT Signaling Tests Passed Successfully! " << std::endl;
    std::cout << "==============================================" << std::endl;
    return 0;
}
