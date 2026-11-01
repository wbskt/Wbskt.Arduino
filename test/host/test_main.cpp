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
#include "../../src/wbskt/RateLimit.h"
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
    CHECK(wbskt_core::isReserved("sys.custom"));
    CHECK(wbskt_core::isReserved("SYS.ping"));
    CHECK(wbskt_core::isReserved("Capabilities"));
    CHECK(wbskt_core::isReserved("state.REPORT"));
    CHECK(!wbskt_core::isReserved("temperature"));
    CHECK(!wbskt_core::isReserved("system.boot"));
    CHECK(!wbskt_core::isSendableType(""));
    CHECK(!wbskt_core::isSendableType(nullptr));
    CHECK(!wbskt_core::isSendableType(std::string(101, 'x').c_str()));
    CHECK(wbskt_core::isSendableType(std::string(100, 'x').c_str()));
}

void isoTime() {
    CHECK(wbskt_core::parseIsoTime("2026-10-31T12:00:00Z") == kNow);
    CHECK(wbskt_core::parseIsoTime("2026-10-31T12:00:00.1234567Z") == kNow);
    CHECK(wbskt_core::parseIsoTime("2026-10-31T13:30:00+01:30") == kNow);
    CHECK(wbskt_core::parseIsoTime("2026-10-31T12:00:00") == kNow);
    CHECK(wbskt_core::parseIsoTime("1970-01-01T00:00:00Z") == 0);
    CHECK(wbskt_core::parseIsoTime("2024-02-29T00:00:00Z") == 1709164800);
    CHECK(wbskt_core::parseIsoTime("not a time") == -1);
    CHECK(wbskt_core::parseIsoTime("2026-13-01T00:00:00Z") == -1);
    CHECK(wbskt_core::parseIsoTime(nullptr) == -1);
    CHECK(wbskt_core::formatIsoTime(kNow, 42) == "2026-10-31T12:00:00.042Z");
    CHECK(wbskt_core::formatIsoTime(1709164800, 0) == "2024-02-29T00:00:00.000Z");
}

void messages() {
    JsonDocument payload;
    payload["celsius"] = 21.5;
    std::string text = wbskt_core::encodeMessage("temperature", payload.as<JsonVariantConst>());
    JsonDocument unstamped;
    readJson(text, unstamped);
    CHECK(unstamped["sentAt"].isNull());

    const size_t before = text.size();
    wbskt_core::stampSentAt(text, kNow * 1000 + 7);
    CHECK(text.size() - before <= wbskt_core::kSentAtBytes);
    JsonDocument frame;
    readJson(text, frame);
    CHECK(std::strcmp(frame["type"], "temperature") == 0);
    CHECK(frame["payload"]["celsius"].as<double>() == 21.5);
    CHECK(std::strcmp(frame["sentAt"], "2026-10-31T12:00:00.007Z") == 0);

    std::string clockless = wbskt_core::encodeMessage("temperature", payload.as<JsonVariantConst>());
    wbskt_core::stampSentAt(clockless, -1);
    JsonDocument stillUnstamped;
    readJson(clockless, stillUnstamped);
    CHECK(stillUnstamped["sentAt"].isNull());
}

void capabilities() {
    std::vector<wbskt_core::Command> commands;
    commands.push_back({"relay.set", "Switch the relay", {{"on", wbskt_core::DataType::Boolean, true, "", ""}}});
    JsonDocument frame;
    readJson(wbskt_core::encodeCapabilities({"wbskt-arduino", "1.0.0", "ESP32 ESP32-S3"}, commands), frame);
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
    wbskt_core::Incoming in;
    const char* ping = R"({"type":"sys.ping","payload":{"timestamp":"2026-10-31T12:00:00.1234567Z","id":"p1"}})";
    wbskt_core::readFrame(ping, std::strlen(ping), kNow, in);
    CHECK(in.action == wbskt_core::Action::Reply);
    JsonDocument pong;
    readJson(in.reply, pong);
    CHECK(std::strcmp(pong["type"], "sys.pong") == 0);
    CHECK(std::strcmp(pong["payload"]["originalTimestamp"], "2026-10-31T12:00:00.1234567Z") == 0);
    CHECK(std::strcmp(pong["payload"]["id"], "p1") == 0);
}

void commandIsAckedThenDelivered() {
    wbskt_core::Incoming in;
    const char* command = R"({"type":"relay.set","payload":{"on":true},"commandId":"c1","expiresAt":"2026-10-31T12:01:00Z"})";
    wbskt_core::readFrame(command, std::strlen(command), kNow, in);
    CHECK(in.action == wbskt_core::Action::Deliver);
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
    wbskt_core::Incoming in;
    const char* command = R"({"type":"relay.set","payload":{},"commandId":"c2","expiresAt":"2026-10-31T11:59:59Z"})";
    wbskt_core::readFrame(command, std::strlen(command), kNow, in);
    CHECK(in.action == wbskt_core::Action::Reply);
    JsonDocument ack;
    readJson(in.reply, ack);
    CHECK(std::strcmp(ack["payload"]["commandId"], "c2") == 0);
    CHECK(std::strcmp(ack["payload"]["type"], "relay.set") == 0);
    CHECK(std::strcmp(ack["payload"]["refused"], "expired") == 0);
}

void expiryIsNotJudgedWithoutAClock() {
    wbskt_core::Incoming in;
    const char* command = R"({"type":"relay.set","payload":{},"commandId":"c3","expiresAt":"2020-01-01T00:00:00Z"})";
    wbskt_core::readFrame(command, std::strlen(command), 12, in);
    CHECK(in.action == wbskt_core::Action::Deliver);
}

void plainMessagesAreDeliveredWithoutAck() {
    wbskt_core::Incoming in;
    const char* message = R"({"type":"hello","payload":5})";
    wbskt_core::readFrame(message, std::strlen(message), kNow, in);
    CHECK(in.action == wbskt_core::Action::Deliver);
    CHECK(in.reply.empty());
    CHECK(in.payload.as<int>() == 5);
}

void junkAndPlatformMessagesAreIgnored() {
    const char* frames[] = {"not json", R"({"payload":1})", R"({"type":""})", R"({"type":"sys.other"})",
                            R"({"type":"capabilities"})"};
    for (const char* frame : frames) {
        wbskt_core::Incoming in;
        wbskt_core::readFrame(frame, std::strlen(frame), kNow, in);
        CHECK(in.action == wbskt_core::Action::Ignore);
        CHECK(in.reply.empty());
    }
}

void outboxKeepsOrderAndDropsTheOldest() {
    constexpr size_t kEntry = wbskt_core::Outbox::kEntryOverhead;
    // Room for exactly two four-byte messages.
    wbskt_core::Outbox outbox(2 * (kEntry + 4));
    CHECK(outbox.push("aaaa", {1000, 1}));
    CHECK(outbox.push("bbbb", {-1, 2}));
    CHECK(outbox.push("cccc", {3000, 3}));
    CHECK(outbox.size() == 2);
    CHECK(outbox.dropped() == 1);

    std::string frame;
    wbskt_core::Stamp stamp{};
    outbox.front(frame, stamp);
    CHECK(frame == "bbbb");
    CHECK(stamp.epochMillis == -1);
    CHECK(stamp.enqueuedAt == 2);
    outbox.pop();
    outbox.front(frame, stamp);
    CHECK(frame == "cccc");
    CHECK(stamp.epochMillis == 3000);
    outbox.pop();
    CHECK(outbox.empty());
    CHECK(outbox.bytesUsed() == 0);
}

void outboxWrapsAroundAndCountsBytes() {
    wbskt_core::Outbox outbox(100);
    std::string frame;
    wbskt_core::Stamp stamp{};
    // Odd sizes walk the entries across the end of the buffer many times.
    for (int i = 0; i < 200; ++i) {
        const std::string message(static_cast<size_t>(i % 37), static_cast<char>('a' + i % 26));
        outbox.push(message, {i, static_cast<uint32_t>(i)});
        CHECK(outbox.bytesUsed() <= outbox.capacity());
        outbox.front(frame, stamp);
        if (outbox.size() == 1) {
            CHECK(frame == message);
        }
        if (i % 3 == 0) {
            outbox.pop();
        }
    }
    size_t last = 0;
    while (!outbox.empty()) {
        outbox.front(frame, stamp);
        CHECK(static_cast<size_t>(stamp.epochMillis) >= last);
        last = static_cast<size_t>(stamp.epochMillis);
        CHECK(frame.size() == static_cast<size_t>(stamp.epochMillis % 37));
        outbox.pop();
    }

    wbskt_core::Outbox tiny(10);
    CHECK(!tiny.push("too big for it", {0, 0}));
    CHECK(tiny.dropped() == 1);
    CHECK(tiny.empty());
}

void sendRate() {
    wbskt_core::TokenBucket bucket(10, 3);
    CHECK(bucket.take(0));
    CHECK(bucket.take(0));
    CHECK(bucket.take(0));
    CHECK(!bucket.take(0));
    CHECK(!bucket.take(99));
    CHECK(bucket.take(100));
    CHECK(!bucket.take(100));
    // A long pause refills only up to the burst.
    CHECK(bucket.take(60000));
    CHECK(bucket.take(60000));
    CHECK(bucket.take(60000));
    CHECK(!bucket.take(60000));
}

uint32_t ceilingRandom(uint32_t max) { return max; }
uint32_t zeroRandom(uint32_t) { return 0; }

void reconnectCeilings() {
    wbskt_core::ReconnectPolicy top(ceilingRandom);
    CHECK(top.firstDelay(false) == 10000);
    CHECK(top.firstDelay(true) == 30000);
    CHECK(top.retryDelay(0) == 2000);
    CHECK(top.retryDelay(3) == 16000);
    CHECK(top.retryDelay(5) == 60000);
    CHECK(top.retryDelay(1000) == 60000);
    CHECK(top.afterThrottle(5000) == 7000);

    wbskt_core::ReconnectPolicy bottom(zeroRandom);
    CHECK(bottom.retryDelay(10) == 0);
    CHECK(bottom.afterThrottle(5000) == 5000);
}

void upgradeFailures() {
    using wbskt_core::UpgradeFailure;
    using wbskt_core::classifyUpgradeFailure;
    const auto classify = [](const char* reason) { return classifyUpgradeFailure(reason, strlen(reason)); };
    CHECK(classify("HTTP 401") == UpgradeFailure::TokenRefused);
    CHECK(classify("WebSocket handshake failed - HTTP 401") == UpgradeFailure::TokenRefused);
    CHECK(classify("WebSocket handshake failed - HTTP 503") == UpgradeFailure::Other);
    CHECK(classify("HTTP 4010") == UpgradeFailure::Other);
    CHECK(classify("Connection lost") == UpgradeFailure::Other);
    CHECK(classifyUpgradeFailure(nullptr, 0) == UpgradeFailure::Unknown);
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
    outboxWrapsAroundAndCountsBytes();
    sendRate();
    reconnectCeilings();
    upgradeFailures();
    if (failures == 0) {
        std::printf("all host tests passed\n");
    }
    return failures == 0 ? 0 : 1;
}
