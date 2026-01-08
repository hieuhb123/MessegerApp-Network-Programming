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
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>

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
    // logging
    ofstream logFile;
    mutex log_mutex;

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
        // Messages table: store direct messages between users
        const char *sql3 =
            "CREATE TABLE IF NOT EXISTS messages (\n"
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,\n"
            "  sender TEXT NOT NULL,\n"
            "  receiver TEXT NOT NULL,\n"
            "  content TEXT NOT NULL,\n"
            "  ts INTEGER NOT NULL DEFAULT (strftime('%s','now'))\n"
            ");";
        rc = sqlite3_exec(db, sql3, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            cerr << COLOR_RED << "Failed to create messages table: " << (err?err:"") << COLOR_RESET << endl;
            if (err) sqlite3_free(err);
            return false;
        }
        // Groups table
        const char *sql4 =
            "CREATE TABLE IF NOT EXISTS groups (name TEXT PRIMARY KEY, owner TEXT);";
        rc = sqlite3_exec(db, sql4, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            cerr << COLOR_RED << "Failed to create groups table: " << (err?err:"") << COLOR_RESET << endl;
            if (err) sqlite3_free(err);
            return false;
        }
        // Group members table
        const char *sql5 =
            "CREATE TABLE IF NOT EXISTS group_members (groupname TEXT, member TEXT, PRIMARY KEY(groupname,member));";
        rc = sqlite3_exec(db, sql5, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            cerr << COLOR_RED << "Failed to create group_members table: " << (err?err:"") << COLOR_RESET << endl;
            if (err) sqlite3_free(err);
            return false;
        }
        // Group messages table
        const char *sql6 =
            "CREATE TABLE IF NOT EXISTS group_messages ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " groupname TEXT NOT NULL,"
            " sender TEXT NOT NULL,"
            " content TEXT NOT NULL,"
            " ts INTEGER NOT NULL DEFAULT (strftime('%s','now'))"
            " );";
        rc = sqlite3_exec(db, sql6, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            cerr << COLOR_RED << "Failed to create group_messages table: " << (err?err:"") << COLOR_RESET << endl;
            if (err) sqlite3_free(err);
            return false;
        }
        return true;
    }

    void logActivity(const string &msg) {
        // thread-safe append with timestamp
        lock_guard<mutex> lock(log_mutex);
        if (!logFile.is_open()) return;
        // timestamp
        using namespace chrono;
        auto now = system_clock::now();
        time_t t = system_clock::to_time_t(now);
        struct tm tm;
        localtime_r(&t, &tm);
        char buf[64];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        logFile << "[" << buf << "] " << msg << endl;
        logFile.flush();
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

    // Group helpers
    bool createGroup(const string& groupname, const string& owner) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string g = trimStr(groupname);
        string o = trimStr(owner);
        if (g.empty() || o.empty()) return false;
        // ensure group doesn't already exist
        const char *chk = "SELECT 1 FROM groups WHERE name = ? LIMIT 1;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, chk, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, g.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc == SQLITE_ROW) return false; // exists

        const char *ins = "INSERT INTO groups(name,owner) VALUES(?,?);";
        if (sqlite3_prepare_v2(db, ins, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, g.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, o.c_str(), -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE) return false;
        // add owner as member
        const char *minsert = "INSERT OR REPLACE INTO group_members(groupname,member) VALUES(?,?);";
        if (sqlite3_prepare_v2(db, minsert, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, g.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, o.c_str(), -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE);
    }

    bool addUserToGroup(const string& groupname, const string& user) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string g = trimStr(groupname);
        string u = trimStr(user);
        if (g.empty() || u.empty()) return false;
        // ensure group exists
        const char *chk = "SELECT 1 FROM groups WHERE name = ? LIMIT 1;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, chk, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, g.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_ROW) return false;
        const char *ins = "INSERT OR REPLACE INTO group_members(groupname,member) VALUES(?,?);";
        if (sqlite3_prepare_v2(db, ins, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, g.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, u.c_str(), -1, SQLITE_STATIC);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE);
    }

    bool removeUserFromGroup(const string& groupname, const string& user) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string g = trimStr(groupname);
        string u = trimStr(user);
        if (g.empty() || u.empty()) return false;
        const char *del = "DELETE FROM group_members WHERE groupname = ? AND member = ?;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, del, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, g.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, u.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        int changes = sqlite3_changes(db);
        return (rc == SQLITE_DONE && changes > 0);
    }

    bool isMemberOfGroup(const string& groupname, const string& user) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        const char *q = "SELECT 1 FROM group_members WHERE groupname = ? AND member = ? LIMIT 1;";
        sqlite3_stmt *stmt = nullptr;
        bool ok = false;
        if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, groupname.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, user.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) ok = true;
            sqlite3_finalize(stmt);
        }
        return ok;
    }

    vector<string> listGroupsForUser(const string& user) {
        vector<string> out;
        lock_guard<mutex> lock(users_mutex);
        if (!db) return out;
        const char *sql = "SELECT groupname FROM group_members WHERE member = ? ORDER BY groupname;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
        sqlite3_bind_text(stmt, 1, user.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *g = sqlite3_column_text(stmt, 0);
            if (g) out.push_back(reinterpret_cast<const char*>(g));
        }
        sqlite3_finalize(stmt);
        return out;
    }

    bool saveGroupMessage(const string& groupname, const string& sender, const string& content) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        const char *ins = "INSERT INTO group_messages(groupname,sender,content) VALUES(?,?,?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, ins, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, groupname.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, sender.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return (rc == SQLITE_DONE);
    }

    string getGroupHistory(const string& groupname, int limit = 200) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return string("No DB");
        const char *q =
            "SELECT sender, content, ts FROM group_messages WHERE groupname = ? ORDER BY id ASC LIMIT ?;";
        sqlite3_stmt *stmt = nullptr;
        string out;
        if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) != SQLITE_OK) return string("DB error");
        sqlite3_bind_text(stmt, 1, groupname.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *sender = sqlite3_column_text(stmt, 0);
            const unsigned char *body = sqlite3_column_text(stmt, 1);
            int ts = sqlite3_column_int(stmt, 2);
            time_t t = static_cast<time_t>(ts);
            struct tm lt;
            localtime_r(&t, &lt);
            char tbuf[64];
            strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &lt);
            string line;
            line.reserve(128);
            if (tbuf) line += string("[") + tbuf + "] ";
            if (sender) line += reinterpret_cast<const char*>(sender);
            line += ": ";
            if (body) line += reinterpret_cast<const char*>(body);
            line += "\n";
            if (out.size() + line.size() > BUFFER_SIZE - 32) { out += "...\n"; break; }
            out += line;
        }
        sqlite3_finalize(stmt);
        if (out.empty()) out = string("(no messages)\n");
        return out;
    }

    vector<string> listGroupMembers(const string& groupname) {
        vector<string> out;
        lock_guard<mutex> lock(users_mutex);
        if (!db) return out;
        const char *sql = "SELECT member FROM group_members WHERE groupname = ? ORDER BY member;";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
        sqlite3_bind_text(stmt, 1, groupname.c_str(), -1, SQLITE_STATIC);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *m = sqlite3_column_text(stmt, 0);
            if (m) out.push_back(reinterpret_cast<const char*>(m));
        }
        sqlite3_finalize(stmt);
        return out;
    }

    bool acceptFriendRequest(const string& from, const string& to) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string ufrom = trimStr(from);
        string uto = trimStr(to);
        if (ufrom.empty() || uto.empty()) return false;
        // Only accept if there is a pending request from 'from' -> 'to'
        const char *check_q = "SELECT status FROM friends WHERE user = ? AND friend = ? LIMIT 1;";
        sqlite3_stmt *stmt = nullptr;
        bool hasPending = false;
        if (sqlite3_prepare_v2(db, check_q, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, ufrom.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, uto.c_str(), -1, SQLITE_STATIC);
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                const unsigned char *st = sqlite3_column_text(stmt, 0);
                string s = st ? reinterpret_cast<const char*>(st) : string();
                if (s == "pending") hasPending = true;
            }
            sqlite3_finalize(stmt);
        } else {
            return false;
        }

        if (!hasPending) {
            // no pending request to accept
            return false;
        }

        // set both directions to 'accepted'
        const char *sql = "INSERT OR REPLACE INTO friends(user,friend,status) VALUES(?,?,?);";
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

    bool refuseFriendRequest(const string& from, const string& to) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        string ufrom = trimStr(from);
        string uto = trimStr(to);
        if (ufrom.empty() || uto.empty()) return false;
        const char *sql = "DELETE FROM friends WHERE user = ? AND friend = ? AND status = 'pending';";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, ufrom.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, uto.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        // sqlite3_step returns SQLITE_DONE even if no rows deleted; check changes
        int changes = sqlite3_changes(db);
        return (rc == SQLITE_DONE && changes > 0);
    }

    vector<string> listFriends(const string& username) {
        vector<string> out;
        
        // First, get the friends list from DB with their friendship status
        vector<pair<string, string>> friendsWithStatus;
        {
            lock_guard<mutex> lock(users_mutex);
            if (!db) return out;
            string uname = trimStr(username);
            if (uname.empty()) return out;
            
            // Query 1: Get outgoing requests and accepted friends (where user = current user)
            const char *sql = "SELECT friend, status FROM friends WHERE user = ? AND (status = 'accepted' OR status = 'pending');";
            sqlite3_stmt *stmt = nullptr;
            if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return out;
            sqlite3_bind_text(stmt, 1, uname.c_str(), -1, SQLITE_STATIC);
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const unsigned char *f = sqlite3_column_text(stmt, 0);
                const unsigned char *s = sqlite3_column_text(stmt, 1);
                if (f) {
                    string friendName = reinterpret_cast<const char*>(f);
                    string friendStatus = s ? reinterpret_cast<const char*>(s) : "unknown";
                    // Mark outgoing pending requests as "outgoing" to differentiate from incoming
                    if (friendStatus == "pending") {
                        friendStatus = "outgoing";
                    }
                    friendsWithStatus.push_back({friendName, friendStatus});
                }
            }
            sqlite3_finalize(stmt);
            
            // Query 2: Get incoming friend requests (where friend = current user and status = pending)
            const char *sql2 = "SELECT user FROM friends WHERE friend = ? AND status = 'pending';";
            if (sqlite3_prepare_v2(db, sql2, -1, &stmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(stmt, 1, uname.c_str(), -1, SQLITE_STATIC);
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    const unsigned char *f = sqlite3_column_text(stmt, 0);
                    if (f) {
                        string friendName = reinterpret_cast<const char*>(f);
                        friendsWithStatus.push_back({friendName, "pending"});
                    }
                }
                sqlite3_finalize(stmt);
            }
        }
        
        // Then check online status for each friend
        for (const auto& fs : friendsWithStatus) {
            bool isOnline = false;
            {
                lock_guard<mutex> lock(clients_mutex);
                for (const auto& c : clients) {
                    if (c.username == fs.first) {
                        isOnline = true;
                        break;
                    }
                }
            }
            string onlineStatus = isOnline ? "online" : "offline";
            out.push_back(fs.first + ": " + fs.second + ", " + onlineStatus);
        }
        
        return out;
    }

    string friendStatus(const string& viewer, const string& other) {
        if (viewer == other) return string("self");
        const char *q = "SELECT status FROM friends WHERE user = ? AND friend = ? LIMIT 1;";
        sqlite3_stmt *stmt = nullptr;
        // check viewer -> other
        if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, viewer.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, other.c_str(), -1, SQLITE_STATIC);
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                const unsigned char *st = sqlite3_column_text(stmt, 0);
                string s = st ? reinterpret_cast<const char*>(st) : "";
                sqlite3_finalize(stmt);
                if (s == "accepted") return string("friend");
                if (s == "pending") return string("outgoing");
            }
            sqlite3_finalize(stmt);
        }
        // check other -> viewer (incoming pending)
        if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, other.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, viewer.c_str(), -1, SQLITE_STATIC);
            int rc = sqlite3_step(stmt);
            if (rc == SQLITE_ROW) {
                const unsigned char *st = sqlite3_column_text(stmt, 0);
                string s = st ? reinterpret_cast<const char*>(st) : "";
                sqlite3_finalize(stmt);
                if (s == "accepted") return string("friend");
                if (s == "pending") return string("incoming");
            }
            sqlite3_finalize(stmt);
        }
        return string("none");
    }

    string listAllUsersWithStatus(const string& viewer) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return string("No DB");
        string v = trimStr(viewer);
        if (v.empty()) return string("No viewer");
        const char *sql = "SELECT username FROM users ORDER BY username;";
        sqlite3_stmt *stmt = nullptr;
        string out;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return string("DB error");
        out = "Users and status:\n";
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *u = sqlite3_column_text(stmt, 0);
            if (!u) continue;
            string uname = reinterpret_cast<const char*>(u);
            string status = friendStatus(v, uname);
            out += "- " + uname + ": " + status + "\n";
            if (out.size() > BUFFER_SIZE - 64) { // safety truncation
                out += "...\n";
                break;
            }
        }
        sqlite3_finalize(stmt);
        return out;
    }

    // Check if two users are friends (accepted)
    bool areFriends(const string& a, const string& b) {
        if (a == b) return false;
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        const char *q = "SELECT 1 FROM friends WHERE user = ? AND friend = ? AND status = 'accepted' LIMIT 1;";
        sqlite3_stmt *stmt = nullptr;
        bool ok = false;
        if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) ok = true;
            sqlite3_finalize(stmt);
        }
        if (ok) return true;
        // Check reverse just in case (shouldn't be needed if both rows exist)
        if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, b.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 2, a.c_str(), -1, SQLITE_STATIC);
            if (sqlite3_step(stmt) == SQLITE_ROW) ok = true;
            sqlite3_finalize(stmt);
        }
        return ok;
    }

    bool saveMessage(const string& sender, const string& receiver, const string& content) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return false;
        const char *ins = "INSERT INTO messages(sender,receiver,content) VALUES(?,?,?);";
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(db, ins, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, sender.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, receiver.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_STATIC);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        return rc == SQLITE_DONE;
    }

    string getConversationHistory(const string& a, const string& b, int limit = 100) {
        lock_guard<mutex> lock(users_mutex);
        if (!db) return string("No DB");
        const char *q =
            "SELECT sender, content, ts FROM messages\n"
            "WHERE (sender = ? AND receiver = ?) OR (sender = ? AND receiver = ?)\n"
            "ORDER BY id ASC LIMIT ?;";
        sqlite3_stmt *stmt = nullptr;
        string out;
        if (sqlite3_prepare_v2(db, q, -1, &stmt, nullptr) != SQLITE_OK) return string("DB error");
        sqlite3_bind_text(stmt, 1, a.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, b.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, b.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, a.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 5, limit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *sender = sqlite3_column_text(stmt, 0);
            const unsigned char *body = sqlite3_column_text(stmt, 1);
            int ts = sqlite3_column_int(stmt, 2);
            // format timestamp to human-readable
            time_t t = static_cast<time_t>(ts);
            struct tm lt;
            localtime_r(&t, &lt);
            char tbuf[64];
            strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &lt);
            string line;
            line.reserve(128);
            if (tbuf) line += string("[") + tbuf + "] ";
            if (sender) line += reinterpret_cast<const char*>(sender);
            line += ": ";
            if (body) line += reinterpret_cast<const char*>(body);
            line += "\n";
            if (out.size() + line.size() > BUFFER_SIZE - 32) { out += "...\n"; break; }
            out += line;
        }
        sqlite3_finalize(stmt);
        if (out.empty()) out = string("(no messages)\n");
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

        // open activity log
        logFile.open("server_activity.log", ios::app);
        if (logFile.is_open()) {
            logActivity(string("Server started on port ") + to_string(PORT));
        } else {
            cerr << COLOR_YELLOW << "Warning: could not open server_activity.log for writing" << COLOR_RESET << endl;
        }

        running = true;
        cout << COLOR_GREEN << "✓ Server started on port " << PORT << COLOR_RESET << endl;
        cout << COLOR_CYAN << "Waiting for connections..." << COLOR_RESET << endl;
        logActivity("Waiting for connections...");
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
                    logActivity("Failed to accept connection");
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

            {
                string peer = string(inet_ntoa(client_addr.sin_addr)) + ":" + to_string(ntohs(client_addr.sin_port));
                cout << COLOR_CYAN << "New connection from " << peer << COLOR_RESET << endl;
                logActivity(string("New connection from ") + peer);
            }

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
                string uname = string(msg.username);
                string pwd = string(msg.content);
                Message resp{};
                resp.type = MSG_AUTH_RESPONSE;
                strncpy(resp.username, "Server", sizeof(resp.username) - 1);
                if (uname.empty() || pwd.empty()) {
                    resp.content[0] = AUTH_FAILURE;
                    send(client_socket, &resp, sizeof(Message), 0);
                } else {
                    if (addUser(uname, pwd)) {
                        resp.content[0] = AUTH_SUCCESS;
                        send(client_socket, &resp, sizeof(Message), 0);
                        client_info.username = uname;
                        authed = true;
                        break;
                    }
                         
                    else resp.content[0] = AUTH_FAILURE;
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

        cout << COLOR_GREEN << "User '" << client_info.username 
             << "' joined the chat (Total users: " << clients.size() << ")" 
             << COLOR_RESET << endl;
        logActivity(string("User '") + client_info.username + " joined (total=" + to_string(clients.size()) + ")");

        // Handle messages from client
        while (running) {
            bytes_received = recv(client_socket, &msg, sizeof(Message), 0);
            
            if (bytes_received <= 0) {
                // Client disconnected
                break;
            }
            if (msg.type == MSG_FRIEND_REQUEST) {
                string to = string(msg.content);
                bool ok = sendFriendRequest(client_info.username, to);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Friend request: ") + client_info.username + " -> " + to + (ok?" [ok]":" [fail]"));
            }
            else if (msg.type == MSG_FRIEND_ACCEPT) {
                string from = string(msg.content); // the user who requested
                bool ok = acceptFriendRequest(from, client_info.username);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
            }
            else if (msg.type == MSG_GROUP_CREATE) {
                string gname = trimStr(string(msg.content));
                bool ok = createGroup(gname, client_info.username);
                Message resp{}; resp.type = MSG_GROUP_CREATE_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Group create: ") + client_info.username + " -> " + gname + (ok?" [ok]":" [fail]"));
            }
            else if (msg.type == MSG_GROUP_ADD) {
                // Expect: msg.username = groupname, msg.content = username-to-add
                string gname = trimStr(string(msg.username));
                string who = trimStr(string(msg.content));
                bool ok = false;
                // only members can add (simple policy)
                if (isMemberOfGroup(gname, client_info.username)) ok = addUserToGroup(gname, who);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Group add: ") + client_info.username + " add " + who + " to " + gname + (ok?" [ok]":" [fail]"));
            }
            else if (msg.type == MSG_GROUP_REMOVE) {
                // Expect: msg.username = groupname, msg.content = username-to-remove
                string gname = trimStr(string(msg.username));
                string who = trimStr(string(msg.content));
                bool ok = false;
                // only members can remove (or owner could have been enforced)
                if (isMemberOfGroup(gname, client_info.username)) ok = removeUserFromGroup(gname, who);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Group remove: ") + client_info.username + " remove " + who + " from " + gname + (ok?" [ok]":" [fail]"));
            }
            else if (msg.type == MSG_GROUP_LEAVE) {
                // Expect: msg.content = groupname
                string gname = trimStr(string(msg.content));
                bool ok = removeUserFromGroup(gname, client_info.username);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Group leave: ") + client_info.username + " left " + gname + (ok?" [ok]":" [fail]"));
            }
            else if (msg.type == MSG_GROUP_MESSAGE) {
                // msg.username = groupname, msg.content = body
                string gname = trimStr(string(msg.username));
                string body = string(msg.content);
                bool ok = false;
                if (!gname.empty() && !body.empty() && isMemberOfGroup(gname, client_info.username)) {
                    ok = saveGroupMessage(gname, client_info.username, body);
                    if (ok) {
                        // deliver to online members (excluding sender)
                        vector<string> members = listGroupMembers(gname);
                        lock_guard<mutex> lock(clients_mutex);
                        for (const auto &c : clients) {
                            if (c.username == client_info.username) continue;
                            if (find(members.begin(), members.end(), c.username) != members.end()) {
                                Message gm{}; 
                                gm.type = MSG_GROUP_TEXT; 
                                strncpy(gm.username, gname.c_str(), sizeof(gm.username)-1);
                                // content: sender:body
                                string payload = client_info.username + string(": ") + body;
                                strncpy(gm.content, payload.c_str(), sizeof(gm.content)-1);
                                send(c.socket, &gm, sizeof(Message), 0);
                            }
                        }
                            logActivity(string("Group message: ") + client_info.username + " -> " + gname + " (len=" + to_string(body.size()) + ")");
                    }
                }
            }
            else if (msg.type == MSG_GROUP_HISTORY_REQUEST) {
                string gname = trimStr(string(msg.username));
                string listing;
                if (!gname.empty() && isMemberOfGroup(gname, client_info.username)) {
                    listing = getGroupHistory(gname, 500);
                } else {
                    listing = string("Invalid group or access denied\n");
                }
                Message resp{}; resp.type = MSG_GROUP_HISTORY_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                strncpy(resp.content, listing.c_str(), sizeof(resp.content)-1);
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Group history requested: ") + client_info.username + " -> " + gname);
            }
            else if (msg.type == MSG_GROUP_MEMBERS_REQUEST) {
                string gname = trimStr(string(msg.username));
                string listing;
                if (!gname.empty() && isMemberOfGroup(gname, client_info.username)) {
                    auto members = listGroupMembers(gname);
                    for (size_t i = 0; i < members.size(); ++i) {
                        listing += members[i];
                        if (i + 1 < members.size()) listing += ", ";
                    }
                    if (listing.empty()) listing = string("(no members)");
                } else {
                    listing = string("Access denied or invalid group");
                }
                Message resp{}; resp.type = MSG_GROUP_MEMBERS_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                strncpy(resp.content, listing.c_str(), sizeof(resp.content)-1);
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Group members requested: ") + client_info.username + " -> " + gname);
            }
            else if (msg.type == MSG_GROUP_LIST_REQUEST) {
                auto groups = listGroupsForUser(client_info.username);
                Message resp{}; resp.type = MSG_GROUP_LIST_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                string combined;
                for (size_t i = 0; i < groups.size(); ++i) { combined += groups[i]; if (i+1<groups.size()) combined += ", "; }
                strncpy(resp.content, combined.c_str(), sizeof(resp.content)-1);
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Group list requested: ") + client_info.username);
            }
            else if (msg.type == MSG_FRIEND_REFUSE) {
                string from = string(msg.content); // the user who requested
                bool ok = refuseFriendRequest(from, client_info.username);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Friend refuse: ") + client_info.username + " <- " + from + (ok?" [ok]":" [fail]"));
            }
            else if (msg.type == MSG_FRIEND_LIST_REQUEST) {
                auto friends = listFriends(client_info.username);
                Message resp{}; resp.type = MSG_FRIEND_LIST_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                string combined = "Friends: ";
                for (size_t i=0;i<friends.size();++i) { combined += friends[i]; if (i+1<friends.size()) combined += ", "; }
                strncpy(resp.content, combined.c_str(), sizeof(resp.content)-1);
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Friend list requested: ") + client_info.username);
            }
            else if (msg.type == MSG_FRIEND_REMOVE) {
                string target = string(msg.content);
                bool ok = removeFriend(client_info.username, target);
                Message resp{}; resp.type = MSG_AUTH_RESPONSE; strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = ok ? AUTH_SUCCESS : AUTH_FAILURE;
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("Friend remove: ") + client_info.username + " -/-> " + target + (ok?" [ok]":" [fail]"));
            }
            else if (msg.type == MSG_ALL_USERS_STATUS_REQUEST) {
                string listing = listAllUsersWithStatus(client_info.username);
                Message resp{}; 
                resp.type = MSG_ALL_USERS_STATUS_RESPONSE; 
                strncpy(resp.username, "Server", sizeof(resp.username)-1);
                resp.content[0] = '\0';
                strncpy(resp.content, listing.c_str(), sizeof(resp.content)-1);
                send(client_socket, &resp, sizeof(Message), 0);
                logActivity(string("All users/status requested: ") + client_info.username);
            }
            else if (msg.type == MSG_DIRECT_MESSAGE) {
                // msg.username holds the receiver, msg.content holds the body; sender is client_info.username
                string to = trimStr(string(msg.username));
                string body = string(msg.content);
                bool ok = false;
                if (!to.empty() && !body.empty()) {
                    ok = saveMessage(client_info.username, to, body);
                    if (ok) {
                        // deliver to online recipient as a chat message (MSG_TEXT)
                        lock_guard<mutex> lock(clients_mutex);
                        for (const auto &c : clients) {
                            if (c.username == to) {
                                Message dm{};
                                dm.type = MSG_TEXT;
                                strncpy(dm.username, client_info.username.c_str(), sizeof(dm.username)-1);
                                strncpy(dm.content, body.c_str(), sizeof(dm.content)-1);
                          // deliver DM to online recipient
                                send(c.socket, &dm, sizeof(Message), 0);
                                break;
                            }
                        }
                            logActivity(string("Direct message: ") + client_info.username + " -> " + to + " (len=" + to_string(body.size()) + ")");
                    }
                }
            }
            else if (msg.type == MSG_HISTORY_REQUEST) {
                // msg.username holds the peer
                string peer = trimStr(string(msg.username));
                string listing;
                if (!peer.empty()) {
                    listing = getConversationHistory(client_info.username, peer, 200);
                } else {
                    listing = string("Invalid peer\n");
                }
                Message resp{}; 
                resp.type = MSG_HISTORY_RESPONSE; 
                strncpy(resp.username, "Server", sizeof(resp.username)-1);
                strncpy(resp.content, listing.c_str(), sizeof(resp.content)-1);
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

        cout << COLOR_YELLOW << "User '" << client_info.username 
             << "' left the chat (Total users: " << clients.size() << ")" 
             << COLOR_RESET << endl;
        logActivity(string("User '") + client_info.username + " left (total=" + to_string(clients.size()) + ")");

        close(client_socket);
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
