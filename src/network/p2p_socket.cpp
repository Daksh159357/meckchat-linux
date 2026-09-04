#include "meckchat/network/p2p_socket.h"
#include "meckchat/core/logger.h"
#include "meckchat/network/netlink_wireguard.h"
#include <QtEndian>

namespace MeckChat::Network {

P2PSocket::P2PSocket(QObject *parent)
    : QObject(parent),
      m_socket(new QTcpSocket(this)) {

    connect(m_socket, &QTcpSocket::connected, this, &P2PSocket::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &P2PSocket::onSocketDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &P2PSocket::onSocketReadyRead);
    connect(m_socket, &QTcpSocket::bytesWritten, this, &P2PSocket::onSocketBytesWritten);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &P2PSocket::onSocketError);
}

P2PSocket::~P2PSocket() {
    stopServer();
    if (m_socket) {
        m_socket->disconnect();
        m_socket->abort();
    }
}

bool P2PSocket::isValidPeerAddress(const QHostAddress &addr, bool enforceWireGuardSubnet) {
    if (addr.isNull() || addr.protocol() != QAbstractSocket::IPv4Protocol) {
        return false;
    }
    if (addr.isBroadcast() || addr == QHostAddress::AnyIPv4) {
        return false;
    }
    if (enforceWireGuardSubnet) {
        return NetlinkWireGuard::isSubnetValid(addr.toString());
    }
    return true;
}

QList<Protocol::P2PFrame> P2PSocket::processStreamBuffer(
    QByteArray &buffer,
    QString &errorString,
    quint32 maxPayloadSize
) {
    QList<Protocol::P2PFrame> frames;

    // Buffer growth attack protection
    if (buffer.size() > MAX_RECEIVE_BUFFER_SIZE) {
        errorString = "Receive buffer exceeded maximum size limit without valid frames. Buffer flushed.";
        buffer.clear();
        return frames;
    }

    while (buffer.size() >= 8) {
        const char *ptr = buffer.constData();

        // 1. Validate Magic (2 bytes: 0x4D 0x43)
        uint16_t magic = qFromBigEndian(*reinterpret_cast<const uint16_t*>(ptr));
        if (magic != Protocol::FRAME_MAGIC) {
            errorString = QString("Invalid frame magic: 0x%1").arg(magic, 4, 16, QChar('0'));
            // Scan forward to resynchronize or drop invalid prefix
            int nextMagicIdx = buffer.indexOf(QByteArray::fromHex("4D43"), 1);
            if (nextMagicIdx > 0) {
                buffer.remove(0, nextMagicIdx);
                continue;
            } else {
                buffer.clear();
                return frames;
            }
        }

        // 2. Extract Type & Length
        uint32_t payloadLen = qFromBigEndian(*reinterpret_cast<const uint32_t*>(ptr + 4));
        if (payloadLen > maxPayloadSize) {
            errorString = QString("Rejected oversized payload length: %1 bytes (max allowed: %2)").arg(payloadLen).arg(maxPayloadSize);
            buffer.clear();
            return frames;
        }

        int totalFrameSize = 12 + static_cast<int>(payloadLen);
        if (buffer.size() < totalFrameSize) {
            // Incomplete frame, wait for subsequent TCP chunks
            break;
        }

        // 3. Complete frame slice available
        QByteArray singlePacket = buffer.left(totalFrameSize);
        buffer.remove(0, totalFrameSize);

        auto frameOpt = Protocol::P2PFrame::decode(singlePacket);
        if (frameOpt.has_value()) {
            frames.append(*frameOpt);
        } else {
            errorString = "CRC or Frame validation failed on received packet.";
        }
    }

    return frames;
}

void P2PSocket::setState(P2PState state) {
    if (m_state != state) {
        m_state = state;
        emit stateChanged(m_state);
    }
}

P2PState P2PSocket::state() const {
    return m_state;
}

bool P2PSocket::isConnected() const {
    return (m_state == P2PState::Connected) || (!m_serverClientBuffers.isEmpty());
}

QString P2PSocket::peerAddress() const {
    if (m_state == P2PState::Connected) {
        return m_socket->peerAddress().toString();
    }
    if (!m_serverClientBuffers.isEmpty()) {
        return m_serverClientBuffers.begin().key()->peerAddress().toString();
    }
    return QString();
}

quint16 P2PSocket::peerPort() const {
    if (m_state == P2PState::Connected) {
        return m_socket->peerPort();
    }
    if (!m_serverClientBuffers.isEmpty()) {
        return m_serverClientBuffers.begin().key()->peerPort();
    }
    return 0;
}

void P2PSocket::connectToPeer(const QString &virtualIp, int port) {
    if (m_state == P2PState::Connected || m_state == P2PState::Connecting) {
        disconnectFromPeer();
    }

    m_readBuffer.clear();
    m_outgoingQueue.clear();
    m_queuedBytes = 0;
    setState(P2PState::Connecting);

    Core::Logger::info("P2PSocket", QString("Initiating P2P TCP connection to %1:%2").arg(virtualIp).arg(port));
    m_socket->connectToHost(virtualIp, static_cast<quint16>(port));
}

void P2PSocket::disconnectFromPeer() {
    if (m_state != P2PState::Disconnected && m_state != P2PState::Closing) {
        setState(P2PState::Closing);
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->disconnectFromHost();
        }
    }
}

bool P2PSocket::sendFrame(const Protocol::P2PFrame &frame) {
    QByteArray packet = frame.encode();
    bool sentAny = false;

    // 1. If client socket is connected, queue and flush
    if (m_state == P2PState::Connected && m_socket->state() == QAbstractSocket::ConnectedState) {
        if (m_queuedBytes + packet.size() <= MAX_OUTGOING_QUEUE_SIZE) {
            m_outgoingQueue.enqueue(packet);
            m_queuedBytes += packet.size();
            flushWriteQueue();
            sentAny = true;
        } else {
            Core::Logger::error("P2PSocket", "Outgoing write queue full (backpressure limit exceeded). Dropping frame.");
            emit errorOccurred("Outgoing write queue full (backpressure)");
            return false;
        }
    }

    // 2. If server has active incoming peer connections, transmit to connected server peers
    if (!m_serverClientBuffers.isEmpty()) {
        for (auto it = m_serverClientBuffers.begin(); it != m_serverClientBuffers.end(); ++it) {
            QTcpSocket *clientSock = it.key();
            if (clientSock && clientSock->state() == QAbstractSocket::ConnectedState) {
                clientSock->write(packet);
                sentAny = true;
            }
        }
    }

    if (!sentAny) {
        Core::Logger::warning("P2PSocket", "Cannot send frame: P2P socket not in Connected state and no server peers connected.");
        return false;
    }

    return true;
}

void P2PSocket::flushWriteQueue() {
    while (!m_outgoingQueue.isEmpty() && m_socket->state() == QAbstractSocket::ConnectedState) {
        const QByteArray &chunk = m_outgoingQueue.head();
        qint64 written = m_socket->write(chunk);
        if (written < 0) {
            Core::Logger::error("P2PSocket", QString("Socket write error: %1").arg(m_socket->errorString()));
            break;
        }

        if (written == chunk.size()) {
            m_outgoingQueue.dequeue();
            m_queuedBytes -= written;
        } else if (written > 0) {
            m_outgoingQueue.head().remove(0, static_cast<int>(written));
            m_queuedBytes -= written;
            break; // Wait for bytesWritten signal before continuing
        } else {
            break;
        }
    }
}

void P2PSocket::onSocketConnected() {
    Core::Logger::info("P2PSocket", QString("P2P TCP connection established with %1:%2").arg(peerAddress()).arg(peerPort()));
    setState(P2PState::Connected);
    emit connected();
    flushWriteQueue();
}

void P2PSocket::onSocketDisconnected() {
    Core::Logger::info("P2PSocket", "P2P TCP socket disconnected.");
    setState(P2PState::Disconnected);
    m_readBuffer.clear();
    m_outgoingQueue.clear();
    m_queuedBytes = 0;
    emit disconnected();
}

void P2PSocket::onSocketReadyRead() {
    m_readBuffer.append(m_socket->readAll());
    processIncomingBytes(m_socket, m_readBuffer);
}

void P2PSocket::onSocketBytesWritten(qint64 bytes) {
    Q_UNUSED(bytes);
    flushWriteQueue();
}

void P2PSocket::onSocketError(QAbstractSocket::SocketError socketError) {
    Q_UNUSED(socketError);
    QString errStr = m_socket->errorString();
    Core::Logger::warning("P2PSocket", QString("P2P socket error: %1").arg(errStr));
    emit errorOccurred(errStr);
}

void P2PSocket::processIncomingBytes(QTcpSocket *sock, QByteArray &readBuffer) {
    QString errStr;
    QList<Protocol::P2PFrame> frames = processStreamBuffer(readBuffer, errStr, MAX_FRAME_PAYLOAD_SIZE);

    if (!errStr.isEmpty()) {
        Core::Logger::warning("P2PSocket", QString("Frame decoding error: %1").arg(errStr));
        emit errorOccurred(errStr);
    }

    QString senderIp = sock ? sock->peerAddress().toString() : QString();
    for (const auto &frame : frames) {
        emit frameReceived(frame);
        emit frameReceivedFrom(frame, senderIp);
    }
}

// -----------------------------------------------------------------------------
// TCP Server Implementation
// -----------------------------------------------------------------------------

bool P2PSocket::startServer(quint16 port, const QHostAddress &bindAddress) {
    if (!m_server) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &P2PSocket::onServerNewConnection);
    }

    if (m_server->isListening()) {
        m_server->close();
    }

    if (!m_server->listen(bindAddress, port)) {
        QString errStr = QString("P2P TCP server failed to listen on %1:%2 - %3")
            .arg(bindAddress.toString()).arg(port).arg(m_server->errorString());
        Core::Logger::error("P2PSocket", errStr);
        emit errorOccurred(errStr);
        return false;
    }

    Core::Logger::info("P2PSocket", QString("P2P TCP server listening on %1:%2").arg(bindAddress.toString()).arg(port));
    return true;
}

void P2PSocket::stopServer() {
    if (m_server && m_server->isListening()) {
        Core::Logger::info("P2PSocket", "Stopping P2P TCP server.");
        m_server->close();
    }

    auto clients = m_serverClientBuffers.keys();
    m_serverClientBuffers.clear();
    for (QTcpSocket *clientSock : clients) {
        if (clientSock) {
            clientSock->disconnect();
            clientSock->abort();
            clientSock->deleteLater();
        }
    }
}

bool P2PSocket::isServerListening() const {
    return m_server && m_server->isListening();
}

quint16 P2PSocket::serverPort() const {
    return m_server ? m_server->serverPort() : 0;
}

void P2PSocket::onServerNewConnection() {
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *clientSock = m_server->nextPendingConnection();
        if (!clientSock) continue;

        if (m_serverClientBuffers.size() >= MAX_CONCURRENT_CONNECTIONS) {
            Core::Logger::warning("P2PSocket", "Max concurrent P2P connections reached. Rejecting incoming connection.");
            clientSock->abort();
            clientSock->deleteLater();
            continue;
        }

        QString peerIp = clientSock->peerAddress().toString();
        Core::Logger::info("P2PSocket", QString("Accepted incoming P2P connection from %1:%2").arg(peerIp).arg(clientSock->peerPort()));

        m_serverClientBuffers.insert(clientSock, QByteArray());
        emit peerConnected(peerIp);

        connect(clientSock, &QTcpSocket::readyRead, clientSock, [this, clientSock]() {
            if (!m_serverClientBuffers.contains(clientSock)) return;
            m_serverClientBuffers[clientSock].append(clientSock->readAll());
            processIncomingBytes(clientSock, m_serverClientBuffers[clientSock]);
        });

        connect(clientSock, &QTcpSocket::disconnected, clientSock, [this, clientSock]() {
            QString ip = clientSock->peerAddress().toString();
            Core::Logger::info("P2PSocket", QString("Incoming peer connection disconnected: %1").arg(ip));
            m_serverClientBuffers.remove(clientSock);
            emit peerDisconnected(ip);
            clientSock->deleteLater();
        });

        connect(clientSock, &QTcpSocket::errorOccurred, clientSock, [clientSock](QAbstractSocket::SocketError err) {
            Q_UNUSED(err);
            Core::Logger::warning("P2PSocket", QString("Incoming peer socket error: %1").arg(clientSock->errorString()));
        });
    }
}

} // namespace MeckChat::Network
