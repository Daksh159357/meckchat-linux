#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <optional>

namespace MeckChat::Protocol {

enum class Platform {
    Linux,
    Android,
    Windows,
    Unknown
};

QString platformToString(Platform p);
Platform stringToPlatform(const QString &str);

struct Device {
    QString deviceId;
    QString displayName;
    Platform platform{Platform::Linux};
    QDateTime lastSeen;
    bool isOnline{true};
    QString virtualIp;

    QJsonObject toPresenceOnlineJson() const;
    QJsonObject toPresenceOfflineJson() const;
    static std::optional<Device> fromPresenceJson(const QJsonObject &json);
};

struct DiscoveryRequest {
    QString deviceId;
    qint64 timestamp{0};

    QJsonObject toJson() const;
    static std::optional<DiscoveryRequest> fromJson(const QJsonObject &json);
};

struct ChatMessage {
    QString messageId;
    QString senderDeviceId;
    QString recipientDeviceId;
    QString content;
    qint64 timestamp{0};
    std::optional<QString> replyToMessageId;

    QJsonObject toJson() const;
    static std::optional<ChatMessage> fromJson(const QJsonObject &json);
};

struct MessageAck {
    QString messageId;
    QString status; // "delivered" | "read"
    qint64 timestamp{0};

    QJsonObject toJson() const;
    static std::optional<MessageAck> fromJson(const QJsonObject &json);
};

struct FileOffer {
    QString transferId;
    QString fileName;
    qint64 fileSize{0};
    QString sha256;
    int chunkSize{65536};
    int totalChunks{0};

    QJsonObject toJson() const;
    static std::optional<FileOffer> fromJson(const QJsonObject &json);
};

} // namespace MeckChat::Protocol
