#include "RateLimit.h"

namespace wbskt_core {

void TokenBucket::refill(uint32_t nowMs) {
    if (!started_) {
        started_ = true;
        lastMs_ = nowMs;
        return;
    }
    const uint32_t elapsed = nowMs - lastMs_;
    lastMs_ = nowMs;
    const uint64_t cap = burst_ * 1000ULL;
    milliTokens_ += static_cast<uint64_t>(elapsed) * perSecond_;
    if (milliTokens_ > cap) {
        milliTokens_ = cap;
    }
}

bool TokenBucket::take(uint32_t nowMs) {
    refill(nowMs);
    if (milliTokens_ < 1000) {
        return false;
    }
    milliTokens_ -= 1000;
    return true;
}

}  // namespace wbskt_core
