#pragma once

#include <stdint.h>

// Software-only state for one physical radio. No SPI/RadioLib ownership yet.
// onDio1Rise() is deliberately limited to the same volatile stores as the
// legacy ISR; count is diagnostic, not an atomic synchronization primitive.
struct RadioRuntimeState {
    // Ambient RSSI telemetry and packet quiet window belong to the radio,
    // not the transport. Hardware readiness, TX and IRQ gates stay at the
    // sampling call site so no RadioLib operation is hidden in this state.
    struct NoiseState {
        float floorDbm = -99.0f;
        float sum = 0.0f;
        int count = 0;
        uint32_t lastPacketMs = 0;
        uint32_t lastSampleMs = 0;

        void recordPacket(uint32_t now) { lastPacketMs = now; }
        bool canSample(uint32_t now) const {
            return (uint32_t)(now - lastPacketMs) >= 500 &&
                   (uint32_t)(now - lastSampleMs) >= 10;
        }
        void sample(float instantRssi, uint32_t now) {
            lastSampleMs = now; // even invalid readings consume the sample slot
            if (instantRssi < -150.0f || instantRssi > -30.0f) return;
            ++count;
            sum += instantRssi;
            if (count >= 20) {
                float next = sum / 20;
                if (next < -150.0f) next = -150.0f;
                if (next > -50.0f) next = -50.0f;
                floorDbm = next;
                resetSamples();
            }
        }
        void resetSamples() { sum = 0.0f; count = 0; }
        int16_t floorX10() const { return (int16_t)(floorDbm * 10.0f); }
    } noise;

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
