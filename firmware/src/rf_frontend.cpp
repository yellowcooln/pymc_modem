#include "rf_frontend.h"
#include "rf_frontend_controller.h"
#include "board_config.h"

#include <string.h>
#ifdef ARDUINO_ARCH_ESP32
#include <Preferences.h>
#endif

namespace RFFrontEnd {
namespace {
static constexpr const char* NVS_NAMESPACE = "lora_modem";
static constexpr const char* STATION_G3_RF_CONFIG_KEY = "g3_rf_cfg";
static constexpr const char* STATION_G3_PA_HIGH_KEY = "g3_pa_high";
static constexpr const char* STATION_G3_LNA_ENABLED_KEY = "g3_lna_en";
static constexpr const char* V43_LNA_BYPASS_KEY = "v43_lna_bp";

class PrimaryIO : public RfFrontEndController::IO {
public:
    bool read(const char* key, unsigned& value) override {
#ifdef ARDUINO_ARCH_ESP32
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, true)) return false;
        bool found = p.isKey(key);
        if (found) {
            if (strcmp(key, STATION_G3_RF_CONFIG_KEY) == 0) value = p.getUChar(key, 0);
            else if (strstr(key, "agc_sec")) value = p.getUShort(key, 0);
            else value = p.getBool(key, false) ? 1U : 0U;
        }
        p.end();
        return found;
#else
        (void)key; (void)value;
        return false;
#endif
    }
    bool write(const char* key, unsigned value, RfFrontEndController::ValueType type) override {
#ifdef ARDUINO_ARCH_ESP32
        Preferences p;
        if (!p.begin(NVS_NAMESPACE, false)) return false;
        size_t written = 0;
        switch (type) {
        case RfFrontEndController::ValueType::Bool: written = p.putBool(key, value != 0); break;
        case RfFrontEndController::ValueType::UChar: written = p.putUChar(key, static_cast<uint8_t>(value)); break;
        case RfFrontEndController::ValueType::UShort: written = p.putUShort(key, static_cast<uint16_t>(value)); break;
        }
        p.end();
        return written > 0;
#else
        (void)key; (void)value; (void)type;
        return false;
#endif
    }
    void gpio(int8_t pin, bool high) override {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, high ? HIGH : LOW);
    }
};

RfFrontEndController::Config primaryConfig() {
    RfFrontEndController::Config c;
    c.paPin = BOARD.rf_frontend.pa_mode_pin;
    c.paActiveHigh = BOARD.rf_frontend.pa_high_active_high;
    c.paDefaultHigh = BOARD.rf_frontend.pa_default_high;
    c.paSelectable = BOARD.rf_frontend.pa_user_selectable;
    c.lnaPin = BOARD.rf_frontend.lna_mode_pin;
    c.lnaActiveHigh = BOARD.rf_frontend.lna_enabled_active_high;
    c.lnaDefaultEnabled = BOARD.rf_frontend.lna_default_enabled;
    c.lnaSelectable = BOARD.rf_frontend.lna_user_selectable;
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    c.heltecCtxPin = 5;
    c.agcSupported = true;
    c.agcKey = "v43_agc_sec";
#elif defined(BOARD_STATION_G3) && defined(ARDUINO_ARCH_ESP32)
    c.agcSupported = true;
    c.agcKey = "g3_agc_sec";
#elif defined(BOARD_STATION_G2) && defined(ARDUINO_ARCH_ESP32)
    c.agcSupported = true;
    c.agcKey = "g2_agc_sec";
#endif
    c.agcDefaultSec = static_cast<uint16_t>(BOARD.sx126x_agc_reset_interval_ms / 1000U);
    return c;
}
PrimaryIO primaryIO;
RfFrontEndController primary(primaryConfig(), primaryIO); // RF1 only; no RF2 owner or activation

#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
void logCtx(bool bypass, const char* reason) {
    Serial.printf("[RF] Heltec V4.3 FEM RX LNA %s for %s (GPIO5=%s)\n",
                  bypass ? "bypassed" : "enabled", reason,
                  bypass ? "HIGH" : "LOW");
}
#endif
} // namespace

void begin() {
    primary.begin();
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    logCtx(primary.isFemLnaBypassed(), "RX");
#endif
#if (defined(BOARD_HELTEC_V43) || defined(BOARD_STATION_G2) || defined(BOARD_STATION_G3)) && defined(ARDUINO_ARCH_ESP32)
    Serial.printf("[AGC] %s agc.reset.interval=%u s\n", BOARD.name,
                  (unsigned)primary.getAgcResetIntervalSec());
#endif
}
bool hasPaModeControl() { return primary.hasPaModeControl(); }
bool isPaHighPowerEnabled() { return primary.isPaHighPowerEnabled(); }
bool setPaHighPowerEnabled(bool enabled, bool persist) { return primary.setPaHighPowerEnabled(enabled, persist); }
bool hasStationG3LnaControl() { return primary.hasStationG3LnaControl(); }
bool isStationG3LnaEnabled() { return primary.isStationG3LnaEnabled(); }
bool setStationG3LnaEnabled(bool enabled, bool persist) { return primary.setStationG3LnaEnabled(enabled, persist); }
bool setStationG3RfConfig(bool paHighPower, bool lnaEnabled, bool persist) {
    return primary.setStationG3RfConfig(paHighPower, lnaEnabled, persist);
}
bool hasHeltecV43LnaControl() { return primary.hasHeltecV43LnaControl(); }
bool isFemLnaBypassed() { return primary.isFemLnaBypassed(); }
bool isExternalLnaEnabled() { return primary.isExternalLnaEnabled(); }
bool setFemLnaBypassed(bool bypass, bool persist) {
    bool ok = primary.setFemLnaBypassed(bypass, persist);
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    if (ok) logCtx(bypass, "RX");
#endif
    return ok;
}
void prepareTransmit() {
    primary.prepareTransmit();
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    logCtx(true, "TX");
#endif
}
void prepareReceive() {
    primary.prepareReceive();
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    logCtx(primary.isFemLnaBypassed(), "RX");
#endif
}
void prepareStandby() {
    primary.prepareStandby();
#if defined(BOARD_HELTEC_V43) && defined(ARDUINO_ARCH_ESP32)
    logCtx(true, "standby");
#endif
}
bool hasAgcResetIntervalControl() { return primary.hasAgcResetIntervalControl(); }
uint16_t getAgcResetIntervalSec() { return primary.getAgcResetIntervalSec(); }
bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist) {
    return primary.setAgcResetIntervalSec(intervalSec, persist);
}
} // namespace RFFrontEnd
