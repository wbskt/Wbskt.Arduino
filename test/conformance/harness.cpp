// Conformance harness for the ESP32 SDK: runs the real WbsktClient on Linux, over the shims in
// shim/, and drives it from JSON lines on stdin. See Tests/Conformance/README.md in wbskt/Wbskt.
#include <Wbskt.h>
#include <poll.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

bool running = true;

void emit(const JsonDocument& event) {
    std::string line;
    serializeJson(event, line);
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

void emit(const char* name) {
    JsonDocument event;
    event["event"] = name;
    emit(event);
}

// "http://127.0.0.1:5000" to "127.0.0.1:5000"; the shims take a port in the host.
std::string hostOf(const char* url) {
    std::string text = url != nullptr ? url : "";
    const size_t scheme = text.find("://");
    if (scheme != std::string::npos) {
        text = text.substr(scheme + 3);
    }
    const size_t slash = text.find('/');
    return slash == std::string::npos ? text : text.substr(0, slash);
}

WbsktDataType dataTypeOf(const char* name) {
    const std::string type = name != nullptr ? name : "string";
    if (type == "number") {
        return WbsktDataType::Number;
    }
    if (type == "boolean") {
        return WbsktDataType::Boolean;
    }
    if (type == "object") {
        return WbsktDataType::Object;
    }
    return WbsktDataType::String;
}

void declareCommands(WbsktClient& client, const char* json) {
    JsonDocument commands;
    if (json == nullptr || deserializeJson(commands, json) != DeserializationError::Ok) {
        return;
    }
    for (JsonObjectConst command : commands.as<JsonArrayConst>()) {
        WbsktCommandBuilder builder = client.addCommand(command["command"], command["description"] | "");
        for (JsonObjectConst p : command["parameters"].as<JsonArrayConst>()) {
            builder.param(p["name"], dataTypeOf(p["dataType"]), p["isRequired"] | false, p["label"] | "",
                          p["description"] | "");
        }
    }
}

void handle(WbsktClient& client, const std::string& line) {
    JsonDocument op;
    if (deserializeJson(op, line) != DeserializationError::Ok) {
        return;
    }
    const std::string name = op["op"] | "";
    if (name == "stop") {
        running = false;
        return;
    }
    JsonDocument event;
    bool accepted = false;
    if (name == "send") {
        event["type"] = op["type"];
        accepted = client.send(op["type"] | "", op["payload"].as<JsonVariantConst>());
    } else if (name == "state") {
        event["type"] = "state.report";
        JsonDocument values;
        values.set(op["values"]);
        accepted = client.reportState(values);
    } else {
        return;
    }
    event["event"] = accepted ? "sent" : "rejected";
    if (!accepted) {
        event["reason"] = "send returned false";
    }
    emit(event);
}

}  // namespace

int main() {
    // Built once per run: the config keeps pointers into these.
    static const std::string apiHost = hostOf(getenv("WBSKT_API_URL"));
    static const std::string socketHost = hostOf(getenv("WBSKT_SOCKET_URL"));
    WbsktConfig config;
    config.apiHost = apiHost.c_str();
    config.socketHost = socketHost.c_str();
    config.ntpServer = nullptr;

    const char* pin = getenv("WBSKT_POLICY_PIN");
    WbsktClient client(getenv("WBSKT_DEVICE_NAME"), pin != nullptr && *pin != '\0' ? pin : nullptr, config);
    declareCommands(client, getenv("WBSKT_COMMANDS"));
    client.onConnected([] { emit("connected"); });
    client.onDisconnected([] { emit("disconnected"); });
    client.onMessage([](const WbsktMessage& message) {
        JsonDocument event;
        event["event"] = "message";
        event["type"] = message.type;
        event["payload"] = message.payload;
        if (message.commandId != nullptr) {
            event["commandId"] = message.commandId;
        } else {
            event["commandId"] = nullptr;
        }
        emit(event);
    });

    client.begin();
    emit("started");

    std::string pending;
    while (running) {
        pollfd in{STDIN_FILENO, POLLIN, 0};
        while (running && poll(&in, 1, 0) > 0) {
            char chunk[4096];
            const ssize_t n = read(STDIN_FILENO, chunk, sizeof(chunk));
            if (n <= 0) {
                running = false;
                break;
            }
            pending.append(chunk, static_cast<size_t>(n));
            size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos) {
                handle(client, pending.substr(0, newline));
                pending.erase(0, newline + 1);
            }
        }
        client.loop();
        usleep(2000);
    }
    client.end();
    return 0;
}
