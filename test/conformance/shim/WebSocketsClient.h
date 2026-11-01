// A stand-in for links2004/WebSockets 2.7.3 on Linux, over plain ws://. It copies the library's
// behaviour where the SDK depends on it:
// - loop() connects only while millis() - lastConnectionFail >= reconnectInterval; begin() sets
//   lastConnectionFail to 0, and every failure or disconnect sets it to millis();
// - a failed TCP connect raises no event;
// - a refused upgrade raises WStype_DISCONNECTED with "HTTP <code>" when the answer carried
//   Connection: upgrade, and "WebSocket handshake failed - HTTP <code>" when it did not;
// - sendTXT() is false unless connected, and isConnected() is true from the TCP connect on.
// The host may carry a port of its own ("127.0.0.1:6000"), which wins over the port argument.
#pragma once

#include <Arduino.h>

#include <functional>

enum WStype_t {
    WStype_ERROR,
    WStype_DISCONNECTED,
    WStype_CONNECTED,
    WStype_TEXT,
    WStype_BIN,
    WStype_FRAGMENT_TEXT_START,
    WStype_FRAGMENT_BIN_START,
    WStype_FRAGMENT,
    WStype_FRAGMENT_FIN,
    WStype_PING,
    WStype_PONG,
};

class WebSocketsClient {
public:
    using Handler = std::function<void(WStype_t type, uint8_t* payload, size_t length)>;

    ~WebSocketsClient();

    void beginSslWithCA(const char* host, uint16_t port, const char* url, const char* caCert, const char* protocol);
    void onEvent(Handler handler) { handler_ = std::move(handler); }
    void setAuthorization(const char* authorization) { authorization_ = authorization ? authorization : ""; }
    void setReconnectInterval(unsigned long interval) { reconnectInterval_ = interval; }
    void loop();
    bool sendTXT(const char* payload);
    bool isConnected() const { return status_ != Status::NotConnected; }
    void disconnect();

private:
    enum class Status { NotConnected, Header, Connected };

    void connect();
    void readHeader();
    void readFrames();
    bool sendFrame(uint8_t opcode, const uint8_t* data, size_t length);
    bool readAvailable();
    void dropConnection(const char* reason);
    void raise(WStype_t type, const uint8_t* payload, size_t length);

    Handler handler_;
    std::string host_;
    uint16_t port_ = 0;
    std::string path_;
    std::string protocol_;
    std::string authorization_;
    unsigned long reconnectInterval_ = 500;
    unsigned long lastConnectionFail_ = 0;
    Status status_ = Status::NotConnected;
    int fd_ = -1;
    std::string buffer_;
};
