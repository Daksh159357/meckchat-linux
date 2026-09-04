#include "meckchat/protocol/file_transfer_controller.h"
#include "meckchat/core/logger.h"
#include <QJsonDocument>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

namespace MeckChat::Protocol {

FileTransferController::FileTransferController(Network::P2PSocket *socket, QObject *parent)
    : QObject(parent),
      m_socket(socket) {

    if (m_socket) {
        connect(m_socket, &Network::P2PSocket::frameReceivedFrom, this, &FileTransferController::onFrameReceivedFrom);
        connect(m_socket, &Network::P2PSocket::peerDisconnected, this, &FileTransferController::onPeerDisconnected);
        connect(m_socket, &Network::P2PSocket::disconnected, this, &FileTransferController::onSocketDisconnected);
    }
}

FileTransferController::~FileTransferController() {
    auto tids = m_sessions.keys();
    for (const QString &tid : tids) {
        cleanupSession(tid, true);
    }
    m_sessions.clear();
}

QString FileTransferController::calculateFileSha256(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    char buffer[DEFAULT_FILE_CHUNK_SIZE];
    while (!file.atEnd()) {
        qint64 bytesRead = file.read(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            hash.addData(QByteArrayView(buffer, bytesRead));
        } else if (bytesRead < 0) {
            return QString();
        }
    }
    return hash.result().toHex();
}

QString FileTransferController::sendFile(const QString &filePath, const QString &peerVirtualIp) {
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile() || !info.isReadable()) {
        Core::Logger::error("FileTransferController", QString("Cannot send file: file unreadable or not found: %1").arg(filePath));
        return QString();
    }

    qint64 size = info.size();
    QString sha256 = calculateFileSha256(filePath);
    if (sha256.isEmpty()) {
        Core::Logger::error("FileTransferController", QString("Failed to compute SHA-256 for file: %1").arg(filePath));
        return QString();
    }

    QString tid = generateTransferId();
    auto session = std::make_shared<FileTransferSession>();
    session->transferId = tid;
    session->fileName = sanitizeFileName(info.fileName());
    session->fileSize = size;
    session->expectedSha256 = sha256;
    session->chunkSize = DEFAULT_FILE_CHUNK_SIZE;
    session->totalChunks = (size == 0) ? 0 : static_cast<int>((size + DEFAULT_FILE_CHUNK_SIZE - 1) / DEFAULT_FILE_CHUNK_SIZE);
    session->direction = TransferDirection::Outgoing;
    session->state = TransferState::Offering;
    session->sourceFilePath = filePath;
    session->transferredBytes = 0;
    session->currentChunkIndex = 0;

    m_sessions.insert(tid, session);

    // Construct FileOffer Frame
    FileOffer offer;
    offer.transferId = tid;
    offer.fileName = session->fileName;
    offer.fileSize = size;
    offer.sha256 = sha256;
    offer.chunkSize = DEFAULT_FILE_CHUNK_SIZE;
    offer.totalChunks = session->totalChunks;

    P2PFrame frame;
    frame.type = FrameType::FileOffer;
    frame.payload = QJsonDocument(offer.toJson()).toJson(QJsonDocument::Compact);

    if (m_socket && !peerVirtualIp.isEmpty() && !m_socket->isConnected()) {
        m_socket->connectToPeer(peerVirtualIp);
    }

    if (m_socket) {
        m_socket->sendFrame(frame);
    }

    Core::Logger::info("FileTransferController", QString("Dispatched FileOffer (%1, %2 bytes, SHA-256: %3)").arg(session->fileName).arg(size).arg(sha256));
    emit transferStarted(tid, session->fileName, size, TransferDirection::Outgoing);
    return tid;
}

bool FileTransferController::acceptTransfer(const QString &transferId, const QString &destinationDir) {
    if (!m_sessions.contains(transferId)) {
        return false;
    }

    auto session = m_sessions.value(transferId);
    if (session->state != TransferState::Offering || session->direction != TransferDirection::Incoming) {
        return false;
    }

    QString targetDir = destinationDir;
    if (targetDir.isEmpty()) {
        targetDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (targetDir.isEmpty()) targetDir = QDir::currentPath();
    }

    QDir dir(targetDir);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    session->finalFilePath = dir.filePath(session->fileName);
    session->tempFilePath = dir.filePath(QString(".tmp_%1").arg(transferId));

    session->fileHandle = std::make_unique<QFile>(session->tempFilePath);
    if (!session->fileHandle->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Core::Logger::error("FileTransferController", QString("Failed to open temporary file for writing: %1").arg(session->tempFilePath));
        session->state = TransferState::Failed;
        emit transferFailed(transferId, "Failed to open temporary download file");
        return false;
    }

    session->cryptoHash.reset();
    session->state = TransferState::Transferring;
    session->transferredBytes = 0;
    session->currentChunkIndex = 0;

    // Send FileAccept Frame
    FileAccept acc;
    acc.transferId = transferId;
    P2PFrame frame;
    frame.type = FrameType::FileAccept;
    frame.payload = QJsonDocument(acc.toJson()).toJson(QJsonDocument::Compact);

    if (m_socket) {
        m_socket->sendFrame(frame);
    }

    Core::Logger::info("FileTransferController", QString("Accepted incoming transfer %1 (saving to %2)").arg(transferId).arg(session->finalFilePath));
    emit transferStarted(transferId, session->fileName, session->fileSize, TransferDirection::Incoming);

    // If file size is 0, transfer is already complete
    if (session->fileSize == 0) {
        session->fileHandle->close();
        if (QFile::exists(session->finalFilePath)) QFile::remove(session->finalFilePath);
        QFile::rename(session->tempFilePath, session->finalFilePath);
        session->state = TransferState::Completed;
        emit transferCompleted(transferId, session->finalFilePath);
    }

    return true;
}

bool FileTransferController::rejectTransfer(const QString &transferId, const QString &reason) {
    if (!m_sessions.contains(transferId)) {
        return false;
    }

    auto session = m_sessions.value(transferId);
    session->state = TransferState::Rejected;

    FileReject rej;
    rej.transferId = transferId;
    rej.reason = reason;

    P2PFrame frame;
    frame.type = FrameType::FileReject;
    frame.payload = QJsonDocument(rej.toJson()).toJson(QJsonDocument::Compact);

    if (m_socket) {
        m_socket->sendFrame(frame);
    }

    Core::Logger::info("FileTransferController", QString("Rejected file transfer: %1 (Reason: %2)").arg(transferId).arg(reason));
    emit transferRejected(transferId, reason);
    cleanupSession(transferId, true);
    return true;
}

bool FileTransferController::cancelTransfer(const QString &transferId, const QString &reason) {
    if (!m_sessions.contains(transferId)) {
        return false;
    }

    auto session = m_sessions.value(transferId);
    session->state = TransferState::Cancelled;

    FileCancel can;
    can.transferId = transferId;
    can.reason = reason;

    P2PFrame frame;
    frame.type = FrameType::FileCancel;
    frame.payload = QJsonDocument(can.toJson()).toJson(QJsonDocument::Compact);

    if (m_socket) {
        m_socket->sendFrame(frame);
    }

    Core::Logger::info("FileTransferController", QString("Cancelled file transfer: %1 (Reason: %2)").arg(transferId).arg(reason));
    emit transferCancelled(transferId, reason);
    cleanupSession(transferId, true);
    return true;
}

std::optional<TransferState> FileTransferController::getTransferState(const QString &transferId) const {
    if (m_sessions.contains(transferId)) {
        return m_sessions.value(transferId)->state;
    }
    return std::nullopt;
}

qint64 FileTransferController::getTransferredBytes(const QString &transferId) const {
    if (m_sessions.contains(transferId)) {
        return m_sessions.value(transferId)->transferredBytes;
    }
    return 0;
}

qint64 FileTransferController::getTotalBytes(const QString &transferId) const {
    if (m_sessions.contains(transferId)) {
        return m_sessions.value(transferId)->fileSize;
    }
    return 0;
}

void FileTransferController::onFrameReceivedFrom(const P2PFrame &frame, const QString &senderIp) {
    Q_UNUSED(senderIp);
    switch (frame.type) {
        case FrameType::FileOffer:
            handleInboundFileOffer(frame.payload);
            break;
        case FrameType::FileAccept:
            handleInboundFileAccept(frame.payload);
            break;
        case FrameType::FileReject:
            handleInboundFileReject(frame.payload);
            break;
        case FrameType::FileChunk:
            handleInboundFileChunk(frame.payload);
            break;
        case FrameType::FileComplete:
            handleInboundFileComplete(frame.payload);
            break;
        case FrameType::FileCancel:
            handleInboundFileCancel(frame.payload);
            break;
        default:
            break;
    }
}

void FileTransferController::handleInboundFileOffer(const QByteArray &payload) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        Core::Logger::warning("FileTransferController", "Received malformed FileOffer payload.");
        return;
    }

    auto offerOpt = FileOffer::fromJson(doc.object());
    if (!offerOpt.has_value()) {
        Core::Logger::warning("FileTransferController", "Failed to deserialize FileOffer fields.");
        return;
    }

    const FileOffer &offer = *offerOpt;
    auto session = std::make_shared<FileTransferSession>();
    session->transferId = offer.transferId;
    session->fileName = sanitizeFileName(offer.fileName);
    session->fileSize = offer.fileSize;
    session->expectedSha256 = offer.sha256;
    session->chunkSize = offer.chunkSize > 0 ? offer.chunkSize : DEFAULT_FILE_CHUNK_SIZE;
    session->totalChunks = offer.totalChunks;
    session->direction = TransferDirection::Incoming;
    session->state = TransferState::Offering;

    m_sessions.insert(offer.transferId, session);

    Core::Logger::info("FileTransferController", QString("Received FileOffer %1 for %2 (%3 bytes)").arg(offer.transferId).arg(session->fileName).arg(session->fileSize));
    emit fileOfferReceived(offer);
}

void FileTransferController::handleInboundFileAccept(const QByteArray &payload) {
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    auto accOpt = FileAccept::fromJson(doc.object());
    if (!accOpt.has_value()) return;

    QString tid = accOpt->transferId;
    if (!m_sessions.contains(tid)) return;

    auto session = m_sessions.value(tid);
    if (session->direction != TransferDirection::Outgoing || session->state != TransferState::Offering) {
        return;
    }

    session->fileHandle = std::make_unique<QFile>(session->sourceFilePath);
    if (!session->fileHandle->open(QIODevice::ReadOnly)) {
        Core::Logger::error("FileTransferController", QString("Failed to open source file for reading: %1").arg(session->sourceFilePath));
        session->state = TransferState::Failed;
        emit transferFailed(tid, "Failed to open source file for reading");
        return;
    }

    session->state = TransferState::Transferring;
    session->transferredBytes = 0;
    session->currentChunkIndex = 0;

    Core::Logger::info("FileTransferController", QString("Remote peer accepted file transfer %1. Starting chunk streaming.").arg(tid));

    // Start streaming chunks
    sendNextChunk(tid);
}

void FileTransferController::sendNextChunk(const QString &transferId) {
    if (!m_sessions.contains(transferId)) return;

    auto session = m_sessions.value(transferId);
    if (session->state != TransferState::Transferring || !session->fileHandle || !session->fileHandle->isOpen()) {
        return;
    }

    if (session->fileHandle->atEnd() || session->transferredBytes >= session->fileSize) {
        // All chunks sent! Send FileComplete
        session->fileHandle->close();
        session->state = TransferState::Verifying;

        FileComplete comp;
        comp.transferId = transferId;
        comp.sha256 = session->expectedSha256;
        comp.status = "verified";

        P2PFrame frame;
        frame.type = FrameType::FileComplete;
        frame.payload = QJsonDocument(comp.toJson()).toJson(QJsonDocument::Compact);

        if (m_socket) {
            m_socket->sendFrame(frame);
        }

        session->state = TransferState::Completed;
        Core::Logger::info("FileTransferController", QString("All chunks transmitted for %1. FileComplete sent.").arg(transferId));
        emit transferCompleted(transferId, session->sourceFilePath);
        return;
    }

    QByteArray chunkData = session->fileHandle->read(session->chunkSize);
    if (chunkData.isEmpty() && session->fileSize > 0) {
        session->state = TransferState::Failed;
        emit transferFailed(transferId, "Error reading chunk from source file");
        return;
    }

    QByteArray packet = FileChunk::encode(transferId, session->currentChunkIndex, chunkData);

    P2PFrame frame;
    frame.type = FrameType::FileChunk;
    frame.payload = packet;

    if (m_socket) {
        m_socket->sendFrame(frame);
    }

    session->transferredBytes += chunkData.size();
    session->currentChunkIndex++;

    emit transferProgress(transferId, session->transferredBytes, session->fileSize);

    // Schedule next chunk on Qt event loop to prevent blocking and allow socket drain
    QTimer::singleShot(0, this, [this, transferId]() {
        sendNextChunk(transferId);
    });
}

void FileTransferController::handleInboundFileReject(const QByteArray &payload) {
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    auto rejOpt = FileReject::fromJson(doc.object());
    if (!rejOpt.has_value()) return;

    QString tid = rejOpt->transferId;
    if (!m_sessions.contains(tid)) return;

    auto session = m_sessions.value(tid);
    session->state = TransferState::Rejected;

    Core::Logger::info("FileTransferController", QString("File transfer %1 was rejected by remote peer. Reason: %2").arg(tid).arg(rejOpt->reason));
    emit transferRejected(tid, rejOpt->reason);
    cleanupSession(tid, true);
}

void FileTransferController::handleInboundFileChunk(const QByteArray &payload) {
    QString tid;
    uint32_t chunkIndex = 0;
    QByteArray chunkData;

    if (!FileChunk::decode(payload, tid, chunkIndex, chunkData)) {
        Core::Logger::warning("FileTransferController", "Failed to decode incoming FileChunk packet.");
        return;
    }

    if (!m_sessions.contains(tid)) {
        Core::Logger::warning("FileTransferController", QString("Received FileChunk for unknown transfer ID: %1").arg(tid));
        return;
    }

    auto session = m_sessions.value(tid);
    if (session->state != TransferState::Transferring || session->direction != TransferDirection::Incoming) {
        Core::Logger::warning("FileTransferController", QString("Received FileChunk while session %1 not in Transferring state.").arg(tid));
        return;
    }

    if (chunkIndex != session->currentChunkIndex) {
        Core::Logger::error("FileTransferController", QString("Out-of-order chunk received: expected %1, got %2").arg(session->currentChunkIndex).arg(chunkIndex));
        session->state = TransferState::Failed;
        emit transferFailed(tid, "Out-of-order chunk received");
        cleanupSession(tid, true);
        return;
    }

    if (session->fileHandle && session->fileHandle->isOpen()) {
        session->fileHandle->write(chunkData);
    }
    session->cryptoHash.addData(chunkData);
    session->transferredBytes += chunkData.size();
    session->currentChunkIndex++;

    emit transferProgress(tid, session->transferredBytes, session->fileSize);
}

void FileTransferController::handleInboundFileComplete(const QByteArray &payload) {
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    auto compOpt = FileComplete::fromJson(doc.object());
    if (!compOpt.has_value()) return;

    QString tid = compOpt->transferId;
    if (!m_sessions.contains(tid)) return;

    auto session = m_sessions.value(tid);
    if (session->direction != TransferDirection::Incoming || session->state != TransferState::Transferring) {
        return;
    }

    session->state = TransferState::Verifying;
    if (session->fileHandle && session->fileHandle->isOpen()) {
        session->fileHandle->flush();
        session->fileHandle->close();
    }

    QString calculatedSha256 = session->cryptoHash.result().toHex();
    if (calculatedSha256.compare(session->expectedSha256, Qt::CaseInsensitive) == 0) {
        // Integrity verification passed! Atomic rename to final path
        if (QFile::exists(session->finalFilePath)) {
            QFile::remove(session->finalFilePath);
        }
        if (QFile::rename(session->tempFilePath, session->finalFilePath)) {
            session->state = TransferState::Completed;
            Core::Logger::info("FileTransferController", QString("File transfer %1 successfully completed and verified! Saved to: %2").arg(tid).arg(session->finalFilePath));
            emit transferCompleted(tid, session->finalFilePath);
        } else {
            session->state = TransferState::Failed;
            emit transferFailed(tid, "Failed to move temporary file to destination path");
            cleanupSession(tid, true);
        }
    } else {
        // Hash mismatch! Corrupted file
        Core::Logger::error("FileTransferController", QString("SHA-256 integrity check failed for %1! Expected: %2, Computed: %3").arg(tid).arg(session->expectedSha256).arg(calculatedSha256));
        session->state = TransferState::Failed;
        emit transferFailed(tid, "SHA-256 checksum mismatch (corrupted file)");
        cleanupSession(tid, true);
    }
}

void FileTransferController::handleInboundFileCancel(const QByteArray &payload) {
    QJsonDocument doc = QJsonDocument::fromJson(payload);
    auto canOpt = FileCancel::fromJson(doc.object());
    if (!canOpt.has_value()) return;

    QString tid = canOpt->transferId;
    if (!m_sessions.contains(tid)) return;

    auto session = m_sessions.value(tid);
    session->state = TransferState::Cancelled;

    Core::Logger::info("FileTransferController", QString("File transfer %1 was cancelled by remote peer. Reason: %2").arg(tid).arg(canOpt->reason));
    emit transferCancelled(tid, canOpt->reason);
    cleanupSession(tid, true);
}

void FileTransferController::cleanupSession(const QString &transferId, bool removeTempFile) {
    if (!m_sessions.contains(transferId)) return;

    auto session = m_sessions.value(transferId);
    if (session->fileHandle && session->fileHandle->isOpen()) {
        session->fileHandle->close();
    }

    if (removeTempFile && !session->tempFilePath.isEmpty() && QFile::exists(session->tempFilePath)) {
        QFile::remove(session->tempFilePath);
    }
}

void FileTransferController::onPeerDisconnected(const QString &peerIp) {
    Q_UNUSED(peerIp);
    onSocketDisconnected();
}

void FileTransferController::onSocketDisconnected() {
    auto tids = m_sessions.keys();
    for (const QString &tid : tids) {
        auto session = m_sessions.value(tid);
        if (session->state == TransferState::Offering || session->state == TransferState::Transferring) {
            session->state = TransferState::Failed;
            emit transferFailed(tid, "Peer disconnected during file transfer");
            cleanupSession(tid, true);
        }
    }
}

} // namespace MeckChat::Protocol
