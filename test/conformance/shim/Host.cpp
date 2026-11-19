// Linux implementations of the shim headers: time, randomness, NVS in a file and HTTP over plain
// TCP. The WebSocket stand-in is in WebSocketsShim.cpp; the real library's network client in
// real/WiFiClient.cpp.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <HostNet.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>
#include <thread>

EspClass ESP;
WiFiClass WiFi;

using hostnet::connectTcp;
using hostnet::lower;
using hostnet::splitHost;
using hostnet::trim;
using hostnet::writeAll;

namespace {

const auto kStart = std::chrono::steady_clock::now();

std::mt19937& generator() {
    static std::mt19937 instance{std::random_device{}()};
    return instance;
}

std::string storagePath() {
    const char* path = getenv("WBSKT_STORAGE");
    return path != nullptr && *path != '\0' ? path : "wbskt-nvs.json";
}

JsonDocument loadStorage() {
    JsonDocument doc;
    std::ifstream in(storagePath());
    if (in) {
        std::stringstream text;
        text << in.rdbuf();
        deserializeJson(doc, text.str());
    }
    if (!doc.is<JsonObject>()) {
        doc.to<JsonObject>();
    }
    return doc;
}

void saveStorage(const JsonDocument& doc) {
    std::ofstream out(storagePath(), std::ios::trunc);
    std::string text;
    serializeJson(doc, text);
    out << text;
}

}  // namespace

namespace hostnet {

std::string lower(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

std::string trim(const std::string& text) {
    size_t start = text.find_first_not_of(" \t\r\n");
    size_t end = text.find_last_not_of(" \t\r\n");
    return start == std::string::npos ? "" : text.substr(start, end - start + 1);
}

// "host:port" into its parts; a bare host keeps the given port.
void splitHost(const std::string& text, std::string& host, uint16_t& port) {
    const size_t colon = text.rfind(':');
    if (colon == std::string::npos) {
        host = text;
        return;
    }
    host = text.substr(0, colon);
    port = static_cast<uint16_t>(atoi(text.c_str() + colon + 1));
}

int connectTcp(const std::string& host, uint16_t port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0) {
        return -1;
    }
    int fd = -1;
    for (addrinfo* a = found; a != nullptr; a = a->ai_next) {
        fd = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (fd < 0) {
            continue;
        }
        if (connect(fd, a->ai_addr, a->ai_addrlen) == 0) {
            break;
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(found);
    if (fd >= 0) {
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    return fd;
}

bool writeAll(int fd, const void* data, size_t length) {
    const char* p = static_cast<const char*>(data);
    while (length > 0) {
        const ssize_t n = send(fd, p, length, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        p += n;
        length -= static_cast<size_t>(n);
    }
    return true;
}

}  // namespace hostnet

unsigned long millis() {
    return static_cast<unsigned long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kStart).count());
}

void delay(unsigned long ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

BaseType_t xTaskCreate(TaskFunction_t task, const char*, uint32_t, void* argument, UBaseType_t, TaskHandle_t*) {
    std::thread(task, argument).detach();
    return pdPASS;
}

// A FreeRTOS task never returns from vTaskDelete(nullptr); here the thread just ends after it.
void vTaskDelete(TaskHandle_t) {}

// The host clock is already set.
void configTime(long, int, const char*) {}

uint32_t esp_random() { return generator()(); }

// Preferences

bool Preferences::begin(const char* name, bool readOnly) {
    name_ = name;
    readOnly_ = readOnly;
    open_ = true;
    return true;
}

void Preferences::end() { open_ = false; }

String Preferences::getString(const char* key, const String& fallback) {
    JsonDocument doc = loadStorage();
    const char* value = doc[name_][key];
    return value != nullptr ? String(value) : fallback;
}

size_t Preferences::putString(const char* key, const String& value) {
    if (!open_ || readOnly_) {
        return 0;
    }
    JsonDocument doc = loadStorage();
    doc[name_][key] = value.c_str();
    saveStorage(doc);
    return value.length();
}

bool Preferences::remove(const char* key) {
    if (!open_ || readOnly_) {
        return false;
    }
    JsonDocument doc = loadStorage();
    doc[name_].remove(key);
    saveStorage(doc);
    return true;
}

uint8_t Preferences::getUChar(const char* key, uint8_t fallback) {
    JsonDocument doc = loadStorage();
    JsonVariantConst value = doc[name_][key];
    return value.is<uint8_t>() ? value.as<uint8_t>() : fallback;
}

size_t Preferences::putUChar(const char* key, uint8_t value) {
    if (!open_ || readOnly_) {
        return 0;
    }
    JsonDocument doc = loadStorage();
    doc[name_][key] = value;
    saveStorage(doc);
    return 1;
}

bool Preferences::clear() {
    if (!open_ || readOnly_) {
        return false;
    }
    JsonDocument doc = loadStorage();
    doc.remove(name_);
    saveStorage(doc);
    return true;
}

// HTTPClient

bool HTTPClient::begin(WiFiClientSecure&, const String& url) {
    std::string text = url.str();
    const size_t scheme = text.find("://");
    if (scheme != std::string::npos) {
        text = text.substr(scheme + 3);
    }
    const size_t slash = text.find('/');
    path_ = slash == std::string::npos ? "/" : text.substr(slash);
    port_ = 80;
    splitHost(text.substr(0, slash), host_, port_);
    requestHeaders_.clear();
    responseHeaders_.clear();
    body_ = "";
    return !host_.empty();
}

void HTTPClient::addHeader(const String& name, const String& value) {
    requestHeaders_.emplace_back(name.str(), value.str());
}

void HTTPClient::collectHeaders(const char* names[], size_t count) {
    wanted_.clear();
    for (size_t i = 0; i < count; ++i) {
        wanted_.push_back(lower(names[i]));
    }
}

String HTTPClient::header(const char* name) const {
    auto found = responseHeaders_.find(lower(name));
    return found == responseHeaders_.end() ? String() : String(found->second);
}

int HTTPClient::POST(const String& body) {
    const int fd = connectTcp(host_, port_);
    if (fd < 0) {
        return -1;  // HTTPC_ERROR_CONNECTION_REFUSED
    }
    std::string request = "POST " + path_ + " HTTP/1.1\r\nHost: " + host_ + ":" + std::to_string(port_) +
                          "\r\nConnection: close\r\nContent-Length: " + std::to_string(body.length()) + "\r\n";
    for (const auto& header : requestHeaders_) {
        request += header.first + ": " + header.second + "\r\n";
    }
    request += "\r\n" + body.str();
    if (!writeAll(fd, request.data(), request.size())) {
        close(fd);
        return -1;
    }

    std::string response;
    char chunk[4096];
    ssize_t n;
    while ((n = recv(fd, chunk, sizeof(chunk), 0)) > 0) {
        response.append(chunk, static_cast<size_t>(n));
    }
    close(fd);

    const size_t headerEnd = response.find("\r\n\r\n");
    if (response.compare(0, 5, "HTTP/") != 0 || headerEnd == std::string::npos) {
        return -5;  // HTTPC_ERROR_CONNECTION_LOST
    }
    std::istringstream lines(response.substr(0, headerEnd));
    std::string line;
    std::getline(lines, line);
    const int status = atoi(line.c_str() + line.find(' ') + 1);
    while (std::getline(lines, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string name = lower(trim(line.substr(0, colon)));
        for (const std::string& wanted : wanted_) {
            if (wanted == name) {
                responseHeaders_[name] = trim(line.substr(colon + 1));
            }
        }
    }
    body_ = response.substr(headerEnd + 4);
    return status;
}
