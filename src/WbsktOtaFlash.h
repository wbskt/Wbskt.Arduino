// The ESP32 side of OTA: the app partitions, and writing a downloaded image into the idle one.
// The conformance harness links a fake of these (test/conformance/shim/OtaFlash.cpp).
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>
#include <string>

namespace wbskt_ota {

struct Partitions {
    std::string running;
    // The one the next restart boots: the running one, or one an update just wrote.
    std::string boot;
    std::string next;
    // Bytes the idle partition holds, 0 when the partition table has no OTA slot.
    size_t nextBytes = 0;
};

Partitions partitions();

using Progress = std::function<void(uint8_t percent)>;

// Downloads url into the idle partition, checks its SHA-256, and makes it the one to boot.
// Empty on success, otherwise why it failed; on failure the running firmware stays the boot one.
std::string install(const char* url, const char* rootCa, const std::string& sha256, size_t expectedBytes,
                    const Progress& progress);

// Makes the partition with this label the one to boot. False if there is none.
bool bootFrom(const std::string& label);

[[noreturn]] void restart();

}  // namespace wbskt_ota
