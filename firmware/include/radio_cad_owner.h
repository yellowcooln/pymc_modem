#pragma once

#include "radio_command_context.h"
#include "response_route.h"

// Synchronous CAD command worker. The caller supplies board-specific RX
// activity, recovery, watchdog and transport hooks; no singleton radio is
// selected here. Admission remains at the dispatcher (secondary is disabled).
template <class Hardware, class Receiving, class Clock, class Pause, class Feed,
          class Apply, class Resume, class Emit>
void runRadioCad(Hardware& hardware, RadioCommandContext& owner, ResponseRoute route,
                 Receiving receiving, Clock clock, Pause pause, Feed feed,
                 Apply apply, Resume resume, Emit emit) {
    auto& radio = hardware.radio;
    auto& runtime = owner.runtime;
    // A reception is a busy verdict, without standby or RX restart.
    if (receiving()) {
        emit(CMD_CAD_RESP, 1, route);
        return;
    }
    radio.standby();
    pause(1);
    runtime.clearIrq();
    int state;
    if (runtime.cad.custom) {
        ChannelScanConfig_t cfg = {};
        cfg.cad.symNum = runtime.cad.symNum;
        cfg.cad.detPeak = runtime.cad.detPeak;
        cfg.cad.detMin = runtime.cad.detMin;
        cfg.cad.exitMode = runtime.cad.exitMode;
        cfg.cad.timeout = 0;
        cfg.cad.irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS;
        cfg.cad.irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK;
        state = radio.startChannelScan(cfg);
    } else {
        state = radio.startChannelScan();
    }
    if (state != RADIOLIB_ERR_NONE) {
        runtime.clearIrq();
        emit(CMD_ERROR, ERR_CAD_FAILED, route);
        resume();
        return;
    }
    const uint32_t cadStart = clock();
    uint16_t irq = 0;
    while ((uint32_t)(clock() - cadStart) < 500) {
        irq = radio.getIrqFlags();
        if (irq & RADIOLIB_SX126X_IRQ_CAD_DONE) break;
        feed();
        pause(1);
    }
    if (!(irq & RADIOLIB_SX126X_IRQ_CAD_DONE)) {
        radio.standby();
        pause(5);
        apply();
        runtime.clearIrq();
        emit(CMD_ERROR, ERR_CAD_FAILED, route);
        resume();
        return;
    }
    radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_CAD_DONE |
                        RADIOLIB_SX126X_IRQ_CAD_DETECTED);
    runtime.clearIrq();
    emit(CMD_CAD_RESP, (irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED) ? 1 : 0, route);
    // The host probes repeatedly: restore RX before the next request.
    resume();
}
