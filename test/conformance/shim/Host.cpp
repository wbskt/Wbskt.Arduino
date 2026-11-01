// Linux implementations of the shim headers: time, randomness, NVS in a file, HTTP and WebSocket
// over plain TCP.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <esp_system.h>
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

EspClass ESP;
WiFiClass WiFi;

namespace {

const auto kStart = std::chrono::steady_clock::now();

std::mt19937& generator() {
    static std::mt19937 instance{std::random_device{}()};
    return instance;
}

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

unsigned long millis() {
    return static_cast<unsigned long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kStart).count());
}

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

// WebSocketsClient

WebSocketsClient::~WebSocketsClient() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

void WebSocketsClient::beginSslWithCA(const char* host, uint16_t port, const char* url, const char*,
                                      const char* protocol) {
    port_ = port;
    splitHost(host, host_, port_);
    path_ = url;
    protocol_ = protocol ? protocol : "";
    // Like the library, begin() drops the socket without an event, and clears the authorization.
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    status_ = Status::NotConnected;
    authorization_.clear();
    buffer_.clear();
    lastConnectionFail_ = 0;
}

void WebSocketsClient::raise(WStype_t type, const uint8_t* payload, size_t length) {
    if (handler_) {
        handler_(type, const_cast<uint8_t*>(payload), length);
    }
}

void WebSocketsClient::loop() {
    if (port_ == 0) {
        return;
    }
    if (status_ == Status::NotConnected) {
        if (millis() - lastConnectionFail_ < reconnectInterval_) {
            return;
        }
        connect();
        return;
    }
    // What arrived before the peer closed (a refusal, a close frame) is handled first.
    const bool open = readAvailable();
    if (status_ == Status::Header) {
        readHeader();
    }
    if (status_ == Status::Connected) {
        readFrames();
    }
    if (!open && status_ != Status::NotConnected) {
        dropConnection("Connection lost");
    }
}

void WebSocketsClient::connect() {
    fd_ = connectTcp(host_, port_);
    if (fd_ < 0) {
        lastConnectionFail_ = millis();
        return;
    }
    lastConnectionFail_ = 0;
    status_ = Status::Header;
    std::string request = "GET " + path_ + " HTTP/1.1\r\nHost: " + host_ + ":" + std::to_string(port_) +
                          "\r\nConnection: Upgrade\r\nUpgrade: websocket\r\nSec-WebSocket-Version: 13\r\n"
                          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
    if (!protocol_.empty()) {
        request += "Sec-WebSocket-Protocol: " + protocol_ + "\r\n";
    }
    request += "User-Agent: arduino-WebSocket-Client\r\n";
    if (!authorization_.empty()) {
        request += "Authorization: " + authorization_ + "\r\n";
    }
    request += "\r\n";
    if (!writeAll(fd_, request.data(), request.size())) {
        dropConnection("Connection lost");
    }
}

bool WebSocketsClient::readAvailable() {
    char chunk[4096];
    while (true) {
        const ssize_t n = recv(fd_, chunk, sizeof(chunk), MSG_DONTWAIT);
        if (n > 0) {
            buffer_.append(chunk, static_cast<size_t>(n));
            continue;
        }
        if (n == 0) {
            return false;
        }
        return errno == EAGAIN || errno == EWOULDBLOCK;
    }
}

void WebSocketsClient::readHeader() {
    const size_t end = buffer_.find("\r\n\r\n");
    if (end == std::string::npos) {
        return;
    }
    std::istringstream lines(buffer_.substr(0, end));
    buffer_.erase(0, end + 4);
    std::string line;
    std::getline(lines, line);
    const int code = line.size() > 9 ? atoi(line.c_str() + 9) : 0;
    bool isUpgrade = false;
    bool hasAccept = false;
    while (std::getline(lines, line)) {
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string name = lower(trim(line.substr(0, colon)));
        const std::string value = lower(trim(line.substr(colon + 1)));
        if (name == "connection" && value == "upgrade") {
            isUpgrade = true;
        } else if (name == "sec-websocket-accept" && !value.empty()) {
            hasAccept = true;
        }
    }

    if (isUpgrade && code == 101 && hasAccept) {
        status_ = Status::Connected;
        raise(WStype_CONNECTED, reinterpret_cast<const uint8_t*>(path_.c_str()), path_.size());
        return;
    }
    const std::string reason =
        isUpgrade ? "HTTP " + std::to_string(code) : "WebSocket handshake failed - HTTP " + std::to_string(code);
    dropConnection(reason.c_str());
}

void WebSocketsClient::readFrames() {
    while (status_ == Status::Connected && buffer_.size() >= 2) {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(buffer_.data());
        const uint8_t opcode = p[0] & 0x0F;
        const bool masked = (p[1] & 0x80) != 0;
        uint64_t length = p[1] & 0x7F;
        size_t offset = 2;
        if (length == 126) {
            if (buffer_.size() < 4) {
                return;
            }
            length = (static_cast<uint64_t>(p[2]) << 8) | p[3];
            offset = 4;
        } else if (length == 127) {
            if (buffer_.size() < 10) {
                return;
            }
            length = 0;
            for (int i = 0; i < 8; ++i) {
                length = (length << 8) | p[2 + i];
            }
            offset = 10;
        }
        const size_t maskOffset = offset;
        if (masked) {
            offset += 4;
        }
        if (buffer_.size() < offset + length) {
            return;
        }
        std::string data = buffer_.substr(offset, static_cast<size_t>(length));
        if (masked) {
            for (size_t i = 0; i < data.size(); ++i) {
                data[i] = static_cast<char>(data[i] ^ p[maskOffset + i % 4]);
            }
        }
        buffer_.erase(0, offset + static_cast<size_t>(length));

        switch (opcode) {
            case 0x1:
                raise(WStype_TEXT, reinterpret_cast<const uint8_t*>(data.c_str()), data.size());
                break;
            case 0x2:
                raise(WStype_BIN, reinterpret_cast<const uint8_t*>(data.data()), data.size());
                break;
            case 0x8:
                sendFrame(0x8, reinterpret_cast<const uint8_t*>(data.data()), data.size() >= 2 ? 2 : 0);
                dropConnection(nullptr);
                return;
            case 0x9:
                sendFrame(0xA, reinterpret_cast<const uint8_t*>(data.data()), data.size());
                break;
            default:
                break;
        }
    }
}

bool WebSocketsClient::sendFrame(uint8_t opcode, const uint8_t* data, size_t length) {
    if (fd_ < 0) {
        return false;
    }
    std::string frame;
    frame += static_cast<char>(0x80 | opcode);
    if (length < 126) {
        frame += static_cast<char>(0x80 | length);
    } else if (length <= 0xFFFF) {
        frame += static_cast<char>(0x80 | 126);
        frame += static_cast<char>(length >> 8);
        frame += static_cast<char>(length & 0xFF);
    } else {
        frame += static_cast<char>(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            frame += static_cast<char>((static_cast<uint64_t>(length) >> (8 * i)) & 0xFF);
        }
    }
    const uint32_t mask = esp_random();
    const uint8_t key[4] = {static_cast<uint8_t>(mask), static_cast<uint8_t>(mask >> 8),
                            static_cast<uint8_t>(mask >> 16), static_cast<uint8_t>(mask >> 24)};
    frame.append(reinterpret_cast<const char*>(key), 4);
    for (size_t i = 0; i < length; ++i) {
        frame += static_cast<char>(data[i] ^ key[i % 4]);
    }
    return writeAll(fd_, frame.data(), frame.size());
}

bool WebSocketsClient::sendTXT(const char* payload) {
    if (status_ != Status::Connected) {
        return false;
    }
    return sendFrame(0x1, reinterpret_cast<const uint8_t*>(payload), strlen(payload));
}

void WebSocketsClient::disconnect() {
    if (status_ == Status::NotConnected) {
        return;
    }
    if (status_ == Status::Connected) {
        const uint8_t normal[2] = {0x03, 0xE8};
        sendFrame(0x8, normal, 2);
    }
    dropConnection(nullptr);
}

void WebSocketsClient::dropConnection(const char* reason) {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    status_ = Status::NotConnected;
    buffer_.clear();
    lastConnectionFail_ = millis();
    if (reason != nullptr && *reason != '\0') {
        raise(WStype_DISCONNECTED, reinterpret_cast<const uint8_t*>(reason), strlen(reason));
    } else {
        raise(WStype_DISCONNECTED, nullptr, 0);
    }
}
