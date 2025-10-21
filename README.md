# C++ Messenger Application

A simple terminal-based messenger application built with C++ using socket programming. This application features a multi-threaded server that can handle multiple clients simultaneously, with real-time message broadcasting.

## Features

- **Multi-client support**: Server can handle up to 10 concurrent clients
- **Real-time messaging**: Messages are instantly broadcast to all connected clients
- **User identification**: Each client has a unique username
- **Colorful terminal output**: Enhanced readability with ANSI color codes
- **Thread-safe operations**: Mutex-protected client list and operations
- **User notifications**: Join/leave notifications for all clients
- **Commands**: Built-in commands like `/quit` and `/users`

## Project Structure

```
MessengerApp/
├── src/
│   ├── server.cpp          # Server implementation
│   └── client.cpp          # Client implementation
├── include/
│   └── common.h            # Shared constants and structures
├── bin/                    # Compiled binaries (generated)
├── obj/                    # Object files (generated)
├── Makefile               # Build configuration
└── README.md              # This file
```

## Requirements
The project requires a C++17-capable compiler and a few development libraries depending on which parts you want to build (console server/client and optional Qt GUI client).

Minimum requirements

- C++ compiler with C++17 support (g++ or clang++)
- CMake (for the Qt client)
- make and build-essential (gcc, g++, make)
- POSIX-compatible OS (Linux, macOS)
- pthreads (usually provided by the standard C/C++ toolchain)
- SQLite development library (headers + runtime)

Recommended packages (install examples)

Debian / Ubuntu:

```bash
sudo apt update
sudo apt install build-essential cmake git libsqlite3-dev
# If you want to build the Qt GUI client:
sudo apt install qtbase5-dev libqt5network5 libqt5network5-dev qtchooser qt5-qmake qtbase5-dev-tools
```

Fedora:

```bash
sudo dnf install @development-tools cmake sqlite-devel
sudo dnf install qt5-qtbase-devel qt5-qttools-devel
```

Arch Linux / Manjaro:

```bash
sudo pacman -Syu base-devel cmake sqlite qt5-base
```

macOS (Homebrew):

```bash
brew install sqlite cmake qt
# You may need to point CMake to Qt's installation via -DCMAKE_PREFIX_PATH
```

Notes

- The console server and client require only a C++ compiler, make, and `libsqlite3-dev` to build and run.
- The Qt GUI client (in `qt-client/`) requires Qt development packages and CMake. If you don't need the GUI, you can ignore the Qt packages and build only the console binaries with `make`.
- If you see errors about missing `sqlite3.h`, install the `libsqlite3-dev` (Debian/Ubuntu) or `sqlite-devel` (Fedora) package and re-run `make`.
- For Qt6 instead of Qt5, adjust the Qt CMake `find_package` lines in `qt-client/CMakeLists.txt` accordingly.

## Building

To build both server and client:

```bash
make
```

To build only the server:

```bash
make run-server
```

To build only the client:

```bash
make run-client
```

To clean build artifacts:

```bash
make clean
```

## Running the Application

### Step 1: Start the Server

In one terminal, run:

```bash
make run-server
```

Or directly:

```bash
./bin/server
```

The server will start listening on port 8080 and display:
```
========================================
    C++ Messenger Server
========================================
✓ Server started on port 8080
Waiting for connections...
```

### Step 2: Start Client(s)

In another terminal (or multiple terminals for multiple clients), run:

```bash
make run-client
```

Or directly:

```bash
./bin/client
```

You'll be prompted to:
1. Enter server IP (press Enter for localhost/127.0.0.1)
2. Enter your username (1-31 characters)

Example:
```
========================================
    C++ Messenger Client
========================================

Enter server IP (or press Enter for localhost): 
Enter your username: Alice

✓ Connected to server at 127.0.0.1:8080
✓ Joined chat as 'Alice'
Type your messages and press Enter to send

Commands:
  /quit - Exit the chat
  /users - List online users

You: 
```

### Step 3: Start Chatting!

Type your messages and press Enter to send. All connected clients will receive your messages in real-time.

## Available Commands

While in the chat, you can use these commands:

- `/quit` or `/exit` - Disconnect from the server and exit
- `/users` - Request list of online users (if implemented on server)

## Example Usage

### Terminal 1 (Server):
```
========================================
    C++ Messenger Server
========================================
✓ Server started on port 8080
Waiting for connections...
New connection from 127.0.0.1:54321
✓ User 'Alice' joined the chat (Total users: 1)
New connection from 127.0.0.1:54322
✓ User 'Bob' joined the chat (Total users: 2)
[Alice]: Hello everyone!
[Bob]: Hi Alice!
```

### Terminal 2 (Client - Alice):
```
✓ Connected to server at 127.0.0.1:8080
✓ Joined chat as 'Alice'
Type your messages and press Enter to send

[Server]: Bob joined the chat
You: Hello everyone!
[Bob]: Hi Alice!
You: 
```

### Terminal 3 (Client - Bob):
```
✓ Connected to server at 127.0.0.1:8080
✓ Joined chat as 'Bob'
Type your messages and press Enter to send

Connected users: Alice, Bob
[Alice]: Hello everyone!
You: Hi Alice!
```

## Technical Details

### Network Protocol

- **Transport**: TCP/IP sockets
- **Port**: 8080 (configurable in `common.h`)
- **Message Structure**: Fixed-size struct with type, username, and content fields
- **Buffer Size**: 4096 bytes per message

### Message Types

1. `MSG_TEXT` - Regular text message
2. `MSG_USERNAME` - Username registration
3. `MSG_DISCONNECT` - Client disconnect notification
4. `MSG_USER_LIST` - List of connected users

### Threading Model

- **Server**: Main thread accepts connections, spawns new thread for each client
- **Client**: Main thread for sending, separate thread for receiving messages

### Configuration

All configuration constants are in `include/common.h`:

- `PORT` - Server port (default: 8080)
- `MAX_CLIENTS` - Maximum concurrent clients (default: 10)
- `BUFFER_SIZE` - Message buffer size (default: 4096 bytes)

## Troubleshooting

### "Address already in use" error
If you see this error when starting the server, either:
- Wait a few seconds for the port to be released
- Use a different port (modify `PORT` in `common.h` and rebuild)

### "Connection refused" error
- Make sure the server is running before starting clients
- Verify the server IP address is correct
- Check if a firewall is blocking port 8080

### Client can't connect to remote server
- Make sure both machines are on the same network
- Use the server's actual IP address (not localhost)
- Ensure firewall rules allow connections on port 8080

## Limitations

- No message encryption (plaintext communication)
- No persistent message history
- No private messaging between users
- Maximum message size is 4096 bytes
- No user authentication

## Future Enhancements

Possible improvements:
- [ ] Add SSL/TLS encryption
- [ ] Implement private messaging
- [ ] Add message history/logging
- [ ] Create a GUI client
- [ ] Add file transfer capability
- [ ] Implement user authentication
- [ ] Add message timestamps
- [ ] Support for chat rooms/channels

## License

This is a demo project for educational purposes.

## Author

Created for learning C++ socket programming and multi-threaded applications.
