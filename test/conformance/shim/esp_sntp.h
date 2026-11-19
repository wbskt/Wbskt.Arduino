#pragma once

// The host has its own clock: SNTP is never running, and the library's configTime does nothing.
inline bool esp_sntp_enabled() { return false; }
