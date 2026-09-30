#!/usr/bin/env python3
"""Exercise production no-radio admission and the Duo board contract."""
from pathlib import Path
import subprocess
import tempfile
import sys

fw = Path(__file__).resolve().parents[1]
main = (fw / 'src/main.cpp').read_text()
assert main.index('rejectUnavailableRadioCommand(cmd, BOARD.has_lora_radio') < main.index('case CMD_TX_REQUEST:', main.index('// ─── Host command dispatch'))
assert 'static RadioHardware hardware(BOARD);' in main
assert 'static RadioHardware primaryRadioHardware(BOARD)' not in main
assert 'OpenHopSX1262& radio =' not in main
assert main.count('uint16_t port = BOARD.has_lora_radio ? (wcfg.tcpPort ? wcfg.tcpPort : 5055) : 5055;') == 2
assert 'String token = (BOARD.has_wifi || !BOARD.has_lora_radio) ? wcfg.tcpToken : String();' in main
board = (fw / 'include/boards/ethermesh_duo.h').read_text()
assert 'RF1 schematic: NSS20 SCK21 MOSI22 MISO23 RST26 ANT_SW27 BUSY32 DIO1 33' in board
assert 'RF2 schematic (not configured): DIO1=5 BUSY=6 RST=14 NSS=15' in board
assert 'MISO=16 MOSI=17 SCK=18 ANT_SW=19' in board
assert 'if (BOARD.has_lora_radio) {' in main
assert 'if (!BOARD.has_lora_radio) return;' in main
assert 'if (!primaryRadioRuntime.ready || primaryRadioRuntime.txActive) return;' in main
assert 'custom_release = false' in (fw / 'platformio.ini').read_text().split('[env:ethermesh_duo]', 1)[1].split('[env:', 1)[0]
sys.path.insert(0, str(fw / 'tools'))
from release_envs import discover_release_envs
from build_firmware_assets import discover_envs
from package_release_assets import discover_platformio_envs
expected = discover_release_envs(fw / 'platformio.ini')
assert expected == discover_envs() == discover_platformio_envs()
assert 'ethermesh_duo' not in expected and 'ethermesh_1w' in expected
with tempfile.TemporaryDirectory(prefix='openhop-duo-admission-') as td:
    cpp = Path(td) / 'test.cpp'
    cpp.write_text('''#include "board_config.h"
#include "radio_command_admission.h"
#include <cassert>
#include <cstring>
int main() {
    assert(!BOARD.has_lora_radio && !BOARD.has_wifi && BOARD.has_network);
    assert(BOARD.rf_switch.en_pin == -1 && BOARD.static_gpio_count == 0);
    assert(BOARD.pin_lora_nss == 20 && BOARD.pin_lora_sck == 21 && BOARD.pin_lora_mosi == 22);
    assert(BOARD.pin_lora_miso == 23 && BOARD.pin_lora_rst == 26);
    assert(BOARD.pin_lora_busy == 32 && BOARD.pin_lora_dio1 == 33);
    assert(BOARD.pin_i2c_sda == -1 && BOARD.pin_i2c_scl == -1);
    assert(BOARD.ethernet.enabled && BOARD.pin_protocol_uart_rx == -1);
    assert(strstr(BOARD.fw_suffix, "no_rf"));
    const uint8_t blocked[] = {CMD_TX_REQUEST, CMD_CAD_REQUEST, CMD_SET_CONFIG,
        CMD_RX_START, CMD_SET_CAD_PARAMS, CMD_SET_AUTO_CAD,
        CMD_RADIO_STANDBY, CMD_RADIO_RESUME};
    for (auto cmd : blocked) {
        int calls = 0;
        auto reject = [&](uint8_t err) { assert(err == ERR_NO_RADIO); ++calls; };
        assert(rejectUnavailableRadioCommand(cmd, false, false, reject) && calls == 1);
        assert(rejectUnavailableRadioCommand(cmd, false, true, reject) && calls == 2);
        assert(rejectUnavailableRadioCommand(cmd, true, false, reject) && calls == 3);
        assert(!rejectUnavailableRadioCommand(cmd, true, true, reject) && calls == 3);
    }
    const uint8_t live[] = {CMD_PING, CMD_GET_VERSION, CMD_GET_WIFI, CMD_AUTH};
    for (auto cmd : live) {
        assert(!rejectUnavailableRadioCommand(cmd, false, false, [](uint8_t){ assert(false); }));
    }
}
''')
    subprocess.run(['g++', '-std=c++17', '-DBOARD_ETHERMESH_DUO', '-I'+str(fw/'include'), str(cpp), '-o', str(Path(td)/'test')], check=True)
    subprocess.run([str(Path(td)/'test')], check=True)
print('Duo no-radio admission and board contract OK')
