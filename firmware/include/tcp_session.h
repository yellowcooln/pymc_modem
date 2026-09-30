#pragma once

#include "frame_parser.h"
#include "response_route.h"
#include "tcp_endpoint_identity.h"
#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>

// One socket's parser, authorization and transport state. Each listener owns
// a separate session and attaches at most one client at a time.
class TcpSession {
public:
    explicit TcpSession(TcpEndpointIdentity endpoint) : endpoint_(endpoint) {}
    void configure(const String& token);
    void accept(WiFiClient incoming);
    void disconnect();
    void invalidateInterface(const IPAddress& address);
    void service();
    bool isReady();
    bool connected();
    bool hasClient();
    bool requiresAuth() const;
    String clientIP();
    const TcpEndpointIdentity& endpoint() const { return endpoint_; }
    ResponseRoute responseRoute() { return {TransportSource::TCP, this, generation_}; }
    void writeForRoute(const uint8_t* data, size_t len, uint32_t generation);
    void writeForRadioEvent(const uint8_t* data, size_t len, uint8_t originRadio);
    void write(const uint8_t* data, size_t len);

private:
    const TcpEndpointIdentity endpoint_;
    void sendFrame(uint8_t cmd, const uint8_t* payload, uint16_t len);
    void onFrame(uint8_t cmd, const uint8_t* payload, uint16_t len);
    void onError(uint8_t err);
    static void frameCallback(uint8_t cmd, const uint8_t* payload,
                              uint16_t len, TransportSource src);
    static void errorCallback(uint8_t err, TransportSource src);
    // FrameParser's callback ABI has no context argument. Only the cooperative
    // main-loop feed binds this pointer, restoring it after each byte.
    static TcpSession* feeding_;

    WiFiClient client_;
    uint32_t generation_ = 0;
    uint32_t localIP_ = 0;
    String token_;
    bool authenticated_ = false;
    FrameParser parser_;
    uint32_t frameCount_ = 0;
    uint32_t parsedFrameCount_ = 0;
};

// Bounded cooperative fan-out; caller owns the sessions. The active listener
// still supplies only one entry and retains its single-client admission.
void writeRadioEventToSessions(TcpSession* const* sessions, size_t count,
                               const uint8_t* data, size_t len, uint8_t originRadio);
