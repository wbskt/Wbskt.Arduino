// A stand-in for links2004/WebSockets 2.7.3 on Linux, over plain ws://. It copies the library's
// behaviour where the SDK depends on it:
// - loop() connects only while millis() - lastConnectionFail >= reconnectInterval; begin() sets
//   lastConnectionFail to 0, and every failure or disconnect sets it to millis();
// - a failed TCP connect raises no event;
// - a refused upgrade raises WStype_DISCONNECTED with "HTTP <code>" when the answer carried
//   Connection: upgrade, and "WebSocket handshake failed - HTTP <code>" when it did not;
// - sendTXT() is false unless connected, and isConnected() is true only once the 101 is read, which
//   happens on a later loop() than the one that connects and sends the upgrade request;
// - a connect allocates the network client (_client.tcp) whether or not it succeeds. Only the
//   library frees it: disconnect(), or the next loop() while not connected, cleans it up and raises
//   WStype_DISCONNECTED "TCP connection cleanup". begin() sets the pointer to NULL without freeing
//   it, so begin() over a client still allocated leaks it; here that aborts the harness;
// - the heartbeat: a ping every interval while connected, and a disconnect once `misses` pongs in
//   a row haven't come back within the timeout (WebSockets::handleHBTimeout).
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

enum WSclientsStatus_t { WSC_NOT_CONNECTED, WSC_HEADER, WSC_BODY, WSC_CONNECTED };

// The fields of the library's WSclient_t a subclass reads. tcp is only a marker here: non-null
// while the library holds a network client, as it is in the library.
struct WSclient_t {
    void* tcp = nullptr;
    WSclientsStatus_t status = WSC_NOT_CONNECTED;
};

// The library's base, as far as a subclass reaches into it: clientDisconnect(client, code) sends a
// close frame with that code, then drops the connection.
class WebSockets {
protected:
    virtual ~WebSockets() = default;
    void clientDisconnect(WSclient_t*, uint16_t code) { closeWith(code); }
    virtual void closeWith(uint16_t code) = 0;
};

class WebSocketsClient : protected WebSockets {
public:
    using Handler = std::function<void(WStype_t type, uint8_t* payload, size_t length)>;

    ~WebSocketsClient();

    void beginSslWithCA(const char* host, uint16_t port, const char* url, const char* caCert, const char* protocol);
    void onEvent(Handler handler) { handler_ = std::move(handler); }
    void setAuthorization(const char* authorization) { authorization_ = authorization ? authorization : ""; }
    void setReconnectInterval(unsigned long interval) { reconnectInterval_ = interval; }
    void loop();
    bool sendTXT(const char* payload);
    void enableHeartbeat(uint32_t pingInterval, uint32_t pongTimeout, uint8_t disconnectTimeoutCount) {
        pingInterval_ = pingInterval;
        pongTimeout_ = pongTimeout;
        disconnectTimeoutCount_ = disconnectTimeoutCount;
        pongReceived_ = false;
    }
    bool isConnected() const { return _client.status == WSC_CONNECTED; }
    void disconnect();

protected:
    WSclient_t _client;
    bool clientIsConnected(WSclient_t*);
    void closeWith(uint16_t code) override;

private:
    void connect();
    void readHeader();
    void readFrames();
    bool sendFrame(uint8_t opcode, const uint8_t* data, size_t length);
    bool readAvailable();
    void dropConnection(const char* reason);
    void cleanUp();
    void raise(WStype_t type, const uint8_t* payload, size_t length);
    void heartbeat();

    Handler handler_;
    std::string host_;
    uint16_t port_ = 0;
    std::string path_;
    std::string protocol_;
    std::string authorization_;
    unsigned long reconnectInterval_ = 500;
    unsigned long lastConnectionFail_ = 0;
    int fd_ = -1;
    std::string buffer_;
    uint32_t pingInterval_ = 0;
    uint32_t pongTimeout_ = 0;
    uint8_t disconnectTimeoutCount_ = 0;
    unsigned long lastPing_ = 0;
    bool pongReceived_ = false;
    uint8_t pongTimeoutCount_ = 0;
};
