#!/usr/bin/env python3
"""Compile the production RadioHardware seam with recording SPI/RadioLib boundaries."""
import pathlib
import shutil
import subprocess
import tempfile


def main():
    firmware = pathlib.Path(__file__).resolve().parents[1]
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ is required")
    boards = (
        ("BOARD_ETHERMESH_1W", "ARDUINO_ARCH_ESP32"),
        ("BOARD_HELTEC_V3", "ARDUINO_ARCH_ESP32"),
        ("BOARD_PHOTON_1W_XIAO_ESP32C6", "ARDUINO_ARCH_ESP32"),
        ("BOARD_HELTEC_T114", "NRF52"),
        ("BOARD_RAK4631_WISMESH_ETH", "NRF52"),
        ("BOARD_RAK4631_USB", "NRF52"),
    )
    with tempfile.TemporaryDirectory(prefix="openhop-radio-hardware-") as directory:
        executable = pathlib.Path(directory) / "test"
        for board, architecture in boards:
            subprocess.run([
                compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                "-Wno-missing-field-initializers", f"-D{board}", f"-D{architecture}",
                f"-I{firmware / 'tests' / 'radio_hardware_stubs'}",
                f"-I{firmware / 'include'}",
                str(firmware / "tests" / "radio_hardware_test.cpp"),
                "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)
            print(f"{board}: PASS")


if __name__ == "__main__":
    main()
