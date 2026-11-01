// A relay whose firmware the console can update over the air. Bump FIRMWARE_VERSION, export the
// compiled binary, host the .bin on HTTPS and send sys.ota.update with its URL and SHA-256.
// Build with a partition scheme that has OTA (Minimal SPIFFS).

#include <WiFi.h>
#include <Wbskt.h>

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const char* POLICY_PIN = "000000";
const char* FIRMWARE_VERSION = "1.0.0";
const int RELAY_PIN = 2;

WbsktClient wbskt("garage-relay", POLICY_PIN);

void setRelay(bool on) {
    digitalWrite(RELAY_PIN, on ? HIGH : LOW);
    JsonDocument state;
    state["relay"] = on;
    wbskt.reportState(state);
}

void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    wbskt.addCommand("relay.set", "Switch the relay").param("on", WbsktDataType::Boolean, true, "On");
    wbskt.onMessage([](const WbsktMessage& message) {
        if (strcmp(message.type, "relay.set") == 0) {
            setRelay(message.payload["on"] | false);
        }
    });
    wbskt.onConnected([] { setRelay(digitalRead(RELAY_PIN) == HIGH); });

    // Every firmware you ship keeps this line, so the next update can reach it too.
    wbskt.enableOta(FIRMWARE_VERSION);
    wbskt.onOtaRequest([](const WbsktOtaRequest& request) {
        Serial.printf("update to %s requested\n", request.version);
        return true;
    });
    wbskt.onOta([](WbsktOtaState state, uint8_t percent) {
        if (state == WbsktOtaState::Accepted) {
            // The relay stays as it is while loop() is blocked downloading; switch it off to be safe.
            digitalWrite(RELAY_PIN, LOW);
        } else if (state == WbsktOtaState::Downloading) {
            Serial.printf("downloading %u%%\n", percent);
        } else if (state == WbsktOtaState::Installed) {
            Serial.printf("running %s\n", FIRMWARE_VERSION);
        }
    });
    wbskt.begin();
}

void loop() {
    wbskt.loop();
}
