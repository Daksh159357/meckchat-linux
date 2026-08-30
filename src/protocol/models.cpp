#include "meckchat/protocol/models.h"

namespace MeckChat::Protocol {

QString platformToString(Platform p) {
    switch (p) {
        case Platform::Linux: return "linux";
        case Platform::Android: return "android";
        case Platform::Windows: return "windows";
        default: return "unknown";
    }
}

Platform stringToPlatform(const QString &str) {
    QString s = str.toLower();
    if (s == "linux") return Platform::Linux;
    if (s == "android") return Platform::Android;
    if (s == "windows") return Platform::Windows;
    return Platform::Unknown;
}

QJsonObject Device::toPresenceOnlineJson() const {
    QJsonObject obj;
    obj["type"] = "presence_online";
    obj["protocol_version"] = 1;
    obj["device_id"] = deviceId;
    obj["display_name"] = displayName;
    obj["platform"] = platformToString(platform);
    obj["timestamp"] = QDateTime::currentSecsSinceEpoch();
    return obj;
}

QJsonObject Device::toPresenceOfflineJson() const {
    QJsonObject obj;
    obj["type"] = "presence_offline";
    obj["device_id"] = deviceId;
    return obj;
}

std::optional<Device> Device::fromPresenceJson(const QJsonObject &json) {
    QString deviceId = json["device_id"].toString();
    if (deviceId.isEmpty()) return std::nullopt;

    Device dev;
    dev.deviceId = deviceId;
    dev.displayName = json["display_name"].toString("Unknown Device");
    dev.platform = stringToPlatform(json["platform"].toString());
    dev.isOnline = (json["type"].toString() == "presence_online");

    if (json.contains("timestamp")) {
        qint64 ts = json["timestamp"].toInteger();
        dev.lastSeen = QDateTime::fromSecsSinceEpoch(ts);
    } else {
        dev.lastSeen = QDateTime::currentDateTime();
    }

    return dev;
}

QJsonObject DiscoveryRequest::toJson() const {
    QJsonObject obj;
    obj["type"] = "discovery_request";
    obj["protocol_version"] = 1;
    obj["device_id"] = deviceId;
    obj["timestamp"] = timestamp > 0 ? timestamp : QDateTime::currentSecsSinceEpoch();
    return obj;
}

std::optional<DiscoveryRequest> DiscoveryRequest::fromJson(const QJsonObject &json) {
    if (json["type"].toString() != "discovery_request") return std::nullopt;
    QString deviceId = json["device_id"].toString();
    if (deviceId.isEmpty()) return std::nullopt;

    DiscoveryRequest req;
    req.deviceId = deviceId;
    req.timestamp = json["timestamp"].toInteger();
    return req;
}

QJsonObject ChatMessage::toJson() const {
    QJsonObject obj;
    obj["message_id"] = messageId;
    obj["sender_device_id"] = senderDeviceId;
    obj["recipient_device_id"] = recipientDeviceId;
    obj["content"] = content;
    obj["timestamp"] = timestamp > 0 ? timestamp : QDateTime::currentSecsSinceEpoch();
    if (replyToMessageId.has_value()) {
        obj["reply_to_message_id"] = *replyToMessageId;
    } else {
        obj["reply_to_message_id"] = QJsonValue::Null;
    }
    return obj;
}

std::optional<ChatMessage> ChatMessage::fromJson(const QJsonObject &json) {
    QString msgId = json["message_id"].toString();
    QString sender = json["sender_device_id"].toString();
    QString recipient = json["recipient_device_id"].toString();
    if (msgId.isEmpty() || sender.isEmpty() || recipient.isEmpty()) return std::nullopt;

    ChatMessage msg;
    msg.messageId = msgId;
    msg.senderDeviceId = sender;
    msg.recipientDeviceId = recipient;
    msg.content = json["content"].toString();
    msg.timestamp = json["timestamp"].toInteger();
    if (json.contains("reply_to_message_id") && !json["reply_to_message_id"].isNull()) {
        msg.replyToMessageId = json["reply_to_message_id"].toString();
    }
    return msg;
}

QJsonObject MessageAck::toJson() const {
    QJsonObject obj;
    obj["message_id"] = messageId;
    obj["status"] = status;
    obj["timestamp"] = timestamp > 0 ? timestamp : QDateTime::currentSecsSinceEpoch();
    return obj;
}

std::optional<MessageAck> MessageAck::fromJson(const QJsonObject &json) {
    QString msgId = json["message_id"].toString();
    if (msgId.isEmpty()) return std::nullopt;
    MessageAck ack;
    ack.messageId = msgId;
    ack.status = json["status"].toString("delivered");
    ack.timestamp = json["timestamp"].toInteger();
    return ack;
}

QJsonObject FileOffer::toJson() const {
    QJsonObject obj;
    obj["transfer_id"] = transferId;
    obj["file_name"] = fileName;
    obj["file_size"] = fileSize;
    obj["sha256"] = sha256;
    obj["chunk_size"] = chunkSize;
    obj["total_chunks"] = totalChunks;
    return obj;
}

std::optional<FileOffer> FileOffer::fromJson(const QJsonObject &json) {
    QString tid = json["transfer_id"].toString();
    if (tid.isEmpty()) return std::nullopt;
    FileOffer offer;
    offer.transferId = tid;
    offer.fileName = json["file_name"].toString();
    offer.fileSize = json["file_size"].toInteger();
    offer.sha256 = json["sha256"].toString();
    offer.chunkSize = json["chunk_size"].toInt(65536);
    offer.totalChunks = json["total_chunks"].toInt(0);
    return offer;
}

} // namespace MeckChat::Protocol
