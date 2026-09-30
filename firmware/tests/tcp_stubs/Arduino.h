#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <utility>

class String {
public:
    String() = default;
    String(const char* s) : value_(s) {}
    String(std::string s) : value_(std::move(s)) {}
    size_t length() const { return value_.size(); }
    const char* c_str() const { return value_.c_str(); }
    bool operator==(const char* s) const { return value_ == s; }
private:
    std::string value_;
};

struct FakeSerial {
    std::vector<uint8_t> output;
    size_t write(const uint8_t* bytes, size_t len) {
        output.insert(output.end(), bytes, bytes + len);
        return len;
    }
    template <typename... Args> void printf(const char*, Args...) {}
    void println(const char*) {}
};
inline FakeSerial Serial;
inline void delay(unsigned) {}
