#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include "meckchat/core/config.h"
#include "meckchat/network/mqtt_signaling.h"
#include "meckchat/network/p2p_socket.h"
#include "meckchat/network/wireguard_service.h"

namespace MeckChat::UI {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onSendMessage();
    void onPeerSelected(int row);

private:
    void setupUi();

    QListWidget *m_peerList{nullptr};
    QTextEdit *m_chatDisplay{nullptr};
    QLineEdit *m_messageInput{nullptr};
    QPushButton *m_sendButton{nullptr};
    QLabel *m_statusLabel{nullptr};

    Network::MqttSignalingClient *m_mqttClient{nullptr};
    Network::P2PSocket *m_p2pSocket{nullptr};
    Network::WireGuardService *m_wireguardService{nullptr};
};

} // namespace MeckChat::UI
