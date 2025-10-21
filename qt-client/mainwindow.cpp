#include "mainwindow.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QHostAddress>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    logView = new QPlainTextEdit(central);
    logView->setReadOnly(true);
    input = new QLineEdit(central);
    serverIp = new QLineEdit(central);
    serverIp->setPlaceholderText("127.0.0.1");
    connectBtn = new QPushButton("Connect", central);
    sendBtn = new QPushButton("Send", central);

    QVBoxLayout *layout = new QVBoxLayout(central);
    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(new QLabel("Server:"));
    top->addWidget(serverIp);
    top->addWidget(connectBtn);
    layout->addLayout(top);
    layout->addWidget(logView);

    QHBoxLayout *bottom = new QHBoxLayout();
    bottom->addWidget(input);
    bottom->addWidget(sendBtn);
    layout->addLayout(bottom);

    socket = new QTcpSocket(this);

    connect(connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(socket, &QTcpSocket::readyRead, this, &MainWindow::onReadyRead);
}

MainWindow::~MainWindow() {
    socket->disconnectFromHost();
}

void MainWindow::onConnectClicked() {
    QString ip = serverIp->text();
    if (ip.isEmpty()) ip = "127.0.0.1";
    socket->connectToHost(QHostAddress(ip), PORT);
    if (socket->waitForConnected(3000)) {
        logView->appendPlainText("Connected to server");
    } else {
        logView->appendPlainText("Failed to connect");
    }
}

void MainWindow::onSendClicked() {
    if (socket->state() != QAbstractSocket::ConnectedState) return;
    QString text = input->text();
    if (text.isEmpty()) return;

    Message msg;
    msg.type = MSG_TEXT;
    strncpy(msg.username, "qtuser", sizeof(msg.username)-1);
    strncpy(msg.content, text.toStdString().c_str(), sizeof(msg.content)-1);

    socket->write(reinterpret_cast<const char*>(&msg), sizeof(Message));
    input->clear();
}

void MainWindow::onReadyRead() {
    while (socket->bytesAvailable() >= (int)sizeof(Message)) {
        Message msg;
        qint64 r = socket->read(reinterpret_cast<char*>(&msg), sizeof(Message));
        if (r == sizeof(Message)) {
            QString line = QString("[%1] %2").arg(msg.username).arg(msg.content);
            logView->appendPlainText(line);
        }
    }
}
