#pragma once

#include <stdint.h>

// Stable binding of a TCP listener's radio endpoint and session slot. The
// radio gates unsolicited event delivery; neither field enters the wire format
// or replaces a command's generation-bound ResponseRoute.
struct TcpEndpointIdentity {
    const uint8_t radio;
    const uint8_t session;
};
