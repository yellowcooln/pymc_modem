#pragma once
#include "Arduino.h"
#include <cstdint>
#include <string>

class IPAddress {
public:
    IPAddress() : bytes_{} {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a,b,c,d} {}
    uint8_t operator[](size_t i) const { return bytes_[i]; }
    explicit operator uint32_t() const {
        return (uint32_t(bytes_[0]) << 24) | (uint32_t(bytes_[1]) << 16) |
               (uint32_t(bytes_[2]) << 8) | bytes_[3];
    }
    String toString() const {
        return std::to_string(bytes_[0]) + "." + std::to_string(bytes_[1]) + "." +
               std::to_string(bytes_[2]) + "." + std::to_string(bytes_[3]);
    }
private:
    uint8_t bytes_[4];
};
