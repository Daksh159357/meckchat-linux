#include "meckchat/network/mqtt_signaling.h"
#include "meckchat/core/logger.h"
#include "meckchat/core/config.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QtEndian>
#include <QSslConfiguration>
#include <QUuid>

namespace MeckChat::Network {

// MQTT 3.1.1 Fixed Header Packet Types
namespace PacketType {
    constexpr quint8 CONNECT     = 0x10;
    constexpr quint8 CONNACK     = 0x20;
    constexpr quint8 PUBLISH     = 0x30;
    constexpr quint8 PUBACK      = 0x40;
    constexpr quint8 SUBSCRIBE   = 0x82; // Bits 1-3 fixed: 0010
    constexpr quint8 SUBACK      = 0x90;
    constexpr quint8 PINGREQ     = 0xC0;
    constexpr quint8 PINGRESP    = 0xD0;
    constexpr quint8 DISCONNECT  = 0xE0;
}

MqttSignalingClient::MqttSignalingClient(QObject *parent)
    : QObject(parent),
      m_socket(new QSslSocket(this)),
      m_pingTimer(new QTimer(this)),
      m_pingTimeoutTimer(new QTimer(this)),
      m_reconnectTimer(new QTimer(this)) {

    m_pingTimer->setInterval(PING_INTERVAL_MS);
    m_pingTimeoutTimer->setInterval(PING_TIMEOUT_MS);
    m_pingTimeoutTimer->setSingleShot(true);
    m_reconnectTimer->setSingleShot(true);

    connect(m_socket, &QSslSocket::connected, this, &MqttSignalingClient::onSocketConnected);
    connect(m_socket, &QSslSocket::encrypted, this, &MqttSignalingClient::onSocketEncrypted);
    connect(m_socket, &QSslSocket::readyRead, this, &MqttSignalingClient::onSocketReadyRead);
    connect(m_socket, &QSslSocket::errorOccurred, this, &MqttSignalingClient::onSocketError);
    connect(m_socket, &QSslSocket::sslErrors, this, &MqttSignalingClient::onSslErrors);
    connect(m_socket, &QSslSocket::disconnected, this, &MqttSignalingClient::onSocketDisconnected);

    connect(m_pingTimer, &QTimer::timeout, this, &MqttSignalingClient::onPingTimer);
    connect(m_pingTimeoutTimer, &QTimer::timeout, this, &MqttSignalingClient::onPingTimeout);
    connect(m_reconnectTimer, &QTimer::timeout, this, &MqttSignalingClient::onReconnectTimer);
}

MqttSignalingClient::~MqttSignalingClient() {
    disconnectFromBroker();
}

bool MqttSignalingClient::isValidDeviceId(const QString &deviceId) {
    if (deviceId.length() < 4 || deviceId.length() > 128) {
        return false;
    }
    if (!deviceId.startsWith("mc_")) {
        return false;
    }
    // Only allow alphanumeric, underscore, hyphen, and dot
    static const QRegularExpression regex("^[a-zA-Z0-9_\\-\\.]+$");
    return regex.match(deviceId).hasMatch();
}

bool MqttSignalingClient::isValidTopic(const QString &topic) {
    if (topic.isEmpty() || topic.length() > 256) {
        return false;
    }
    // Check for null characters or non-printable ASCII
    for (const QChar &ch : topic) {
        if (ch.isNull() || ch.toLatin1() < 32 || ch.toLatin1() > 126) {
            return false;
        }
    }
    return true;
}

QByteArray MqttSignalingClient::encodeVariableLength(quint32 length) {
    QByteArray encoded;
    do {
        quint8 byte = length % 128;
        length /= 128;
        if (length > 0) {
            byte |= 0x80;
        }
        encoded.append(static_cast<char>(byte));
    } while (length > 0 && encoded.size() < 4);
    return encoded;
}

bool MqttSignalingClient::decodeVariableLength(const QByteArray &buf, int offset, quint32 &length, int &bytesConsumed) {
    quint32 multiplier = 1;
    quint32 value = 0;
    bytesConsumed = 0;

    while (offset + bytesConsumed < buf.size()) {
        quint8 byte = static_cast<quint8>(buf.at(offset + bytesConsumed));
        bytesConsumed++;
        value += (byte & 127) * multiplier;
        if (multiplier > 128 * 128 * 128) {
            // Malformed variable length
            return false;
        }
        multiplier *= 128;
        if ((byte & 128) == 0) {
            length = value;
            return true;
        }
        if (bytesConsumed >= 4) {
            return false;
        }
    }
    return false; // Incomplete
}

QByteArray MqttSignalingClient::encodeUtf8String(const QString &str) {
    QByteArray utf8 = str.toUtf8();
    quint16 len = static_cast<quint16>(utf8.size());
    QByteArray out;
    out.append(static_cast<char>((len >> 8) & 0xFF));
    out.append(static_cast<char>(len & 0xFF));
    out.append(utf8);
    return out;
}

bool MqttSignalingClient::decodeUtf8String(const QByteArray &buf, int &offset, QString &str) {
    if (offset + 2 > buf.size()) {
        return false;
    }
    quint16 len = (static_cast<quint8>(buf[offset]) << 8) | static_cast<quint8>(buf[offset + 1]);
    offset += 2;
    if (offset + len > buf.size()) {
        return false;
    }
    str = QString::fromUtf8(buf.mid(offset, len));
    offset += len;
    return true;
}

quint16 MqttSignalingClient::nextPacketId() {
    if (m_nextPacketId == 0) m_nextPacketId = 1;
    return m_nextPacketId++;
}

void MqttSignalingClient::setState(MqttState state) {
    if (m_state != state) {
        m_state = state;
        emit stateChanged(m_state);
    }
}

bool MqttSignalingClient::isConnected() const {
    return m_state == MqttState::Ready;
}

MqttState MqttSignalingClient::state() const {
    return m_state;
}

void MqttSignalingClient::connectToBroker(const QString &host, int port) {
    m_host = host.isEmpty() ? Core::AppConfig::instance().mqttBrokerHost() : host;
    m_port = port == 0 ? Core::AppConfig::instance().mqttBrokerPort() : port;
    m_intentionalDisconnect = false;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }

    m_receiveBuffer.clear();
    setState(MqttState::Connecting);

    Core::Logger::info("MqttSignaling", QString("Initiating TLS connection to MQTT Broker: %1:%2").arg(m_host).arg(m_port));

    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    m_socket->setSslConfiguration(sslConfig);
    m_socket->setPeerVerifyMode(QSslSocket::VerifyPeer);
    m_socket->setPeerVerifyName(m_host);
    m_socket->connectToHostEncrypted(m_host, static_cast<quint16>(m_port));
}

void MqttSignalingClient::disconnectFromBroker() {
    m_intentionalDisconnect = true;
    m_reconnectTimer->stop();
    m_pingTimer->stop();
    m_pingTimeoutTimer->stop();

    if (m_state == MqttState::Ready) {
        QString localId = Core::AppConfig::instance().deviceId();
        if (isValidDeviceId(localId)) {
            publishOfflinePresence(localId);
        }
        sendDisconnect();
    }

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }

    setState(MqttState::Disconnected);
    m_receiveBuffer.clear();
}

void MqttSignalingClient::onSocketConnected() {
    Core::Logger::debug("MqttSignaling", "TCP socket connected, starting TLS handshake...");
}

void MqttSignalingClient::onSocketEncrypted() {
    Core::Logger::info("MqttSignaling", QString("TLS handshake successful with %1. Peer certificate verified.").arg(m_host));
    setState(MqttState::Connected);
    sendConnect();
}

void MqttSignalingClient::onSocketReadyRead() {
    m_receiveBuffer.append(m_socket->readAll());
    processIncomingData();
}

void MqttSignalingClient::onSocketError(QAbstractSocket::SocketError error) {
    Q_UNUSED(error);
    QString errorMsg = m_socket->errorString();
    Core::Logger::warning("MqttSignaling", QString("Socket error: %1").arg(errorMsg));
    emit errorOccurred(errorMsg);
}

void MqttSignalingClient::onSslErrors(const QList<QSslError> &errors) {
    for (const auto &err : errors) {
        Core::Logger::error("MqttSignaling", QString("TLS Verification Error: %1").arg(err.errorString()));
    }
    // Strict TLS policy: NEVER call ignoreSslErrors()
    m_socket->abort();
    emit errorOccurred("TLS certificate verification failed.");
}

void MqttSignalingClient::onSocketDisconnected() {
    m_pingTimer->stop();
    m_pingTimeoutTimer->stop();
    m_pingOutstanding = false;
    m_receiveBuffer.clear();

    Core::Logger::info("MqttSignaling", "MQTT Socket disconnected.");
    setState(MqttState::Disconnected);
    emit disconnected();

    if (!m_intentionalDisconnect) {
        scheduleReconnect();
    }
}

void MqttSignalingClient::scheduleReconnect() {
    if (m_reconnectTimer->isActive()) return;

    // Exponential backoff with upper bound and jitter
    int backoff = INITIAL_RECONNECT_MS * (1 << std::min(m_reconnectAttempts, 6));
    backoff = std::min(backoff, MAX_RECONNECT_MS);
    int jitter = QRandomGenerator::global()->bounded(200) - 100;
    int delay = std::max(1000, backoff + jitter);

    m_reconnectAttempts++;
    Core::Logger::info("MqttSignaling", QString("Scheduling reconnect attempt %1 in %2 ms").arg(m_reconnectAttempts).arg(delay));
    m_reconnectTimer->start(delay);
}

void MqttSignalingClient::resetReconnect() {
    m_reconnectAttempts = 0;
    m_reconnectTimer->stop();
}

void MqttSignalingClient::onReconnectTimer() {
    if (m_intentionalDisconnect) return;
    Core::Logger::info("MqttSignaling", "Attempting MQTT reconnection...");
    connectToBroker(m_host, m_port);
}

void MqttSignalingClient::sendConnect() {
    QString localId = Core::AppConfig::instance().deviceId();
    if (!isValidDeviceId(localId)) {
        localId = "mc_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
        Core::AppConfig::instance().setDeviceId(localId);
    }

    // Protocol Name & Level (MQTT 3.1.1)
    QByteArray variableHeader;
    variableHeader.append(encodeUtf8String("MQTT"));
    variableHeader.append(static_cast<char>(0x04)); // Protocol Level 4 (3.1.1)

    // Connect Flags: Clean Session (0x02) | Will Flag (0x04) | Will QoS 1 (0x08) | Will Retain (0x20) = 0x2E
    quint8 connectFlags = 0x02 | 0x04 | 0x08 | 0x20;
    variableHeader.append(static_cast<char>(connectFlags));

    // Keep Alive (60 seconds)
    quint16 keepAlive = DEFAULT_KEEPALIVE_SECONDS;
    variableHeader.append(static_cast<char>((keepAlive >> 8) & 0xFF));
    variableHeader.append(static_cast<char>(keepAlive & 0xFF));

    // Payload: Client ID, Will Topic, Will Message
    QByteArray payload;
    payload.append(encodeUtf8String(localId));

    QString willTopic = QString("meckchat/v1/presence/offline/%1").arg(localId);
    payload.append(encodeUtf8String(willTopic));

    QJsonObject willJson;
    willJson["type"] = "presence_offline";
    willJson["device_id"] = localId;
    QByteArray willPayload = QJsonDocument(willJson).toJson(QJsonDocument::Compact);
    quint16 willLen = static_cast<quint16>(willPayload.size());
    payload.append(static_cast<char>((willLen >> 8) & 0xFF));
    payload.append(static_cast<char>(willLen & 0xFF));
    payload.append(willPayload);

    QByteArray packet;
    packet.append(static_cast<char>(PacketType::CONNECT));
    packet.append(encodeVariableLength(variableHeader.size() + payload.size()));
    packet.append(variableHeader);
    packet.append(payload);

    m_socket->write(packet);
    m_socket->flush();
    Core::Logger::debug("MqttSignaling", QString("Sent MQTT CONNECT for client %1").arg(localId));
}

void MqttSignalingClient::sendSubscribe(const QList<QPair<QString, quint8>> &topics) {
    if (topics.isEmpty() || m_socket->state() != QAbstractSocket::ConnectedState) return;

    quint16 packetId = nextPacketId();
    QByteArray variableHeader;
    variableHeader.append(static_cast<char>((packetId >> 8) & 0xFF));
    variableHeader.append(static_cast<char>(packetId & 0xFF));

    QByteArray payload;
    for (const auto &item : topics) {
        payload.append(encodeUtf8String(item.first));
        payload.append(static_cast<char>(item.second & 0x03));
    }

    QByteArray packet;
    packet.append(static_cast<char>(PacketType::SUBSCRIBE));
    packet.append(encodeVariableLength(variableHeader.size() + payload.size()));
    packet.append(variableHeader);
    packet.append(payload);

    m_socket->write(packet);
    m_socket->flush();
    Core::Logger::debug("MqttSignaling", QString("Sent MQTT SUBSCRIBE with %1 topic filters (PacketID %2)").arg(topics.size()).arg(packetId));
}

void MqttSignalingClient::sendPublish(const QString &topic, const QByteArray &payload, quint8 qos, bool retain) {
    if (!isValidTopic(topic) || m_socket->state() != QAbstractSocket::ConnectedState) return;

    quint8 fixedHeader = PacketType::PUBLISH;
    if (retain) fixedHeader |= 0x01;
    fixedHeader |= ((qos & 0x03) << 1);

    QByteArray variableHeader;
    variableHeader.append(encodeUtf8String(topic));
    if (qos > 0) {
        quint16 packetId = nextPacketId();
        variableHeader.append(static_cast<char>((packetId >> 8) & 0xFF));
        variableHeader.append(static_cast<char>(packetId & 0xFF));
    }

    QByteArray packet;
    packet.append(static_cast<char>(fixedHeader));
    packet.append(encodeVariableLength(variableHeader.size() + payload.size()));
    packet.append(variableHeader);
    packet.append(payload);

    m_socket->write(packet);
    m_socket->flush();
    Core::Logger::debug("MqttSignaling", QString("Sent MQTT PUBLISH to '%1' (QoS %2, Retain %3, Size %4 bytes)").arg(topic).arg(qos).arg(retain).arg(payload.size()));
}

void MqttSignalingClient::sendPuback(quint16 packetId) {
    QByteArray packet;
    packet.append(static_cast<char>(PacketType::PUBACK));
    packet.append(encodeVariableLength(2));
    packet.append(static_cast<char>((packetId >> 8) & 0xFF));
    packet.append(static_cast<char>(packetId & 0xFF));
    m_socket->write(packet);
    m_socket->flush();
}

void MqttSignalingClient::sendPingreq() {
    QByteArray packet;
    packet.append(static_cast<char>(PacketType::PINGREQ));
    packet.append(static_cast<char>(0x00));
    m_socket->write(packet);
    m_socket->flush();
}

void MqttSignalingClient::sendDisconnect() {
    QByteArray packet;
    packet.append(static_cast<char>(PacketType::DISCONNECT));
    packet.append(static_cast<char>(0x00));
    m_socket->write(packet);
    m_socket->flush();
    Core::Logger::debug("MqttSignaling", "Sent MQTT DISCONNECT");
}

void MqttSignalingClient::onPingTimer() {
    if (m_state == MqttState::Ready) {
        m_pingOutstanding = true;
        m_pingTimeoutTimer->start();
        sendPingreq();
    }
}

void MqttSignalingClient::onPingTimeout() {
    if (m_pingOutstanding) {
        Core::Logger::warning("MqttSignaling", "MQTT keepalive PING timeout. Reconnecting...");
        m_socket->abort();
    }
}

void MqttSignalingClient::processIncomingData() {
    while (!m_receiveBuffer.isEmpty()) {
        if (m_receiveBuffer.size() < 2) {
            return; // Need at least header + 1 byte remaining length
        }

        quint8 header = static_cast<quint8>(m_receiveBuffer.at(0));
        quint32 remainingLength = 0;
        int bytesConsumed = 0;

        if (!decodeVariableLength(m_receiveBuffer, 1, remainingLength, bytesConsumed)) {
            if (m_receiveBuffer.size() > 5) {
                Core::Logger::error("MqttSignaling", "Corrupted MQTT stream framing. Resetting connection.");
                m_socket->abort();
            }
            return; // Wait for more data
        }

        int totalFrameSize = 1 + bytesConsumed + remainingLength;
        if (m_receiveBuffer.size() < totalFrameSize) {
            return; // Wait for complete frame
        }

        QByteArray framePayload = m_receiveBuffer.mid(1 + bytesConsumed, remainingLength);
        m_receiveBuffer.remove(0, totalFrameSize);

        quint8 packetType = header & 0xF0;
        switch (packetType) {
            case PacketType::CONNACK:
                handleConnack(framePayload);
                break;
            case PacketType::PUBLISH:
                handlePublish(header, framePayload);
                break;
            case PacketType::PUBACK:
                handlePuback(framePayload);
                break;
            case PacketType::SUBACK:
                handleSuback(framePayload);
                break;
            case PacketType::PINGRESP:
                handlePingresp();
                break;
            default:
                Core::Logger::debug("MqttSignaling", QString("Received unhandled MQTT packet type: 0x%1").arg(packetType, 2, 16, QChar('0')));
                break;
        }
    }
}

void MqttSignalingClient::handleConnack(const QByteArray &data) {
    if (data.size() < 2) {
        Core::Logger::error("MqttSignaling", "Malformed CONNACK frame.");
        m_socket->abort();
        return;
    }

    quint8 returnCode = static_cast<quint8>(data.at(1));
    if (returnCode != 0) {
        Core::Logger::error("MqttSignaling", QString("MQTT broker rejected connection with code: %1").arg(returnCode));
        m_socket->abort();
        emit errorOccurred(QString("Broker connection rejected with code %1").arg(returnCode));
        return;
    }

    Core::Logger::info("MqttSignaling", "MQTT CONNACK received: Connection Accepted.");
    setState(MqttState::Subscribing);

    // Canonical MeckChat Subscriptions (QoS 1)
    QList<QPair<QString, quint8>> subscriptions = {
        {"meckchat/v1/presence/online/+", 1},
        {"meckchat/v1/presence/offline/+", 1},
        {"meckchat/v1/discovery", 1}
    };
    sendSubscribe(subscriptions);
    m_pingTimer->start();
}

void MqttSignalingClient::handleSuback(const QByteArray &data) {
    if (data.size() < 3) {
        Core::Logger::error("MqttSignaling", "Malformed SUBACK frame.");
        return;
    }

    Core::Logger::info("MqttSignaling", "MQTT SUBACK received: Subscriptions active.");
    setState(MqttState::Ready);
    resetReconnect();
    emit connected();

    // Publish our online presence
    Protocol::Device localDevice;
    localDevice.deviceId = Core::AppConfig::instance().deviceId();
    localDevice.displayName = Core::AppConfig::instance().displayName();
    localDevice.platform = Protocol::Platform::Linux;
    localDevice.isOnline = true;
    publishOnlinePresence(localDevice);
}

void MqttSignalingClient::handlePuback(const QByteArray &data) {
    if (data.size() >= 2) {
        quint16 packetId = (static_cast<quint8>(data.at(0)) << 8) | static_cast<quint8>(data.at(1));
        Core::Logger::debug("MqttSignaling", QString("Received PUBACK for PacketID %1").arg(packetId));
    }
}

void MqttSignalingClient::handlePingresp() {
    m_pingOutstanding = false;
    m_pingTimeoutTimer->stop();
    Core::Logger::debug("MqttSignaling", "Received MQTT PINGRESP");
}

void MqttSignalingClient::handlePublish(quint8 header, const QByteArray &data) {
    quint8 qos = (header >> 1) & 0x03;
    int offset = 0;
    QString topic;

    if (!decodeUtf8String(data, offset, topic)) {
        Core::Logger::warning("MqttSignaling", "Malformed PUBLISH topic string.");
        return;
    }

    quint16 packetId = 0;
    if (qos > 0) {
        if (offset + 2 > data.size()) {
            Core::Logger::warning("MqttSignaling", "Malformed PUBLISH packet ID.");
            return;
        }
        packetId = (static_cast<quint8>(data.at(offset)) << 8) | static_cast<quint8>(data.at(offset + 1));
        offset += 2;
        sendPuback(packetId);
    }

    QByteArray payload = data.mid(offset);
    if (payload.size() > static_cast<int>(MAX_SIGNALING_PAYLOAD_SIZE)) {
        Core::Logger::warning("MqttSignaling", QString("Rejected oversized payload on '%1' (%2 bytes)").arg(topic).arg(payload.size()));
        return;
    }

    processReceivedPayload(topic, payload);
}

void MqttSignalingClient::processReceivedPayload(const QString &topic, const QByteArray &payload) {
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        Core::Logger::warning("MqttSignaling", QString("Invalid JSON payload received on topic '%1': %2").arg(topic).arg(parseError.errorString()));
        return;
    }

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    QString deviceId = obj["device_id"].toString();
    QString myDeviceId = Core::AppConfig::instance().deviceId();

    if (!isValidDeviceId(deviceId)) {
        Core::Logger::warning("MqttSignaling", QString("Rejected payload with invalid deviceId: '%1'").arg(deviceId));
        return;
    }

    // Ignore echoes of our own messages
    if (deviceId == myDeviceId) {
        return;
    }

    if (topic.startsWith("meckchat/v1/presence/online/")) {
        auto devOpt = Protocol::Device::fromPresenceJson(obj);
        if (devOpt.has_value() && devOpt->deviceId == deviceId) {
            Core::Logger::info("MqttSignaling", QString("Peer discovered online: %1 (%2, %3)").arg(devOpt->displayName).arg(devOpt->deviceId).arg(Protocol::platformToString(devOpt->platform)));
            emit deviceDiscovered(*devOpt);
        } else {
            Core::Logger::warning("MqttSignaling", "Malformed presence_online payload.");
        }
    } else if (topic.startsWith("meckchat/v1/presence/offline/")) {
        Core::Logger::info("MqttSignaling", QString("Peer went offline: %1").arg(deviceId));
        emit deviceLeft(deviceId);
    } else if (topic == "meckchat/v1/discovery") {
        auto reqOpt = Protocol::DiscoveryRequest::fromJson(obj);
        if (reqOpt.has_value() && reqOpt->deviceId == deviceId) {
            Core::Logger::info("MqttSignaling", QString("Discovery requested by peer: %1").arg(deviceId));
            emit discoveryRequested(deviceId);

            // Respond to discovery by announcing our presence
            Protocol::Device localDevice;
            localDevice.deviceId = myDeviceId;
            localDevice.displayName = Core::AppConfig::instance().displayName();
            localDevice.platform = Protocol::Platform::Linux;
            localDevice.isOnline = true;
            publishOnlinePresence(localDevice);
        }
    } else {
        Core::Logger::debug("MqttSignaling", QString("Unhandled topic: %1").arg(topic));
    }
}

void MqttSignalingClient::publishOnlinePresence(const Protocol::Device &localDevice) {
    if (!isValidDeviceId(localDevice.deviceId)) return;

    QString topic = QString("meckchat/v1/presence/online/%1").arg(localDevice.deviceId);
    QJsonObject json = localDevice.toPresenceOnlineJson();
    QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);

    // QoS 1, Retain = true
    sendPublish(topic, payload, 1, true);
    Core::Logger::info("MqttSignaling", QString("Published online presence for device %1").arg(localDevice.deviceId));
}

void MqttSignalingClient::publishOfflinePresence(const QString &deviceId) {
    if (!isValidDeviceId(deviceId)) return;

    QString topic = QString("meckchat/v1/presence/offline/%1").arg(deviceId);
    QJsonObject json;
    json["type"] = "presence_offline";
    json["device_id"] = deviceId;
    QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);

    // QoS 1, Retain = true
    sendPublish(topic, payload, 1, true);
    Core::Logger::info("MqttSignaling", QString("Published offline presence for device %1").arg(deviceId));
}

void MqttSignalingClient::broadcastDiscoveryRequest(const QString &deviceId) {
    if (!isValidDeviceId(deviceId)) return;

    QString topic = "meckchat/v1/discovery";
    Protocol::DiscoveryRequest req;
    req.deviceId = deviceId;
    req.timestamp = QDateTime::currentSecsSinceEpoch();
    QByteArray payload = QJsonDocument(req.toJson()).toJson(QJsonDocument::Compact);

    // QoS 1, Retain = false
    sendPublish(topic, payload, 1, false);
    Core::Logger::info("MqttSignaling", QString("Broadcasted discovery request for device %1").arg(deviceId));
}

} // namespace MeckChat::Network
