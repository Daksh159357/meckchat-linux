#pragma once

#include <QString>
#include <QByteArray>
#include <optional>
#include <cstdint>

namespace MeckChat::Crypto {

struct KeyPair {
    QByteArray privateKey; // 32 bytes raw private key
    QByteArray publicKey;  // 32 bytes raw public key

    ~KeyPair();
    KeyPair() = default;
    KeyPair(const KeyPair &other) = default;
    KeyPair(KeyPair &&other) noexcept = default;
    KeyPair &operator=(const KeyPair &other) = default;
    KeyPair &operator=(KeyPair &&other) noexcept = default;

    void clear();
};

class CryptoProvider {
public:
    // Generate fresh ephemeral X25519 / Curve25519 keypair
    static std::optional<KeyPair> generateX25519KeyPair();

    // Derive 32-byte shared secret from X25519 ECDH
    static std::optional<QByteArray> deriveSharedSecret(
        const QByteArray &privateKey,
        const QByteArray &peerPublicKey
    );

    // Derive 32-byte authentication key using Argon2id
    static std::optional<QByteArray> deriveArgon2idKey(
        const QString &password,
        const QByteArray &salt,
        uint32_t memCostKb = 65536, // 64 MB
        uint32_t iterations = 3,
        uint32_t parallelism = 1,
        size_t keyLen = 32
    );

    // Compute HMAC-SHA256 authentication proof
    static std::optional<QByteArray> computeHmacSha256(
        const QByteArray &key,
        const QByteArray &message
    );

    // Constant-time verification of HMAC proofs
    static bool verifyHmacConstantTime(
        const QByteArray &expected,
        const QByteArray &actual
    );

    // Cryptographically secure random byte generator
    static QByteArray generateSecureRandomBytes(size_t numBytes);

    // Memory zeroization utilities
    static void secureCleanse(void *ptr, size_t len);
    static void secureCleanse(QByteArray &data);
};

} // namespace MeckChat::Crypto
