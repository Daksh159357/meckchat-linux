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

constexpr int MAX_CHAT_MESSAGE_CONTENT_SIZE = 64 * 1024; // 64 KiB

QString generateMessageId();

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

struct TypingIndicator {
    QString senderDeviceId;
    QString recipientDeviceId;
    bool isTyping{false};
    qint64 timestamp{0};

    QJsonObject toJson() const;
    static std::optional<TypingIndicator> fromJson(const QJsonObject &json);
};

constexpr int DEFAULT_FILE_CHUNK_SIZE = 65536; // 64 KiB

QString generateTransferId();
QString sanitizeFileName(const QString &rawName);

struct FileOffer {
    QString transferId;
    QString fileName;
    qint64 fileSize{0};
    QString sha256;
    int chunkSize{DEFAULT_FILE_CHUNK_SIZE};
    int totalChunks{0};

    QJsonObject toJson() const;
    static std::optional<FileOffer> fromJson(const QJsonObject &json);
};

struct FileAccept {
    QString transferId;

    QJsonObject toJson() const;
    static std::optional<FileAccept> fromJson(const QJsonObject &json);
};

struct FileReject {
    QString transferId;
    QString reason{"user_rejected"};

    QJsonObject toJson() const;
    static std::optional<FileReject> fromJson(const QJsonObject &json);
};

struct FileComplete {
    QString transferId;
    QString sha256;
    QString status{"verified"};

    QJsonObject toJson() const;
    static std::optional<FileComplete> fromJson(const QJsonObject &json);
};

struct FileCancel {
    QString transferId;
    QString reason{"cancelled"};

    QJsonObject toJson() const;
    static std::optional<FileCancel> fromJson(const QJsonObject &json);
};

struct FileChunk {
    static QByteArray encode(const QString &transferId, uint32_t chunkIndex, const QByteArray &chunkData);
    static bool decode(const QByteArray &rawBytes, QString &transferId, uint32_t &chunkIndex, QByteArray &chunkData);
};

struct PairingRequest {
    QString sessionId;
    QString senderDeviceId;
    QString receiverDeviceId;
    qint64 timestamp{0};
    QString saltBase64;
    QString ephemeralPublicKeyBase64;
    QString proposedVirtualIp;
    QString authProofBase64;

    QJsonObject toJson() const;
    static std::optional<PairingRequest> fromJson(const QJsonObject &json);
    QByteArray buildTranscriptMessage() const;
};

struct PairingResponse {
    QString sessionId;
    QString senderDeviceId;
    QString receiverDeviceId;
    qint64 timestamp{0};
    QString saltBase64;
    QString ephemeralPublicKeyBase64;
    QString proposedVirtualIp;
    QString authProofBase64;
    QString status{"accepted"}; // "accepted" | "rejected"
    std::optional<QString> errorMessage;

    QJsonObject toJson() const;
    static std::optional<PairingResponse> fromJson(const QJsonObject &json);
    QByteArray buildTranscriptMessage() const;
};

} // namespace MeckChat::Protocol
