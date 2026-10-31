// Sends a temperature reading every 10 seconds. Uses the ESP32's internal sensor so it runs on a
// bare board; swap in your own sensor's reading.

#include <WiFi.h>
#include <Wbskt.h>

const char* WIFI_SSID = "your-wifi";
const char* WIFI_PASSWORD = "your-password";
// A registration policy's PIN, from the console (Policies).
const char* POLICY_PIN = "000000";

WbsktClient wbskt("greenhouse-sensor", POLICY_PIN);
unsigned long lastReading = 0;

void setup() {
    Serial.begin(115200);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    wbskt.onConnected([] { Serial.println("connected to Wbskt"); });
    wbskt.onDisconnected([] { Serial.println("disconnected; reconnecting"); });
    wbskt.begin();
}

void loop() {
    wbskt.loop();

    if (millis() - lastReading >= 10000) {
        lastReading = millis();
        JsonDocument reading;
        reading["celsius"] = temperatureRead();
        // Queued while offline and sent in order once connected.
        wbskt.send("temperature", reading);
    }
}
