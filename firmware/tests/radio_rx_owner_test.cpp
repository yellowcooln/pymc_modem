#include "radio_rx_owner.h"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>

struct RecordingRadio {
    int lengths = 0, reads = 0, starts = 0, rssiReads = 0, snrReads = 0;
    int length = 2, result = RADIOLIB_ERR_NONE, startResult = RADIOLIB_ERR_NONE;
    int16_t rssi = -91;
    float snr = 2.5f;
    std::array<uint8_t, 2> bytes{{0xAB, 0xCD}};
    int getPacketLength() { ++lengths; return length; }
    int readData(uint8_t* data, int len) {
        ++reads;
        if (result == RADIOLIB_ERR_NONE) std::memcpy(data, bytes.data(), len);
        return result;
    }
    float getRSSI() { ++rssiReads; return rssi; }
    float getSNR() { ++snrReads; return snr; }
    int startReceive() { ++starts; return startResult; }
};
struct RecordingHardware { RecordingRadio radio; };
struct Event { uint8_t origin, command; std::vector<uint8_t> bytes; };

int main() {
    RecordingHardware primaryHardware, secondaryHardware;
    RadioConfigState primaryConfig, secondaryConfig;
    RadioRuntimeState primaryRuntime, secondaryRuntime;
    StatusResp primaryStatus{}, secondaryStatus{};
    RadioCommandContext primary{0, 0, primaryConfig, primaryRuntime, primaryStatus};
    RadioCommandContext secondary{1, 1, secondaryConfig, secondaryRuntime, secondaryStatus};
    std::vector<Event> primarySink, secondarySink;
    int preparePrimary = 0, prepareSecondary = 0, primaryDisplay = 0, secondaryDisplay = 0;
    auto primaryPrepare = [&] { ++preparePrimary; };
    auto secondaryPrepare = [&] { ++prepareSecondary; };
    auto primaryEmit = [&](uint8_t command, const uint8_t* bytes, uint16_t size, uint8_t origin) {
        primarySink.push_back({origin, command, {bytes, bytes + size}});
    };
    auto secondaryEmit = [&](uint8_t command, const uint8_t* bytes, uint16_t size, uint8_t origin) {
        secondarySink.push_back({origin, command, {bytes, bytes + size}});
    };
    auto displayPrimary = [&](const RadioConfig&, int16_t, int16_t) { ++primaryDisplay; };
    auto displaySecondary = [&](const RadioConfig&, int16_t, int16_t) { ++secondaryDisplay; };

    secondaryHardware.radio.rssi = -72;
    secondaryHardware.radio.snr = -1.3f;
    handleRadioRx(secondaryHardware, secondary, secondaryPrepare, displaySecondary, secondaryEmit,
                  [&] { assert(secondarySink.size() == 1); return 1000U; });
    assert(secondaryHardware.radio.lengths == 1 && secondaryHardware.radio.reads == 1);
    assert(secondaryHardware.radio.rssiReads == 1 && secondaryHardware.radio.snrReads == 1);
    assert(secondaryHardware.radio.starts == 1 && prepareSecondary == 1 && secondaryDisplay == 1);
    assert(primaryHardware.radio.lengths == 0 && primaryHardware.radio.starts == 0);
    assert(preparePrimary == 0 && primaryDisplay == 0 && primarySink.empty());
    assert(secondaryStatus.rx_count == 1 && primaryStatus.rx_count == 0);
    assert(secondaryStatus.last_rssi == -72 && secondaryStatus.last_snr == -13);
    assert(secondaryRuntime.noise.lastPacketMs == 1000 && primaryRuntime.noise.lastPacketMs == 0);
    assert(secondarySink.size() == 1 && secondarySink[0].origin == 1 && secondarySink[0].command == CMD_RX_PACKET);
    assert((secondarySink[0].bytes == std::vector<uint8_t>{0xB8, 0xFF, 0xF3, 0xFF, 0xB8, 0xFF, 0xAB, 0xCD}));

    handleRadioRx(primaryHardware, primary, primaryPrepare, displayPrimary, primaryEmit, [] { return 2000U; });
    assert(primarySink.size() == 1 && primarySink[0].origin == 0);
    assert((primarySink[0].bytes == std::vector<uint8_t>{0xA5, 0xFF, 0x19, 0, 0xA5, 0xFF, 0xAB, 0xCD}));
    assert(primaryStatus.rx_count == 1 && primaryRuntime.noise.lastPacketMs == 2000);
    assert(secondaryStatus.rx_count == 1 && secondarySink.size() == 1);

    secondaryHardware.radio.length = 0;
    handleRadioRx(secondaryHardware, secondary, secondaryPrepare, displaySecondary, secondaryEmit, [] { return 3000U; });
    assert(secondaryHardware.radio.reads == 1 && secondaryHardware.radio.starts == 2);
    assert(secondaryStatus.rx_count == 1 && secondarySink.size() == 1);
    secondaryHardware.radio.length = 2;
    secondaryHardware.radio.result = -1;
    handleRadioRx(secondaryHardware, secondary, secondaryPrepare, displaySecondary, secondaryEmit, [] { return 3001U; });
    assert(secondaryStatus.crc_errors == 1 && primaryStatus.crc_errors == 0);
    assert(secondaryRuntime.noise.lastPacketMs == 1000 && secondarySink.size() == 1);
    assert(secondaryHardware.radio.starts == 3 && prepareSecondary == 3);
    secondaryRuntime.standby = true;
    assert(startRadioReceive(secondaryHardware, secondaryRuntime, secondaryPrepare));
    assert(secondaryHardware.radio.starts == 3 && prepareSecondary == 3);
    secondaryRuntime.standby = false;
    secondaryHardware.radio.startResult = -2;
    assert(!startRadioReceive(secondaryHardware, secondaryRuntime, secondaryPrepare));
    assert(secondaryHardware.radio.starts == 4 && prepareSecondary == 4);
    assert(primaryHardware.radio.starts == 1 && preparePrimary == 1);
}
