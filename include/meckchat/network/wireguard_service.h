#pragma once

#include <QObject>
#include <QString>

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
    ~WireGuardService() override = default;

    bool startTunnel(const QString &interfaceName, const QString &virtualIp, const QString &privateKey);
    void stopTunnel();
    TunnelStatus status() const;

    bool addPeer(const QString &peerPublicKey, const QString &peerVirtualIp, const QString &endpoint = QString());
    bool removePeer(const QString &peerPublicKey);

signals:
    void statusChanged(TunnelStatus status);
    void errorOccurred(const QString &error);

private:
    TunnelStatus m_status{TunnelStatus::Down};
    QString m_interfaceName;
    QString m_virtualIp;
};

} // namespace MeckChat::Network
