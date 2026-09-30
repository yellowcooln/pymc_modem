#pragma once

#include "radio_config_state.h"

// Optional observation of the power command; production can read OCP on the
// supplied radio while independent host radios need no diagnostic interface.
struct NoRadioPowerDiagnostics {
    template <typename Radio>
    void beforePower(Radio&) const {}
    template <typename Radio>
    void afterPower(Radio&, const RadioConfig&, int8_t, int8_t, int) const {}
};

// Apply only the SX1262 modulation/configuration commands. The caller owns
// board initialization (TCXO, RF switch, PA/OCP) and RX/display side effects.
// Radio is explicit: a second endpoint must never borrow the primary chip.
template <typename Radio, typename PowerDiagnostics = NoRadioPowerDiagnostics>
bool applyRadioConfig(Radio& radio, const RadioConfig& cfg, int8_t maxPowerDbm,
                      PowerDiagnostics diagnostics = {}) {
    radio.standby(); // legacy: standby failure is not an apply failure
    if (radio.setFrequency(cfg.freq_hz / 1e6f) != RADIOLIB_ERR_NONE) return false;
    if (radio.setBandwidth(cfg.bandwidth_hz / 1000.0f) != RADIOLIB_ERR_NONE) return false;
    if (radio.setSpreadingFactor(cfg.sf) != RADIOLIB_ERR_NONE) return false;
    if (radio.setCodingRate(cfg.cr) != RADIOLIB_ERR_NONE) return false;
    const int8_t power = cfg.power_dbm > maxPowerDbm ? maxPowerDbm : cfg.power_dbm;
    diagnostics.beforePower(radio);
    const int powerResult = radio.setOutputPower(power);
    diagnostics.afterPower(radio, cfg, power, maxPowerDbm, powerResult);
    if (powerResult != RADIOLIB_ERR_NONE) return false;
    if (radio.setSyncWord(cfg.syncword) != RADIOLIB_ERR_NONE) return false;
    if (radio.setPreambleLength(cfg.preamble_len) != RADIOLIB_ERR_NONE) return false;
    radio.explicitHeader();
    radio.setCRC(1);
    radio.invertIQ(false);
    radio.autoLDRO();
    return true;
}

// Preserve the legacy SET_CONFIG contract: 14 requested bytes are stored
// before apply, even when the hardware rejects them. Invalid wire data never
// touches the state or chip.
template <typename Radio, typename PowerDiagnostics = NoRadioPowerDiagnostics>
bool applyRadioConfigRequest(Radio& radio, RadioConfigState& state,
                             const uint8_t* payload, size_t len, int8_t maxPowerDbm,
                             PowerDiagnostics diagnostics = {}) {
    if (!state.setFromWire(payload, len)) return false;
    return applyRadioConfig(radio, state.config(), maxPowerDbm, diagnostics);
}
