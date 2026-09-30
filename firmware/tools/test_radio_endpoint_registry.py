#!/usr/bin/env python3
"""Four authenticated production TCP sessions and production radio query dispatch."""
from pathlib import Path
import shutil
import subprocess
import tempfile

fw = Path(__file__).resolve().parents[1]
source = (fw / 'src/main.cpp').read_text()
queries = source[source.index('// ─── Endpoint-owned radio queries'):source.index('// ─── Host command dispatch')]
with tempfile.TemporaryDirectory(prefix='openhop-endpoints-') as tmp:
    cpp = Path(tmp) / 'registry.cpp'
    cpp.write_text('''#include "radio_endpoint_registry.h"
#include "tcp_session.h"
#include "protocol.h"
#include <cassert>
#include <cstring>
#include <memory>
#include <vector>
struct Reply { uint8_t cmd; std::vector<uint8_t> bytes; ResponseRoute route; };
std::vector<Reply> replies;
void sendFrame(uint8_t cmd, const uint8_t* data, uint16_t len, ResponseRoute route) {
    replies.push_back({cmd, data ? std::vector<uint8_t>(data, data + len) : std::vector<uint8_t>{}, route});
}
void sendError(uint8_t error, ResponseRoute route) { sendFrame(CMD_ERROR, &error, 1, route); }
void noteTransportFrameError(uint8_t) {}
void processHostCommand(uint8_t, const uint8_t*, uint16_t, ResponseRoute) { assert(false); }
struct { bool has_lora_radio = true; } BOARD;
StatusResp livePrimary = {};
namespace RuntimeStats {
struct Snapshot { StatusResp status; };
Snapshot capture() { return {livePrimary}; }
}
''' + queries + '''
static void authenticate(TcpSession& session) {
    session.configure(String("secret"));
    auto socket = std::make_shared<fake_tcp::Socket>();
    session.accept(WiFiClient(socket));
    assert(!session.isReady());
    const uint8_t cmd = CMD_AUTH;

    uint8_t frame[] = {PROTO_SYNC, cmd, 6, 0, 's','e','c','r','e','t', 0, 0};
    const uint16_t crc = crc16_ccitt(frame + 1, 9);
    frame[10] = static_cast<uint8_t>(crc);
    frame[11] = static_cast<uint8_t>(crc >> 8);
    for (auto byte : frame) socket->input.push_back(byte);
    session.service();
    assert(session.isReady());
    assert(socket->output.size() >= 6 && socket->output[1] == CMD_AUTH_OK);
}
static void expect(uint8_t cmd, const void* bytes, size_t len, ResponseRoute route) {
    assert(replies.size() == 1 && replies[0].cmd == cmd);
    assert(replies[0].route.tcp == route.tcp && replies[0].route.tcpGeneration == route.tcpGeneration);
    assert(replies[0].bytes.size() == len);
    if (len) assert(std::memcmp(replies[0].bytes.data(), bytes, len) == 0);
    replies.clear();
}
int main() {
    RadioConfigState cfg[4];
    RadioRuntimeState runtime[4];
    StatusResp status[4] = {};
    RadioCommandContext owner[] = {
        {0, 0, cfg[0], runtime[0], status[0]},
        {1, 0, cfg[1], runtime[1], status[1]},
        {2, 0, cfg[2], runtime[2], status[2]},
        {3, 0, cfg[3], runtime[3], status[3]}
    };
    TcpSession session[] = {
        TcpSession(TcpEndpointIdentity{0, 0}), TcpSession(TcpEndpointIdentity{1, 0}),
        TcpSession(TcpEndpointIdentity{2, 0}), TcpSession(TcpEndpointIdentity{3, 0})
    };
    RadioEndpointRegistry<TcpSession, 4> registry;
    for (unsigned i = 0; i < 4; ++i) {
        authenticate(session[i]);
        runtime[i].ready = true;
        runtime[i].noise.floorDbm = -80.0f - static_cast<float>(i * 10);
        status[i].rx_count = 100 + i;
        RadioConfig wire = cfg[i].config();
        wire.freq_hz += i * 1000000;
        assert(cfg[i].setFromWire(reinterpret_cast<uint8_t*>(&wire), sizeof(wire)));
        assert(registry.add(i, 0, static_cast<uint16_t>(5055 + i), owner[i], &session[i]));
    }
    assert(registry.size() == 4);
    for (unsigned i = 0; i < 4; ++i) {
        auto route = session[i].responseRoute();
        assert(registry.resolve(route) == &owner[i]);
        assert(dispatchRadioQuery(CMD_GET_CONFIG, nullptr, 0, route, *registry.resolve(route)));
        expect(CMD_CONFIG_RESP, cfg[i].wireData(), sizeof(RadioConfig), route);
        assert(dispatchRadioQuery(CMD_STATUS_REQ, nullptr, 0, route, *registry.resolve(route)));
        if (i == 0) expect(CMD_STATUS_RESP, &livePrimary, sizeof(StatusResp), route);
        else expect(CMD_STATUS_RESP, &status[i], sizeof(StatusResp), route);
        int16_t noise = runtime[i].noise.floorX10();
        assert(dispatchRadioQuery(CMD_NOISE_REQ, nullptr, 0, route, *registry.resolve(route)));
        expect(CMD_NOISE_RESP, &noise, 2, route);
    }
    RadioEndpointRegistry<TcpSession, 4> invalid;
    assert(invalid.add(0, 0, 5055, owner[0], &session[0]));
    // Slot 0 is local to each radio, not globally reserved by the first.
    assert(invalid.add(1, 0, 5056, owner[1], &session[1]));
    RadioCommandContext duplicateRadio{0, 1, cfg[2], runtime[2], status[2]};
    TcpSession duplicateSocket(TcpEndpointIdentity{0, 1});
    assert(!invalid.add(0, 1, 5057, duplicateRadio, &duplicateSocket));
    assert(!invalid.add(2, 0, 5055, owner[2], &session[2]));
    assert(!invalid.add(1, 0, 5057, owner[1], &session[1]));
    assert(!invalid.add(2, 0, 5057, owner[0], &session[2]));
    RadioCommandContext aliasCfg{2, 0, cfg[0], runtime[2], status[2]};
    RadioCommandContext aliasRuntime{2, 0, cfg[2], runtime[0], status[2]};
    RadioCommandContext aliasStatus{2, 0, cfg[2], runtime[2], status[0]};
    assert(!invalid.add(2, 0, 5057, aliasCfg, &session[2]));
    assert(!invalid.add(2, 0, 5057, aliasRuntime, &session[2]));
    assert(!invalid.add(2, 0, 5057, aliasStatus, &session[2]));
    assert(!invalid.add(2, 0, 0, owner[2], &session[2]));
    assert(!invalid.add(2, 0, 5057, owner[2], &session[0]));
    assert(registry.resolve({TransportSource::TCP, nullptr, 0}) == nullptr);
    TcpSession forged(TcpEndpointIdentity{2, 0});
    authenticate(forged);
    assert(registry.resolve(forged.responseRoute()) == nullptr);
    assert(registry.resolve({TransportSource::TCP, &session[2], session[2].responseRoute().tcpGeneration + 1}) == nullptr);
    auto replaced = session[2].responseRoute();
    session[2].accept(WiFiClient(std::make_shared<fake_tcp::Socket>()));
    assert(registry.resolve(replaced) == nullptr); // old authenticated socket
    assert(registry.resolve(session[2].responseRoute()) == nullptr); // replacement pre-auth
    authenticate(session[2]); // replacement must authenticate independently
    assert(registry.resolve(session[2].responseRoute()) == &owner[2]);
    auto expired = session[3].responseRoute();
    session[3].disconnect();
    assert(registry.resolve(expired) == nullptr);
    assert(registry.resolve({TransportSource::USB, &session[0], 0}) == nullptr);
    assert(registry.resolve({TransportSource::USB, nullptr, 0}) == &owner[0]);
    assert(rejectUnownedCommand(CMD_SET_CONFIG, session[1].responseRoute(), owner[1]));
    uint8_t error = ERR_INVALID_CMD;
    expect(CMD_ERROR, &error, 1, session[1].responseRoute());
    assert(rejectUnownedCommand(CMD_TX_REQUEST, session[2].responseRoute(), owner[2]));
    expect(CMD_ERROR, &error, 1, session[2].responseRoute());
    assert(!rejectUnownedCommand(CMD_GET_CONFIG, session[2].responseRoute(), owner[2]));
}
''')
    compiler = shutil.which('g++')
    subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-DARDUINO_ARCH_ESP32', '-I' + str(fw / 'tests/tcp_stubs'),
                    '-I' + str(fw / 'include'), str(cpp),
                    str(fw / 'src/tcp_session.cpp'), str(fw / 'src/frame_parser.cpp'),
                    '-o', str(Path(tmp) / 'registry')], check=True)
    subprocess.run([str(Path(tmp) / 'registry')], check=True)
print('Four authenticated session identities and isolated production queries passed')
