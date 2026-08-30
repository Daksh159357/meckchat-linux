#include "mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent) {
    setupUi();
}

void MainWindow::setupUi() {
    setWindowTitle("MeckChat Linux");
    resize(900, 600);

    auto *centralWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(centralWidget);
    layout->setAlignment(Qt::AlignCenter);

    auto *titleLabel = new QLabel("MeckChat Native Linux Client", centralWidget);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignCenter);

    auto *subtitleLabel = new QLabel("Global Encrypted Peer-to-Peer Communication (C++ / Qt 6)", centralWidget);
    subtitleLabel->setAlignment(Qt::AlignCenter);

    layout->addWidget(titleLabel);
    layout->addSpacing(10);
    layout->addWidget(subtitleLabel);

    setCentralWidget(centralWidget);
}
