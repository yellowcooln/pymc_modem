#pragma once

#include "radio_command_context.h"
#include "response_route.h"

enum class RadioTxEvent { CadBusy, Start, StartFailed, Done, Timeout };

// Synchronous TX owns only the explicitly supplied chip and software state.
// Board RF switch, logging, recovery config, RX and transport stay caller hooks.
// Admission remains primary-only until front-end and DIO1 ownership are split.
template <class Hardware, class Receiving, class Clock, class MicroClock,
          class Pause, class Feed, class Prepare, class Led, class Apply,
          class Resume, class Complete, class Log, class Emit>
void runRadioTx(Hardware& hardware, RadioCommandContext& owner,
                const uint8_t* payload, uint16_t len, ResponseRoute route,
                Receiving receiving, Clock clock, MicroClock microClock,
                Pause pause, Feed feed, Prepare prepare, Led led, Apply apply,
                Resume resume, Complete complete, Log log, Emit emit) {
    auto& radio = hardware.radio;
    auto& runtime = owner.runtime;
    auto error = [&](uint8_t code) { emit(CMD_ERROR, &code, 1, route); };
    if (!len || len > MAX_LORA_PAYLOAD) {
        error(ERR_PAYLOAD_TOO_BIG);
        return;
    }
    // Before ANY mode change: a reception must finish on its own IRQ.
    if (receiving()) {
        error(ERR_CHANNEL_BUSY);
        return;
    }
    runtime.txActive = true;
    if (runtime.cad.autoEnabled) {
        bool clear = false;
        for (uint8_t attempt = 0; attempt < 2; ++attempt) {
            if (receiving()) {
                runtime.txActive = false;
                error(ERR_CHANNEL_BUSY);
                return; // passive guard: never restart RX
            }
            ChannelScanConfig_t cfg = {};
            cfg.cad.symNum = runtime.cad.symNum;
            cfg.cad.detPeak = runtime.cad.detPeak;
            cfg.cad.detMin = runtime.cad.detMin;
            cfg.cad.exitMode = runtime.cad.exitMode;
            cfg.cad.irqFlags = RADIOLIB_IRQ_CAD_DEFAULT_FLAGS;
            cfg.cad.irqMask = RADIOLIB_IRQ_CAD_DEFAULT_MASK;
            runtime.clearIrq();
            if (radio.startChannelScan(cfg) != RADIOLIB_ERR_NONE) {
                runtime.clearIrq();
                break;
            }
            const uint32_t started = clock();
            uint16_t irq = 0;
            while ((uint32_t)(clock() - started) < 200) {
                irq = radio.getIrqFlags();
                if (irq & RADIOLIB_SX126X_IRQ_CAD_DONE) break;
                feed();
                pause(2);
            }
            radio.clearIrqFlags(RADIOLIB_SX126X_IRQ_CAD_DONE |
                                RADIOLIB_SX126X_IRQ_CAD_DETECTED);
            runtime.clearIrq();
            if (!(irq & RADIOLIB_SX126X_IRQ_CAD_DONE)) break;
            if (!(irq & RADIOLIB_SX126X_IRQ_CAD_DETECTED)) {
                clear = true;
                break;
            }
            pause(50 + (microClock() % 150));
        }
        if (!clear) {
            log(RadioTxEvent::CadBusy, 0, 0);
            runtime.txActive = false;
            error(ERR_CHANNEL_BUSY);
            resume();
            return;
        }
    }
    radio.standby();
    pause(1);
    prepare();
    runtime.clearIrq();
    const uint32_t irqStart = runtime.irqCount();
    led(true);
    int state = radio.startTransmit(const_cast<uint8_t*>(payload), len);
    log(RadioTxEvent::Start, state, 0);
    if (state != RADIOLIB_ERR_NONE) {
        log(RadioTxEvent::StartFailed, state, 0);
        runtime.txActive = false;
        radio.finishTransmit();
        led(false);
        error(ERR_TX_TIMEOUT);
        resume();
        return;
    }
    const uint32_t started = clock();
    while (!runtime.irqPending() && (uint32_t)(clock() - started) < 4500) {
        feed();
        pause(2);
    }
    const bool txOk = runtime.irqPending();
    radio.finishTransmit();
    led(false);
    runtime.clearIrq();
    runtime.txActive = false;
    runtime.noise.recordPacket(clock());
    complete();
    if (txOk) {
        owner.status.tx_count++;
        const uint32_t airtime = radio.getTimeOnAir(len);
        uint8_t response[4] = {static_cast<uint8_t>(airtime),
            static_cast<uint8_t>(airtime >> 8), static_cast<uint8_t>(airtime >> 16),
            static_cast<uint8_t>(airtime >> 24)};
        emit(CMD_TX_DONE, response, sizeof(response), route);
        log(RadioTxEvent::Done, 0, airtime);
    } else {
        log(RadioTxEvent::Timeout, 0, runtime.irqCount() - irqStart);
        radio.standby();
        pause(5);
        apply();
        error(ERR_TX_TIMEOUT);
    }
    resume();
}
