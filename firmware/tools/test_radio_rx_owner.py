#!/usr/bin/env python3
"""Run the production RX template against two independent recording owners."""
import pathlib
import shutil
import subprocess
import tempfile


def main():
    firmware = pathlib.Path(__file__).resolve().parents[1]
    source = (firmware / "src" / "main.cpp").read_text()
    start = source.split("bool startReceive() {", 1)[1].split("// ─── Endpoint-owned radio queries", 1)[0]
    assert "startRadioReceive(primaryRadioHardware, primaryRadioRuntime" in start
    assert "handleRadioRx(primaryRadioHardware, owner" in start
    assert "broadcastFrame(cmd, payload, len, origin)" in start
    assert "RFFrontEnd::prepareReceive()" in start
    assert "RadioCommandContext owner{0, 0, primaryRadioConfig, primaryRadioRuntime, status}" in start
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ is required")
    with tempfile.TemporaryDirectory(prefix="openhop-radio-rx-") as directory:
        binary = pathlib.Path(directory) / "rx_owner_test"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-DRADIOLIB_ERR_NONE=0", f"-I{firmware / 'include'}",
                        str(firmware / "tests" / "radio_rx_owner_test.cpp"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
    print("radio RX owner and primary production binding: PASS")


if __name__ == "__main__":
    main()
