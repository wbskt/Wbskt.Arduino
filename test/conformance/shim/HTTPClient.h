// HTTPClient over a plain TCP socket. The URL may carry a port of its own after the host
// ("https://127.0.0.1:5000/..."); the scheme is ignored and the request goes out as plain HTTP.
#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>

#include <map>
#include <vector>

class HTTPClient {
public:
    bool begin(WiFiClientSecure& client, const String& url);
    void addHeader(const String& name, const String& value);
    void collectHeaders(const char* names[], size_t count);
    int POST(const String& body);
    String getString() const { return body_; }
    String header(const char* name) const;
    void end() {}

private:
    std::string host_;
    uint16_t port_ = 80;
    std::string path_;
    std::vector<std::pair<std::string, std::string>> requestHeaders_;
    std::vector<std::string> wanted_;
    std::map<std::string, std::string> responseHeaders_;
    String body_;
};
