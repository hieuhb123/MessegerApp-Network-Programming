#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"

using namespace std;

struct ClientInfo {
    int socket;
    string username;
    sockaddr_in address;
};

class MessengerServer {
private:
    int server_socket;
    vector<ClientInfo> clients;
    mutex clients_mutex;
    bool running;

public:
    MessengerServer() : server_socket(-1), running(false) {}

    ~MessengerServer() {
        stop();
    }

    bool start() {
        // Create socket
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
            cerr << COLOR_RED << "Failed to create socket" << COLOR_RESET << endl;
            return false;
        }

        // Set socket options to reuse address
        int opt = 1;
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            cerr << COLOR_RED << "Failed to set socket options" << COLOR_RESET << endl;
            close(server_socket);
            return false;
        }

        // Bind socket to port
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(PORT);

        if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            cerr << COLOR_RED << "Failed to bind socket to port " << PORT << COLOR_RESET << endl;
            close(server_socket);
            return false;
        }

        // Listen for connections
        if (listen(server_socket, MAX_CLIENTS) < 0) {
            cerr << COLOR_RED << "Failed to listen on socket" << COLOR_RESET << endl;
            close(server_socket);
            return false;
        }

        running = true;
        cout << COLOR_GREEN << "✓ Server started on port " << PORT << COLOR_RESET << endl;
        cout << COLOR_CYAN << "Waiting for connections..." << COLOR_RESET << endl;
        return true;
    }

    void acceptConnections() {
        while (running) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

            if (client_socket < 0) {
                if (running) {
                    cerr << COLOR_RED << "Failed to accept connection" << COLOR_RESET << endl;
                }
                continue;
            }

            // Check if max clients reached
            {
                lock_guard<mutex> lock(clients_mutex);
                if (clients.size() >= MAX_CLIENTS) {
                    cout << COLOR_YELLOW << "Max clients reached. Connection rejected." << COLOR_RESET << endl;
                    close(client_socket);
                    continue;
                }
            }

            cout << COLOR_CYAN << "New connection from " 
                 << inet_ntoa(client_addr.sin_addr) << ":" 
                 << ntohs(client_addr.sin_port) << COLOR_RESET << endl;

            // Start thread to handle client
            thread(&MessengerServer::handleClient, this, client_socket, client_addr).detach();
        }
    }

    void handleClient(int client_socket, sockaddr_in client_addr) {
        Message msg;
        ClientInfo client_info;
        client_info.socket = client_socket;
        client_info.address = client_addr;
        client_info.username = "Anonymous";

        // Wait for username
        int bytes_received = recv(client_socket, &msg, sizeof(Message), 0);
        if (bytes_received > 0 && msg.type == MSG_USERNAME) {
            client_info.username = string(msg.username);
            
            {
                lock_guard<mutex> lock(clients_mutex);
                clients.push_back(client_info);
            }

            cout << COLOR_GREEN << "✓ User '" << client_info.username 
                 << "' joined the chat (Total users: " << clients.size() << ")" 
                 << COLOR_RESET << endl;

            // Broadcast join message
            string join_msg = client_info.username + " joined the chat";
            broadcastMessage("Server", join_msg, client_socket);

            // Send user list to the new client
            sendUserList(client_socket);
        }

        // Handle messages from client
        while (running) {
            bytes_received = recv(client_socket, &msg, sizeof(Message), 0);
            
            if (bytes_received <= 0) {
                // Client disconnected
                break;
            }

            if (msg.type == MSG_TEXT) {
                string message = string(msg.content);
                cout << COLOR_BLUE << "[" << client_info.username << "]: " 
                     << COLOR_RESET << message << endl;
                
                // Broadcast message to all other clients
                broadcastMessage(client_info.username, message, client_socket);
            }
            else if (msg.type == MSG_DISCONNECT) {
                break;
            }
        }

        // Remove client from list
        {
            lock_guard<mutex> lock(clients_mutex);
            clients.erase(
                remove_if(clients.begin(), clients.end(),
                    [client_socket](const ClientInfo& c) { return c.socket == client_socket; }),
                clients.end()
            );
        }

        cout << COLOR_YELLOW << "✗ User '" << client_info.username 
             << "' left the chat (Total users: " << clients.size() << ")" 
             << COLOR_RESET << endl;

        // Broadcast leave message
        string leave_msg = client_info.username + " left the chat";
        broadcastMessage("Server", leave_msg, client_socket);

        close(client_socket);
    }

    void broadcastMessage(const string& username, const string& content, int exclude_socket = -1) {
        Message msg;
        msg.type = MSG_TEXT;
        strncpy(msg.username, username.c_str(), sizeof(msg.username) - 1);
        strncpy(msg.content, content.c_str(), sizeof(msg.content) - 1);

        lock_guard<mutex> lock(clients_mutex);
        for (const auto& client : clients) {
            if (client.socket != exclude_socket) {
                send(client.socket, &msg, sizeof(Message), 0);
            }
        }
    }

    void sendUserList(int client_socket) {
        lock_guard<mutex> lock(clients_mutex);
        
        string user_list = "Connected users: ";
        for (size_t i = 0; i < clients.size(); i++) {
            user_list += clients[i].username;
            if (i < clients.size() - 1) {
                user_list += ", ";
            }
        }

        Message msg;
        msg.type = MSG_USER_LIST;
        strncpy(msg.username, "Server", sizeof(msg.username) - 1);
        strncpy(msg.content, user_list.c_str(), sizeof(msg.content) - 1);
        
        send(client_socket, &msg, sizeof(Message), 0);
    }

    void stop() {
        if (running) {
            running = false;
            
            // Close all client connections
            {
                lock_guard<mutex> lock(clients_mutex);
                for (const auto& client : clients) {
                    close(client.socket);
                }
                clients.clear();
            }

            // Close server socket
            if (server_socket >= 0) {
                close(server_socket);
                server_socket = -1;
            }

            cout << COLOR_RED << "\nServer stopped" << COLOR_RESET << endl;
        }
    }
};

int main() {
    cout << COLOR_MAGENTA << "========================================" << COLOR_RESET << endl;
    cout << COLOR_MAGENTA << "    C++ Messenger Server" << COLOR_RESET << endl;
    cout << COLOR_MAGENTA << "========================================" << COLOR_RESET << endl;

    MessengerServer server;
    
    if (!server.start()) {
        return 1;
    }

    // Accept connections in main thread
    server.acceptConnections();

    return 0;
}
