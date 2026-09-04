#include <cassert>
#include <iostream>
#include <QCoreApplication>
#include <QDateTime>
#include <QUuid>
#include <QCryptographicHash>

#include "meckchat/crypto/crypto_provider.h"
#include "meckchat/crypto/pairing_session.h"
#include "meckchat/core/config.h"
#include "meckchat/core/logger.h"

using namespace MeckChat::Crypto;
using namespace MeckChat::Protocol;

void testX25519KeysAndEcdh() {
    std::cout << "[RUN] testX25519KeysAndEcdh" << std::endl;

    // 1. Generate keypairs for Alice and Bob
    auto aliceKeys = CryptoProvider::generateX25519KeyPair();
    auto bobKeys = CryptoProvider::generateX25519KeyPair();

    assert(aliceKeys.has_value());
    assert(bobKeys.has_value());
    assert(aliceKeys->privateKey.size() == 32);
    assert(aliceKeys->publicKey.size() == 32);
    assert(bobKeys->privateKey.size() == 32);
    assert(bobKeys->publicKey.size() == 32);
    assert(aliceKeys->publicKey != bobKeys->publicKey);

    // 2. Derive shared secrets
    auto aliceShared = CryptoProvider::deriveSharedSecret(aliceKeys->privateKey, bobKeys->publicKey);
    auto bobShared = CryptoProvider::deriveSharedSecret(bobKeys->privateKey, aliceKeys->publicKey);

    assert(aliceShared.has_value());
    assert(bobShared.has_value());
    assert(aliceShared->size() == 32);
    assert(bobShared->size() == 32);
    assert(CryptoProvider::verifyHmacConstantTime(*aliceShared, *bobShared));

    // 3. Reject invalid key lengths
    QByteArray badKey(31, 0xAA);
    assert(!CryptoProvider::deriveSharedSecret(badKey, bobKeys->publicKey).has_value());
    assert(!CryptoProvider::deriveSharedSecret(aliceKeys->privateKey, badKey).has_value());

    std::cout << "[PASS] testX25519KeysAndEcdh" << std::endl;
}

void testArgon2idKdf() {
    std::cout << "[RUN] testArgon2idKdf" << std::endl;

    QString password = "SecurePairingPin9876";
    QByteArray salt = QByteArray::fromHex("0102030405060708090a0b0c0d0e0f10");

    auto key1 = CryptoProvider::deriveArgon2idKey(password, salt);
    auto key2 = CryptoProvider::deriveArgon2idKey(password, salt);

    assert(key1.has_value());
    assert(key2.has_value());
    assert(key1->size() == 32);
    assert(CryptoProvider::verifyHmacConstantTime(*key1, *key2));

    // Different password produces different key
    auto diffKey = CryptoProvider::deriveArgon2idKey("DifferentPin1234", salt);
    assert(diffKey.has_value());
    assert(!CryptoProvider::verifyHmacConstantTime(*key1, *diffKey));

    // Empty password rejected
    assert(!CryptoProvider::deriveArgon2idKey("", salt).has_value());

    std::cout << "[PASS] testArgon2idKdf" << std::endl;
}

void testHmacSha256() {
    std::cout << "[RUN] testHmacSha256" << std::endl;

    QByteArray key = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f");
    QByteArray message = "mc_sender||mc_receiver||1725000000||pubkeyBase64";

    auto hmac = CryptoProvider::computeHmacSha256(key, message);
    assert(hmac.has_value());
    assert(hmac->size() == 32);

    // Constant time match
    assert(CryptoProvider::verifyHmacConstantTime(*hmac, *hmac));

    // Tampered message
    QByteArray tamperedMessage = message + "X";
    auto tamperedHmac = CryptoProvider::computeHmacSha256(key, tamperedMessage);
    assert(!CryptoProvider::verifyHmacConstantTime(*hmac, *tamperedHmac));

    // Tampered key
    QByteArray tamperedKey = key;
    tamperedKey[0] = tamperedKey[0] ^ 0xFF;
    auto tamperedKeyHmac = CryptoProvider::computeHmacSha256(tamperedKey, message);
    assert(!CryptoProvider::verifyHmacConstantTime(*hmac, *tamperedKeyHmac));

    std::cout << "[PASS] testHmacSha256" << std::endl;
}

void testTimestampAndReplayProtection() {
    std::cout << "[RUN] testTimestampAndReplayProtection" << std::endl;

    ReplayProtectionCache cache;
    qint64 now = 1725000000;

    // 1. Boundary checking (300s window)
    assert(cache.isTimestampValid(now, now));
    assert(cache.isTimestampValid(now - 150, now));
    assert(cache.isTimestampValid(now + 150, now));
    assert(cache.isTimestampValid(now - 300, now));
    assert(cache.isTimestampValid(now + 300, now));

    assert(!cache.isTimestampValid(now - 301, now));
    assert(!cache.isTimestampValid(now + 301, now));
    assert(!cache.isTimestampValid(0, now));
    assert(!cache.isTimestampValid(-100, now));

    // 2. Replay detection
    QString sess1 = "sess_001";
    QByteArray hash1 = QCryptographicHash::hash("transcript_alice_bob", QCryptographicHash::Sha256);

    assert(!cache.isReplayed(sess1, hash1, now, now));
    cache.registerSession(sess1, hash1, now);

    // Replaying identical session must be detected
    assert(cache.isReplayed(sess1, hash1, now, now));

    // Replaying same transcript with new session ID must be detected
    QString sess2 = "sess_002";
    assert(cache.isReplayed(sess2, hash1, now, now));

    // Fresh session and transcript must be accepted
    QByteArray hash2 = QCryptographicHash::hash("transcript_alice_charlie", QCryptographicHash::Sha256);
    assert(!cache.isReplayed(sess2, hash2, now, now));
    cache.registerSession(sess2, hash2, now);

    // 3. Pruning expired sessions
    cache.pruneExpired(now + 400); // 400s later, sess1 and sess2 expired
    // Stale timestamp will still be rejected by timestamp validity
    assert(cache.isReplayed(sess1, hash1, now, now + 400));

    std::cout << "[PASS] testTimestampAndReplayProtection" << std::endl;
}

void testEndToEndPairingHandshake() {
    std::cout << "[RUN] testEndToEndPairingHandshake" << std::endl;

    QString aliceId = "mc_alice_unit_test";
    QString bobId = "mc_bob_unit_test";
    QString sharedSecret = "MeckChat@2026_Secure_Passphrase";

    // 1. Alice initiates pairing
    MeckChat::Core::AppConfig::instance().setDeviceId(aliceId);
    MeckChat::Core::AppConfig::instance().setVirtualIp("10.77.0.2");

    PairingSession aliceSession;
    auto requestOpt = aliceSession.initiatePairing(bobId, sharedSecret, "10.77.0.2");

    assert(requestOpt.has_value());
    assert(aliceSession.state() == PairingState::AwaitingResponse);
    assert(requestOpt->senderDeviceId == aliceId);
    assert(requestOpt->receiverDeviceId == bobId);
    assert(!requestOpt->authProofBase64.isEmpty());

    // 2. Bob handles incoming request
    MeckChat::Core::AppConfig::instance().setDeviceId(bobId);
    MeckChat::Core::AppConfig::instance().setVirtualIp("10.77.0.3");

    PairingSession bobSession;
    auto responseOpt = bobSession.handleIncomingRequest(*requestOpt, sharedSecret, "10.77.0.3");

    assert(responseOpt.has_value());
    assert(bobSession.state() == PairingState::Paired);
    assert(responseOpt->status == "accepted");
    assert(bobSession.result().has_value());

    // 3. Alice handles incoming response
    MeckChat::Core::AppConfig::instance().setDeviceId(aliceId);
    bool aliceAccepted = aliceSession.handleIncomingResponse(*responseOpt, sharedSecret);

    assert(aliceAccepted);
    assert(aliceSession.state() == PairingState::Paired);
    assert(aliceSession.result().has_value());

    // 4. Verify shared secret agreement
    QByteArray aliceSecret = aliceSession.result()->sharedSecret;
    QByteArray bobSecret = bobSession.result()->sharedSecret;

    assert(aliceSecret.size() == 32);
    assert(bobSecret.size() == 32);
    assert(CryptoProvider::verifyHmacConstantTime(aliceSecret, bobSecret));

    // Verify WireGuard parameters exchanged
    assert(aliceSession.result()->peerDeviceId == bobId);
    assert(aliceSession.result()->peerVirtualIp == "10.77.0.3");
    assert(bobSession.result()->peerDeviceId == aliceId);
    assert(bobSession.result()->peerVirtualIp == "10.77.0.2");

    std::cout << "[PASS] testEndToEndPairingHandshake" << std::endl;
}

void testPairingFailureScenarios() {
    std::cout << "[RUN] testPairingFailureScenarios" << std::endl;

    QString aliceId = "mc_alice_unit_test";
    QString bobId = "mc_bob_unit_test";

    // 1. Wrong password failure
    MeckChat::Core::AppConfig::instance().setDeviceId(aliceId);
    PairingSession aliceSession;
    auto req = aliceSession.initiatePairing(bobId, "CorrectPassword123", "10.77.0.2");
    assert(req.has_value());

    MeckChat::Core::AppConfig::instance().setDeviceId(bobId);
    PairingSession bobSession;
    auto failResp = bobSession.handleIncomingRequest(*req, "WrongPassword456", "10.77.0.3");
    assert(!failResp.has_value());
    assert(bobSession.state() == PairingState::Failed);
    assert(bobSession.lastError() == PairingError::AuthFailed);

    // 2. Expired timestamp failure
    PairingSession bobSession2;
    auto expiredReq = *req;
    expiredReq.timestamp = QDateTime::currentSecsSinceEpoch() - 600; // 10 minutes ago
    auto expiredResp = bobSession2.handleIncomingRequest(expiredReq, "CorrectPassword123", "10.77.0.3");
    assert(!expiredResp.has_value());
    assert(bobSession2.state() == PairingState::Failed);
    assert(bobSession2.lastError() == PairingError::TimestampExpired);

    // 3. Replay attack rejection
    PairingSession bobSession3;
    auto validResp = bobSession3.handleIncomingRequest(*req, "CorrectPassword123", "10.77.0.3");
    assert(validResp.has_value());

    // Replay the exact same request again
    auto replayedResp = bobSession3.handleIncomingRequest(*req, "CorrectPassword123", "10.77.0.3");
    assert(!replayedResp.has_value());
    assert(bobSession3.state() == PairingState::Failed);
    assert(bobSession3.lastError() == PairingError::ReplayDetected);

    std::cout << "[PASS] testPairingFailureScenarios" << std::endl;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    MeckChat::Core::Logger::init();

    testX25519KeysAndEcdh();
    testArgon2idKdf();
    testHmacSha256();
    testTimestampAndReplayProtection();
    testEndToEndPairingHandshake();
    testPairingFailureScenarios();

    std::cout << "\n==============================================" << std::endl;
    std::cout << "  All Pairing & Crypto Tests Passed! " << std::endl;
    std::cout << "==============================================" << std::endl;
    return 0;
}
