#pragma once
#include "IPAddress.h"
#include <deque>
#include <memory>
#include <vector>

namespace fake_tcp {
struct Socket {
    IPAddress remote{192, 168, 1, 2};
    IPAddress local{192, 168, 1, 10};
    bool connected = true;
    bool stopped = false;
    bool no_delay = false;
    std::deque<uint8_t> input;
    std::vector<uint8_t> output;
};
using SocketPtr = std::shared_ptr<Socket>;
}

class WiFiClient {
public:
    WiFiClient() = default;
    explicit WiFiClient(fake_tcp::SocketPtr socket) : socket_(std::move(socket)) {}
    // Arduino-ESP32 NetworkClient::operator bool() delegates to connected().
    explicit operator bool() const { return connected(); }
    bool connected() const { return socket_ && socket_->connected; }
    int available() const { return socket_ ? static_cast<int>(socket_->input.size()) : 0; }
    int read() {
        if (!socket_ || socket_->input.empty()) return -1;
        const int byte = socket_->input.front();
        socket_->input.pop_front();
        return byte;
    }
    size_t write(const uint8_t* bytes, size_t len) {
        if (!connected()) return 0;
        socket_->output.insert(socket_->output.end(), bytes, bytes + len);
        return len;
    }
    void stop() { if (socket_) { socket_->connected = false; socket_->stopped = true; } }
    void setNoDelay(bool enabled) { if (socket_) socket_->no_delay = enabled; }
    IPAddress remoteIP() const { return socket_->remote; }
    IPAddress localIP() const { return socket_->local; }
private:
    fake_tcp::SocketPtr socket_;
};

class WiFiServer {
public:
    explicit WiFiServer(uint16_t port) : port_(port) { current = this; }
    ~WiFiServer() { if (current == this) current = nullptr; }
    void begin() { active = true; }
    void end() { active = false; }
    void setNoDelay(bool enabled) { no_delay = enabled; }
    WiFiClient available() {
        if (pending.empty()) return {};
        WiFiClient next(pending.front());
        pending.pop_front();
        return next;
    }
    uint16_t port() const { return port_; }
    static inline WiFiServer* current = nullptr;
    std::deque<fake_tcp::SocketPtr> pending;
    bool active = false;
    bool no_delay = false;
private:
    uint16_t port_;
};
