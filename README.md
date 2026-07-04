# Advanced Chat Application (Socket Programming in C++)

A complete chat application built for **CS-3001 (Computer Networks)** using raw POSIX
sockets in C++ — no external libraries. It includes both a **TCP version** (reliable,
connection-oriented chat with authentication and file transfer) and a **UDP version**
(fast, connectionless broadcast chat with a custom ACK/retry reliability layer).

## Features

### TCP Version (Port 8080)
- Connection establishment with `socket()`, `bind()`, `listen()`, `accept()`, `connect()`
- User authentication (username + password)
- Multiple clients at the same time (one thread per client)
- Public broadcast messages and private messages (`/msg <user> <text>`)
- File transfer between users with Base64 encoding (`/file <user> <path>`)
- Message history — server stores the last 100 messages (`/history <n>`)
- User status system: online / away / busy (`/status <state>`)
- Active user list (`/list`), join/leave notifications
- Connection and disconnection logging with timestamps (`server_log.txt`)

### UDP Version (Port 8081)
- Connectionless messaging with `sendto()` / `recvfrom()`
- Broadcast messages to all online users
- Online users tracking (server identifies clients by IP:port)
- User status updates and notifications (join, leave, status change)
- **Packet loss handling**: every message carries a sequence number; the client
  waits for an ACK from the server and retries up to 3 times on timeout
- Timestamped server log (`udp_server_log.txt`)

## Commands

| Command | Description |
|---------|-------------|
| `/msg <user> <text>` | Send a private message (TCP) |
| `/file <user> <path>` | Send a file to a user (TCP, Base64 encoded) |
| `/status <online/away/busy>` | Change your status |
| `/history <number>` | Show the last N messages (TCP) |
| `/list` | Show active users with their status |
| `/help` | Show all commands |
| `/exit` | Leave the chat |

Any text that is **not** a command is broadcast to everyone.

## Project Structure

```
Advanced_Chat_Application/
├── TCP_Version/
│   ├── server.cpp        # TCP server (port 8080)
│   └── client.cpp        # TCP client
├── UDP_Version/
│   ├── server.cpp        # UDP server (port 8081)
│   └── client.cpp        # UDP client
├── Report/
│   └── Report.docx       # Project report
├── Makefile
└── README.md
```

## Build

Requires Linux (or WSL on Windows) with `g++` installed.

```bash
make            # builds TCP and UDP versions
```

Or compile manually:

```bash
g++ TCP_Version/server.cpp -o TCP_Version/server -pthread
g++ TCP_Version/client.cpp -o TCP_Version/client -pthread
g++ UDP_Version/server.cpp -o UDP_Version/server
g++ UDP_Version/client.cpp -o UDP_Version/client -pthread
```

## Run

**TCP** (open 3+ terminals):

```bash
cd TCP_Version
./server          # terminal 1
./client          # terminal 2 — login: subhan / 1234
./client          # terminal 3 — login: ali / 1111
```

Test accounts: `subhan/1234`, `ali/1111`, `sara/2222`, `ahmed/3333`

**UDP** (open 3+ terminals):

```bash
cd UDP_Version
./server          # terminal 1
./client          # terminal 2 — just enter a username
./client          # terminal 3
```

## How File Transfer Works

1. Sender types `/file ali notes.txt`
2. The client reads the file in binary mode and encodes it with **Base64**
   (binary bytes → safe text characters) so it fits inside the text protocol.
3. The server forwards `FILE_FROM <sender> <name> <data>` to the recipient.
4. The recipient's client decodes the Base64 data and saves it as
   `received_<name>`.

## How UDP Reliability Works

UDP doesn't guarantee delivery, so a simple Stop-and-Wait scheme is added:

```
Client                    Server
  | -- MSG 5 "hello" -->    |
  | <----- ACK 5 ---------  |   (delivered: done)
  |
  | -- MSG 6 "hi" ------->  X   (packet lost)
  |   ...1s timeout...      |
  | -- MSG 6 "hi" ------->  |   (retry, max 3 attempts)
  | <----- ACK 6 ---------  |
```

## Tech

- Language: **C++** (compiles with `g++`, C++11 or later)
- Sockets: POSIX (`sys/socket.h`, `netinet/in.h`, `arpa/inet.h`)
- Threads: `pthread`
- No external libraries
