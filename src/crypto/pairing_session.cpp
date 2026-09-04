#include "meckchat/crypto/pairing_session.h"
#include "meckchat/core/logger.h"
#include "meckchat/core/config.h"

#include <QUuid>
#include <QCryptographicHash>

namespace MeckChat::Crypto {

// -----------------------------------------------------------------------------
// ReplayProtectionCache Implementation
// -----------------------------------------------------------------------------

bool ReplayProtectionCache::isTimestampValid(qint64 timestamp, qint64 currentTimestamp) const {
    if (timestamp <= 0) return false;
    qint64 now = (currentTimestamp > 0) ? currentTimestamp : QDateTime::currentSecsSinceEpoch();
    return std::abs(now - timestamp) <= REPLAY_WINDOW_SECONDS;
}

bool ReplayProtectionCache::isReplayed(
    const QString &sessionId,
    const QByteArray &transcriptHash,
    qint64 timestamp,
    qint64 currentTimestamp
) {
    pruneExpired(currentTimestamp);

    if (sessionId.isEmpty() || transcriptHash.isEmpty()) {
        return true;
    }

    if (!isTimestampValid(timestamp, currentTimestamp)) {
        return true;
    }

    if (m_seenSessions.contains(sessionId)) {
        return true;
    }

    for (auto it = m_seenSessions.cbegin(); it != m_seenSessions.cend(); ++it) {
        if (it.value().transcriptHash == transcriptHash) {
            return true;
        }
    }

    return false;
}

void ReplayProtectionCache::registerSession(
    const QString &sessionId,
    const QByteArray &transcriptHash,
    qint64 timestamp
) {
    if (sessionId.isEmpty() || transcriptHash.isEmpty()) return;
    m_seenSessions.insert(sessionId, Entry{timestamp, transcriptHash});
}

void ReplayProtectionCache::pruneExpired(qint64 currentTimestamp) {
    qint64 now = (currentTimestamp > 0) ? currentTimestamp : QDateTime::currentSecsSinceEpoch();
    for (auto it = m_seenSessions.begin(); it != m_seenSessions.end();) {
        if (std::abs(now - it.value().timestamp) > REPLAY_WINDOW_SECONDS) {
            it = m_seenSessions.erase(it);
        } else {
            ++it;
        }
    }
}

void ReplayProtectionCache::clear() {
    m_seenSessions.clear();
}

// -----------------------------------------------------------------------------
// PairingSession Implementation
// -----------------------------------------------------------------------------

PairingSession::PairingSession(QObject *parent)
    : QObject(parent) {}

PairingSession::~PairingSession() {
    reset();
}

QString PairingSession::pairingStateToString(PairingState state) {
    switch (state) {
        case PairingState::Idle: return "Idle";
        case PairingState::Initiating: return "Initiating";
        case PairingState::AwaitingResponse: return "AwaitingResponse";
        case PairingState::AwaitingPeerApproval: return "AwaitingPeerApproval";
        case PairingState::Verifying: return "Verifying";
        case PairingState::Paired: return "Paired";
        case PairingState::Failed: return "Failed";
    }
    return "Unknown";
}

QString PairingSession::pairingErrorToString(PairingError error) {
    switch (error) {
        case PairingError::None: return "None";
        case PairingError::InvalidMessage: return "InvalidMessage";
        case PairingError::AuthFailed: return "AuthFailed";
        case PairingError::TimestampExpired: return "TimestampExpired";
        case PairingError::ReplayDetected: return "ReplayDetected";
        case PairingError::CryptoError: return "CryptoError";
        case PairingError::ProtocolMismatch: return "ProtocolMismatch";
        case PairingError::Cancelled: return "Cancelled";
        case PairingError::Timeout: return "Timeout";
    }
    return "UnknownError";
}

PairingState PairingSession::state() const {
    return m_state;
}

PairingError PairingSession::lastError() const {
    return m_lastError;
}

QString PairingSession::lastErrorString() const {
    return m_lastErrorString;
}

std::optional<PairingResult> PairingSession::result() const {
    return m_result;
}

void PairingSession::setState(PairingState state) {
    if (m_state != state) {
        m_state = state;
        Core::Logger::info("PairingSession", QString("Pairing state transitioned to: %1").arg(pairingStateToString(m_state)));
        emit stateChanged(m_state);
    }
}

void PairingSession::setFailed(PairingError error, const QString &reason) {
    m_lastError = error;
    m_lastErrorString = reason;
    Core::Logger::error("PairingSession", QString("Pairing failed: [%1] %2").arg(pairingErrorToString(error), reason));
    setState(PairingState::Failed);
    emit pairingFailed(error, reason);
}

void PairingSession::reset() {
    m_ephemeralKeyPair.clear();
    CryptoProvider::secureCleanse(m_salt);
    m_salt.clear();
    m_sessionId.clear();
    m_peerDeviceId.clear();
    m_result.reset();
    m_lastError = PairingError::None;
    m_lastErrorString.clear();
    setState(PairingState::Idle);
}

void PairingSession::cancelPairing() {
    if (m_state != PairingState::Idle && m_state != PairingState::Failed) {
        Core::Logger::info("PairingSession", "Pairing cancelled by user.");
        setFailed(PairingError::Cancelled, "Pairing cancelled by local user.");
        reset();
    }
}

std::optional<Protocol::PairingRequest> PairingSession::initiatePairing(
    const QString &peerDeviceId,
    const QString &sharedSecretPassword,
    const QString &localVirtualIp
) {
    reset();
    setState(PairingState::Initiating);

    QString localDeviceId = Core::AppConfig::instance().deviceId();
    if (localDeviceId.isEmpty() || peerDeviceId.isEmpty() || sharedSecretPassword.isEmpty()) {
        setFailed(PairingError::InvalidMessage, "Device IDs and shared secret password cannot be empty.");
        return std::nullopt;
    }

    // 1. Generate fresh ephemeral X25519 keypair
    auto keyPairOpt = CryptoProvider::generateX25519KeyPair();
    if (!keyPairOpt.has_value()) {
        setFailed(PairingError::CryptoError, "Failed to generate ephemeral X25519 keypair.");
        return std::nullopt;
    }
    m_ephemeralKeyPair = std::move(*keyPairOpt);

    // 2. Generate cryptographically random 16-byte salt
    m_salt = CryptoProvider::generateSecureRandomBytes(16);
    if (m_salt.size() != 16) {
        setFailed(PairingError::CryptoError, "Failed to generate CSPRNG salt.");
        return std::nullopt;
    }

    // 3. Derive Argon2id authentication key
    auto kdfKeyOpt = CryptoProvider::deriveArgon2idKey(sharedSecretPassword, m_salt);
    if (!kdfKeyOpt.has_value()) {
        setFailed(PairingError::CryptoError, "Argon2id derivation failed.");
        return std::nullopt;
    }
    QByteArray kdfKey = std::move(*kdfKeyOpt);

    // 4. Construct pairing request
    m_sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_peerDeviceId = peerDeviceId;

    Protocol::PairingRequest req;
    req.sessionId = m_sessionId;
    req.senderDeviceId = localDeviceId;
    req.receiverDeviceId = peerDeviceId;
    req.timestamp = QDateTime::currentSecsSinceEpoch();
    req.saltBase64 = m_salt.toBase64();
    req.ephemeralPublicKeyBase64 = m_ephemeralKeyPair.publicKey.toBase64();
    req.proposedVirtualIp = localVirtualIp.isEmpty() ? Core::AppConfig::instance().virtualIp() : localVirtualIp;

    // 5. Compute HMAC-SHA256 authentication proof
    QByteArray transcript = req.buildTranscriptMessage();
    auto proofOpt = CryptoProvider::computeHmacSha256(kdfKey, transcript);
    CryptoProvider::secureCleanse(kdfKey);

    if (!proofOpt.has_value()) {
        setFailed(PairingError::CryptoError, "HMAC-SHA256 proof computation failed.");
        return std::nullopt;
    }
    req.authProofBase64 = proofOpt->toBase64();

    // Register session in local replay cache
    QByteArray transcriptHash = QCryptographicHash::hash(transcript, QCryptographicHash::Sha256);
    m_replayCache.registerSession(req.sessionId, transcriptHash, req.timestamp);

    setState(PairingState::AwaitingResponse);
    Core::Logger::info("PairingSession", QString("Pairing request prepared for peer %1 (Session %2)").arg(peerDeviceId, m_sessionId));
    return req;
}

std::optional<Protocol::PairingResponse> PairingSession::handleIncomingRequest(
    const Protocol::PairingRequest &request,
    const QString &sharedSecretPassword,
    const QString &localVirtualIp
) {
    reset();
    setState(PairingState::Verifying);

    QString localDeviceId = Core::AppConfig::instance().deviceId();

    // 1. Validate Target Device ID
    if (request.receiverDeviceId != localDeviceId) {
        setFailed(PairingError::InvalidMessage, "Pairing request is addressed to a different device ID.");
        return std::nullopt;
    }

    // 2. Validate Timestamp & Replay Window (300 seconds)
    qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!m_replayCache.isTimestampValid(request.timestamp, now)) {
        setFailed(PairingError::TimestampExpired, QString("Pairing request timestamp expired (|%1 - %2| > 300s).").arg(now).arg(request.timestamp));
        return std::nullopt;
    }

    // 3. Check for replay attack
    QByteArray transcript = request.buildTranscriptMessage();
    QByteArray transcriptHash = QCryptographicHash::hash(transcript, QCryptographicHash::Sha256);
    if (m_replayCache.isReplayed(request.sessionId, transcriptHash, request.timestamp, now)) {
        setFailed(PairingError::ReplayDetected, "Replayed pairing request detected.");
        return std::nullopt;
    }

    // 4. Decode Public Key & Salt
    QByteArray peerPubKey = QByteArray::fromBase64(request.ephemeralPublicKeyBase64.toUtf8());
    QByteArray salt = QByteArray::fromBase64(request.saltBase64.toUtf8());
    QByteArray peerProof = QByteArray::fromBase64(request.authProofBase64.toUtf8());

    if (peerPubKey.size() != 32 || salt.size() < 8 || peerProof.size() != 32) {
        setFailed(PairingError::InvalidMessage, "Malformed public key, salt, or auth proof size.");
        return std::nullopt;
    }

    // 5. Derive Argon2id key and verify authentication proof
    auto kdfKeyOpt = CryptoProvider::deriveArgon2idKey(sharedSecretPassword, salt);
    if (!kdfKeyOpt.has_value()) {
        setFailed(PairingError::CryptoError, "Argon2id derivation failed on request verification.");
        return std::nullopt;
    }
    QByteArray kdfKey = std::move(*kdfKeyOpt);

    auto expectedProofOpt = CryptoProvider::computeHmacSha256(kdfKey, transcript);
    if (!expectedProofOpt.has_value()) {
        CryptoProvider::secureCleanse(kdfKey);
        setFailed(PairingError::CryptoError, "Failed to compute expected authentication proof.");
        return std::nullopt;
    }

    bool authValid = CryptoProvider::verifyHmacConstantTime(*expectedProofOpt, peerProof);
    if (!authValid) {
        CryptoProvider::secureCleanse(kdfKey);
        setFailed(PairingError::AuthFailed, "Pairing authentication failed: Incorrect shared secret password or proof mismatch.");
        return std::nullopt;
    }

    // Register incoming session in replay cache
    m_replayCache.registerSession(request.sessionId, transcriptHash, request.timestamp);

    // 6. Generate our own ephemeral X25519 keypair
    auto ourKeyPairOpt = CryptoProvider::generateX25519KeyPair();
    if (!ourKeyPairOpt.has_value()) {
        CryptoProvider::secureCleanse(kdfKey);
        setFailed(PairingError::CryptoError, "Failed to generate responder ephemeral keypair.");
        return std::nullopt;
    }
    m_ephemeralKeyPair = std::move(*ourKeyPairOpt);

    // 7. Derive ECDH shared secret
    auto sharedSecretOpt = CryptoProvider::deriveSharedSecret(m_ephemeralKeyPair.privateKey, peerPubKey);
    if (!sharedSecretOpt.has_value()) {
        CryptoProvider::secureCleanse(kdfKey);
        setFailed(PairingError::CryptoError, "ECDH shared secret derivation failed.");
        return std::nullopt;
    }

    // 8. Construct PairingResponse
    Protocol::PairingResponse resp;
    resp.sessionId = request.sessionId;
    resp.senderDeviceId = localDeviceId;
    resp.receiverDeviceId = request.senderDeviceId;
    resp.timestamp = QDateTime::currentSecsSinceEpoch();
    resp.saltBase64 = salt.toBase64();
    resp.ephemeralPublicKeyBase64 = m_ephemeralKeyPair.publicKey.toBase64();
    resp.proposedVirtualIp = localVirtualIp.isEmpty() ? Core::AppConfig::instance().virtualIp() : localVirtualIp;
    resp.status = "accepted";

    // 9. Compute responder authentication proof
    QByteArray respTranscript = resp.buildTranscriptMessage();
    auto respProofOpt = CryptoProvider::computeHmacSha256(kdfKey, respTranscript);
    CryptoProvider::secureCleanse(kdfKey);

    if (!respProofOpt.has_value()) {
        setFailed(PairingError::CryptoError, "Failed to compute response proof.");
        return std::nullopt;
    }
    resp.authProofBase64 = respProofOpt->toBase64();

    // Register response in replay cache
    QByteArray respTranscriptHash = QCryptographicHash::hash(respTranscript, QCryptographicHash::Sha256);
    m_replayCache.registerSession(resp.sessionId + "_resp", respTranscriptHash, resp.timestamp);

    // 10. Finalize Paired State
    PairingResult res;
    res.peerDeviceId = request.senderDeviceId;
    res.peerPublicKeyBase64 = request.ephemeralPublicKeyBase64;
    res.peerVirtualIp = request.proposedVirtualIp;
    res.sharedSecret = *sharedSecretOpt;
    m_result = res;

    setState(PairingState::Paired);
    Core::Logger::info("PairingSession", QString("Pairing handshake successfully completed with peer %1").arg(res.peerDeviceId));
    emit pairingSucceeded(res);

    return resp;
}

bool PairingSession::handleIncomingResponse(
    const Protocol::PairingResponse &response,
    const QString &sharedSecretPassword
) {
    if (m_state != PairingState::AwaitingResponse) {
        setFailed(PairingError::InvalidMessage, "Unexpected pairing response received in non-awaiting state.");
        return false;
    }

    setState(PairingState::Verifying);

    QString localDeviceId = Core::AppConfig::instance().deviceId();

    if (response.sessionId != m_sessionId) {
        setFailed(PairingError::InvalidMessage, "Session ID mismatch in pairing response.");
        return false;
    }

    if (response.receiverDeviceId != localDeviceId || response.senderDeviceId != m_peerDeviceId) {
        setFailed(PairingError::InvalidMessage, "Device ID mismatch in pairing response.");
        return false;
    }

    if (response.status != "accepted") {
        setFailed(PairingError::AuthFailed, response.errorMessage.value_or("Peer rejected pairing request."));
        return false;
    }

    // Validate Timestamp & Replay
    qint64 now = QDateTime::currentSecsSinceEpoch();
    if (!m_replayCache.isTimestampValid(response.timestamp, now)) {
        setFailed(PairingError::TimestampExpired, "Pairing response timestamp expired (>300s).");
        return false;
    }

    QByteArray respTranscript = response.buildTranscriptMessage();
    QByteArray respTranscriptHash = QCryptographicHash::hash(respTranscript, QCryptographicHash::Sha256);
    if (m_replayCache.isReplayed(response.sessionId + "_resp", respTranscriptHash, response.timestamp, now)) {
        setFailed(PairingError::ReplayDetected, "Replayed pairing response detected.");
        return false;
    }

    // Decode Public Key & Salt
    QByteArray peerPubKey = QByteArray::fromBase64(response.ephemeralPublicKeyBase64.toUtf8());
    QByteArray salt = QByteArray::fromBase64(response.saltBase64.toUtf8());
    QByteArray peerProof = QByteArray::fromBase64(response.authProofBase64.toUtf8());

    if (peerPubKey.size() != 32 || salt.size() < 8 || peerProof.size() != 32) {
        setFailed(PairingError::InvalidMessage, "Malformed response public key, salt, or proof size.");
        return false;
    }

    // Derive Argon2id key and verify response proof
    auto kdfKeyOpt = CryptoProvider::deriveArgon2idKey(sharedSecretPassword, salt);
    if (!kdfKeyOpt.has_value()) {
        setFailed(PairingError::CryptoError, "Argon2id derivation failed on response verification.");
        return false;
    }
    QByteArray kdfKey = std::move(*kdfKeyOpt);

    auto expectedProofOpt = CryptoProvider::computeHmacSha256(kdfKey, respTranscript);
    CryptoProvider::secureCleanse(kdfKey);

    if (!expectedProofOpt.has_value()) {
        setFailed(PairingError::CryptoError, "Failed to compute expected response proof.");
        return false;
    }

    bool authValid = CryptoProvider::verifyHmacConstantTime(*expectedProofOpt, peerProof);
    if (!authValid) {
        setFailed(PairingError::AuthFailed, "Peer response authentication failed: Proof mismatch or invalid password.");
        return false;
    }

    // Derive ECDH shared secret
    auto sharedSecretOpt = CryptoProvider::deriveSharedSecret(m_ephemeralKeyPair.privateKey, peerPubKey);
    if (!sharedSecretOpt.has_value()) {
        setFailed(PairingError::CryptoError, "ECDH shared secret derivation failed on initiator side.");
        return false;
    }

    m_replayCache.registerSession(response.sessionId + "_resp", respTranscriptHash, response.timestamp);

    PairingResult res;
    res.peerDeviceId = response.senderDeviceId;
    res.peerPublicKeyBase64 = response.ephemeralPublicKeyBase64;
    res.peerVirtualIp = response.proposedVirtualIp;
    res.sharedSecret = *sharedSecretOpt;
    m_result = res;

    setState(PairingState::Paired);
    Core::Logger::info("PairingSession", QString("Pairing successfully authenticated with peer %1").arg(res.peerDeviceId));
    emit pairingSucceeded(res);

    return true;
}

} // namespace MeckChat::Crypto
