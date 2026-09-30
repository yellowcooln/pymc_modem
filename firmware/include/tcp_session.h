#pragma once

#include "frame_parser.h"
#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>

// One socket's parser, authorization and transport state. The listener owns
// admission policy and may currently attach only one session.
class TcpSession {
public:
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
    void write(const uint8_t* data, size_t len);

private:
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
    uint32_t localIP_ = 0;
    String token_;
    bool authenticated_ = false;
    FrameParser parser_;
    uint32_t frameCount_ = 0;
    uint32_t parsedFrameCount_ = 0;
};
