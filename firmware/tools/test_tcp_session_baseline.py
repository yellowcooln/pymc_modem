#!/usr/bin/env python3
"""Build and exercise the production TCP session/parser with host socket shims."""
from __future__ import annotations

import pathlib
import re
import shutil
import subprocess
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
        assert len(replies) >= 40 and set(replies) == {"route"}, (
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

        # Recompile the same extracted production encoder without ESP32 so the
        # W5100S TCPServer::write path is exercised with an ungated raw queue.
        nrf_executable = pathlib.Path(directory) / "nrf_broadcast_auth_test"
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            f"-I{firmware / 'tests' / 'tcp_stubs'}", f"-I{firmware / 'include'}",
            str(firmware / "tests" / "nrf_broadcast_auth_test.cpp"),
            str(output), "-o", str(nrf_executable),
        ], check=True)
        subprocess.run([str(nrf_executable)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
