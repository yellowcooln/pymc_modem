#pragma once

#include "radio_runtime_state.h"

// Fixed callback slots: RadioLib accepts a plain function pointer, not a
// closure. Bind each slot once to a static-lifetime runtime before attaching
// its DIO1 interrupt. Never rebind or destroy an owner while an IRQ is live.
// An aligned pointer load/store is one machine word on supported MCUs; there
// is no multi-field publication or mutable owner lookup in the ISR.
template <unsigned Slot>
struct RadioIrqOwner {
    static bool bind(RadioRuntimeState& runtime) {
        if (owner_ != nullptr) return owner_ == &runtime;
        owner_ = &runtime;
        return true;
    }

#if defined(ESP32)
    IRAM_ATTR
#endif
    static void onDio1Rise() {
        RadioRuntimeState* const owner = owner_;
        if (owner != nullptr) owner->onDio1Rise();
    }

private:
    static RadioRuntimeState* volatile owner_;
};

template <unsigned Slot>
RadioRuntimeState* volatile RadioIrqOwner<Slot>::owner_ = nullptr;

// RX IRQs are consumed only by their own radio's loop worker. A TX-owned IRQ
// remains pending for that radio's TX wait loop, as in the legacy dispatcher.
template <typename Receive>
void dispatchRadioRx(RadioRuntimeState& runtime, Receive&& receive) {
    if (runtime.takeRxIrq()) receive();
}
