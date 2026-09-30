#include <cstdint>
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_SX126X_IRQ_CAD_DONE 1
#define RADIOLIB_SX126X_IRQ_CAD_DETECTED 2
#define RADIOLIB_IRQ_CAD_DEFAULT_FLAGS 3
#define RADIOLIB_IRQ_CAD_DEFAULT_MASK 4
struct ChannelScanConfig_t {
    struct { uint8_t symNum, detPeak, detMin, exitMode; uint32_t timeout, irqFlags, irqMask; } cad;
};
#include "radio_cad_owner.h"
#include <cassert>
#include <vector>

struct RecordingRadio {
    int standbys = 0, starts = 0, clears = 0;
    int startResult = RADIOLIB_ERR_NONE;
    uint16_t flags = RADIOLIB_SX126X_IRQ_CAD_DONE;
    bool custom = false;
    int standby() { ++standbys; return 0; }
    int startChannelScan() { ++starts; custom = false; return startResult; }
    int startChannelScan(ChannelScanConfig_t cfg) {
        ++starts; custom = true;
        assert(cfg.cad.irqFlags == RADIOLIB_IRQ_CAD_DEFAULT_FLAGS);
        assert(cfg.cad.irqMask == RADIOLIB_IRQ_CAD_DEFAULT_MASK);
        assert(cfg.cad.symNum == 4 && cfg.cad.detPeak == 25);
        return startResult;
    }
    uint16_t getIrqFlags() { return flags; }
    void clearIrqFlags(uint16_t mask) {
        assert(mask == (RADIOLIB_SX126X_IRQ_CAD_DONE | RADIOLIB_SX126X_IRQ_CAD_DETECTED));
        ++clears;
    }
};
struct RecordingHardware { RecordingRadio radio; };
struct Result { uint8_t command, value; ResponseRoute route; };
int main() {
    RecordingHardware a, b;
    RadioConfigState ac, bc;
    RadioRuntimeState ar, br;
    StatusResp as{}, bs{};
    RadioCommandContext primary{0, 0, ac, ar, as};
    RadioCommandContext secondary{1, 1, bc, br, bs};
    uint8_t params[4] = {4, 25, 9, 0};
    br.cad.setParams(params);
    std::vector<Result> replies;
    uint32_t now = 100;
    int pauses = 0, feeds = 0, applies = 0, resumes = 0;
    ResponseRoute route{TransportSource::TCP, nullptr, 17};
    auto emit = [&](uint8_t cmd, uint8_t value, ResponseRoute r) { replies.push_back({cmd, value, r}); };
    bool applied = true;
    auto invoke = [&](RecordingHardware& hw, RadioCommandContext& owner, bool receiving = false) {
        runRadioCad(hw, owner, route, [&] { return receiving; }, [&] { return now; },
                    [&](unsigned ms) { now += ms; ++pauses; }, [&] { ++feeds; },
                    [&] { ++applies; return applied; },
                    [&] { assert(!replies.empty()); ++resumes; }, emit);
    };
    invoke(b, secondary);
    assert(b.radio.custom && b.radio.starts == 1 && b.radio.standbys == 1 && b.radio.clears == 1);
    assert(a.radio.starts == 0 && ar.irqCount() == 0 && replies.size() == 1);
    assert(replies.back().command == CMD_CAD_RESP && replies.back().value == 0);
    assert(replies.back().route.tcpGeneration == 17 && resumes == 1);
    replies.clear();
    b.radio.flags = RADIOLIB_SX126X_IRQ_CAD_DONE | RADIOLIB_SX126X_IRQ_CAD_DETECTED;
    invoke(b, secondary);
    assert(replies.back().value == 1 && resumes == 2);
    replies.clear();
    invoke(b, secondary, true);
    assert(replies.back().value == 1 && b.radio.starts == 2 && resumes == 2);
    replies.clear();
    b.radio.startResult = -1;
    invoke(b, secondary);
    assert(replies.back().command == CMD_ERROR && replies.back().value == ERR_CAD_FAILED);
    assert(resumes == 3 && applies == 0 && b.radio.clears == 2);
    replies.clear();
    b.radio.startResult = 0;
    b.radio.flags = 0;
    now = 0xfffffff0u;
    applied = false; // Failed reconfiguration still replies and attempts RX.
    invoke(b, secondary);
    assert(now == static_cast<uint32_t>(0xfffffff0u + 506u));
    assert(replies.back().command == CMD_ERROR && replies.back().value == ERR_CAD_FAILED);
    assert(replies.back().route.tcpGeneration == route.tcpGeneration);
    assert(applies == 1 && resumes == 4 && feeds > 0 && pauses > 0);
    assert(b.radio.standbys == 5 && a.radio.standbys == 0);
    assert(!br.irqPending() && ar.irqCount() == 0);
    invoke(a, primary);
    assert(a.radio.starts == 1 && b.radio.starts == 4 && resumes == 5);
}
