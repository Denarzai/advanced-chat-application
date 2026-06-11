// ============================================================
//  CS-3001 Advanced Chat Application - TCP Client
//  Connects to server on port 8080
//  Compile: g++ client.cpp -o client -pthread
// ============================================================

#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <cstring>
#include <unistd.h>     // close()
#include <sys/socket.h> // socket(), connect(), send(), recv()
#include <netinet/in.h> // sockaddr_in
#include <arpa/inet.h>  // inet_pton()
#include <pthread.h>

using namespace std;

#define SERVER_IP "127.0.0.1"
#define PORT 8080

bool running = true; // shared flag so both threads know when to stop

// ---------------- Base64 Encoding / Decoding ----------------
// Base64 turns binary file bytes into safe text characters
// so the file can travel inside our text-based chat protocol.

const string BASE64_CHARS =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

string base64Encode(const string &input) {
    string output;
    int val = 0;      // holds bits we have collected so far
    int bits = -6;    // how many extra bits we have
    for (unsigned char c : input) {
        val = (val << 8) + c; // add 8 new bits
        bits += 8;
        while (bits >= 0) {
            // take the top 6 bits and convert to a base64 character
            output += BASE64_CHARS[(val >> bits) & 0x3F];
            bits -= 6;
        }
    }
    // handle leftover bits
    if (bits > -6) {
        output += BASE64_CHARS[((val << 8) >> (bits + 8)) & 0x3F];
    }
    // pad with '=' so length is a multiple of 4
    while (output.size() % 4 != 0) {
        output += '=';
    }
    return output;
}

string base64Decode(const string &input) {
    string output;
    int val = 0;
    int bits = -8;
    for (unsigned char c : input) {
        if (c == '=') break; // padding means we are done
        size_t pos = BASE64_CHARS.find(c);
        if (pos == string::npos) continue; // skip invalid chars
        val = (val << 6) + (int)pos; // add 6 new bits
        bits += 6;
        if (bits >= 0) {
            // we have a full byte now
            output += (char)((val >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return output;
}

// ---------------- Socket Helpers ----------------

void sendLine(int sock, const string &msg) {
    string data = msg + "\n";
    send(sock, data.c_str(), data.size(), 0);
}

// Read one line from the server (1 byte at a time, until '\n')
bool recvLine(int sock, string &line) {
    line = "";
    char ch;
    while (true) {
        int n = recv(sock, &ch, 1, 0);
        if (n <= 0) return false;
        if (ch == '\n') break;
        line += ch;
    }
    return true;
}

// ---------------- Receiver Thread ----------------
// This thread only listens for messages from the server and prints them.

void *receiveThread(void *arg) {
    int sock = *(int *)arg;
    string line;
    while (running && recvLine(sock, line)) {
        // Check if it is an incoming file
        if (line.rfind("FILE_FROM ", 0) == 0) {
            // Format: FILE_FROM <sender> <filename> <base64data>
            stringstream ss(line);
            string keyword, sender, filename, base64data;
            ss >> keyword >> sender >> filename >> base64data;

            string fileData = base64Decode(base64data);

            // Save with a prefix so we don't overwrite our own files
            string savedName = "received_" + filename;
            ofstream outFile(savedName, ios::binary);
            if (outFile.is_open()) {
                outFile.write(fileData.c_str(), fileData.size());
                outFile.close();
                cout << ">> File received from " << sender
                     << " and saved as '" << savedName << "' ("
                     << fileData.size() << " bytes)" << endl;
            } else {
                cout << ">> Error: could not save file from " << sender << endl;
            }
        } else {
            cout << line << endl; // normal chat message
        }
    }
    running = false;
    cout << ">> Disconnected from server." << endl;
    return NULL;
}

// ---------------- File Sending ----------------
// Reads a file, encodes it to Base64 and sends it through the server.

void sendFile(int sock, const string &target, const string &path) {
    ifstream inFile(path, ios::binary);
    if (!inFile.is_open()) {
        cout << ">> Error: cannot open file '" << path << "'" << endl;
        return;
    }
    // Read whole file into a string
    stringstream buffer;
    buffer << inFile.rdbuf();
    string fileData = buffer.str();
    inFile.close();

    if (fileData.size() > 1024 * 1024) { // 1 MB limit to keep it simple
        cout << ">> Error: file too large (max 1 MB for this demo)." << endl;
        return;
    }

    // Extract only the filename from the path (remove folders)
    string filename = path;
    size_t slash = path.find_last_of("/\\");
    if (slash != string::npos) {
        filename = path.substr(slash + 1);
    }

    string encoded = base64Encode(fileData);
    sendLine(sock, "FILE " + target + " " + filename + " " + encoded);
    cout << ">> Sending file '" << filename << "' (" << fileData.size()
         << " bytes) to " << target << "..." << endl;
}

// ---------------- Main ----------------

int main() {
    // 1. Create TCP socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket() failed");
        return 1;
    }

    // 2. Connect to the server
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    if (connect(sock, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connect() failed (is the server running?)");
        return 1;
    }
    cout << ">> Connected to chat server " << SERVER_IP << ":" << PORT << endl;

    // 3. Login (authentication)
    string username, password;
    cout << "Username: ";
    getline(cin, username);
    cout << "Password: ";
    getline(cin, password);

    sendLine(sock, "LOGIN " + username + " " + password);

    string reply;
    if (!recvLine(sock, reply)) {
        cout << ">> Server closed the connection." << endl;
        close(sock);
        return 1;
    }
    cout << reply << endl;
    if (reply.rfind("LOGIN_FAIL", 0) == 0) {
        close(sock);
        return 1; // login failed, stop here
    }

    // 4. Start the receiver thread
    pthread_t tid;
    pthread_create(&tid, NULL, receiveThread, &sock);

    // 5. Main loop: read user input and send it
    string input;
    while (running && getline(cin, input)) {
        if (input.empty()) continue;

        if (input.rfind("/file ", 0) == 0) {
            // /file <user> <path> -> handled locally (read + encode file)
            stringstream ss(input);
            string command, target, path;
            ss >> command >> target >> path;
            if (target.empty() || path.empty()) {
                cout << ">> Usage: /file <user> <path>" << endl;
            } else {
                sendFile(sock, target, path);
            }
        } else {
            sendLine(sock, input); // everything else goes to the server
        }

        if (input == "/exit") {
            break;
        }
    }

    // 6. Cleanup
    running = false;
    close(sock);          // closing the socket also stops the receiver thread
    pthread_join(tid, NULL);
    cout << ">> Client closed." << endl;
    return 0;
}
