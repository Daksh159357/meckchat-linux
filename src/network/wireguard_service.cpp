#include "meckchat/network/wireguard_service.h"
#include "meckchat/core/logger.h"
#include "meckchat/crypto/crypto_provider.h"

namespace MeckChat::Network {

WireGuardService::WireGuardService(QObject *parent)
    : QObject(parent) {}

WireGuardService::~WireGuardService() {
    stopTunnel();
}

bool WireGuardService::isWireGuardSupported() {
    return NetlinkWireGuard::isWireGuardSupported();
}

TunnelStatus WireGuardService::status() const {
    return m_status;
}

QString WireGuardService::interfaceName() const {
    return m_interfaceName;
}

QString WireGuardService::virtualIp() const {
    return m_virtualIp;
}

quint16 WireGuardService::listenPort() const {
    return m_listenPort;
}

QString WireGuardService::lastError() const {
    return m_lastError;
}

void WireGuardService::setStatus(TunnelStatus status) {
    if (m_status != status) {
        m_status = status;
        Core::Logger::info("WireGuardService", QString("Tunnel status changed: %1").arg(static_cast<int>(status)));
        emit statusChanged(m_status);
    }
}

void WireGuardService::setError(const QString &error) {
    m_lastError = error;
    Core::Logger::error("WireGuardService", error);
    setStatus(TunnelStatus::Error);
    emit errorOccurred(error);
}

bool WireGuardService::startTunnel(
    const QString &interfaceName,
    const QString &virtualIp,
    const QString &privateKeyBase64,
    quint16 listenPort
) {
    m_interfaceName = interfaceName.isEmpty() ? "wg-meckchat" : interfaceName;
    m_virtualIp = virtualIp.isEmpty() ? "10.77.0.2" : virtualIp;
    m_listenPort = listenPort == 0 ? 51820 : listenPort;

    setStatus(TunnelStatus::Configuring);

    // 1. Validate Subnet (must be in 10.77.0.0/16)
    if (!NetlinkWireGuard::isSubnetValid(m_virtualIp)) {
        setError(QString("Invalid virtual IP: %1. Must be a valid address within 10.77.0.0/16.").arg(m_virtualIp));
        return false;
    }

    // 2. Validate Private Key if provided
    QByteArray rawPrivKey;
    if (!privateKeyBase64.isEmpty()) {
        if (!NetlinkWireGuard::isValidBase64Key(privateKeyBase64)) {
            setError("Invalid WireGuard private key. Must be a 32-byte Base64 string.");
            return false;
        }
        rawPrivKey = QByteArray::fromBase64(privateKeyBase64.toUtf8());
    }

    // 3. Verify Linux Kernel WireGuard Support
    if (!isWireGuardSupported()) {
        setError("WireGuard kernel module/subsystem is not available in the running Linux kernel.");
        return false;
    }

    QString errStr;

    // 4. Create Interface (RTM_NEWLINK)
    bool existed = NetlinkWireGuard::interfaceExists(m_interfaceName);
    if (!NetlinkWireGuard::createInterface(m_interfaceName, errStr)) {
        Crypto::CryptoProvider::secureCleanse(rawPrivKey);
        setError(QString("Failed to create WireGuard interface '%1': %2").arg(m_interfaceName, errStr));
        return false;
    }
    m_interfaceCreatedByUs = !existed;

    // 5. Configure WireGuard Device (Private Key & Port via Generic Netlink)
    if (!rawPrivKey.isEmpty()) {
        QList<WireGuardPeerConfig> existingPeers = m_peers.values();
        if (!NetlinkWireGuard::setWireGuardDevice(m_interfaceName, rawPrivKey, m_listenPort, existingPeers, errStr)) {
            Crypto::CryptoProvider::secureCleanse(rawPrivKey);
            setError(QString("Failed to configure WireGuard device: %1").arg(errStr));
            return false;
        }
    }
    Crypto::CryptoProvider::secureCleanse(rawPrivKey);

    // 6. Assign Virtual IP address (10.77.x.x/16)
    if (!NetlinkWireGuard::setInterfaceAddress(m_interfaceName, m_virtualIp, 16, errStr)) {
        setError(QString("Failed to assign virtual IP %1/16 to %2: %3").arg(m_virtualIp, m_interfaceName, errStr));
        return false;
    }

    // 7. Bring Interface UP
    if (!NetlinkWireGuard::setInterfaceUp(m_interfaceName, true, errStr)) {
        setError(QString("Failed to bring up interface %1: %2").arg(m_interfaceName, errStr));
        return false;
    }

    setStatus(TunnelStatus::Up);
    Core::Logger::info("WireGuardService", QString("WireGuard tunnel %1 successfully started at %2/16 (Port %3)").arg(m_interfaceName, m_virtualIp).arg(m_listenPort));
    return true;
}

void WireGuardService::stopTunnel() {
    if (m_status == TunnelStatus::Down) return;

    Core::Logger::info("WireGuardService", QString("Stopping WireGuard interface %1").arg(m_interfaceName));
    QString errStr;

    if (NetlinkWireGuard::interfaceExists(m_interfaceName)) {
        NetlinkWireGuard::setInterfaceUp(m_interfaceName, false, errStr);
        if (m_interfaceCreatedByUs) {
            NetlinkWireGuard::deleteInterface(m_interfaceName, errStr);
            m_interfaceCreatedByUs = false;
        }
    }

    m_peers.clear();
    setStatus(TunnelStatus::Down);
}

bool WireGuardService::addPeer(
    const QString &peerPublicKeyBase64,
    const QString &peerVirtualIp,
    const QString &endpoint,
    quint16 persistentKeepalive
) {
    if (!NetlinkWireGuard::isValidBase64Key(peerPublicKeyBase64)) {
        setError("Invalid peer public key. Must be a 32-byte Base64 string.");
        return false;
    }

    if (!NetlinkWireGuard::isSubnetValid(peerVirtualIp)) {
        setError(QString("Invalid peer virtual IP: %1. Must be in 10.77.0.0/16.").arg(peerVirtualIp));
        return false;
    }

    WireGuardPeerConfig peer;
    peer.publicKey = QByteArray::fromBase64(peerPublicKeyBase64.toUtf8());
    peer.endpoint = endpoint;
    peer.persistentKeepalive = persistentKeepalive;

    WireGuardAllowedIp aip;
    aip.address = QHostAddress(peerVirtualIp);
    aip.cidrMask = 32; // Exact peer host route
    peer.allowedIps.append(aip);
    peer.remove = false;

    if (m_status == TunnelStatus::Up) {
        QString errStr;
        QList<WireGuardPeerConfig> peerList = {peer};
        if (!NetlinkWireGuard::setWireGuardDevice(m_interfaceName, QByteArray(), 0, peerList, errStr)) {
            setError(QString("Failed to configure peer %1 in kernel: %2").arg(peerPublicKeyBase64, errStr));
            return false;
        }
    }

    m_peers.insert(peerPublicKeyBase64, peer);
    Core::Logger::info("WireGuardService", QString("Added peer %1 (IP: %2/32, Endpoint: %3)").arg(peerPublicKeyBase64, peerVirtualIp, endpoint));
    emit peerAdded(peerPublicKeyBase64);
    return true;
}

bool WireGuardService::removePeer(const QString &peerPublicKeyBase64) {
    if (!m_peers.contains(peerPublicKeyBase64)) {
        return true; // Already not present
    }

    WireGuardPeerConfig peer;
    peer.publicKey = QByteArray::fromBase64(peerPublicKeyBase64.toUtf8());
    peer.remove = true;

    if (m_status == TunnelStatus::Up) {
        QString errStr;
        QList<WireGuardPeerConfig> peerList = {peer};
        NetlinkWireGuard::setWireGuardDevice(m_interfaceName, QByteArray(), 0, peerList, errStr);
    }

    m_peers.remove(peerPublicKeyBase64);
    Core::Logger::info("WireGuardService", QString("Removed peer %1").arg(peerPublicKeyBase64));
    emit peerRemoved(peerPublicKeyBase64);
    return true;
}

bool WireGuardService::isPeerConfigured(const QString &peerPublicKeyBase64) const {
    return m_peers.contains(peerPublicKeyBase64);
}

QList<WireGuardPeerConfig> WireGuardService::peers() const {
    return m_peers.values();
}

} // namespace MeckChat::Network
