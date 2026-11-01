// The fake hosts speak plain HTTP and ws://, so there is nothing to verify.
#pragma once

class WiFiClientSecure {
public:
    void setCACert(const char*) {}
};
