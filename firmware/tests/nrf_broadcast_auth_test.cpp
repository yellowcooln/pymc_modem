// Linked with the production main.cpp frame-output block on the nRF code path.
// W5100S write intentionally models the real raw queue: it does NOT gate auth.
#include "tcp_server.h"
#include "protocol.h"
#include <cassert>
#include <vector>

std::vector<uint8_t>& fakeUartOutput();
void broadcastFrame(uint8_t, const uint8_t*, uint16_t, uint8_t);

namespace TCPServer {
static bool ready = false;
static std::vector<uint8_t> output;
bool isClientReady() { return ready; }
void write(const uint8_t* data, size_t len) {
    output.insert(output.end(), data, data + len);
}
} // namespace TCPServer

int main() {
    const uint8_t rx[] = {0x42, 0x00, 0x9c};
    std::vector<uint8_t> expected = {PROTO_SYNC, CMD_RX_PACKET, sizeof(rx), 0};
    expected.insert(expected.end(), rx, rx + sizeof(rx));
    const uint16_t crc = crc16_ccitt(expected.data() + 1, expected.size() - 1);
    expected.push_back(static_cast<uint8_t>(crc));
    expected.push_back(static_cast<uint8_t>(crc >> 8));

    // Accepted TCP socket has not completed CMD_AUTH. RX must still reach USB/UART.
    broadcastFrame(CMD_RX_PACKET, rx, sizeof(rx), 0);
    assert(TCPServer::output.empty());
    assert(Serial.output == expected);
    assert(fakeUartOutput() == expected);

    TCPServer::ready = true;
    Serial.output.clear();
    fakeUartOutput().clear();
    broadcastFrame(CMD_RX_PACKET, rx, sizeof(rx), 0);
    assert(TCPServer::output == expected);
    assert(Serial.output == expected);
    assert(fakeUartOutput() == expected);
}
