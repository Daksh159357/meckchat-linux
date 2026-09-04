#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QFile>
#include <QCryptographicHash>
#include <memory>
#include "meckchat/protocol/models.h"
#include "meckchat/protocol/framing.h"
#include "meckchat/network/p2p_socket.h"

namespace MeckChat::Protocol {

enum class TransferState {
    Idle,
    Offering,
    Accepted,
    Rejected,
    Transferring,
    Verifying,
    Completed,
    Failed,
    Cancelled
};

enum class TransferDirection {
    Outgoing,
    Incoming
};

struct FileTransferSession {
    QString transferId;
    QString fileName;
    qint64 fileSize{0};
    QString expectedSha256;
    int chunkSize{DEFAULT_FILE_CHUNK_SIZE};
    int totalChunks{0};
    TransferDirection direction{TransferDirection::Outgoing};
    TransferState state{TransferState::Idle};

    qint64 transferredBytes{0};
    uint32_t currentChunkIndex{0};

    QString sourceFilePath;
    QString tempFilePath;
    QString finalFilePath;

    std::unique_ptr<QFile> fileHandle;
    QCryptographicHash cryptoHash{QCryptographicHash::Sha256};
};

class FileTransferController : public QObject {
    Q_OBJECT

public:
    explicit FileTransferController(Network::P2PSocket *socket, QObject *parent = nullptr);
    ~FileTransferController() override;

    // Sender API
    QString sendFile(const QString &filePath, const QString &peerVirtualIp = QString());

    // Receiver API
    bool acceptTransfer(const QString &transferId, const QString &destinationDir = QString());
    bool rejectTransfer(const QString &transferId, const QString &reason = "user_rejected");

    // Cancellation API
    bool cancelTransfer(const QString &transferId, const QString &reason = "cancelled_by_user");

    // Queries
    std::optional<TransferState> getTransferState(const QString &transferId) const;
    qint64 getTransferredBytes(const QString &transferId) const;
    qint64 getTotalBytes(const QString &transferId) const;

    // Helper to calculate file SHA-256 in a streaming fashion (bounded memory)
    static QString calculateFileSha256(const QString &filePath);

signals:
    void fileOfferReceived(const MeckChat::Protocol::FileOffer &offer);
    void transferStarted(const QString &transferId, const QString &fileName, qint64 fileSize, MeckChat::Protocol::TransferDirection direction);
    void transferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes);
    void transferCompleted(const QString &transferId, const QString &filePath);
    void transferFailed(const QString &transferId, const QString &errorString);
    void transferRejected(const QString &transferId, const QString &reason);
    void transferCancelled(const QString &transferId, const QString &reason);

private slots:
    void onFrameReceivedFrom(const MeckChat::Protocol::P2PFrame &frame, const QString &senderIp);
    void onPeerDisconnected(const QString &peerIp);
    void onSocketDisconnected();

private:
    void handleInboundFileOffer(const QByteArray &payload);
    void handleInboundFileAccept(const QByteArray &payload);
    void handleInboundFileReject(const QByteArray &payload);
    void handleInboundFileChunk(const QByteArray &payload);
    void handleInboundFileComplete(const QByteArray &payload);
    void handleInboundFileCancel(const QByteArray &payload);

    void sendNextChunk(const QString &transferId);
    void cleanupSession(const QString &transferId, bool removeTempFile = false);

    Network::P2PSocket *m_socket{nullptr};
    QMap<QString, std::shared_ptr<FileTransferSession>> m_sessions;
};

} // namespace MeckChat::Protocol
