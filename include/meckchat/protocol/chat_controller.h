#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QDateTime>
#include <memory>
#include "meckchat/protocol/models.h"
#include "meckchat/protocol/framing.h"
#include "meckchat/network/p2p_socket.h"

namespace MeckChat::Protocol {

enum class MessageState {
    Pending,
    Sent,
    Delivered,
    Read,
    Failed
};

struct TrackedMessage {
    ChatMessage message;
    MessageState state{MessageState::Pending};
    qint64 sendTimestamp{0};
};

class ChatController : public QObject {
    Q_OBJECT

public:
    explicit ChatController(Network::P2PSocket *socket, QObject *parent = nullptr);
    ~ChatController() override;

    // Send a chat message to a peer
    QString sendMessage(const QString &recipientDeviceId, const QString &content, const QString &peerVirtualIp = QString());

    // Send a message read receipt
    bool sendReadReceipt(const QString &messageId, const QString &peerVirtualIp = QString());

    // Send typing notification
    void sendTypingNotification(const QString &recipientDeviceId, bool isTyping, const QString &peerVirtualIp = QString());

    // State queries
    std::optional<TrackedMessage> getTrackedMessage(const QString &messageId) const;
    bool isPeerTyping(const QString &peerDeviceId) const;

    // Configuration
    void setAckTimeoutMs(int timeoutMs);
    int ackTimeoutMs() const;

signals:
    void messageSent(const QString &messageId);
    void messageReceived(const MeckChat::Protocol::ChatMessage &message);
    void messageStatusChanged(const QString &messageId, MeckChat::Protocol::MessageState state);
    void messageAckReceived(const MeckChat::Protocol::MessageAck &ack);
    void peerTypingChanged(const QString &peerDeviceId, bool isTyping);
    void chatErrorOccurred(const QString &error);

private slots:
    void onFrameReceived(const MeckChat::Protocol::P2PFrame &frame);
    void onFrameReceivedFrom(const MeckChat::Protocol::P2PFrame &frame, const QString &senderIp);
    void onPeerDisconnected(const QString &peerIp);
    void onSocketDisconnected();

private:
    void handleInboundChatMessage(const QByteArray &payload, const QString &senderIp);
    void handleInboundMessageAck(const QByteArray &payload);
    void handleInboundTypingIndicator(const QByteArray &payload);

    void cleanExpiredDuplicateCache();

    Network::P2PSocket *m_socket{nullptr};

    // Outgoing messages tracking: messageId -> TrackedMessage
    QMap<QString, TrackedMessage> m_outgoingMessages;
    // Outgoing ACK timers: messageId -> QTimer*
    QMap<QString, QTimer*> m_ackTimers;

    // Inbound duplicate detection: messageId -> receive timestamp
    QMap<QString, qint64> m_receivedMessageCache;

    // Peer typing states: peerDeviceId -> auto-expiry timer
    QMap<QString, QTimer*> m_peerTypingTimers;

    // Local outgoing typing debounce
    QTimer *m_localTypingStopTimer{nullptr};
    QString m_lastTypingRecipient;
    bool m_localIsTyping{false};

    int m_ackTimeoutMs{10000}; // 10 seconds default
    static constexpr qint64 DUPLICATE_CACHE_WINDOW_SECS = 300; // 5 minutes
};

} // namespace MeckChat::Protocol
