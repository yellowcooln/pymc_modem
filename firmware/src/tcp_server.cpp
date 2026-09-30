// Legacy single-listener API. RF2 remains dormant until hardware and command
// dispatch are independently owned; no second port is started here.
#include "tcp_server.h"
#include "tcp_listener.h"

namespace TCPServer {

static TcpListener primary(TcpEndpointIdentity{0, 0});

void begin(uint16_t port, const String& token) { primary.begin(port, token); }
void end() { primary.end(); }
void invalidateInterface(const IPAddress& address) { primary.invalidateInterface(address); }
void loop() { primary.loop(); }
bool isClientReady() { return primary.isClientReady(); }
String getClientIP() { return primary.getClientIP(); }
void write(const uint8_t* data, size_t len) { primary.write(data, len); }
void writeRadioEvent(const uint8_t* data, size_t len, uint8_t originRadio) {
    primary.writeRadioEvent(data, len, originRadio);
}

} // namespace TCPServer
