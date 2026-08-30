#include <cassert>
#include <iostream>
#include "meckchat/protocol/models.h"

using namespace MeckChat::Protocol;

void testDeviceSerialization() {
    Device dev;
    dev.deviceId = "mc_test_device_123";
    dev.displayName = "Linux Node";
    dev.platform = Platform::Linux;
    dev.isOnline = true;

    QJsonObject json = dev.toPresenceOnlineJson();
    assert(json["device_id"].toString() == "mc_test_device_123");
    assert(json["platform"].toString() == "linux");
    assert(json["type"].toString() == "presence_online");

    auto deserialized = Device::fromPresenceJson(json);
    assert(deserialized.has_value());
    assert(deserialized->deviceId == "mc_test_device_123");
    assert(deserialized->displayName == "Linux Node");
    assert(deserialized->platform == Platform::Linux);
    assert(deserialized->isOnline == true);

    std::cout << "[PASS] testDeviceSerialization" << std::endl;
}

void testChatMessageSerialization() {
    ChatMessage msg;
    msg.messageId = "msg_001";
    msg.senderDeviceId = "mc_sender";
    msg.recipientDeviceId = "mc_receiver";
    msg.content = "Hello Cross Platform";
    msg.timestamp = 1725000000;

    QJsonObject json = msg.toJson();
    auto deserialized = ChatMessage::fromJson(json);

    assert(deserialized.has_value());
    assert(deserialized->messageId == "msg_001");
    assert(deserialized->senderDeviceId == "mc_sender");
    assert(deserialized->recipientDeviceId == "mc_receiver");
    assert(deserialized->content == "Hello Cross Platform");
    assert(deserialized->timestamp == 1725000000);

    std::cout << "[PASS] testChatMessageSerialization" << std::endl;
}

int main() {
    testDeviceSerialization();
    testChatMessageSerialization();
    std::cout << "All Linux Protocol Tests Passed Successfully!" << std::endl;
    return 0;
}
