#pragma once
#ifndef SENDERSOCKET_H
#define SENDERSOCKET_H

#define MAGIC_PORT 22345

#define STATUS_OK 0
#define ALREADY_CONNECTED 1
#define NOT_CONNECTED 2
#define INVALID_NAME 3
#define FAILED_SEND 4
#define TIMEOUT 5
#define FAILED_RECV 6

const double alpha = 0.125;
const double beta = 0.25;
const int MAX_RETX = 50;
class SenderSocket {
private:
    SOCKET sock;
    sockaddr_in server;
    sockaddr_in local;
    hostent* remote;
    bool isConnected;

    int windowSize;
    int effectiveWindow;
    DWORD base;
    DWORD nextSeq;
    DWORD nextSend;
    DWORD lastReleased;
    DWORD firstDataTime;
    DWORD lastAckTime;

    Packet* packets;

    HANDLE workers;
    HANDLE stats;
    HANDLE quit;
    HANDLE receive;
    HANDLE complete;
    HANDLE empty;
    HANDLE full;

    LONG numTimeouts;
    LONG numRetries;
    double devRTT;
    double RTO;
    long timeout;
    bool hasRetransmitted;

    static DWORD WINAPI WorkerRun(LPVOID);
    static DWORD WINAPI PrintStats(LPVOID);
    void HandleACK(int* dup, int* rtx);
public:
    SenderSocket();
    ~SenderSocket();
    double estRTT;

    DWORD Open(const char* targetHost, int port, int senderWindow, LinkProperties* lp);
    DWORD Send(char* buffer, int bytes);
    DWORD Close(double* timeElapsed);
    double GetTimeStamp() {
        auto now = std::chrono::steady_clock::now();
        auto duration = now.time_since_epoch();
        auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(duration);
        return static_cast<double>(microseconds.count()) / 1000.0;
    }
};

#endif