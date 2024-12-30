# Reliable UDP File Transfer Protocol

A custom implementation of a reliable file transfer protocol over UDP, featuring sliding window flow control, RTT estimation, and multi-threaded packet management. Built using C++ and Windows Socket Programming.

## Academic Context

This project was developed as part of CSCE 463 (Networks and Distributed Processing) at Texas A&M University under Professor Dmitri Loguinov. While this code is available for educational reference and demonstration purposes, please respect academic integrity if you're currently enrolled in this course.

**Course Details:**
- Course: CSCE 463 - Networks and Distributed Processing
- Reference Text: "Computer Networking: A Top-Down Approach" (6th Ed.)
- Implementation Focus: Transport Layer Protocols & Reliable Data Transfer

## Technical Features

- **Sliding Window Control**: Dynamic window sizing with cumulative ACKs and pipelining
- **RTT Estimation**: 
 - Implements Jacobson's algorithm for adaptive RTO
 - Dynamic timeout adjustment based on network conditions
 - Fast retransmit for handling packet loss
- **Multi-threaded Architecture**: 
 - Producer-consumer design for packet management
 - Windows events and semaphores for synchronization
 - Dedicated stats thread for real-time performance monitoring

## Core Components

- Reliable data transfer over unreliable UDP
- Flow control with dynamic window sizing
- Thread-safe circular buffer implementation
- Real-time throughput and RTT statistics
- Packet loss detection and recovery mechanisms

## Technologies

- C++
- Windows Socket Programming (WinSock)
- Windows Threading API
- Windows Event & Synchronization Primitives

## Building and Usage

```bash
# Build using Visual Studio 2022
# Run the program with following parameters:
sender.exe <target-host> <power> <window-size> <RTT> <loss-rate1> <loss-rate2> <bottleneck-speed>
