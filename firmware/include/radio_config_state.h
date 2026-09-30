#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "protocol.h"

// Configuration for one radio. Keep the wire struct unmodified; the current
// single-radio firmware owns exactly one instance of this state.
class RadioConfigState {
public:
    const RadioConfig& config() const { return config_; }
    const uint8_t* wireData() const {
        return reinterpret_cast<const uint8_t*>(&config_);
    }

    // The command stores the requested bytes before hardware apply. A failed
    // apply must not roll the request back (legacy GET_CONFIG behavior).
    bool setFromWire(const uint8_t* payload, size_t len) {
        if (!payload || len != sizeof(config_)) return false;
        std::memcpy(&config_, payload, sizeof(config_));
        return true;
    }

private:
    RadioConfig config_ = {
        869618000, 62500, 8, 8, 22, 0x12, 16
    };
};
