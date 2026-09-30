#pragma once

#include <stdint.h>

// Stable binding of a TCP listener's radio endpoint and session slot. This is
// command-ingress metadata only; it is not a wire-format field or reply route.
struct TcpEndpointIdentity {
    const uint8_t radio;
    const uint8_t session;
};
