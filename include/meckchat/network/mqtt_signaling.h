#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QPair>
#include <QSslSocket>
#include <QSslError>
#include <QTimer>
#include "meckchat/protocol/models.h"

namespace MeckChat::Network {

enum class MqttState {
    Disconnected,
    Connecting,
    Connected,
    Subscribing,
    Ready
};

class MqttSignalingClient : public QObject {
    Q_OBJECT

public:
    explicit MqttSignalingClient(QObject *parent = nullptr);
    ~MqttSignalingClient() override;

    void connectToBroker(const QString &host = QString(), int port = 0);
    void disconnectFromBroker();
    bool isConnected() const;
    MqttState state() const;

    void publishOnlinePresence(const Protocol::Device &localDevice);
    void publishOfflinePresence(const QString &deviceId);
    void broadcastDiscoveryRequest(const QString &deviceId);

    // Topic & Device ID Validation
    static bool isValidDeviceId(const QString &deviceId);
    static bool isValidTopic(const QString &topic);

    // Protocol Codec Helpers (Public for testing)
    static QByteArray encodeVariableLength(quint32 length);
    static bool decodeVariableLength(const QByteArray &buf, int offset, quint32 &length, int &bytesConsumed);
    static QByteArray encodeUtf8String(const QString &str);
    static bool decodeUtf8String(const QByteArray &buf, int &offset, QString &str);

signals:
    void connected();
    void disconnected();
    void stateChanged(MqttState newState);
    void deviceDiscovered(const Protocol::Device &device);
    void deviceLeft(const QString &deviceId);
    void discoveryRequested(const QString &senderDeviceId);
    void errorOccurred(const QString &error);

private slots:
    void onSocketConnected();
    void onSocketEncrypted();
    void onSocketReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError> &errors);
    void onSocketDisconnected();
    void onPingTimer();
    void onPingTimeout();
    void onReconnectTimer();

private:
    void setState(MqttState state);
    void scheduleReconnect();
    void resetReconnect();

    // MQTT Protocol Encoding & Sending
    void sendConnect();
    void sendSubscribe(const QList<QPair<QString, quint8>> &topics);
    void sendPublish(const QString &topic, const QByteArray &payload, quint8 qos, bool retain);
    void sendPuback(quint16 packetId);
    void sendPingreq();
    void sendDisconnect();

    // MQTT Protocol Decoding & Dispatch
    void processIncomingData();
    void handleConnack(const QByteArray &data);
    void handlePublish(quint8 header, const QByteArray &data);
    void handleSuback(const QByteArray &data);
    void handlePuback(const QByteArray &data);
    void handlePingresp();

    void processReceivedPayload(const QString &topic, const QByteArray &payload);
    quint16 nextPacketId();

    QSslSocket *m_socket{nullptr};
    QTimer *m_pingTimer{nullptr};
    QTimer *m_pingTimeoutTimer{nullptr};
    QTimer *m_reconnectTimer{nullptr};

    QByteArray m_receiveBuffer;
    MqttState m_state{MqttState::Disconnected};

    QString m_host;
    int m_port{8883};
    quint16 m_nextPacketId{1};
    int m_reconnectAttempts{0};
    bool m_intentionalDisconnect{false};
    bool m_pingOutstanding{false};

    // Constants
    static constexpr quint32 MAX_SIGNALING_PAYLOAD_SIZE = 65536; // 64 KB limit
    static constexpr int DEFAULT_KEEPALIVE_SECONDS = 60;
    static constexpr int PING_INTERVAL_MS = 25000;
    static constexpr int PING_TIMEOUT_MS = 10000;
    static constexpr int INITIAL_RECONNECT_MS = 1000;
    static constexpr int MAX_RECONNECT_MS = 60000;
};

} // namespace MeckChat::Network
