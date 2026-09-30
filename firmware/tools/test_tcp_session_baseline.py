#!/usr/bin/env python3
"""Build and exercise the production TCP session/parser with host socket shims."""
from __future__ import annotations

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile


def main() -> int:
    firmware = pathlib.Path(__file__).resolve().parents[1]
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ is required for TCP session tests")
    with tempfile.TemporaryDirectory(prefix="openhop-tcp-session-") as directory:
        executable = pathlib.Path(directory) / "tcp_session_baseline_test"
        # Compile the actual frame-output functions from main.cpp, not a
        # test reimplementation of the routing/serialization logic.
        main_source = (firmware / "src" / "main.cpp").read_text()
        dispatcher = main_source.split("// ─── Host command dispatch", 1)[1].split(
            "// Legacy USB/UART", 1)[0]
        assert "ResponseRoute route)" in dispatcher
        replies = re.findall(r"send(?:Frame|Error)\([^;]*?,\s*(route|src)\);", dispatcher)
        # CAD and TX replies pass through their route-preserving owner
        # callbacks (exercised by their independent recording-radio tests).
        assert 'sendFrame(response, bytes, size, replyRoute)' in dispatcher
        assert len(replies) >= 30 and set(replies) == {"route"}, (
            "Every synchronous command reply (including TX completion and errors) "
            "must retain its ingress route")
        start = main_source.index("static void writeFrame(")
        end = main_source.index("// ─── Remote log", start)
        output = pathlib.Path(directory) / "production_frame_output.cpp"
        output.write_text('''#include "tcp_session.h"
#include "tcp_server.h"
#include "response_route.h"
#include "protocol.h"
#include <vector>
std::vector<uint8_t>& fakeUartOutput() { static std::vector<uint8_t> out; return out; }
struct FakeUart { void write(const uint8_t* bytes, size_t len) {
    fakeUartOutput().insert(fakeUartOutput().end(), bytes, bytes + len);
} };
static FakeUart PROTO_UART;
static bool uartEnabled = true;
''' + main_source[start:end])
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-DARDUINO_ARCH_ESP32",
            f"-I{firmware / 'tests' / 'tcp_stubs'}", f"-I{firmware / 'include'}",
            str(firmware / "tests" / "tcp_session_baseline_test.cpp"),
            str(firmware / "src" / "tcp_server.cpp"),
            str(firmware / "src" / "tcp_session.cpp"),
            str(firmware / "src" / "frame_parser.cpp"),
            str(output),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)

        # Bind production W5100S readiness and token-policy definitions to
        # recording socket/IP shims; raw write intentionally remains ungated.
        ethernet_source = (firmware / "src" / "w5100s_ethernet_transport.cpp").read_text()
        auth_start = ethernet_source.index("    static bool requiresAuth() {", ethernet_source.index("namespace TCPServer {"))
        auth_end = ethernet_source.index("\n    }", auth_start) + len("\n    }")
        ready_start = ethernet_source.index("    bool isClientReady() {", auth_end)
        ready_end = ethernet_source.index("\n    }", ready_start) + len("\n    }")
        auth = ethernet_source[auth_start:auth_end]
        readiness = ethernet_source[ready_start:ready_end]
        assert "requiredToken.length()" in auth
        assert "EthernetManager::hasIP()" in readiness
        assert "socketReader.connected()" in readiness
        assert "requiresAuth()" in readiness
        w5100s = pathlib.Path(directory) / "production_w5100s_readiness.cpp"
        w5100s.write_text('''#include "tcp_server.h"
namespace EthernetManager { bool hasIP(); }
namespace TCPServer {
struct FakeClient { explicit operator bool() const; };
struct FakeSocketReader { bool connected() const; };
extern FakeClient client;
extern FakeSocketReader socketReader;
extern String requiredToken;
extern bool authenticated;
''' + auth + '\n' + readiness + '\n} // namespace TCPServer\n')
        nrf_output = output
        if "--probe-broken-broadcast" in sys.argv:
            # Mutation probe: ungate just the copied nRF broadcast expression.
            broken = main_source[start:end].replace(
                "/*toTCP=*/TCPServer::isClientReady(),",
                "/*toTCP=*/true,", 1)
            assert broken != main_source[start:end]
            nrf_output = pathlib.Path(directory) / "broken_frame_output.cpp"
            nrf_output.write_text(output.read_text().replace(main_source[start:end], broken))
        nrf_executable = pathlib.Path(directory) / "nrf_broadcast_auth_test"
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            f"-I{firmware / 'tests' / 'tcp_stubs'}", f"-I{firmware / 'include'}",
            str(firmware / "tests" / "nrf_broadcast_auth_test.cpp"),
            str(nrf_output), str(w5100s), "-o", str(nrf_executable),
        ], check=True)
        subprocess.run([str(nrf_executable)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
