// The host is always online.
#pragma once

enum wl_status_t { WL_IDLE_STATUS = 0, WL_CONNECTED = 3, WL_DISCONNECTED = 6 };

class WiFiClass {
public:
    wl_status_t status() const { return WL_CONNECTED; }
};

extern WiFiClass WiFi;
