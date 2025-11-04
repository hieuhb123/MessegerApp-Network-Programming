#ifndef COMMON_H
#define COMMON_H

#include <string>

// Network configuration
// #define SERVER_IP "140.245.98.236"
// #define PORT 4424
#define SERVER_IP "127.0.0.1"
#define PORT 8080
#define MAX_CLIENTS 10
#define BUFFER_SIZE 4096

// Message types
#define MSG_TEXT 1
#define MSG_USERNAME 2
#define MSG_DISCONNECT 3
#define MSG_USER_LIST 4

// Account management (no encryption/plaintext passwords)
#define MSG_REGISTER 10
#define MSG_LOGIN 11
#define MSG_AUTH_RESPONSE 12
#define MSG_CHANGE_PASSWORD 13
#define MSG_DELETE_ACCOUNT 14

// Auth response values (content[0] == 1 for success, 0 for failure)
#define AUTH_SUCCESS 1
#define AUTH_FAILURE 0

// Friend system
#define MSG_FRIEND_REQUEST 20
#define MSG_FRIEND_ACCEPT 21
#define MSG_FRIEND_REFUSE 22
#define MSG_FRIEND_LIST_REQUEST 23
#define MSG_FRIEND_LIST_RESPONSE 24
// Unfriend
#define MSG_FRIEND_REMOVE 25

// All users with friendship status relative to requester
#define MSG_ALL_USERS_STATUS_REQUEST 26
#define MSG_ALL_USERS_STATUS_RESPONSE 27

// Direct messaging and history
// Client sends a direct message request; server stores it and delivers to the recipient as MSG_TEXT
#define MSG_DIRECT_MESSAGE 28
// Client requests conversation history with a peer; server responds with newline-delimited lines
#define MSG_HISTORY_REQUEST 29
#define MSG_HISTORY_RESPONSE 30

// Group chat
#define MSG_GROUP_CREATE 40
#define MSG_GROUP_CREATE_RESPONSE 41
#define MSG_GROUP_ADD 42
#define MSG_GROUP_REMOVE 43
#define MSG_GROUP_LEAVE 44
#define MSG_GROUP_MESSAGE 45
#define MSG_GROUP_TEXT 46
#define MSG_GROUP_HISTORY_REQUEST 47
#define MSG_GROUP_HISTORY_RESPONSE 48
#define MSG_GROUP_LIST_REQUEST 49
#define MSG_GROUP_LIST_RESPONSE 50

// Color codes for terminal output
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"

struct Message {
    int type;
    char username[32];
    char content[BUFFER_SIZE];
};

#endif // COMMON_H
