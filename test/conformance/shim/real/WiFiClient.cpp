// WiFiClient over a plain, non-blocking-read TCP socket, and the ESP32 SHA call, for building the
// harness over the real WebSockets library.
#include <HostNet.h>
#include <WiFiClient.h>
#include <poll.h>
#include <sha/sha_parallel_engine.h>
#include <sys/socket.h>
#include <unistd.h>

// libsha1.h hides itself on an ESP32, where the library uses the SHA engine instead.
#undef ESP32
extern "C" {
#include "libsha1/libsha1.h"
}

WiFiClient::~WiFiClient() { stop(); }

int WiFiClient::connect(const char* host, uint16_t port) {
    stop();
    std::string name;
    hostnet::splitHost(host ? host : "", name, port);
    fd_ = hostnet::connectTcp(name, port);
    return fd_ >= 0 ? 1 : 0;
}

size_t WiFiClient::write(const uint8_t* data, size_t length) {
    if (fd_ < 0 || !hostnet::writeAll(fd_, data, length)) {
        return 0;
    }
    return length;
}

// Reads what the socket has, waiting up to waitMs for something to arrive. False once it closed.
bool WiFiClient::fill(int waitMs) {
    if (fd_ < 0) {
        return false;
    }
    pollfd p{fd_, POLLIN, 0};
    if (poll(&p, 1, waitMs) <= 0) {
        return true;
    }
    char buffer[4096];
    const ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
    if (n <= 0) {
        close(fd_);
        fd_ = -1;
        return false;
    }
    pending_.append(buffer, static_cast<size_t>(n));
    return true;
}

int WiFiClient::available() {
    if (pending_.empty()) {
        fill(0);
    }
    return static_cast<int>(pending_.size());
}

int WiFiClient::read() {
    uint8_t c;
    return read(&c, 1) == 1 ? c : -1;
}

int WiFiClient::read(uint8_t* buffer, size_t length) {
    if (pending_.empty()) {
        fill(0);
    }
    const size_t n = pending_.size() < length ? pending_.size() : length;
    memcpy(buffer, pending_.data(), n);
    pending_.erase(0, n);
    return static_cast<int>(n);
}

// Stream::readBytes: waits up to the timeout for the rest.
size_t WiFiClient::readBytes(uint8_t* buffer, size_t length) {
    const unsigned long started = millis();
    size_t got = 0;
    while (got < length) {
        got += static_cast<size_t>(read(buffer + got, length - got));
        if (got == length || millis() - started >= timeoutMs_) {
            break;
        }
        if (!fill(10) && pending_.empty()) {
            break;
        }
    }
    return got;
}

// Stream::readStringUntil: waits up to the timeout for each character.
String WiFiClient::readStringUntil(char terminator) {
    std::string line;
    unsigned long last = millis();
    while (true) {
        if (pending_.empty()) {
            if (millis() - last >= timeoutMs_ || (!fill(10) && pending_.empty())) {
                break;
            }
            continue;
        }
        const char c = pending_[0];
        pending_.erase(0, 1);
        last = millis();
        if (c == terminator) {
            break;
        }
        line += c;
    }
    return String(line);
}

void WiFiClient::stop() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    pending_.clear();
}

uint8_t WiFiClient::connected() {
    if (fd_ >= 0 && pending_.empty()) {
        // A peer that closed reads as 0 bytes: the ESP32 core reports that as not connected.
        pollfd p{fd_, POLLIN, 0};
        if (poll(&p, 1, 0) > 0) {
            fill(0);
        }
    }
    return fd_ >= 0 || !pending_.empty();
}

void esp_sha(esp_sha_type, const unsigned char* input, size_t length, unsigned char* output) {
    SHA1_CTX ctx;
    SHA1Init(&ctx);
    SHA1Update(&ctx, input, static_cast<uint32_t>(length));
    SHA1Final(output, &ctx);
}
