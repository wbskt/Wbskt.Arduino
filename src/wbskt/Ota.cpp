#include "Ota.h"

#include <ctype.h>
#include <strings.h>

#include "Protocol.h"

namespace wbskt_core {

namespace {

bool isHexDigest(const std::string& text) {
    if (text.size() != kSha256HexLength) {
        return false;
    }
    for (const char c : text) {
        if (!isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

}  // namespace

std::string readOtaRequest(JsonVariantConst payload, size_t slotBytes, OtaRequest& out) {
    out = OtaRequest();
    const char* url = payload["url"];
    const char* sha256 = payload["sha256"];
    if (url == nullptr || *url == '\0') {
        return "url is missing";
    }
    if (strncasecmp(url, "https://", 8) != 0 || url[8] == '\0') {
        return "url must start with https://";
    }
    if (sha256 == nullptr || !isHexDigest(sha256)) {
        return "sha256 must be 64 hex digits";
    }
    if (slotBytes == 0) {
        return "this build has no OTA partition; pick a partition scheme with OTA";
    }
    JsonVariantConst size = payload["size"];
    if (!size.isNull()) {
        if (!size.is<uint32_t>()) {
            return "size must be a whole number of bytes";
        }
        out.size = size.as<uint32_t>();
        if (out.size > slotBytes) {
            return "the image is " + std::to_string(out.size) + " bytes and the OTA partition holds " +
                   std::to_string(slotBytes);
        }
    }
    out.url = url;
    out.sha256 = sha256;
    out.version = payload["version"] | "";
    return "";
}

const char* otaStateName(OtaState state) {
    switch (state) {
        case OtaState::Accepted:
            return "accepted";
        case OtaState::Downloading:
            return "downloading";
        case OtaState::Installed:
            return "installed";
        case OtaState::Failed:
            return "failed";
        default:
            return "rejected";
    }
}

std::string encodeOtaStatus(const char* commandId, OtaState state, int progress, const char* version,
                            const char* error) {
    JsonDocument doc;
    doc["type"] = kOtaStatus;
    JsonObject payload = doc["payload"].to<JsonObject>();
    if (commandId != nullptr && *commandId != '\0') {
        payload["commandId"] = commandId;
    }
    payload["state"] = otaStateName(state);
    if (progress >= 0) {
        payload["progress"] = progress > 100 ? 100 : progress;
    }
    if (version != nullptr && *version != '\0') {
        payload["version"] = version;
    }
    if (error != nullptr && *error != '\0') {
        payload["error"] = error;
    }
    std::string out;
    serializeJson(doc, out);
    return out;
}

std::string toHex(const uint8_t* bytes, size_t length) {
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) {
        out += kDigits[bytes[i] >> 4];
        out += kDigits[bytes[i] & 0x0F];
    }
    return out;
}

bool sameDigest(const std::string& a, const std::string& b) {
    return a.size() == b.size() && strncasecmp(a.c_str(), b.c_str(), a.size()) == 0;
}

}  // namespace wbskt_core
