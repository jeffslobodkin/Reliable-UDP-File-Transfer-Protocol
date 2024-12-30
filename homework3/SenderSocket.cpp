#include "pch.h"
SenderSocket::SenderSocket() :
    sock(INVALID_SOCKET),
    remote(nullptr),
    isConnected(false),
    quit(NULL),
    receive(NULL),
    complete(NULL),
    empty(NULL),
    full(NULL),
    workers(NULL),
    stats(NULL),
    windowSize(0),
    base(0),
    nextSeq(0),
    nextSend(0),
    lastReleased(0),
    effectiveWindow(0),
    packets(nullptr),
    firstDataTime(0),
    lastAckTime(0),
    numTimeouts(0),
    numRetries(0),
    estRTT(-1),
    devRTT(-1),
    RTO(0)
{
    WSADATA wsaData;
    WORD wVersionRequested = MAKEWORD(2, 2);
    if (WSAStartup(wVersionRequested, &wsaData) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        throw std::runtime_error("Socket creation failed");
    }

    quit = CreateEvent(NULL, TRUE, FALSE, NULL);
    receive = CreateEvent(NULL, FALSE, FALSE, NULL);
    complete = CreateEvent(NULL, TRUE, FALSE, NULL);


    ZeroMemory(&server, sizeof(server));
}

DWORD SenderSocket::Open(const char* targetHost, int port, int senderWindow, LinkProperties* lp) {
    if (isConnected) {
        return ALREADY_CONNECTED;
    }

    this->windowSize = senderWindow;
    effectiveWindow = 0;
    packets = new Packet[this->windowSize];
    base = 0;
    nextSeq = 0;
    nextSend = 0;
    lastReleased = 0;
    numTimeouts = 0;
    numRetries = 0;
    hasRetransmitted = false;

    empty = CreateSemaphore(NULL, 0, this->windowSize, NULL);
    full = CreateSemaphore(NULL, 0, this->windowSize, NULL);

    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    DWORD serverIP = inet_addr(targetHost);
    if (serverIP != INADDR_NONE) {
        server.sin_addr.s_addr = serverIP;
    }
    else {
        struct hostent* host;
        host = gethostbyname(targetHost);

        if (host == nullptr) {
            printf("target %s is invalid\n", targetHost);
            return INVALID_NAME;
        }
        memcpy(&server.sin_addr, host->h_addr, host->h_length);
    }

    int kernelBuf = 20e6;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (char*)&kernelBuf, sizeof(kernelBuf));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (char*)&kernelBuf, sizeof(kernelBuf));

    sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;

    if (bind(sock, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        throw std::runtime_error("Bind failed");
    }

    SenderSynHeader synPacket;
    memset(&synPacket, 0, sizeof(synPacket));
    synPacket.sdh.flags.magic = MAGIC_PROTOCOL;
    synPacket.sdh.flags.SYN = 1;
    synPacket.sdh.seq = 0;
    synPacket.lp = *lp;
    synPacket.lp.bufferSize = senderWindow + 50;

    RTO = std::max(1.0, 1.5 * lp->RTT);
    estRTT = -1;
    devRTT = -1;

    clock_t startTime = clock();
    for (int i = 0; i < 3; i++) {
        if (sendto(sock, (char*)&synPacket, sizeof(synPacket), 0,
            (sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
            continue;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);

        timeval timeout;
        timeout.tv_sec = (long)ceil(2 * lp->RTT);
        timeout.tv_usec = 0;

        if (select(0, &readSet, NULL, NULL, &timeout) > 0) {
            ReceiverHeader rh;
            int fromLen = sizeof(server);

            if (recvfrom(sock, (char*)&rh, sizeof(rh), 0, (sockaddr*)&server, &fromLen) != SOCKET_ERROR) {
                if (rh.flags.SYN && rh.flags.ACK) {
                    if (i == 0) {
                        estRTT = (double)(clock() - startTime) / CLOCKS_PER_SEC;
                        RTO = estRTT + 4 * 0.010;
                    }

                    isConnected = true;
                    effectiveWindow = std::min(windowSize, (int)rh.recvWnd);
                    lastReleased = std::min(windowSize, (int)rh.recvWnd);
                    ReleaseSemaphore(empty, lastReleased, NULL);

                    WSAEventSelect(sock, receive, FD_READ);

                    stats = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&PrintStats, this, 0, NULL);
                    workers = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)&WorkerRun, this, 0, NULL);

                    return STATUS_OK;
                }
            }
        }
    }
    return TIMEOUT;
}

DWORD SenderSocket::Send(char* buffer, int bytes) {
    HANDLE arr[] = { quit, empty };
    DWORD result = WaitForMultipleObjects(2, arr, FALSE, INFINITE);

    if (result == WAIT_OBJECT_0) {
        return TIMEOUT;
    }

    int slot = nextSeq % windowSize;
    Packet* p = packets + slot;

    SenderDataHeader* sdh = (SenderDataHeader*)p->pkt;
    sdh->seq = nextSeq;
    sdh->flags.ACK = 0;
    sdh->flags.SYN = 0;
    sdh->flags.FIN = 0;
    sdh->flags.reserved = 0;
    sdh->flags.magic = MAGIC_PROTOCOL;

    memcpy(sdh + 1, buffer, bytes);
    p->size = bytes + sizeof(SenderDataHeader);
    p->type = 0;

    nextSeq++;
    if (firstDataTime == 0) {
        firstDataTime = GetTimeStamp();
    }
    ReleaseSemaphore(full, 1, NULL);

    return STATUS_OK;
}
DWORD WINAPI SenderSocket::WorkerRun(LPVOID self) {
    SenderSocket* s = (SenderSocket*)self;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    HANDLE events[] = { s->receive, s->full, s->quit };
    bool complete = false;
    int dup = 0;
    int rtx = 0;
    int retxCount;
    while (!complete || (complete && s->base < s->nextSend)) {
        DWORD timeout = INFINITE;
        if (s->nextSend > s->base) {
            ULONGLONG timerExpire = s->packets[s->base % s->windowSize].txTime + (ULONGLONG)(s->RTO * CLOCKS_PER_SEC);
            ULONGLONG cur_time = clock();

            if (timerExpire > cur_time) {
                ULONGLONG diff = timerExpire - cur_time;
                if (diff > INFINITE) {
                    timeout = INFINITE;
                }
                else {
                    timeout = (DWORD)(1000 * diff / CLOCKS_PER_SEC);
                }
            }
            else {
                timeout = 0;
            }
        }

        int ret = WaitForMultipleObjects(3, events, false, timeout);
        switch (ret) {
        case WAIT_OBJECT_0:
            s->HandleACK(&dup, &rtx);
            break;

        case WAIT_OBJECT_0 + 1:
            if (s->nextSend < s->base + s->effectiveWindow) {
                if (sendto(s->sock, s->packets[s->nextSend % s->windowSize].pkt,
                    s->packets[s->nextSend % s->windowSize].size,
                    0, (struct sockaddr*)&(s->server), sizeof(s->server)) != SOCKET_ERROR) {
                    s->packets[s->nextSend % s->windowSize].txTime = clock();
                    s->nextSend++;
                }
            }
            break;

        case WAIT_OBJECT_0 + 2:
            complete = true;
            break;

        case WAIT_TIMEOUT:


            if (sendto(s->sock, s->packets[s->base % s->windowSize].pkt,
                s->packets[s->base % s->windowSize].size,
                0, (struct sockaddr*)&(s->server), sizeof(s->server)) != SOCKET_ERROR) {
                InterlockedIncrement(&(s->numTimeouts));
                s->packets[s->base % s->windowSize].txTime = clock();
                rtx++;
            }
            break;
        }
    }
    return STATUS_OK;
}

void SenderSocket::HandleACK(int* dup, int* rtx) {
    ReceiverHeader rh;
    struct sockaddr_in res;
    int size = sizeof(res);

    if (recvfrom(sock, (char*)&rh, sizeof(ReceiverHeader), 0, (struct sockaddr*)&res, &size) == SOCKET_ERROR) {
        return;
    }

    int y = rh.ackSeq;
    if (y > base) {
        lastAckTime = GetTimeStamp();

        if (*rtx == 0) {
            ULONGLONG currentTime = clock();
            ULONGLONG packetTime = packets[(y - 1) % windowSize].txTime;
            if (currentTime >= packetTime) {
                double sampleRTT = (currentTime - packetTime) / (double)CLOCKS_PER_SEC;
                if (sampleRTT > 0) {
                    if (estRTT == -1) {
                        estRTT = sampleRTT;
                        devRTT = sampleRTT / 2.0;
                    }
                    else {
                        double error = sampleRTT - estRTT;
                        estRTT = (1 - alpha) * estRTT + alpha * sampleRTT;
                        devRTT = (1 - beta) * devRTT + beta * fabs(error);
                        RTO = estRTT + 4 * std::max(0.010, devRTT);
                    }
                }
            }
        }

        base = y;
        hasRetransmitted = false;
        *dup = 0;
        *rtx = 0;

        effectiveWindow = std::min(windowSize, (int)rh.recvWnd);
        int newReleased = base + effectiveWindow - lastReleased;
        if (newReleased > 0) {
            ReleaseSemaphore(empty, newReleased, NULL);
            lastReleased += newReleased;
        }
    }
    else if (y == base && base < nextSend) {
        (*dup)++;
        if (*dup == 3) {
            if (!hasRetransmitted) {
                if (sendto(sock, packets[base % windowSize].pkt, packets[base % windowSize].size, 0, (struct sockaddr*)&server, sizeof(server)) != SOCKET_ERROR) {
                    InterlockedIncrement(&numRetries);
                    packets[base % windowSize].txTime = clock();
                    hasRetransmitted = true;
                }
            }

        }
    }
}

DWORD SenderSocket::Close(double* elapsedTime) {
    if (!isConnected) {
        return NOT_CONNECTED;
    }

    while (base < nextSeq) {
        Sleep(10);
    }

    if (elapsedTime) {
        *elapsedTime = (lastAckTime - firstDataTime) / 1000.0;
    }

    SetEvent(quit);
    WaitForSingleObject(workers, INFINITE);
    CloseHandle(workers);

    SetEvent(complete);
    WaitForSingleObject(stats, INFINITE);
    CloseHandle(stats);

    SenderDataHeader* finHeader = new SenderDataHeader();
    finHeader->flags.ACK = 0;
    finHeader->flags.FIN = 1;
    finHeader->flags.reserved = 0;
    finHeader->flags.SYN = 0;
    finHeader->seq = std::max((int)this->base, 0);
    finHeader->flags.magic = MAGIC_PROTOCOL;

    for (int i = 0; i < 50; i++) {
        if (sendto(sock, (char*)finHeader, sizeof(SenderDataHeader), 0,
            (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR) {
            delete finHeader;
            return FAILED_SEND;
        }

        fd_set fdRead;
        FD_ZERO(&fdRead);
        FD_SET(sock, &fdRead);

        timeval TO;
        TO.tv_sec = 1;
        TO.tv_usec = 0;

        if (select(0, &fdRead, NULL, NULL, &TO) > 0) {
            ReceiverHeader rh;
            int size = sizeof(struct sockaddr_in);

            if (recvfrom(sock, (char*)&rh, sizeof(ReceiverHeader), 0,
                (struct sockaddr*)&server, &size) != SOCKET_ERROR) {

                if (rh.flags.ACK && rh.flags.FIN) {
                    printf("[%.3f] <-- FIN-ACK %d window %X\n", *elapsedTime, rh.ackSeq, rh.recvWnd);
                    return STATUS_OK;
                }
            }
        }
    }

    delete finHeader;
    return TIMEOUT;
}

DWORD WINAPI SenderSocket::PrintStats(LPVOID self) {
    SenderSocket* s = (SenderSocket*)self;

    ULONGLONG startTime = GetTickCount64();
    ULONGLONG prevTime = startTime;
    DWORD prevBase = s->base;
    Sleep(2000);

    while (WaitForSingleObject(s->complete, 0) == WAIT_TIMEOUT) {
        ULONGLONG currentTime = GetTickCount64();
        double elapsedSeconds = (currentTime - startTime) / 1000.0;

        if (elapsedSeconds >= floor(elapsedSeconds / 2.0) * 2.0) {
            double mbTransferred = (s->base * (MAX_PKT_SIZE - sizeof(SenderDataHeader))) / (1024.0 * 1024.0);

            double intervalSeconds = (currentTime - prevTime) / 1000.0;
            double bytesInInterval = (s->base - prevBase) * (MAX_PKT_SIZE - sizeof(SenderDataHeader));
            double speed = (bytesInInterval * 8.0) / (intervalSeconds * 1000000.0);

            printf("[%2.0f] B %d ( %.1f MB) N %d T %d F %d W %d S %.3f Mbps RTT %.3f\n",
                elapsedSeconds,
                s->base,
                mbTransferred,
                s->nextSeq,
                s->numTimeouts,
                s->numRetries,
                s->windowSize,
                speed,
                s->estRTT);

            prevTime = currentTime;
            prevBase = s->base;
        }

        Sleep(2000);
    }
    return 0;
}


SenderSocket::~SenderSocket() {
    if (quit) CloseHandle(quit);
    if (receive) CloseHandle(receive);
    if (complete) CloseHandle(complete);
    if (empty) CloseHandle(empty);
    if (full) CloseHandle(full);

    if (packets) delete[] packets;

    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    WSACleanup();
}