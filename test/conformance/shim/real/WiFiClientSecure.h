// The fake hosts speak plain HTTP and ws://, so the "secure" client is the plain one with the
// certificate calls the SDK and the WebSockets library make.
#pragma once

#include <WiFiClient.h>

class WiFiClientSecure : public WiFiClient {
public:
    void setCACert(const char*) {}
    void setInsecure() {}
    void setCertificate(const char*) {}
    void setPrivateKey(const char*) {}
    void setCACertBundle(const uint8_t*, size_t = 0) {}
    bool verify(const char*, const char*) { return true; }
};
