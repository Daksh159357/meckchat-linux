#pragma once

#include <QObject>
#include <QString>
#include <QByteArray>
#include <QDateTime>
#include <QMap>
#include <QSet>
#include <optional>
#include <memory>

#include "meckchat/protocol/models.h"
#include "meckchat/crypto/crypto_provider.h"

namespace MeckChat::Crypto {

enum class PairingState {
    Idle,
    Initiating,
    AwaitingResponse,
    AwaitingPeerApproval,
    Verifying,
    Paired,
    Failed
};

enum class PairingError {
    None,
    InvalidMessage,
    AuthFailed,
    TimestampExpired,
    ReplayDetected,
    CryptoError,
    ProtocolMismatch,
    Cancelled,
    Timeout
};

struct PairingResult {
    QString peerDeviceId;
    QString peerPublicKeyBase64;
    QString peerVirtualIp;
    QByteArray sharedSecret; // 32-byte derived ECDH shared secret
};

class ReplayProtectionCache {
public:
    static constexpr qint64 REPLAY_WINDOW_SECONDS = 300; // 300 seconds

    bool isTimestampValid(qint64 timestamp, qint64 currentTimestamp = 0) const;
    bool isReplayed(const QString &sessionId, const QByteArray &transcriptHash, qint64 timestamp, qint64 currentTimestamp = 0);
    void registerSession(const QString &sessionId, const QByteArray &transcriptHash, qint64 timestamp);
    void pruneExpired(qint64 currentTimestamp = 0);
    void clear();

private:
    struct Entry {
        qint64 timestamp;
        QByteArray transcriptHash;
    };
    QMap<QString, Entry> m_seenSessions;
};

class PairingSession : public QObject {
    Q_OBJECT

public:
    explicit PairingSession(QObject *parent = nullptr);
    ~PairingSession() override;

    // Initiator side: Generate ephemeral keypair, compute proof, return PairingRequest
    std::optional<Protocol::PairingRequest> initiatePairing(
        const QString &peerDeviceId,
        const QString &sharedSecretPassword,
        const QString &localVirtualIp = QString()
    );

    // Responder side: Verify request proof, generate ephemeral keypair, return PairingResponse
    std::optional<Protocol::PairingResponse> handleIncomingRequest(
        const Protocol::PairingRequest &request,
        const QString &sharedSecretPassword,
        const QString &localVirtualIp = QString()
    );

    // Initiator side: Process peer's response, verify peer proof, establish shared secret
    bool handleIncomingResponse(
        const Protocol::PairingResponse &response,
        const QString &sharedSecretPassword
    );

    void cancelPairing();
    void reset();

    PairingState state() const;
    PairingError lastError() const;
    QString lastErrorString() const;
    std::optional<PairingResult> result() const;

    static QString pairingStateToString(PairingState state);
    static QString pairingErrorToString(PairingError error);

signals:
    void stateChanged(PairingState newState);
    void pairingSucceeded(const MeckChat::Crypto::PairingResult &result);
    void pairingFailed(MeckChat::Crypto::PairingError error, const QString &reason);

private:
    void setState(PairingState state);
    void setFailed(PairingError error, const QString &reason);

    PairingState m_state{PairingState::Idle};
    PairingError m_lastError{PairingError::None};
    QString m_lastErrorString;

    // Ephemeral session data
    QString m_sessionId;
    QString m_peerDeviceId;
    KeyPair m_ephemeralKeyPair;
    QByteArray m_salt;
    std::optional<PairingResult> m_result;

    ReplayProtectionCache m_replayCache;
};

} // namespace MeckChat::Crypto
