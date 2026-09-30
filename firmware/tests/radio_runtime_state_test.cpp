#include "radio_runtime_state.h"
#include <cassert>
#include <cstdint>

static void independentIrqsAndRxOwnership() {
    RadioRuntimeState first, second;
    first.ready = second.ready = true;
    first.onDio1Rise();
    assert(first.irqPending());
    assert(first.irqCount() == 1);
    assert(first.rxPending());
    assert(!second.rxPending());
    second.txActive = true;
    second.onDio1Rise();
    assert(second.irqPending());
    assert(!second.rxPending());
    assert(first.takeRxIrq());
    assert(!first.irqPending());
    assert(second.irqPending());
    second.clearIrq();
    assert(second.irqCount() == 1);
    assert(!second.irqPending());
}

static void legacyStandbyAndTxTransitions() {
    RadioRuntimeState radio;
    assert(!radio.ready);
    assert(!radio.standby);
    assert(!radio.txActive);
    assert(radio.receiveAllowed());
    radio.standby = true;
    assert(!radio.receiveAllowed());
    radio.standby = false;
    radio.txActive = true;
    radio.onDio1Rise();
    assert(!radio.takeRxIrq());
    assert(radio.irqPending());  // TX wait loop still owns its completion
    radio.clearIrq();
    radio.txActive = false;
    radio.onDio1Rise();
    assert(radio.takeRxIrq());
    assert(radio.irqCount() == 2);
}

static void independentCadPolicies() {
    RadioRuntimeState first, second;
    assert(!first.cad.custom && !second.cad.custom);
    assert(!first.cad.autoEnabled && !second.cad.autoEnabled);
    assert(first.cad.symNum == 0x01 && first.cad.detPeak == 22);
    assert(first.cad.detMin == 10 && first.cad.exitMode == 0);
    const uint8_t settings[] = {0x04, 31, 12, 1};
    first.cad.setParams(settings);
    first.cad.autoEnabled = true;
    assert(first.cad.custom && first.cad.autoEnabled);
    assert(first.cad.symNum == 0x04 && first.cad.detPeak == 31);
    assert(first.cad.detMin == 12 && first.cad.exitMode == 1);
    assert(!second.cad.custom && !second.cad.autoEnabled);
    assert(second.cad.symNum == 0x01 && second.cad.detPeak == 22);
    second.cad.setParams(settings);
    assert(second.cad.custom && !second.cad.autoEnabled);
}

static void independentNoiseSampling() {
    RadioRuntimeState first, second;
    assert(first.noise.floorX10() == -990);
    assert(second.noise.floorX10() == -990);
    first.noise.recordPacket(1000);
    assert(!first.noise.canSample(1499));
    assert(first.noise.canSample(1500));
    assert(!second.noise.canSample(0));  // boot quiet period
    assert(second.noise.canSample(500));
    first.noise.sample(-40.0f, 1500); // valid but clamped at publication
    assert(!first.noise.canSample(1509));
    assert(first.noise.canSample(1510));
    first.noise.sample(-160.0f, 1510); // invalid, no accumulation
    for (int i = 1; i < 20; ++i) first.noise.sample(-40.0f, 1510 + i * 10);
    assert(first.noise.floorX10() == -500);
    assert(second.noise.floorX10() == -990);
    second.noise.sample(-120.0f, 500);
    assert(second.noise.floorX10() == -990);
    second.noise.resetSamples();
    for (int i = 0; i < 20; ++i) second.noise.sample(-110.0f, 510 + i * 10);
    assert(second.noise.floorX10() == -1100);
    assert(first.noise.floorX10() == -500);
    first.noise.recordPacket(0xFFFFFF00U);
    assert(!first.noise.canSample(0x000000F3U)); // wraparound, 499 ms
    assert(first.noise.canSample(0x000000F4U));
}

int main() {
    independentNoiseSampling();
    independentIrqsAndRxOwnership();
    legacyStandbyAndTxTransitions();
    independentCadPolicies();
}
