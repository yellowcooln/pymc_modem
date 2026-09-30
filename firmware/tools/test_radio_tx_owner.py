#!/usr/bin/env python3
"""Compile production TX worker against two recording hardware/owner pairs."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
source = (firmware / 'src/main.cpp').read_text()
assert '#include "radio_tx_owner.h"' in source
assert 'runRadioTx(primaryRadioHardware, owner, payload, len, route,' in source
assert 'owner.radioId != 0 && cmd != CMD_GET_CONFIG' in source
assert 'RFFrontEnd::prepareTransmit()' in source
assert 'applyConfig(primaryRadioHardware, primaryRadioConfig, BOARD)' in source
assert 'startReceive();' in source
assert 'sendFrame(response, bytes, size, replyRoute)' in source
with tempfile.TemporaryDirectory(prefix='openhop-tx-') as tmp:
    binary = Path(tmp) / 'test'
    subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(firmware / 'include'),
                    str(firmware / 'tests/radio_tx_owner_test.cpp'), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('radio TX owner and primary binding: PASS')
