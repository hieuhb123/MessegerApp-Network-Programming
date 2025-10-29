#include "mainwindow.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <QInputDialog>
#include <QDateTime>

#include <cstring>
#include <cerrno>
#include <iostream>
#include <sys/select.h>
#include <QScrollBar>

using namespace std;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    QWidget *central = new QWidget(this);
    setCentralWidget(central);

    logView = new QPlainTextEdit(central);
    logView->setReadOnly(true);
    input = new QLineEdit(central);

    // server / connection
    serverIp = new QLineEdit(central);
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
    historyBtn = new QPushButton("History", central);

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
    // Conversations list + log view side-by-side
    convoList = new QListWidget(central);
    convoList->addItem("All");
    convoList->setCurrentRow(0);
    QHBoxLayout *mid = new QHBoxLayout();
    mid->addWidget(convoList, 1);
    mid->addWidget(logView, 4);
    layout->addLayout(mid);

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
    actions->addWidget(historyBtn);
    layout->addLayout(actions);

    sockfd = -1;
    reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(reconnectIntervalMs);
    connect(reconnectTimer, &QTimer::timeout, this, &MainWindow::attemptConnect);

    connect(connectBtn, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(registerBtn, &QPushButton::clicked, this, &MainWindow::onRegisterClicked);
    connect(loginBtn, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(addFriendBtn, &QPushButton::clicked, this, &MainWindow::onAddFriendClicked);
    connect(acceptFriendBtn, &QPushButton::clicked, this, &MainWindow::onAcceptFriendClicked);
    connect(listFriendsBtn, &QPushButton::clicked, this, &MainWindow::onListFriendsClicked);
    connect(unfriendBtn, &QPushButton::clicked, this, &MainWindow::onUnfriendClicked);
    connect(usersBtn, &QPushButton::clicked, this, &MainWindow::onUsersClicked);
    connect(historyBtn, &QPushButton::clicked, this, &MainWindow::onHistoryClicked);
    connect(convoList, &QListWidget::currentTextChanged, this, &MainWindow::onConversationChanged);
    // We receive responses manually per action (no onReadyRead)

    setLoggedInState(false);
    // Auto-connect to localhost on startup and try again on failure
    if (serverIp->text().isEmpty()) serverIp->setText(SERVER_IP);
    // Load credentials from optional config and prefill fields
    QString cfgUser, cfgPass;
    if (loadCredentials(cfgUser, cfgPass)) {
        if (!cfgUser.isEmpty()) usernameEdit->setText(cfgUser);
        if (!cfgPass.isEmpty()) passwordEdit->setText(cfgPass);
    }
    QTimer::singleShot(0, this, &MainWindow::attemptConnect);
}

MainWindow::~MainWindow() {
    if (sockfd >= 0) {
        ::shutdown(sockfd, SHUT_RDWR);
        ::close(sockfd);
        sockfd = -1;
    }
    if (reconnectTimer && reconnectTimer->isActive()) reconnectTimer->stop();
    cleanupSocket();
}

void MainWindow::onConnectClicked() {
    attemptConnect();
}

void MainWindow::attemptConnect() {
    if (sockfd >= 0) return; // already connected
    QString ip = serverIp->text();
    if (ip.isEmpty()) ip = SERVER_IP;
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        logView->appendPlainText("Failed to create socket");
        scheduleReconnect();
        return;
    }

    struct sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);

    if (::inet_pton(AF_INET, ip.toStdString().c_str(), &server_addr.sin_addr) <= 0) {
        logView->appendPlainText("Invalid server IP address");
        ::close(fd);
        scheduleReconnect();
        return;
    }

    if (::connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        logView->appendPlainText("Failed to connect to server (connect()) — will retry");
        ::close(fd);
        scheduleReconnect();
        return;
    }

    // success
    sockfd = fd;

    if (reconnectTimer && reconnectTimer->isActive()) reconnectTimer->stop();
    logView->appendPlainText("Connected to server (auto-connect)");
    // Try auto-login if credentials are present
    tryAutoLogin();
}

void MainWindow::scheduleReconnect() {
    if (!reconnectTimer) return;
    if (!reconnectTimer->isActive()) {
        reconnectTimer->start();
        logView->appendPlainText("Reconnecting in " + QString::number(reconnectIntervalMs/1000.0) + "s...");
    }
}

void MainWindow::cleanupSocket() {
    if (sockfd >= 0) {
        ::shutdown(sockfd, SHUT_RDWR);
        ::close(sockfd);
        sockfd = -1;
    }
    if (pollTimer && pollTimer->isActive()) pollTimer->stop();
}
void MainWindow::sendMessage(const Message &msg) {
    if (sockfd < 0) {
        logView->appendPlainText("Not connected");
        return;
    }
    ssize_t sent = ::send(sockfd, &msg, sizeof(Message), 0);
    if (sent < 0) {
        logView->appendPlainText(QString("SEND error: %1").arg(strerror(errno)));
    } else {
        logView->appendPlainText(QString("-> SENT type=%1 bytes=%2").arg(msg.type).arg((long long)sent));
    }
}

bool MainWindow::recvMessageBlocking(Message &out, int timeoutMs) {
    if (sockfd < 0) return false;
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(sockfd, &rfds);
    struct timeval tv;
    tv.tv_sec = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int rv = select(sockfd + 1, &rfds, nullptr, nullptr, &tv);
    if (rv <= 0) {
        return false; // timeout or error
    }
    ssize_t got = ::recv(sockfd, &out, sizeof(Message), MSG_WAITALL);
    if (got == 0) {
        logView->appendPlainText("Disconnected by server — will retry");
        cleanupSocket();
        scheduleReconnect();
        return false;
    }
    if (got < 0) {
        logView->appendPlainText("Socket recv error — will retry");
        cleanupSocket();
        scheduleReconnect();
        return false;
    }
    if (got == (ssize_t)sizeof(Message)) {
        // quick debug: log received type
        logView->appendPlainText(QString("<- RECV type=%1 from=%2").arg(out.type).arg(QString::fromUtf8(out.username)));
        return true;
    }
    return false;
}

void MainWindow::setLoggedInState(bool loggedIn_) {
    loggedIn = loggedIn_;
    usernameEdit->setEnabled(!loggedIn);
    passwordEdit->setEnabled(!loggedIn);
    registerBtn->setEnabled(!loggedIn);
    loginBtn->setEnabled(!loggedIn);
    sendBtn->setEnabled(loggedIn);
    if (historyBtn) historyBtn->setEnabled(loggedIn);
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
    Message resp{};
    if (recvMessageBlocking(resp, 3000) && resp.type == MSG_AUTH_RESPONSE) {
        if (resp.content[0] == AUTH_SUCCESS) {
            currentUser = user;
            setLoggedInState(true);
            logView->appendPlainText("Register success");
            // start polling incoming chat messages
            if (!pollTimer) {
                pollTimer = new QTimer(this);
                connect(pollTimer, &QTimer::timeout, this, &MainWindow::pollMessages);
                pollTimer->setInterval(200);
            }
            pollTimer->start();
            // populate friends list after account creation
            onListFriendsClicked();
        } else {
            logView->appendPlainText("Register failed");
        }
    } else {
        logView->appendPlainText("No response for register");
    }
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
    Message msg{}, resp;
    msg.type = MSG_LOGIN;
    strncpy(msg.username, user.toStdString().c_str(), sizeof(msg.username)-1);
    strncpy(msg.content, pass.toStdString().c_str(), sizeof(msg.content)-1);
    sendMessage(msg);
    if (recvMessageBlocking(resp, 3000) && resp.type == MSG_AUTH_RESPONSE && resp.content[0] == AUTH_SUCCESS) {
        currentUser = user;
        setLoggedInState(true);
        logView->appendPlainText("Login success");
        // start polling incoming chat messages
        if (!pollTimer) {
            pollTimer = new QTimer(this);
            connect(pollTimer, &QTimer::timeout, this, &MainWindow::pollMessages);
            pollTimer->setInterval(200);
        }
        pollTimer->start();
        // populate friends list
        onListFriendsClicked();
    } else {
        logView->appendPlainText("Login failed or timed out");
    }
}

void MainWindow::onSendClicked() {
    if (sockfd < 0) return;
    QString text = input->text();
    if (text.isEmpty()) return;
    QString target = convoList->currentItem() ? convoList->currentItem()->text() : QString("All");
    if (target == "All") {
        // broadcast
        Message msg;
        msg.type = MSG_TEXT;
        strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1);
        strncpy(msg.content, text.toStdString().c_str(), sizeof(msg.content)-1);
        QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        logView->appendPlainText("[" + now + "] [Me] " + text);
        conversations["All"].append("[" + now + "] [Me] " + text);
        if (convoList->currentItem() && convoList->currentItem()->text() == "All") {
            logView->setPlainText(conversations["All"].join("\n"));
            logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
        }
        ::send(sockfd, &msg, sizeof(Message), 0);
    } else {
        // direct message to selected user
        Message msg{};
        msg.type = MSG_DIRECT_MESSAGE;
        // put peer into msg.username; server will use authenticated currentUser as sender
        strncpy(msg.username, target.toStdString().c_str(), sizeof(msg.username)-1);
        strncpy(msg.content, text.toStdString().c_str(), sizeof(msg.content)-1);
        sendMessage(msg);
        // optimistically append to own view
    QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString line = QString("[%1] [Me -> %2] %3").arg(now, target, text);
    conversations[target].append(line);
        if (convoList->currentItem() && convoList->currentItem()->text() == target) {
            logView->setPlainText(conversations[target].join("\n"));
            logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
        }
    }
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
    Message resp{};
    if (recvMessageBlocking(resp, 3000) && resp.type == MSG_FRIEND_LIST_RESPONSE) {
        // Parse content like: "Friends: a, b, c"
        QString payload = QString::fromUtf8(resp.content);
        QString list = payload;
        if (payload.startsWith("Friends:", Qt::CaseInsensitive)) {
            list = payload.mid(QString("Friends:").length());
        }
        QStringList names = list.split(',', Qt::SkipEmptyParts);
        for (QString &n : names) n = n.trimmed();
        names.removeAll("");

        // Rebuild convoList: keep "All" at top, replace others with friend names
        QString currentSel = convoList->currentItem() ? convoList->currentItem()->text() : QString("All");
        for (int i = convoList->count()-1; i >= 0; --i) {
            if (convoList->item(i)->text() != "All") {
                delete convoList->takeItem(i);
            }
        }
        names.sort(Qt::CaseInsensitive);
        for (const QString &n : names) {
            if (n == "All") continue;
            convoList->addItem(n);
        }
        bool restored = false;
        for (int i = 0; i < convoList->count(); ++i) {
            if (convoList->item(i)->text() == currentSel) { convoList->setCurrentRow(i); restored = true; break; }
        }
        if (!restored) convoList->setCurrentRow(0);
        logView->appendPlainText(QString("Friends updated (%1)").arg(names.size()));
    } else {
        logView->appendPlainText("No friend list response");
    }
}

void MainWindow::onUnfriendClicked() {
    bool ok;
    QString who = QInputDialog::getText(this, "Unfriend", "Friend username to remove:", QLineEdit::Normal, "", &ok);
    if (!ok || who.isEmpty()) return;
    Message msg{}; msg.type = MSG_FRIEND_REMOVE; strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1); strncpy(msg.content, who.toStdString().c_str(), sizeof(msg.content)-1);
    sendMessage(msg);
    Message resp{};
    if (recvMessageBlocking(resp, 3000)) {
        if (resp.type == MSG_AUTH_RESPONSE && resp.content[0] == AUTH_SUCCESS) {
            logView->appendPlainText("Unfriend success");
        } else {
            logView->appendPlainText("Unfriend failed");
        }
    }
}

void MainWindow::onUsersClicked() {
    Message msg{}; 
    msg.type = MSG_ALL_USERS_STATUS_REQUEST; 
    strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1); 
    sendMessage(msg);
    Message resp{};
    if (recvMessageBlocking(resp, 3000) && resp.type == MSG_ALL_USERS_STATUS_RESPONSE) {
        logView->appendPlainText(QString::fromUtf8(resp.content));
    }
}

void MainWindow::onHistoryClicked() {
    if (sockfd < 0 || !convoList->currentItem()) return;
    QString peer = convoList->currentItem()->text();
    if (peer == "All" || peer.isEmpty()) {
        logView->appendPlainText("Select a user to load history");
        return;
    }
    Message msg{}; msg.type = MSG_HISTORY_REQUEST;
    strncpy(msg.username, peer.toStdString().c_str(), sizeof(msg.username)-1);
    sendMessage(msg);
    Message resp{};
    if (recvMessageBlocking(resp, 3000) && resp.type == MSG_HISTORY_RESPONSE) {
        // Replace conversation view with server history
        QString text = QString::fromUtf8(resp.content);
        QStringList lines = text.split("\n", Qt::KeepEmptyParts);
        conversations[peer] = lines;
        if (convoList->currentItem() && convoList->currentItem()->text() == peer) {
            logView->setPlainText(conversations[peer].join("\n"));
            logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
        }
    } else {
        logView->appendPlainText("No history response");
    }
}

// onReadyRead removed — manual recv per action will be used

void MainWindow::onConversationChanged() {
    if (!convoList->currentItem()) return;
    const QString who = convoList->currentItem()->text();
    const auto lines = conversations.value(who);
    logView->setPlainText(lines.join("\n"));
    if (who != "All") {
        onHistoryClicked();
    }
}

bool MainWindow::loadCredentials(QString &user, QString &pass) {
    user.clear(); pass.clear();
    QFile f("config.json");
    if (!f.exists()) return false;
    if (!f.open(QIODevice::ReadOnly)) return false;
    auto doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return false;
    auto obj = doc.object();
    if (obj.contains("server") && obj["server"].isString()) {
        QString srv = obj["server"].toString();
        if (!srv.isEmpty()) serverIp->setText(srv);
    }
    if (obj.contains("username") && obj["username"].isString()) user = obj["username"].toString();
    if (obj.contains("password") && obj["password"].isString()) pass = obj["password"].toString();
    return true;
}

void MainWindow::tryAutoLogin() {
    if (loggedIn || sockfd < 0) return;
    const QString user = usernameEdit->text();
    const QString pass = passwordEdit->text();
    if (user.isEmpty() || pass.isEmpty()) return;
    Message msg{}, resp;
    msg.type = MSG_LOGIN;
    strncpy(msg.username, user.toStdString().c_str(), sizeof(msg.username)-1);
    strncpy(msg.content, pass.toStdString().c_str(), sizeof(msg.content)-1);
    sendMessage(msg);
    if (recvMessageBlocking(resp, 3000) && resp.type == MSG_AUTH_RESPONSE && resp.content[0] == AUTH_SUCCESS) {
        currentUser = user;
        setLoggedInState(true);
        logView->appendPlainText("Auto-login success");
        // start polling incoming chat messages
        if (!pollTimer) {
            pollTimer = new QTimer(this);
            connect(pollTimer, &QTimer::timeout, this, &MainWindow::pollMessages);
            pollTimer->setInterval(200);
        }
        pollTimer->start();
    } else {
        logView->appendPlainText("Auto-login failed or timed out");
    }
}

void MainWindow::pollMessages() {
    if (sockfd < 0) return;
    while (true) {
        Message peek{};
        ssize_t got = ::recv(sockfd, &peek, sizeof(Message), MSG_PEEK | MSG_DONTWAIT);
        if (got == 0) {
            logView->appendPlainText("Disconnected by server");
            cleanupSocket();
            scheduleReconnect();
            break;
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // no more data now
            }
            // other socket error
            logView->appendPlainText("Socket error while polling; will reconnect");
            cleanupSocket();
            scheduleReconnect();
            break;
        }
        if (got != (ssize_t)sizeof(Message)) {
            // incomplete; shouldn't happen with MSG_PEEK; break to avoid busy loop
            break;
        }
        if (peek.type != MSG_TEXT) {
            // leave non-chat messages for the blocking handlers
            break;
        }
        // it's a chat message; actually read and consume it
        Message msg{};
        ssize_t got2 = ::recv(sockfd, &msg, sizeof(Message), 0);
        if (got2 != (ssize_t)sizeof(Message)) break;
        QString from = QString::fromUtf8(msg.username);
        QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        QString line = QString("[%1] [%2] %3").arg(now, from, QString::fromUtf8(msg.content));
        conversations["All"].append(line);
        conversations[from].append(line);
        // ensure list has this conversation
        bool found = false;
        for (int i = 0; i < convoList->count(); ++i) {
            if (convoList->item(i)->text() == from) { found = true; break; }
        }
        if (!found) convoList->addItem(from);

        if (convoList->currentItem()) {
            QString cur = convoList->currentItem()->text();
            if (cur == from || cur == "All") {
                if (cur == "All") logView->setPlainText(conversations["All"].join("\n"));
                else logView->setPlainText(conversations[from].join("\n"));
                
            } else {
                // show a brief notification in the log for unseen incoming DMs
                logView->appendPlainText(line);
            }
            logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
        } else {
            // no selection; append to log
            logView->appendPlainText(line);
        }
    }
}
