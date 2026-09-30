#!/usr/bin/env python3
"""Compile the firmware's radio runtime seam and exercise independent instances."""
import pathlib
import shutil
import subprocess
import tempfile


def main():
    firmware = pathlib.Path(__file__).resolve().parents[1]
    compiler = shutil.which("g++")
    if not compiler:
        raise SystemExit("g++ is required")
    source = (firmware / "src" / "main.cpp").read_text()
    callback = source.split("void onDio1Rise() {", 1)[1].split("\n}\n", 1)[0]
    rx_dispatch = source.split("    // DIO1 during TX is consumed", 1)[1].split("\n\n    while (Serial.available())", 1)[0]
    with tempfile.TemporaryDirectory(prefix="openhop-radio-runtime-") as directory:
        directory = pathlib.Path(directory)
        executable = directory / "radio_runtime_state_test"
        subprocess.run([
            compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
            f"-I{firmware / 'include'}",
            str(firmware / 'tests' / 'radio_runtime_state_test.cpp'),
            "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)
        # Execute the actual ISR and loop dispatch bodies, not a model of them.
        coupling = directory / "main_runtime_coupling.cpp"
        coupling.write_text('''#include "radio_runtime_state.h"
#include <cassert>
RadioRuntimeState primaryRadioRuntime;
int rxCalls = 0;
void handleLoRaRx() { ++rxCalls; }
void onDio1Rise() {''' + callback + '''\n}
void dispatchRx() {
    // DIO1 during TX is consumed''' + rx_dispatch + '''\n}
int main() {
    onDio1Rise();
    assert(primaryRadioRuntime.irqCount() == 1);
    dispatchRx();
    assert(rxCalls == 1 && !primaryRadioRuntime.irqPending());
    primaryRadioRuntime.txActive = true;
    onDio1Rise();
    dispatchRx();
    assert(rxCalls == 1 && primaryRadioRuntime.irqPending());
    primaryRadioRuntime.clearIrq();
    assert(primaryRadioRuntime.irqCount() == 2);
}
''')
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-I{firmware / 'include'}", str(coupling),
                        "-o", str(directory / "coupling")], check=True)
        subprocess.run([str(directory / "coupling")], check=True)

        # Run the production CAD setters and RadioLib scan-selection block
        # against a recording radio; no physical operation is substituted into
        # main.cpp, only the hardware object is stubbed at the host boundary.
        setter = source.split("    case CMD_SET_CAD_PARAMS: {", 1)[1].split("    case ", 1)[0]
        scan = source.split("        primaryRadioRuntime.clearIrq();\n        int state;", 1)[1].split("\n        if (state != RADIOLIB_ERR_NONE)", 1)[0]
        auto_scan = source.split("                ChannelScanConfig_t cfg = {};", 1)[1].split("                primaryRadioRuntime.clearIrq();", 1)[0]
        cad_coupling = directory / "cad_coupling.cpp"
        cad_coupling.write_text('''#include "radio_runtime_state.h"
#include <cassert>
#include <cstdint>
struct ScanCad { uint8_t symNum = 0, detPeak = 0, detMin = 0, exitMode = 0;
                 int timeout = -1, irqFlags = 0, irqMask = 0; };
struct ChannelScanConfig_t { ScanCad cad; };
constexpr int RADIOLIB_IRQ_CAD_DEFAULT_FLAGS = 3;
constexpr int RADIOLIB_IRQ_CAD_DEFAULT_MASK = 2;
constexpr int CMD_SET_CAD_PARAMS = 1, CMD_CAD_PARAMS_RESP = 2, ERR_INVALID_CONFIG = 3;
struct RecordingRadio {
    int defaults = 0, custom = 0;
    ChannelScanConfig_t last;
    int startChannelScan() { ++defaults; return 0; }
    int startChannelScan(ChannelScanConfig_t cfg) { ++custom; last = cfg; return 0; }
} radio;
RadioRuntimeState primaryRadioRuntime, second;
int errors = 0, responses = 0, sleeps = 0;
void sendError(int, int) { ++errors; }
void sendFrame(int, const uint8_t*, int, int) { ++responses; }
void delay(int) { ++sleeps; }
void configure(const uint8_t* payload, int len) {
    int src = 0;
    switch (CMD_SET_CAD_PARAMS) {
    case CMD_SET_CAD_PARAMS: {''' + setter + '''
    }
}
void startScan() {
    primaryRadioRuntime.clearIrq();
    int state;
''' + scan + '''
    assert(state == 0);
}
ChannelScanConfig_t autoScanConfig() {
    ChannelScanConfig_t cfg = {};
''' + auto_scan + '''
    return cfg;
}
int main() {
    startScan();
    assert(radio.defaults == 1 && radio.custom == 0);
    const uint8_t requested[4] = {4, 31, 12, 1};
    configure(requested, 3);
    assert(errors == 1 && responses == 0 && !primaryRadioRuntime.cad.custom);
    configure(requested, 4);
    assert(responses == 1 && sleeps == 1 && primaryRadioRuntime.cad.custom);
    startScan();
    assert(radio.defaults == 1 && radio.custom == 1);
    assert(radio.last.cad.symNum == 4 && radio.last.cad.detPeak == 31);
    assert(radio.last.cad.detMin == 12 && radio.last.cad.exitMode == 1);
    assert(radio.last.cad.irqFlags == 3 && radio.last.cad.irqMask == 2);
    auto autoCfg = autoScanConfig();
    assert(autoCfg.cad.symNum == 4 && autoCfg.cad.detPeak == 31);
    assert(autoCfg.cad.detMin == 12 && autoCfg.cad.exitMode == 1);
    assert(autoCfg.cad.irqFlags == 3 && autoCfg.cad.irqMask == 2);
    assert(!second.cad.custom && second.cad.symNum == 1);
}
''')
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-I{firmware / 'include'}", str(cad_coupling),
                        "-o", str(directory / "cad_coupling")], check=True)
        subprocess.run([str(directory / "cad_coupling")], check=True)
    print("radio runtime state and main.cpp ISR/CAD coupling: PASS")


if __name__ == "__main__":
    main()
