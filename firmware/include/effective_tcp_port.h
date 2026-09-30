#pragma once

#include <cstdint>

#if defined(BOARD_ETHERMESH_DUO)
inline constexpr bool duoDiagnosticTcpPort = true;
#else
inline constexpr bool duoDiagnosticTcpPort = false;
#endif

// Duo diagnostics intentionally ignore an older persisted TCP port. Other
// boards retain their saved port, including future radio-less targets.
inline constexpr uint16_t effectiveTcpPort(uint16_t savedPort) {
#if defined(BOARD_ETHERMESH_DUO)
    (void)savedPort;
    return 5055;
#else
    return savedPort;
#endif
}

inline constexpr bool acceptsTcpPort(uint16_t requestedPort) {
#if defined(BOARD_ETHERMESH_DUO)
    return requestedPort == 5055;
#else
    (void)requestedPort;
    return true;
#endif
}
