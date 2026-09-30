#!/usr/bin/env python3
"""Build and exercise the production TCP session/parser with host socket shims."""
from __future__ import annotations

import pathlib
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
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            f"-I{firmware / 'tests' / 'tcp_stubs'}", f"-I{firmware / 'include'}",
            str(firmware / "tests" / "tcp_session_baseline_test.cpp"),
            str(firmware / "src" / "tcp_server.cpp"),
            str(firmware / "src" / "tcp_session.cpp"),
            str(firmware / "src" / "frame_parser.cpp"),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
