#include "wireguard_manager.h"

WireGuardManager::WireGuardManager(QObject *parent)
    : QObject(parent) {}

bool WireGuardManager::initializeInterface(const QString &interfaceName, const QString &virtualIp) {
    m_interfaceName = interfaceName;
    m_virtualIp = virtualIp;
    m_connected = true;
    emit tunnelStateChanged(m_connected);
    return true;
}

void WireGuardManager::shutdownInterface() {
    if (m_connected) {
        m_connected = false;
        emit tunnelStateChanged(m_connected);
    }
}

bool WireGuardManager::isConnected() const {
    return m_connected;
}
