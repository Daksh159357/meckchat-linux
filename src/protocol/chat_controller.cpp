#include "meckchat/protocol/chat_controller.h"
#include "meckchat/core/logger.h"
#include "meckchat/core/config.h"
#include <QJsonDocument>

namespace MeckChat::Protocol {

ChatController::ChatController(Network::P2PSocket *socket, QObject *parent)
    : QObject(parent),
      m_socket(socket),
      m_localTypingStopTimer(new QTimer(this)) {

    m_localTypingStopTimer->setSingleShot(true);
    connect(m_localTypingStopTimer, &QTimer::timeout, this, [this]() {
        if (m_localIsTyping) {
            sendTypingNotification(m_lastTypingRecipient, false);
        }
    });

    if (m_socket) {
        connect(m_socket, &Network::P2PSocket::frameReceivedFrom, this, &ChatController::onFrameReceivedFrom);
        connect(m_socket, &Network::P2PSocket::peerDisconnected, this, &ChatController::onPeerDisconnected);
        connect(m_socket, &Network::P2PSocket::disconnected, this, &ChatController::onSocketDisconnected);
    }
}

ChatController::~ChatController() {
    for (auto timer : m_ackTimers) {
        if (timer) {
            timer->stop();
            delete timer;
        }
    }
    m_ackTimers.clear();

    for (auto timer : m_peerTypingTimers) {
        if (timer) {
            timer->stop();
            delete timer;
        }
    }
    m_peerTypingTimers.clear();
}

void ChatController::setAckTimeoutMs(int timeoutMs) {
    m_ackTimeoutMs = timeoutMs;
}

int ChatController::ackTimeoutMs() const {
    return m_ackTimeoutMs;
}

std::optional<TrackedMessage> ChatController::getTrackedMessage(const QString &messageId) const {
    if (m_outgoingMessages.contains(messageId)) {
        return m_outgoingMessages.value(messageId);
    }
    return std::nullopt;
}

bool ChatController::isPeerTyping(const QString &peerDeviceId) const {
    return m_peerTypingTimers.contains(peerDeviceId);
}

QString ChatController::sendMessage(const QString &recipientDeviceId, const QString &content, const QString &peerVirtualIp) {
    if (content.toUtf8().size() > MAX_CHAT_MESSAGE_CONTENT_SIZE) {
        Core::Logger::error("ChatController", QString("Cannot send message: content size exceeds limit (%1 bytes)").arg(content.toUtf8().size()));
        emit chatErrorOccurred("Message size exceeds maximum 64 KiB limit.");
        return QString();
    }

    if (recipientDeviceId.isEmpty()) {
        Core::Logger::error("ChatController", "Cannot send message: recipientDeviceId is empty.");
        emit chatErrorOccurred("Recipient device ID is empty.");
        return QString();
    }

    QString msgId = generateMessageId();
    ChatMessage msg;
    msg.messageId = msgId;
    msg.senderDeviceId = Core::AppConfig::instance().deviceId();
    msg.recipientDeviceId = recipientDeviceId;
    msg.content = content;
    msg.timestamp = QDateTime::currentSecsSinceEpoch();

    QByteArray payload = QJsonDocument(msg.toJson()).toJson(QJsonDocument::Compact);

    P2PFrame frame;
    frame.type = FrameType::ChatMessage;
    frame.payload = payload;

    if (m_socket && !peerVirtualIp.isEmpty() && !m_socket->isConnected()) {
        m_socket->connectToPeer(peerVirtualIp);
    }

    TrackedMessage tracked;
    tracked.message = msg;
    tracked.state = MessageState::Sent;
    tracked.sendTimestamp = msg.timestamp;
    m_outgoingMessages.insert(msgId, tracked);

    // Start ACK Timeout Timer
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, msgId]() {
        if (m_outgoingMessages.contains(msgId)) {
            if (m_outgoingMessages[msgId].state == MessageState::Sent || m_outgoingMessages[msgId].state == MessageState::Pending) {
                m_outgoingMessages[msgId].state = MessageState::Failed;
                Core::Logger::warning("ChatController", QString("Message ACK timed out for message: %1").arg(msgId));
                emit messageStatusChanged(msgId, MessageState::Failed);
            }
        }
        if (m_ackTimers.contains(msgId)) {
            m_ackTimers.value(msgId)->deleteLater();
            m_ackTimers.remove(msgId);
        }
    });
    m_ackTimers.insert(msgId, timer);
    timer->start(m_ackTimeoutMs);

    bool sent = false;
    if (m_socket) {
        sent = m_socket->sendFrame(frame);
    }

    if (!sent) {
        Core::Logger::warning("ChatController", QString("Socket failed to transmit message frame: %1").arg(msgId));
        // Keep pending or fail if disconnected
        if (!m_socket || !m_socket->isConnected()) {
            m_outgoingMessages[msgId].state = MessageState::Pending;
        }
    } else {
        Core::Logger::info("ChatController", QString("Dispatched ChatMessage frame (%1, payload %2 bytes)").arg(msgId).arg(payload.size()));
    }

    emit messageSent(msgId);
    return msgId;
}

bool ChatController::sendReadReceipt(const QString &messageId, const QString &peerVirtualIp) {
    if (messageId.isEmpty()) return false;

    MessageAck ack;
    ack.messageId = messageId;
    ack.status = "read";
    ack.timestamp = QDateTime::currentSecsSinceEpoch();

    P2PFrame frame;
    frame.type = FrameType::MessageAck;
    frame.payload = QJsonDocument(ack.toJson()).toJson(QJsonDocument::Compact);

    if (m_socket && !peerVirtualIp.isEmpty() && !m_socket->isConnected()) {
        m_socket->connectToPeer(peerVirtualIp);
    }

    if (m_socket) {
        Core::Logger::info("ChatController", QString("Sent Read Receipt ACK for message: %1").arg(messageId));
        return m_socket->sendFrame(frame);
    }
    return false;
}

void ChatController::sendTypingNotification(const QString &recipientDeviceId, bool isTyping, const QString &peerVirtualIp) {
    if (recipientDeviceId.isEmpty()) return;

    m_lastTypingRecipient = recipientDeviceId;

    if (isTyping) {
        if (!m_localIsTyping) {
            m_localIsTyping = true;
            TypingIndicator typing;
            typing.senderDeviceId = Core::AppConfig::instance().deviceId();
            typing.recipientDeviceId = recipientDeviceId;
            typing.isTyping = true;
            typing.timestamp = QDateTime::currentSecsSinceEpoch();

            P2PFrame frame;
            frame.type = FrameType::TypingIndicator;
            frame.payload = QJsonDocument(typing.toJson()).toJson(QJsonDocument::Compact);

            if (m_socket && !peerVirtualIp.isEmpty() && !m_socket->isConnected()) {
                m_socket->connectToPeer(peerVirtualIp);
            }

            if (m_socket) {
                m_socket->sendFrame(frame);
            }
        }
        // Restart 3s timeout
        m_localTypingStopTimer->start(3000);
    } else {
        if (m_localIsTyping) {
            m_localIsTyping = false;
            m_localTypingStopTimer->stop();

            TypingIndicator typing;
            typing.senderDeviceId = Core::AppConfig::instance().deviceId();
            typing.recipientDeviceId = recipientDeviceId;
            typing.isTyping = false;
            typing.timestamp = QDateTime::currentSecsSinceEpoch();

            P2PFrame frame;
            frame.type = FrameType::TypingIndicator;
            frame.payload = QJsonDocument(typing.toJson()).toJson(QJsonDocument::Compact);

            if (m_socket) {
                m_socket->sendFrame(frame);
            }
        }
    }
}

void ChatController::onFrameReceived(const P2PFrame &frame) {
    onFrameReceivedFrom(frame, QString());
}

void ChatController::onFrameReceivedFrom(const P2PFrame &frame, const QString &senderIp) {
    switch (frame.type) {
        case FrameType::ChatMessage:
            handleInboundChatMessage(frame.payload, senderIp);
            break;
        case FrameType::MessageAck:
            handleInboundMessageAck(frame.payload);
            break;
        case FrameType::TypingIndicator:
            handleInboundTypingIndicator(frame.payload);
            break;
        default:
            break;
    }
}

void ChatController::cleanExpiredDuplicateCache() {
    qint64 now = QDateTime::currentSecsSinceEpoch();
    auto it = m_receivedMessageCache.begin();
    while (it != m_receivedMessageCache.end()) {
        if (now - it.value() > DUPLICATE_CACHE_WINDOW_SECS) {
            it = m_receivedMessageCache.erase(it);
        } else {
            ++it;
        }
    }
}

void ChatController::handleInboundChatMessage(const QByteArray &payload, const QString &senderIp) {
    Q_UNUSED(senderIp);
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        Core::Logger::warning("ChatController", "Received malformed ChatMessage JSON payload.");
        return;
    }

    auto msgOpt = ChatMessage::fromJson(doc.object());
    if (!msgOpt.has_value()) {
        Core::Logger::warning("ChatController", "Failed to deserialize ChatMessage fields.");
        return;
    }

    const ChatMessage &msg = *msgOpt;
    cleanExpiredDuplicateCache();

    // Check for duplicate message
    if (m_receivedMessageCache.contains(msg.messageId)) {
        Core::Logger::info("ChatController", QString("Duplicate ChatMessage received (%1). Re-emitting ACK.").arg(msg.messageId));
        // Re-send ACK so sender's ACK timer clears
        MessageAck ack;
        ack.messageId = msg.messageId;
        ack.status = "delivered";
        ack.timestamp = QDateTime::currentSecsSinceEpoch();

        P2PFrame ackFrame;
        ackFrame.type = FrameType::MessageAck;
        ackFrame.payload = QJsonDocument(ack.toJson()).toJson(QJsonDocument::Compact);
        if (m_socket) {
            m_socket->sendFrame(ackFrame);
        }
        return;
    }

    // Record in duplicate detection cache
    m_receivedMessageCache.insert(msg.messageId, QDateTime::currentSecsSinceEpoch());

    Core::Logger::info("ChatController", QString("Received ChatMessage %1 from %2").arg(msg.messageId).arg(msg.senderDeviceId));

    // Clear any typing indicator for sender
    if (m_peerTypingTimers.contains(msg.senderDeviceId)) {
        m_peerTypingTimers[msg.senderDeviceId]->stop();
        m_peerTypingTimers[msg.senderDeviceId]->deleteLater();
        m_peerTypingTimers.remove(msg.senderDeviceId);
        emit peerTypingChanged(msg.senderDeviceId, false);
    }

    emit messageReceived(msg);

    // Automatically emit Delivery ACK
    MessageAck ack;
    ack.messageId = msg.messageId;
    ack.status = "delivered";
    ack.timestamp = QDateTime::currentSecsSinceEpoch();

    P2PFrame ackFrame;
    ackFrame.type = FrameType::MessageAck;
    ackFrame.payload = QJsonDocument(ack.toJson()).toJson(QJsonDocument::Compact);
    if (m_socket) {
        m_socket->sendFrame(ackFrame);
    }
}

void ChatController::handleInboundMessageAck(const QByteArray &payload) {
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        Core::Logger::warning("ChatController", "Received malformed MessageAck JSON payload.");
        return;
    }

    auto ackOpt = MessageAck::fromJson(doc.object());
    if (!ackOpt.has_value()) {
        Core::Logger::warning("ChatController", "Failed to deserialize MessageAck fields.");
        return;
    }

    const MessageAck &ack = *ackOpt;
    Core::Logger::info("ChatController", QString("Received MessageAck for %1 (status: %2)").arg(ack.messageId).arg(ack.status));

    if (m_outgoingMessages.contains(ack.messageId)) {
        if (m_ackTimers.contains(ack.messageId)) {
            m_ackTimers[ack.messageId]->stop();
            m_ackTimers[ack.messageId]->deleteLater();
            m_ackTimers.remove(ack.messageId);
        }

        MessageState newState = (ack.status == "read") ? MessageState::Read : MessageState::Delivered;
        m_outgoingMessages[ack.messageId].state = newState;
        emit messageStatusChanged(ack.messageId, newState);
    }

    emit messageAckReceived(ack);
}

void ChatController::handleInboundTypingIndicator(const QByteArray &payload) {
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
        return;
    }

    auto typingOpt = TypingIndicator::fromJson(doc.object());
    if (!typingOpt.has_value()) return;

    const TypingIndicator &typing = *typingOpt;
    QString peerId = typing.senderDeviceId;

    if (typing.isTyping) {
        if (!m_peerTypingTimers.contains(peerId)) {
            auto *timer = new QTimer(this);
            timer->setSingleShot(true);
            connect(timer, &QTimer::timeout, this, [this, peerId]() {
                if (m_peerTypingTimers.contains(peerId)) {
                    m_peerTypingTimers[peerId]->deleteLater();
                    m_peerTypingTimers.remove(peerId);
                    emit peerTypingChanged(peerId, false);
                }
            });
            m_peerTypingTimers.insert(peerId, timer);
            timer->start(5000); // 5-second auto-expiry
            emit peerTypingChanged(peerId, true);
        } else {
            // Restart expiry timer
            m_peerTypingTimers[peerId]->start(5000);
        }
    } else {
        if (m_peerTypingTimers.contains(peerId)) {
            m_peerTypingTimers[peerId]->stop();
            m_peerTypingTimers[peerId]->deleteLater();
            m_peerTypingTimers.remove(peerId);
            emit peerTypingChanged(peerId, false);
        }
    }
}

void ChatController::onPeerDisconnected(const QString &peerIp) {
    Q_UNUSED(peerIp);
    onSocketDisconnected();
}

void ChatController::onSocketDisconnected() {
    // Clear all peer typing indicators on disconnect
    auto peerIds = m_peerTypingTimers.keys();
    for (const QString &peerId : peerIds) {
        if (m_peerTypingTimers.contains(peerId)) {
            m_peerTypingTimers[peerId]->stop();
            m_peerTypingTimers[peerId]->deleteLater();
            m_peerTypingTimers.remove(peerId);
            emit peerTypingChanged(peerId, false);
        }
    }
}

} // namespace MeckChat::Protocol
