// The ESP32 core's network client, over a plain TCP socket, for the real WebSockets library.
// The host may carry a port of its own ("127.0.0.1:6000"), which wins over the port argument, as in
// the other shims.
#pragma once

#include <Arduino.h>

class WiFiClient {
public:
    WiFiClient() = default;
    WiFiClient(const WiFiClient&) = delete;
    WiFiClient& operator=(const WiFiClient&) = delete;
    virtual ~WiFiClient();

    int connect(const char* host, uint16_t port);
    int connect(const char* host, uint16_t port, int32_t) { return connect(host, port); }
    size_t write(const uint8_t* data, size_t length);
    size_t write(uint8_t c) { return write(&c, 1); }
    int available();
    int read();
    int read(uint8_t* buffer, size_t length);
    size_t readBytes(uint8_t* buffer, size_t length);
    size_t readBytes(char* buffer, size_t length) { return readBytes(reinterpret_cast<uint8_t*>(buffer), length); }
    String readStringUntil(char terminator);
    void flush() {}
    void stop();
    uint8_t connected();
    explicit operator bool() { return connected(); }
    void setNoDelay(bool) {}
    void setTimeout(unsigned long ms) { timeoutMs_ = ms; }

private:
    bool fill(int waitMs);

    int fd_ = -1;
    std::string pending_;
    unsigned long timeoutMs_ = 1000;
};
