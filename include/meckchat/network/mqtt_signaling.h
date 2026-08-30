#pragma once

#include <QObject>
#include <QString>
#include "meckchat/protocol/models.h"

namespace MeckChat::Network {

class MqttSignalingClient : public QObject {
    Q_OBJECT

public:
    explicit MqttSignalingClient(QObject *parent = nullptr);
    ~MqttSignalingClient() override = default;

    void connectToBroker(const QString &host, int port);
    void disconnectFromBroker();
    bool isConnected() const;

    void publishOnlinePresence(const Protocol::Device &localDevice);
    void publishOfflinePresence(const QString &deviceId);
    void broadcastDiscoveryRequest(const QString &deviceId);

signals:
    void connected();
    void disconnected();
    void deviceDiscovered(const Protocol::Device &device);
    void deviceLeft(const QString &deviceId);
    void discoveryRequested(const QString &senderDeviceId);
    void errorOccurred(const QString &error);

private:
    bool m_connected{false};
};

} // namespace MeckChat::Network
