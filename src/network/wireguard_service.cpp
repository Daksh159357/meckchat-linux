#include "meckchat/network/wireguard_service.h"
#include "meckchat/core/logger.h"

namespace MeckChat::Network {

WireGuardService::WireGuardService(QObject *parent)
    : QObject(parent) {}

bool WireGuardService::startTunnel(const QString &interfaceName, const QString &virtualIp, const QString &/*privateKey*/) {
    Core::Logger::info("WireGuardService", QString("Starting WireGuard interface %1 at IP %2").arg(interfaceName, virtualIp));
    m_interfaceName = interfaceName;
    m_virtualIp = virtualIp;
    m_status = TunnelStatus::Up;
    emit statusChanged(m_status);
    return true;
}

void WireGuardService::stopTunnel() {
    if (m_status != TunnelStatus::Down) {
        Core::Logger::info("WireGuardService", "Stopping WireGuard interface");
        m_status = TunnelStatus::Down;
        emit statusChanged(m_status);
    }
}

TunnelStatus WireGuardService::status() const {
    return m_status;
}

bool WireGuardService::addPeer(const QString &peerPublicKey, const QString &peerVirtualIp, const QString &endpoint) {
    Core::Logger::info("WireGuardService", QString("Configured peer %1 (IP: %2, Endpoint: %3)")
                                              .arg(peerPublicKey, peerVirtualIp, endpoint));
    return true;
}

bool WireGuardService::removePeer(const QString &peerPublicKey) {
    Core::Logger::info("WireGuardService", QString("Removed peer %1").arg(peerPublicKey));
    return true;
}

} // namespace MeckChat::Network
