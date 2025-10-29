#pragma once
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QTimer>
#include <QVBoxLayout>
#include <QLabel>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMap>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <cstring>

#include "./include/common.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectClicked();
    void onRegisterClicked();
    void onLoginClicked();
    void onSendClicked();
    void onAddFriendClicked();
    void onAcceptFriendClicked();
    void onListFriendsClicked();
    void onUnfriendClicked();
    void onUsersClicked();
    void onHistoryClicked();
    // removed auto read; we'll recv manually per action
    void onConversationChanged();
    void pollMessages();

private:
    // helpers
    void sendMessage(const Message &msg);
    void setLoggedInState(bool loggedIn);
    bool recvMessageBlocking(Message &out, int timeoutMs = 2000);
    void attemptConnect();
    void scheduleReconnect();
    void cleanupSocket();
    bool loadCredentials(QString &user, QString &pass);
    void tryAutoLogin();

    // UI
    QPlainTextEdit *logView;
    QLineEdit *input;
    QLineEdit *serverIp;
    QPushButton *connectBtn;
    QPushButton *sendBtn;
    QPushButton *historyBtn;
    QListWidget *convoList;
    int sockfd;
    QTimer *reconnectTimer = nullptr;
    int reconnectIntervalMs = 2000;
    QTimer *pollTimer = nullptr;

    // auth widgets
    QLineEdit *usernameEdit;
    QLineEdit *passwordEdit;
    QPushButton *registerBtn;
    QPushButton *loginBtn;

    // friend / utility buttons
    QPushButton *addFriendBtn;
    QPushButton *acceptFriendBtn;
    QPushButton *listFriendsBtn;
    QPushButton *unfriendBtn;
    QPushButton *usersBtn;
    

    QString currentUser;
    bool loggedIn = false;
    QMap<QString, QStringList> conversations; // username -> lines
};
