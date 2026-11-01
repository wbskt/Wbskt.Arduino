// The device protocol's names and limits. Docs/Device.Protocol.md in wbskt/Wbskt is the spec.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace wbskt_core {

constexpr const char* kSubprotocol = "wbskt.v1";
constexpr size_t kMaxMessageBytes = 64 * 1024;
constexpr size_t kMaxTypeLength = 100;

constexpr const char* kPing = "sys.ping";
constexpr const char* kPong = "sys.pong";
constexpr const char* kAck = "sys.ack";
constexpr const char* kCapabilities = "capabilities";
constexpr const char* kStateReport = "state.report";
constexpr const char* kRefusedExpired = "expired";
constexpr const char* kOtaUpdate = "sys.ota.update";
constexpr const char* kOtaStatus = "sys.ota.status";
// What capabilities lists under "system" when the firmware takes sys.ota.update.
constexpr const char* kSystemOta = "ota";
constexpr size_t kSha256HexLength = 64;

constexpr const char* kRegisterPath = "/api/v1/client-registrations/initiate";
constexpr const char* kLoginPath = "/api/v1/client-auth/login";
constexpr const char* kSocketPath = "/ws";

constexpr int kStatusPending = 0;
constexpr int kStatusRegistered = 1;

constexpr const char* kCodeUnauthorized = "CLIENT_UNAUTHORIZED";

// What the client sends at most: under the server's 10 a second with bursts of 100, leaving room for
// acks, pongs and state reports.
constexpr uint32_t kSendPerSecond = 8;
constexpr uint32_t kSendBurst = 50;

// Renew the access token this long before it expires.
constexpr unsigned long kTokenRenewBeforeMs = 60UL * 1000UL;

// A type only the platform may send: sys.* in any case, capabilities and state.report.
bool isReserved(const char* type);

// 1-100 characters and not reserved.
bool isSendableType(const char* type);

enum class UpgradeFailure {
    TokenRefused,  // the upgrade was answered 401: log in again
    Other,         // any other status, or the network: keep the token
    Unknown,       // no reason given (WebSockets before 2.7.2)
};

// Reads the reason WebSockets passes with WStype_DISCONNECTED when an upgrade fails: "HTTP 401",
// or "WebSocket handshake failed - HTTP 401" when the answer had no Connection: upgrade.
UpgradeFailure classifyUpgradeFailure(const char* reason, size_t length);

}  // namespace wbskt_core
