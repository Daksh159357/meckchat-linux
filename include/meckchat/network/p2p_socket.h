#pragma once

#include <QObject>
#include <QTcpSocket>
#include "meckchat/protocol/framing.h"

namespace MeckChat::Network {

class P2PSocket : public QObject {
    Q_OBJECT

public:
    explicit P2PSocket(QObject *parent = nullptr);
    ~P2PSocket() override = default;

    void connectToPeer(const QString &virtualIp, int port = 7788);
    void disconnectFromPeer();
    bool isConnected() const;

    bool sendFrame(const Protocol::P2PFrame &frame);

signals:
    void connected();
    void disconnected();
    void frameReceived(const Protocol::P2PFrame &frame);
    void errorOccurred(const QString &error);

private slots:
    void onReadyRead();

private:
    QTcpSocket *m_socket{nullptr};
    QByteArray m_readBuffer;
};

} // namespace MeckChat::Network
