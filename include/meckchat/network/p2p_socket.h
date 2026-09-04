#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTcpServer>
#include <QHostAddress>
#include <QByteArray>
#include <QList>
#include <QQueue>
#include <QMap>
#include <QTimer>
#include "meckchat/protocol/framing.h"

namespace MeckChat::Network {

enum class P2PState {
    Disconnected,
    Connecting,
    Connected,
    Closing
};

class P2PSocket : public QObject {
    Q_OBJECT

public:
    explicit P2PSocket(QObject *parent = nullptr);
    ~P2PSocket() override;

    // Client operations
    void connectToPeer(const QString &virtualIp, int port = 7788);
    void disconnectFromPeer();
    bool isConnected() const;
    P2PState state() const;

    QString peerAddress() const;
    quint16 peerPort() const;

    // Send frame to active connection or default peer
    bool sendFrame(const Protocol::P2PFrame &frame);

    // Server operations
    bool startServer(quint16 port = 7788, const QHostAddress &bindAddress = QHostAddress::AnyIPv4);
    void stopServer();
    bool isServerListening() const;
    quint16 serverPort() const;

    // Stream decoding helper (public for unit testing)
    static QList<Protocol::P2PFrame> processStreamBuffer(
        QByteArray &buffer,
        QString &errorString,
        quint32 maxPayloadSize = 4 * 1024 * 1024
    );

    // Address validation
    static bool isValidPeerAddress(const QHostAddress &addr, bool enforceWireGuardSubnet = false);

signals:
    void connected();
    void disconnected();
    void stateChanged(P2PState newState);
    void frameReceived(const Protocol::P2PFrame &frame);
    void frameReceivedFrom(const Protocol::P2PFrame &frame, const QString &senderIp);
    void peerConnected(const QString &peerIp);
    void peerDisconnected(const QString &peerIp);
    void errorOccurred(const QString &error);

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketReadyRead();
    void onSocketBytesWritten(qint64 bytes);
    void onSocketError(QAbstractSocket::SocketError socketError);
    void onServerNewConnection();

private:
    void setState(P2PState state);
    void processIncomingBytes(QTcpSocket *sock, QByteArray &readBuffer);
    void flushWriteQueue();

    // Client socket
    QTcpSocket *m_socket{nullptr};
    P2PState m_state{P2PState::Disconnected};
    QByteArray m_readBuffer;
    QQueue<QByteArray> m_outgoingQueue;
    qint64 m_queuedBytes{0};

    // Server
    QTcpServer *m_server{nullptr};
    QMap<QTcpSocket*, QByteArray> m_serverClientBuffers;

    // Configurable limits
    static constexpr quint32 MAX_FRAME_PAYLOAD_SIZE = 4 * 1024 * 1024; // 4 MB
    static constexpr int MAX_RECEIVE_BUFFER_SIZE = 8 * 1024 * 1024;    // 8 MB
    static constexpr qint64 MAX_OUTGOING_QUEUE_SIZE = 16 * 1024 * 1024; // 16 MB
    static constexpr int MAX_CONCURRENT_CONNECTIONS = 32;
    static constexpr int CONNECTION_TIMEOUT_MS = 5000;
};

} // namespace MeckChat::Network
