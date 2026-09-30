#!/usr/bin/env python3
"""Compile the production CAD worker with independent recording radio owners."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
source = (firmware / 'src/main.cpp').read_text()
assert '#include "radio_cad_owner.h"' in source
assert 'runRadioCad(primaryRadioHardwareInstance(), owner, route,' in source
assert 'RFFrontEnd::prepareReceive()' in source
with tempfile.TemporaryDirectory(prefix='openhop-cad-') as tmp:
    binary = Path(tmp) / 'test'
    subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(firmware / 'include'),
                    str(firmware / 'tests/radio_cad_owner_test.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('radio CAD owner and primary binding: PASS')
