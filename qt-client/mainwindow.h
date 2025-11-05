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
#include <QList>
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
    void onRegisterClicked();
    void onLoginClicked();
    void onSendClicked();
    void onListFriendsClicked();
    void onUsersClicked();
    void onCreateGroupClicked();
    void onGroupsClicked();
    void onHistoryClicked();
    // removed auto read; we'll recv manually per action
    void onConversationChanged();
    void pollMessages();
    void attemptConnect();
    void scheduleReconnect();
    void onDisconnectClicked();

private:
    // helpers
    void sendMessage(const Message &msg);
    void flushPendingMessages();
    void appendLog(const QString &text);
    void setLoggedInState(bool loggedIn);
    bool recvMessageBlocking(Message &out, int timeoutMs = 2000);
    void cleanupSocket();
    bool loadCredentials(QString &user, QString &pass);
    void tryAutoLogin();

    // UI
    QPlainTextEdit *logView;
    QLineEdit *input;
    QPushButton *sendBtn;
    QPushButton *historyBtn;
    QListWidget *convoList;
    int sockfd;
    int reconnectIntervalMs = 2000;
    QTimer *reconnectTimer = nullptr;
    QTimer *pollTimer = nullptr;

    // auth widgets
    QLineEdit *usernameEdit;
    QLineEdit *passwordEdit;
    QPushButton *registerBtn;
    QPushButton *loginBtn;

    // friend / utility buttons
    QPushButton *listFriendsBtn;
    QPushButton *usersBtn;
    // connection controls
    QPushButton *connectBtn;
    QPushButton *disconnectBtn;
    // group buttons
    QPushButton *createGroupBtn;
    QPushButton *listGroupsBtn;
    

    QString currentUser;
    bool loggedIn = false;
    QMap<QString, QStringList> conversations; // username -> lines
    QList<Message> pendingMessages; // messages queued while offline
    QFile logFile;
};
