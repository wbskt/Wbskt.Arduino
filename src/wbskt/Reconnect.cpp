#include "Reconnect.h"

namespace wbskt_core {

uint32_t ReconnectPolicy::firstDelay(bool serverRestarting) const {
    return random_(serverRestarting ? kRestartSpreadMs : kSpreadMs);
}

uint32_t ReconnectPolicy::retryDelay(uint32_t failures) const {
    uint32_t ceiling = kBaseMs;
    for (uint32_t i = 0; i < failures && ceiling < kCapMs; ++i) {
        ceiling *= 2;
    }
    return random_(ceiling < kCapMs ? ceiling : kCapMs);
}

uint32_t ReconnectPolicy::afterThrottle(uint32_t retryAfterMs) const {
    return retryAfterMs + random_(kBaseMs);
}

}  // namespace wbskt_core
