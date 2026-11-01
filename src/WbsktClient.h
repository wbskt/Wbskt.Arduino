// Connect an ESP32 to Wbskt: register once with a policy PIN, keep the credentials in NVS, hold a
// WebSocket open, send readings and take commands. Follows Docs/Device.Protocol.md in wbskt/Wbskt.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebSocketsClient.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "wbskt/Frames.h"
#include "wbskt/Ota.h"
#include "wbskt/Outbox.h"
#include "wbskt/RateLimit.h"
#include "wbskt/Reconnect.h"

using WbsktDataType = wbskt_core::DataType;
using WbsktOtaState = wbskt_core::OtaState;

// An update the console asked for, passed to onOtaRequest before anything is downloaded.
struct WbsktOtaRequest {
    const char* url;
    const char* sha256;
    // Empty when the command didn't say.
    const char* version;
    // Bytes, or 0 when the command didn't say.
    size_t size;
};

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
    // RAM for messages waiting to go out (offline, or over the send rate), allocated once. Each
    // message takes its JSON size plus 16 bytes; when full, the oldest go first.
    size_t offlineBufferBytes = 16 * 1024;
    // NTP server for the clock that stamps sentAt and judges command expiry. nullptr if the
    // sketch sets the clock itself.
    const char* ntpServer = "pool.ntp.org";
    // A WebSocket ping this often while connected; after heartbeatMisses pongs that don't come
    // back within heartbeatTimeoutMs, the link counts as dead and the client reconnects.
    // heartbeatIntervalMs = 0 turns it off.
    uint32_t heartbeatIntervalMs = 20000;
    uint32_t heartbeatTimeoutMs = 10000;
    uint8_t heartbeatMisses = 2;
    // Sign in on a FreeRTOS task, so loop() keeps running during the TLS handshake. false signs in
    // inside loop(), which then blocks for a few seconds.
    bool backgroundSignIn = true;
    // PEM roots for the host firmware is downloaded from, when it isn't covered by rootCa (GitHub
    // release assets aren't: they need the roots their CDN chains to). nullptr uses rootCa.
    const char* otaRootCa = nullptr;
    // After an update the new firmware has this long, from boot, to connect to Wbskt. If it
    // doesn't, or it restarts otaTrialBoots times without connecting, the board goes back to
    // the firmware it had. 0 waits for ever (restarts still count).
    uint32_t otaConfirmTimeoutMs = 5UL * 60UL * 1000UL;
    uint8_t otaTrialBoots = 3;
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
    // Call from loop() as often as possible.
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

    // Take firmware updates from the console (sys.ota.update), before begin(). The version is
    // reported as the "firmware" state variable on every connect. Every firmware you ship must
    // call this too, or the board can only be updated over USB after that.
    void enableOta(const char* firmwareVersion);
    // Called before an update is downloaded; return false to refuse it (low battery, busy).
    void onOtaRequest(std::function<bool(const WbsktOtaRequest&)> handler) { onOtaRequest_ = std::move(handler); }
    // Called as an update goes: Accepted, Downloading with a percentage (loop() is blocked while it
    // downloads), then a restart; Failed or Rejected if it didn't happen; Installed once the new
    // firmware has connected.
    void onOta(std::function<void(WbsktOtaState, uint8_t percent)> handler) { onOta_ = std::move(handler); }

    // Messages waiting to go out.
    size_t queuedMessages() const { return outbox_.size(); }
    // Messages dropped because the buffer was full.
    size_t droppedMessages() const { return outbox_.dropped(); }
    // Discard the stored credentials, so the next connection registers again.
    void forgetCredentials();

private:
    enum class State { Stopped, Waiting, SigningIn, Connecting, Connected };
    enum class SignInStep { Register, Login };

    // One HTTPS call, made on the sign-in task or inline. Only the task writes it while in flight.
    struct Exchange {
        const char* path = nullptr;
        std::string body;
        int status = 0;
        std::string response;
        uint32_t retryAfterMs = 0;
        std::atomic<bool> done{true};
    };

    static void signInTask(void* client);
    void startSignIn();
    void runExchange();
    void finishSignIn();
    void finishRegistration(int status, const JsonDocument& response);
    void finishLogin(int status, const JsonDocument& response);
    bool tokenValid() const;
    void openSocket();
    void onSocketEvent(WStype_t type, uint8_t* payload, size_t length);
    void connectionDropped();
    void attemptFailed();
    void schedule(uint32_t delayMs);
    bool enqueue(std::string frame);
    bool transmit(std::string& frame, wbskt_core::Stamp stamp);
    void flushOutbox();
    void sendCapabilities();
    int64_t nowEpochMillis() const;
    void checkOtaTrial();
    void otaConnected();
    void receiveOta(const char* commandId, JsonVariantConst payload);
    void rejectOta(const char* commandId, const std::string& reason);
    void runOta();
    void rollBack();
    void otaStatus(const char* commandId, WbsktOtaState state, int progress = -1, const char* version = nullptr,
                   const char* error = nullptr);
    void notifyOta(WbsktOtaState state, uint8_t percent);

    String deviceName_;
    String policyPin_;
    WbsktConfig config_;
    std::vector<wbskt_core::Command> commands_;
    wbskt_core::Outbox outbox_;
    wbskt_core::TokenBucket sendRate_;
    wbskt_core::ReconnectPolicy policy_;
    wbskt_core::Incoming incoming_;
    WebSocketsClient socket_;
    Preferences prefs_;
    Exchange exchange_;
    SignInStep signInStep_ = SignInStep::Login;
    std::string sending_;

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
    bool attemptStarted_ = false;

    String firmwareVersion_;
    bool otaEnabled_ = false;
    bool otaPending_ = false;
    std::string otaCommandId_;
    wbskt_core::OtaRequest otaRequest_;
    // Set while new firmware is on trial: it has until otaConfirmTimeoutMs to connect.
    bool otaTrial_ = false;
    String otaTrialCommandId_;
    String otaTrialVersion_;

    std::function<bool(const WbsktOtaRequest&)> onOtaRequest_;
    std::function<void(WbsktOtaState, uint8_t)> onOta_;
    MessageHandler onMessage_;
    EventHandler onConnected_;
    EventHandler onDisconnected_;
};
