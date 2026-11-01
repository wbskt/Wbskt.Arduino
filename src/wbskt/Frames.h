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

// An application message, without sentAt: that is added as it goes out (stampSentAt), so a
// message queued before the clock was set can still carry the time it was made.
std::string encodeMessage(const char* type, JsonVariantConst payload);
// Adds "sentAt" to a frame from encodeMessage. Bytes it adds at most: kSentAtBytes.
void stampSentAt(std::string& frame, int64_t epochMillis);
constexpr size_t kSentAtBytes = 37;
// system lists the platform commands the firmware takes, such as "ota"; left out when empty.
std::string encodeCapabilities(const DeviceInfo& device, const std::vector<Command>& commands,
                               const std::vector<std::string>& system = {});

enum class Action {
    Ignore,   // not a message, or a platform message the device has nothing to do for
    Reply,    // send `reply` and stop (a pong, or a refusal of an expired command)
    Deliver,  // send `reply` if not empty (the ack), then hand the message to the sketch
    System,   // send `reply` if not empty (the ack), then the SDK carries it out (sys.ota.update)
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
