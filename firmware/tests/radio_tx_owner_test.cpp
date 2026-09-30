#include <cstdint>
#define RADIOLIB_ERR_NONE 0
#define RADIOLIB_SX126X_IRQ_CAD_DONE 1
#define RADIOLIB_SX126X_IRQ_CAD_DETECTED 2
#define RADIOLIB_IRQ_CAD_DEFAULT_FLAGS 3
#define RADIOLIB_IRQ_CAD_DEFAULT_MASK 4
struct ChannelScanConfig_t {
    struct { uint8_t symNum, detPeak, detMin, exitMode; uint32_t irqFlags, irqMask; } cad;
};
#include "radio_tx_owner.h"
#include <cassert>
#include <vector>
#include <string>
struct Radio {
    int standbyCount = 0, scanCount = 0, startCount = 0, finishCount = 0, clears = 0;
    int scanResult = 0, startResult = 0;
    std::vector<uint16_t> scans{RADIOLIB_SX126X_IRQ_CAD_DONE};
    uint16_t current = 0;
    bool tx = false;
    uint32_t airtime = 0x12345678;
    ChannelScanConfig_t config{};
    int standby() { ++standbyCount; return 0; }
    int startChannelScan(ChannelScanConfig_t c) {
        config = c; ++scanCount; current = scans.at(scanCount - 1); return scanResult;
    }
    uint16_t getIrqFlags() { return current; }
    void clearIrqFlags(uint16_t flags) {
        assert(flags == (RADIOLIB_SX126X_IRQ_CAD_DONE | RADIOLIB_SX126X_IRQ_CAD_DETECTED));
        ++clears;
    }
    int startTransmit(uint8_t* data, uint16_t len) {
        assert(data && len); ++startCount; tx = true; return startResult;
    }
    int finishTransmit() { ++finishCount; tx = false; return 0; }
    uint32_t getTimeOnAir(uint16_t) { return airtime; }
};
struct Hardware { Radio radio; };
struct Reply { uint8_t cmd; std::vector<uint8_t> data; uint32_t generation; };
struct Fixture {
    Hardware a, b;
    RadioConfigState ac, bc;
    RadioRuntimeState ar, br;
    StatusResp as{}, bs{};
    RadioCommandContext primary{0, 0, ac, ar, as};
    RadioCommandContext secondary{1, 1, bc, br, bs};
    uint32_t now = 100;
    int feeds = 0, resumes = 0, prepares = 0, ledOn = 0, ledOff = 0, reapplies = 0, completes = 0;
    bool receiving = false, receivingDuringCad = false, applyResult = true;
    int receptionAtCheck = 2;
    bool triggerTxIrq = true;
    std::vector<Reply> replies;
    std::vector<std::string> events;
    void run(Hardware& hw, RadioCommandContext& owner, uint16_t len = 4,
             ResponseRoute route = {TransportSource::TCP, nullptr, 17}) {
        uint8_t payload[4] = {1, 2, 3, 4};
        int checks = 0;
        runRadioTx(hw, owner, payload, len, route,
            [&] { ++checks; return checks == 1 ? receiving : (receivingDuringCad && checks == receptionAtCheck); },
            [&] { return now; }, [&] { return now * 1000u + 17; },
            [&](unsigned ms) { now += ms; events.push_back("pause"); },
            [&] { ++feeds; if (triggerTxIrq && hw.radio.tx) owner.runtime.onDio1Rise(); },
            [&] { ++prepares; events.push_back("prepare"); },
            [&](bool on) { if (on) ++ledOn; else ++ledOff; },
            [&] { ++reapplies; return applyResult; },
            [&] { ++resumes; events.push_back("resume"); },
            [&] { ++completes; },
            [&](RadioTxEvent, int, uint32_t) {},
            [&](uint8_t cmd, const uint8_t* bytes, uint16_t n, ResponseRoute r) {
                replies.push_back({cmd, bytes ? std::vector<uint8_t>(bytes, bytes+n) : std::vector<uint8_t>{}, r.tcpGeneration});
                events.push_back("reply");
            });
    }
    void error(uint8_t code) { assert(replies.size() == 1 && replies[0].cmd == CMD_ERROR && replies[0].data == std::vector<uint8_t>{code}); }
};
int main() {
    Fixture f;
    f.receiving = true;
    f.run(f.b, f.secondary);
    f.error(ERR_CHANNEL_BUSY);
    assert(f.b.radio.standbyCount == 0 && f.b.radio.startCount == 0 && f.resumes == 0 && !f.br.txActive);
    f.receiving = false; f.replies.clear(); f.events.clear();
    f.run(f.b, f.secondary, 0);
    f.error(ERR_PAYLOAD_TOO_BIG);
    assert(f.b.radio.startCount == 0 && f.resumes == 0);
    f.replies.clear();
    f.br.cad.autoEnabled = true;
    uint8_t cad[4] = {4, 25, 9, 2}; f.br.cad.setParams(cad);
    f.b.radio.scans = {3, 3};
    f.run(f.b, f.secondary);
    f.error(ERR_CHANNEL_BUSY);
    assert(f.b.radio.scanCount == 2 && f.b.radio.startCount == 0 && f.resumes == 1);
    assert(f.b.radio.config.cad.symNum == 4 && f.b.radio.config.cad.detPeak == 25 && f.b.radio.config.cad.detMin == 9 && f.b.radio.config.cad.exitMode == 2);
    assert(f.b.radio.config.cad.irqFlags == RADIOLIB_IRQ_CAD_DEFAULT_FLAGS && f.b.radio.config.cad.irqMask == RADIOLIB_IRQ_CAD_DEFAULT_MASK);
    assert(f.feeds == 0 && !f.br.txActive && f.ar.irqCount() == 0 && f.a.radio.startCount == 0);
    f.replies.clear(); f.b.radio.scanCount = 0; f.b.radio.scans = {3, 3}; f.receivingDuringCad = true;
    f.run(f.b, f.secondary);
    f.error(ERR_CHANNEL_BUSY);
    assert(f.b.radio.scanCount == 0 && f.resumes == 1); // passive guard never restarts RX
    f.replies.clear(); f.receptionAtCheck = 3; f.b.radio.scans = {3, 3};
    f.run(f.b, f.secondary);
    f.error(ERR_CHANNEL_BUSY);
    assert(f.b.radio.scanCount == 1 && f.resumes == 1); // retry sees live RX
    f.receivingDuringCad = false; f.replies.clear();
    f.b.radio.scanResult = -1; f.run(f.b, f.secondary);
    f.error(ERR_CHANNEL_BUSY); assert(f.resumes == 2);
    f.b.radio.scanResult = 0; f.b.radio.scans = {0}; f.b.radio.scanCount = 0; f.replies.clear();
    f.run(f.b, f.secondary);
    f.error(ERR_CHANNEL_BUSY); assert(f.feeds > 0 && f.resumes == 3);
    f.br.cad.autoEnabled = false; f.replies.clear(); f.events.clear();
    f.b.radio.startResult = -1; f.run(f.b, f.secondary);
    f.error(ERR_TX_TIMEOUT);
    assert(f.b.radio.finishCount == 1 && f.ledOn == 1 && f.ledOff == 1 && f.resumes == 4 && f.br.noise.lastPacketMs == 0);
    f.b.radio.startResult = 0; f.replies.clear(); f.events.clear(); f.triggerTxIrq = false;
    f.now = 0xfffffff0u; int before = f.feeds;
    f.applyResult = false; // failed reapply still reports timeout and resumes RX
    // timeout uses modular arithmetic; a prior RX DIO1 must not complete TX.
    f.br.onDio1Rise();
    f.run(f.b, f.secondary);
    f.error(ERR_TX_TIMEOUT);
    assert(f.feeds - before >= 2250 && f.reapplies == 1 && f.resumes == 5);
    assert(f.br.noise.lastPacketMs == static_cast<uint32_t>(f.now - 5) && f.completes == 1 && !f.br.irqPending());
    assert(f.events[f.events.size()-2] == "reply" && f.events.back() == "resume");
    f.replies.clear(); f.events.clear(); f.triggerTxIrq = true;
    // Inject TX IRQ through the watchdog hook after startTransmit.
    f.run(f.b, f.secondary, 4, {TransportSource::TCP, nullptr, 29});
    assert(f.replies.size() == 1 && f.replies[0].cmd == CMD_TX_DONE);
    assert(f.replies[0].data == (std::vector<uint8_t>{0x78,0x56,0x34,0x12}) && f.replies[0].generation == 29);
    assert(f.bs.tx_count == 1 && f.as.tx_count == 0 && f.completes == 2);
    assert(f.events[f.events.size()-2] == "reply" && f.events.back() == "resume");
    f.replies.clear(); f.run(f.a, f.primary);
    assert(f.a.radio.startCount == 1 && f.as.tx_count == 1 && f.bs.tx_count == 1);
    assert(f.a.radio.scanCount == 0 && !f.ar.txActive && !f.br.txActive);
    Fixture clear;
    clear.br.cad.autoEnabled = true;
    clear.b.radio.scans = {RADIOLIB_SX126X_IRQ_CAD_DONE};
    clear.run(clear.b, clear.secondary);
    assert(clear.b.radio.scanCount == 1 && clear.b.radio.startCount == 1);
    assert(clear.replies.size() == 1 && clear.replies[0].cmd == CMD_TX_DONE);
    assert(clear.as.tx_count == 0 && clear.bs.tx_count == 1 && clear.a.radio.startCount == 0);
    assert(clear.resumes == 1 && clear.prepares == 1 && clear.reapplies == 0);
    assert(!clear.br.txActive && !clear.ar.txActive);
}
