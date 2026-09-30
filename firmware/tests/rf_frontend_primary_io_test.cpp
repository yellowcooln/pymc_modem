// Compile with the actual rf_frontend.cpp and board header, replacing only
// the Arduino GPIO/Preferences boundary with a typed in-memory store.
#include "rf_frontend.h"
#include "board_config.h"
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>

#define OUTPUT 1
#define HIGH 1
#define LOW 0
static std::map<int, int> levels;
static void pinMode(int, int) {}
static void digitalWrite(int pin, int level) { levels[pin] = level; }
struct SerialStub { void printf(const char*, ...) {} };
static SerialStub Serial;

struct Entry { enum Type { Bool, UChar, UShort } type; unsigned value; };
static std::map<std::string, Entry> store;
static int reads = 0;
static int writes = 0;
class Preferences {
public:
    bool begin(const char* name, bool) { assert(std::strcmp(name, "lora_modem") == 0); return true; }
    void end() {}
    bool isKey(const char* key) { return store.count(key) != 0; }
    bool getBool(const char* key, bool fallback = false) { return get(key, Entry::Bool, fallback) != 0; }
    uint8_t getUChar(const char* key, uint8_t fallback = 0) { return static_cast<uint8_t>(get(key, Entry::UChar, fallback)); }
    uint16_t getUShort(const char* key, uint16_t fallback = 0) { return static_cast<uint16_t>(get(key, Entry::UShort, fallback)); }
    size_t putBool(const char* key, bool value) { return put(key, Entry::Bool, value); }
    size_t putUChar(const char* key, uint8_t value) { return put(key, Entry::UChar, value); }
    size_t putUShort(const char* key, uint16_t value) { return put(key, Entry::UShort, value); }
private:
    unsigned get(const char* key, Entry::Type type, unsigned fallback) {
        ++reads;
        auto it = store.find(key);
        return it != store.end() && it->second.type == type ? it->second.value : fallback;
    }
    size_t put(const char* key, Entry::Type type, unsigned value) {
        ++writes; store[key] = {type, value}; return 1;
    }
};

// Retain the selected board's RF policy while making AGC's fallback
// nonzero, so a hardcoded getUShort(key, 0) cannot pass this regression.
static BoardConfig testBoard = [] {
    BoardConfig config = BOARD;
    config.sx126x_agc_reset_interval_ms = 7000;
    return config;
}();
#define BOARD testBoard
#include "../src/rf_frontend.cpp" // production adapter and controller facade

static void reset() { store.clear(); levels.clear(); reads = writes = 0; }
static void boot() { RFFrontEnd::begin(); assert(writes == 0); }
static void testAgc(const char* key) {
    const unsigned fallback = BOARD.sx126x_agc_reset_interval_ms / 1000U;
    reset(); store[key] = {Entry::Bool, 1}; boot();
    assert(RFFrontEnd::getAgcResetIntervalSec() == fallback);
    reset(); boot(); assert(RFFrontEnd::getAgcResetIntervalSec() == fallback);
    reset(); store[key] = {Entry::UShort, 17}; boot();
    assert(RFFrontEnd::getAgcResetIntervalSec() == 17);
    reset(); store[key] = {Entry::UShort, 4000}; boot();
    assert(RFFrontEnd::getAgcResetIntervalSec() == 3600);
}

int main() {
#if defined(BOARD_HELTEC_V43)
    reset(); store["v43_lna_bp"] = {Entry::UChar, 0}; boot();
    assert(RFFrontEnd::isFemLnaBypassed()); assert(levels[5] == HIGH);
    reset(); boot(); assert(RFFrontEnd::isFemLnaBypassed());
    reset(); store["v43_lna_bp"] = {Entry::Bool, 0}; boot();
    assert(!RFFrontEnd::isFemLnaBypassed()); assert(levels[5] == LOW);
    testAgc("v43_agc_sec");
#elif defined(BOARD_STATION_G3)
    const int pa = BOARD.rf_frontend.pa_mode_pin;
    reset(); store["g3_pa_high"] = {Entry::UChar, 1};
    store["g3_lna_en"] = {Entry::UChar, 0}; boot();
    assert(RFFrontEnd::isPaHighPowerEnabled() == BOARD.rf_frontend.pa_default_high);
    assert(RFFrontEnd::isStationG3LnaEnabled() == BOARD.rf_frontend.lna_default_enabled);
    assert(levels[pa] == (BOARD.rf_frontend.pa_default_high == BOARD.rf_frontend.pa_high_active_high));
    reset(); boot();
    assert(RFFrontEnd::isPaHighPowerEnabled() == BOARD.rf_frontend.pa_default_high);
    assert(RFFrontEnd::isStationG3LnaEnabled() == BOARD.rf_frontend.lna_default_enabled);
    reset(); store["g3_pa_high"] = {Entry::Bool, 1};
    store["g3_lna_en"] = {Entry::Bool, 0}; boot();
    assert(RFFrontEnd::isPaHighPowerEnabled()); assert(!RFFrontEnd::isStationG3LnaEnabled());
    // A present packed key wins, including when its type is wrong.
    reset(); store["g3_rf_cfg"] = {Entry::Bool, 3};
    store["g3_pa_high"] = {Entry::Bool, 1};
    store["g3_lna_en"] = {Entry::Bool, 1}; boot();
    assert(!RFFrontEnd::isPaHighPowerEnabled()); assert(!RFFrontEnd::isStationG3LnaEnabled());
    reset(); store["g3_rf_cfg"] = {Entry::UChar, 3};
    store["g3_pa_high"] = {Entry::Bool, 0}; boot();
    assert(RFFrontEnd::isPaHighPowerEnabled()); assert(RFFrontEnd::isStationG3LnaEnabled());
    testAgc("g3_agc_sec");
#elif defined(BOARD_STATION_G2)
    testAgc("g2_agc_sec");
#endif
}
