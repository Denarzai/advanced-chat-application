// ============================================================
//  CS-3001 Advanced Chat Application - UDP Client
//  Talks to server on port 8081
//  Compile: g++ client.cpp -o client -pthread
//
//  UDP does NOT guarantee delivery, so this client adds a simple
//  reliability layer: every chat message gets a sequence number,
//  and the client waits for an ACK from the server. If no ACK
//  arrives within 1 second, the message is sent again (max 3 tries).
// ============================================================

#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <cstdlib>      // atoi()
#include <unistd.h>     // close(), usleep()
#include <sys/socket.h> // socket(), sendto(), recvfrom()
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

using namespace std;

#define SERVER_IP "127.0.0.1"
#define PORT 8081
#define BUFFER_SIZE 2048
#define MAX_RETRIES 3
#define ACK_TIMEOUT_MS 1000 // wait 1 second for ACK

bool running = true;
int lastAckedSeq = -1; // updated by the receiver thread when an ACK arrives
pthread_mutex_t ackLock = PTHREAD_MUTEX_INITIALIZER;

int sock;                 // the UDP socket (shared by both threads)
sockaddr_in serverAddr;   // server address (used by sendto)

// ---------------- Send helper ----------------

void sendPacket(const string &msg) {
    sendto(sock, msg.c_str(), msg.size(), 0,
           (const sockaddr *)&serverAddr, sizeof(serverAddr));
}

// ---------------- Receiver Thread ----------------
// Listens for packets from the server. ACK packets update
// lastAckedSeq; everything else is printed to the screen.

void *receiveThread(void *arg) {
    char buffer[BUFFER_SIZE];
    while (running) {
        sockaddr_in fromAddr;
        socklen_t addrLen = sizeof(fromAddr);
        int n = recvfrom(sock, buffer, BUFFER_SIZE - 1, 0,
                         (sockaddr *)&fromAddr, &addrLen);
        if (n < 0) continue;  // timeout (SO_RCVTIMEO) -> check 'running' again
        if (n == 0) break;
        buffer[n] = '\0';
        string packet(buffer);

        if (packet.rfind("ACK ", 0) == 0) {
            // ACK <seq> -> record it so the sender stops retrying
            int seq = atoi(packet.substr(4).c_str());
            pthread_mutex_lock(&ackLock);
            lastAckedSeq = seq;
            pthread_mutex_unlock(&ackLock);
        } else {
            cout << packet << endl; // chat message or notification
        }
    }
    return NULL;
}

// ---------------- Reliable Send ----------------
// Sends "MSG <seq> <text>" and waits for "ACK <seq>".
// Retries up to MAX_RETRIES times if the ACK is lost.

void reliableSend(int seq, const string &text) {
    string packet = "MSG " + to_string(seq) + " " + text;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        sendPacket(packet);

        // Wait up to 1 second, checking every 50 ms if the ACK arrived
        int waited = 0;
        while (waited < ACK_TIMEOUT_MS) {
            pthread_mutex_lock(&ackLock);
            bool acked = (lastAckedSeq >= seq);
            pthread_mutex_unlock(&ackLock);
            if (acked) return; // success!
            usleep(50 * 1000); // sleep 50 ms
            waited += 50;
        }
        if (attempt < MAX_RETRIES) {
            cout << ">> No ACK for message " << seq
                 << ", retrying (attempt " << attempt + 1 << ")..." << endl;
        }
    }
    cout << ">> Message " << seq << " failed after "
         << MAX_RETRIES << " attempts (packet loss)." << endl;
}

// ---------------- Main ----------------

int main() {
    // 1. Create UDP socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket() failed");
        return 1;
    }

    // Set a 500 ms receive timeout. This way the receiver thread
    // wakes up regularly and can stop when 'running' becomes false.
    timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500 * 1000; // 500 ms
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 2. Set up the server address (no connect() needed for UDP)
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    // 3. Ask for username and join
    string username;
    cout << "Username: ";
    getline(cin, username);
    if (username.empty()) username = "guest";

    sendPacket("JOIN " + username);
    cout << ">> Joining UDP chat on " << SERVER_IP << ":" << PORT << endl;
    cout << ">> Commands: /status <online/away/busy> | /list | /help | /exit" << endl;
    cout << ">> Anything else you type is broadcast to everyone." << endl;

    // 4. Start receiver thread
    pthread_t tid;
    pthread_create(&tid, NULL, receiveThread, NULL);

    // 5. Main input loop
    int seqNumber = 0; // sequence number for reliability
    string input;
    while (running && getline(cin, input)) {
        if (input.empty()) continue;

        if (input == "/exit") {
            sendPacket("LEAVE");
            break;
        }
        else if (input == "/help") {
            cout << ">> /status <online/away/busy> | /list | /help | /exit" << endl;
            cout << ">> Any other text = broadcast message to all users." << endl;
        }
        else if (input == "/list") {
            sendPacket("LIST");
        }
        else if (input.rfind("/status ", 0) == 0) {
            // /status away  ->  STATUS away
            string newStatus = input.substr(8);
            sendPacket("STATUS " + newStatus);
        }
        else if (input[0] == '/') {
            cout << ">> Unknown command. Type /help." << endl;
        }
        else {
            // Normal broadcast message, sent reliably with ACK + retry
            seqNumber++;
            reliableSend(seqNumber, input);
        }
    }

    // 6. Cleanup
    running = false;
    close(sock); // unblocks recvfrom in the receiver thread
    pthread_join(tid, NULL);
    cout << ">> UDP client closed." << endl;
    return 0;
}
