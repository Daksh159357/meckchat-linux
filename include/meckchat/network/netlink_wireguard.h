#pragma once

#include <QString>
#include <QByteArray>
#include <QList>
#include <QHostAddress>
#include <optional>
#include <cstdint>

namespace MeckChat::Network {

struct WireGuardAllowedIp {
    QHostAddress address;
    quint8 cidrMask{32};

    bool operator==(const WireGuardAllowedIp &other) const {
        return address == other.address && cidrMask == other.cidrMask;
    }
};

struct WireGuardPeerConfig {
    QByteArray publicKey; // 32 bytes raw public key
    QString endpoint;     // "IP:PORT" e.g. "192.168.1.50:51820"
    quint16 persistentKeepalive{25};
    QList<WireGuardAllowedIp> allowedIps;
    bool remove{false};
};

struct WireGuardDeviceStatus {
    QString ifName;
    QByteArray publicKey;
    quint16 listenPort{0};
    QList<WireGuardPeerConfig> peers;
};

class NetlinkWireGuard {
public:
    // Check if the Linux kernel has WireGuard support and resolve family ID
    static bool isWireGuardSupported();
    static int resolveWireGuardFamily();

    // Interface lifecycle (via rtnetlink / RTM_NEWLINK / RTM_DELLINK)
    static bool createInterface(const QString &ifName, QString &errorString);
    static bool deleteInterface(const QString &ifName, QString &errorString);
    static bool setInterfaceAddress(const QString &ifName, const QString &virtualIp, int prefixLen, QString &errorString);
    static bool setInterfaceUp(const QString &ifName, bool up, QString &errorString);
    static bool interfaceExists(const QString &ifName);

    // Device & Peer configuration (via Generic Netlink / WG_CMD_SET_DEVICE / WG_CMD_GET_DEVICE)
    static bool setWireGuardDevice(
        const QString &ifName,
        const QByteArray &privateKey,
        quint16 listenPort,
        const QList<WireGuardPeerConfig> &peers,
        QString &errorString
    );

    static std::optional<WireGuardDeviceStatus> getWireGuardDevice(
        const QString &ifName,
        QString &errorString
    );

    // Validation Helpers
    static bool isSubnetValid(const QString &ip, const QString &expectedSubnet = "10.77.0.0", int prefixLen = 16);
    static bool isValidBase64Key(const QString &keyBase64);
    static bool parseEndpoint(const QString &endpointStr, QHostAddress &addr, quint16 &port);
};

} // namespace MeckChat::Network
