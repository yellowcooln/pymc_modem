#pragma once

#include "tcp_session.h"
#include <WiFi.h>

// One listening socket and one immutable radio/session binding. Main-loop
// owned: routes referring to its session must not outlive this object.
class TcpListener {
public:
    explicit TcpListener(TcpEndpointIdentity endpoint) : session_(endpoint) {}
    ~TcpListener() { end(); }
    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;

    void begin(uint16_t port, const String& token);
    void end();
    void loop();
    void invalidateInterface(const IPAddress& address);
    bool isClientReady() { return session_.isReady(); }
    TcpSession* session() { return &session_; }
    String getClientIP() { return session_.clientIP(); }
    void write(const uint8_t* data, size_t len) { session_.write(data, len); }
    void writeRadioEvent(const uint8_t* data, size_t len, uint8_t originRadio) {
        session_.writeForRadioEvent(data, len, originRadio);
    }
private:
    WiFiServer* server_ = nullptr;
    TcpSession session_;
    uint32_t acceptedCount_ = 0;
};

// A caller-owned view; it neither starts listeners nor changes radio policy.
// A second listener may be tested here without enabling it in firmware setup.
class TcpListenerBank {
public:
    TcpListenerBank(TcpListener* const* listeners, size_t count)
        : listeners_(listeners), count_(count) {}
    void loop() {
        for (size_t i = 0; i < count_; ++i)
            if (listeners_[i]) listeners_[i]->loop();
    }
    void invalidateInterface(const IPAddress& address) {
        for (size_t i = 0; i < count_; ++i)
            if (listeners_[i]) listeners_[i]->invalidateInterface(address);
    }
    void writeRadioEvent(const uint8_t* data, size_t len, uint8_t originRadio) {
        for (size_t i = 0; i < count_; ++i)
            if (listeners_[i]) listeners_[i]->writeRadioEvent(data, len, originRadio);
    }
private:
    TcpListener* const* listeners_;
    size_t count_;
};
