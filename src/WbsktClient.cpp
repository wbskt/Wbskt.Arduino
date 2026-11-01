#include "WbsktClient.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
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
// An HTTPS call with mbedTLS needs about 6 KB of stack; the task exists only while signing in.
constexpr uint32_t kSignInStackBytes = 10 * 1024;

uint32_t espRandom(uint32_t max) {
    return max == 0 ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(esp_random()) * (max + 1ULL)) >> 32);
}

std::string serialize(const JsonDocument& doc) {
    std::string out;
    serializeJson(doc, out);
    return out;
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
      outbox_(config.offlineBufferBytes),
      sendRate_(wbskt_core::kSendPerSecond, wbskt_core::kSendBurst),
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
    // The sign-in task writes into exchange_, so it has to finish first.
    while (!exchange_.done.load()) {
        delay(10);
    }
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
            if (tokenValid()) {
                openSocket();
            } else {
                startSignIn();
            }
            return;
        case State::SigningIn:
            if (exchange_.done.load()) {
                finishSignIn();
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
            flushOutbox();
            return;
    }
}

bool WbsktClient::tokenValid() const {
    return !token_.isEmpty() && millis() - tokenIssuedAt_ + wbskt_core::kTokenRenewBeforeMs < tokenLifetimeMs_;
}

void WbsktClient::startSignIn() {
    JsonDocument body;
    if (clientRefId_.isEmpty() || secret_.isEmpty()) {
        if (policyPin_.isEmpty()) {
            log_e("wbskt: no stored credentials and no policy PIN to register with");
            state_ = State::Stopped;
            return;
        }
        signInStep_ = SignInStep::Register;
        exchange_.path = wbskt_core::kRegisterPath;
        body["pin"] = policyPin_;
        body["name"] = deviceName_;
    } else {
        signInStep_ = SignInStep::Login;
        exchange_.path = wbskt_core::kLoginPath;
        body["clientRefId"] = clientRefId_;
        body["secret"] = secret_;
    }
    exchange_.body = serialize(body);
    exchange_.done.store(false);
    state_ = State::SigningIn;

    if (config_.backgroundSignIn &&
        xTaskCreate(signInTask, "wbskt-signin", kSignInStackBytes, this, 1, nullptr) == pdPASS) {
        return;
    }
    runExchange();
    finishSignIn();
}

void WbsktClient::signInTask(void* client) {
    static_cast<WbsktClient*>(client)->runExchange();
    vTaskDelete(nullptr);
}

void WbsktClient::runExchange() {
    exchange_.status = -1;
    exchange_.response.clear();
    exchange_.retryAfterMs = 0;

    WiFiClientSecure tls;
    tls.setCACert(config_.rootCa);
    HTTPClient http;
    String url = String("https://") + config_.apiHost;
    if (config_.port != 443) {
        url += ":" + String(config_.port);
    }
    url += exchange_.path;
    if (http.begin(tls, url)) {
        http.addHeader("Content-Type", "application/json");
        http.addHeader("Accept", "application/json");
        const char* headers[] = {"Retry-After"};
        http.collectHeaders(headers, 1);
        const int status = http.POST(String(exchange_.body.c_str()));
        if (status > 0) {
            exchange_.response = http.getString().c_str();
            exchange_.retryAfterMs = static_cast<uint32_t>(http.header("Retry-After").toInt()) * 1000UL;
        }
        http.end();
        exchange_.status = status;
    }
    exchange_.done.store(true);
}

void WbsktClient::finishSignIn() {
    if (state_ != State::SigningIn) {
        return;
    }
    JsonDocument response;
    deserializeJson(response, exchange_.response);
    if (exchange_.status == 429) {
        schedule(policy_.afterThrottle(exchange_.retryAfterMs));
        return;
    }
    if (signInStep_ == SignInStep::Register) {
        finishRegistration(exchange_.status, response);
    } else {
        finishLogin(exchange_.status, response);
    }
}

void WbsktClient::finishRegistration(int status, const JsonDocument& response) {
    const char* ref = response["clientRefId"];
    const char* secret = response["secret"];
    if ((status != 200 && status != 201) || ref == nullptr || secret == nullptr) {
        log_w("wbskt: registration answered %d", status);
        attemptFailed();
        return;
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
    schedule(0);
}

void WbsktClient::finishLogin(int status, const JsonDocument& response) {
    const char* token = response["accessToken"];
    if (status == 200 && token != nullptr) {
        token_ = token;
        tokenIssuedAt_ = millis();
        tokenLifetimeMs_ = (response["expiresIn"] | 3600UL) * 1000UL;
        openSocket();
        return;
    }
    const char* code = response["code"];
    if (status == 401 && code != nullptr && strcmp(code, wbskt_core::kCodeUnauthorized) == 0) {
        // Deleted, or its secret rotated: these credentials will never work again.
        forgetCredentials();
    }
    log_w("wbskt: login answered %d %s", status, code ? code : "");
    attemptFailed();
}

void WbsktClient::openSocket() {
    // begin() clears the authorization, so it is set after.
    socket_.beginSslWithCA(config_.socketHost, config_.port, wbskt_core::kSocketPath, config_.rootCa, wbskt_core::kSubprotocol);
    authHeader_ = "Bearer " + token_;
    socket_.setAuthorization(authHeader_.c_str());
    if (config_.heartbeatIntervalMs > 0) {
        socket_.enableHeartbeat(config_.heartbeatIntervalMs, config_.heartbeatTimeoutMs, config_.heartbeatMisses);
    }
    // begin() zeroes the library's last-failure time, and it only connects once millis() minus that
    // reaches the interval, so the interval must be 0 for the next loop() to make the attempt.
    socket_.setReconnectInterval(0);
    attemptStarted_ = false;
    state_ = State::Connecting;
    waitStartedAt_ = millis();
}

void WbsktClient::onSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            failures_ = 0;
            rejectedUpgrades_ = 0;
            state_ = State::Connected;
            sendCapabilities();
            flushOutbox();
            if (onConnected_) {
                onConnected_();
            }
            break;
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
            wbskt_core::readFrame(reinterpret_cast<const char*>(payload), length, nowEpochMillis() / 1000, incoming_);
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
    failures_ = 0;
    schedule(policy_.firstDelay(false));
    if (onDisconnected_) {
        onDisconnected_();
    }
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
    return enqueue(wbskt_core::encodeMessage(type, payload));
}

bool WbsktClient::reportState(const JsonDocument& values) {
    return enqueue(wbskt_core::encodeMessage(wbskt_core::kStateReport, values.as<JsonVariantConst>()));
}

void WbsktClient::publishCapabilities() {
    if (state_ == State::Connected) {
        sendCapabilities();
    }
}

void WbsktClient::sendCapabilities() {
    socket_.sendTXT(wbskt_core::encodeCapabilities(
        {"wbskt-arduino", WBSKT_SDK_VERSION, std::string("ESP32 ") + ESP.getChipModel()}, commands_).c_str());
}

bool WbsktClient::enqueue(std::string frame) {
    if (frame.size() + wbskt_core::kSentAtBytes > wbskt_core::kMaxMessageBytes) {
        return false;
    }
    const wbskt_core::Stamp stamp{nowEpochMillis(), static_cast<uint32_t>(millis())};
    // Straight out when nothing is waiting, so a new message never overtakes the backlog.
    if (state_ == State::Connected && outbox_.empty() && sendRate_.take(millis()) && transmit(frame, stamp)) {
        return true;
    }
    outbox_.push(frame, stamp);
    return true;
}

bool WbsktClient::transmit(std::string& frame, wbskt_core::Stamp stamp) {
    int64_t sentAt = stamp.epochMillis;
    if (sentAt < 0) {
        // Made before the clock was set: date it back from now, if the clock is set by now.
        const int64_t now = nowEpochMillis();
        if (now >= 0) {
            sentAt = now - static_cast<uint32_t>(millis() - stamp.enqueuedAt);
        }
    }
    wbskt_core::stampSentAt(frame, sentAt);
    return socket_.sendTXT(frame.c_str());
}

void WbsktClient::flushOutbox() {
    wbskt_core::Stamp stamp{};
    while (state_ == State::Connected && !outbox_.empty() && sendRate_.take(millis())) {
        outbox_.front(sending_, stamp);
        if (!transmit(sending_, stamp)) {
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

int64_t WbsktClient::nowEpochMillis() const {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    if (tv.tv_sec < wbskt_core::kClockValidAfter) {
        return -1;
    }
    return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}
