// UTC instants as the server writes them ("2026-10-31T12:00:00.1234567Z") and as devices send them.
#pragma once

#include <stdint.h>
#include <time.h>

#include <string>

namespace wbskt_core {

// Seconds since 1970, or -1 when the text isn't an ISO 8601 date and time. No zone means UTC.
int64_t parseIsoTime(const char* text);

// "2026-10-31T12:00:00.123Z" for epoch seconds plus milliseconds.
std::string formatIsoTime(int64_t epochSeconds, uint16_t millis);

// Before 2020 the clock hasn't been set (no NTP yet), so it can't stamp or judge times.
constexpr int64_t kClockValidAfter = 1577836800;

}  // namespace wbskt_core
