#include "tcp_listener.h"
#include "radio_endpoint_registry.h"
#include "protocol.h"
#include <cassert>
#include <cstdio>
#include <memory>
#include <vector>

extern StatusResp livePrimary;
void sendFrame(uint8_t, const uint8_t*, uint16_t, ResponseRoute);
void sendError(uint8_t, ResponseRoute);
void broadcastFrame(uint8_t, const uint8_t*, uint16_t, uint8_t);
void dispatchTestCommand(uint8_t, const uint8_t*, uint16_t, ResponseRoute, RadioCommandContext&);
TcpListenerBank* testBank = nullptr;
static RadioEndpointRegistry<TcpSession, 4>* owners;
static unsigned handled[4] = {};
static unsigned errors = 0;
void noteTransportFrameError(uint8_t error) { assert(error == ERR_CRC_MISMATCH); ++errors; }
void processHostCommand(uint8_t cmd, const uint8_t* data, uint16_t size, ResponseRoute route) {
    auto* owner = owners->resolve(route);
    if (!owner) { sendError(ERR_INVALID_CMD, route); return; }
    ++handled[owner->radioId];
    dispatchTestCommand(cmd, data, size, route, *owner);
}
static std::vector<uint8_t> frame(uint8_t cmd, const std::vector<uint8_t>& data = {}) {
    std::vector<uint8_t> bytes{PROTO_SYNC, cmd, static_cast<uint8_t>(data.size()),
                               static_cast<uint8_t>(data.size() >> 8)};
    bytes.insert(bytes.end(), data.begin(), data.end());
    uint16_t crc = crc16_ccitt(bytes.data() + 1, bytes.size() - 1);
    bytes.push_back(static_cast<uint8_t>(crc));
    bytes.push_back(static_cast<uint8_t>(crc >> 8));
    return bytes;
}
static void feed(const fake_tcp::SocketPtr& sock, const std::vector<uint8_t>& bytes) {
    sock->input.insert(sock->input.end(), bytes.begin(), bytes.end());
}
static void expect(const fake_tcp::SocketPtr& sock, const std::vector<uint8_t>& bytes) {
    assert(sock->output == bytes);
    sock->output.clear();
}
static fake_tcp::SocketPtr connect(unsigned index, IPAddress local = {192,168,1,10}) {
    auto sock = std::make_shared<fake_tcp::Socket>();
    sock->local = local;
    auto* server = WiFiServer::forPort(static_cast<uint16_t>(5055 + index));
    assert(server && server->active);
    server->pending.push_back(sock);
    return sock;
}
int main() {
    TcpListener a({0,0}), b({1,0}), c({2,0}), d({3,0});
    TcpListener* listeners[] = {&a,&b,&c,&d};
    TcpListenerBank bank(listeners, 4);
    testBank = &bank;
    RadioConfigState config[4];
    RadioRuntimeState runtime[4];
    StatusResp status[4] = {};
    StatusResp expectedStatus[4] = {};
    RadioCommandContext context[] = {
        {0,0,config[0],runtime[0],status[0]}, {1,0,config[1],runtime[1],status[1]},
        {2,0,config[2],runtime[2],status[2]}, {3,0,config[3],runtime[3],status[3]}
    };
    RadioEndpointRegistry<TcpSession,4> registry;
    owners = &registry;
    const char* tokens[] = {"alpha", "bravo", "charlie", "delta"};
    fake_tcp::SocketPtr sockets[4];
    for (unsigned i = 0; i < 4; ++i) {
        listeners[i]->begin(static_cast<uint16_t>(5055+i), String(tokens[i]));
        assert(registry.add(i, 0, static_cast<uint16_t>(5055+i), context[i], listeners[i]->session()));
        runtime[i].ready = true;
        runtime[i].noise.floorDbm = -81.0f - i * 10.0f;
        status[i].rx_count = 101 + i;
        expectedStatus[i].rx_count = 101 + i;
        auto wire = config[i].config();
        wire.freq_hz += i * 1000000;
        assert(config[i].setFromWire(reinterpret_cast<const uint8_t*>(&wire), sizeof(wire)));
        sockets[i] = connect(i, i == 2 ? IPAddress{10,0,0,10} : IPAddress{192,168,1,10});
        feed(sockets[i], frame(CMD_AUTH, std::vector<uint8_t>(tokens[i], tokens[i] + std::strlen(tokens[i]))));
    }
    livePrimary.rx_count = 101;
    assert(registry.size() == 4);
    bank.loop();
    for (unsigned i = 0; i < 4; ++i) {
        expect(sockets[i], frame(CMD_AUTH_OK));
        assert(listeners[i]->isClientReady());
    }
    // All four simultaneously make progress; the owner and bytes must match the ingress port.
    for (uint8_t cmd : {CMD_GET_CONFIG, CMD_STATUS_REQ, CMD_NOISE_REQ}) {
        for (auto& socket : sockets) feed(socket, frame(cmd));
        bank.loop();
        for (unsigned i = 0; i < 4; ++i) {
            std::vector<uint8_t> payload;
            uint8_t response = 0;
            if (cmd == CMD_GET_CONFIG) {
                response = CMD_CONFIG_RESP;
                auto* ptr = config[i].wireData();
                payload.assign(ptr, ptr + sizeof(RadioConfig));
            } else if (cmd == CMD_STATUS_REQ) {
                response = CMD_STATUS_RESP;
                const auto* ptr = reinterpret_cast<const uint8_t*>(&expectedStatus[i]);
                payload.assign(ptr, ptr + sizeof(StatusResp));
            } else {
                response = CMD_NOISE_RESP;
                int16_t floor = runtime[i].noise.floorX10();
                payload = {static_cast<uint8_t>(floor), static_cast<uint8_t>(floor >> 8)};
            }
            expect(sockets[i], frame(response, payload));
        }
    }
    // A busy first port cannot starve the others; one frame per owner per pass.
    feed(sockets[0], frame(CMD_GET_CONFIG));
    feed(sockets[0], frame(CMD_GET_CONFIG));
    for (unsigned i = 1; i < 4; ++i) feed(sockets[i], frame(CMD_GET_CONFIG));
    bank.loop();
    for (unsigned i = 0; i < 4; ++i) {
        const auto* ptr = config[i].wireData();
        expect(sockets[i], frame(CMD_CONFIG_RESP, std::vector<uint8_t>(ptr, ptr + sizeof(RadioConfig))));
    }
    assert(sockets[0]->input.size() == frame(CMD_GET_CONFIG).size());
    bank.loop();
    const auto* first = config[0].wireData();
    expect(sockets[0], frame(CMD_CONFIG_RESP, std::vector<uint8_t>(first, first + sizeof(RadioConfig))));
    for (unsigned i = 1; i < 4; ++i) assert(sockets[i]->output.empty());
    const auto event = frame(CMD_RX_PACKET, {0x4a});
    for (unsigned origin = 0; origin < 4; ++origin) {
        broadcastFrame(CMD_RX_PACKET, reinterpret_cast<const uint8_t*>("J"), 1, origin);
        for (unsigned i = 0; i < 4; ++i)
            if (i == origin) expect(sockets[i], event);
            else assert(sockets[i]->output.empty());
    }
    for (unsigned i = 1; i < 4; ++i) {
        for (uint8_t command : {CMD_SET_CONFIG, CMD_TX_REQUEST}) {
            feed(sockets[i], frame(command));
            bank.loop();
            expect(sockets[i], frame(CMD_ERROR, {ERR_INVALID_CMD}));
        }
    }
    auto corrupt = frame(CMD_GET_CONFIG); corrupt.back() ^= 1;
    feed(sockets[0], corrupt); bank.loop();
    expect(sockets[0], frame(CMD_ERROR, {ERR_CRC_MISMATCH}));
    assert(errors == 1);
    auto stale = listeners[2]->session()->responseRoute();
    sockets[2]->connected = false;
    auto replacement = connect(2);
    feed(replacement, frame(CMD_GET_CONFIG)); bank.loop();
    expect(replacement, frame(CMD_ERROR, {ERR_UNAUTHORIZED}));
    assert(replacement->stopped && registry.resolve(stale) == nullptr);
    replacement = connect(2, {10,0,0,10});
    feed(replacement, frame(CMD_AUTH, {'c','h','a','r','l','i','e'})); bank.loop();
    expect(replacement, frame(CMD_AUTH_OK));
    assert(registry.resolve(stale) == nullptr);
    stale.tcp->writeForRoute(event.data(), event.size(), stale.tcpGeneration);
    assert(replacement->output.empty());
    feed(replacement, frame(CMD_NOISE_REQ)); bank.loop();
    int16_t floor = runtime[2].noise.floorX10();
    expect(replacement, frame(CMD_NOISE_RESP, {static_cast<uint8_t>(floor), static_cast<uint8_t>(floor >> 8)}));
    auto denied = connect(0);
    denied->remote = {8,8,8,8};
    const auto handledBeforeDenied = handled[0];
    sockets[0]->connected = false;
    bank.loop();
    assert(denied->stopped && handled[0] == handledBeforeDenied);
    bank.invalidateInterface({192,168,1,10});
    assert(!a.isClientReady() && !b.isClientReady() && !d.isClientReady());
    assert(c.isClientReady());
    bank.invalidateInterface({10,0,0,10});
    assert(replacement->stopped && !c.isClientReady());
    for (unsigned i = 0; i < 4; ++i) assert(handled[i] >= 3);
    std::puts("four listener integration PASS");
}
