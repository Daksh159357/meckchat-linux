#include "meckchat/protocol/models.h"
#include <QUuid>
#include <QFileInfo>
#include <QDir>

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

QString generateMessageId() {
    return QString("msg_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
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
        obj["reply_to_message_id"] = QJsonValue();
    }
    return obj;
}

std::optional<ChatMessage> ChatMessage::fromJson(const QJsonObject &json) {
    QString msgId = json["message_id"].toString();
    QString sender = json["sender_device_id"].toString();
    QString recipient = json["recipient_device_id"].toString();
    if (msgId.isEmpty() || sender.isEmpty() || recipient.isEmpty()) return std::nullopt;

    QString content = json["content"].toString();
    if (content.toUtf8().size() > MAX_CHAT_MESSAGE_CONTENT_SIZE) {
        return std::nullopt; // Reject oversized messages
    }

    ChatMessage msg;
    msg.messageId = msgId;
    msg.senderDeviceId = sender;
    msg.recipientDeviceId = recipient;
    msg.content = content;
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

QJsonObject TypingIndicator::toJson() const {
    QJsonObject obj;
    obj["sender_device_id"] = senderDeviceId;
    obj["recipient_device_id"] = recipientDeviceId;
    obj["is_typing"] = isTyping;
    obj["timestamp"] = timestamp > 0 ? timestamp : QDateTime::currentSecsSinceEpoch();
    return obj;
}

std::optional<TypingIndicator> TypingIndicator::fromJson(const QJsonObject &json) {
    QString sender = json["sender_device_id"].toString();
    QString recipient = json["recipient_device_id"].toString();
    if (sender.isEmpty() || recipient.isEmpty()) return std::nullopt;

    TypingIndicator typing;
    typing.senderDeviceId = sender;
    typing.recipientDeviceId = recipient;
    typing.isTyping = json.contains("is_typing") ? json["is_typing"].toBool() : json["typing"].toBool(false);
    typing.timestamp = json["timestamp"].toInteger();
    return typing;
}

QString generateTransferId() {
    return QString("ft_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces).replace("-", "").left(13));
}

QString sanitizeFileName(const QString &rawName) {
    QString normalized = rawName;
    normalized.replace('\\', '/');
    QFileInfo info(normalized);
    QString clean = info.fileName();
    clean.replace('/', "");
    clean.replace('\\', "");
    clean.replace("..", "");
    clean = clean.trimmed();
    if (clean.isEmpty() || clean == "." || clean == "..") {
        return "received_file";
    }
    return clean;
}

QJsonObject FileOffer::toJson() const {
    QJsonObject obj;
    obj["transfer_id"] = transferId;
    obj["file_name"] = sanitizeFileName(fileName);
    obj["file_size"] = fileSize;
    obj["sha256"] = sha256;
    obj["chunk_size"] = chunkSize > 0 ? chunkSize : DEFAULT_FILE_CHUNK_SIZE;
    obj["total_chunks"] = totalChunks;
    return obj;
}

std::optional<FileOffer> FileOffer::fromJson(const QJsonObject &json) {
    QString tid = json["transfer_id"].toString();
    QString name = sanitizeFileName(json["file_name"].toString());
    qint64 size = json["file_size"].toInteger();
    QString hash = json["sha256"].toString();

    if (tid.isEmpty() || name.isEmpty() || size < 0 || hash.isEmpty()) return std::nullopt;

    FileOffer offer;
    offer.transferId = tid;
    offer.fileName = name;
    offer.fileSize = size;
    offer.sha256 = hash;
    offer.chunkSize = json["chunk_size"].toInt(DEFAULT_FILE_CHUNK_SIZE);
    offer.totalChunks = json["total_chunks"].toInt(0);
    return offer;
}

QJsonObject FileAccept::toJson() const {
    QJsonObject obj;
    obj["transfer_id"] = transferId;
    return obj;
}

std::optional<FileAccept> FileAccept::fromJson(const QJsonObject &json) {
    QString tid = json["transfer_id"].toString();
    if (tid.isEmpty()) return std::nullopt;
    FileAccept acc;
    acc.transferId = tid;
    return acc;
}

QJsonObject FileReject::toJson() const {
    QJsonObject obj;
    obj["transfer_id"] = transferId;
    obj["reason"] = reason;
    return obj;
}

std::optional<FileReject> FileReject::fromJson(const QJsonObject &json) {
    QString tid = json["transfer_id"].toString();
    if (tid.isEmpty()) return std::nullopt;
    FileReject rej;
    rej.transferId = tid;
    rej.reason = json["reason"].toString("user_rejected");
    return rej;
}

QJsonObject FileComplete::toJson() const {
    QJsonObject obj;
    obj["transfer_id"] = transferId;
    obj["sha256"] = sha256;
    obj["status"] = status;
    return obj;
}

std::optional<FileComplete> FileComplete::fromJson(const QJsonObject &json) {
    QString tid = json["transfer_id"].toString();
    QString hash = json["sha256"].toString();
    if (tid.isEmpty() || hash.isEmpty()) return std::nullopt;
    FileComplete comp;
    comp.transferId = tid;
    comp.sha256 = hash;
    comp.status = json["status"].toString("verified");
    return comp;
}

QJsonObject FileCancel::toJson() const {
    QJsonObject obj;
    obj["transfer_id"] = transferId;
    obj["reason"] = reason;
    return obj;
}

std::optional<FileCancel> FileCancel::fromJson(const QJsonObject &json) {
    QString tid = json["transfer_id"].toString();
    if (tid.isEmpty()) return std::nullopt;
    FileCancel can;
    can.transferId = tid;
    can.reason = json["reason"].toString("cancelled");
    return can;
}

QByteArray FileChunk::encode(const QString &transferId, uint32_t chunkIndex, const QByteArray &chunkData) {
    QByteArray result;
    result.resize(24 + chunkData.size());

    // 16 bytes transfer ID (fixed length)
    QByteArray tidBytes = transferId.toUtf8();
    if (tidBytes.size() > 16) tidBytes = tidBytes.left(16);
    memset(result.data(), 0, 16);
    memcpy(result.data(), tidBytes.constData(), tidBytes.size());

    // 4 bytes chunkIndex (big endian)
    uint32_t beIndex = qToBigEndian(chunkIndex);
    memcpy(result.data() + 16, &beIndex, 4);

    // 4 bytes chunkSize (big endian)
    uint32_t beSize = qToBigEndian(static_cast<uint32_t>(chunkData.size()));
    memcpy(result.data() + 20, &beSize, 4);

    // Chunk payload
    if (!chunkData.isEmpty()) {
        memcpy(result.data() + 24, chunkData.constData(), chunkData.size());
    }

    return result;
}

bool FileChunk::decode(const QByteArray &rawBytes, QString &transferId, uint32_t &chunkIndex, QByteArray &chunkData) {
    if (rawBytes.size() < 24) {
        return false;
    }

    const char *ptr = rawBytes.constData();

    // 16 bytes Transfer ID
    transferId = QString::fromUtf8(QByteArray(ptr, 16)).trimmed();
    // Strip trailing null characters if any
    int nullIdx = transferId.indexOf(QChar('\0'));
    if (nullIdx != -1) {
        transferId = transferId.left(nullIdx);
    }

    if (transferId.isEmpty()) {
        return false;
    }

    chunkIndex = qFromBigEndian(*reinterpret_cast<const uint32_t*>(ptr + 16));
    uint32_t chunkSize = qFromBigEndian(*reinterpret_cast<const uint32_t*>(ptr + 20));

    if (rawBytes.size() != 24 + static_cast<int>(chunkSize)) {
        return false;
    }

    chunkData = rawBytes.mid(24, chunkSize);
    return true;
}

QJsonObject PairingRequest::toJson() const {
    QJsonObject obj;
    obj["type"] = "pairing_request";
    obj["protocol_version"] = 1;
    obj["session_id"] = sessionId;
    obj["sender_device_id"] = senderDeviceId;
    obj["receiver_device_id"] = receiverDeviceId;
    obj["timestamp"] = timestamp > 0 ? timestamp : QDateTime::currentSecsSinceEpoch();
    obj["salt"] = saltBase64;
    obj["ephemeral_public_key"] = ephemeralPublicKeyBase64;
    obj["proposed_virtual_ip"] = proposedVirtualIp;
    obj["auth_proof"] = authProofBase64;
    return obj;
}

std::optional<PairingRequest> PairingRequest::fromJson(const QJsonObject &json) {
    if (json["type"].toString() != "pairing_request") return std::nullopt;
    QString sessId = json["session_id"].toString();
    QString sender = json["sender_device_id"].toString();
    QString receiver = json["receiver_device_id"].toString();
    QString salt = json["salt"].toString();
    QString pubKey = json["ephemeral_public_key"].toString();
    QString virtIp = json["proposed_virtual_ip"].toString();
    QString proof = json["auth_proof"].toString();
    qint64 ts = json["timestamp"].toInteger();

    if (sessId.isEmpty() || sender.isEmpty() || receiver.isEmpty() ||
        salt.isEmpty() || pubKey.isEmpty() || virtIp.isEmpty() || proof.isEmpty() || ts <= 0) {
        return std::nullopt;
    }

    PairingRequest req;
    req.sessionId = sessId;
    req.senderDeviceId = sender;
    req.receiverDeviceId = receiver;
    req.timestamp = ts;
    req.saltBase64 = salt;
    req.ephemeralPublicKeyBase64 = pubKey;
    req.proposedVirtualIp = virtIp;
    req.authProofBase64 = proof;
    return req;
}

QByteArray PairingRequest::buildTranscriptMessage() const {
    return senderDeviceId.toUtf8() +
           receiverDeviceId.toUtf8() +
           QByteArray::number(timestamp) +
           ephemeralPublicKeyBase64.toUtf8();
}

QJsonObject PairingResponse::toJson() const {
    QJsonObject obj;
    obj["type"] = "pairing_response";
    obj["protocol_version"] = 1;
    obj["session_id"] = sessionId;
    obj["sender_device_id"] = senderDeviceId;
    obj["receiver_device_id"] = receiverDeviceId;
    obj["timestamp"] = timestamp > 0 ? timestamp : QDateTime::currentSecsSinceEpoch();
    obj["salt"] = saltBase64;
    obj["ephemeral_public_key"] = ephemeralPublicKeyBase64;
    obj["proposed_virtual_ip"] = proposedVirtualIp;
    obj["auth_proof"] = authProofBase64;
    obj["status"] = status;
    if (errorMessage.has_value()) {
        obj["error_message"] = *errorMessage;
    }
    return obj;
}

std::optional<PairingResponse> PairingResponse::fromJson(const QJsonObject &json) {
    if (json["type"].toString() != "pairing_response") return std::nullopt;
    QString sessId = json["session_id"].toString();
    QString sender = json["sender_device_id"].toString();
    QString receiver = json["receiver_device_id"].toString();
    QString salt = json["salt"].toString();
    QString pubKey = json["ephemeral_public_key"].toString();
    QString virtIp = json["proposed_virtual_ip"].toString();
    QString proof = json["auth_proof"].toString();
    QString stat = json["status"].toString("accepted");
    qint64 ts = json["timestamp"].toInteger();

    if (sessId.isEmpty() || sender.isEmpty() || receiver.isEmpty() ||
        salt.isEmpty() || pubKey.isEmpty() || virtIp.isEmpty() || proof.isEmpty() || ts <= 0) {
        return std::nullopt;
    }

    PairingResponse resp;
    resp.sessionId = sessId;
    resp.senderDeviceId = sender;
    resp.receiverDeviceId = receiver;
    resp.timestamp = ts;
    resp.saltBase64 = salt;
    resp.ephemeralPublicKeyBase64 = pubKey;
    resp.proposedVirtualIp = virtIp;
    resp.authProofBase64 = proof;
    resp.status = stat;
    if (json.contains("error_message") && !json["error_message"].isNull()) {
        resp.errorMessage = json["error_message"].toString();
    }
    return resp;
}

QByteArray PairingResponse::buildTranscriptMessage() const {
    return senderDeviceId.toUtf8() +
           receiverDeviceId.toUtf8() +
           QByteArray::number(timestamp) +
           ephemeralPublicKeyBase64.toUtf8();
}

} // namespace MeckChat::Protocol
