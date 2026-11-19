// The WebSocket stand-in's implementation (shim/WebSocketsClient.h), over plain TCP.
#include <HostNet.h>
#include <WebSocketsClient.h>
#include <esp_system.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <random>
#include <sstream>

using hostnet::connectTcp;
using hostnet::lower;
using hostnet::splitHost;
using hostnet::trim;
using hostnet::writeAll;

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
    // The library's begin() sets _client.tcp to NULL without deleting it: on a board that leaks the
    // TLS client and its socket, so a caller must have the library free it first.
    if (_client.tcp != nullptr) {
        fprintf(stderr, "shim: begin() over a network client still allocated leaks it\n");
        abort();
    }
    _client.status = WSC_NOT_CONNECTED;
    authorization_.clear();
    buffer_.clear();
    lastConnectionFail_ = 0;
    lastPing_ = 0;
    pongReceived_ = false;
    pongTimeoutCount_ = 0;
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
    if (!clientIsConnected(&_client)) {
        if (millis() - lastConnectionFail_ < reconnectInterval_) {
            return;
        }
        connect();
        return;
    }
    // What arrived before the peer closed (a refusal, a close frame) is handled first.
    const bool open = readAvailable();
    if (_client.status == WSC_HEADER) {
        readHeader();
    }
    if (_client.status == WSC_CONNECTED) {
        readFrames();
    }
    if (!open && _client.status != WSC_NOT_CONNECTED) {
        dropConnection("Connection lost");
        return;
    }
    if (_client.status == WSC_CONNECTED) {
        heartbeat();
    }
}

void WebSocketsClient::heartbeat() {
    if (pingInterval_ == 0) {
        return;
    }
    if (millis() - lastPing_ > pingInterval_) {
        if (!sendFrame(0x9, nullptr, 0)) {
            dropConnection(nullptr);
            return;
        }
        lastPing_ = millis();
        pongReceived_ = false;
    }
    if (pongReceived_) {
        pongTimeoutCount_ = 0;
    } else if (millis() - lastPing_ > pongTimeout_) {
        ++pongTimeoutCount_;
        lastPing_ = millis() - pingInterval_ - 500;
        if (disconnectTimeoutCount_ != 0 && pongTimeoutCount_ >= disconnectTimeoutCount_) {
            dropConnection(nullptr);
        }
    }
}

void WebSocketsClient::connect() {
    _client.tcp = this;
    fd_ = connectTcp(host_, port_);
    if (fd_ < 0) {
        lastConnectionFail_ = millis();
        return;
    }
    lastConnectionFail_ = 0;
    _client.status = WSC_HEADER;
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
        _client.status = WSC_CONNECTED;
        raise(WStype_CONNECTED, reinterpret_cast<const uint8_t*>(path_.c_str()), path_.size());
        return;
    }
    const std::string reason =
        isUpgrade ? "HTTP " + std::to_string(code) : "WebSocket handshake failed - HTTP " + std::to_string(code);
    dropConnection(reason.c_str());
}

void WebSocketsClient::readFrames() {
    while (_client.status == WSC_CONNECTED && buffer_.size() >= 2) {
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
                raise(WStype_PING, reinterpret_cast<const uint8_t*>(data.data()), data.size());
                break;
            case 0xA:
                pongReceived_ = true;
                raise(WStype_PONG, reinterpret_cast<const uint8_t*>(data.data()), data.size());
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
    if (_client.status != WSC_CONNECTED) {
        return false;
    }
    return sendFrame(0x1, reinterpret_cast<const uint8_t*>(payload), strlen(payload));
}

void WebSocketsClient::closeWith(uint16_t code) {
    if (_client.status == WSC_NOT_CONNECTED) {
        return;
    }
    if (_client.status == WSC_CONNECTED) {
        const uint8_t payload[2] = {static_cast<uint8_t>(code >> 8), static_cast<uint8_t>(code & 0xFF)};
        sendFrame(0x8, payload, 2);
    }
    dropConnection(nullptr);
}

// The library's clientIsConnected(): true while connected or mid-handshake; otherwise it frees a
// network client it still holds, raising "TCP connection cleanup".
bool WebSocketsClient::clientIsConnected(WSclient_t*) {
    if (_client.tcp != nullptr && _client.status != WSC_NOT_CONNECTED) {
        return true;
    }
    if (_client.tcp != nullptr) {
        dropConnection("TCP connection cleanup");
    }
    return false;
}

void WebSocketsClient::disconnect() {
    if (clientIsConnected(&_client)) {
        closeWith(1000);
    }
}

void WebSocketsClient::cleanUp() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    _client.tcp = nullptr;
}

void WebSocketsClient::dropConnection(const char* reason) {
    cleanUp();
    _client.status = WSC_NOT_CONNECTED;
    buffer_.clear();
    lastConnectionFail_ = millis();
    if (reason != nullptr && *reason != '\0') {
        raise(WStype_DISCONNECTED, reinterpret_cast<const uint8_t*>(reason), strlen(reason));
    } else {
        raise(WStype_DISCONNECTED, nullptr, 0);
    }
}
