#include <Arduino.h>
#ifdef ARDUINO_ARCH_ESP32
#include "tcp_listener.h"
#include "net_filter.h"

void TcpListener::begin(uint16_t port, const String& token) {
    end();
    session_.configure(token);
    server_ = new WiFiServer(port);
    server_->begin();
    server_->setNoDelay(true);
}

void TcpListener::end() {
    session_.disconnect();
    if (server_) {
        server_->end();
        delete server_;
        server_ = nullptr;
    }
}

void TcpListener::invalidateInterface(const IPAddress& address) {
    session_.invalidateInterface(address);
}

void TcpListener::loop() {
    if (!server_) return;
    if (!session_.connected()) {
        if (session_.hasClient()) session_.disconnect();
        WiFiClient incoming = server_->available();
        if (incoming) {
            IPAddress addr = incoming.remoteIP();
            if (!isLanAddress(addr)) {
                Serial.printf(
                    "[TCP] rejecting non-LAN client %u.%u.%u.%u "
                    "(firmware accepts only RFC1918 / link-local / loopback)\n",
                    addr[0], addr[1], addr[2], addr[3]);
                incoming.stop();
                return;
            }
            session_.accept(incoming);
            ++acceptedCount_;
            Serial.printf("[TCP] accepted client %s (#%lu, auth=%s)\n",
                          session_.clientIP().c_str(),
                          (unsigned long)acceptedCount_,
                          session_.requiresAuth() ? "required" : "open");
        }
    }
    session_.service();
}
#endif // ARDUINO_ARCH_ESP32
