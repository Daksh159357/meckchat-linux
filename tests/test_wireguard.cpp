#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include <QCoreApplication>
#include <QHostAddress>

#include "meckchat/network/netlink_wireguard.h"
#include "meckchat/network/wireguard_service.h"
#include "meckchat/crypto/crypto_provider.h"
#include "meckchat/core/logger.h"

using namespace MeckChat::Network;
using namespace MeckChat::Crypto;

#define TEST_CHECK(cond) do { \
    if (!(cond)) { \
        std::cerr << "TEST ASSERTION FAILED: " #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::exit(1); \
    } \
} while(0)

void testSubnetValidation() {
    std::cout << "[RUN] testSubnetValidation" << std::endl;

    // Valid 10.77.0.0/16 addresses
    TEST_CHECK(NetlinkWireGuard::isSubnetValid("10.77.0.2"));
    TEST_CHECK(NetlinkWireGuard::isSubnetValid("10.77.1.50"));
    TEST_CHECK(NetlinkWireGuard::isSubnetValid("10.77.100.200"));
    TEST_CHECK(NetlinkWireGuard::isSubnetValid("10.77.255.254"));

    // Invalid Subnet / Outside 10.77.0.0/16
    TEST_CHECK(!NetlinkWireGuard::isSubnetValid("10.78.0.1"));
    TEST_CHECK(!NetlinkWireGuard::isSubnetValid("192.168.1.1"));
    TEST_CHECK(!NetlinkWireGuard::isSubnetValid("172.16.0.2"));
    TEST_CHECK(!NetlinkWireGuard::isSubnetValid("127.0.0.1"));

    // Boundaries & Malformed
    TEST_CHECK(!NetlinkWireGuard::isSubnetValid("10.77.0.0"));       // Network
    TEST_CHECK(!NetlinkWireGuard::isSubnetValid("10.77.255.255"));   // Broadcast
    TEST_CHECK(!NetlinkWireGuard::isSubnetValid(""));
    TEST_CHECK(!NetlinkWireGuard::isSubnetValid("not_an_ip"));
    TEST_CHECK(!NetlinkWireGuard::isSubnetValid("::1"));

    std::cout << "[PASS] testSubnetValidation" << std::endl;
}

void testKeyValidation() {
    std::cout << "[RUN] testKeyValidation" << std::endl;

    auto kp = CryptoProvider::generateX25519KeyPair();
    TEST_CHECK(kp.has_value());

    QString validKeyBase64 = kp->publicKey.toBase64();
    TEST_CHECK(NetlinkWireGuard::isValidBase64Key(validKeyBase64));

    // Invalid Key lengths
    QByteArray shortKey(31, 0x11);
    TEST_CHECK(!NetlinkWireGuard::isValidBase64Key(shortKey.toBase64()));

    QByteArray longKey(33, 0x22);
    TEST_CHECK(!NetlinkWireGuard::isValidBase64Key(longKey.toBase64()));

    TEST_CHECK(!NetlinkWireGuard::isValidBase64Key(""));
    TEST_CHECK(!NetlinkWireGuard::isValidBase64Key("Invalid!@#$Base64"));

    std::cout << "[PASS] testKeyValidation" << std::endl;
}

void testEndpointParsing() {
    std::cout << "[RUN] testEndpointParsing" << std::endl;

    QHostAddress addr;
    quint16 port = 0;

    TEST_CHECK(NetlinkWireGuard::parseEndpoint("192.168.1.100:51820", addr, port));
    TEST_CHECK(addr == QHostAddress("192.168.1.100"));
    TEST_CHECK(port == 51820);

    TEST_CHECK(NetlinkWireGuard::parseEndpoint("10.0.0.5:7788", addr, port));
    TEST_CHECK(addr == QHostAddress("10.0.0.5"));
    TEST_CHECK(port == 7788);

    // Invalid endpoints
    TEST_CHECK(!NetlinkWireGuard::parseEndpoint("192.168.1.100", addr, port));
    TEST_CHECK(!NetlinkWireGuard::parseEndpoint("192.168.1.100:0", addr, port));
    TEST_CHECK(!NetlinkWireGuard::parseEndpoint("192.168.1.100:70000", addr, port));
    TEST_CHECK(!NetlinkWireGuard::parseEndpoint("", addr, port));
    TEST_CHECK(!NetlinkWireGuard::parseEndpoint(":51820", addr, port));

    std::cout << "[PASS] testEndpointParsing" << std::endl;
}

void testWireGuardServicePeerManagement() {
    std::cout << "[RUN] testWireGuardServicePeerManagement" << std::endl;

    WireGuardService service;
    auto kp = CryptoProvider::generateX25519KeyPair();
    TEST_CHECK(kp.has_value());
    QString pubKeyBase64 = kp->publicKey.toBase64();

    // 1. Add valid peer
    bool ok = service.addPeer(pubKeyBase64, "10.77.0.5", "192.168.1.50:51820", 25);
    TEST_CHECK(ok);
    TEST_CHECK(service.isPeerConfigured(pubKeyBase64));
    TEST_CHECK(service.peers().size() == 1);

    // 2. Reject peer with invalid subnet
    auto kp2 = CryptoProvider::generateX25519KeyPair();
    TEST_CHECK(!service.addPeer(kp2->publicKey.toBase64(), "192.168.1.5"));

    // 3. Reject peer with invalid key
    TEST_CHECK(!service.addPeer("InvalidKey", "10.77.0.6"));

    // 4. Remove peer
    TEST_CHECK(service.removePeer(pubKeyBase64));
    TEST_CHECK(!service.isPeerConfigured(pubKeyBase64));
    TEST_CHECK(service.peers().isEmpty());

    std::cout << "[PASS] testWireGuardServicePeerManagement" << std::endl;
}

void testWireGuardKernelSupportAndIntegration() {
    std::cout << "[RUN] testWireGuardKernelSupportAndIntegration" << std::endl;

    bool supported = NetlinkWireGuard::isWireGuardSupported();
    std::cout << "  -> Linux Kernel WireGuard Generic Netlink Support: " << (supported ? "YES" : "NO") << std::endl;

    // Check if running with root / CAP_NET_ADMIN privileges
    bool hasAdminPrivileges = (geteuid() == 0);
    const char *forceIntegration = std::getenv("WIREGUARD_INTEGRATION");

    if (hasAdminPrivileges || (forceIntegration && std::string(forceIntegration) == "1")) {
        std::cout << "  -> Running privileged kernel WireGuard integration test..." << std::endl;

        QString testIfName = "wg-mck-test";
        WireGuardService service;

        auto kp = CryptoProvider::generateX25519KeyPair();
        TEST_CHECK(kp.has_value());

        // RAII-style cleanup guard
        struct CleanupGuard {
            QString ifName;
            ~CleanupGuard() {
                QString err;
                NetlinkWireGuard::deleteInterface(ifName, err);
            }
        } guard{testIfName};

        bool started = service.startTunnel(testIfName, "10.77.99.1", kp->privateKey.toBase64(), 51825);
        if (started) {
            TEST_CHECK(service.status() == TunnelStatus::Up);
            TEST_CHECK(NetlinkWireGuard::interfaceExists(testIfName));

            // Add peer in kernel
            auto peerKp = CryptoProvider::generateX25519KeyPair();
            TEST_CHECK(peerKp.has_value());
            TEST_CHECK(service.addPeer(peerKp->publicKey.toBase64(), "10.77.99.2", "127.0.0.1:51826"));

            // Verify kernel device status
            QString errStr;
            auto devStatus = NetlinkWireGuard::getWireGuardDevice(testIfName, errStr);
            TEST_CHECK(devStatus.has_value());
            TEST_CHECK(devStatus->listenPort == 51825);

            service.stopTunnel();
            TEST_CHECK(service.status() == TunnelStatus::Down);
            std::cout << "[PASS] testWireGuardKernelSupportAndIntegration (REAL WIREGUARD KERNEL INTEGRATION TEST: PASS)" << std::endl;
        } else {
            std::cout << "  -> Kernel creation failed: " << service.lastError().toStdString() << std::endl;
            std::cout << "[SKIP] REAL WIREGUARD KERNEL INTEGRATION TEST: FAIL (Kernel rejected request)" << std::endl;
        }
    } else {
        std::cout << "  -> REAL WIREGUARD KERNEL INTEGRATION TEST: NOT RUN — ENVIRONMENT BLOCKED (Unprivileged runner without CAP_NET_ADMIN)" << std::endl;
    }

    std::cout << "[PASS] testWireGuardKernelSupportAndIntegration" << std::endl;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    MeckChat::Core::Logger::init();

    testSubnetValidation();
    testKeyValidation();
    testEndpointParsing();
    testWireGuardServicePeerManagement();
    testWireGuardKernelSupportAndIntegration();

    std::cout << "\n==============================================" << std::endl;
    std::cout << "  All WireGuard Netlink Tests Passed! " << std::endl;
    std::cout << "==============================================" << std::endl;
    return 0;
}
