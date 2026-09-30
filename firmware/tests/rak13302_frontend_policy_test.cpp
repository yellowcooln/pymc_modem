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
    // Same numeric ID must not let an impostor clear the owner's TX grant.
    {
        Gpio ownerGpio, duplicateGpio, peerGpio;
        TxArbiter shared;
        FrontEnd<Gpio> owner(0, 27, ownerGpio, shared);
        FrontEnd<Gpio> duplicate(0, 19, duplicateGpio, shared);
        FrontEnd<Gpio> peer(1, 19, peerGpio, shared);
        assert(owner.begin() && peer.begin());
        assert(owner.begin() && ownerGpio.writes.size() == 1); // idempotent
        assert(!duplicate.begin() && duplicateGpio.writes.empty());
        assert(owner.setSupplyReady(true) && peer.setSupplyReady(true));
        assert(owner.transmit() && shared.owner() == 0);
        assert(!duplicate.shutdown() && !duplicate.finishTransmit());
        assert(shared.owner() == 0 && !peer.transmit());
        assert(owner.finishTransmit() && shared.owner() == -1);
        assert(peer.transmit() && peer.finishTransmit());
    }
    // A controller destroyed while granted cannot silently free the interlock.
    {
        TxArbiter shared;
        Gpio a, b;
        FrontEnd<Gpio> peer(1, 19, b, shared);
        assert(peer.begin() && peer.setSupplyReady(true));
        {
            FrontEnd<Gpio> abandoned(0, 27, a, shared);
            assert(abandoned.begin() && abandoned.setSupplyReady(true));
            assert(abandoned.transmit());
        }
        FrontEnd<Gpio> replacement(0, 27, a, shared);
        assert(!replacement.begin() && !peer.transmit());
        assert(shared.owner() == 0);
    }
    {
    Gpio a, b;
    TxArbiter arbiter;
    FrontEnd<Gpio> first(0, 27, a, arbiter), second(1, 19, b, arbiter);
    assert(!first.receive() && !first.transmit());
    assert(!first.setSupplyReady(true));
    assert(first.begin() && second.begin());
    assert(first.begin() && first.state() == State::Off);
    assert(a.writes.size() == 1 && !a.writes[0].level);
    assert(b.writes.size() == 1 && !b.writes[0].level);
    assert(!first.transmit() && !second.receive()); // rail unverified
    assert(first.setSupplyReady(true) && second.setSupplyReady(true));
    assert(first.receive() && second.receive());
    assert(first.transmit() && arbiter.owner() == 0);
    const auto before = b.writes.size();
    assert(!second.transmit() && b.writes.size() == before);
    assert(arbiter.owner() == 0);
    const auto txWrites = a.writes.size();
    assert(!first.shutdown() && arbiter.owner() == 0);
    assert(first.state() == State::Transmit && a.writes.size() == txWrites);
    assert(!first.receive() && arbiter.owner() == 0);
    assert(first.finishTransmit() && first.state() == State::Receive && arbiter.owner() == -1);
    assert(a.writes.size() == txWrites && a.writes.back().level); // RX retains HIGH
    assert(first.shutdown());
    assert(second.transmit() && arbiter.owner() == 1);
    const auto activeWrites = b.writes.size();
    assert(!second.setSupplyReady(false) && arbiter.owner() == 1);
    assert(b.writes.size() == activeWrites && second.state() == State::Fault);
    assert(!second.setSupplyReady(true) && !second.supplyReady());
    assert(!first.transmit() && !second.shutdown());
    assert(second.finishTransmit() && arbiter.owner() == -1);
    assert(second.state() == State::Off && !second.transmit());
    assert(!second.supplyReady());
    assert(first.setSupplyReady(false) && !first.receive());
    for (auto w : a.writes) assert(w.pin == 27);
    for (auto w : b.writes) assert(w.pin == 19);
    }

    // Invalid ID/pin and failed initialization never touch a pin or grant TX.
    {
    TxArbiter arbiter;
    Gpio bad;
    FrontEnd<Gpio> invalidId(2, 27, bad, arbiter), invalidPin(0, -1, bad, arbiter);
    FrontEnd<Gpio> unowned(1, 99, bad, arbiter);
    assert(!invalidId.begin() && !invalidPin.begin() && !unowned.begin());
    assert(bad.writes.empty() && arbiter.owner() == -1);
    bad.failConfigure = true;
    FrontEnd<Gpio> failedBegin(0, 27, bad, arbiter);
    assert(!failedBegin.begin() && !failedBegin.setSupplyReady(true));
    assert(bad.writes.empty());
    bad.failConfigure = false;
    assert(failedBegin.begin()); // failed registration/configuration is retryable
    }

    // Failed HIGH: attempt LOW and release only if LOW succeeds.
    {
    TxArbiter arbiter;
    Gpio c, d;
    FrontEnd<Gpio> third(0, 27, c, arbiter), fourth(1, 19, d, arbiter);
    assert(third.begin() && fourth.begin());
    assert(third.setSupplyReady(true) && fourth.setSupplyReady(true));
    c.failPin = 27; c.failLevel = 1;
    assert(!third.receive() && third.state() == State::Off && arbiter.owner() == -1);
    assert(!third.transmit() && arbiter.owner() == -1 && third.state() == State::Off);
    assert(fourth.transmit());
    assert(fourth.finishTransmit() && fourth.shutdown());
    // Failed LOW during shutdown retains the grant and latches Fault.
    c.failPin = -1;
    assert(third.transmit());
    c.failPin = 27; c.failLevel = 0;
    assert(!third.shutdown() && third.state() == State::Transmit);
    assert(!third.setSupplyReady(false) && third.state() == State::Fault);
    assert(!third.finishTransmit() && third.state() == State::Fault);
    assert(arbiter.owner() == 0 && !fourth.transmit());
    assert(!third.receive() && !third.transmit());
    c.failPin = -1;
    assert(third.shutdown() && arbiter.owner() == -1);
    assert(third.setSupplyReady(true) && fourth.transmit());
    assert(fourth.finishTransmit() && fourth.shutdown());
    }
    // Failed HIGH and LOW together also retains the grant.
    TxArbiter arbiter;
    Gpio d;
    FrontEnd<Gpio> fourth(1, 19, d, arbiter);
    assert(fourth.begin() && fourth.setSupplyReady(true));
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
    assert(fourth.transmit() && fourth.finishTransmit() && fourth.shutdown());
}
