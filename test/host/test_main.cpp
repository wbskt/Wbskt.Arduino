// Host tests for the parts of the SDK that don't touch the network or the ESP32.
// Build: make -C test/host ARDUINOJSON=<path to ArduinoJson/src>

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "../../src/wbskt/Frames.h"
#include "../../src/wbskt/IsoTime.h"
#include "../../src/wbskt/Outbox.h"
#include "../../src/wbskt/Protocol.h"
#include "../../src/wbskt/Reconnect.h"

namespace {

int failures = 0;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                     \
        }                                                                   \
    } while (0)

constexpr int64_t kNow = 1793448000;  // 2026-10-31T12:00:00Z

void readJson(const std::string& text, JsonDocument& doc) {
    CHECK(deserializeJson(doc, text) == DeserializationError::Ok);
}

void reservedTypes() {
    CHECK(wbskt::isReserved("sys.custom"));
    CHECK(wbskt::isReserved("SYS.ping"));
    CHECK(wbskt::isReserved("Capabilities"));
    CHECK(wbskt::isReserved("state.REPORT"));
    CHECK(!wbskt::isReserved("temperature"));
    CHECK(!wbskt::isReserved("system.boot"));
    CHECK(!wbskt::isSendableType(""));
    CHECK(!wbskt::isSendableType(nullptr));
    CHECK(!wbskt::isSendableType(std::string(101, 'x').c_str()));
    CHECK(wbskt::isSendableType(std::string(100, 'x').c_str()));
}

void isoTime() {
    CHECK(wbskt::parseIsoTime("2026-10-31T12:00:00Z") == kNow);
    CHECK(wbskt::parseIsoTime("2026-10-31T12:00:00.1234567Z") == kNow);
    CHECK(wbskt::parseIsoTime("2026-10-31T13:30:00+01:30") == kNow);
    CHECK(wbskt::parseIsoTime("2026-10-31T12:00:00") == kNow);
    CHECK(wbskt::parseIsoTime("1970-01-01T00:00:00Z") == 0);
    CHECK(wbskt::parseIsoTime("2024-02-29T00:00:00Z") == 1709164800);
    CHECK(wbskt::parseIsoTime("not a time") == -1);
    CHECK(wbskt::parseIsoTime("2026-13-01T00:00:00Z") == -1);
    CHECK(wbskt::parseIsoTime(nullptr) == -1);
    CHECK(wbskt::formatIsoTime(kNow, 42) == "2026-10-31T12:00:00.042Z");
    CHECK(wbskt::formatIsoTime(1709164800, 0) == "2024-02-29T00:00:00.000Z");
}

void messages() {
    JsonDocument payload;
    payload["celsius"] = 21.5;
    JsonDocument frame;
    readJson(wbskt::encodeMessage("temperature", payload.as<JsonVariantConst>(), kNow, 7), frame);
    CHECK(std::strcmp(frame["type"], "temperature") == 0);
    CHECK(frame["payload"]["celsius"].as<double>() == 21.5);
    CHECK(std::strcmp(frame["sentAt"], "2026-10-31T12:00:00.007Z") == 0);

    JsonDocument unstamped;
    readJson(wbskt::encodeMessage("temperature", payload.as<JsonVariantConst>(), 5, 0), unstamped);
    CHECK(unstamped["sentAt"].isNull());
}

void capabilities() {
    std::vector<wbskt::Command> commands;
    commands.push_back({"relay.set", "Switch the relay", {{"on", wbskt::DataType::Boolean, true, "", ""}}});
    JsonDocument frame;
    readJson(wbskt::encodeCapabilities({"wbskt-arduino", "1.0.0", "ESP32 ESP32-S3"}, commands), frame);
    CHECK(std::strcmp(frame["type"], "capabilities") == 0);
    CHECK(std::strcmp(frame["payload"]["agent"], "wbskt-arduino") == 0);
    JsonVariantConst relay = frame["payload"]["capabilities"][0];
    CHECK(std::strcmp(relay["command"], "relay.set") == 0);
    CHECK(std::strcmp(relay["parameters"][0]["name"], "on") == 0);
    CHECK(std::strcmp(relay["parameters"][0]["label"], "on") == 0);
    CHECK(std::strcmp(relay["parameters"][0]["dataType"], "boolean") == 0);
    CHECK(relay["parameters"][0]["isRequired"].as<bool>());
}

void pingIsAnsweredWithPong() {
    wbskt::Incoming in;
    const char* ping = R"({"type":"sys.ping","payload":{"timestamp":"2026-10-31T12:00:00.1234567Z","id":"p1"}})";
    wbskt::readFrame(ping, std::strlen(ping), kNow, in);
    CHECK(in.action == wbskt::Action::Reply);
    JsonDocument pong;
    readJson(in.reply, pong);
    CHECK(std::strcmp(pong["type"], "sys.pong") == 0);
    CHECK(std::strcmp(pong["payload"]["originalTimestamp"], "2026-10-31T12:00:00.1234567Z") == 0);
    CHECK(std::strcmp(pong["payload"]["id"], "p1") == 0);
}

void commandIsAckedThenDelivered() {
    wbskt::Incoming in;
    const char* command = R"({"type":"relay.set","payload":{"on":true},"commandId":"c1","expiresAt":"2026-10-31T12:01:00Z"})";
    wbskt::readFrame(command, std::strlen(command), kNow, in);
    CHECK(in.action == wbskt::Action::Deliver);
    CHECK(std::strcmp(in.type, "relay.set") == 0);
    CHECK(std::strcmp(in.commandId, "c1") == 0);
    CHECK(in.payload["on"].as<bool>());
    JsonDocument ack;
    readJson(in.reply, ack);
    CHECK(std::strcmp(ack["type"], "sys.ack") == 0);
    CHECK(std::strcmp(ack["payload"]["commandId"], "c1") == 0);
    CHECK(ack["payload"]["refused"].isNull());
}

void expiredCommandIsRefused() {
    wbskt::Incoming in;
    const char* command = R"({"type":"relay.set","payload":{},"commandId":"c2","expiresAt":"2026-10-31T11:59:59Z"})";
    wbskt::readFrame(command, std::strlen(command), kNow, in);
    CHECK(in.action == wbskt::Action::Reply);
    JsonDocument ack;
    readJson(in.reply, ack);
    CHECK(std::strcmp(ack["payload"]["commandId"], "c2") == 0);
    CHECK(std::strcmp(ack["payload"]["type"], "relay.set") == 0);
    CHECK(std::strcmp(ack["payload"]["refused"], "expired") == 0);
}

void expiryIsNotJudgedWithoutAClock() {
    wbskt::Incoming in;
    const char* command = R"({"type":"relay.set","payload":{},"commandId":"c3","expiresAt":"2020-01-01T00:00:00Z"})";
    wbskt::readFrame(command, std::strlen(command), 12, in);
    CHECK(in.action == wbskt::Action::Deliver);
}

void plainMessagesAreDeliveredWithoutAck() {
    wbskt::Incoming in;
    const char* message = R"({"type":"hello","payload":5})";
    wbskt::readFrame(message, std::strlen(message), kNow, in);
    CHECK(in.action == wbskt::Action::Deliver);
    CHECK(in.reply.empty());
    CHECK(in.payload.as<int>() == 5);
}

void junkAndPlatformMessagesAreIgnored() {
    const char* frames[] = {"not json", R"({"payload":1})", R"({"type":""})", R"({"type":"sys.other"})",
                            R"({"type":"capabilities"})"};
    for (const char* frame : frames) {
        wbskt::Incoming in;
        wbskt::readFrame(frame, std::strlen(frame), kNow, in);
        CHECK(in.action == wbskt::Action::Ignore);
        CHECK(in.reply.empty());
    }
}

void outboxKeepsOrderAndDropsTheOldest() {
    wbskt::Outbox outbox(2);
    outbox.push("a");
    outbox.push("b");
    outbox.push("c");
    CHECK(outbox.size() == 2);
    CHECK(outbox.dropped() == 1);
    CHECK(outbox.front() == "b");
    outbox.pop();
    CHECK(outbox.front() == "c");
}

uint32_t ceilingRandom(uint32_t max) { return max; }
uint32_t zeroRandom(uint32_t) { return 0; }

void reconnectCeilings() {
    wbskt::ReconnectPolicy top(ceilingRandom);
    CHECK(top.firstDelay(false) == 10000);
    CHECK(top.firstDelay(true) == 30000);
    CHECK(top.retryDelay(0) == 2000);
    CHECK(top.retryDelay(3) == 16000);
    CHECK(top.retryDelay(5) == 60000);
    CHECK(top.retryDelay(1000) == 60000);
    CHECK(top.afterThrottle(5000) == 7000);

    wbskt::ReconnectPolicy bottom(zeroRandom);
    CHECK(bottom.retryDelay(10) == 0);
    CHECK(bottom.afterThrottle(5000) == 5000);
}

}  // namespace

int main() {
    reservedTypes();
    isoTime();
    messages();
    capabilities();
    pingIsAnsweredWithPong();
    commandIsAckedThenDelivered();
    expiredCommandIsRefused();
    expiryIsNotJudgedWithoutAClock();
    plainMessagesAreDeliveredWithoutAck();
    junkAndPlatformMessagesAreIgnored();
    outboxKeepsOrderAndDropsTheOldest();
    reconnectCeilings();
    if (failures == 0) {
        std::printf("all host tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
