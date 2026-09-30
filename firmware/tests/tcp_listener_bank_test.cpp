#include "tcp_listener.h"
#include "protocol.h"
#include "response_route.h"
#include <WiFi.h>
#include <cassert>
#include <cstdio>
#include <vector>

struct CommandRecord { uint8_t command; ResponseRoute route; };
static std::vector<CommandRecord> commands;
static std::vector<uint8_t> errors;
void processHostCommand(uint8_t command, const uint8_t*, uint16_t, ResponseRoute route) {
    commands.push_back({command, route});
}
void noteTransportFrameError(uint8_t error) { errors.push_back(error); }

static std::vector<uint8_t> frame(uint8_t command, std::vector<uint8_t> payload = {}) {
    std::vector<uint8_t> bytes{PROTO_SYNC, command, static_cast<uint8_t>(payload.size()),
                               static_cast<uint8_t>(payload.size() >> 8)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const auto crc = crc16_ccitt(bytes.data() + 1, bytes.size() - 1);
    bytes.push_back(static_cast<uint8_t>(crc)); bytes.push_back(static_cast<uint8_t>(crc >> 8));
    return bytes;
}
static void feed(const fake_tcp::SocketPtr& socket, const std::vector<uint8_t>& bytes) {
    socket->input.insert(socket->input.end(), bytes.begin(), bytes.end());
}
static fake_tcp::SocketPtr incoming(WiFiServer* server, IPAddress local = {192,168,1,10}) {
    auto socket = std::make_shared<fake_tcp::Socket>();
    socket->local = local;
    server->pending.push_back(socket);
    return socket;
}
static void expect(const fake_tcp::SocketPtr& socket, const std::vector<uint8_t>& bytes) {
    assert(socket->output == bytes);
    socket->output.clear();
}
int main() {
    TcpListener rf1({0, 0}), rf2({1, 1});
    TcpListener* listeners[] = {&rf1, &rf2};
    TcpListenerBank bank(listeners, 2);
    rf1.begin(5055, String("alpha"));
    rf2.begin(5056, String("bravo"));
    auto* server1 = WiFiServer::forPort(5055);
    auto* server2 = WiFiServer::forPort(5056);
    assert(server1 && server2 && server1 != server2 && server1->active && server2->active);
    auto a = incoming(server1);
    auto b = incoming(server2, {10,0,0,10});
    feed(a, frame(CMD_AUTH, {'a','l','p','h','a'}));
    feed(b, frame(CMD_AUTH, {'a','l','p','h','a'}));
    bank.loop();
    expect(a, frame(CMD_AUTH_OK));
    expect(b, frame(CMD_ERROR, {ERR_UNAUTHORIZED}));
    assert(rf1.isClientReady() && !rf2.isClientReady() && b->stopped);
    b = incoming(server2, {10,0,0,10});
    feed(b, frame(CMD_AUTH, {'b','r','a','v','o'}));
    bank.loop();
    expect(b, frame(CMD_AUTH_OK));
    assert(rf2.isClientReady());
    // Each listener admits only LAN clients and services one frame per pass.
    auto extra = incoming(server2);
    feed(extra, frame(CMD_PING));
    auto publicClient = incoming(server1);
    publicClient->remote = {8,8,8,8};
    feed(publicClient, frame(CMD_PING));
    bank.loop();
    assert(!extra->stopped && extra->input.size() == frame(CMD_PING).size());
    assert(!publicClient->stopped); // both existing owners retain their sockets
    assert(commands.empty());
    server1->pending.clear(); server2->pending.clear();
    feed(a, frame(CMD_PING)); feed(b, frame(CMD_GET_CONFIG));
    bank.loop();
    assert(commands.size() == 2);
    assert(commands[0].command == CMD_PING && commands[0].route.tcp->endpoint().radio == 0);
    assert(commands[1].command == CMD_GET_CONFIG && commands[1].route.tcp->endpoint().radio == 1);
    const auto reply = frame(CMD_PONG);
    commands[1].route.tcp->writeForRoute(reply.data(), reply.size(), commands[1].route.tcpGeneration);
    expect(b, reply); assert(a->output.empty());
    commands[0].route.tcp->writeForRoute(reply.data(), reply.size(), commands[0].route.tcpGeneration);
    expect(a, reply); assert(b->output.empty());
    const auto event = frame(CMD_RX_PACKET, {0x42});
    bank.writeRadioEvent(event.data(), event.size(), 0);
    expect(a, event); assert(b->output.empty());
    bank.writeRadioEvent(event.data(), event.size(), 1);
    expect(b, event); assert(a->output.empty());
    auto corrupt = frame(CMD_PING); corrupt.back() ^= 1;
    feed(b, corrupt); bank.loop();
    expect(b, frame(CMD_ERROR, {ERR_CRC_MISMATCH}));
    assert(errors == std::vector<uint8_t>{ERR_CRC_MISMATCH} && a->output.empty());
    const auto stale = commands[1].route;
    b->connected = false;
    auto replacement = incoming(server2, {10,0,0,10});
    feed(replacement, frame(CMD_PING)); bank.loop();
    expect(replacement, frame(CMD_ERROR, {ERR_UNAUTHORIZED}));
    assert(replacement->stopped && rf1.isClientReady());
    replacement = incoming(server2, {10,0,0,10});
    feed(replacement, frame(CMD_AUTH, {'b','r','a','v','o'})); bank.loop();
    expect(replacement, frame(CMD_AUTH_OK));
    stale.tcp->writeForRoute(reply.data(), reply.size(), stale.tcpGeneration);
    assert(replacement->output.empty());
    bank.invalidateInterface({192,168,1,10});
    assert(a->stopped && !rf1.isClientReady() && rf2.isClientReady());
    auto denied = incoming(server1);
    denied->remote = {8,8,8,8};
    feed(denied, frame(CMD_PING));
    bank.loop();
    assert(denied->stopped && commands.size() == 2 && !rf1.isClientReady());
    bank.writeRadioEvent(event.data(), event.size(), 0);
    assert(a->output.empty() && replacement->output.empty());
    bank.invalidateInterface({10,0,0,10});
    assert(replacement->stopped && !rf2.isClientReady());
    rf2.end();
    assert(!WiFiServer::forPort(5056) && WiFiServer::forPort(5055));
    rf1.end();
    assert(!WiFiServer::forPort(5055));
    std::puts("TCP production listener bank: PASS");
}
