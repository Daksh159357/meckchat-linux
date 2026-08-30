#include "ui/mainwindow.h"
#include "meckchat/core/logger.h"

namespace MeckChat::UI {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_mqttClient(new Network::MqttSignalingClient(this)),
      m_p2pSocket(new Network::P2PSocket(this)),
      m_wireguardService(new Network::WireGuardService(this)) {
    setupUi();
    Core::Logger::info("MainWindow", "MeckChat Linux UI initialized");
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

    auto *peersHeader = new QLabel("Discovered Peers (P2P)", leftPanel);
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

    auto *inputLayout = new QHBoxLayout();
    m_messageInput = new QLineEdit(rightPanel);
    m_messageInput->setPlaceholderText("Type encrypted message...");
    m_sendButton = new QPushButton("Send", rightPanel);

    inputLayout->addWidget(m_messageInput);
    inputLayout->addWidget(m_sendButton);
    rightLayout->addLayout(inputLayout);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    splitter->setSizes({300, 700});

    mainLayout->addWidget(splitter);
    setCentralWidget(centralWidget);

    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::onSendMessage);
    connect(m_messageInput, &QLineEdit::returnPressed, this, &MainWindow::onSendMessage);
    connect(m_peerList, &QListWidget::currentRowChanged, this, &MainWindow::onPeerSelected);
}

void MainWindow::onSendMessage() {
    QString text = m_messageInput->text().trimmed();
    if (text.isEmpty()) return;

    m_chatDisplay->append(QString("<b>Me:</b> %1").arg(text));
    m_messageInput->clear();
}

void MainWindow::onPeerSelected(int row) {
    if (row >= 0) {
        m_statusLabel->setText(QString("Selected Peer: %1").arg(m_peerList->item(row)->text()));
    }
}

} // namespace MeckChat::UI
