#ifndef COMMON_H
#define COMMON_H

#include <string>

// Network configuration
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
