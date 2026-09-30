#pragma once

#include <stdint.h>

// Software-only state for one physical radio. No SPI/RadioLib ownership yet.
// onDio1Rise() is deliberately limited to the same volatile stores as the
// legacy ISR; count is diagnostic, not an atomic synchronization primitive.
struct RadioRuntimeState {
    // Per-radio CAD policy. The caller retains RadioLib scan/IRQ timing and
    // persists autoEnabled where the board already supports persistence.
    struct CadPolicy {
        bool autoEnabled = false;
        bool custom = false;
        uint8_t symNum = 0x01;
        uint8_t detPeak = 22;
        uint8_t detMin = 10;
        uint8_t exitMode = 0x00;

        void setParams(const uint8_t* params) {
            symNum = params[0];
            detPeak = params[1];
            detMin = params[2];
            exitMode = params[3];
            custom = true;
        }
    } cad;

    volatile bool dio1Pending = false;
    volatile uint32_t dio1Count = 0;
    bool ready = false;
    bool standby = false;
    bool txActive = false;

#if defined(ESP32)
    IRAM_ATTR
#endif
    void onDio1Rise() volatile {
        dio1Pending = true;
        dio1Count = dio1Count + 1;
    }

    bool irqPending() const volatile { return dio1Pending; }
    uint32_t irqCount() const volatile { return dio1Count; }
    void clearIrq() volatile { dio1Pending = false; }

    // The TX wait loop, not the RX dispatcher, consumes IRQs while TX owns
    // the radio. Preserve the legacy check/clear ordering.
    bool rxPending() const { return irqPending() && !txActive; }
    bool takeRxIrq() {
        if (!rxPending()) return false;
        clearIrq();
        return true;
    }

    bool receiveAllowed() const { return !standby; }
};
