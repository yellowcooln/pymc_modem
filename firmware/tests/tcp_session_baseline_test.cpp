// Host contract: compile the real tcp_server.cpp and frame_parser.cpp against
// in-memory socket/Arduino shims; only main.cpp command/error hooks are faked.
#include "tcp_server.h"
#include "tcp_session.h"
#include "frame_parser.h"
#include "protocol.h"
#include <WiFi.h>
#include <cassert>
#include <cstdio>
#include <vector>

struct Command {
    uint8_t id;
    std::vector<uint8_t> payload;
    TransportSource source;
};
static std::vector<Command> commands;
static std::vector<uint8_t> parse_errors;

void processHostCommand(uint8_t cmd, const uint8_t* payload, uint16_t len,
                        TransportSource src) {
    commands.push_back({cmd, std::vector<uint8_t>(payload, payload + len), src});
}
void noteTransportFrameError(uint8_t error) { parse_errors.push_back(error); }

static std::vector<uint8_t> frame(uint8_t cmd, std::vector<uint8_t> payload = {}) {
    std::vector<uint8_t> bytes = {PROTO_SYNC, cmd,
        static_cast<uint8_t>(payload.size()),
        static_cast<uint8_t>(payload.size() >> 8)};
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    const uint16_t crc = crc16_ccitt(bytes.data() + 1, bytes.size() - 1);
    bytes.push_back(static_cast<uint8_t>(crc));
    bytes.push_back(static_cast<uint8_t>(crc >> 8));
    return bytes;
}
static void enqueue(const fake_tcp::SocketPtr& socket, const std::vector<uint8_t>& bytes) {
    socket->input.insert(socket->input.end(), bytes.begin(), bytes.end());
}
static fake_tcp::SocketPtr connect(IPAddress remote = {192, 168, 1, 2},
                                   IPAddress local = {192, 168, 1, 10}) {
    auto socket = std::make_shared<fake_tcp::Socket>();
    socket->remote = remote;
    socket->local = local;
    WiFiServer::current->pending.push_back(socket);
    return socket;
}
static void start(const char* token = "secret") {
    TCPServer::end();
    commands.clear();
    parse_errors.clear();
    TCPServer::begin(5055, String(token));
    assert(WiFiServer::current && WiFiServer::current->active);
    assert(WiFiServer::current->port() == 5055);
}
static void expectOutput(const fake_tcp::SocketPtr& socket, uint8_t cmd,
                         std::vector<uint8_t> payload = {}) {
    assert(socket->output == frame(cmd, payload));
    socket->output.clear();
}
static void authenticate(const fake_tcp::SocketPtr& socket) {
    enqueue(socket, frame(CMD_AUTH, {'s','e','c','r','e','t'}));
    TCPServer::loop();
    assert(TCPServer::isClientReady());
    expectOutput(socket, CMD_AUTH_OK);
}

static void testAuthGateAndResponses() {
    start();
    auto rejected = connect();
    enqueue(rejected, frame(CMD_PING));
    TCPServer::loop();
    expectOutput(rejected, CMD_ERROR, {ERR_UNAUTHORIZED});
    assert(rejected->stopped && commands.empty() && !TCPServer::isClientReady());

    auto wrong = connect();
    enqueue(wrong, frame(CMD_AUTH, {'b','a','d'}));
    TCPServer::loop();
    expectOutput(wrong, CMD_ERROR, {ERR_UNAUTHORIZED});
    assert(wrong->stopped && commands.empty());

    auto allowed = connect();
    TCPServer::loop();
    assert(!TCPServer::isClientReady());
    authenticate(allowed);
    enqueue(allowed, frame(CMD_AUTH, {'w','r','o','n','g'}));
    TCPServer::loop();
    expectOutput(allowed, CMD_AUTH_OK); // repeated AUTH is idempotent after login
    assert(commands.empty());
    enqueue(allowed, frame(CMD_PING));
    TCPServer::loop();
    assert(commands.size() == 1 && commands[0].id == CMD_PING);
    assert(commands[0].source == TransportSource::TCP);
    assert(allowed->no_delay);
    assert(TCPServer::getClientIP() == "192.168.1.2");
    const auto response = frame(CMD_PONG);
    TCPServer::write(response.data(), response.size());
    expectOutput(allowed, CMD_PONG);
}

static void testParsingAndFairness() {
    start("");
    auto socket = connect();
    const auto first = frame(CMD_GET_CONFIG);
    const auto second = frame(CMD_STATUS_REQ);
    enqueue(socket, first);
    enqueue(socket, second);
    TCPServer::loop();
    assert(TCPServer::isClientReady() && commands.size() == 1);
    assert(commands[0].id == CMD_GET_CONFIG && socket->input.size() == second.size());
    TCPServer::loop();
    assert(commands.size() == 2 && commands[1].id == CMD_STATUS_REQ);

    auto corrupt = frame(CMD_PING);
    corrupt.back() ^= 0x01;
    enqueue(socket, corrupt);
    TCPServer::loop();
    expectOutput(socket, CMD_ERROR, {ERR_CRC_MISMATCH});
    assert(parse_errors == std::vector<uint8_t>{ERR_CRC_MISMATCH});
    assert(commands.size() == 2 && TCPServer::isClientReady());

    enqueue(socket, {PROTO_SYNC, CMD_PING, 0x10, 0x01}); // length 272 > parser capacity 271
    TCPServer::loop();
    expectOutput(socket, CMD_ERROR, {ERR_PAYLOAD_TOO_BIG});
    assert(parse_errors.size() == 2 && parse_errors.back() == ERR_PAYLOAD_TOO_BIG);

    const auto large = frame(CMD_TX_REQUEST, std::vector<uint8_t>(260, 0x42));
    enqueue(socket, large);
    TCPServer::loop();
    assert(commands.size() == 2 && socket->input.size() == large.size() - 256);
    TCPServer::loop();
    assert(commands.size() == 3 && commands.back().id == CMD_TX_REQUEST);
    assert(commands.back().payload == std::vector<uint8_t>(260, 0x42));
}

static void testReconnectAndInterfaceInvalidation() {
    start();
    auto old = connect();
    authenticate(old);
    old->connected = false; // remote closes, queued bytes must not be carried forward
    auto fresh = connect();
    enqueue(fresh, frame(CMD_PING));
    TCPServer::loop();
    expectOutput(fresh, CMD_ERROR, {ERR_UNAUTHORIZED});
    assert(fresh->stopped && commands.empty() && !TCPServer::isClientReady());
    assert(TCPServer::getClientIP().length() == 0);

    auto ethernet = connect({10,0,0,2}, {10,0,0,10});
    authenticate(ethernet);
    TCPServer::invalidateInterface({192,168,1,10});
    assert(TCPServer::isClientReady());
    TCPServer::invalidateInterface({10,0,0,10});
    assert(ethernet->stopped && !TCPServer::isClientReady());
    auto next = connect();
    enqueue(next, frame(CMD_AUTH, {'s','e','c','r','e','t'}));
    TCPServer::loop();
    expectOutput(next, CMD_AUTH_OK);
    assert(TCPServer::isClientReady());
    TCPServer::end();
    assert(!TCPServer::isClientReady() && WiFiServer::current == nullptr);
    const auto response = frame(CMD_PONG);
    TCPServer::write(response.data(), response.size());
    assert(next->output.empty());
}

static void testPartialFrameAndSingleClientOwnership() {
    start();
    auto owner = connect();
    const auto auth = frame(CMD_AUTH, {'s','e','c','r','e','t'});
    enqueue(owner, std::vector<uint8_t>(auth.begin(), auth.begin() + 3));
    TCPServer::loop();
    assert(!TCPServer::isClientReady() && owner->output.empty());
    auto waiting = connect({10,1,2,3});
    enqueue(waiting, frame(CMD_PING));
    TCPServer::loop();
    assert(!waiting->stopped && waiting->input.size() == frame(CMD_PING).size());
    enqueue(owner, std::vector<uint8_t>(auth.begin() + 3, auth.end()));
    TCPServer::loop();
    expectOutput(owner, CMD_AUTH_OK);
    assert(TCPServer::isClientReady() && WiFiServer::current->pending.size() == 1);
    owner->connected = false;
    TCPServer::loop();
    expectOutput(waiting, CMD_ERROR, {ERR_UNAUTHORIZED});
    assert(waiting->stopped && commands.empty());
}

static void testPartialAuthCannotCrossReconnect() {
    start();
    auto old = connect();
    const auto auth = frame(CMD_AUTH, {'s','e','c','r','e','t'});
    enqueue(old, std::vector<uint8_t>(auth.begin(), auth.begin() + 3));
    TCPServer::loop();
    assert(!TCPServer::isClientReady() && old->output.empty());

    old->connected = false;
    auto fresh = connect({192,168,1,3});
    enqueue(fresh, auth);
    TCPServer::loop();
    expectOutput(fresh, CMD_AUTH_OK);
    assert(TCPServer::isClientReady() && TCPServer::getClientIP() == "192.168.1.3");
    assert(commands.empty() && parse_errors.empty());

    // Even after authentication, a truncated frame must not contaminate the
    // next socket or allow it to inherit the previous socket's authorization.
    enqueue(fresh, std::vector<uint8_t>(auth.begin(), auth.begin() + 3));
    TCPServer::loop();
    fresh->connected = false;
    auto next = connect({192,168,1,4});
    enqueue(next, frame(CMD_PING));
    TCPServer::loop();
    expectOutput(next, CMD_ERROR, {ERR_UNAUTHORIZED});
    assert(next->stopped && !TCPServer::isClientReady());
    assert(commands.empty() && parse_errors.empty());
}

static void testNonLanRejectedBeforeParsing() {
    start("");
    auto public_socket = connect({8,8,8,8});
    enqueue(public_socket, frame(CMD_PING));
    TCPServer::loop();
    assert(public_socket->stopped && !TCPServer::isClientReady());
    assert(commands.empty() && parse_errors.empty());
    auto lan = connect({172,16,0,1});
    enqueue(lan, frame(CMD_PING));
    TCPServer::loop();
    assert(TCPServer::isClientReady() && commands.size() == 1);
}

static void testIndependentSessions() {
    commands.clear();
    parse_errors.clear();
    TcpSession first, second;
    first.configure(String("secret"));
    second.configure(String("other"));
    auto a = std::make_shared<fake_tcp::Socket>();
    auto b = std::make_shared<fake_tcp::Socket>();
    b->remote = {10, 0, 0, 2};
    b->local = {10, 0, 0, 10};
    first.accept(WiFiClient(a));
    second.accept(WiFiClient(b));

    const auto auth = frame(CMD_AUTH, {'s','e','c','r','e','t'});
    enqueue(a, std::vector<uint8_t>(auth.begin(), auth.begin() + 3));
    first.service();
    enqueue(b, frame(CMD_AUTH, {'o','t','h','e','r'}));
    second.service();
    expectOutput(b, CMD_AUTH_OK);
    assert(second.isReady() && !first.isReady());

    enqueue(a, std::vector<uint8_t>(auth.begin() + 3, auth.end()));
    first.service();
    expectOutput(a, CMD_AUTH_OK);
    assert(first.isReady() && second.isReady());
    enqueue(a, frame(CMD_PING));
    enqueue(b, frame(CMD_GET_CONFIG));
    first.service();
    second.service();
    assert(commands.size() == 2 && commands[0].id == CMD_PING &&
           commands[1].id == CMD_GET_CONFIG);

    first.invalidateInterface({192,168,1,10});
    assert(a->stopped && !first.isReady() && second.isReady());
    enqueue(b, frame(CMD_STATUS_REQ));
    second.service();
    assert(commands.size() == 3 && commands.back().id == CMD_STATUS_REQ);
    second.disconnect();
}

int main() {
    testAuthGateAndResponses();
    testParsingAndFairness();
    testReconnectAndInterfaceInvalidation();
    testPartialFrameAndSingleClientOwnership();
    testPartialAuthCannotCrossReconnect();
    testNonLanRejectedBeforeParsing();
    testIndependentSessions();
    TCPServer::end();
    std::puts("TCP production session baseline: PASS (7 scenarios)");
}
