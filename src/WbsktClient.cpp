#include "WbsktClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <sys/time.h>

#include "wbskt/IsoTime.h"
#include "wbskt/Protocol.h"
#include "wbskt/RootCa.h"
#include "wbskt/Version.h"

namespace {

constexpr const char* kPrefsNamespace = "wbskt";
constexpr const char* kPrefsRef = "ref";
constexpr const char* kPrefsSecret = "secret";
// A socket handshake that hasn't finished by now has failed; the library doesn't always say so.
constexpr unsigned long kConnectTimeoutMs = 20000;
constexpr uint32_t kWifiPollMs = 1000;
// The library's own reconnect is turned off once an attempt has started: the client schedules
// every attempt itself.
constexpr unsigned long kNeverReconnect = 0xFFFFFFFFUL;
// A failed upgrade without a reason (WebSockets before 2.7.2) counts against the token after this many.
constexpr uint32_t kUpgradeFailuresBeforeLogin = 3;

uint32_t espRandom(uint32_t max) {
    return max == 0 ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(esp_random()) * (max + 1ULL)) >> 32);
}

}  // namespace

WbsktCommandBuilder& WbsktCommandBuilder::param(const char* name, WbsktDataType type, bool required,
                                                const char* label, const char* description) {
    command_.parameters.push_back({name, type, required, label, description});
    return *this;
}

WbsktClient::WbsktClient(const char* deviceName, const char* policyPin, WbsktConfig config)
    : deviceName_(deviceName),
      policyPin_(policyPin ? policyPin : ""),
      config_(config),
      outbox_(config.offlineBufferSize),
      policy_(espRandom) {}

WbsktCommandBuilder WbsktClient::addCommand(const char* name, const char* description) {
    commands_.push_back({name, description, {}});
    return WbsktCommandBuilder(commands_.back());
}

void WbsktClient::begin() {
    if (config_.rootCa == nullptr) {
        config_.rootCa = wbskt_core::kLetsEncryptRoots;
    }
    if (config_.ntpServer != nullptr) {
        configTime(0, 0, config_.ntpServer);
    }

    prefs_.begin(kPrefsNamespace, true);
    clientRefId_ = prefs_.getString(kPrefsRef, "");
    secret_ = prefs_.getString(kPrefsSecret, "");
    prefs_.end();

    socket_.onEvent([this](WStype_t type, uint8_t* payload, size_t length) { onSocketEvent(type, payload, length); });
    failures_ = 0;
    state_ = State::Waiting;
    schedule(0);
}

void WbsktClient::end() {
    const bool wasConnected = state_ == State::Connected;
    state_ = State::Stopped;
    socket_.disconnect();
    if (wasConnected && onDisconnected_) {
        onDisconnected_();
    }
}

void WbsktClient::loop() {
    switch (state_) {
        case State::Stopped:
            return;
        case State::Waiting:
            if (millis() - waitStartedAt_ < waitMs_) {
                return;
            }
            if (WiFi.status() != WL_CONNECTED) {
                schedule(kWifiPollMs);
                return;
            }
            if (ensureToken()) {
                openSocket();
            }
            return;
        case State::Connecting:
            socket_.loop();
            if (!attemptStarted_) {
                attemptStarted_ = true;
                socket_.setReconnectInterval(kNeverReconnect);
                // A refused TCP connect raises no event; don't wait out the timeout for it.
                if (state_ == State::Connecting && !socket_.isConnected()) {
                    attemptFailed();
                    return;
                }
            }
            if (state_ == State::Connecting && millis() - waitStartedAt_ > kConnectTimeoutMs) {
                // Leave Connecting first, so the disconnect event this raises isn't counted twice.
                attemptFailed();
                socket_.disconnect();
            }
            return;
        case State::Connected:
            socket_.loop();
            return;
    }
}

bool WbsktClient::ensureToken() {
    if (tokenValid()) {
        return true;
    }
    if ((clientRefId_.isEmpty() || secret_.isEmpty()) && !registerDevice()) {
        return false;
    }
    return login();
}

bool WbsktClient::tokenValid() const {
    return !token_.isEmpty() && millis() - tokenIssuedAt_ + wbskt_core::kTokenRenewBeforeMs < tokenLifetimeMs_;
}

bool WbsktClient::registerDevice() {
    if (policyPin_.isEmpty()) {
        log_e("wbskt: no stored credentials and no policy PIN to register with");
        state_ = State::Stopped;
        return false;
    }

    JsonDocument body;
    body["pin"] = policyPin_;
    body["name"] = deviceName_;
    JsonDocument response;
    uint32_t retryAfterMs = 0;
    const int status = postJson(wbskt_core::kRegisterPath, body, response, retryAfterMs);
    if (status == 429) {
        schedule(policy_.afterThrottle(retryAfterMs));
        return false;
    }
    const char* ref = response["clientRefId"];
    const char* secret = response["secret"];
    if ((status != 200 && status != 201) || ref == nullptr || secret == nullptr) {
        log_w("wbskt: registration answered %d", status);
        attemptFailed();
        return false;
    }

    // Saved even while approval is pending: registering again would make a second device.
    clientRefId_ = ref;
    secret_ = secret;
    prefs_.begin(kPrefsNamespace, false);
    prefs_.putString(kPrefsRef, clientRefId_);
    prefs_.putString(kPrefsSecret, secret_);
    prefs_.end();
    if ((response["status"] | wbskt_core::kStatusRegistered) == wbskt_core::kStatusPending) {
        log_i("wbskt: registered; waiting for approval in the console");
    }
    return true;
}

bool WbsktClient::login() {
    JsonDocument body;
    body["clientRefId"] = clientRefId_;
    body["secret"] = secret_;
    JsonDocument response;
    uint32_t retryAfterMs = 0;
    const int status = postJson(wbskt_core::kLoginPath, body, response, retryAfterMs);

    const char* token = response["accessToken"];
    if (status == 200 && token != nullptr) {
        token_ = token;
        tokenIssuedAt_ = millis();
        tokenLifetimeMs_ = (response["expiresIn"] | 3600UL) * 1000UL;
        return true;
    }
    if (status == 429) {
        schedule(policy_.afterThrottle(retryAfterMs));
        return false;
    }
    const char* code = response["code"];
    if (status == 401 && code != nullptr && strcmp(code, wbskt_core::kCodeUnauthorized) == 0) {
        // Deleted, or its secret rotated: these credentials will never work again.
        forgetCredentials();
    }
    log_w("wbskt: login answered %d %s", status, code ? code : "");
    attemptFailed();
    return false;
}

int WbsktClient::postJson(const char* path, const JsonDocument& body, JsonDocument& response, uint32_t& retryAfterMs) {
    WiFiClientSecure tls;
    tls.setCACert(config_.rootCa);
    HTTPClient http;
    String url = String("https://") + config_.apiHost;
    if (config_.port != 443) {
        url += ":" + String(config_.port);
    }
    url += path;
    if (!http.begin(tls, url)) {
        return -1;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");
    const char* headers[] = {"Retry-After"};
    http.collectHeaders(headers, 1);

    String payload;
    serializeJson(body, payload);
    const int status = http.POST(payload);
    if (status > 0) {
        deserializeJson(response, http.getString());
        retryAfterMs = static_cast<uint32_t>(http.header("Retry-After").toInt()) * 1000UL;
    }
    http.end();
    return status;
}

void WbsktClient::openSocket() {
    // begin() clears the authorization, so it is set after.
    socket_.beginSslWithCA(config_.socketHost, config_.port, wbskt_core::kSocketPath, config_.rootCa, wbskt_core::kSubprotocol);
    authHeader_ = "Bearer " + token_;
    socket_.setAuthorization(authHeader_.c_str());
    // begin() zeroes the library's last-failure time, and it only connects once millis() minus that
    // reaches the interval, so the interval must be 0 for the next loop() to make the attempt.
    socket_.setReconnectInterval(0);
    attemptStarted_ = false;
    state_ = State::Connecting;
    waitStartedAt_ = millis();
}

void WbsktClient::onSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED: {
            failures_ = 0;
            rejectedUpgrades_ = 0;
            socket_.sendTXT(wbskt_core::encodeCapabilities(
                {"wbskt-arduino", WBSKT_SDK_VERSION, std::string("ESP32 ") + ESP.getChipModel()}, commands_).c_str());
            flushOutbox();
            state_ = State::Connected;
            if (onConnected_) {
                onConnected_();
            }
            break;
        }
        case WStype_DISCONNECTED:
            if (state_ == State::Connected) {
                connectionDropped();
            } else if (state_ == State::Connecting) {
                const wbskt_core::UpgradeFailure failure =
                    wbskt_core::classifyUpgradeFailure(reinterpret_cast<const char*>(payload), length);
                if (failure == wbskt_core::UpgradeFailure::TokenRefused ||
                    (failure == wbskt_core::UpgradeFailure::Unknown &&
                     ++rejectedUpgrades_ >= kUpgradeFailuresBeforeLogin)) {
                    token_ = "";
                    rejectedUpgrades_ = 0;
                }
                attemptFailed();
            }
            break;
        case WStype_TEXT: {
            int64_t now = nowSeconds();
            wbskt_core::readFrame(reinterpret_cast<const char*>(payload), length, now, incoming_);
            if (!incoming_.reply.empty()) {
                socket_.sendTXT(incoming_.reply.c_str());
            }
            if (incoming_.action == wbskt_core::Action::Deliver && onMessage_) {
                onMessage_({incoming_.type, incoming_.payload, incoming_.commandId});
            }
            break;
        }
        default:
            break;
    }
}

void WbsktClient::connectionDropped() {
    // The library doesn't pass the close code, so a server restart (1001) can't be told apart from
    // any other drop; both wait 0-10 s.
    if (onDisconnected_) {
        onDisconnected_();
    }
    failures_ = 0;
    schedule(policy_.firstDelay(false));
}

void WbsktClient::attemptFailed() {
    schedule(policy_.retryDelay(failures_++));
}

void WbsktClient::schedule(uint32_t delayMs) {
    if (state_ == State::Stopped) {
        return;
    }
    state_ = State::Waiting;
    waitStartedAt_ = millis();
    waitMs_ = delayMs;
}

bool WbsktClient::send(const char* type, JsonVariantConst payload) {
    if (!wbskt_core::isSendableType(type)) {
        return false;
    }
    uint16_t millis = 0;
    const int64_t now = nowSeconds(&millis);
    return enqueue(wbskt_core::encodeMessage(type, payload, now, millis));
}

bool WbsktClient::reportState(const JsonDocument& values) {
    uint16_t millis = 0;
    const int64_t now = nowSeconds(&millis);
    return enqueue(wbskt_core::encodeMessage(wbskt_core::kStateReport, values.as<JsonVariantConst>(), now, millis));
}

void WbsktClient::publishCapabilities() {
    if (state_ == State::Connected) {
        socket_.sendTXT(wbskt_core::encodeCapabilities(
            {"wbskt-arduino", WBSKT_SDK_VERSION, std::string("ESP32 ") + ESP.getChipModel()}, commands_).c_str());
    }
}

bool WbsktClient::enqueue(std::string frame) {
    if (frame.size() > wbskt_core::kMaxMessageBytes) {
        return false;
    }
    if (state_ == State::Connected && outbox_.empty() && socket_.sendTXT(frame.c_str())) {
        return true;
    }
    outbox_.push(std::move(frame));
    return true;
}

void WbsktClient::flushOutbox() {
    while (!outbox_.empty()) {
        if (!socket_.sendTXT(outbox_.front().c_str())) {
            return;
        }
        outbox_.pop();
    }
}

void WbsktClient::forgetCredentials() {
    clientRefId_ = "";
    secret_ = "";
    token_ = "";
    prefs_.begin(kPrefsNamespace, false);
    prefs_.remove(kPrefsRef);
    prefs_.remove(kPrefsSecret);
    prefs_.end();
}

int64_t WbsktClient::nowSeconds(uint16_t* millis) const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (millis != nullptr) {
        *millis = static_cast<uint16_t>(tv.tv_usec / 1000);
    }
    return static_cast<int64_t>(tv.tv_sec);
}
