#pragma once

#include <QObject>
#include <QString>

/**
 * Native Linux WireGuard Tunnel and Interface Manager for MeckChat.
 */
class WireGuardManager : public QObject {
    Q_OBJECT

public:
    explicit WireGuardManager(QObject *parent = nullptr);
    ~WireGuardManager() override = default;

    bool initializeInterface(const QString &interfaceName, const QString &virtualIp);
    void shutdownInterface();
    bool isConnected() const;

signals:
    void tunnelStateChanged(bool connected);
    void errorOccurred(const QString &error);

private:
    QString m_interfaceName;
    QString m_virtualIp;
    bool m_connected{false};
};
