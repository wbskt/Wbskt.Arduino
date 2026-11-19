// The host is always online; the real WebSockets library also wants the network client from here.
#pragma once

#include <WiFiClient.h>

enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };

class WiFiClass {
public:
    wl_status_t status() const { return WL_CONNECTED; }
};

extern WiFiClass WiFi;

class WiFiServer {};
