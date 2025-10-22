#include "mainwindow.h"
#include <QHBoxLayout>
#include <QLabel>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <QInputDialog>

#include <cstring>

using namespace std;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    logView = new QPlainTextEdit(central);
    logView->setReadOnly(true);
    input = new QLineEdit(central);

    // server / connection
    serverIp = new QLineEdit(central);
    serverIp->setPlaceholderText("127.0.0.1");
    connectBtn = new QPushButton("Connect", central);

    // auth
    usernameEdit = new QLineEdit(central);
    usernameEdit->setPlaceholderText("username (1-31 chars)");
    passwordEdit = new QLineEdit(central);
    passwordEdit->setPlaceholderText("password");
    passwordEdit->setEchoMode(QLineEdit::Password);
    registerBtn = new QPushButton("Register", central);
    loginBtn = new QPushButton("Login", central);

    // friend / util buttons
    addFriendBtn = new QPushButton("Add Friend", central);
    acceptFriendBtn = new QPushButton("Accept Friend", central);
    listFriendsBtn = new QPushButton("List Friends", central);
    unfriendBtn = new QPushButton("Unfriend", central);
    usersBtn = new QPushButton("Users", central);

    sendBtn = new QPushButton("Send", central);

    QVBoxLayout *layout = new QVBoxLayout(central);
    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(new QLabel("Server:"));
    top->addWidget(serverIp);
    top->addWidget(connectBtn);
    top->addWidget(new QLabel("User:"));
    top->addWidget(usernameEdit);
    top->addWidget(new QLabel("Pass:"));
    top->addWidget(passwordEdit);
    top->addWidget(registerBtn);
    top->addWidget(loginBtn);
    layout->addLayout(top);
    layout->addWidget(logView);

    QHBoxLayout *bottom = new QHBoxLayout();
    bottom->addWidget(input);
    bottom->addWidget(sendBtn);
    layout->addLayout(bottom);

    QHBoxLayout *actions = new QHBoxLayout();
    actions->addWidget(addFriendBtn);
    actions->addWidget(acceptFriendBtn);
    actions->addWidget(listFriendsBtn);
    actions->addWidget(unfriendBtn);
    actions->addWidget(usersBtn);
    layout->addLayout(actions);

    sockfd = -1;

    connect(connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(registerBtn, &QPushButton::clicked, this, &MainWindow::onRegisterClicked);
    connect(loginBtn, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(addFriendBtn, &QPushButton::clicked, this, &MainWindow::onAddFriendClicked);
    connect(acceptFriendBtn, &QPushButton::clicked, this, &MainWindow::onAcceptFriendClicked);
    connect(listFriendsBtn, &QPushButton::clicked, this, &MainWindow::onListFriendsClicked);
    connect(unfriendBtn, &QPushButton::clicked, this, &MainWindow::onUnfriendClicked);
    connect(usersBtn, &QPushButton::clicked, this, &MainWindow::onUsersClicked);
    // readyRead will be triggered by QSocketNotifier when data arrives

    setLoggedInState(false);
}

MainWindow::~MainWindow() {
    if (readNotifier) {
        readNotifier->setEnabled(false);
        delete readNotifier;
        readNotifier = nullptr;
    }
    if (sockfd >= 0) {
        ::shutdown(sockfd, SHUT_RDWR);
        ::close(sockfd);
        sockfd = -1;
    }
}

void MainWindow::onConnectClicked() {
    QString ip = serverIp->text();
    if (ip.isEmpty()) ip = "127.0.0.1";
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        logView->appendPlainText("Failed to create socket");
        return;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, ip.toStdString().c_str(), &server_addr.sin_addr) <= 0) {
        logView->appendPlainText("Invalid server IP address");
        ::close(sockfd);
        return;
    }

    if (::connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        logView->appendPlainText("Failed to connect to server (connect())");
        ::close(sockfd);
        return;
    }
    // create notifier to receive read events on sockfd
    if (readNotifier) {
        readNotifier->setEnabled(false);
        delete readNotifier;
        readNotifier = nullptr;
    }
    readNotifier = new QSocketNotifier(sockfd, QSocketNotifier::Read, this);
    connect(readNotifier, &QSocketNotifier::activated, this, &MainWindow::onReadyRead);

    logView->appendPlainText("Connected to server (manual connect)");
}

void MainWindow::sendMessage(const Message &msg) {
    if (sockfd < 0) {
        logView->appendPlainText("Not connected");
        return;
    }
    ssize_t sent = ::send(sockfd, &msg, sizeof(Message), 0);
    (void)sent;
}

void MainWindow::setLoggedInState(bool loggedIn_) {
    loggedIn = loggedIn_;
    usernameEdit->setEnabled(!loggedIn);
    passwordEdit->setEnabled(!loggedIn);
    registerBtn->setEnabled(!loggedIn);
    loginBtn->setEnabled(!loggedIn);
    sendBtn->setEnabled(loggedIn);
    addFriendBtn->setEnabled(loggedIn);
    acceptFriendBtn->setEnabled(loggedIn);
    listFriendsBtn->setEnabled(loggedIn);
    unfriendBtn->setEnabled(loggedIn);
    usersBtn->setEnabled(loggedIn);
}

void MainWindow::onRegisterClicked() {
    if (sockfd < 0) {
        logView->appendPlainText("Not connected");
        return;
    }
    QString user = usernameEdit->text();
    QString pass = passwordEdit->text();
    if (user.isEmpty() || user.size() > 31) {
        logView->appendPlainText("Username must be 1-31 chars");
        return;
    }
    Message msg{};
    msg.type = MSG_REGISTER;
    strncpy(msg.username, user.toStdString().c_str(), sizeof(msg.username)-1);
    strncpy(msg.content, pass.toStdString().c_str(), sizeof(msg.content)-1);
    sendMessage(msg);
}

void MainWindow::onLoginClicked() {
    if (sockfd < 0) {
        logView->appendPlainText("Not connected");
        return;
    }
    QString user = usernameEdit->text();
    QString pass = passwordEdit->text();
    if (user.isEmpty() || user.size() > 31) {
        logView->appendPlainText("Username must be 1-31 chars");
        return;
    }
    Message msg{};
    msg.type = MSG_LOGIN;
    strncpy(msg.username, user.toStdString().c_str(), sizeof(msg.username)-1);
    strncpy(msg.content, pass.toStdString().c_str(), sizeof(msg.content)-1);
    sendMessage(msg);
}

void MainWindow::onSendClicked() {
    if (sockfd < 0) return;
    QString text = input->text();
    if (text.isEmpty()) return;

    Message msg;
    msg.type = MSG_TEXT;
    strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1);
    strncpy(msg.content, text.toStdString().c_str(), sizeof(msg.content)-1);

    ::send(sockfd, &msg, sizeof(Message), 0);
    input->clear();
}

void MainWindow::onAddFriendClicked() {
    bool ok;
    QString friendname = QInputDialog::getText(this, "Add Friend", "Friend username:", QLineEdit::Normal, "", &ok);
    if (!ok || friendname.isEmpty()) return;
    Message msg{}; msg.type = MSG_FRIEND_REQUEST; strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1); strncpy(msg.content, friendname.toStdString().c_str(), sizeof(msg.content)-1);
    sendMessage(msg);
    logView->appendPlainText("Friend request sent to " + friendname);
}

void MainWindow::onAcceptFriendClicked() {
    bool ok;
    QString from = QInputDialog::getText(this, "Accept Friend", "Friend username to accept:", QLineEdit::Normal, "", &ok);
    if (!ok || from.isEmpty()) return;
    Message msg{}; msg.type = MSG_FRIEND_ACCEPT; strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1); strncpy(msg.content, from.toStdString().c_str(), sizeof(msg.content)-1);
    sendMessage(msg);
    logView->appendPlainText("Accepted friend request from " + from);
}

void MainWindow::onListFriendsClicked() {
    Message msg{}; msg.type = MSG_FRIEND_LIST_REQUEST; strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1); sendMessage(msg);
}

void MainWindow::onUnfriendClicked() {
    bool ok;
    QString who = QInputDialog::getText(this, "Unfriend", "Friend username to remove:", QLineEdit::Normal, "", &ok);
    if (!ok || who.isEmpty()) return;
    Message msg{}; msg.type = MSG_FRIEND_REMOVE; strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1); strncpy(msg.content, who.toStdString().c_str(), sizeof(msg.content)-1);
    sendMessage(msg);
}

void MainWindow::onUsersClicked() {
    Message msg{}; msg.type = MSG_USER_LIST; strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1); sendMessage(msg);
}

void MainWindow::onReadyRead() {
    while (true) {
        Message msg;
        ssize_t r = ::recv(sockfd, &msg, sizeof(Message), MSG_DONTWAIT);
        if (r <= 0) break;
        if (r == (ssize_t)sizeof(Message)) {
            // Handle auth response
            if (msg.type == MSG_AUTH_RESPONSE) {
                if (msg.content[0] == AUTH_SUCCESS) {
                    currentUser = QString(msg.username);
                    setLoggedInState(true);
                    logView->appendPlainText("Authentication succeeded as " + currentUser);
                } else {
                    logView->appendPlainText("Authentication failed: " + QString(msg.content + 1));
                }
                continue;
            }

            if (msg.type == MSG_TEXT || msg.type == MSG_USER_LIST || msg.type == MSG_FRIEND_LIST_RESPONSE) {
                QString line = QString("[%1] %2").arg(msg.username).arg(msg.content);
                logView->appendPlainText(line);
            }
        }
    }
}
