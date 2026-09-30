// Production main.cpp encoder/broadcast and W5100S requiresAuth/isClientReady
// are linked into this host test. Only socket/IP state and raw queue are fake.
#include "tcp_server.h"
#include "protocol.h"
#include <cassert>
#include <vector>

std::vector<uint8_t>& fakeUartOutput();
void broadcastFrame(uint8_t, const uint8_t*, uint16_t, uint8_t);

namespace EthernetManager {
bool ip = true;
bool hasIP() { return ip; }
}
namespace TCPServer {
// Keep these shim class definitions identical to the extraction harness.
struct FakeClient { explicit operator bool() const; };
struct FakeSocketReader { bool connected() const; };
bool accepted = true;
bool connected = true;
FakeClient::operator bool() const { return accepted; }
bool FakeSocketReader::connected() const { return TCPServer::connected; }
FakeClient client;
FakeSocketReader socketReader;
String requiredToken("secret");
bool authenticated = false;
std::vector<uint8_t> output;
// The production W5100S write() queues without checking authentication.
void write(const uint8_t* data, size_t len) {
    output.insert(output.end(), data, data + len);
}
}

int main() {
    const uint8_t rx[] = {0x42, 0x00, 0x9c};
    std::vector<uint8_t> expected = {PROTO_SYNC, CMD_RX_PACKET, sizeof(rx), 0};
    expected.insert(expected.end(), rx, rx + sizeof(rx));
    const uint16_t crc = crc16_ccitt(expected.data() + 1, expected.size() - 1);
    expected.push_back(static_cast<uint8_t>(crc));
    expected.push_back(static_cast<uint8_t>(crc >> 8));

    auto check = [&](bool tcp) {
        TCPServer::output.clear();
        Serial.output.clear();
        fakeUartOutput().clear();
        assert(TCPServer::isClientReady() == tcp);
        broadcastFrame(CMD_RX_PACKET, rx, sizeof(rx), 0);
        assert(TCPServer::output == (tcp ? expected : std::vector<uint8_t>{}));
        assert(Serial.output == expected);
        assert(fakeUartOutput() == expected);
    };

    // Accepted socket, configured token, no CMD_AUTH yet.
    check(false);
    TCPServer::authenticated = true; // successful CMD_AUTH transition
    check(true);
    TCPServer::connected = false;
    check(false);
    TCPServer::connected = true;
    EthernetManager::ip = false;
    check(false);
    EthernetManager::ip = true;
    TCPServer::accepted = false;
    check(false);
    TCPServer::accepted = true;
    TCPServer::authenticated = false;
    TCPServer::requiredToken = String();
    check(true); // no token: connected client is ready without CMD_AUTH
    TCPServer::connected = false;
    check(false);
    TCPServer::connected = true;
    EthernetManager::ip = false;
    check(false);
}
