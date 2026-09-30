#pragma once

#include <cstddef>
#include <cstdint>
#include "radio_command_context.h"
#include "response_route.h"

// A bounded, caller-owned directory of command owners, not an SPI bus or
// listener factory. Several radios may share a bus with independent CS pins.
// Session supplies endpoint(), responseRoute() and isReady(); the latter
// must include transport authentication. No wire format depends on this type.
template <typename Session, size_t Capacity>
class RadioEndpointRegistry {
    static_assert(Capacity > 0, "at least one endpoint required");
public:
    bool add(uint8_t radioId, uint8_t sessionId, uint16_t port,
             RadioCommandContext& owner, Session* session) {
        if (!port || count_ == Capacity || owner.radioId != radioId ||
            owner.sessionId != sessionId) return false;
        if (session && (session->endpoint().radio != radioId ||
                        session->endpoint().session != sessionId)) return false;
        for (size_t i = 0; i < count_; ++i) {
            const Entry& other = entries_[i];
            if (other.radioId == radioId || other.sessionId == sessionId ||
                other.port == port || other.owner == &owner ||
                (session && other.session == session) ||
                &other.owner->config == &owner.config ||
                &other.owner->runtime == &owner.runtime ||
                &other.owner->status == &owner.status) return false;
        }
        entries_[count_++] = {radioId, sessionId, port, &owner, session};
        return true;
    }

    // TCP must match both the bound session object and its current generation;
    // a forged endpoint identity or stale replacement socket cannot borrow it.
    RadioCommandContext* resolve(ResponseRoute route) const {
        if (route.source == TransportSource::TCP && route.tcp) {
            for (size_t i = 0; i < count_; ++i) {
                const Entry& e = entries_[i];
                if (e.session != route.tcp || !e.session ||
                    e.session->endpoint().radio != e.radioId ||
                    e.session->endpoint().session != e.sessionId) continue;
                if (!e.session->isReady() ||
                    e.session->responseRoute().tcpGeneration != route.tcpGeneration)
                    return nullptr;
                return e.owner;
            }
            return nullptr;
        }
        if (route.source == TransportSource::TCP || route.tcp) return nullptr;
        for (size_t i = 0; i < count_; ++i)
            if (entries_[i].radioId == 0 && entries_[i].sessionId == 0)
                return entries_[i].owner;
        return nullptr;
    }
    size_t size() const { return count_; }

private:
    struct Entry {
        uint8_t radioId;
        uint8_t sessionId;
        uint16_t port;
        RadioCommandContext* owner;
        Session* session;
    };
    Entry entries_[Capacity] = {};
    size_t count_ = 0;
};
