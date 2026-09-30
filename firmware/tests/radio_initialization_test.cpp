#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "board_config.h"
#include "radio_initialization.h"

struct RecordingRadio {
    std::vector<std::string> calls;
    int setDio2AsRfSwitch(bool enabled) { calls.push_back(enabled ? "dio2:on" : "dio2:off"); return 7; }
    void setRfSwitchPins(uint32_t rx, uint32_t tx) {
        calls.push_back("switch:" + std::to_string(rx) + ":" + std::to_string(tx));
    }
    int setCurrentLimit(int16_t limit) { calls.push_back("current:" + std::to_string(limit)); return 8; }
    int setRxBoostedGainMode(bool enabled) { calls.push_back(enabled ? "boost:on" : "boost:off"); return 9; }
    int applyRegisterPatch08B5() { calls.push_back("patch"); return 10; }
};
struct RecordingLog {
    std::vector<std::string> lines;
    void printf(const char* format, ...) {
        char buffer[160];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        lines.emplace_back(buffer);
    }
};

int main() {
    RecordingRadio first, second;
    RecordingLog firstLog, secondLog;
    BoardConfig firstPolicy = BOARD, secondPolicy = BOARD;
    firstPolicy.rf_switch = {-1, 0, 21, -1, true};
    firstPolicy.sx126x_current_limit_ma = 140;
    firstPolicy.sx126x_rx_boosted_gain = true;
    firstPolicy.sx126x_register_patch = true;
    secondPolicy.rf_switch = {-1, 0, -1, 22, false};
    secondPolicy.sx126x_current_limit_ma = 90;
    secondPolicy.sx126x_rx_boosted_gain = false;
    secondPolicy.sx126x_register_patch = false;
    configureRadioRfSwitch(first, firstPolicy.rf_switch, firstLog);
    configureRadioSx126xOptions(first, firstPolicy, firstLog);
    configureRadioRfSwitch(second, secondPolicy.rf_switch, secondLog);
    configureRadioSx126xOptions(second, secondPolicy, secondLog);
    const auto nc = std::to_string(static_cast<uint32_t>(RADIOLIB_NC));
    assert((first.calls == std::vector<std::string>{"dio2:on", "switch:21:" + nc, "current:140", "boost:on", "patch"}));
    assert((second.calls == std::vector<std::string>{"switch:" + nc + ":22", "current:90"}));
    assert((firstLog.lines == std::vector<std::string>{
        "[INFO] setDio2AsRfSwitch(true) -> 7\n",
        "[INFO] setRfSwitchPins(rx=21 tx=" + nc + ")\n",
        "[INFO] setCurrentLimit(140 mA) -> 8\n",
        "[INFO] setRxBoostedGainMode(true) -> 9\n",
        "[INFO] SX126x register patch 0x08B5 -> 10\n"}));
    assert(secondLog.lines.size() == 2);
    assert(first.calls.size() == 5 && second.calls.size() == 2);

    // Existing board defaults and ordering, including disabled/no-radio policies.
    RecordingRadio legacy;
    RecordingLog legacyLog;
    configureRadioRfSwitch(legacy, BOARD.rf_switch, legacyLog);
    configureRadioSx126xOptions(legacy, BOARD, legacyLog);
#if defined(BOARD_RAK3401)
    assert((legacy.calls == std::vector<std::string>{"dio2:on", "current:140", "boost:on", "patch"}));
#elif defined(BOARD_ETHERMESH_1W) || defined(BOARD_HELTEC_T114)
    assert((legacy.calls == std::vector<std::string>{"dio2:on", "current:140", "boost:on"}));
#elif defined(BOARD_XIAO_NRF52_WIO)
    assert((legacy.calls == std::vector<std::string>{"dio2:on", "switch:5:" + nc, "current:140", "boost:on"}));
#elif defined(BOARD_STATION_G3)
    assert((legacy.calls == std::vector<std::string>{"dio2:on", "current:140"}));
#elif defined(BOARD_ESP32_P4_NANO)
    assert((legacy.calls == std::vector<std::string>{"dio2:on", "current:140"}));
#endif
    BoardConfig disabled = BOARD;
    disabled.rf_switch = {-1, 0, -1, -1, false};
    disabled.sx126x_current_limit_ma = -1;
    disabled.sx126x_rx_boosted_gain = false;
    disabled.sx126x_register_patch = false;
    RecordingRadio none;
    RecordingLog noneLog;
    configureRadioRfSwitch(none, disabled.rf_switch, noneLog);
    configureRadioSx126xOptions(none, disabled, noneLog);
    assert(none.calls.empty() && noneLog.lines.empty());
}
