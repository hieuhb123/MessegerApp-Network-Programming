#pragma once
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QTcpSocket>
#include <QVBoxLayout>

#include "./include/common.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onConnectClicked();
    void onSendClicked();
    void onReadyRead();

private:
    QPlainTextEdit *logView;
    QLineEdit *input;
    QLineEdit *serverIp;
    QPushButton *connectBtn;
    QPushButton *sendBtn;
    QTcpSocket *socket;
};
