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
#include <sqlite3.h>

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
    const string user_db_path = "users.sqlite"; // SQLite database file
    mutex users_mutex;
    sqlite3* db = nullptr;

public:
    MessengerServer() : server_socket(-1), running(false) {}

    ~MessengerServer() {
        stop();
    }

    // Initialize SQLite DB (create users table if not exists)
    bool initDb() {
        lock_guard<mutex> lock(users_mutex);
        int rc = sqlite3_open(user_db_path.c_str(), &db);
        if (rc != SQLITE_OK) {
            cerr << COLOR_RED << "Failed to open user DB: " << sqlite3_errmsg(db) << COLOR_RESET << endl;
            if (db) sqlite3_close(db);
            db = nullptr;
            return false;
        }

        const char *sql = "CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY, password TEXT);";
        char *err = nullptr;
        rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            cerr << COLOR_RED << "Failed to create users table: " << (err?err:"") << COLOR_RESET << endl;
            if (err) sqlite3_free(err);
            return false;
        }
        // Create friends table: store undirected friendships as two rows or requests
        const char *sql2 = "CREATE TABLE IF NOT EXISTS friends (user TEXT, friend TEXT, status TEXT, PRIMARY KEY(user,friend));";
        rc = sqlite3_exec(db, sql2, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            cerr << COLOR_RED << "Failed to create friends table: " << (err?err:"") << COLOR_RESET << endl;
            if (err) sqlite3_free(err);
            return false;
        }
        return true;
    }

    // Trim leading/trailing whitespace
    string trimStr(const string &s) {
        const char* ws = " \t\n\r";
        size_t start = s.find_first_not_of(ws);
        if (start == string::npos) return string();
        size_t end = s.find_last_not_of(ws);
        return s.substr(start, end - start + 1);
    }

    bool addUser(const string& username, const string& password) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string uname = trimStr(username);
        if (uname.empty()) return false;
        const char *sql = "INSERT INTO users(username,password) VALUES(?,?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, uname.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE);
    }

    bool verifyUser(const string& username, const string& password) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string uname = trimStr(username);
        if (uname.empty()) return false;
        const char *sql = "SELECT password FROM users WHERE username = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, uname.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        bool ok = false;
        if (rc == SQLITE_ROW) {
            const unsigned char *stored = sqlite3_column_text(stmt, 0);
            if (stored && password == reinterpret_cast<const char*>(stored)) ok = true;
        }
        sqlite3_finalize(stmt);
        return ok;
    }

    bool changePassword(const string& username, const string& newpass) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string uname = trimStr(username);
        if (uname.empty()) return false;
        const char *sql = "UPDATE users SET password = ? WHERE username = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, newpass.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, uname.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE);
    }

    bool deleteUser(const string& username) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string uname = trimStr(username);
        if (uname.empty()) return false;
        const char *sql = "DELETE FROM users WHERE username = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, uname.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE);
    }

    // Friend system DB helpers
    bool sendFriendRequest(const string& from, const string& to) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string ufrom = trimStr(from);
        string uto = trimStr(to);
        if (ufrom.empty() || uto.empty()) return false;
        // Insert request with status 'pending'
        const char *sql = "INSERT OR REPLACE INTO friends(user,friend,status) VALUES(?,?,?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, ufrom.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, uto.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, "pending", -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE);
    }

    bool acceptFriendRequest(const string& from, const string& to) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string ufrom = trimStr(from);
        string uto = trimStr(to);
        if (ufrom.empty() || uto.empty()) return false;
        // set both directions to 'accepted'
        const char *sql = "INSERT OR REPLACE INTO friends(user,friend,status) VALUES(?,?,?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, ufrom.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, uto.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, "accepted", -1, SQLITE_STATIC);
        int rc1 = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc1 != SQLITE_DONE) return false;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, uto.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, ufrom.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, "accepted", -1, SQLITE_STATIC);
        int rc2 = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc2 == SQLITE_DONE);
    }

    vector<string> listFriends(const string& username) {
        vector<string> out;
        lock_guard<mutex> lock(users_mutex);
        if (!db) return out;
        string uname = trimStr(username);
        if (uname.empty()) return out;
        const char *sql = "SELECT friend FROM friends WHERE user = ? AND status = 'accepted';";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
        sqlite3_bind_text(stmt, 1, uname.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *f = sqlite3_column_text(stmt, 0);
            if (f) out.push_back(reinterpret_cast<const char*>(f));
        }
        sqlite3_finalize(stmt);
        return out;
    }

    bool removeFriend(const string& user, const string& friendname) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        const char *sql = "DELETE FROM friends WHERE (user = ? AND friend = ?) OR (user = ? AND friend = ?);";
        sqlite3_stmt *stmt = nullptr;
        string u = trimStr(user);
        string f = trimStr(friendname);
        if (u.empty() || f.empty()) return false;
        cout << "Removing friendship between '" << u << "' and '" << f << "'" << endl;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, u.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, f.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, f.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, u.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE);
    }

    bool start() {
        // Create socket
        if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
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
        // Initialize SQLite DB
        if (!initDb()) {
            cerr << COLOR_RED << "Failed to initialize user DB" << COLOR_RESET << endl;
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

        // Authentication flow (register/login/change/delete) before joining
        int bytes_received = recv(client_socket, &msg, sizeof(Message), 0);
        bool authed = false;
        while (bytes_received > 0) {
            if (msg.type == MSG_REGISTER) {
                // msg.username and msg.content contain username and password
                string uname = string(msg.username);
                string pwd = string(msg.content);
                Message resp{};
                resp.type = MSG_AUTH_RESPONSE;
                strncpy(resp.username, "Server", sizeof(resp.username) - 1);
                if (uname.empty() || pwd.empty()) {
                    resp.content[0] = AUTH_FAILURE;
                    send(client_socket, &resp, sizeof(Message), 0);
                } else {
                    if (addUser(uname, pwd)) resp.content[0] = AUTH_SUCCESS; else resp.content[0] = AUTH_FAILURE;
                    send(client_socket, &resp, sizeof(Message), 0);
                }
            }
            else if (msg.type == MSG_LOGIN) {
                string uname = string(msg.username);
                string pwd = string(msg.content);
                Message resp{};
                resp.type = MSG_AUTH_RESPONSE;
                strncpy(resp.username, "Server", sizeof(resp.username) - 1);
                if (verifyUser(uname, pwd)) {
                    resp.content[0] = AUTH_SUCCESS;
                    send(client_socket, &resp, sizeof(Message), 0);
                    client_info.username = uname;
                    authed = true;
                    break;
                } else {
                    resp.content[0] = AUTH_FAILURE;
                    send(client_socket, &resp, sizeof(Message), 0);
                }
            }
            else if (msg.type == MSG_CHANGE_PASSWORD) {
                string uname = string(msg.username);
                string newpass = string(msg.content);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                if (changePassword(uname, newpass)) resp.content[0] = AUTH_SUCCESS; else resp.content[0] = AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
            }
            else if (msg.type == MSG_DELETE_ACCOUNT) {
                string uname = string(msg.username);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                if (deleteUser(uname)) resp.content[0] = AUTH_SUCCESS; else resp.content[0] = AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
            }
            else if (msg.type == MSG_USERNAME) {
                client_info.username = string(msg.username);
                authed = true; // fallback
                break;
            }

            bytes_received = recv(client_socket, &msg, sizeof(Message), 0);
        }

        if (!authed) {
            close(client_socket);
            return;
        }

        // Add to client list
        {
            lock_guard<mutex> lock(clients_mutex);
            clients.push_back(client_info);
        }

        cout << COLOR_GREEN << "\u2713 User '" << client_info.username 
             << "' joined the chat (Total users: " << clients.size() << ")" 
             << COLOR_RESET << endl;

        // Broadcast join message
        string join_msg = client_info.username + " joined the chat";
        broadcastMessage("Server", join_msg, client_socket);

        // Send user list to the new client
        sendUserList(client_socket);

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
            else if (msg.type == MSG_FRIEND_REQUEST) {
                string to = string(msg.content);
                bool ok = sendFriendRequest(client_info.username, to);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
            }
            else if (msg.type == MSG_FRIEND_ACCEPT) {
                string from = string(msg.content); // the user who requested
                bool ok = acceptFriendRequest(from, client_info.username);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
            }
            else if (msg.type == MSG_FRIEND_LIST_REQUEST) {
                auto friends = listFriends(client_info.username);
                Message resp{}; resp.type = MSG_FRIEND_LIST_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                string combined = "Friends: ";
                for (size_t i=0;i<friends.size();++i) { combined += friends[i]; if (i+1<friends.size()) combined += ", "; }
                strncpy(resp.content, combined.c_str(), sizeof(resp.content)-1);
                send(client_socket, &resp, sizeof(Message), 0);
            }
            else if (msg.type == MSG_FRIEND_REMOVE) {
                string target = string(msg.content);
                bool ok = removeFriend(client_info.username, target);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
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

            // Close DB
            if (db) {
                sqlite3_close(db);
                db = nullptr;
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
