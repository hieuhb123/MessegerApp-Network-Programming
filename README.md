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

- C++ compiler with C++17 support (g++)
- Linux/Unix environment (uses POSIX sockets)
- pthread library
- SQLite library
```bash
sudo apt update
sudo apt install libsqlite3-dev
```

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
