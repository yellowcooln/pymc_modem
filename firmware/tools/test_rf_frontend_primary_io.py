#!/usr/bin/env python3
"""Exercise the production primary RF facade against typed Preferences semantics."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("g++")
if not compiler:
    raise SystemExit("g++ is required")
with tempfile.TemporaryDirectory(prefix="rf-front-end-primary-") as directory:
    for board in ("HELTEC_V43", "STATION_G3", "STATION_G2"):
        binary = Path(directory) / board
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers",
            "-DARDUINO_ARCH_ESP32", f"-DBOARD_{board}",
            f"-I{firmware / 'include'}", f"-I{firmware / 'tests/stubs'}",
            str(firmware / "tests/rf_frontend_primary_io_test.cpp"),
            "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)
        print(f"production PrimaryIO typed Preferences: {board} PASS")
