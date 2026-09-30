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

int main() {
    independentIrqsAndRxOwnership();
    legacyStandbyAndTxTransitions();
}
