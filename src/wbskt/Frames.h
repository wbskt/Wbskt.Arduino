// Turning messages into protocol frames and frames into what the client should do. No Arduino or
// network code here, so it runs in the host tests.
#pragma once

#include <ArduinoJson.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace wbskt_core {

enum class DataType { String, Number, Boolean, Object };

struct Parameter {
    std::string name;
    DataType dataType;
    bool required;
    std::string label;
    std::string description;
};

struct Command {
    std::string name;
    std::string description;
    std::vector<Parameter> parameters;
};

struct DeviceInfo {
    std::string agent;
    std::string version;
    std::string os;
};

// An application message. sentAt is left out when the clock isn't set, and the server uses
// its arrival time instead.
std::string encodeMessage(const char* type, JsonVariantConst payload, int64_t nowSeconds, uint16_t nowMillis);
std::string encodeCapabilities(const DeviceInfo& device, const std::vector<Command>& commands);

enum class Action {
    Ignore,   // not a message, or a platform message the device has nothing to do for
    Reply,    // send `reply` and stop (a pong, or a refusal of an expired command)
    Deliver,  // send `reply` if not empty (the ack), then hand the message to the sketch
};

struct Incoming {
    Action action = Action::Ignore;
    std::string reply;
    JsonDocument document;  // owns what type, payload and commandId point into
    const char* type = nullptr;
    const char* commandId = nullptr;
    JsonVariantConst payload;
};

// What to do with one text frame from the server. nowSeconds < kClockValidAfter means the clock
// isn't set, and then no command is refused as expired.
void readFrame(const char* data, size_t length, int64_t nowSeconds, Incoming& out);

}  // namespace wbskt_core
