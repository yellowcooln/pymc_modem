#pragma once

#include <stdint.h>

// Explicitly owned RF front-end state. IO belongs to the caller and must outlive
// the controller; constructing a controller never touches hardware or storage.
class RfFrontEndController {
public:
    enum class ValueType { Bool, UChar, UShort };
    struct IO {
        virtual ~IO() = default;
        virtual bool read(const char* key, unsigned& value) = 0;
        virtual bool write(const char* key, unsigned value, ValueType type) = 0;
        virtual void gpio(int8_t pin, bool high) = 0;
    };
    struct Config {
        int8_t paPin = -1, lnaPin = -1, heltecCtxPin = -1;
        bool paActiveHigh = true, lnaActiveHigh = true;
        bool paDefaultHigh = false, lnaDefaultEnabled = true;
        bool paSelectable = false, lnaSelectable = false;
        bool agcSupported = false;
        const char* agcKey = nullptr;
        uint16_t agcDefaultSec = 0;
    };

    RfFrontEndController(const Config& config, IO& io) : config_(config), io_(io) {}

    void begin() {
        paHigh_ = false;
        lnaEnabled_ = false;
        inReceive_ = false;
        paLevel(); // conservative GPIO before any persistent reads
        lnaLevel(false);
        paHigh_ = config_.paDefaultHigh;
        lnaEnabled_ = config_.lnaDefaultEnabled;
        unsigned value;
        if (hasPaModeControl() && hasStationG3LnaControl()) {
            if (io_.read("g3_rf_cfg", value)) {
                paHigh_ = (value & 1) != 0;
                lnaEnabled_ = (value & 2) != 0;
            } else {
                if (io_.read("g3_pa_high", value)) paHigh_ = value != 0;
                if (io_.read("g3_lna_en", value)) lnaEnabled_ = value != 0;
            }
        } else if (hasPaModeControl() && io_.read("g3_pa_high", value)) {
            paHigh_ = value != 0;
        }
        paLevel();
        lnaLevel(false);
        femBypassed_ = true;
        if (hasHeltecV43LnaControl()) {
            if (io_.read("v43_lna_bp", value)) femBypassed_ = value != 0;
            ctxLevel(femBypassed_);
        }
        agcSec_ = config_.agcDefaultSec;
        if (hasAgcResetIntervalControl() && io_.read(config_.agcKey, value)) {
            agcSec_ = value > 3600 ? 3600 : static_cast<uint16_t>(value);
        }
        if (agcSec_ > 3600) agcSec_ = 3600;
    }
    bool hasPaModeControl() const { return config_.paSelectable && config_.paPin >= 0; }
    bool isPaHighPowerEnabled() const { return hasPaModeControl() && paHigh_; }
    bool setPaHighPowerEnabled(bool enabled, bool persist) {
        if (!hasPaModeControl()) return false;
        if (hasStationG3LnaControl()) return setStationG3RfConfig(enabled, lnaEnabled_, persist);
        if (persist && !io_.write("g3_pa_high", enabled, ValueType::Bool)) return false;
        paHigh_ = enabled;
        paLevel();
        return true;
    }
    bool hasStationG3LnaControl() const { return config_.lnaSelectable && config_.lnaPin >= 0; }
    bool isStationG3LnaEnabled() const { return hasStationG3LnaControl() && lnaEnabled_; }
    bool setStationG3LnaEnabled(bool enabled, bool persist) {
        return setStationG3RfConfig(paHigh_, enabled, persist);
    }
    bool setStationG3RfConfig(bool paHighPower, bool lnaEnabled, bool persist) {
        if (!hasPaModeControl() || !hasStationG3LnaControl()) return false;
        unsigned packed = (paHighPower ? 1U : 0U) | (lnaEnabled ? 2U : 0U);
        if (persist && !io_.write("g3_rf_cfg", packed, ValueType::UChar)) return false;
        paHigh_ = paHighPower;
        lnaEnabled_ = lnaEnabled;
        paLevel();
        lnaLevel(inReceive_ && lnaEnabled_);
        return true;
    }
    bool hasHeltecV43LnaControl() const { return config_.heltecCtxPin >= 0; }
    bool isFemLnaBypassed() const { return hasHeltecV43LnaControl() && femBypassed_; }
    bool isExternalLnaEnabled() const { return hasHeltecV43LnaControl() && !femBypassed_; }
    bool setFemLnaBypassed(bool bypass, bool persist) {
        if (!hasHeltecV43LnaControl()) return false;
        if (persist && !io_.write("v43_lna_bp", bypass, ValueType::Bool)) return false;
        femBypassed_ = bypass;
        ctxLevel(femBypassed_); // legacy applies immediately, even outside RX
        return true;
    }
    void prepareTransmit() { inReceive_ = false; lnaLevel(false); ctxLevel(true); }
    void prepareReceive() {
        inReceive_ = true;
        lnaLevel(hasStationG3LnaControl() ? lnaEnabled_ : true);
        ctxLevel(femBypassed_);
    }
    void prepareStandby() { inReceive_ = false; lnaLevel(false); ctxLevel(true); }
    bool hasAgcResetIntervalControl() const { return config_.agcSupported && config_.agcKey; }
    uint16_t getAgcResetIntervalSec() const { return hasAgcResetIntervalControl() ? agcSec_ : 0; }
    bool setAgcResetIntervalSec(uint16_t intervalSec, bool persist) {
        if (!hasAgcResetIntervalControl()) return false;
        if (intervalSec > 3600) intervalSec = 3600;
        if (persist && !io_.write(config_.agcKey, intervalSec, ValueType::UShort)) return false;
        agcSec_ = intervalSec;
        return true;
    }
private:
    void level(int8_t pin, bool active, bool activeHigh) {
        if (pin >= 0) io_.gpio(pin, active == activeHigh);
    }
    void paLevel() { level(config_.paPin, paHigh_, config_.paActiveHigh); }
    void lnaLevel(bool enabled) { level(config_.lnaPin, enabled, config_.lnaActiveHigh); }
    void ctxLevel(bool bypass) { if (config_.heltecCtxPin >= 0) io_.gpio(config_.heltecCtxPin, bypass); }
    Config config_;
    IO& io_;
    bool paHigh_ = false, lnaEnabled_ = true, inReceive_ = false, femBypassed_ = true;
    uint16_t agcSec_ = 0;
};
