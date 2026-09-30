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
    binding = source.split("if (!RadioIrqOwner<0>::bind(primaryRadioRuntime)) {", 1)[1].split("primaryRadio().setDio1Action(onDio1Rise);", 1)[0]
    assert 'while (true) delay(1000);' in binding
    assert source.index("if (!RadioIrqOwner<0>::bind(primaryRadioRuntime)) {") < source.index("primaryRadio().setDio1Action(onDio1Rise);")
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
        coupling.write_text('''#include "radio_irq_owner.h"
#include <cassert>
RadioRuntimeState primaryRadioRuntime, secondaryRadioRuntime;
int primaryRxCalls = 0, secondaryRxCalls = 0;
void handleLoRaRx() { ++primaryRxCalls; }
void onDio1Rise() {''' + callback + '''\n}
void dispatchRx() {
    // DIO1 during TX is consumed''' + rx_dispatch + '''\n}
int main() {
    // Stable bindings are established before either radio attaches its DIO1.
    assert(RadioIrqOwner<0>::bind(primaryRadioRuntime));
    assert(RadioIrqOwner<1>::bind(secondaryRadioRuntime));
    assert(!RadioIrqOwner<0>::bind(secondaryRadioRuntime));
    onDio1Rise();
    assert(primaryRadioRuntime.irqCount() == 1 && !secondaryRadioRuntime.irqPending());
    dispatchRx();
    assert(primaryRxCalls == 1 && !primaryRadioRuntime.irqPending());
    primaryRadioRuntime.txActive = true;
    onDio1Rise();
    RadioIrqOwner<1>::onDio1Rise();
    dispatchRx();
    assert(primaryRxCalls == 1 && primaryRadioRuntime.irqPending());
    assert(secondaryRadioRuntime.irqCount() == 1 && secondaryRadioRuntime.irqPending());
    dispatchRadioRx(secondaryRadioRuntime, [] { ++secondaryRxCalls; });
    assert(secondaryRxCalls == 1 && !secondaryRadioRuntime.irqPending());
    assert(primaryRadioRuntime.irqPending());
    primaryRadioRuntime.clearIrq();
    assert(primaryRadioRuntime.irqCount() == 2);
    secondaryRadioRuntime.txActive = true;
    RadioIrqOwner<1>::onDio1Rise();
    dispatchRadioRx(secondaryRadioRuntime, [] { ++secondaryRxCalls; });
    assert(secondaryRxCalls == 1 && secondaryRadioRuntime.irqPending());
    assert(!primaryRadioRuntime.irqPending());
    secondaryRadioRuntime.clearIrq();
    secondaryRadioRuntime.txActive = false;
    RadioIrqOwner<1>::onDio1Rise();
    dispatchRadioRx(secondaryRadioRuntime, [] { ++secondaryRxCalls; });
    assert(secondaryRxCalls == 2 && !secondaryRadioRuntime.irqPending());
    assert(primaryRxCalls == 1 && primaryRadioRuntime.irqCount() == 2);
}
''')
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-I{firmware / 'include'}", str(coupling),
                        "-o", str(directory / "coupling")], check=True)
        subprocess.run([str(directory / "coupling")], check=True)

        # Compile and execute the actual main.cpp sampler with recording
        # RadioLib/millis boundaries, plus two independent runtime instances.
        sampler = source.split("void sampleNoiseFloor() {", 1)[1].split("\n}\n", 1)[0]
        noise_coupling = directory / "noise_coupling.cpp"
        noise_coupling.write_text('''#include "radio_runtime_state.h"
#include <cassert>
#include <cstdint>
RadioRuntimeState primaryRadioRuntime, second;
uint32_t clockMs = 0;
uint32_t millis() { return clockMs; }
struct RecordingRadio {
    int reads = 0;
    float rssi = -100.0f;
    uint32_t readDelayMs = 0;
    float getRSSI(bool lastPacket) { assert(!lastPacket); ++reads; clockMs += readDelayMs; return rssi; }
} radio;
RecordingRadio& primaryRadio() { return radio; }
void sampleNoiseFloor() {''' + sampler + '''\n}
int main() {
    primaryRadioRuntime.ready = true;
    clockMs = 500;
    sampleNoiseFloor();
    assert(radio.reads == 1);
    sampleNoiseFloor();
    assert(radio.reads == 1);
    for (int i = 1; i < 20; ++i) { clockMs = 500 + i * 10; sampleNoiseFloor(); }
    assert(primaryRadioRuntime.noise.floorX10() == -1000);
    assert(second.noise.floorX10() == -990);
    primaryRadioRuntime.noise.recordPacket(clockMs);
    clockMs += 499;
    sampleNoiseFloor();
    assert(radio.reads == 20);
    clockMs++;
    primaryRadioRuntime.onDio1Rise();
    sampleNoiseFloor();
    assert(radio.reads == 20);
    primaryRadioRuntime.clearIrq();
    primaryRadioRuntime.txActive = true;
    sampleNoiseFloor();
    assert(radio.reads == 20);
    primaryRadioRuntime.txActive = false;
    radio.rssi = -120.0f;
    sampleNoiseFloor();
    assert(radio.reads == 21);
    assert(primaryRadioRuntime.noise.floorX10() == -1000);
    // A slow SPI RSSI read must not move the sample timestamp forward.
    primaryRadioRuntime.noise = RadioRuntimeState::NoiseState{};
    radio.reads = 0;
    radio.readDelayMs = 5;
    clockMs = 500;
    sampleNoiseFloor();
    clockMs = 510;
    sampleNoiseFloor();
    assert(radio.reads == 2);
}
''')
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        f"-I{firmware / 'include'}", str(noise_coupling),
                        "-o", str(directory / "noise_coupling")], check=True)
        subprocess.run([str(directory / "noise_coupling")], check=True)

        # Exercise the production CAD policy setter. Auto-CAD is exercised
        # through the production TX worker by test_radio_tx_owner.py.
        setter = source.split("    case CMD_SET_CAD_PARAMS: {", 1)[1].split("    case ", 1)[0]
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
    int route = 0;
    switch (CMD_SET_CAD_PARAMS) {
    case CMD_SET_CAD_PARAMS: {''' + setter + '''
    }
}
int main() {
    const uint8_t requested[4] = {4, 31, 12, 1};
    configure(requested, 3);
    assert(errors == 1 && responses == 0 && !primaryRadioRuntime.cad.custom);
    configure(requested, 4);
    assert(responses == 1 && sleeps == 1 && primaryRadioRuntime.cad.custom);
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
