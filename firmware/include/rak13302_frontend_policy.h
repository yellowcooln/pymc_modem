#pragma once

#include <stdint.h>

// Dormant RAK13302 policy. Not connected to any board's GPIO or radio path.
// ANT_SW (SKY66122 CSD/CPS, tied together) is high in both RX and TX;
// SX1262 DIO2/CTX selects the path. Supply readiness must be verified by
// board-specific hardware code before it is reported here. In particular,
// an E22P RF_EN pin is NOT a RAK13302 supply-ready signal.
namespace Rak13302 {

enum class State : uint8_t { Off, Receive, Transmit, Fault };
template <class Gpio> class FrontEnd;

// Single cooperative owner for two radios. Callers must serialize access;
// this is not an IRQ-safe mutex. It prevents simultaneous TX grants only:
// it does NOT protect the other receiver from near-field TX overload.
class TxArbiter {
public:
    TxArbiter() = default;
    TxArbiter(const TxArbiter&) = delete;
    TxArbiter& operator=(const TxArbiter&) = delete;
    int owner() const { return owner_; }

private:
    template <class Gpio> friend class FrontEnd;
    bool registerController(uint8_t radio, const void* controller) {
        if (radio >= 2 || registered_[radio]) return false;
        registered_[radio] = controller;
        return true;
    }
    void unregisterController(uint8_t radio, const void* controller) {
        // An abandoned active grant must remain latched (fail closed).
        if (radio < 2 && registered_[radio] == controller && ownerToken_ != controller)
            registered_[radio] = nullptr;
    }
    bool owns(uint8_t radio, const void* controller) const {
        return radio < 2 && owner_ == radio && ownerToken_ == controller &&
               registered_[radio] == controller;
    }
    bool acquire(uint8_t radio, const void* controller) {
        if (radio >= 2 || registered_[radio] != controller || owner_ != -1) return false;
        owner_ = radio;
        ownerToken_ = controller;
        return true;
    }
    bool release(uint8_t radio, const void* controller) {
        if (!owns(radio, controller)) return false;
        owner_ = -1;
        ownerToken_ = nullptr;
        return true;
    }
    const void* registered_[2] = {nullptr, nullptr};
    const void* ownerToken_ = nullptr;
    int owner_ = -1;
};

// GPIO contract: valid(pin), configureLow(pin) must latch LOW before enabling
// output, write(pin, level) returns true only when the operation succeeded.
// On a failed LOW write, latch Fault and retain any TX grant: another TX must
// not be admitted until the output is successfully driven LOW.
template <class Gpio>
class FrontEnd {
public:
    FrontEnd(uint8_t radio, int antSwPin, Gpio& gpio, TxArbiter& arbiter)
        : radio_(radio), pin_(antSwPin), gpio_(gpio), arbiter_(arbiter) {}
    FrontEnd(const FrontEnd&) = delete;
    FrontEnd& operator=(const FrontEnd&) = delete;
    ~FrontEnd() { if (initialized_) arbiter_.unregisterController(radio_, this); }

    bool begin() {
        if (initialized_) return true;
        if (radio_ >= 2 || pin_ < 0 || !gpio_.valid(pin_) ||
            !arbiter_.registerController(radio_, this)) return false;
        if (!gpio_.configureLow(pin_)) {
            arbiter_.unregisterController(radio_, this);
            return false;
        }
        initialized_ = true;
        state_ = State::Off;
        return true;
    }

    // This method does not turn on or test the supply. The caller must
    // establish the RAK13302 rails and settling independently.
    bool setSupplyReady(bool ready) {
        if (!initialized_) return false;
        if (!ready) {
            supplyReady_ = false;
            // Loss of a rail assertion cannot prove that the SX1262 stopped.
            // Keep ANT_SW and the TX interlock until explicit acknowledgement.
            if (txActive_) {
                state_ = State::Fault;
                return false;
            }
            return shutdown();
        }
        if (state_ == State::Fault) return false;
        supplyReady_ = true;
        return true;
    }

    bool receive() {
        if (!initialized_ || !supplyReady_ || state_ == State::Fault ||
            state_ == State::Transmit) return false;
        if (!gpio_.write(pin_, true)) return failHigh();
        state_ = State::Receive;
        return true;
    }

    bool transmit() {
        if (!initialized_ || !supplyReady_ || state_ == State::Fault ||
            state_ == State::Transmit || !arbiter_.acquire(radio_, this)) return false;
        if (!gpio_.write(pin_, true)) return failHigh();
        state_ = State::Transmit;
        txActive_ = true;
        return true;
    }

    // Acknowledge only AFTER RadioLib finishTransmit()/standby succeeds and
    // the SX1262 can no longer transmit. This method does not stop the radio.
    // On a healthy supply RX keeps ANT_SW HIGH; after supply loss drive LOW.
    bool finishTransmit() {
        if (!initialized_ || !txActive_ || !arbiter_.owns(radio_, this)) return false;
        txActive_ = false; // failed LOW may now be retried via shutdown()
        if (!supplyReady_ || state_ == State::Fault) return shutdown();
        state_ = State::Receive;
        return arbiter_.release(radio_, this);
    }

    // Normal shutdown cannot stop an active SX1262 TX. Never relinquish TX
    // ownership when a LOW write fails.
    bool shutdown() {
        if (!initialized_ || txActive_) return false;
        if (!gpio_.write(pin_, false)) {
            state_ = State::Fault;
            return false;
        }
        state_ = State::Off;
        if (arbiter_.owns(radio_, this)) arbiter_.release(radio_, this);
        return true;
    }

    State state() const { return state_; }
    bool supplyReady() const { return supplyReady_; }
    int pin() const { return pin_; }

private:
    bool failHigh() {
        // A failed HIGH write can leave the pin indeterminate. Keep the
        // interlock if the subsequent safe LOW write also fails.
        shutdown();
        return false;
    }

    uint8_t radio_;
    int pin_;
    Gpio& gpio_;
    TxArbiter& arbiter_;
    bool initialized_ = false;
    bool supplyReady_ = false;
    bool txActive_ = false;
    State state_ = State::Off;
};

}  // namespace Rak13302
