// Force-included in the build over the real WebSockets library: what the ESP32 Arduino core 3.x
// defines and the library expects without including anything.
#pragma once

#include <Arduino.h>

#define ESP_ARDUINO_VERSION_VAL(major, minor, patch) (((major) << 16) | ((minor) << 8) | (patch))
#define ESP_ARDUINO_VERSION ESP_ARDUINO_VERSION_VAL(3, 1, 0)

#define bit(b) (1UL << (b))

inline void yield() {}
inline void randomSeed(unsigned long seed) { srandom(static_cast<unsigned int>(seed)); }
inline long random(long howBig) { return howBig <= 0 ? 0 : ::random() % howBig; }
inline long random(long howSmall, long howBig) { return howSmall >= howBig ? howSmall : howSmall + random(howBig - howSmall); }

// The library has begin() overloads taking one; the SDK never calls them.
class IPAddress {
public:
    String toString() const { return String("0.0.0.0"); }
};
