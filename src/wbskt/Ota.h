// The sys.ota.update command and the sys.ota.status frames that answer it. No Arduino or network
// code here, so it runs in the host tests; the download and flashing are in src/WbsktOta.cpp.
#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

#include <string>

namespace wbskt_core {

enum class OtaState { Accepted, Downloading, Installed, Failed, Rejected };

struct OtaRequest {
    std::string url;
    std::string sha256;
    std::string version;
    // Bytes, or 0 when the command didn't say.
    size_t size = 0;
};

// Reads the payload of sys.ota.update. Empty when it can be carried out, otherwise why not.
// slotBytes is the size of the idle app partition, 0 when there is none.
std::string readOtaRequest(JsonVariantConst payload, size_t slotBytes, OtaRequest& out);

// progress < 0 leaves it out; null or empty strings are left out too.
std::string encodeOtaStatus(const char* commandId, OtaState state, int progress = -1, const char* version = nullptr,
                            const char* error = nullptr);

const char* otaStateName(OtaState state);

std::string toHex(const uint8_t* bytes, size_t length);

// Two hex digests, compared without regard to case.
bool sameDigest(const std::string& a, const std::string& b);

}  // namespace wbskt_core
