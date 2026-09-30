#!/usr/bin/env python3
"""Host-only four-port integration using production listeners, routes and query handlers."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

fw = Path(__file__).resolve().parents[1]
source = (fw / 'src/main.cpp').read_text()
frame = source[source.index('static void writeFrame('):source.index('// ─── Remote log', source.index('static void writeFrame('))]
queries = source[source.index('// ─── Endpoint-owned radio queries'):source.index('// ─── Host command dispatch')]
if '--probe-cross-leak' in sys.argv:
    # Demonstrate that the wire-level assertions reject primary status on secondary ports.
    assert 'if (owner.radioId == 0) owner.status' in queries
    queries = queries.replace('if (owner.radioId == 0) owner.status',
                              'if (owner.radioId != 0) owner.status', 1)
with tempfile.TemporaryDirectory(prefix='openhop-four-listener-') as tmp:
    generated = Path(tmp) / 'production.cpp'
    generated.write_text('''#include "tcp_session.h"
#include "tcp_listener.h"
#include "tcp_server.h"
#include "radio_endpoint_registry.h"
#include "protocol.h"
#include <cstring>
#include <vector>
struct FakeUart { void write(const uint8_t*, size_t) {} };
static FakeUart PROTO_UART;
static bool uartEnabled = false;
struct { bool has_lora_radio = true; } BOARD;
StatusResp livePrimary = {};
namespace RuntimeStats {
struct Snapshot { StatusResp status; };
Snapshot capture() { return {livePrimary}; }
}
extern TcpListenerBank* testBank;
namespace TCPServer {
void write(const uint8_t*, size_t) { __builtin_trap(); }
void writeRadioEvent(const uint8_t* bytes, size_t size, uint8_t radio) {
    testBank->writeRadioEvent(bytes, size, radio);
}
}
''' + frame + '\n' + queries + '''
void dispatchTestCommand(uint8_t cmd, const uint8_t* bytes, uint16_t size,
                         ResponseRoute route, RadioCommandContext& owner) {
    if (rejectUnownedCommand(cmd, route, owner)) return;
    if (dispatchRadioQuery(cmd, bytes, size, route, owner)) return;
    // Hardware commands are refused before any physical radio dispatch.
    __builtin_trap();
}
''')
    exe = Path(tmp) / 'four_listener'
    subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-DARDUINO_ARCH_ESP32',
                    '-I' + str(fw / 'tests/tcp_stubs'), '-I' + str(fw / 'include'),
                    str(fw / 'tests/four_listener_integration_test.cpp'), str(generated),
                    str(fw / 'src/tcp_listener.cpp'), str(fw / 'src/tcp_session.cpp'),
                    str(fw / 'src/frame_parser.cpp'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Four production listeners, registry ownership and wire queries: PASS')
