#pragma once

#include "frame_parser.h"

class TcpSession;

// A command's return path, fixed at ingress. No mutable 'current TCP' lookup.
// The TCP pointer belongs to the listener and must only be used in the
// cooperative main loop while that session remains connected/authorized.
struct ResponseRoute {
    TransportSource source;
    TcpSession* tcp = nullptr;
    uint32_t tcpGeneration = 0;
};
