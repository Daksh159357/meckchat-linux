#include "meckchat/network/netlink_wireguard.h"
#include "meckchat/core/logger.h"
#include "meckchat/crypto/crypto_provider.h"

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/wireguard.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <vector>

namespace MeckChat::Network {

namespace {

// Helper class for assembling Netlink buffers with proper 4-byte alignment
class NetlinkBuffer {
public:
    NetlinkBuffer(size_t initialCapacity = 1024) {
        m_buf.reserve(initialCapacity);
    }

    void appendRaw(const void *data, size_t len) {
        if (!data || len == 0) return;
        const char *p = reinterpret_cast<const char*>(data);
        m_buf.insert(m_buf.end(), p, p + len);
    }

    template<typename T>
    void append(const T &val) {
        appendRaw(&val, sizeof(T));
    }

    void putAttr(uint16_t type, const void *data, size_t len) {
        uint16_t attrLen = NLA_HDRLEN + len;
        struct nlattr nla;
        nla.nla_type = type;
        nla.nla_len = attrLen;
        append(nla);
        if (data && len > 0) {
            appendRaw(data, len);
        }
        // Pad to 4-byte alignment (NLA_ALIGN)
        size_t pad = NLA_ALIGN(attrLen) - attrLen;
        for (size_t i = 0; i < pad; ++i) {
            m_buf.push_back(0);
        }
    }

    void putAttrU8(uint16_t type, uint8_t val) {
        putAttr(type, &val, sizeof(uint8_t));
    }

    void putAttrU16(uint16_t type, uint16_t val) {
        putAttr(type, &val, sizeof(uint16_t));
    }

    void putAttrU32(uint16_t type, uint32_t val) {
        putAttr(type, &val, sizeof(uint32_t));
    }

    void putAttrStr(uint16_t type, const QString &str) {
        QByteArray utf8 = str.toUtf8();
        putAttr(type, utf8.constData(), utf8.size() + 1); // null-terminated
    }

    void putAttrRaw(uint16_t type, const QByteArray &data) {
        putAttr(type, data.constData(), static_cast<size_t>(data.size()));
    }

    size_t startNested(uint16_t type) {
        size_t offset = m_buf.size();
        struct nlattr nla;
        nla.nla_type = NLA_F_NESTED | type;
        nla.nla_len = 0; // will be updated at endNested
        append(nla);
        return offset;
    }

    void endNested(size_t offset) {
        uint16_t len = static_cast<uint16_t>(m_buf.size() - offset);
        struct nlattr *nla = reinterpret_cast<struct nlattr*>(m_buf.data() + offset);
        nla->nla_len = len;
        // Align overall nested buffer
        size_t pad = NLA_ALIGN(len) - len;
        for (size_t i = 0; i < pad; ++i) {
            m_buf.push_back(0);
        }
    }

    uint8_t* data() { return m_buf.data(); }
    const uint8_t* data() const { return m_buf.data(); }
    size_t size() const { return m_buf.size(); }

private:
    std::vector<uint8_t> m_buf;
};

// Send Netlink request and wait for ACK or Error response
int sendNetlinkRequest(int nlSocket, NetlinkBuffer &buffer, QString &errorString) {
    struct nlmsghdr *nlh = reinterpret_cast<struct nlmsghdr*>(buffer.data());
    nlh->nlmsg_len = static_cast<uint32_t>(buffer.size());

    struct sockaddr_nl sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;

    if (sendto(nlSocket, buffer.data(), buffer.size(), 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        int err = errno;
        errorString = QString("Failed to send Netlink message: %1").arg(std::strerror(err));
        return -err;
    }

    uint8_t respBuf[4096];
    ssize_t len = recv(nlSocket, respBuf, sizeof(respBuf), 0);
    if (len < 0) {
        int err = errno;
        errorString = QString("Failed to receive Netlink response: %1").arg(std::strerror(err));
        return -err;
    }

    struct nlmsghdr *respHdr = reinterpret_cast<struct nlmsghdr*>(respBuf);
    if (respHdr->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *errPayload = reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(respHdr));
        if (errPayload->error != 0) {
            int netErr = -errPayload->error;
            if (netErr == EPERM || netErr == EACCES) {
                errorString = "Insufficient privileges: CAP_NET_ADMIN required to manage WireGuard network interfaces.";
            } else {
                errorString = QString("Netlink error: %1 (code %2)").arg(std::strerror(netErr)).arg(netErr);
            }
            return -netErr;
        }
    }

    return 0; // Success ACK
}

} // namespace

bool NetlinkWireGuard::isSubnetValid(const QString &ip, const QString &expectedSubnet, int prefixLen) {
    QHostAddress addr(ip);
    if (addr.isNull() || addr.protocol() != QAbstractSocket::IPv4Protocol) {
        return false;
    }

    QHostAddress subnet(expectedSubnet);
    if (subnet.isNull()) return false;

    quint32 mask = 0xFFFFFFFF << (32 - prefixLen);
    quint32 ipVal = addr.toIPv4Address();
    quint32 subVal = subnet.toIPv4Address();

    if ((ipVal & mask) != (subVal & mask)) {
        return false;
    }

    // Disallow network (.0) and broadcast (.255.255 for /16 or host boundaries)
    quint32 hostPart = ipVal & (~mask);
    if (hostPart == 0 || hostPart == (~mask)) {
        return false;
    }

    return true;
}

bool NetlinkWireGuard::isValidBase64Key(const QString &keyBase64) {
    if (keyBase64.trimmed().isEmpty()) return false;
    QByteArray decoded = QByteArray::fromBase64(keyBase64.trimmed().toUtf8());
    return decoded.size() == 32;
}

bool NetlinkWireGuard::parseEndpoint(const QString &endpointStr, QHostAddress &addr, quint16 &port) {
    QString str = endpointStr.trimmed();
    int lastColon = str.lastIndexOf(':');
    if (lastColon <= 0 || lastColon == str.length() - 1) return false;

    QString hostPart = str.left(lastColon);
    QString portPart = str.mid(lastColon + 1);

    bool ok = false;
    uint p = portPart.toUInt(&ok);
    if (!ok || p == 0 || p > 65535) return false;

    if (!addr.setAddress(hostPart)) return false;
    port = static_cast<quint16>(p);
    return true;
}

bool NetlinkWireGuard::interfaceExists(const QString &ifName) {
    return if_nametoindex(ifName.toUtf8().constData()) > 0;
}

int NetlinkWireGuard::resolveWireGuardFamily() {
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (sock < 0) return -1;

    NetlinkBuffer buf;
    struct nlmsghdr nlh;
    std::memset(&nlh, 0, sizeof(nlh));
    nlh.nlmsg_type = GENL_ID_CTRL;
    nlh.nlmsg_flags = NLM_F_REQUEST;
    nlh.nlmsg_seq = 1;
    buf.append(nlh);

    struct genlmsghdr gnlh;
    std::memset(&gnlh, 0, sizeof(gnlh));
    gnlh.cmd = CTRL_CMD_GETFAMILY;
    gnlh.version = 1;
    buf.append(gnlh);

    buf.putAttrStr(CTRL_ATTR_FAMILY_NAME, WG_GENL_NAME);

    struct nlmsghdr *hdr = reinterpret_cast<struct nlmsghdr*>(buf.data());
    hdr->nlmsg_len = static_cast<uint32_t>(buf.size());

    struct sockaddr_nl sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;

    if (sendto(sock, buf.data(), buf.size(), 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        close(sock);
        return -1;
    }

    uint8_t respBuf[4096];
    ssize_t len = recv(sock, respBuf, sizeof(respBuf), 0);
    close(sock);

    if (len < static_cast<ssize_t>(NLMSG_SPACE(GENL_HDRLEN))) return -1;

    struct nlmsghdr *respHdr = reinterpret_cast<struct nlmsghdr*>(respBuf);
    if (respHdr->nlmsg_type == NLMSG_ERROR) return -1;

    size_t payloadOffset = NLMSG_SPACE(GENL_HDRLEN);
    size_t attrLen = respHdr->nlmsg_len - payloadOffset;
    const uint8_t *attrPtr = respBuf + payloadOffset;

    while (attrLen >= NLA_HDRLEN) {
        const struct nlattr *nla = reinterpret_cast<const struct nlattr*>(attrPtr);
        if (nla->nla_len < NLA_HDRLEN || nla->nla_len > attrLen) break;

        if (nla->nla_type == CTRL_ATTR_FAMILY_ID) {
            uint16_t familyId = *reinterpret_cast<const uint16_t*>(attrPtr + NLA_HDRLEN);
            return familyId;
        }

        size_t alignedLen = NLA_ALIGN(nla->nla_len);
        if (alignedLen > attrLen) break;
        attrPtr += alignedLen;
        attrLen -= alignedLen;
    }

    return -1;
}

bool NetlinkWireGuard::isWireGuardSupported() {
    return resolveWireGuardFamily() > 0;
}

bool NetlinkWireGuard::createInterface(const QString &ifName, QString &errorString) {
    if (interfaceExists(ifName)) {
        return true; // Already exists
    }

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
        errorString = QString("Failed to open Netlink route socket: %1").arg(std::strerror(errno));
        return false;
    }

    NetlinkBuffer buf;
    struct nlmsghdr nlh;
    std::memset(&nlh, 0, sizeof(nlh));
    nlh.nlmsg_type = RTM_NEWLINK;
    nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;
    nlh.nlmsg_seq = 2;
    buf.append(nlh);

    struct ifinfomsg ifi;
    std::memset(&ifi, 0, sizeof(ifi));
    ifi.ifi_family = AF_UNSPEC;
    buf.append(ifi);

    buf.putAttrStr(IFLA_IFNAME, ifName);

    size_t linkInfo = buf.startNested(IFLA_LINKINFO);
    buf.putAttrStr(IFLA_INFO_KIND, "wireguard");
    buf.endNested(linkInfo);

    int res = sendNetlinkRequest(sock, buf, errorString);
    close(sock);

    if (res != 0 && res != -EEXIST) {
        return false;
    }

    return true;
}

bool NetlinkWireGuard::deleteInterface(const QString &ifName, QString &errorString) {
    if (!interfaceExists(ifName)) {
        return true; // Already removed
    }

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
        errorString = QString("Failed to open Netlink route socket: %1").arg(std::strerror(errno));
        return false;
    }

    NetlinkBuffer buf;
    struct nlmsghdr nlh;
    std::memset(&nlh, 0, sizeof(nlh));
    nlh.nlmsg_type = RTM_DELLINK;
    nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh.nlmsg_seq = 3;
    buf.append(nlh);

    struct ifinfomsg ifi;
    std::memset(&ifi, 0, sizeof(ifi));
    ifi.ifi_family = AF_UNSPEC;
    ifi.ifi_index = if_nametoindex(ifName.toUtf8().constData());
    buf.append(ifi);

    buf.putAttrStr(IFLA_IFNAME, ifName);

    int res = sendNetlinkRequest(sock, buf, errorString);
    close(sock);

    if (res != 0 && res != -ENODEV && res != -ENOENT) {
        return false;
    }

    return true;
}

bool NetlinkWireGuard::setInterfaceAddress(const QString &ifName, const QString &virtualIp, int prefixLen, QString &errorString) {
    unsigned int ifIndex = if_nametoindex(ifName.toUtf8().constData());
    if (ifIndex == 0) {
        errorString = QString("Interface %1 does not exist.").arg(ifName);
        return false;
    }

    struct in_addr addr;
    if (inet_pton(AF_INET, virtualIp.toUtf8().constData(), &addr) <= 0) {
        errorString = QString("Invalid virtual IP address: %1").arg(virtualIp);
        return false;
    }

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
        errorString = QString("Failed to open Netlink route socket: %1").arg(std::strerror(errno));
        return false;
    }

    NetlinkBuffer buf;
    struct nlmsghdr nlh;
    std::memset(&nlh, 0, sizeof(nlh));
    nlh.nlmsg_type = RTM_NEWADDR;
    nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;
    nlh.nlmsg_seq = 4;
    buf.append(nlh);

    struct ifaddrmsg ifa;
    std::memset(&ifa, 0, sizeof(ifa));
    ifa.ifa_family = AF_INET;
    ifa.ifa_prefixlen = static_cast<uint8_t>(prefixLen);
    ifa.ifa_flags = IFA_F_PERMANENT;
    ifa.ifa_scope = RT_SCOPE_UNIVERSE;
    ifa.ifa_index = ifIndex;
    buf.append(ifa);

    buf.putAttr(IFA_LOCAL, &addr, sizeof(addr));
    buf.putAttr(IFA_ADDRESS, &addr, sizeof(addr));

    int res = sendNetlinkRequest(sock, buf, errorString);
    close(sock);

    return res == 0;
}

bool NetlinkWireGuard::setInterfaceUp(const QString &ifName, bool up, QString &errorString) {
    unsigned int ifIndex = if_nametoindex(ifName.toUtf8().constData());
    if (ifIndex == 0) {
        errorString = QString("Interface %1 does not exist.").arg(ifName);
        return false;
    }

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
        errorString = QString("Failed to open Netlink route socket: %1").arg(std::strerror(errno));
        return false;
    }

    NetlinkBuffer buf;
    struct nlmsghdr nlh;
    std::memset(&nlh, 0, sizeof(nlh));
    nlh.nlmsg_type = RTM_SETLINK;
    nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh.nlmsg_seq = 5;
    buf.append(nlh);

    struct ifinfomsg ifi;
    std::memset(&ifi, 0, sizeof(ifi));
    ifi.ifi_family = AF_UNSPEC;
    ifi.ifi_index = ifIndex;
    ifi.ifi_change = IFF_UP;
    ifi.ifi_flags = up ? IFF_UP : 0;
    buf.append(ifi);

    int res = sendNetlinkRequest(sock, buf, errorString);
    close(sock);

    return res == 0;
}

bool NetlinkWireGuard::setWireGuardDevice(
    const QString &ifName,
    const QByteArray &privateKey,
    quint16 listenPort,
    const QList<WireGuardPeerConfig> &peers,
    QString &errorString
) {
    int familyId = resolveWireGuardFamily();
    if (familyId <= 0) {
        errorString = "WireGuard Generic Netlink family not found in Linux kernel.";
        return false;
    }

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (sock < 0) {
        errorString = QString("Failed to open Netlink generic socket: %1").arg(std::strerror(errno));
        return false;
    }

    NetlinkBuffer buf;
    struct nlmsghdr nlh;
    std::memset(&nlh, 0, sizeof(nlh));
    nlh.nlmsg_type = static_cast<uint16_t>(familyId);
    nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh.nlmsg_seq = 6;
    buf.append(nlh);

    struct genlmsghdr gnlh;
    std::memset(&gnlh, 0, sizeof(gnlh));
    gnlh.cmd = WG_CMD_SET_DEVICE;
    gnlh.version = WG_GENL_VERSION;
    buf.append(gnlh);

    buf.putAttrStr(WGDEVICE_A_IFNAME, ifName);

    if (privateKey.size() == 32) {
        buf.putAttrRaw(WGDEVICE_A_PRIVATE_KEY, privateKey);
    }

    if (listenPort > 0) {
        buf.putAttrU16(WGDEVICE_A_LISTEN_PORT, listenPort);
    }

    if (!peers.isEmpty()) {
        size_t peersNest = buf.startNested(WGDEVICE_A_PEERS);
        for (int i = 0; i < peers.size(); ++i) {
            const auto &peer = peers.at(i);
            if (peer.publicKey.size() != 32) continue;

            size_t peerNest = buf.startNested(0);
            buf.putAttrRaw(WGPEER_A_PUBLIC_KEY, peer.publicKey);

            if (peer.remove) {
                buf.putAttrU32(WGPEER_A_FLAGS, WGPEER_F_REMOVE_ME);
            } else {
                buf.putAttrU32(WGPEER_A_FLAGS, WGPEER_F_REPLACE_ALLOWEDIPS);
                if (peer.persistentKeepalive > 0) {
                    buf.putAttrU16(WGPEER_A_PERSISTENT_KEEPALIVE_INTERVAL, peer.persistentKeepalive);
                }

                // Peer endpoint
                if (!peer.endpoint.isEmpty()) {
                    QHostAddress epAddr;
                    quint16 epPort = 0;
                    if (parseEndpoint(peer.endpoint, epAddr, epPort)) {
                        if (epAddr.protocol() == QAbstractSocket::IPv4Protocol) {
                            struct sockaddr_in sin;
                            std::memset(&sin, 0, sizeof(sin));
                            sin.sin_family = AF_INET;
                            sin.sin_port = htons(epPort);
                            sin.sin_addr.s_addr = htonl(epAddr.toIPv4Address());
                            buf.putAttr(WGPEER_A_ENDPOINT, &sin, sizeof(sin));
                        }
                    }
                }

                // Allowed IPs
                if (!peer.allowedIps.isEmpty()) {
                    size_t allowedNest = buf.startNested(WGPEER_A_ALLOWEDIPS);
                    for (int j = 0; j < peer.allowedIps.size(); ++j) {
                        const auto &aip = peer.allowedIps.at(j);
                        if (aip.address.protocol() == QAbstractSocket::IPv4Protocol) {
                            size_t aipNest = buf.startNested(0);
                            buf.putAttrU16(WGALLOWEDIP_A_FAMILY, AF_INET);
                            struct in_addr in;
                            in.s_addr = htonl(aip.address.toIPv4Address());
                            buf.putAttr(WGALLOWEDIP_A_IPADDR, &in, sizeof(in));
                            buf.putAttrU8(WGALLOWEDIP_A_CIDR_MASK, aip.cidrMask);
                            buf.endNested(aipNest);
                        }
                    }
                    buf.endNested(allowedNest);
                }
            }
            buf.endNested(peerNest);
        }
        buf.endNested(peersNest);
    }

    int res = sendNetlinkRequest(sock, buf, errorString);
    close(sock);

    return res == 0;
}

std::optional<WireGuardDeviceStatus> NetlinkWireGuard::getWireGuardDevice(
    const QString &ifName,
    QString &errorString
) {
    int familyId = resolveWireGuardFamily();
    if (familyId <= 0) {
        errorString = "WireGuard Generic Netlink family not found in Linux kernel.";
        return std::nullopt;
    }

    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
    if (sock < 0) {
        errorString = QString("Failed to open Netlink generic socket: %1").arg(std::strerror(errno));
        return std::nullopt;
    }

    NetlinkBuffer buf;
    struct nlmsghdr nlh;
    std::memset(&nlh, 0, sizeof(nlh));
    nlh.nlmsg_type = static_cast<uint16_t>(familyId);
    nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh.nlmsg_seq = 7;
    buf.append(nlh);

    struct genlmsghdr gnlh;
    std::memset(&gnlh, 0, sizeof(gnlh));
    gnlh.cmd = WG_CMD_GET_DEVICE;
    gnlh.version = WG_GENL_VERSION;
    buf.append(gnlh);

    buf.putAttrStr(WGDEVICE_A_IFNAME, ifName);

    struct nlmsghdr *hdr = reinterpret_cast<struct nlmsghdr*>(buf.data());
    hdr->nlmsg_len = static_cast<uint32_t>(buf.size());

    struct sockaddr_nl sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;

    if (sendto(sock, buf.data(), buf.size(), 0, (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        errorString = QString("Failed to send WG_CMD_GET_DEVICE request: %1").arg(std::strerror(errno));
        close(sock);
        return std::nullopt;
    }

    WireGuardDeviceStatus status;
    status.ifName = ifName;

    uint8_t respBuf[8192];
    ssize_t len = recv(sock, respBuf, sizeof(respBuf), 0);
    close(sock);

    if (len < static_cast<ssize_t>(NLMSG_SPACE(GENL_HDRLEN))) {
        errorString = "Truncated response for WG_CMD_GET_DEVICE.";
        return std::nullopt;
    }

    struct nlmsghdr *respHdr = reinterpret_cast<struct nlmsghdr*>(respBuf);
    if (respHdr->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *errPayload = reinterpret_cast<struct nlmsgerr*>(NLMSG_DATA(respHdr));
        int netErr = -errPayload->error;
        errorString = QString("WG_CMD_GET_DEVICE failed: %1").arg(std::strerror(netErr));
        return std::nullopt;
    }

    size_t payloadOffset = NLMSG_SPACE(GENL_HDRLEN);
    size_t attrLen = respHdr->nlmsg_len - payloadOffset;
    const uint8_t *attrPtr = respBuf + payloadOffset;

    while (attrLen >= NLA_HDRLEN) {
        const struct nlattr *nla = reinterpret_cast<const struct nlattr*>(attrPtr);
        if (nla->nla_len < NLA_HDRLEN || nla->nla_len > attrLen) break;

        uint16_t type = nla->nla_type & NLA_TYPE_MASK;
        if (type == WGDEVICE_A_PUBLIC_KEY && nla->nla_len >= NLA_HDRLEN + 32) {
            status.publicKey = QByteArray(reinterpret_cast<const char*>(attrPtr + NLA_HDRLEN), 32);
        } else if (type == WGDEVICE_A_LISTEN_PORT && nla->nla_len >= NLA_HDRLEN + 2) {
            status.listenPort = *reinterpret_cast<const quint16*>(attrPtr + NLA_HDRLEN);
        }

        size_t alignedLen = NLA_ALIGN(nla->nla_len);
        if (alignedLen > attrLen) break;
        attrPtr += alignedLen;
        attrLen -= alignedLen;
    }

    return status;
}

} // namespace MeckChat::Network
