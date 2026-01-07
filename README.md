# Messenger App - Network Programming

A real-time messaging application built with C++ featuring a Qt-based client and a multi-threaded server with SQLite database support.

## Features

- User registration and authentication
- Friend management system
- Direct messaging between users
- Group chat functionality
- Message history storage
- Multi-threaded server architecture
- Qt5 graphical user interface

## Project Structure

```
mess_app/
├── server/                 # Server-side application
│   ├── src/
│   │   └── server.cpp     # Main server implementation
│   ├── include/
│   │   └── common.h       # Shared protocol definitions
│   └── Makefile           # Server build configuration
│
├── qt-client/             # Qt-based client application
│   ├── main.cpp           # Application entry point
│   ├── mainwindow.cpp     # Main window implementation
│   ├── mainwindow.h       # Main window header
│   ├── include/
│   │   └── common.h       # Shared protocol definitions
│   └── CMakeLists.txt     # CMake build configuration
│
└── README.md              # This file
```

## Prerequisites

### For Server
- GCC/G++ compiler with C++17 support
- Make
- SQLite3 development libraries
- pthread library

### For Qt Client
- CMake (version 3.14 or higher)
- Qt5 development libraries (Widgets and Network modules)
- GCC/G++ compiler with C++17 support

## Installation

### Ubuntu/Debian

```bash
# Install server dependencies
sudo apt update
sudo apt install build-essential g++ make libsqlite3-dev

# Install Qt client dependencies
sudo apt install cmake qtbase5-dev qt5-qmake
```


## Building

### Build Server

```bash
cd server
make server
```

This will create the server executable at `server/bin/server`.

### Build Qt Client

```bash
cd qt-client
mkdir -p build
cd build
cmake ..
make
```

This will create the client executable at `qt-client/build/messenger_qt`.

### Build Everything

From the project root:

```bash
# Build server
cd server && make server && cd ..

# Build client
cd qt-client && mkdir -p build && cd build && cmake .. && make && cd ../..
```

## Running

### 1. Start the Server

```bash
cd server
./bin/server
```

The server will:
- Create a SQLite database file (`users.sqlite`) if it doesn't exist
- Start listening on port 8080 (default)
- Display connection and activity logs

**Server Commands:**
- The server runs continuously and logs all activities
- Press `Ctrl+C` to stop the server

### 2. Start the Client(s)

In a new terminal:

```bash
cd qt-client/build
./messenger_qt
```

You can start multiple client instances to test messaging between users.

**Client Features:**
- Register new accounts
- Login with existing credentials
- Add/remove friends
- Send direct messages
- Create and join group chats
- View message history

## Configuration

### Server Configuration

Edit `server/src/server.cpp` to modify:
- **Port**: Change `PORT` constant (default: 8080)
- **Database path**: Modify `user_db_path` variable (default: "users.sqlite")

### Client Configuration

The client connects to `localhost:8080` by default. To connect to a remote server, modify the connection settings in the client code.

## Database Schema

The server uses SQLite with the following tables:

- **users**: Stores user credentials (username, password)
- **friends**: Manages friend relationships and requests
- **messages**: Stores direct message history
- **groups**: Group chat information
- **group_members**: Group membership data

## Development

### Clean Build

```bash
# Clean server
cd server
make clean

# Clean client
cd qt-client/build
make clean
# Or remove build directory entirely
cd .. && rm -rf build
```

### Debugging

Enable debug output by compiling with debug flags:

```bash
# Server with debug symbols
cd server
make CXXFLAGS="-std=c++17 -Wall -Wextra -pthread -I./include -g" server

# Client with debug symbols
cd qt-client/build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

## Troubleshooting

### Port Already in Use

If the server fails to start with "Address already in use":
```bash
# Find and kill process using port 8080
sudo lsof -i :8080
kill -9 <PID>
```

### SQLite Errors

If you encounter database errors:
```bash
# Remove and recreate database
cd server
rm users.sqlite
./bin/server  # Will recreate database
```

### Qt Client Fails to Build

Ensure Qt5 is properly installed:
```bash
# Check Qt installation
qmake --version

# If Qt5 is in a custom location, specify it:
cmake -DCMAKE_PREFIX_PATH=/path/to/Qt/5.15.2/gcc_64 ..
```

### Connection Issues

- Ensure the server is running before starting clients
- Check firewall settings if connecting remotely
- Verify the server IP and port in client configuration

## Architecture

### Server
- Multi-threaded design with one thread per client connection
- Mutex-protected shared resources for thread safety
- SQLite database for persistent storage
- Message broadcasting and routing system

### Client
- Event-driven Qt application
- Asynchronous network communication using Qt Network
- GUI built with Qt Widgets

## Protocol

The application uses a custom text-based protocol over TCP sockets. Messages are formatted as commands with parameters separated by spaces.

## License

This project is for educational purposes as part of Network Programming coursework.

## Contributors

- Original repository: https://github.com/hieuhb123/MessegerApp-Network-Programming

## Support

For issues and questions, please refer to the original repository or contact the course instructor.
