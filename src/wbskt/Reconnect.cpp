#include "Reconnect.h"

#include <string.h>

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

bool parseRetryAfterMs(const char* header, uint32_t& ms) {
    constexpr uint32_t kMaxSeconds = 24UL * 60 * 60;
    if (header == nullptr) {
        return false;
    }
    // Surrounding spaces are allowed around a header value.
    const char* begin = header;
    while (*begin == ' ' || *begin == '\t') {
        ++begin;
    }
    const char* end = begin + strlen(begin);
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) {
        --end;
    }
    if (begin == end) {
        return false;
    }
    uint32_t seconds = 0;
    for (const char* c = begin; c != end; ++c) {
        if (*c < '0' || *c > '9') {
            return false;
        }
        seconds = seconds * 10 + static_cast<uint32_t>(*c - '0');
        if (seconds > kMaxSeconds) {
            seconds = kMaxSeconds;
        }
    }
    ms = seconds * 1000UL;
    return true;
}

}  // namespace wbskt_core
