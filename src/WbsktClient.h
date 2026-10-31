// Connect an ESP32 to Wbskt: register once with a policy PIN, keep the credentials in NVS, hold a
// WebSocket open, send readings and take commands. Follows Docs/Device.Protocol.md in wbskt/Wbskt.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebSocketsClient.h>

#include <functional>
#include <string>
#include <vector>

#include "wbskt/Frames.h"
#include "wbskt/Outbox.h"
#include "wbskt/Reconnect.h"

using WbsktDataType = wbskt_core::DataType;

struct WbsktMessage {
    const char* type;
    JsonVariantConst payload;
    // Set when the server asked for delivery to be confirmed. The SDK has already acked it.
    const char* commandId;
};

struct WbsktConfig {
    const char* apiHost = "api.dev.wbskt.com";
    const char* socketHost = "ws.dev.wbskt.com";
    uint16_t port = 443;
    // PEM roots the hosts' certificates chain to. nullptr uses the Let's Encrypt roots, which the
    // hosted service and a default self-hosted install (Traefik with ACME) use.
    const char* rootCa = nullptr;
    // Messages kept while offline. Each is a JSON string in RAM, so keep this small.
    size_t offlineBufferSize = 32;
    // NTP server for the clock that stamps sentAt and judges command expiry. nullptr if the
    // sketch sets the clock itself.
    const char* ntpServer = "pool.ntp.org";
};

class WbsktCommandBuilder {
public:
    explicit WbsktCommandBuilder(wbskt_core::Command& command) : command_(command) {}

    WbsktCommandBuilder& param(const char* name, WbsktDataType type, bool required = false,
                               const char* label = "", const char* description = "");

private:
    wbskt_core::Command& command_;
};

class WbsktClient {
public:
    using MessageHandler = std::function<void(const WbsktMessage&)>;
    using EventHandler = std::function<void()>;

    WbsktClient(const char* deviceName, const char* policyPin, WbsktConfig config = WbsktConfig());

    // Call once WiFi is set up. Connecting happens in loop().
    void begin();
    // Call from loop() as often as possible. Logging in can block for a few seconds.
    void loop();
    void end();

    bool connected() const { return state_ == State::Connected; }

    void onMessage(MessageHandler handler) { onMessage_ = std::move(handler); }
    void onConnected(EventHandler handler) { onConnected_ = std::move(handler); }
    void onDisconnected(EventHandler handler) { onDisconnected_ = std::move(handler); }

    // Declare a command the console can send, before begin(). Chain .param(...) for its fields.
    WbsktCommandBuilder addCommand(const char* name, const char* description = "");
    // Tell the server again, after adding commands once connected.
    void publishCapabilities();

    // Send an application message, or queue it while offline. False if the type is reserved or
    // empty, or the message is over 64 KB.
    bool send(const char* type, JsonVariantConst payload);
    bool send(const char* type, const JsonDocument& payload) { return send(type, payload.as<JsonVariantConst>()); }
    // Report the state variables that changed. Numbers are kept as readings for charts.
    bool reportState(const JsonDocument& values);

    // Messages dropped because the offline buffer was full.
    size_t droppedMessages() const { return outbox_.dropped(); }
    // Discard the stored credentials, so the next connection registers again.
    void forgetCredentials();

private:
    enum class State { Stopped, Waiting, Connecting, Connected };

    bool ensureToken();
    bool registerDevice();
    bool login();
    int postJson(const char* path, const JsonDocument& body, JsonDocument& response, uint32_t& retryAfterMs);
    void openSocket();
    void onSocketEvent(WStype_t type, uint8_t* payload, size_t length);
    void connectionDropped();
    void attemptFailed();
    void schedule(uint32_t delayMs);
    bool enqueue(std::string frame);
    void flushOutbox();
    int64_t nowSeconds(uint16_t* millis = nullptr) const;
    bool tokenValid() const;

    String deviceName_;
    String policyPin_;
    WbsktConfig config_;
    std::vector<wbskt_core::Command> commands_;
    wbskt_core::Outbox outbox_;
    wbskt_core::ReconnectPolicy policy_;
    wbskt_core::Incoming incoming_;
    WebSocketsClient socket_;
    Preferences prefs_;

    String clientRefId_;
    String secret_;
    String token_;
    String authHeader_;
    unsigned long tokenIssuedAt_ = 0;
    unsigned long tokenLifetimeMs_ = 0;

    State state_ = State::Stopped;
    unsigned long waitStartedAt_ = 0;
    uint32_t waitMs_ = 0;
    uint32_t failures_ = 0;
    uint32_t rejectedUpgrades_ = 0;

    MessageHandler onMessage_;
    EventHandler onConnected_;
    EventHandler onDisconnected_;
};
