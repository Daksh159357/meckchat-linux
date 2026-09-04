#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include "meckchat/core/config.h"
#include "meckchat/network/mqtt_signaling.h"
#include "meckchat/network/p2p_socket.h"
#include "meckchat/network/wireguard_service.h"
#include "meckchat/protocol/chat_controller.h"
#include "meckchat/protocol/file_transfer_controller.h"

namespace MeckChat::UI {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onSendMessage();
    void onSendFile();
    void onPeerSelected(int row);
    void onTextChanged(const QString &text);
    void onMessageReceived(const MeckChat::Protocol::ChatMessage &msg);
    void onMessageStatusChanged(const QString &msgId, MeckChat::Protocol::MessageState state);
    void onPeerTypingChanged(const QString &peerId, bool isTyping);

    // File transfer slots
    void onFileOfferReceived(const MeckChat::Protocol::FileOffer &offer);
    void onTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes);
    void onTransferCompleted(const QString &transferId, const QString &filePath);
    void onTransferFailed(const QString &transferId, const QString &errorString);
    void onTransferCancelled(const QString &transferId, const QString &reason);

private:
    void setupUi();

    QListWidget *m_peerList{nullptr};
    QTextEdit *m_chatDisplay{nullptr};
    QLineEdit *m_messageInput{nullptr};
    QPushButton *m_sendButton{nullptr};
    QPushButton *m_sendFileButton{nullptr};
    QProgressBar *m_transferProgressBar{nullptr};
    QLabel *m_transferStatusLabel{nullptr};
    QLabel *m_statusLabel{nullptr};
    QLabel *m_typingLabel{nullptr};

    Network::MqttSignalingClient *m_mqttClient{nullptr};
    Network::P2PSocket *m_p2pSocket{nullptr};
    Network::WireGuardService *m_wireguardService{nullptr};
    Protocol::ChatController *m_chatController{nullptr};
    Protocol::FileTransferController *m_fileTransferController{nullptr};

    QString m_selectedPeerId;
    QString m_selectedPeerIp;
};

} // namespace MeckChat::UI
