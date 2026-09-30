#!/usr/bin/env python3
"""Exercise the production read-only radio command branches with two owners."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
source = (firmware / 'src/main.cpp').read_text()
start = source.index('// ─── Endpoint-owned radio queries')
end = source.index('// ─── Host command dispatch', start)
wrapper_start = source.index('void processHostCommand(uint8_t cmd, const uint8_t* payload, uint16_t len,\n                        ResponseRoute route) {')
wrapper_end = source.index('// Legacy USB/UART', wrapper_start)
with tempfile.TemporaryDirectory(prefix='openhop-command-dispatch-') as tmp:
    cpp = Path(tmp) / 'dispatch.cpp'
    cpp.write_text('''#include "radio_command_context.h"
#include "tcp_session.h"
#include "response_route.h"
#include "protocol.h"
#include <cassert>
#include <cstring>
#include <vector>
struct Reply { uint8_t cmd; std::vector<uint8_t> data; ResponseRoute route; };
std::vector<Reply> replies;
void sendFrame(uint8_t cmd, const uint8_t* data, uint16_t len, ResponseRoute route) {
    replies.push_back({cmd, data ? std::vector<uint8_t>(data, data + len) : std::vector<uint8_t>{}, route});
}
void sendError(uint8_t err, ResponseRoute route) { sendFrame(CMD_ERROR, &err, 1, route); }
struct { bool has_lora_radio = true; } BOARD;
StatusResp livePrimary = {};
namespace RuntimeStats {
struct Snapshot { StatusResp status; };
Snapshot capture() { return {livePrimary}; }
}
RadioConfigState primaryRadioConfig;
RadioRuntimeState primaryRadioRuntime;
StatusResp status = {};
''' + source[start:end] + '''
int routed = 0;
void processHostCommand(uint8_t cmd, const uint8_t* payload, uint16_t len,
                        ResponseRoute route, RadioCommandContext& owner) {
    ++routed;
    dispatchRadioQuery(cmd, payload, len, route, owner);
}
''' + source[wrapper_start:wrapper_end] + '''
static void expect(uint8_t cmd, const void* data, size_t len, ResponseRoute route) {
    assert(replies.size() == 1 && replies[0].cmd == cmd);
    assert(replies[0].route.source == route.source && replies[0].route.tcp == route.tcp);
    assert(replies[0].route.tcpGeneration == route.tcpGeneration);
    assert(replies[0].data.size() == len);
    if (len) assert(memcmp(replies[0].data.data(), data, len) == 0);
    replies.clear();
}
int main() {
    RadioConfigState aConfig, bConfig;
    RadioRuntimeState aRuntime, bRuntime;
    StatusResp aStatus = {}, bStatus = {};
    RadioCommandContext a{0, 0, aConfig, aRuntime, aStatus};
    RadioCommandContext b{1, 7, bConfig, bRuntime, bStatus};
    aRuntime.ready = bRuntime.ready = true;
    RadioConfig unique = bConfig.config();
    unique.freq_hz = 915000000;
    assert(bConfig.setFromWire(reinterpret_cast<const uint8_t*>(&unique), sizeof(unique)));
    aRuntime.noise.floorDbm = -85;
    bRuntime.noise.floorDbm = -115;
    livePrimary.rx_count = 13;
    bStatus.rx_count = 41;
    ResponseRoute usb{TransportSource::USB, nullptr, 0};
    ResponseRoute primary{TransportSource::TCP, reinterpret_cast<TcpSession*>(0x100), 7};
    ResponseRoute secondary{TransportSource::TCP, reinterpret_cast<TcpSession*>(0x200), 9};
    const uint8_t* none = nullptr;
    assert(dispatchRadioQuery(CMD_GET_CONFIG, none, 0, usb, a));
    expect(CMD_CONFIG_RESP, aConfig.wireData(), sizeof(RadioConfig), usb);
    assert(dispatchRadioQuery(CMD_GET_CONFIG, none, 0, secondary, b));
    expect(CMD_CONFIG_RESP, bConfig.wireData(), sizeof(RadioConfig), secondary);
    assert(dispatchRadioQuery(CMD_GET_CONFIG, none, 0, primary, a));
    expect(CMD_CONFIG_RESP, aConfig.wireData(), sizeof(RadioConfig), primary);
    assert(dispatchRadioQuery(CMD_STATUS_REQ, none, 0, secondary, b));
    expect(CMD_STATUS_RESP, &bStatus, sizeof(bStatus), secondary);
    assert(dispatchRadioQuery(CMD_STATUS_REQ, none, 0, primary, a));
    expect(CMD_STATUS_RESP, &livePrimary, sizeof(livePrimary), primary);
    int16_t noise = -1150;
    assert(dispatchRadioQuery(CMD_NOISE_REQ, none, 0, secondary, b));
    expect(CMD_NOISE_RESP, &noise, sizeof(noise), secondary);
    noise = -850;
    assert(dispatchRadioQuery(CMD_NOISE_REQ, none, 0, usb, a));
    expect(CMD_NOISE_RESP, &noise, sizeof(noise), usb);
    assert(!dispatchRadioQuery(CMD_SET_CONFIG, none, 0, secondary, b));
    assert(replies.empty());
    TcpSession first(TcpEndpointIdentity{0, 0});
    TcpSession second(TcpEndpointIdentity{1, 7});
    TcpSession wrongSession(TcpEndpointIdentity{1, 99});
    TcpSession invalid(TcpEndpointIdentity{0, 3});
    ResponseRoute firstRoute{TransportSource::TCP, &first, 1};
    ResponseRoute secondRoute{TransportSource::TCP, &second, 2};
    ResponseRoute wrongSessionRoute{TransportSource::TCP, &wrongSession, 4};
    ResponseRoute invalidRoute{TransportSource::TCP, &invalid, 3};
    assert(rejectUnownedCommand(CMD_GET_CONFIG, wrongSessionRoute, b));
    uint8_t bad = ERR_INVALID_CMD;
    expect(CMD_ERROR, &bad, 1, wrongSessionRoute);
    assert(rejectUnownedCommand(CMD_GET_CONFIG, invalidRoute, a));
    expect(CMD_ERROR, &bad, 1, invalidRoute);
    assert(!rejectUnownedCommand(CMD_GET_CONFIG, firstRoute, a));
    assert(!rejectUnownedCommand(CMD_GET_CONFIG, usb, a));
    assert(replies.empty());
    assert(!rejectUnownedCommand(CMD_GET_CONFIG, secondRoute, b));
    assert(dispatchRadioQuery(CMD_GET_CONFIG, none, 0, secondRoute, b));
    expect(CMD_CONFIG_RESP, bConfig.wireData(), sizeof(RadioConfig), secondRoute);
    assert(rejectUnownedCommand(CMD_GET_CONFIG, firstRoute, b));
    expect(CMD_ERROR, &bad, 1, firstRoute);
    assert(rejectUnownedCommand(CMD_SET_CONFIG, secondRoute, b));
    expect(CMD_ERROR, &bad, 1, secondRoute);
    assert(rejectUnownedCommand(CMD_NOISE_REQ, usb, b));
    expect(CMD_ERROR, &bad, 1, usb);
    processHostCommand(CMD_GET_CONFIG, none, 0, secondRoute);
    expect(CMD_ERROR, &bad, 1, secondRoute);
    processHostCommand(CMD_GET_CONFIG, none, 0, invalidRoute);
    expect(CMD_ERROR, &bad, 1, invalidRoute);
    assert(routed == 0);
    primaryRadioRuntime.ready = true;
    processHostCommand(CMD_GET_CONFIG, none, 0, firstRoute);
    expect(CMD_CONFIG_RESP, primaryRadioConfig.wireData(), sizeof(RadioConfig), firstRoute);
    processHostCommand(CMD_GET_CONFIG, none, 0, usb);
    expect(CMD_CONFIG_RESP, primaryRadioConfig.wireData(), sizeof(RadioConfig), usb);
    assert(routed == 2);
    bRuntime.ready = false;
    assert(dispatchRadioQuery(CMD_GET_CONFIG, none, 0, secondary, b));
    uint8_t err = ERR_NO_RADIO;
    expect(CMD_ERROR, &err, 1, secondary);
}
''')
    compiler = shutil.which('g++')
    subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-DARDUINO_ARCH_ESP32',
                    '-I' + str(firmware / 'tests/tcp_stubs'), '-I' + str(firmware / 'include'),
                    str(cpp), '-o', str(Path(tmp) / 'dispatch')], check=True)
    subprocess.run([str(Path(tmp) / 'dispatch')], check=True)
