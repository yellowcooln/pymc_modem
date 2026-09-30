#pragma once

#include "radio_config_state.h"
#include "radio_runtime_state.h"

// Explicit command owner. The secondary radio has no live listener or hardware
// command path yet; never bind it to the primary RadioLib instance by default.
struct RadioCommandContext {
    uint8_t radioId;
    // Expected TCP session slot, independent of the radio ID. Legacy
    // transports have no session and may only use the primary owner.
    uint8_t sessionId;
    RadioConfigState& config;
    RadioRuntimeState& runtime;
    StatusResp& status;
};
