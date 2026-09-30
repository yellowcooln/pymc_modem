#include "radio_config_state.h"

#include <cassert>
#include <cstdint>
#include <cstring>

int main() {
    RadioConfigState primary;
    RadioConfigState secondary;
    const RadioConfig defaults = {869618000, 62500, 8, 8, 22, 0x12, 16};
    static_assert(sizeof(RadioConfig) == 14, "wire config layout changed");
    assert(std::memcmp(&primary.config(), &defaults, sizeof(defaults)) == 0);
    assert(std::memcmp(&secondary.config(), &defaults, sizeof(defaults)) == 0);

    const uint8_t requested[sizeof(RadioConfig)] = {
        0x80, 0x0c, 0xbe, 0x33, 0x40, 0x0d, 0x03, 0x00,
        12, 5, 31, 0x34, 0x12, 9
    };
    assert(!primary.setFromWire(requested, sizeof(requested) - 1));
    assert(std::memcmp(primary.wireData(), &defaults, sizeof(defaults)) == 0);
    assert(primary.setFromWire(requested, sizeof(requested)));
    assert(std::memcmp(primary.wireData(), requested, sizeof(requested)) == 0);
    assert(std::memcmp(secondary.wireData(), &defaults, sizeof(defaults)) == 0);

    // Even a request the hardware might reject is retained for GET_CONFIG.
    uint8_t other[sizeof(requested)] = {};
    assert(secondary.setFromWire(other, sizeof(other)));
    assert(std::memcmp(secondary.wireData(), other, sizeof(other)) == 0);
    assert(std::memcmp(primary.wireData(), requested, sizeof(requested)) == 0);
}
