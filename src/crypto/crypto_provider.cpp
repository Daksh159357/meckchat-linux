#include "meckchat/crypto/crypto_provider.h"
#include "meckchat/core/logger.h"

#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

namespace MeckChat::Crypto {

KeyPair::~KeyPair() {
    clear();
}

void KeyPair::clear() {
    if (!privateKey.isEmpty()) {
        CryptoProvider::secureCleanse(privateKey);
        privateKey.clear();
    }
    publicKey.clear();
}

void CryptoProvider::secureCleanse(void *ptr, size_t len) {
    if (ptr != nullptr && len > 0) {
        OPENSSL_cleanse(ptr, len);
    }
}

void CryptoProvider::secureCleanse(QByteArray &data) {
    if (!data.isEmpty()) {
        OPENSSL_cleanse(data.data(), static_cast<size_t>(data.size()));
    }
}

QByteArray CryptoProvider::generateSecureRandomBytes(size_t numBytes) {
    if (numBytes == 0) return QByteArray();

    QByteArray buf(static_cast<int>(numBytes), 0);
    if (RAND_priv_bytes(reinterpret_cast<unsigned char*>(buf.data()), static_cast<int>(numBytes)) != 1) {
        Core::Logger::error("CryptoProvider", "CSPRNG failure: RAND_priv_bytes returned error.");
        return QByteArray();
    }
    return buf;
}

std::optional<KeyPair> CryptoProvider::generateX25519KeyPair() {
    EVP_PKEY_CTX *pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    if (!pctx) {
        Core::Logger::error("CryptoProvider", "Failed to create EVP_PKEY_CTX for X25519.");
        return std::nullopt;
    }

    if (EVP_PKEY_keygen_init(pctx) <= 0) {
        Core::Logger::error("CryptoProvider", "Failed to initialize X25519 keygen.");
        EVP_PKEY_CTX_free(pctx);
        return std::nullopt;
    }

    EVP_PKEY *pkey = nullptr;
    if (EVP_PKEY_keygen(pctx, &pkey) <= 0 || !pkey) {
        Core::Logger::error("CryptoProvider", "X25519 key generation failed.");
        EVP_PKEY_CTX_free(pctx);
        return std::nullopt;
    }
    EVP_PKEY_CTX_free(pctx);

    KeyPair kp;
    kp.privateKey.resize(32);
    kp.publicKey.resize(32);
    size_t privLen = 32;
    size_t pubLen = 32;

    int privRes = EVP_PKEY_get_raw_private_key(
        pkey,
        reinterpret_cast<unsigned char*>(kp.privateKey.data()),
        &privLen
    );

    int pubRes = EVP_PKEY_get_raw_public_key(
        pkey,
        reinterpret_cast<unsigned char*>(kp.publicKey.data()),
        &pubLen
    );

    EVP_PKEY_free(pkey);

    if (privRes <= 0 || pubRes <= 0 || privLen != 32 || pubLen != 32) {
        Core::Logger::error("CryptoProvider", "Failed to extract raw X25519 key bytes.");
        kp.clear();
        return std::nullopt;
    }

    return kp;
}

std::optional<QByteArray> CryptoProvider::deriveSharedSecret(
    const QByteArray &privateKey,
    const QByteArray &peerPublicKey
) {
    if (privateKey.size() != 32 || peerPublicKey.size() != 32) {
        Core::Logger::error("CryptoProvider", "Invalid key size for X25519 ECDH derivation (must be 32 bytes).");
        return std::nullopt;
    }

    EVP_PKEY *ourPrivKey = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_X25519,
        nullptr,
        reinterpret_cast<const unsigned char*>(privateKey.constData()),
        32
    );
    if (!ourPrivKey) {
        Core::Logger::error("CryptoProvider", "Failed to load raw private key for ECDH.");
        return std::nullopt;
    }

    EVP_PKEY *peerPubKey = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519,
        nullptr,
        reinterpret_cast<const unsigned char*>(peerPublicKey.constData()),
        32
    );
    if (!peerPubKey) {
        Core::Logger::error("CryptoProvider", "Failed to load raw peer public key for ECDH.");
        EVP_PKEY_free(ourPrivKey);
        return std::nullopt;
    }

    EVP_PKEY_CTX *deriveCtx = EVP_PKEY_CTX_new(ourPrivKey, nullptr);
    if (!deriveCtx || EVP_PKEY_derive_init(deriveCtx) <= 0 || EVP_PKEY_derive_set_peer(deriveCtx, peerPubKey) <= 0) {
        Core::Logger::error("CryptoProvider", "Failed to initialize ECDH derivation context.");
        if (deriveCtx) EVP_PKEY_CTX_free(deriveCtx);
        EVP_PKEY_free(peerPubKey);
        EVP_PKEY_free(ourPrivKey);
        return std::nullopt;
    }

    size_t secretLen = 0;
    if (EVP_PKEY_derive(deriveCtx, nullptr, &secretLen) <= 0 || secretLen == 0) {
        Core::Logger::error("CryptoProvider", "Failed to query derived secret length.");
        EVP_PKEY_CTX_free(deriveCtx);
        EVP_PKEY_free(peerPubKey);
        EVP_PKEY_free(ourPrivKey);
        return std::nullopt;
    }

    QByteArray sharedSecret(static_cast<int>(secretLen), 0);
    if (EVP_PKEY_derive(deriveCtx, reinterpret_cast<unsigned char*>(sharedSecret.data()), &secretLen) <= 0) {
        Core::Logger::error("CryptoProvider", "ECDH shared secret derivation failed.");
        secureCleanse(sharedSecret);
        EVP_PKEY_CTX_free(deriveCtx);
        EVP_PKEY_free(peerPubKey);
        EVP_PKEY_free(ourPrivKey);
        return std::nullopt;
    }

    EVP_PKEY_CTX_free(deriveCtx);
    EVP_PKEY_free(peerPubKey);
    EVP_PKEY_free(ourPrivKey);

    return sharedSecret;
}

std::optional<QByteArray> CryptoProvider::deriveArgon2idKey(
    const QString &password,
    const QByteArray &salt,
    uint32_t memCostKb,
    uint32_t iterations,
    uint32_t parallelism,
    size_t keyLen
) {
    if (password.isEmpty() || salt.size() < 8 || keyLen == 0) {
        Core::Logger::error("CryptoProvider", "Invalid parameters for Argon2id derivation.");
        return std::nullopt;
    }

    EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "ARGON2ID", nullptr);
    if (!kdf) {
        Core::Logger::error("CryptoProvider", "EVP_KDF_fetch for ARGON2ID failed. OpenSSL 3 provider missing.");
        return std::nullopt;
    }

    EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx) {
        Core::Logger::error("CryptoProvider", "EVP_KDF_CTX_new failed for Argon2id.");
        return std::nullopt;
    }

    QByteArray passUtf8 = password.toUtf8();
    OSSL_PARAM params[6];
    params[0] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_PASSWORD,
        passUtf8.data(),
        static_cast<size_t>(passUtf8.size())
    );
    params[1] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_SALT,
        const_cast<char*>(salt.constData()),
        static_cast<size_t>(salt.size())
    );
    params[2] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES, &parallelism);
    params[3] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &memCostKb);
    params[4] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER, &iterations);
    params[5] = OSSL_PARAM_construct_end();

    QByteArray derivedKey(static_cast<int>(keyLen), 0);
    int res = EVP_KDF_derive(
        kctx,
        reinterpret_cast<unsigned char*>(derivedKey.data()),
        keyLen,
        params
    );

    secureCleanse(passUtf8);
    EVP_KDF_CTX_free(kctx);

    if (res <= 0) {
        Core::Logger::error("CryptoProvider", "Argon2id derivation execution failed.");
        secureCleanse(derivedKey);
        return std::nullopt;
    }

    return derivedKey;
}

std::optional<QByteArray> CryptoProvider::computeHmacSha256(
    const QByteArray &key,
    const QByteArray &message
) {
    if (key.isEmpty()) {
        Core::Logger::error("CryptoProvider", "HMAC-SHA256 key cannot be empty.");
        return std::nullopt;
    }

    QByteArray hmac(32, 0);
    size_t outLen = 0;

    void *res = EVP_Q_mac(
        nullptr,
        "HMAC",
        nullptr,
        "SHA256",
        nullptr,
        key.constData(),
        static_cast<size_t>(key.size()),
        reinterpret_cast<const unsigned char*>(message.constData()),
        static_cast<size_t>(message.size()),
        reinterpret_cast<unsigned char*>(hmac.data()),
        static_cast<size_t>(hmac.size()),
        &outLen
    );

    if (!res || outLen != 32) {
        Core::Logger::error("CryptoProvider", "EVP_Q_mac failed to compute HMAC-SHA256.");
        secureCleanse(hmac);
        return std::nullopt;
    }

    return hmac;
}

bool CryptoProvider::verifyHmacConstantTime(
    const QByteArray &expected,
    const QByteArray &actual
) {
    if (expected.size() != 32 || actual.size() != 32) {
        return false;
    }

    // CRYPTO_memcmp returns 0 if buffers are equal in constant time
    return CRYPTO_memcmp(expected.constData(), actual.constData(), 32) == 0;
}

} // namespace MeckChat::Crypto
