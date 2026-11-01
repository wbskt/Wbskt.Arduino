// A token bucket: up to `burst` messages at once, then `perSecond` sustained. The client keeps under
// the server's own limit (Docs/Device.Protocol.md §4: 10 a second, bursts of 100), which drops the
// excess silently.
#pragma once

#include <stdint.h>

namespace wbskt_core {

class TokenBucket {
public:
    TokenBucket(uint32_t perSecond, uint32_t burst) : perSecond_(perSecond), burst_(burst), milliTokens_(burst * 1000ULL) {}

    // Takes a token if one is free at nowMs (millis()).
    bool take(uint32_t nowMs);

private:
    void refill(uint32_t nowMs);

    uint32_t perSecond_;
    uint32_t burst_;
    uint64_t milliTokens_;
    uint32_t lastMs_ = 0;
    bool started_ = false;
};

}  // namespace wbskt_core
