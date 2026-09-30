#include "tcp_session.h"
#include "bounded_protocol_service.h"
#include "protocol.h"
#include <cstring>

extern void processHostCommand(uint8_t cmd, const uint8_t* payload,
                               uint16_t len, TransportSource src,
                               const TcpEndpointIdentity& endpoint);
extern void noteTransportFrameError(uint8_t err_code);

TcpSession* TcpSession::feeding_ = nullptr;

void TcpSession::configure(const String& token) {
    disconnect();
    token_ = token;
}

bool TcpSession::requiresAuth() const { return token_.length() > 0; }
bool TcpSession::connected() { return client_ && client_.connected(); }
bool TcpSession::hasClient() { return static_cast<bool>(client_); }
bool TcpSession::isReady() { return connected() && (!requiresAuth() || authenticated_); }

String TcpSession::clientIP() {
    return connected() ? client_.remoteIP().toString() : String();
}

void TcpSession::accept(WiFiClient incoming) {
    disconnect();
    client_ = incoming;
    localIP_ = static_cast<uint32_t>(client_.localIP());
    client_.setNoDelay(true);
}

void TcpSession::disconnect() {
    if (client_) {
        Serial.printf("[TCP] disconnect client %s auth=%u frames=%lu\n",
                      client_.remoteIP().toString().c_str(),
                      authenticated_ ? 1U : 0U,
                      (unsigned long)frameCount_);
        client_.stop();
    }
    authenticated_ = false;
    frameCount_ = 0;
    localIP_ = 0;
    parser_.reset();
}

void TcpSession::invalidateInterface(const IPAddress& address) {
    if (client_ && localIP_ == static_cast<uint32_t>(address)) disconnect();
}

void TcpSession::write(const uint8_t* data, size_t len) {
    if (connected()) client_.write(data, len);
}

void TcpSession::sendFrame(uint8_t cmd, const uint8_t* payload, uint16_t len) {
    if (!connected()) return;
    uint8_t buf[MAX_FRAME_SIZE];
    uint16_t i = 0;
    buf[i++] = PROTO_SYNC;
    buf[i++] = cmd;
    buf[i++] = len & 0xFF;
    buf[i++] = (len >> 8) & 0xFF;
    if (len > 0 && payload) {
        memcpy(buf + i, payload, len);
        i += len;
    }
    const uint16_t crc = crc16_ccitt(buf + 1, 3 + len);
    buf[i++] = crc & 0xFF;
    buf[i++] = crc >> 8;
    write(buf, i);
}

void TcpSession::onFrame(uint8_t cmd, const uint8_t* payload, uint16_t len) {
    ++parsedFrameCount_;
    ++frameCount_;
    Serial.printf("[TCP] frame cmd=0x%02X len=%u auth=%u\n",
                  cmd, (unsigned)len, authenticated_ ? 1U : 0U);
    if (requiresAuth() && !authenticated_) {
        if (cmd == CMD_AUTH && len == token_.length() &&
            memcmp(payload, token_.c_str(), len) == 0) {
            authenticated_ = true;
            Serial.println("[TCP] auth OK");
            sendFrame(CMD_AUTH_OK, nullptr, 0);
        } else {
            if (cmd == CMD_AUTH) Serial.println("[TCP] auth rejected");
            const uint8_t err = ERR_UNAUTHORIZED;
            sendFrame(CMD_ERROR, &err, 1);
            delay(5);
            disconnect();
        }
        return;
    }
    if (cmd == CMD_AUTH) {
        sendFrame(CMD_AUTH_OK, nullptr, 0);
        return;
    }
    processHostCommand(cmd, payload, len, TransportSource::TCP, endpoint_);
}

void TcpSession::onError(uint8_t err) {
    Serial.printf("[TCP] frame parse error 0x%02X\n", err);
    noteTransportFrameError(err);
    sendFrame(CMD_ERROR, &err, 1);
}

void TcpSession::frameCallback(uint8_t cmd, const uint8_t* payload,
                               uint16_t len, TransportSource) {
    feeding_->onFrame(cmd, payload, len);
}
void TcpSession::errorCallback(uint8_t err, TransportSource) {
    feeding_->onError(err);
}

void TcpSession::service() {
    if (!connected()) return;
    // Preserve the original per-pass fairness limits and partial-frame state.
    serviceBoundedProtocolInput(client_, 256, 1, [this](uint8_t byte) {
        const uint32_t previous = parsedFrameCount_;
        TcpSession* prior = feeding_;
        feeding_ = this;
        frameparser_feed(parser_, byte, TransportSource::TCP,
                         frameCallback, errorCallback);
        feeding_ = prior;
        return parsedFrameCount_ != previous;
    });
}
