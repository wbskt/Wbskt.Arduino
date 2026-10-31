// When to try again after the connection drops (Docs/Device.Protocol.md §5). Every delay is drawn
// at random from zero up to a ceiling ("full jitter"), so devices that lost their socket host at the
// same moment come back spread out instead of all at once.
#pragma once

#include <stdint.h>

namespace wbskt_core {

class ReconnectPolicy {
public:
    static constexpr uint32_t kSpreadMs = 10000;
    static constexpr uint32_t kRestartSpreadMs = 30000;
    static constexpr uint32_t kBaseMs = 2000;
    static constexpr uint32_t kCapMs = 60000;

    // random(n) returns a uniform value in [0, n].
    explicit ReconnectPolicy(uint32_t (*random)(uint32_t)) : random_(random) {}

    uint32_t firstDelay(bool serverRestarting) const;
    // The wait after `failures` failed attempts in a row: up to base x 2^failures, capped.
    uint32_t retryDelay(uint32_t failures) const;
    uint32_t afterThrottle(uint32_t retryAfterMs) const;

private:
    uint32_t (*random_)(uint32_t);
};

}  // namespace wbskt_core
