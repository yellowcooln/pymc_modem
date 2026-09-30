#!/usr/bin/env python3
"""Compile and exercise the production radio configuration state on the host."""
import pathlib
import shutil
import subprocess
import tempfile


def main():
    firmware = pathlib.Path(__file__).resolve().parents[1]
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ is required")
    with tempfile.TemporaryDirectory(prefix="openhop-radio-config-") as directory:
        executable = pathlib.Path(directory) / "radio_config_state_test"
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            f"-I{firmware / 'include'}",
            str(firmware / 'tests' / 'radio_config_state_test.cpp'),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
