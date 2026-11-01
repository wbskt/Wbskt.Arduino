#include "Frames.h"

#include <ctype.h>
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
    const std::string field = ",\"sentAt\":\"" +
                              formatIsoTime(epochMillis / 1000, static_cast<uint16_t>(epochMillis % 1000)) + "\"";
    frame.insert(frame.size() - 1, field);
}

std::string encodeCapabilities(const DeviceInfo& device, const std::vector<Command>& commands,
                               const std::vector<std::string>& system) {
    JsonDocument doc;
    doc["type"] = kCapabilities;
    JsonObject payload = doc["payload"].to<JsonObject>();
    payload["agent"] = device.agent;
    payload["version"] = device.version;
    payload["os"] = device.os;
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
