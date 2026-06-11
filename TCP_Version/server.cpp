// ============================================================
//  CS-3001 Advanced Chat Application - TCP Server
//  Port: 8080
//  Compile: g++ server.cpp -o server -pthread
// ============================================================

#include <iostream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <sstream>
#include <fstream>
#include <ctime>
#include <cstring>      // for memset
#include <unistd.h>     // for close()
#include <sys/socket.h> // socket(), bind(), listen(), accept()
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // inet_ntoa()
#include <pthread.h>    // threads

using namespace std;

#define PORT 8080
#define MAX_HISTORY 100

// ---------------- Shared Data (protected by mutex) ----------------

// Info about one connected client
struct ClientInfo {
    int socket_fd;      // the client's socket
    string username;    // login name
    string status;      // online / away / busy
};

vector<ClientInfo> clients;          // all connected clients
deque<string> messageHistory;        // last 100 messages
pthread_mutex_t lock_clients = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t lock_history = PTHREAD_MUTEX_INITIALIZER;

// Simple hardcoded user accounts (username -> password)
map<string, string> userAccounts = {
    {"subhan", "1234"},
    {"ali",    "1111"},
    {"sara",   "2222"},
    {"ahmed",  "3333"}
};

// ---------------- Helper Functions ----------------

// Returns current time as a string like [2026-06-11 14:30:05]
string getTimestamp() {
    time_t now = time(0);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
    return string(buffer);
}

// Write a log line to the console AND to server_log.txt
void logEvent(const string &text) {
    string line = getTimestamp() + " " + text;
    cout << line << endl;
    ofstream logFile("server_log.txt", ios::app); // append mode
    if (logFile.is_open()) {
        logFile << line << endl;
    }
}

// Send one full line (adds '\n') to a socket
void sendLine(int sock, const string &msg) {
    string data = msg + "\n";
    send(sock, data.c_str(), data.size(), 0);
}

// Receive one line (until '\n') from a socket.
// Reads 1 byte at a time - simple and reliable for this assignment.
// Returns false if the client disconnected.
bool recvLine(int sock, string &line) {
    line = "";
    char ch;
    while (true) {
        int n = recv(sock, &ch, 1, 0);
        if (n <= 0) return false;   // connection closed or error
        if (ch == '\n') break;      // end of line
        line += ch;
    }
    return true;
}

// Save a message into the history (keep only last 100)
void addToHistory(const string &msg) {
    pthread_mutex_lock(&lock_history);
    messageHistory.push_back(msg);
    if (messageHistory.size() > MAX_HISTORY) {
        messageHistory.pop_front();
    }
    pthread_mutex_unlock(&lock_history);
}

// Send a message to ALL connected clients
void broadcast(const string &msg) {
    pthread_mutex_lock(&lock_clients);
    for (size_t i = 0; i < clients.size(); i++) {
        sendLine(clients[i].socket_fd, msg);
    }
    pthread_mutex_unlock(&lock_clients);
}

// Find a client's socket by username. Returns -1 if not found.
int findClientSocket(const string &name) {
    int result = -1;
    pthread_mutex_lock(&lock_clients);
    for (size_t i = 0; i < clients.size(); i++) {
        if (clients[i].username == name) {
            result = clients[i].socket_fd;
            break;
        }
    }
    pthread_mutex_unlock(&lock_clients);
    return result;
}

// Remove a client from the list when they disconnect
void removeClient(int sock) {
    pthread_mutex_lock(&lock_clients);
    for (size_t i = 0; i < clients.size(); i++) {
        if (clients[i].socket_fd == sock) {
            clients.erase(clients.begin() + i);
            break;
        }
    }
    pthread_mutex_unlock(&lock_clients);
}

// Build a string with the list of active users and their status
string getUserList() {
    string list = "Active users:";
    pthread_mutex_lock(&lock_clients);
    for (size_t i = 0; i < clients.size(); i++) {
        list += " " + clients[i].username + "(" + clients[i].status + ")";
    }
    pthread_mutex_unlock(&lock_clients);
    return list;
}

// ---------------- Command Handling ----------------

// Handles one command line sent by a client. Returns false if client wants to exit.
bool handleCommand(int sock, const string &username, const string &line) {
    stringstream ss(line);
    string command;
    ss >> command;

    if (command == "/exit") {
        sendLine(sock, "SERVER: Goodbye!");
        return false; // signal to close this client
    }
    else if (command == "/help") {
        sendLine(sock, "SERVER: Commands: /msg <user> <text> | /file <user> <path> | /status <online/away/busy> | /history <number> | /list | /help | /exit");
    }
    else if (command == "/list") {
        sendLine(sock, "SERVER: " + getUserList());
    }
    else if (command == "/msg") {
        string target;
        ss >> target;
        string text;
        getline(ss, text);            // rest of the line is the message
        if (target.empty() || text.empty()) {
            sendLine(sock, "SERVER: Usage: /msg <user> <text>");
            return true;
        }
        int targetSock = findClientSocket(target);
        if (targetSock == -1) {
            sendLine(sock, "SERVER: User '" + target + "' is not online.");
        } else {
            // text starts with a space because of getline, so we trim it
            string privateMsg = getTimestamp() + " [PRIVATE] " + username + ":" + text;
            sendLine(targetSock, privateMsg);
            sendLine(sock, getTimestamp() + " [PRIVATE to " + target + "]" + text);
            logEvent("Private message from " + username + " to " + target);
        }
    }
    else if (command == "/status") {
        string newStatus;
        ss >> newStatus;
        if (newStatus != "online" && newStatus != "away" && newStatus != "busy") {
            sendLine(sock, "SERVER: Usage: /status <online/away/busy>");
            return true;
        }
        pthread_mutex_lock(&lock_clients);
        for (size_t i = 0; i < clients.size(); i++) {
            if (clients[i].socket_fd == sock) {
                clients[i].status = newStatus;
                break;
            }
        }
        pthread_mutex_unlock(&lock_clients);
        broadcast("SERVER: " + username + " is now " + newStatus);
        logEvent(username + " changed status to " + newStatus);
    }
    else if (command == "/history") {
        int count = 10; // default
        ss >> count;
        if (count <= 0) count = 10;
        pthread_mutex_lock(&lock_history);
        int total = messageHistory.size();
        int start = total - count;
        if (start < 0) start = 0;
        sendLine(sock, "SERVER: --- Last " + to_string(total - start) + " messages ---");
        for (int i = start; i < total; i++) {
            sendLine(sock, messageHistory[i]);
        }
        pthread_mutex_unlock(&lock_history);
        sendLine(sock, "SERVER: --- End of history ---");
    }
    else if (command == "FILE") {
        // Format coming from client: FILE <targetUser> <filename> <base64data>
        string target, filename, base64data;
        ss >> target >> filename >> base64data;
        if (target.empty() || filename.empty() || base64data.empty()) {
            sendLine(sock, "SERVER: File transfer failed (bad format).");
            return true;
        }
        int targetSock = findClientSocket(target);
        if (targetSock == -1) {
            sendLine(sock, "SERVER: User '" + target + "' is not online. File not sent.");
        } else {
            // Forward the file to the target client
            sendLine(targetSock, "FILE_FROM " + username + " " + filename + " " + base64data);
            sendLine(sock, "SERVER: File '" + filename + "' sent to " + target + ".");
            logEvent("File '" + filename + "' transferred from " + username + " to " + target);
        }
    }
    else {
        sendLine(sock, "SERVER: Unknown command. Type /help for the list.");
    }
    return true;
}

// ---------------- Client Thread ----------------

// Each connected client runs in its own thread
void *clientThread(void *arg) {
    int sock = *(int *)arg;
    delete (int *)arg; // free the memory we allocated in main

    // ---- Step 1: Authentication ----
    string loginLine;
    if (!recvLine(sock, loginLine)) {
        close(sock);
        return NULL;
    }

    // Expected format: LOGIN <username> <password>
    stringstream ss(loginLine);
    string keyword, username, password;
    ss >> keyword >> username >> password;

    if (keyword != "LOGIN" || userAccounts.find(username) == userAccounts.end()
        || userAccounts[username] != password) {
        sendLine(sock, "LOGIN_FAIL Invalid username or password.");
        logEvent("Failed login attempt for user '" + username + "'");
        close(sock);
        return NULL;
    }

    // Reject if same user already logged in
    if (findClientSocket(username) != -1) {
        sendLine(sock, "LOGIN_FAIL User already logged in.");
        close(sock);
        return NULL;
    }

    sendLine(sock, "LOGIN_OK Welcome " + username + "! Type /help for commands.");

    // ---- Step 2: Add to client list ----
    ClientInfo info;
    info.socket_fd = sock;
    info.username = username;
    info.status = "online";
    pthread_mutex_lock(&lock_clients);
    clients.push_back(info);
    pthread_mutex_unlock(&lock_clients);

    logEvent("User '" + username + "' connected.");
    broadcast("SERVER: " + username + " joined the chat."); // join notification

    // ---- Step 3: Main message loop ----
    string line;
    while (recvLine(sock, line)) {
        if (line.empty()) continue;

        if (line[0] == '/' || line.rfind("FILE ", 0) == 0) {
            // It is a command (or a file packet)
            if (!handleCommand(sock, username, line)) break; // /exit
        } else {
            // Normal public message -> broadcast to everyone
            string msg = getTimestamp() + " " + username + ": " + line;
            addToHistory(msg);
            broadcast(msg);
        }
    }

    // ---- Step 4: Cleanup on disconnect ----
    removeClient(sock);
    close(sock);
    logEvent("User '" + username + "' disconnected.");
    broadcast("SERVER: " + username + " left the chat."); // leave notification
    return NULL;
}

// ---------------- Main ----------------

int main() {
    // 1. Create the listening socket
    int serverSock = socket(AF_INET, SOCK_STREAM, 0); // TCP socket
    if (serverSock < 0) {
        perror("socket() failed");
        return 1;
    }

    // Allow quick restart of the server (avoids "Address already in use")
    int opt = 1;
    setsockopt(serverSock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. Bind to port 8080
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY; // accept from any IP
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSock, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind() failed");
        return 1;
    }

    // 3. Start listening (backlog of 10 pending connections)
    if (listen(serverSock, 10) < 0) {
        perror("listen() failed");
        return 1;
    }

    logEvent("TCP Chat Server started on port " + to_string(PORT));

    // 4. Accept clients forever; each one gets its own thread
    while (true) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);
        int clientSock = accept(serverSock, (sockaddr *)&clientAddr, &addrLen);
        if (clientSock < 0) {
            perror("accept() failed");
            continue; // keep the server running
        }

        logEvent("New connection from " + string(inet_ntoa(clientAddr.sin_addr)));

        // Pass the socket to a new thread
        pthread_t tid;
        int *sockPtr = new int(clientSock);
        if (pthread_create(&tid, NULL, clientThread, sockPtr) != 0) {
            perror("pthread_create() failed");
            close(clientSock);
            delete sockPtr;
        } else {
            pthread_detach(tid); // thread cleans itself up when done
        }
    }

    close(serverSock);
    return 0;
}
