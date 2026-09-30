#include "rak13302_frontend_policy.h"
#include <cassert>
#include <vector>

using namespace Rak13302;
struct Write { int pin; bool level; };
struct Gpio {
    std::vector<Write> writes;
    int failPin = -1;
    int failLevel = -1;
    bool failConfigure = false;
    bool valid(int pin) { return pin == 27 || pin == 19; }
    bool configureLow(int pin) {
        if (failConfigure) return false;
        writes.push_back({pin, false}); // latch low before enabling output
        return true;
    }
    bool write(int pin, bool level) {
        if (pin == failPin && static_cast<int>(level) == failLevel) return false;
        writes.push_back({pin, level});
        return true;
    }
};

int main() {
    Gpio a, b;
    TxArbiter arbiter;
    FrontEnd<Gpio> first(0, 27, a, arbiter), second(1, 19, b, arbiter);
    assert(!first.receive() && !first.transmit());
    assert(!first.setSupplyReady(true));
    assert(first.begin() && second.begin());
    assert(!first.begin() && first.state() == State::Off);
    assert(a.writes.size() == 1 && !a.writes[0].level);
    assert(b.writes.size() == 1 && !b.writes[0].level);
    assert(!first.transmit() && !second.receive()); // rail unverified
    assert(first.setSupplyReady(true) && second.setSupplyReady(true));
    assert(first.receive() && second.receive());
    assert(first.transmit() && arbiter.owner() == 0);
    const auto before = b.writes.size();
    assert(!second.transmit() && b.writes.size() == before);
    assert(!arbiter.release(1) && !arbiter.acquire(0));
    assert(first.shutdown() && arbiter.owner() == -1);
    assert(second.transmit() && arbiter.owner() == 1);
    assert(second.setSupplyReady(false) && arbiter.owner() == -1);
    assert(second.state() == State::Off && !second.transmit());
    assert(!second.supplyReady());
    assert(first.setSupplyReady(false) && !first.receive());
    for (auto w : a.writes) assert(w.pin == 27);
    for (auto w : b.writes) assert(w.pin == 19);

    // Invalid ID/pin and failed initialization never touch a pin or grant TX.
    Gpio bad;
    FrontEnd<Gpio> invalidId(2, 27, bad, arbiter), invalidPin(0, -1, bad, arbiter);
    FrontEnd<Gpio> unowned(1, 99, bad, arbiter);
    assert(!invalidId.begin() && !invalidPin.begin() && !unowned.begin());
    assert(bad.writes.empty() && arbiter.owner() == -1);
    bad.failConfigure = true;
    FrontEnd<Gpio> failedBegin(0, 27, bad, arbiter);
    assert(!failedBegin.begin() && !failedBegin.setSupplyReady(true));
    assert(bad.writes.empty());

    // Failed HIGH: attempt LOW and release only if LOW succeeds.
    Gpio c, d;
    FrontEnd<Gpio> third(0, 27, c, arbiter), fourth(1, 19, d, arbiter);
    assert(third.begin() && fourth.begin());
    assert(third.setSupplyReady(true) && fourth.setSupplyReady(true));
    c.failPin = 27; c.failLevel = 1;
    assert(!third.receive() && third.state() == State::Off && arbiter.owner() == -1);
    assert(!third.transmit() && arbiter.owner() == -1 && third.state() == State::Off);
    assert(fourth.transmit());
    assert(fourth.shutdown());
    // Failed LOW during shutdown retains the grant and latches Fault.
    c.failPin = -1;
    assert(third.transmit());
    c.failPin = 27; c.failLevel = 0;
    assert(!third.shutdown() && third.state() == State::Fault);
    assert(arbiter.owner() == 0 && !fourth.transmit());
    assert(!third.receive() && !third.transmit());
    c.failPin = -1;
    assert(third.shutdown() && arbiter.owner() == -1);
    assert(third.setSupplyReady(true) && fourth.transmit());
    assert(fourth.shutdown());
    // Failed HIGH and LOW together also retains the grant.
    struct FailBoth : Gpio {
        bool failAll = false;
        bool write(int pin, bool level) {
            if (failAll) return false;
            return Gpio::write(pin, level);
        }
    } e;
    FrontEnd<FailBoth> fifth(0, 27, e, arbiter);
    assert(fifth.begin() && fifth.setSupplyReady(true));
    e.failAll = true;
    assert(!fifth.transmit() && fifth.state() == State::Fault && arbiter.owner() == 0);
    assert(!fourth.transmit());
    e.failAll = false;
    assert(fifth.shutdown() && arbiter.owner() == -1);
    assert(fourth.transmit() && fourth.shutdown());
}
