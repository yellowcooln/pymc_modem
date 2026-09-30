#!/usr/bin/env python3
"""Exercise the production radio-configuration seam against independent radios."""
from pathlib import Path
import shutil
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
cpp = r'''#define RADIOLIB_ERR_NONE 0
#include "radio_config_application.h"
#include <cassert>
#include <cstdint>
#include <vector>

struct RecordingRadio {
    std::vector<int> calls;
    int failAt = -1;
    int result(int step) { calls.push_back(step); return failAt == step ? -1 : RADIOLIB_ERR_NONE; }
    int standby() { return result(0); }
    int setFrequency(float v) { freq = v; return result(1); }
    int setBandwidth(float v) { bw = v; return result(2); }
    int setSpreadingFactor(uint8_t v) { sf = v; return result(3); }
    int setCodingRate(uint8_t v) { cr = v; return result(4); }
    int setOutputPower(int8_t v) { power = v; return result(5); }
    int setSyncWord(uint8_t v) { sync = v; return result(6); }
    int setPreambleLength(uint16_t v) { preamble = v; return result(7); }
    int explicitHeader() { return result(8); }
    int setCRC(int) { return result(9); }
    int invertIQ(bool) { return result(10); }
    int autoLDRO() { return result(11); }
    float freq = 0, bw = 0;
    int sf = 0, cr = 0, power = 0, sync = 0, preamble = 0;
};
struct PowerTrace {
    std::vector<int>& events;
    void beforePower(RecordingRadio& radio) {
        assert(radio.calls.size() == 5 && radio.calls.back() == 4);
        events.push_back(50);
    }
    void afterPower(RecordingRadio& radio, const RadioConfig& cfg,
                    int8_t power, int8_t maxPower, int result) {
        assert(radio.calls.size() == 6 && radio.calls.back() == 5);
        assert(cfg.power_dbm == 22 && power == 14 && maxPower == 14);
        events.push_back(result);
    }
};
int main() {
    static_assert(sizeof(RadioConfig) == 14, "wire layout changed");
    RadioConfigState first, second;
    RecordingRadio a, b;
    RadioConfig requested = second.config();
    requested.freq_hz = 915000000;
    requested.bandwidth_hz = 125000;
    requested.power_dbm = 22;
    requested.sf = 11;
    assert(applyRadioConfigRequest(b, second, reinterpret_cast<const uint8_t*>(&requested),
                                   sizeof(requested), 14));
    assert(b.freq == 915 && b.bw == 125 && b.power == 14 && b.sf == 11);
    assert(b.calls.size() == 12 && a.calls.empty());
    assert(first.config().freq_hz == 869618000 && second.config().freq_hz == 915000000);
    assert(applyRadioConfigRequest(a, first, first.wireData(), sizeof(RadioConfig), 22));
    assert(a.power == 22 && a.freq > 869 && a.freq < 870);
    assert(!applyRadioConfigRequest(a, first, nullptr, sizeof(RadioConfig), 22));
    assert(!applyRadioConfigRequest(a, first, reinterpret_cast<const uint8_t*>(&requested), 13, 22));
    assert(a.calls.size() == 12);
    a.failAt = 5;
    assert(!applyRadioConfigRequest(a, first, reinterpret_cast<const uint8_t*>(&requested),
                                    sizeof(requested), 18));
    assert(a.power == 18 && a.calls.size() == 18);
    assert(first.config().freq_hz == requested.freq_hz); // legacy failed-apply state retained
    assert(b.calls.size() == 12 && second.config().power_dbm == 22);
    // Observer fires only on reaching power, strictly around that command,
    // and receives the real result even when RadioLib refuses the setting.
    for (int failure : {4, 5, -1}) {
        RecordingRadio observed;
        observed.failAt = failure;
        std::vector<int> events;
        const bool ok = applyRadioConfig(observed, requested, 14, PowerTrace{events});
        assert(ok == (failure == -1));
        if (failure == 4) {
            assert(events.empty() && observed.calls.size() == 5);
        } else {
            assert((events == std::vector<int>{50, failure == 5 ? -1 : 0}));
            assert(observed.calls.size() == (failure == 5 ? 6u : 12u));
        }
    }
    a.failAt = -1;
    assert(applyRadioConfig(a, first.config(), 18));
    assert(a.power == 18 && a.calls.size() == 30);
}
'''
with tempfile.TemporaryDirectory(prefix='openhop-radio-apply-') as tmp:
    source = Path(tmp) / 'test.cpp'
    source.write_text(cpp)
    binary = Path(tmp) / 'test'
    subprocess.run([shutil.which('g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(firmware / 'include'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
main = (firmware / 'src/main.cpp').read_text()
apply_body = main.split('// ─── Radio configuration', 1)[1].split('bool startReceive()', 1)[0]
set_body = main.split('case CMD_SET_CONFIG: {', 1)[1].split('case CMD_GET_WIFI:', 1)[0]
assert 'applyRadioConfig(hardware.radio, config.config(), board.max_tx_power_dbm,' in apply_body
assert 'PrimaryPowerDiagnostics{board}' in apply_body
assert 'owner.config.setFromWire(payload, len)' in set_body
assert 'applyRadioConfig(primaryRadioHardware.radio, requested, BOARD.max_tx_power_dbm,' in set_body
assert 'PrimaryPowerDiagnostics{BOARD}' in set_body
assert set_body.index('owner.config.setFromWire(payload, len)') < set_body.index('LOG_R_INFO("SET_CONFIG recv') < set_body.index('applyRadioConfig(primaryRadioHardware.radio')
for field in ('freq=%lu', 'bw=%lu', 'sf=%u', 'cr=%u', 'pwr_req=%d', 'sync=0x%04X', 'pre=%u'):
    assert field in set_body
for field in ('setOutputPower=%d', 'ocp_before=%dmA', 'ocp_after=%dmA'):
    assert field in apply_body
assert apply_body.index('currentLimitBefore = (int)radio.getCurrentLimit()') < apply_body.index('const int currentLimitAfter = (int)radio.getCurrentLimit()')
assert 'owner.config.wireData()' in set_body
assert 'applyConfig(primaryRadioConfig.config())' not in main
print('radio config application: OK')
