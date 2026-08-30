#include "meckchat/network/p2p_socket.h"
#include "meckchat/core/logger.h"

namespace MeckChat::Network {

P2PSocket::P2PSocket(QObject *parent)
    : QObject(parent), m_socket(new QTcpSocket(this)) {
    connect(m_socket, &QTcpSocket::connected, this, &P2PSocket::connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &P2PSocket::disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &P2PSocket::onReadyRead);
}

void P2PSocket::connectToPeer(const QString &virtualIp, int port) {
    Core::Logger::info("P2PSocket", QString("Connecting to peer at %1:%2").arg(virtualIp).arg(port));
    m_socket->connectToHost(virtualIp, port);
}

void P2PSocket::disconnectFromPeer() {
    m_socket->disconnectFromHost();
}

bool P2PSocket::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool P2PSocket::sendFrame(const Protocol::P2PFrame &frame) {
    if (!isConnected()) {
        Core::Logger::warning("P2PSocket", "Cannot send frame: socket not connected");
        return false;
    }
    QByteArray packet = frame.encode();
    qint64 written = m_socket->write(packet);
    return written == packet.size();
}

void P2PSocket::onReadyRead() {
    m_readBuffer.append(m_socket->readAll());

    while (m_readBuffer.size() >= 12) {
        const char *ptr = m_readBuffer.constData();
        uint32_t payloadLen = qFromBigEndian(*reinterpret_cast<const uint32_t*>(ptr + 4));
        int totalExpected = 12 + static_cast<int>(payloadLen);

        if (m_readBuffer.size() < totalExpected) {
            break; // Wait for more data
        }

        QByteArray singlePacket = m_readBuffer.left(totalExpected);
        m_readBuffer.remove(0, totalExpected);

        auto frameOpt = Protocol::P2PFrame::decode(singlePacket);
        if (frameOpt.has_value()) {
            emit frameReceived(*frameOpt);
        } else {
            Core::Logger::error("P2PSocket", "CRC or Magic mismatch on received frame");
            emit errorOccurred("CRC or Magic mismatch");
        }
    }
}

} // namespace MeckChat::Network
