#include "ui/mainwindow.h"
#include "meckchat/core/logger.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QStandardPaths>

namespace MeckChat::UI {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_mqttClient(new Network::MqttSignalingClient(this)),
      m_p2pSocket(new Network::P2PSocket(this)),
      m_wireguardService(new Network::WireGuardService(this)),
      m_chatController(new Protocol::ChatController(m_p2pSocket, this)),
      m_fileTransferController(new Protocol::FileTransferController(m_p2pSocket, this)) {
    setupUi();
    Core::Logger::info("MainWindow", "MeckChat Linux UI initialized with real P2P Chat and FileTransferController");
}

void MainWindow::setupUi() {
    setWindowTitle("MeckChat Linux Desktop");
    resize(1000, 650);

    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QHBoxLayout(centralWidget);

    auto *splitter = new QSplitter(Qt::Horizontal, centralWidget);

    // Left Panel: Peers & Status
    auto *leftPanel = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);

    auto *peersHeader = new QLabel("Discovered Peers (P2P / WireGuard)", leftPanel);
    peersHeader->setStyleSheet("font-weight: bold; font-size: 14px;");
    leftLayout->addWidget(peersHeader);

    m_peerList = new QListWidget(leftPanel);
    leftLayout->addWidget(m_peerList);

    m_statusLabel = new QLabel("Status: Online (Linux Native)", leftPanel);
    m_statusLabel->setStyleSheet("color: #0D6EFD; font-weight: 500;");
    leftLayout->addWidget(m_statusLabel);

    // Right Panel: Chat & Messages
    auto *rightPanel = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);

    auto *chatHeader = new QLabel("Encrypted WireGuard P2P Session", rightPanel);
    chatHeader->setStyleSheet("font-weight: bold; font-size: 14px;");
    rightLayout->addWidget(chatHeader);

    m_chatDisplay = new QTextEdit(rightPanel);
    m_chatDisplay->setReadOnly(true);
    rightLayout->addWidget(m_chatDisplay);

    // File Transfer Status / Progress
    m_transferProgressBar = new QProgressBar(rightPanel);
    m_transferProgressBar->setRange(0, 100);
    m_transferProgressBar->setValue(0);
    m_transferProgressBar->setVisible(false);
    rightLayout->addWidget(m_transferProgressBar);

    m_transferStatusLabel = new QLabel("", rightPanel);
    m_transferStatusLabel->setStyleSheet("color: #0D6EFD; font-size: 12px;");
    m_transferStatusLabel->setVisible(false);
    rightLayout->addWidget(m_transferStatusLabel);

    m_typingLabel = new QLabel("", rightPanel);
    m_typingLabel->setStyleSheet("color: #6c757d; font-style: italic; font-size: 12px;");
    rightLayout->addWidget(m_typingLabel);

    auto *inputLayout = new QHBoxLayout();
    m_messageInput = new QLineEdit(rightPanel);
    m_messageInput->setPlaceholderText("Type encrypted message...");
    m_sendButton = new QPushButton("Send", rightPanel);
    m_sendFileButton = new QPushButton("📎 Send File", rightPanel);

    inputLayout->addWidget(m_messageInput);
    inputLayout->addWidget(m_sendButton);
    inputLayout->addWidget(m_sendFileButton);
    rightLayout->addLayout(inputLayout);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setSizes({300, 700});

    mainLayout->addWidget(splitter);
    setCentralWidget(centralWidget);

    // UI Signal Connections
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(m_sendFileButton, &QPushButton::clicked, this, &MainWindow::onSendFile);
    connect(m_messageInput, &QLineEdit::returnPressed, this, &MainWindow::onSendMessage);
    connect(m_messageInput, &QLineEdit::textChanged, this, &MainWindow::onTextChanged);
    connect(m_peerList, &QListWidget::currentRowChanged, this, &MainWindow::onPeerSelected);

    // ChatController Signal Connections
    connect(m_chatController, &Protocol::ChatController::messageReceived, this, &MainWindow::onMessageReceived);
    connect(m_chatController, &Protocol::ChatController::messageStatusChanged, this, &MainWindow::onMessageStatusChanged);
    connect(m_chatController, &Protocol::ChatController::peerTypingChanged, this, &MainWindow::onPeerTypingChanged);

    // FileTransferController Signal Connections
    connect(m_fileTransferController, &Protocol::FileTransferController::fileOfferReceived, this, &MainWindow::onFileOfferReceived);
    connect(m_fileTransferController, &Protocol::FileTransferController::transferProgress, this, &MainWindow::onTransferProgress);
    connect(m_fileTransferController, &Protocol::FileTransferController::transferCompleted, this, &MainWindow::onTransferCompleted);
    connect(m_fileTransferController, &Protocol::FileTransferController::transferFailed, this, &MainWindow::onTransferFailed);
    connect(m_fileTransferController, &Protocol::FileTransferController::transferCancelled, this, &MainWindow::onTransferCancelled);

    // MQTT Peer Discovery Connection
    connect(m_mqttClient, &Network::MqttSignalingClient::deviceDiscovered, this, [this](const Protocol::Device &device) {
        QString itemText = QString("%1 (%2, %3)").arg(device.displayName).arg(device.deviceId).arg(Protocol::platformToString(device.platform));
        m_peerList->addItem(itemText);
    });
}

void MainWindow::onSendMessage() {
    QString text = m_messageInput->text().trimmed();
    if (text.isEmpty()) return;

    QString recipient = m_selectedPeerId.isEmpty() ? "mc_broadcast_peer" : m_selectedPeerId;
    QString msgId = m_chatController->sendMessage(recipient, text, m_selectedPeerIp);

    if (!msgId.isEmpty()) {
        m_chatDisplay->append(QString("<div id='%1'><b>Me:</b> %2 <span style='color: #888; font-size: 10px;'>[Sending...]</span></div>")
            .arg(msgId).arg(text.toHtmlEscaped()));
    }
    m_messageInput->clear();
    m_chatController->sendTypingNotification(recipient, false, m_selectedPeerIp);
}

void MainWindow::onSendFile() {
    QString filePath = QFileDialog::getOpenFileName(this, "Select File to Send");
    if (filePath.isEmpty()) return;

    QString transferId = m_fileTransferController->sendFile(filePath, m_selectedPeerIp);
    if (!transferId.isEmpty()) {
        m_transferProgressBar->setValue(0);
        m_transferProgressBar->setVisible(true);
        m_transferStatusLabel->setText(QString("Offering file: %1").arg(QFileInfo(filePath).fileName()));
        m_transferStatusLabel->setVisible(true);
        m_chatDisplay->append(QString("<span style='color: #0D6EFD;'><b>[File Offer Sent]:</b> %1 (%2 bytes)</span>")
            .arg(QFileInfo(filePath).fileName()).arg(QFileInfo(filePath).size()));
    }
}

void MainWindow::onFileOfferReceived(const Protocol::FileOffer &offer) {
    QString msg = QString("Incoming file offer:\nName: %1\nSize: %2 bytes\n\nDo you want to accept this transfer?")
        .arg(offer.fileName).arg(offer.fileSize);
    auto reply = QMessageBox::question(this, "Incoming File Transfer", msg, QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QString defaultDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        QString destDir = QFileDialog::getExistingDirectory(this, "Select Destination Folder", defaultDir);
        if (destDir.isEmpty()) {
            destDir = defaultDir;
        }
        m_fileTransferController->acceptTransfer(offer.transferId, destDir);
        m_transferProgressBar->setValue(0);
        m_transferProgressBar->setVisible(true);
        m_transferStatusLabel->setText(QString("Receiving %1...").arg(offer.fileName));
        m_transferStatusLabel->setVisible(true);
        m_chatDisplay->append(QString("<span style='color: #198754;'><b>[File Transfer Accepted]:</b> %1</span>").arg(offer.fileName));
    } else {
        m_fileTransferController->rejectTransfer(offer.transferId, "user_declined");
        m_chatDisplay->append(QString("<span style='color: #dc3545;'><b>[File Transfer Rejected]:</b> %1</span>").arg(offer.fileName));
    }
}

void MainWindow::onTransferProgress(const QString &transferId, qint64 transferredBytes, qint64 totalBytes) {
    Q_UNUSED(transferId);
    if (totalBytes > 0) {
        int percent = static_cast<int>((transferredBytes * 100) / totalBytes);
        m_transferProgressBar->setValue(percent);
        m_transferStatusLabel->setText(QString("Transferring: %1 / %2 bytes (%3%)")
            .arg(transferredBytes).arg(totalBytes).arg(percent));
    }
}

void MainWindow::onTransferCompleted(const QString &transferId, const QString &filePath) {
    Q_UNUSED(transferId);
    m_transferProgressBar->setValue(100);
    m_transferStatusLabel->setText("Transfer Complete! Verified SHA-256.");
    m_chatDisplay->append(QString("<span style='color: #198754;'><b>[File Transfer Complete]:</b> Saved to %1</span>").arg(filePath));
}

void MainWindow::onTransferFailed(const QString &transferId, const QString &errorString) {
    Q_UNUSED(transferId);
    m_transferProgressBar->setVisible(false);
    m_transferStatusLabel->setText(QString("Transfer Failed: %1").arg(errorString));
    m_chatDisplay->append(QString("<span style='color: #dc3545;'><b>[File Transfer Failed]:</b> %1</span>").arg(errorString));
}

void MainWindow::onTransferCancelled(const QString &transferId, const QString &reason) {
    Q_UNUSED(transferId);
    m_transferProgressBar->setVisible(false);
    m_transferStatusLabel->setText(QString("Transfer Cancelled (%1)").arg(reason));
    m_chatDisplay->append(QString("<span style='color: #6c757d;'><b>[File Transfer Cancelled]:</b> %1</span>").arg(reason));
}

void MainWindow::onTextChanged(const QString &text) {
    if (!text.isEmpty()) {
        QString recipient = m_selectedPeerId.isEmpty() ? "mc_broadcast_peer" : m_selectedPeerId;
        m_chatController->sendTypingNotification(recipient, true, m_selectedPeerIp);
    }
}

void MainWindow::onMessageReceived(const Protocol::ChatMessage &msg) {
    m_chatDisplay->append(QString("<b>%1:</b> %2")
        .arg(msg.senderDeviceId.toHtmlEscaped()).arg(msg.content.toHtmlEscaped()));
}

void MainWindow::onMessageStatusChanged(const QString &msgId, Protocol::MessageState state) {
    QString statusStr;
    switch (state) {
        case Protocol::MessageState::Sent: statusStr = "[Sent]"; break;
        case Protocol::MessageState::Delivered: statusStr = "[Delivered]"; break;
        case Protocol::MessageState::Read: statusStr = "[Read]"; break;
        case Protocol::MessageState::Failed: statusStr = "[Failed ❌]"; break;
        default: statusStr = "[Pending]"; break;
    }
    Core::Logger::info("MainWindow", QString("Message status updated for %1: %2").arg(msgId).arg(statusStr));
}

void MainWindow::onPeerTypingChanged(const QString &peerId, bool isTyping) {
    if (isTyping) {
        m_typingLabel->setText(QString("%1 is typing...").arg(peerId));
    } else {
        m_typingLabel->clear();
    }
}

void MainWindow::onPeerSelected(int row) {
    if (row >= 0 && row < m_peerList->count()) {
        QString text = m_peerList->item(row)->text();
        m_statusLabel->setText(QString("Selected Peer: %1").arg(text));

        // Extract device id if formatted
        int start = text.indexOf("(mc_");
        if (start != -1) {
            int end = text.indexOf(",", start);
            if (end != -1) {
                m_selectedPeerId = text.mid(start + 1, end - start - 1);
            }
        }
    }
}

} // namespace MeckChat::UI
