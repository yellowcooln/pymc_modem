#include "rf_frontend_controller.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>

struct Fake : RfFrontEndController::IO {
    std::map<std::string, unsigned> values;
    std::map<int, bool> pins;
    std::vector<std::string> events;
    bool fail = false;
    bool read(const char* key, unsigned& value) override {
        events.push_back(std::string("read:") + key);
        auto it = values.find(key);
        if (it == values.end()) return false;
        value = it->second;
        return true;
    }
    bool write(const char* key, unsigned value, RfFrontEndController::ValueType type) override {
        events.push_back(std::string("write:") + key);
        if (fail) return false;
        values[key] = value;
        (void)type;
        return true;
    }
    void gpio(int8_t pin, bool high) override {
        assert(pin >= 0);
        events.push_back(std::string("gpio:") + std::to_string(pin));
        pins[pin] = high;
    }
};

int main() {
    Fake a, b;
    RfFrontEndController::Config ca;
    ca.paPin = 9; ca.lnaPin = 10; ca.paSelectable = ca.lnaSelectable = true;
    ca.lnaActiveHigh = false; ca.lnaDefaultEnabled = true;
    ca.agcSupported = true; ca.agcKey = "g3_agc_sec";
    RfFrontEndController::Config cb = ca;
    cb.paPin = 19; cb.lnaPin = 20;
    RfFrontEndController one(ca, a), two(cb, b);
    a.values["g3_pa_high"] = 1; a.values["g3_lna_en"] = 0;
    one.begin(); two.begin();
    assert(one.isPaHighPowerEnabled() && !one.isStationG3LnaEnabled());
    assert(!two.isPaHighPowerEnabled() && two.isStationG3LnaEnabled());
    assert(a.events[0] == "gpio:9" && a.pins[10]); // migration, safe PA before reads
    assert(b.pins[19] == false && b.pins[20] == true); // RX LNA off (active low)
    one.prepareReceive(); two.prepareReceive();
    assert(a.pins[10] && !b.pins[20]);
    a.fail = true;
    auto count = a.events.size();
    assert(!one.setStationG3RfConfig(false, true, true));
    assert(a.events.size() == count + 1 && one.isPaHighPowerEnabled());
    assert(a.pins[9] && a.pins[10]);
    a.fail = false;
    assert(one.setStationG3RfConfig(false, true, true));
    assert(a.values["g3_rf_cfg"] == 2 && !a.pins[9] && !a.pins[10]);
    one.prepareTransmit(); two.prepareStandby();
    assert(a.pins[10] && b.pins[20]);
    one.prepareReceive(); two.prepareReceive();
    assert(!a.pins[10] && !b.pins[20]);
    assert(two.isStationG3LnaEnabled());
    // A packed record supersedes the legacy booleans on the next boot.
    Fake migrated;
    migrated.values = a.values;
    migrated.values["g3_pa_high"] = 1;
    migrated.values["g3_lna_en"] = 0;
    RfFrontEndController rebooted(ca, migrated);
    rebooted.begin();
    assert(!rebooted.isPaHighPowerEnabled() && rebooted.isStationG3LnaEnabled());
    assert(b.values.empty() && b.pins[19] == false); // other owner unchanged

    Fake absent;
    RfFrontEndController::Config none;
    RfFrontEndController noPins(none, absent);
    noPins.begin(); noPins.prepareTransmit(); noPins.prepareReceive(); noPins.prepareStandby();
    assert(absent.pins.empty());
    assert(!noPins.hasPaModeControl() && !noPins.setPaHighPowerEnabled(true, false));
    assert(!noPins.setStationG3RfConfig(true, true, false));

    Fake heltec;
    RfFrontEndController::Config ch;
    ch.heltecCtxPin = 5; ch.agcSupported = true; ch.agcKey = "v43_agc_sec";
    heltec.values["v43_lna_bp"] = 0;
    heltec.values["v43_agc_sec"] = 9999;
    RfFrontEndController h(ch, heltec);
    h.begin(); assert(!h.isFemLnaBypassed() && !heltec.pins[5]);
    h.prepareTransmit(); assert(heltec.pins[5]);
    h.prepareStandby(); assert(heltec.pins[5]);
    h.prepareReceive(); assert(!heltec.pins[5]);
    heltec.fail = true;
    assert(!h.setFemLnaBypassed(true, true) && !heltec.pins[5]);
    assert(!h.setAgcResetIntervalSec(10, true) && h.getAgcResetIntervalSec() == 3600);
    heltec.fail = false;
    assert(h.setFemLnaBypassed(true, true) && heltec.pins[5]);
    assert(h.setAgcResetIntervalSec(9999, true) && h.getAgcResetIntervalSec() == 3600);
    assert(heltec.values["v43_agc_sec"] == 3600);
}
