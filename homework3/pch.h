#ifndef PCH_H
#define PCH_H
#define NOMINMAX
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <string>
#include <ctime>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

#include "Flags.h"
#include "checksum.h"
#include "ReceiverHeader.h"
#include "LinkProperties.h"
#include "SenderDataHeader.h"
#include "SenderSynHeader.h"
#include "Packet.h"
#include "SenderSocket.h"

#pragma comment(lib,"ws2_32.lib")
#endif