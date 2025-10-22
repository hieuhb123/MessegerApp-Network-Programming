CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -pthread -I./include
LDFLAGS = -pthread -lsqlite3

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# Create directories if they don't exist
$(shell mkdir -p $(OBJ_DIR) $(BIN_DIR))

# Targets
SERVER = $(BIN_DIR)/server

# Source files
SERVER_SRC = $(SRC_DIR)/server.cpp

# Object files
SERVER_OBJ = $(OBJ_DIR)/server.o

.PHONY: all clean server client

all: server client

server: $(SERVER)

$(SERVER): $(SERVER_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

run-server:
	./$(SERVER)

