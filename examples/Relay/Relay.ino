// A relay the console can switch. It declares the relay.set command, so the console offers it with
// an On field, and reports the relay's state so the console shows it.

#include <WiFi.h>
#include <Wbskt.h>

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
const char* POLICY_PIN = "000000";
const int RELAY_PIN = 2;

WbsktClient wbskt("greenhouse-relay", POLICY_PIN);

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
    // The SDK has already confirmed delivery by the time this runs.
    wbskt.onMessage([](const WbsktMessage& message) {
        if (strcmp(message.type, "relay.set") == 0) {
            setRelay(message.payload["on"] | false);
        }
    });
    wbskt.onConnected([] { setRelay(digitalRead(RELAY_PIN) == HIGH); });
    wbskt.begin();
}

void loop() {
    wbskt.loop();
}
