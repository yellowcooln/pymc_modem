#!/usr/bin/env python3
"""Execute main.cpp's CAD dispatcher admission, readiness guard and branches."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
source = (firmware / 'src/main.cpp').read_text()
admission = source[source.index('// ─── Endpoint-owned command admission'):source.index('// ─── Host command dispatch')]
guard = source[source.index('    if (rejectUnavailableRadioCommand(cmd, BOARD.has_lora_radio,', source.index('// ─── Host command dispatch')):source.index('    switch (cmd) {', source.index('// ─── Host command dispatch'))]
params = source[source.index('    case CMD_SET_CAD_PARAMS: {', source.index('// ─── Host command dispatch')):source.index('    case CMD_RX_START: {', source.index('// ─── Host command dispatch'))]
auto = source[source.index('    case CMD_SET_AUTO_CAD: {', source.index('// ─── Host command dispatch')):source.index('    case CMD_SET_DISPLAY_NAME: {', source.index('// ─── Host command dispatch'))]
with tempfile.TemporaryDirectory(prefix='openhop-cad-dispatch-') as tmp:
    cpp = Path(tmp) / 'cad_dispatch.cpp'
    cpp.write_text('''#include "radio_command_context.h"
#include "radio_command_admission.h"
#include "tcp_session.h"
#include "response_route.h"
#include "protocol.h"
#include <cassert>
#include <cstring>
#include <vector>
struct Reply { uint8_t cmd; std::vector<uint8_t> data; ResponseRoute route; };
std::vector<Reply> replies;
int sleeps = 0, persisted = 0;
struct { bool has_lora_radio = true; } BOARD;
RadioRuntimeState primaryRadioRuntime;
void sendFrame(uint8_t cmd, const uint8_t* data, uint16_t len, ResponseRoute route) {
    replies.push_back({cmd, data ? std::vector<uint8_t>(data, data + len) : std::vector<uint8_t>{}, route});
}
void sendError(uint8_t err, ResponseRoute route) { sendFrame(CMD_ERROR, &err, 1, route); }
void delay(int) { ++sleeps; }
struct NodeState { static void setAutoCad(bool) { ++persisted; } };
#define LOG_R_INFO(...) do {} while (0)
''' + admission + '''
void dispatch(uint8_t cmd, const uint8_t* payload, uint16_t len,
              ResponseRoute route, RadioCommandContext& owner) {
    if (rejectUnownedCommand(cmd, route, owner)) return;
''' + guard + '''
    switch (cmd) {
''' + params + auto + '''
    default: assert(false); break;
    }
}
void expect(uint8_t cmd, std::vector<uint8_t> data, ResponseRoute route) {
    assert(replies.size() == 1 && replies[0].cmd == cmd && replies[0].data == data);
    assert(replies[0].route.source == route.source && replies[0].route.tcp == route.tcp);
    assert(replies[0].route.tcpGeneration == route.tcpGeneration);
    replies.clear();
}
int main() {
    RadioConfigState firstConfig, secondConfig;
    RadioRuntimeState first, second;
    StatusResp firstStatus = {}, secondStatus = {};
    RadioCommandContext primary{0, 0, firstConfig, first, firstStatus};
    RadioCommandContext secondary{1, 7, secondConfig, second, secondStatus};
    TcpSession primarySocket(TcpEndpointIdentity{0, 0}), secondarySocket(TcpEndpointIdentity{1, 7});
    TcpSession staleSocket(TcpEndpointIdentity{0, 9});
    ResponseRoute firstRoute{TransportSource::TCP, &primarySocket, 11};
    ResponseRoute secondRoute{TransportSource::TCP, &secondarySocket, 12};
    ResponseRoute staleRoute{TransportSource::TCP, &staleSocket, 13};
    const uint8_t params[4] = {4, 31, 12, 1};
    const uint8_t on[1] = {1};
    first.ready = second.ready = true;
    // An unrelated primary singleton must not make an unready owner available.
    primaryRadioRuntime.ready = true;
    first.ready = false;
    dispatch(CMD_SET_CAD_PARAMS, params, 4, firstRoute, primary);
    expect(CMD_ERROR, {ERR_NO_RADIO}, firstRoute);
    dispatch(CMD_SET_AUTO_CAD, on, 1, firstRoute, primary);
    expect(CMD_ERROR, {ERR_NO_RADIO}, firstRoute);
    assert(!first.cad.custom && !first.cad.autoEnabled && !second.cad.custom && !second.cad.autoEnabled);
    first.ready = true;
    dispatch(CMD_SET_CAD_PARAMS, params, 4, secondRoute, secondary);
    expect(CMD_ERROR, {ERR_INVALID_CMD}, secondRoute);
    dispatch(CMD_SET_AUTO_CAD, on, 1, secondRoute, secondary);
    expect(CMD_ERROR, {ERR_INVALID_CMD}, secondRoute);
    dispatch(CMD_SET_CAD_PARAMS, params, 4, staleRoute, primary);
    expect(CMD_ERROR, {ERR_INVALID_CMD}, staleRoute);
    BOARD.has_lora_radio = false;
    dispatch(CMD_SET_CAD_PARAMS, params, 4, firstRoute, primary);
    expect(CMD_ERROR, {ERR_NO_RADIO}, firstRoute);
    dispatch(CMD_SET_AUTO_CAD, on, 1, firstRoute, primary);
    expect(CMD_ERROR, {ERR_NO_RADIO}, firstRoute);
    BOARD.has_lora_radio = true;
    assert(!first.cad.custom && !first.cad.autoEnabled && !second.cad.custom && !second.cad.autoEnabled);
    dispatch(CMD_SET_CAD_PARAMS, params, 3, firstRoute, primary);
    expect(CMD_ERROR, {ERR_INVALID_CONFIG}, firstRoute);
    dispatch(CMD_SET_AUTO_CAD, on, 0, firstRoute, primary);
    expect(CMD_ERROR, {ERR_INVALID_CMD}, firstRoute);
    assert(!first.cad.custom && !first.cad.autoEnabled && !second.cad.custom && !second.cad.autoEnabled);
    dispatch(CMD_SET_CAD_PARAMS, params, 4, firstRoute, primary);
    expect(CMD_CAD_PARAMS_RESP, {4, 31, 12, 1}, firstRoute);
    assert(first.cad.custom && first.cad.symNum == 4 && first.cad.detPeak == 31 &&
           first.cad.detMin == 12 && first.cad.exitMode == 1);
    dispatch(CMD_SET_AUTO_CAD, on, 1, firstRoute, primary);
    expect(CMD_SET_AUTO_CAD_RESP, {0}, firstRoute);
    assert(first.cad.autoEnabled && !second.cad.custom && !second.cad.autoEnabled);
    assert(sleeps == 1);
#if defined(BOARD_HELTEC_T114)
    assert(persisted == 1);
#else
    assert(persisted == 0);
#endif
}
''')
    compiler = shutil.which('g++')
    for flags in ([], ['-DBOARD_HELTEC_T114']):
        binary = Path(tmp) / ('heltec_cad_dispatch' if flags else 'cad_dispatch')
        subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-DARDUINO_ARCH_ESP32', *flags,
                        '-I' + str(firmware / 'tests/tcp_stubs'), '-I' + str(firmware / 'include'),
                        str(cpp), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('production CAD dispatch: PASS')
