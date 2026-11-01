// NVS, kept in the JSON file named by WBSKT_STORAGE so that a restart of the harness is a reboot.
#pragma once

#include <Arduino.h>

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false);
    void end();
    String getString(const char* key, const String& fallback = String());
    size_t putString(const char* key, const String& value);
    uint8_t getUChar(const char* key, uint8_t fallback = 0);
    size_t putUChar(const char* key, uint8_t value);
    bool remove(const char* key);
    bool clear();

private:
    std::string name_;
    bool readOnly_ = true;
    bool open_ = false;
};
