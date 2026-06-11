// ============================================================
//  CS-3001 Advanced Chat Application - UDP Server
//  Port: 8081
//  Compile: g++ server.cpp -o server
//
//  UDP is connectionless, so the server identifies each client
//  by its IP address + port number (no accept() like TCP).
// ============================================================

#include <iostream>
#include <string>
#include <map>
#include <sstream>
#include <fstream>
#include <ctime>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h> // socket(), bind(), sendto(), recvfrom()
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#define PORT 8081
#define BUFFER_SIZE 2048

// Info about one online user
struct UdpUser {
    string username;
    string status;       // online / away / busy
    sockaddr_in address; // where to send packets back
};

// Key = "ip:port" string, so each client is unique
map<string, UdpUser> onlineUsers;

// ---------------- Helpers ----------------

string getTimestamp() {
    time_t now = time(0);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "[%Y-%m-%d %H:%M:%S]", localtime(&now));
    return string(buffer);
}

void logEvent(const string &text) {
    string line = getTimestamp() + " " + text;
    cout << line << endl;
    ofstream logFile("udp_server_log.txt", ios::app);
    if (logFile.is_open()) {
        logFile << line << endl;
    }
}

// Build the "ip:port" key for a client address
string makeKey(const sockaddr_in &addr) {
    return string(inet_ntoa(addr.sin_addr)) + ":" + to_string(ntohs(addr.sin_port));
}

// Send a message to ONE client using sendto()
void sendTo(int sock, const sockaddr_in &addr, const string &msg) {
    sendto(sock, msg.c_str(), msg.size(), 0,
           (const sockaddr *)&addr, sizeof(addr));
}

// Send a message to ALL online clients (broadcast)
void broadcastAll(int sock, const string &msg) {
    for (auto &pair : onlineUsers) {
        sendTo(sock, pair.second.address, msg);
    }
}

// Build a list of all online users with status
string getOnlineList() {
    string list = "Online users:";
    for (auto &pair : onlineUsers) {
        list += " " + pair.second.username + "(" + pair.second.status + ")";
    }
    return list;
}

// ---------------- Main ----------------

int main() {
    // 1. Create UDP socket (SOCK_DGRAM = datagram = UDP)
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket() failed");
        return 1;
    }

    // 2. Bind to port 8081
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(sock, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind() failed");
        return 1;
    }

    logEvent("UDP Chat Server started on port " + to_string(PORT));

    // 3. Receive packets forever (no accept/listen in UDP)
    char buffer[BUFFER_SIZE];
    while (true) {
        sockaddr_in clientAddr;
        socklen_t addrLen = sizeof(clientAddr);

        int n = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0,
                         (sockaddr *)&clientAddr, &addrLen);
        if (n < 0) {
            perror("recvfrom() failed");
            continue;
        }
        buffer[n] = '\0'; // make it a proper C string
        string packet(buffer);
        string key = makeKey(clientAddr);

        // Parse the first word to know the packet type
        stringstream ss(packet);
        string type;
        ss >> type;

        if (type == "JOIN") {
            // Format: JOIN <username>
            string username;
            ss >> username;
            if (username.empty()) continue;

            UdpUser user;
            user.username = username;
            user.status = "online";
            user.address = clientAddr;
            onlineUsers[key] = user;

            logEvent("User '" + username + "' joined from " + key);
            sendTo(sock, clientAddr, "SERVER: Welcome " + username + "! You are connected via UDP.");
            broadcastAll(sock, "NOTICE: " + username + " is now online."); // join notification
        }
        else if (type == "MSG") {
            // Format: MSG <seq> <text...>
            // <seq> is a sequence number used for the ACK (reliability)
            int seq;
            ss >> seq;
            string text;
            getline(ss, text); // rest of the line

            if (onlineUsers.find(key) == onlineUsers.end()) {
                sendTo(sock, clientAddr, "SERVER: Please /join first.");
                continue;
            }

            // 1) Send ACK back so the sender knows the packet arrived
            sendTo(sock, clientAddr, "ACK " + to_string(seq));

            // 2) Broadcast the message to everyone
            string username = onlineUsers[key].username;
            string msg = getTimestamp() + " " + username + ":" + text;
            broadcastAll(sock, msg);
            logEvent("Broadcast from " + username + " (seq " + to_string(seq) + ")");
        }
        else if (type == "STATUS") {
            // Format: STATUS <online/away/busy>
            string newStatus;
            ss >> newStatus;
            if (onlineUsers.find(key) == onlineUsers.end()) continue;
            if (newStatus != "online" && newStatus != "away" && newStatus != "busy") {
                sendTo(sock, clientAddr, "SERVER: Usage: /status <online/away/busy>");
                continue;
            }
            onlineUsers[key].status = newStatus;
            string username = onlineUsers[key].username;
            logEvent(username + " changed status to " + newStatus);
            // Status update notification to everyone
            broadcastAll(sock, "NOTICE: " + username + " is now " + newStatus + ".");
        }
        else if (type == "LIST") {
            sendTo(sock, clientAddr, "SERVER: " + getOnlineList());
        }
        else if (type == "LEAVE") {
            if (onlineUsers.find(key) != onlineUsers.end()) {
                string username = onlineUsers[key].username;
                onlineUsers.erase(key); // remove from online tracking
                logEvent("User '" + username + "' left.");
                broadcastAll(sock, "NOTICE: " + username + " went offline."); // leave notification
            }
        }
        else {
            sendTo(sock, clientAddr, "SERVER: Unknown packet type '" + type + "'.");
        }
    }

    close(sock);
    return 0;
}
