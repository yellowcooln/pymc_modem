#!/usr/bin/env python3
"""Build and execute the production per-instance RF front-end controller."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
compiler = shutil.which("g++")
if not compiler:
    raise SystemExit("g++ is required")
with tempfile.TemporaryDirectory(prefix="rf-front-end-") as directory:
    binary = Path(directory) / "test"
    subprocess.run([
        compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
        f"-I{firmware / 'include'}",
        str(firmware / "tests/rf_frontend_controller_test.cpp"),
        "-o", str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)

source = (firmware / "src/rf_frontend.cpp").read_text()
assert "RfFrontEndController primary(primaryConfig(), primaryIO)" in source
assert "secondary" not in source.lower()
print("RF front-end controller isolation and primary binding: PASS")
