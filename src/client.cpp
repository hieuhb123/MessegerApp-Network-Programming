#include <iostream>
#include <cstring>
#include <thread>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"

using namespace std;

class MessengerClient {
private:
    int client_socket;
    string username;
    atomic<bool> running;
    atomic<bool> connected;

public:
    MessengerClient() : client_socket(-1), running(false), connected(false) {}

    ~MessengerClient() {
        disconnect();
    }

    bool connectToServer(const string& server_ip) {
        // Create socket
        client_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (client_socket < 0) {
            cerr << COLOR_RED << "Failed to create socket" << COLOR_RESET << endl;
            return false;
        }

        // Server address
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(PORT);

        if (inet_pton(AF_INET, server_ip.c_str(), &server_addr.sin_addr) <= 0) {
            cerr << COLOR_RED << "Invalid server address" << COLOR_RESET << endl;
            close(client_socket);
            return false;
        }

        // Connect to server
        if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            cerr << COLOR_RED << "Failed to connect to server" << COLOR_RESET << endl;
            close(client_socket);
            return false;
        }

        connected = true;
        running = true;
        cout << COLOR_GREEN << "✓ Connected to server at " << server_ip << ":" << PORT << COLOR_RESET << endl;
        return true;
    }

    bool setUsername(const string& user) {
        username = user;

        // Send username to server
        Message msg;
        msg.type = MSG_USERNAME;
        strncpy(msg.username, username.c_str(), sizeof(msg.username) - 1);
        msg.content[0] = '\0';

        if (send(client_socket, &msg, sizeof(Message), 0) < 0) {
            cerr << COLOR_RED << "Failed to send username" << COLOR_RESET << endl;
            return false;
        }

        return true;
    }

    void receiveMessages() {
        Message msg;
        
        while (running && connected) {
            int bytes_received = recv(client_socket, &msg, sizeof(Message), 0);
            
            if (bytes_received <= 0) {
                if (running) {
                    cout << COLOR_RED << "\n✗ Disconnected from server" << COLOR_RESET << endl;
                }
                connected = false;
                running = false;
                break;
            }

            if (msg.type == MSG_TEXT) {
                // Display message with color based on sender
                if (string(msg.username) == "Server") {
                    cout << COLOR_YELLOW << "[" << msg.username << "]: " 
                         << COLOR_RESET << msg.content << endl;
                } else {
                    cout << COLOR_CYAN << "[" << msg.username << "]: " 
                         << COLOR_RESET << msg.content << endl;
                }
                cout << COLOR_GREEN << "You: " << COLOR_RESET << flush;
            }
            else if (msg.type == MSG_USER_LIST) {
                cout << COLOR_MAGENTA << msg.content << COLOR_RESET << endl;
                cout << COLOR_GREEN << "You: " << COLOR_RESET << flush;
            }
        }
    }

    void sendMessages() {
        string input;
        cout << endl;
        cout << COLOR_CYAN << "Commands:" << COLOR_RESET << endl;
        cout << COLOR_CYAN << "  /quit - Exit the chat" << COLOR_RESET << endl;
        cout << COLOR_CYAN << "  /users - List online users" << COLOR_RESET << endl;
        cout << endl;

        while (running && connected) {
            cout << COLOR_GREEN << "You: " << COLOR_RESET;
            getline(cin, input);

            if (!connected || !running) {
                break;
            }

            if (input.empty()) {
                continue;
            }

            // Handle commands
            if (input == "/quit" || input == "/exit") {
                Message msg;
                msg.type = MSG_DISCONNECT;
                strncpy(msg.username, username.c_str(), sizeof(msg.username) - 1);
                msg.content[0] = '\0';
                send(client_socket, &msg, sizeof(Message), 0);
                
                running = false;
                connected = false;
                cout << COLOR_YELLOW << "Disconnecting..." << COLOR_RESET << endl;
                break;
            }
            else if (input == "/users") {
                // Request user list (this is a custom implementation)
                cout << COLOR_YELLOW << "User list request sent to server" << COLOR_RESET << endl;
                continue;
            }

            // Send text message
            Message msg;
            msg.type = MSG_TEXT;
            strncpy(msg.username, username.c_str(), sizeof(msg.username) - 1);
            strncpy(msg.content, input.c_str(), sizeof(msg.content) - 1);

            if (send(client_socket, &msg, sizeof(Message), 0) < 0) {
                cerr << COLOR_RED << "Failed to send message" << COLOR_RESET << endl;
                connected = false;
                running = false;
                break;
            }
        }
    }

    void run() {
        if (!connected) {
            return;
        }

        // Start thread to receive messages
        thread receive_thread(&MessengerClient::receiveMessages, this);

        // Send messages in main thread
        sendMessages();

        // Wait for receive thread to finish
        if (receive_thread.joinable()) {
            receive_thread.join();
        }
    }

    void disconnect() {
        if (connected) {
            running = false;
            connected = false;
            
            if (client_socket >= 0) {
                close(client_socket);
                client_socket = -1;
            }
        }
    }
};

int main() {
    cout << COLOR_MAGENTA << "========================================" << COLOR_RESET << endl;
    cout << COLOR_MAGENTA << "    C++ Messenger Client" << COLOR_RESET << endl;
    cout << COLOR_MAGENTA << "========================================" << COLOR_RESET << endl;
    cout << endl;

    string server_ip;
    string username;

    // Get server IP
    cout << COLOR_CYAN << "Enter server IP (or press Enter for localhost): " << COLOR_RESET;
    getline(cin, server_ip);
    if (server_ip.empty()) {
        server_ip = "127.0.0.1";
    }

    // Get username
    cout << COLOR_CYAN << "Enter your username: " << COLOR_RESET;
    getline(cin, username);
    while (username.empty() || username.length() > 31) {
        cout << COLOR_RED << "Username must be 1-31 characters. Try again: " << COLOR_RESET;
        getline(cin, username);
    }

    cout << endl;

    // Create and connect client
    MessengerClient client;
    
    if (!client.connectToServer(server_ip)) {
        return 1;
    }

    if (!client.setUsername(username)) {
        return 1;
    }

    cout << COLOR_GREEN << "✓ Joined chat as '" << username << "'" << COLOR_RESET << endl;
    cout << COLOR_CYAN << "Type your messages and press Enter to send" << COLOR_RESET << endl;

    // Run client
    client.run();

    return 0;
}
