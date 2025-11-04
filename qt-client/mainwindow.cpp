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

    // server / connection UI removed (manual connect not used)
    // auth
    usernameEdit = new QLineEdit(central);
    usernameEdit->setPlaceholderText("username (1-31 chars)");
    passwordEdit = new QLineEdit(central);
    passwordEdit->setPlaceholderText("password");
    passwordEdit->setEchoMode(QLineEdit::Password);
    registerBtn = new QPushButton("Register", central);
    loginBtn = new QPushButton("Login", central);

    // friend / util buttons (List Friends, Users)
    listFriendsBtn = new QPushButton("List Friends", central);
    usersBtn = new QPushButton("Users", central);
    // group buttons
    createGroupBtn = new QPushButton("Create Group", central);
    listGroupsBtn = new QPushButton("Groups", central);
    sendBtn = new QPushButton("Send", central);

    QVBoxLayout *layout = new QVBoxLayout(central);
    QHBoxLayout *top = new QHBoxLayout();
    // server label and connect button removed
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
    connectBtn = new QPushButton("Connect", central);
    disconnectBtn = new QPushButton("Disconnect", central);
    disconnectBtn->setEnabled(false);
    connect(connectBtn, &QPushButton::clicked, this, &MainWindow::attemptConnect);
    connect(disconnectBtn, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    actions->addWidget(listFriendsBtn);
    actions->addWidget(usersBtn);
    actions->addWidget(createGroupBtn);
    actions->addWidget(listGroupsBtn);
    actions->addWidget(connectBtn);
    actions->addWidget(disconnectBtn);
    layout->addLayout(actions);

    sockfd = -1;

    // connect button removed
    reconnectTimer = new QTimer(this);
    reconnectTimer->setInterval(reconnectIntervalMs);
    connect(reconnectTimer, &QTimer::timeout, this, &MainWindow::attemptConnect);
    connect(registerBtn, &QPushButton::clicked, this, &MainWindow::onRegisterClicked);
    connect(loginBtn, &QPushButton::clicked, this, &MainWindow::onLoginClicked);
    connect(sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(listFriendsBtn, &QPushButton::clicked, this, &MainWindow::onListFriendsClicked);
    connect(usersBtn, &QPushButton::clicked, this, &MainWindow::onUsersClicked);
    connect(createGroupBtn, &QPushButton::clicked, this, &MainWindow::onCreateGroupClicked);
    connect(listGroupsBtn, &QPushButton::clicked, this, &MainWindow::onGroupsClicked);
    connect(convoList, &QListWidget::currentTextChanged, this, &MainWindow::onConversationChanged);
    // We receive responses manually per action (no onReadyRead)

    setLoggedInState(false);
    // Load credentials from optional config and prefill fields
    QString cfgUser, cfgPass;
    if (loadCredentials(cfgUser, cfgPass)) {
        if (!cfgUser.isEmpty()) usernameEdit->setText(cfgUser);
        if (!cfgPass.isEmpty()) passwordEdit->setText(cfgPass);
    }
    // Attempt to connect right after UI is shown
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

    void MainWindow::attemptConnect() {
        if (sockfd >= 0) return; // already connected
        const char *ip = SERVER_IP;
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
        if (::inet_pton(AF_INET, ip, &server_addr.sin_addr) <= 0) {
            logView->appendPlainText("Invalid server IP address");
            ::close(fd);
            scheduleReconnect();
            return;
        }

        if (::connect(fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            ::close(fd);
            logView->appendPlainText("Failed to connect to server — will retry");
            scheduleReconnect();
            return;
        }

        // success
        sockfd = fd;
        if (reconnectTimer && reconnectTimer->isActive()) reconnectTimer->stop();
        logView->appendPlainText("Connected to server (auto-connect)");
            if (connectBtn) connectBtn->setEnabled(false);
            if (disconnectBtn) disconnectBtn->setEnabled(true);
        // If not logged in, try auto-login; otherwise flush any queued messages
        if (!loggedIn) tryAutoLogin();
        else flushPendingMessages();
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
            // flush any messages queued while offline
            flushPendingMessages();
        } else {
            logView->appendPlainText("Auto-login failed or timed out");
        }
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
    if (connectBtn) connectBtn->setEnabled(true);
    if (disconnectBtn) disconnectBtn->setEnabled(false);
}

void MainWindow::onDisconnectClicked() {
    if (reconnectTimer && reconnectTimer->isActive()) reconnectTimer->stop();
    cleanupSocket();
        // mark logged out
    loggedIn = false;
    sendBtn->setEnabled(true);
    logView->appendPlainText("Disconnected (manual)");

    // keep loggedIn state — allow sending which will be queued and flushed on reconnect
    logView->appendPlainText("Disconnected (manual). Chat messages will be queued and sent when reconnected.");
}
void MainWindow::sendMessage(const Message &msg) {
    // If this is a chat message and we're offline, queue it for later send
    if (sockfd < 0) {
        if (msg.type == MSG_DIRECT_MESSAGE || msg.type == MSG_GROUP_MESSAGE || msg.type == MSG_TEXT) {
            pendingMessages.append(msg);
            // optimistic UI append for direct/group messages
            if (msg.type == MSG_DIRECT_MESSAGE) {
                QString target = QString::fromUtf8(msg.username);
                QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                QString line = QString("[%1] [Me -> %2] %3 (queued)").arg(now, target, QString::fromUtf8(msg.content));
                conversations[target].append(line);
                if (convoList->currentItem() && convoList->currentItem()->text() == target) {
                    logView->setPlainText(conversations[target].join("\n"));
                    logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
                } else {
                    logView->appendPlainText(line);
                }
            } else if (msg.type == MSG_GROUP_MESSAGE) {
                QString gname = QString::fromUtf8(msg.username);
                QString key = QString("Group:%1").arg(gname);
                QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                QString line = QString("[%1] [Me -> %2] %3 (queued)").arg(now, gname, QString::fromUtf8(msg.content));
                conversations[key].append(line);
                if (convoList->currentItem() && convoList->currentItem()->text() == key) {
                    logView->setPlainText(conversations[key].join("\n"));
                    logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
                } else {
                    logView->appendPlainText(line);
                }
            } else if (msg.type == MSG_TEXT) {
                QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
                QString line = QString("[%1] [Me] %2 (queued)").arg(now, QString::fromUtf8(msg.content));
                conversations["All"].append(line);
                if (convoList->currentItem() && convoList->currentItem()->text() == "All") {
                    logView->setPlainText(conversations["All"].join("\n"));
                    logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
                } else {
                    logView->appendPlainText(line);
                }
            }
            logView->appendPlainText("Message queued for later delivery (offline)");
            return;
        }
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

void MainWindow::flushPendingMessages() {
    if (sockfd < 0) return;
    if (pendingMessages.isEmpty()) return;
    int sentCount = 0;
    for (const Message &m : qAsConst(pendingMessages)) {
        ssize_t s = ::send(sockfd, &m, sizeof(Message), 0);
        if (s == (ssize_t)sizeof(Message)) {
            ++sentCount;
        } else {
            logView->appendPlainText("Failed to flush a queued message (will retry later)");
            // stop here; keep remaining messages queued
            break;
        }
    }
    if (sentCount > 0) {
        // remove the first sentCount messages
        for (int i = 0; i < sentCount; ++i) pendingMessages.removeFirst();
        logView->appendPlainText(QString("Flushed %1 queued message(s)").arg(sentCount));
    }
}

bool MainWindow::recvMessageBlocking(Message &out, int timeoutMs) {
    if (sockfd < 0) return false;
    ssize_t got = ::recv(sockfd, &out, sizeof(Message), MSG_WAITALL);
    if (got == 0) {
        logView->appendPlainText("Disconnected by server");
        cleanupSocket();
        return false;
    }
    if (got < 0) {
        logView->appendPlainText("Socket recv error");
        cleanupSocket();
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
    if (listFriendsBtn) listFriendsBtn->setEnabled(loggedIn);
    if (usersBtn) usersBtn->setEnabled(loggedIn);
    if (createGroupBtn) createGroupBtn->setEnabled(loggedIn);
    if (listGroupsBtn) listGroupsBtn->setEnabled(loggedIn);
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
    Message msg{}, resp{};
    msg.type = MSG_LOGIN;
    strncpy(msg.username, user.toStdString().c_str(), sizeof(msg.username)-1);
    strncpy(msg.content, pass.toStdString().c_str(), sizeof(msg.content)-1);
    sendMessage(msg);
    if (recvMessageBlocking(resp, 3000) && resp.type == MSG_AUTH_RESPONSE && resp.content[0] == AUTH_SUCCESS) {
        currentUser = user;
        setLoggedInState(true);
        logView->appendPlainText("Login success");
        if (!pollTimer) {
            pollTimer = new QTimer(this);
            connect(pollTimer, &QTimer::timeout, this, &MainWindow::pollMessages);
            pollTimer->setInterval(200);
        }
        pollTimer->start();
        // flush queued messages after manual login
        flushPendingMessages();
        // populate convoList with friends automatically after login
        {
            Message msg{}; 
            msg.type = MSG_FRIEND_LIST_REQUEST; 
            strncpy(msg.username, currentUser.toStdString().c_str(), sizeof(msg.username)-1); 
            sendMessage(msg);
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
                // Also request groups for this user and add them to convo list
                Message gmsg{}; gmsg.type = MSG_GROUP_LIST_REQUEST; strncpy(gmsg.username, currentUser.toStdString().c_str(), sizeof(gmsg.username)-1); sendMessage(gmsg);
                Message gresp{};
                if (recvMessageBlocking(gresp, 3000) && gresp.type == MSG_GROUP_LIST_RESPONSE) {
                    QString gpayload = QString::fromUtf8(gresp.content);
                    QStringList groups = gpayload.split(',', Qt::SkipEmptyParts);
                    for (QString &g : groups) {
                        g = g.trimmed();
                        if (g.isEmpty()) continue;
                        QString key = QString("Group:%1").arg(g);
                        convoList->addItem(key);
                    }
                    logView->appendPlainText(QString("Groups updated (%1)").arg(groups.size()));
                } else {
                    // no groups or failed response is non-fatal
                }
            } else {
                logView->appendPlainText("No friend list response");
            }
        }
    } else {
        logView->appendPlainText("Login failed or timed out");
    }
}

void MainWindow::onSendClicked() {
    QString text = input->text();
    if (text.isEmpty()) return;
    QString target = convoList->currentItem() ? convoList->currentItem()->text() : QString("All");
    if (target.startsWith("Group:", Qt::CaseInsensitive)) {
        QString gname = target.mid(QString("Group:").length());
        Message msg{}; msg.type = MSG_GROUP_MESSAGE;
        strncpy(msg.username, gname.toStdString().c_str(), sizeof(msg.username)-1);
        strncpy(msg.content, text.toStdString().c_str(), sizeof(msg.content)-1);
        sendMessage(msg);
        // optimistic append only when online
        if (sockfd >= 0) {
            QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            QString key = target;
            QString line = QString("[%1] [Me -> %2] %3").arg(now, gname, text);
            conversations[key].append(line);
            if (convoList->currentItem() && convoList->currentItem()->text() == key) {
                logView->setPlainText(conversations[key].join("\n"));
                logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
            }
        }
    } else {
        // direct message to selected user
        Message msg{};
        msg.type = MSG_DIRECT_MESSAGE;
        // put peer into msg.username; server will use authenticated currentUser as sender
        strncpy(msg.username, target.toStdString().c_str(), sizeof(msg.username)-1);
        strncpy(msg.content, text.toStdString().c_str(), sizeof(msg.content)-1);
        sendMessage(msg);
        // optimistic append only when online (sendMessage will append queued UI when offline)
        if (sockfd >= 0) {
            QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
            QString line = QString("[%1] [Me -> %2] %3").arg(now, target, text);
            conversations[target].append(line);
            if (convoList->currentItem() && convoList->currentItem()->text() == target) {
                logView->setPlainText(conversations[target].join("\n"));
                logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
            }
        }
    }
    input->clear();
}


void MainWindow::onListFriendsClicked() {
    // Request server for all users with friendship status relative to current user
    if (sockfd < 0) { logView->appendPlainText("Not connected"); return; }
    Message req{}; 
    req.type = MSG_ALL_USERS_STATUS_REQUEST; 
    strncpy(req.username, currentUser.toStdString().c_str(), sizeof(req.username)-1); 
    sendMessage(req);
    Message resp{};
    if (!(recvMessageBlocking(resp, 3000) && resp.type == MSG_ALL_USERS_STATUS_RESPONSE)) {
        logView->appendPlainText("No users/status response");
        return;
    }

    // resp.content contains a multiline human-readable listing like:
    // Users and status:\n- alice: friend\n- bob: incoming\n...
    QString payload = QString::fromUtf8(resp.content);

    // Build and show a modal dialog with the list and action buttons
    QDialog dlg(this);
    dlg.setWindowTitle("Users and friendship status");
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    QLabel *hint = new QLabel("Select a user to see actions (Accept/Unfriend/Open chat)", &dlg);
    v->addWidget(hint);

    QListWidget *list = new QListWidget(&dlg);
    QStringList lines = payload.split('\n', Qt::SkipEmptyParts);
    for (const QString &ln : lines) {
        // skip header lines that don't have '- '
        QString line = ln.trimmed();
        if (!line.startsWith("- ") && !line.contains(":")) continue;
        // normalize: remove leading '- '
        QString item = line;
        if (item.startsWith("- ")) item = item.mid(2);
        // item like "alice: friend"
        QStringList parts = item.split(':', Qt::KeepEmptyParts);
        if (parts.size() < 2) continue;
        QString name = parts[0].trimmed();
        QString status = parts[1].trimmed();
        // Only include accepted (friend) and pending (incoming/outgoing) statuses
        QString statusLower = status.toLower();
        if (!(statusLower == "friend" || statusLower == "incoming" || statusLower == "outgoing")) continue;
        QListWidgetItem *it = new QListWidgetItem(QString("%1 (%2)").arg(name, status));
        it->setData(Qt::UserRole, name);
        it->setData(Qt::UserRole + 1, status);
        list->addItem(it);
    }
    v->addWidget(list);

    QHBoxLayout *h = new QHBoxLayout();
    QPushButton *acceptBtn = new QPushButton("Accept", &dlg);
    QPushButton *refuseBtn = new QPushButton("Refuse", &dlg);
    QPushButton *unfriendBtn = new QPushButton("Unfriend", &dlg);
    QPushButton *openChatBtn = new QPushButton("Open Chat", &dlg);
    QPushButton *closeBtn = new QPushButton("Close", &dlg);
    h->addWidget(acceptBtn);
    h->addWidget(refuseBtn);
    h->addWidget(unfriendBtn);
    h->addWidget(openChatBtn);
    h->addWidget(closeBtn);
    v->addLayout(h);

    acceptBtn->setEnabled(false);
    refuseBtn->setEnabled(false);
    unfriendBtn->setEnabled(false);
    openChatBtn->setEnabled(false);

    connect(list, &QListWidget::currentItemChanged, this, [=](QListWidgetItem *cur, QListWidgetItem *) {
        if (!cur) {
            acceptBtn->setEnabled(false);
            refuseBtn->setEnabled(false);
            unfriendBtn->setEnabled(false);
            openChatBtn->setEnabled(false);
            return;
        }
        QString name = cur->data(Qt::UserRole).toString();
        QString status = cur->data(Qt::UserRole + 1).toString();
        openChatBtn->setEnabled(!name.isEmpty());
        // enable Accept only if incoming
        acceptBtn->setEnabled(status.compare("incoming", Qt::CaseInsensitive) == 0);
        // enable Decline only if incoming
        refuseBtn->setEnabled(status.compare("incoming", Qt::CaseInsensitive) == 0);
        // enable Unfriend only if already friends
        unfriendBtn->setEnabled(status.compare("friend", Qt::CaseInsensitive) == 0);
    });

    connect(acceptBtn, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return;
        QString name = cur->data(Qt::UserRole).toString();
        // send accept message: server expects MSG_FRIEND_ACCEPT with username=currentUser and content=from
        Message m{}; m.type = MSG_FRIEND_ACCEPT; strncpy(m.username, currentUser.toStdString().c_str(), sizeof(m.username)-1); strncpy(m.content, name.toStdString().c_str(), sizeof(m.content)-1);
        sendMessage(m);
        Message r{}; if (recvMessageBlocking(r, 3000) && r.type == MSG_AUTH_RESPONSE && r.content[0] == AUTH_SUCCESS) {
            logView->appendPlainText("Accepted friend request from " + name);
            cur->setText(QString("%1 (friend)").arg(name));
            cur->setData(Qt::UserRole + 1, QString("friend"));
            acceptBtn->setEnabled(false);
            unfriendBtn->setEnabled(true);
        } else {
            logView->appendPlainText("Failed to accept friend request from " + name);
        }
    });

    connect(refuseBtn, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return;
        QString name = cur->data(Qt::UserRole).toString();
        Message m{}; m.type = MSG_FRIEND_REFUSE; strncpy(m.username, currentUser.toStdString().c_str(), sizeof(m.username)-1); strncpy(m.content, name.toStdString().c_str(), sizeof(m.content)-1);
        sendMessage(m);
        Message r{}; if (recvMessageBlocking(r, 3000) && r.type == MSG_AUTH_RESPONSE && r.content[0] == AUTH_SUCCESS) {
            logView->appendPlainText("Refuse friend request from " + name);
            cur->setText(QString("%1 (none)").arg(name));
            cur->setData(Qt::UserRole + 1, QString("none"));
            acceptBtn->setEnabled(false);
            refuseBtn->setEnabled(false);
            unfriendBtn->setEnabled(false);
        } else {
            logView->appendPlainText("Failed to refuse friend request from " + name);
        }
    });

    connect(unfriendBtn, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return;
        QString name = cur->data(Qt::UserRole).toString();
        Message m{}; m.type = MSG_FRIEND_REMOVE; strncpy(m.username, currentUser.toStdString().c_str(), sizeof(m.username)-1); strncpy(m.content, name.toStdString().c_str(), sizeof(m.content)-1);
        sendMessage(m);
        Message r{}; if (recvMessageBlocking(r, 3000) && r.type == MSG_AUTH_RESPONSE && r.content[0] == AUTH_SUCCESS) {
            logView->appendPlainText("Unfriended " + name);
            cur->setText(QString("%1 (none)").arg(name));
            cur->setData(Qt::UserRole + 1, QString("none"));
            unfriendBtn->setEnabled(false);
            acceptBtn->setEnabled(false);
        } else {
            logView->appendPlainText("Failed to unfriend " + name);
        }
    });

    connect(openChatBtn, &QPushButton::clicked, this, [=,&dlg]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return;
        QString name = cur->data(Qt::UserRole).toString();
        // ensure convoList has an entry and select it
        bool found = false;
        for (int i = 0; i < convoList->count(); ++i) { if (convoList->item(i)->text() == name) { convoList->setCurrentRow(i); found = true; break; } }
        if (!found) {
            convoList->addItem(name);
            convoList->setCurrentRow(convoList->count()-1);
        }
        dlg.accept();
    });

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    // show dialog
    dlg.exec();
}

void MainWindow::onCreateGroupClicked() {
    if (sockfd < 0) { logView->appendPlainText("Not connected"); return; }
    bool ok;
    QString group = QInputDialog::getText(this, "Create Group", "Group name:", QLineEdit::Normal, "", &ok);
    if (!ok || group.isEmpty()) return;
    Message m{}; m.type = MSG_GROUP_CREATE; strncpy(m.username, currentUser.toStdString().c_str(), sizeof(m.username)-1); strncpy(m.content, group.toStdString().c_str(), sizeof(m.content)-1);
    sendMessage(m);
    Message r{}; if (recvMessageBlocking(r, 3000) && r.type == MSG_GROUP_CREATE_RESPONSE && r.content[0] == AUTH_SUCCESS) {
        logView->appendPlainText("Group created: " + group);
        // optionally open chat
    } else {
        logView->appendPlainText("Failed to create group");
    }
}

void MainWindow::onGroupsClicked() {
    if (sockfd < 0) { logView->appendPlainText("Not connected"); return; }
    Message req{}; req.type = MSG_GROUP_LIST_REQUEST; strncpy(req.username, currentUser.toStdString().c_str(), sizeof(req.username)-1); sendMessage(req);
    Message resp{};
    if (!(recvMessageBlocking(resp, 3000) && resp.type == MSG_GROUP_LIST_RESPONSE)) {
        logView->appendPlainText("No group list response");
        return;
    }
    QString payload = QString::fromUtf8(resp.content);
    // Build dialog
    QDialog dlg(this);
    dlg.setWindowTitle("Groups");
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    QLabel *hint = new QLabel("Select a group to manage or open chat", &dlg);
    v->addWidget(hint);
    QListWidget *list = new QListWidget(&dlg);
    QStringList groups = payload.split(',', Qt::SkipEmptyParts);
    for (QString &g : groups) { g = g.trimmed(); if (!g.isEmpty()) { QListWidgetItem *it = new QListWidgetItem(g); it->setData(Qt::UserRole, g); list->addItem(it); } }
    v->addWidget(list);
    QHBoxLayout *h = new QHBoxLayout();
    QPushButton *addMemberBtn = new QPushButton("Add Member", &dlg);
    QPushButton *removeMemberBtn = new QPushButton("Remove Member", &dlg);
    QPushButton *leaveBtn = new QPushButton("Leave Group", &dlg);
    QPushButton *openChatBtn = new QPushButton("Open Chat", &dlg);
    QPushButton *historyBtnLocal = new QPushButton("History", &dlg);
    QPushButton *closeBtn = new QPushButton("Close", &dlg);
    h->addWidget(addMemberBtn); h->addWidget(removeMemberBtn); h->addWidget(leaveBtn); h->addWidget(openChatBtn); h->addWidget(historyBtnLocal); h->addWidget(closeBtn);
    v->addLayout(h);
    addMemberBtn->setEnabled(false); removeMemberBtn->setEnabled(false); leaveBtn->setEnabled(false); openChatBtn->setEnabled(false); historyBtnLocal->setEnabled(false);

    connect(list, &QListWidget::currentItemChanged, this, [=](QListWidgetItem *cur, QListWidgetItem*){
        bool ena = (cur != nullptr);
        addMemberBtn->setEnabled(ena);
        removeMemberBtn->setEnabled(ena);
        leaveBtn->setEnabled(ena);
        openChatBtn->setEnabled(ena);
        historyBtnLocal->setEnabled(ena);
    });

    connect(addMemberBtn, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return; QString g = cur->data(Qt::UserRole).toString();
        bool ok; QString who = QInputDialog::getText(this, "Add Member", "Username to add:", QLineEdit::Normal, "", &ok); if (!ok || who.isEmpty()) return;
        Message m{}; m.type = MSG_GROUP_ADD; strncpy(m.username, g.toStdString().c_str(), sizeof(m.username)-1); strncpy(m.content, who.toStdString().c_str(), sizeof(m.content)-1); sendMessage(m);
        Message r{}; if (recvMessageBlocking(r, 3000) && r.type == MSG_AUTH_RESPONSE && r.content[0] == AUTH_SUCCESS) {
            logView->appendPlainText("Added " + who + " to " + g);
        } else logView->appendPlainText("Failed to add member");
    });

    connect(removeMemberBtn, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return; QString g = cur->data(Qt::UserRole).toString();
        bool ok; QString who = QInputDialog::getText(this, "Remove Member", "Username to remove:", QLineEdit::Normal, "", &ok); if (!ok || who.isEmpty()) return;
        Message m{}; m.type = MSG_GROUP_REMOVE; strncpy(m.username, g.toStdString().c_str(), sizeof(m.username)-1); strncpy(m.content, who.toStdString().c_str(), sizeof(m.content)-1); sendMessage(m);
        Message r{}; if (recvMessageBlocking(r, 3000) && r.type == MSG_AUTH_RESPONSE && r.content[0] == AUTH_SUCCESS) {
            logView->appendPlainText("Removed " + who + " from " + g);
        } else logView->appendPlainText("Failed to remove member");
    });

    connect(leaveBtn, &QPushButton::clicked, this, [=,&dlg]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return; QString g = cur->data(Qt::UserRole).toString();
        Message m{}; m.type = MSG_GROUP_LEAVE; strncpy(m.content, g.toStdString().c_str(), sizeof(m.content)-1); sendMessage(m);
        Message r{}; if (recvMessageBlocking(r, 3000) && r.type == MSG_AUTH_RESPONSE && r.content[0] == AUTH_SUCCESS) {
            logView->appendPlainText("Left group " + g);
            delete list->takeItem(list->row(cur));
        } else logView->appendPlainText("Failed to leave group");
    });

    connect(openChatBtn, &QPushButton::clicked, this, [=,&dlg]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return; QString g = cur->data(Qt::UserRole).toString();
        QString key = QString("Group:%1").arg(g);
        bool found = false; for (int i=0;i<convoList->count();++i) if (convoList->item(i)->text() == key) { convoList->setCurrentRow(i); found = true; break; }
        if (!found) { convoList->addItem(key); convoList->setCurrentRow(convoList->count()-1); }
        dlg.accept();
    });

    connect(historyBtnLocal, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return; QString g = cur->data(Qt::UserRole).toString();
        // request group history
        Message m{}; m.type = MSG_GROUP_HISTORY_REQUEST; strncpy(m.username, g.toStdString().c_str(), sizeof(m.username)-1); sendMessage(m);
        Message r{}; if (recvMessageBlocking(r, 3000) && r.type == MSG_GROUP_HISTORY_RESPONSE) {
            QString text = QString::fromUtf8(r.content);
            QStringList lines = text.split('\n', Qt::KeepEmptyParts);
            QString key = QString("Group:%1").arg(g);
            conversations[key] = lines;
            if (convoList->currentItem() && convoList->currentItem()->text() == key) {
                logView->setPlainText(conversations[key].join("\n"));
                logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
            }
        } else logView->appendPlainText("No group history response");
    });

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.exec();
}


void MainWindow::onUsersClicked() {
    // Request server for all users with friendship status relative to current user
    if (sockfd < 0) { logView->appendPlainText("Not connected"); return; }
    Message req{}; req.type = MSG_ALL_USERS_STATUS_REQUEST; strncpy(req.username, currentUser.toStdString().c_str(), sizeof(req.username)-1); sendMessage(req);
    Message resp{};
    if (!(recvMessageBlocking(resp, 3000) && resp.type == MSG_ALL_USERS_STATUS_RESPONSE)) {
        logView->appendPlainText("No users/status response");
        return;
    }

    QString payload = QString::fromUtf8(resp.content);

    // Build and show a modal dialog with the list and action buttons (Add Friend + Open Chat)
    QDialog dlg(this);
    dlg.setWindowTitle("All Users");
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    QLabel *hint = new QLabel("Select a user to Add Friend or Open Chat", &dlg);
    v->addWidget(hint);

    QListWidget *list = new QListWidget(&dlg);
    QStringList lines = payload.split('\n', Qt::SkipEmptyParts);
    for (const QString &ln : lines) {
        QString line = ln.trimmed();
        if (!line.startsWith("- ") && !line.contains(":")) continue;
        QString item = line;
        if (item.startsWith("- ")) item = item.mid(2);
        QStringList parts = item.split(':', Qt::KeepEmptyParts);
        if (parts.size() < 2) continue;
        QString name = parts[0].trimmed();
        QString status = parts[1].trimmed();
        QListWidgetItem *it = new QListWidgetItem(QString("%1 (%2)").arg(name, status));
        it->setData(Qt::UserRole, name);
        it->setData(Qt::UserRole + 1, status);
        list->addItem(it);
    }
    v->addWidget(list);

    QHBoxLayout *h = new QHBoxLayout();
    QPushButton *addBtn = new QPushButton("Add Friend", &dlg);
    QPushButton *openChatBtn = new QPushButton("Open Chat", &dlg);
    QPushButton *closeBtn = new QPushButton("Close", &dlg);
    h->addWidget(addBtn);
    h->addWidget(openChatBtn);
    h->addWidget(closeBtn);
    v->addLayout(h);

    addBtn->setEnabled(false);
    openChatBtn->setEnabled(false);

    connect(list, &QListWidget::currentItemChanged, this, [=](QListWidgetItem *cur, QListWidgetItem *) {
        if (!cur) { addBtn->setEnabled(false); openChatBtn->setEnabled(false); return; }
        QString name = cur->data(Qt::UserRole).toString();
        QString status = cur->data(Qt::UserRole + 1).toString();
        openChatBtn->setEnabled(!name.isEmpty());
        // Add Friend only if status is not already friend or outgoing/incoming
        bool canAdd = (status.compare("friend", Qt::CaseInsensitive) != 0) &&
                      (status.compare("incoming", Qt::CaseInsensitive) != 0) &&
                      (status.compare("outgoing", Qt::CaseInsensitive) != 0);
        addBtn->setEnabled(canAdd);
    });

    connect(addBtn, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return;
        QString name = cur->data(Qt::UserRole).toString();
        // send friend request
        Message m{}; m.type = MSG_FRIEND_REQUEST; strncpy(m.username, currentUser.toStdString().c_str(), sizeof(m.username)-1); strncpy(m.content, name.toStdString().c_str(), sizeof(m.content)-1);
        sendMessage(m);
        // update UI optimistically to show outgoing
        cur->setText(QString("%1 (outgoing)").arg(name));
        cur->setData(Qt::UserRole + 1, QString("outgoing"));
        addBtn->setEnabled(false);
        logView->appendPlainText("Friend request sent to " + name);
    });

    connect(openChatBtn, &QPushButton::clicked, this, [=,&dlg]() {
        QListWidgetItem *cur = list->currentItem(); if (!cur) return;
        QString name = cur->data(Qt::UserRole).toString();
        bool found = false;
        for (int i = 0; i < convoList->count(); ++i) { if (convoList->item(i)->text() == name) { convoList->setCurrentRow(i); found = true; break; } }
        if (!found) { convoList->addItem(name); convoList->setCurrentRow(convoList->count()-1); }
        dlg.accept();
    });

    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

    dlg.exec();
}

void MainWindow::onHistoryClicked() {
    if (sockfd < 0 || !convoList->currentItem()) return;
    QString peer = convoList->currentItem()->text();
    if (peer == "All" || peer.isEmpty()) {
        logView->appendPlainText("Select a user to load history");
        return;
    }
    if (peer.startsWith("Group:", Qt::CaseInsensitive)) {
        QString g = peer.mid(QString("Group:").length());
        Message msg{}; msg.type = MSG_GROUP_HISTORY_REQUEST; strncpy(msg.username, g.toStdString().c_str(), sizeof(msg.username)-1); sendMessage(msg);
        Message resp{};
        if (recvMessageBlocking(resp, 3000) && resp.type == MSG_GROUP_HISTORY_RESPONSE) {
            QString text = QString::fromUtf8(resp.content);
            QStringList lines = text.split("\n", Qt::KeepEmptyParts);
            conversations[peer] = lines;
            if (convoList->currentItem() && convoList->currentItem()->text() == peer) {
                logView->setPlainText(conversations[peer].join("\n"));
                logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
            }
        } else {
            logView->appendPlainText("No group history response");
        }
    } else {
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
}

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
    // server field removed; we only read username/password here
    if (obj.contains("username") && obj["username"].isString()) user = obj["username"].toString();
    if (obj.contains("password") && obj["password"].isString()) pass = obj["password"].toString();
    return true;
}

void MainWindow::pollMessages() {
    if (sockfd < 0) return;
    while (true) {
        Message peek{};
        ssize_t got = ::recv(sockfd, &peek, sizeof(Message), MSG_PEEK | MSG_DONTWAIT);
        if (got == 0) {
            logView->appendPlainText("Disconnected by server");
            cleanupSocket();
            break;
        }
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // no more data now
            }
            // other socket error
            logView->appendPlainText("Socket error while polling; will reconnect");
            cleanupSocket();
            break;
        }
        if (got != (ssize_t)sizeof(Message)) {
            // incomplete; shouldn't happen with MSG_PEEK; break to avoid busy loop
            break;
        }
        if (peek.type != MSG_TEXT && peek.type != MSG_GROUP_TEXT) {
            // leave non-chat messages for the blocking handlers
            break;
        }
        Message msg{};
        ssize_t got2 = ::recv(sockfd, &msg, sizeof(Message), 0);
        if (got2 != (ssize_t)sizeof(Message)) break;
        QString now = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        if (msg.type == MSG_TEXT) {
            QString from = QString::fromUtf8(msg.username);
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
                    logView->appendPlainText(line);
                }
                logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
            } else {
                logView->appendPlainText(line);
            }
        } else if (msg.type == MSG_GROUP_TEXT) {
            // msg.username == groupname; msg.content == "sender: body"
            QString group = QString::fromUtf8(msg.username);
            QString payload = QString::fromUtf8(msg.content);
            QString line = QString("[%1] [%2] %3").arg(now, group, payload);
            QString key = QString("Group:%1").arg(group);
            conversations["All"].append(line);
            conversations[key].append(line);
            bool foundg = false;
            for (int i = 0; i < convoList->count(); ++i) {
                if (convoList->item(i)->text() == key) { foundg = true; break; }
            }
            if (!foundg) convoList->addItem(key);

            if (convoList->currentItem()) {
                QString cur = convoList->currentItem()->text();
                if (cur == key || cur == "All") {
                    if (cur == "All") logView->setPlainText(conversations["All"].join("\n"));
                    else logView->setPlainText(conversations[key].join("\n"));
                } else {
                    logView->appendPlainText(line);
                }
                logView->verticalScrollBar()->setValue(logView->verticalScrollBar()->maximum());
            } else {
                logView->appendPlainText(line);
            }
        }
    }
}
