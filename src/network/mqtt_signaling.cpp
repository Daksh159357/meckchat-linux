#include "meckchat/network/mqtt_signaling.h"
#include "meckchat/core/logger.h"

namespace MeckChat::Network {

MqttSignalingClient::MqttSignalingClient(QObject *parent)
    : QObject(parent) {}

void MqttSignalingClient::connectToBroker(const QString &host, int port) {
    Core::Logger::info("MqttSignaling", QString("Connecting to MQTT Broker: %1:%2").arg(host).arg(port));
    m_connected = true;
    emit connected();
}

void MqttSignalingClient::disconnectFromBroker() {
    if (m_connected) {
        m_connected = false;
        Core::Logger::info("MqttSignaling", "Disconnected from MQTT Broker");
        emit disconnected();
    }
}

bool MqttSignalingClient::isConnected() const {
    return m_connected;
}

void MqttSignalingClient::publishOnlinePresence(const Protocol::Device &localDevice) {
    Core::Logger::debug("MqttSignaling", QString("Publishing online presence for device: %1").arg(localDevice.deviceId));
}

void MqttSignalingClient::publishOfflinePresence(const QString &deviceId) {
    Core::Logger::debug("MqttSignaling", QString("Publishing offline presence for device: %1").arg(deviceId));
}

void MqttSignalingClient::broadcastDiscoveryRequest(const QString &deviceId) {
    Core::Logger::debug("MqttSignaling", QString("Broadcasting discovery request for device: %1").arg(deviceId));
}

} // namespace MeckChat::Network
