#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QMap>
#include "meckchat/network/netlink_wireguard.h"

namespace MeckChat::Network {

enum class TunnelStatus {
    Down,
    Configuring,
    Up,
    Error
};

class WireGuardService : public QObject {
    Q_OBJECT

public:
    explicit WireGuardService(QObject *parent = nullptr);
    ~WireGuardService() override;

    // Lifecycle
    bool startTunnel(
        const QString &interfaceName = "wg-meckchat",
        const QString &virtualIp = "10.77.0.2",
        const QString &privateKeyBase64 = QString(),
        quint16 listenPort = 51820
    );
    void stopTunnel();
    TunnelStatus status() const;

    QString interfaceName() const;
    QString virtualIp() const;
    quint16 listenPort() const;
    QString lastError() const;

    // Peer Management
    bool addPeer(
        const QString &peerPublicKeyBase64,
        const QString &peerVirtualIp,
        const QString &endpoint = QString(),
        quint16 persistentKeepalive = 25
    );
    bool removePeer(const QString &peerPublicKeyBase64);
    bool isPeerConfigured(const QString &peerPublicKeyBase64) const;
    QList<WireGuardPeerConfig> peers() const;

    // Capability check
    static bool isWireGuardSupported();

signals:
    void statusChanged(TunnelStatus status);
    void errorOccurred(const QString &error);
    void peerAdded(const QString &peerPublicKey);
    void peerRemoved(const QString &peerPublicKey);

private:
    void setStatus(TunnelStatus status);
    void setError(const QString &error);

    TunnelStatus m_status{TunnelStatus::Down};
    QString m_interfaceName{"wg-meckchat"};
    QString m_virtualIp{"10.77.0.2"};
    quint16 m_listenPort{51820};
    QString m_lastError;

    QMap<QString, WireGuardPeerConfig> m_peers;
    bool m_interfaceCreatedByUs{false};
};

} // namespace MeckChat::Network
