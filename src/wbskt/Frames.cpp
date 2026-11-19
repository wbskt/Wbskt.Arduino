#include "Frames.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

#include "IsoTime.h"
#include "Protocol.h"

namespace wbskt_core {

namespace {

bool startsWithIgnoringCase(const char* text, const char* prefix) {
    for (; *prefix != '\0'; ++text, ++prefix) {
        if (tolower(static_cast<unsigned char>(*text)) != tolower(static_cast<unsigned char>(*prefix))) {
            return false;
        }
    }
    return true;
}

bool equalsIgnoringCase(const char* a, const char* b) {
    return startsWithIgnoringCase(a, b) && strlen(a) == strlen(b);
}

const char* dataTypeName(DataType type) {
    switch (type) {
        case DataType::Number:
            return "number";
        case DataType::Boolean:
            return "boolean";
        case DataType::Object:
            return "object";
        default:
            return "string";
    }
}

std::string serialize(const JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace

bool isReserved(const char* type) {
    return startsWithIgnoringCase(type, "sys.") || equalsIgnoringCase(type, kCapabilities) ||
           equalsIgnoringCase(type, kStateReport);
}

bool isSendableType(const char* type) {
    if (type == nullptr) {
        return false;
    }
    const size_t length = strlen(type);
    return length >= 1 && length <= kMaxTypeLength && !isReserved(type);
}

bool isValidClientName(const char* name) {
    if (name == nullptr) {
        return false;
    }
    const char* start = name;
    const char* end = name + strlen(name);
    while (start < end && isspace(static_cast<unsigned char>(*start))) {
        ++start;
    }
    while (end > start && isspace(static_cast<unsigned char>(end[-1]))) {
        --end;
    }
    size_t units = 0;
    for (const char* at = start; at < end; ++at) {
        const unsigned char byte = static_cast<unsigned char>(*at);
        if ((byte & 0xC0) != 0x80) {
            // A lead byte: four-byte sequences are two UTF-16 units, everything else one.
            units += byte >= 0xF0 ? 2 : 1;
        }
    }
    return units >= 1 && units <= kMaxClientNameLength;
}

bool isFinalRegistrationCode(const char* code) {
    static const char* const kFinal[] = {"POLICY_NOT_FOUND", "REGISTRATION_PIN_REQUIRED", "CLIENT_NAME_INVALID"};
    if (code == nullptr) {
        return false;
    }
    for (const char* final : kFinal) {
        if (strcmp(code, final) == 0) {
            return true;
        }
    }
    return false;
}

bool hasNonFiniteNumber(JsonVariantConst payload) {
    if (payload.is<JsonObjectConst>()) {
        for (JsonPairConst pair : payload.as<JsonObjectConst>()) {
            if (hasNonFiniteNumber(pair.value())) {
                return true;
            }
        }
        return false;
    }
    if (payload.is<JsonArrayConst>()) {
        for (JsonVariantConst item : payload.as<JsonArrayConst>()) {
            if (hasNonFiniteNumber(item)) {
                return true;
            }
        }
        return false;
    }
    return payload.is<double>() && !isfinite(payload.as<double>());
}

UpgradeFailure classifyUpgradeFailure(const char* reason, size_t length) {
    if (reason == nullptr || length == 0) {
        return UpgradeFailure::Unknown;
    }
    const std::string text(reason, length);
    const size_t status = text.find("HTTP ");
    if (status == std::string::npos) {
        return UpgradeFailure::Other;
    }
    const std::string code = text.substr(status + 5, 3);
    return code == "401" && (text.size() == status + 8 || !isdigit(static_cast<unsigned char>(text[status + 8])))
               ? UpgradeFailure::TokenRefused
               : UpgradeFailure::Other;
}

int upgradeStatus(const char* reason, size_t length) {
    if (reason == nullptr) {
        return 0;
    }
    const std::string text(reason, length);
    const size_t at = text.find("HTTP ");
    if (at == std::string::npos || text.size() < at + 8) {
        return 0;
    }
    int status = 0;
    for (size_t i = at + 5; i < at + 8; ++i) {
        if (!isdigit(static_cast<unsigned char>(text[i]))) {
            return 0;
        }
        status = status * 10 + (text[i] - '0');
    }
    return text.size() == at + 8 || !isdigit(static_cast<unsigned char>(text[at + 8])) ? status : 0;
}

bool answerKeepsNewFirmware(int status) {
    return (status >= 200 && status < 300) || status == 429 || status >= 500;
}

std::string encodeMessage(const char* type, JsonVariantConst payload) {
    JsonDocument doc;
    doc["type"] = type;
    doc["payload"] = payload;
    return serialize(doc);
}

void stampSentAt(std::string& frame, int64_t epochMillis) {
    if (frame.empty() || frame.back() != '}' || epochMillis < 0) {
        return;
    }
    // Already stamped: the field is the envelope's last, with nothing closing between it and the end.
    const size_t stamped = frame.rfind(",\"sentAt\":\"");
    if (stamped != std::string::npos && frame.find('}', stamped) == frame.size() - 1) {
        return;
    }
    const std::string field = ",\"sentAt\":\"" +
                              formatIsoTime(epochMillis / 1000, static_cast<uint16_t>(epochMillis % 1000)) + "\"";
    frame.insert(frame.size() - 1, field);
}

std::string encodeCapabilities(const ClientInfo& client, const std::vector<Command>& commands,
                               const std::vector<std::string>& system) {
    JsonDocument doc;
    doc["type"] = kCapabilities;
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["agent"] = client.agent;
    payload["version"] = client.version;
    payload["os"] = client.os;
    if (!client.firmwareVersion.empty()) {
        payload["firmwareVersion"] = client.firmwareVersion;
    }
    if (!system.empty()) {
        JsonArray names = payload["system"].to<JsonArray>();
        for (const std::string& name : system) {
            names.add(name);
        }
    }
    JsonArray list = payload["capabilities"].to<JsonArray>();
    for (const Command& command : commands) {
        JsonObject entry = list.add<JsonObject>();
        entry["command"] = command.name;
        entry["description"] = command.description;
        JsonArray parameters = entry["parameters"].to<JsonArray>();
        for (const Parameter& parameter : command.parameters) {
            JsonObject p = parameters.add<JsonObject>();
            p["name"] = parameter.name;
            p["label"] = parameter.label.empty() ? parameter.name : parameter.label;
            p["dataType"] = dataTypeName(parameter.dataType);
            p["description"] = parameter.description;
            p["isRequired"] = parameter.required;
        }
    }
    return serialize(doc);
}

void readFrame(const char* data, size_t length, int64_t nowSeconds, Incoming& out) {
    out.action = Action::Ignore;
    out.reply.clear();
    out.type = nullptr;
    out.commandId = nullptr;
    out.payload = JsonVariantConst();
    out.document.clear();

    if (deserializeJson(out.document, data, length) != DeserializationError::Ok) {
        return;
    }
    const char* type = out.document["type"];
    if (type == nullptr || *type == '\0') {
        return;
    }
    JsonVariantConst payload = out.document["payload"];

    if (equalsIgnoringCase(type, kPing)) {
        JsonDocument pong;
        pong["type"] = kPong;
        pong["payload"]["originalTimestamp"] = payload["timestamp"];
        pong["payload"]["id"] = payload["id"];
        out.reply = serialize(pong);
        out.action = Action::Reply;
        return;
    }
    // The one platform message that is a command: acked and judged for expiry like any other.
    const bool system = equalsIgnoringCase(type, kOtaUpdate);
    if (isReserved(type) && !system) {
        return;
    }

    out.type = type;
    out.payload = payload;
    const char* commandId = out.document["commandId"];
    if (commandId != nullptr && *commandId != '\0') {
        out.commandId = commandId;
        JsonDocument ack;
        ack["type"] = kAck;
        ack["payload"]["commandId"] = commandId;

        const int64_t expiresAt = parseIsoTime(out.document["expiresAt"] | static_cast<const char*>(nullptr));
        if (expiresAt >= 0 && nowSeconds >= kClockValidAfter && expiresAt <= nowSeconds) {
            ack["payload"]["type"] = type;
            ack["payload"]["refused"] = kRefusedExpired;
            out.reply = serialize(ack);
            out.action = Action::Reply;
            return;
        }
        out.reply = serialize(ack);
    }
    out.action = system ? Action::System : Action::Deliver;
}

}  // namespace wbskt_core
