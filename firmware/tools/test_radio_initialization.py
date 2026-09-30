#!/usr/bin/env python3
"""Compile production radio initialization policy against two recording radios."""
import pathlib
import shutil
import subprocess
import tempfile


def main():
    firmware = pathlib.Path(__file__).resolve().parents[1]
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ is required")
    boards = ("BOARD_ETHERMESH_1W", "BOARD_HELTEC_T114", "BOARD_XIAO_NRF52_WIO",
              "BOARD_STATION_G3", "BOARD_RAK3401", "BOARD_ESP32_P4_NANO")
    with tempfile.TemporaryDirectory(prefix="openhop-radio-initialization-") as directory:
        executable = pathlib.Path(directory) / "test"
        for board in boards:
            subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-Wno-missing-field-initializers", f"-D{board}",
                f"-I{firmware / 'tests' / 'radio_hardware_stubs'}",
                f"-I{firmware / 'include'}",
                str(firmware / "tests" / "radio_initialization_test.cpp"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)
            print(f"{board}: PASS")


if __name__ == "__main__":
    main()
