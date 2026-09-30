// Single-client listener. Socket parser/auth state lives in TcpSession so a
// future second listener can own a separate instance without shared state.
#include "tcp_server.h"
#include "tcp_session.h"
#include "net_filter.h"

namespace TCPServer {

static WiFiServer* server = nullptr;
static TcpSession session(TcpEndpointIdentity{0, 0});
static uint32_t acceptedCount = 0;

void begin(uint16_t port, const String& token) {
    end();
    session.configure(token);
    server = new WiFiServer(port);
    server->begin();
    server->setNoDelay(true);
}

void end() {
    session.disconnect();
    if (server) {
        server->end();
        delete server;
        server = nullptr;
    }
}

void invalidateInterface(const IPAddress& address) {
    session.invalidateInterface(address);
}

void loop() {
    if (!server) return;
    if (!session.connected()) {
        if (session.hasClient()) session.disconnect();
        WiFiClient incoming = server->available();
        if (incoming) {
            // Reject non-LAN clients before attaching a parser or auth state.
            IPAddress addr = incoming.remoteIP();
            if (!isLanAddress(addr)) {
                Serial.printf(
                    "[TCP] rejecting non-LAN client %u.%u.%u.%u "
                    "(firmware accepts only RFC1918 / link-local / loopback)\n",
                    addr[0], addr[1], addr[2], addr[3]);
                incoming.stop();
                return;
            }
            session.accept(incoming);
            ++acceptedCount;
            Serial.printf("[TCP] accepted client %s (#%lu, auth=%s)\n",
                          session.clientIP().c_str(),
                          (unsigned long)acceptedCount,
                          session.requiresAuth() ? "required" : "open");
        }
    }
    session.service();
}

bool isClientReady() { return session.isReady(); }
String getClientIP() { return session.clientIP(); }
void write(const uint8_t* data, size_t len) { session.write(data, len); }

} // namespace TCPServer
