#pragma once
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QSocketNotifier>
#include <QVBoxLayout>
#include <QLabel>
#include <QHBoxLayout>
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
    void onReadyRead();

private:
    // helpers
    void sendMessage(const Message &msg);
    void setLoggedInState(bool loggedIn);

    // UI
    QPlainTextEdit *logView;
    QLineEdit *input;
    QLineEdit *serverIp;
    QPushButton *connectBtn;
    QPushButton *sendBtn;
    int sockfd;
    QSocketNotifier *readNotifier = nullptr;

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
};
