#pragma once

#include <RadioLib.h>
#include "board_config.h"

// Post-begin SX1262 policies. The caller supplies the radio and board policy;
// boot-time GPIO/PA controls remain owned by the board-wide RFFrontEnd.
// DIO2 and external RX/TX pins are independent: Wio-SX1262 needs both,
// while boards without external pins skip setRfSwitchPins entirely.
template <typename Radio, typename Logger>
void configureRadioRfSwitch(Radio& radio, const RfSwitchPolicy& policy, Logger& log) {
    if (policy.dio2_as_rf_switch) {
        int state = radio.setDio2AsRfSwitch(true);
        log.printf("[INFO] setDio2AsRfSwitch(true) -> %d\n", state);
    }
    if (policy.rx_pin >= 0 || policy.tx_pin >= 0) {
        uint32_t rx = policy.rx_pin >= 0 ? (uint32_t)policy.rx_pin : RADIOLIB_NC;
        uint32_t tx = policy.tx_pin >= 0 ? (uint32_t)policy.tx_pin : RADIOLIB_NC;
        radio.setRfSwitchPins(rx, tx);
        log.printf("[INFO] setRfSwitchPins(rx=%lu tx=%lu)\n",
                   (unsigned long)rx, (unsigned long)tx);
    }
}

template <typename Radio, typename Logger>
void configureRadioSx126xOptions(Radio& radio, const BoardConfig& policy, Logger& log) {
    if (policy.sx126x_current_limit_ma > 0) {
        int state = radio.setCurrentLimit(policy.sx126x_current_limit_ma);
        log.printf("[INFO] setCurrentLimit(%d mA) -> %d\n",
                   (int)policy.sx126x_current_limit_ma, state);
    }
    if (policy.sx126x_rx_boosted_gain) {
        int state = radio.setRxBoostedGainMode(true);
        log.printf("[INFO] setRxBoostedGainMode(true) -> %d\n", state);
    }
    if (policy.sx126x_register_patch) {
        int state = radio.applyRegisterPatch08B5();
        log.printf("[INFO] SX126x register patch 0x08B5 -> %d\n", state);
    }
}
