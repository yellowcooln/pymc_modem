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
    print("radio runtime state and main.cpp ISR/loop coupling: PASS")


if __name__ == "__main__":
    main()
