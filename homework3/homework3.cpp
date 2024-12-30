#include "pch.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 8) {
        printf("Incorrect number of arguments");
        return -1;
    }

    char* targetHost = argv[1];
    int power = atoi(argv[2]);
    int senderWindow = atoi(argv[3]);

    LinkProperties lp;
    lp.RTT = atof(argv[4]);
    lp.speed = 1e6 * atof(argv[7]);
    lp.pLoss[FORWARD_PATH] = atof(argv[5]);
    lp.pLoss[RETURN_PATH] = atof(argv[6]);
    lp.bufferSize = senderWindow + 50;

    printf("Main: sender W = %d, RTT %.3f sec, loss %g / %g, link %g Mbps\n", senderWindow, lp.RTT, lp.pLoss[FORWARD_PATH], lp.pLoss[RETURN_PATH], atof(argv[7]));

    printf("Main: initializing DWORD array with 2^%d elements... ", power);
    clock_t start = clock();
    UINT64 dwordBufSize = (UINT64)1 << power;
    DWORD* dwordBuf = new DWORD[dwordBufSize];
    for (UINT64 i = 0; i < dwordBufSize; i++) {
        dwordBuf[i] = i;
    }
    printf("done in %d ms\n", (clock() - start) * 1000 / CLOCKS_PER_SEC);

    Checksum cs;
    UINT64 byteBufferSize = dwordBufSize << 2;
    DWORD checksum = cs.CRC32((unsigned char*)dwordBuf, byteBufferSize);

    SenderSocket* ss = new SenderSocket();
    clock_t connectStart = clock();
    int status;
    if ((status = ss->Open(targetHost, MAGIC_PORT, senderWindow, &lp)) != STATUS_OK) {
        printf("Main: connect failed with status %d\n", status);
        delete[] dwordBuf;
        delete ss;
        return -1;
    }
    printf("Main: connected to %s in %.3f sec, pkt size %d bytes\n",
        targetHost,
        (float)(clock() - connectStart) / CLOCKS_PER_SEC,
        MAX_PKT_SIZE);

    clock_t transferStart = clock();
    char* charBuf = (char*)dwordBuf;
    UINT64 off = 0;
    while (off < byteBufferSize) {
        int bytes = std::min(byteBufferSize - off, (UINT64)(MAX_PKT_SIZE - sizeof(SenderDataHeader)));
        if ((status = ss->Send(charBuf + off, bytes)) != STATUS_OK) {
            printf("Main: send failed with status %d\n", status);
            delete[] dwordBuf;
            delete ss;
            return -1;
        }
        off += bytes;
    }

    double timeElapsed = (double)(clock() - transferStart) / CLOCKS_PER_SEC;
    if ((status = ss->Close(&timeElapsed)) != STATUS_OK) {
        printf("Main: close failed with status %d\n", status);
        delete[] dwordBuf;
        delete ss;
        return -1;
    }

    double kbps = (byteBufferSize * 8.0) / (timeElapsed * 1000.0);
    double idealKbps = (8.0 * (MAX_PKT_SIZE - sizeof(SenderDataHeader)) * senderWindow) / (lp.RTT * 1000.0);
    printf("Main: transfer finished in %.3f sec, %.2f Kbps, checksum %X\n", timeElapsed, kbps, checksum);
    printf("Main: estRTT %.3f, ideal rate %.2f Kbps\n", ss->estRTT, idealKbps);

    delete[] dwordBuf;
    delete ss;
    return 0;
}