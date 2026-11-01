# Wbskt for ESP32

Connect an ESP32 to [Wbskt](https://wbskt.com). The board registers once with a policy PIN and
keeps its credentials in flash. It holds a TLS WebSocket open, sends readings and takes commands
from the console and from workflows. After a drop it reconnects without help.

```cpp
#include <WiFi.h>
#include <Wbskt.h>

WbsktClient wbskt("greenhouse-sensor", "483920");

void setup() {
    WiFi.begin("your-wifi", "your-password");
    wbskt.begin();
}

void loop() {
    wbskt.loop();

    static unsigned long last = 0;
    if (millis() - last >= 10000) {
        last = millis();
        JsonDocument reading;
        reading["celsius"] = temperatureRead();
        wbskt.send("temperature", reading);
    }
}
```

Get the PIN from a registration policy in the console (**Policies**). On first boot the board
registers and stores its credentials in NVS. After that it signs in with the stored credentials,
including after a reflash that keeps NVS. If the policy needs approval, the board keeps retrying
and connects once someone approves it.

## Install

- **Arduino IDE:** install the ESP32 boards (Espressif, 2.x or 3.x). Then add this library from a
  ZIP, together with **ArduinoJson** (7.x) and **WebSockets** by Markus Sattler (2.6 or later)
  from the Library Manager.
- **PlatformIO:**

  ```ini
  [env:esp32dev]
  platform = espressif32
  board = esp32dev
  framework = arduino
  lib_deps =
      https://github.com/wbskt/Wbskt.Arduino.git
      bblanchon/ArduinoJson@^7.0.0
      links2004/WebSockets@^2.6.0
  board_build.partitions = min_spiffs.csv
  ```

WiFi and TLS together come to about 1.3 MB, which is more than the default 1.2 MB app partition.
Pick a bigger app partition that still allows OTA: **Minimal SPIFFS (1.9MB APP with OTA)** in the
Arduino IDE (Tools → Partition Scheme), or `min_spiffs.csv` in PlatformIO.

## Commands

```cpp
wbskt.addCommand("relay.set", "Switch the relay").param("on", WbsktDataType::Boolean, true, "On");

wbskt.onMessage([](const WbsktMessage& message) {
    if (strcmp(message.type, "relay.set") == 0) {
        digitalWrite(RELAY_PIN, message.payload["on"] | false);
    }
});
```

Declare commands before `begin()` so the console can offer them with a form. The SDK confirms
delivery as soon as a command arrives, before your handler runs. It refuses a command that has
expired and never passes it to your handler. If the outcome matters, report it as a message of
your own. To add commands after connecting, call `publishCapabilities()`.

See `examples/Relay` for a full sketch.

## State

```cpp
JsonDocument state;
state["humidity"] = 41;
wbskt.reportState(state);
```

Send only what changed. Numbers are kept as readings and charted in the console.

## Offline

`send` and `reportState` don't wait for the network. While the board is offline, messages wait in
RAM, 32 by default (`offlineBufferSize`), with the oldest dropped first. They go out in order once
the connection is back. `droppedMessages()` counts the messages that were dropped.

Each message carries the time it was made. To know that time, the SDK sets the clock over NTP
(`pool.ntp.org`). It needs the same clock to refuse expired commands. Until the clock is set,
messages go out without a time, and the server uses their arrival time instead.

## Configuration

```cpp
WbsktConfig config;
config.apiHost = "api.example.com";    // self-hosted: api.<DOMAIN>
config.socketHost = "ws.example.com";  // ws.<DOMAIN>
config.rootCa = MY_ROOT_CA_PEM;        // default: Let's Encrypt's ISRG Root X1 and X2
config.offlineBufferSize = 16;
config.ntpServer = nullptr;            // you set the clock yourself
WbsktClient wbskt("greenhouse-sensor", "483920", config);
```

TLS is always verified. The default roots cover the hosted service and any self-hosted install
using the default Traefik + Let's Encrypt setup.

## Limits

- A message is at most 64 KB. In practice the WebSockets library receives frames of up to 15 KB on
  ESP32 (`WEBSOCKETS_MAX_DATA_SIZE`), so keep commands small.
- Types starting with `sys.`, plus `capabilities` and `state.report`, are reserved. `send` returns
  `false` for them.
- `loop()` blocks for a few seconds while the board signs in. This happens on the first connection
  and about once an hour after that, because the access token is reused until a minute before it
  expires.
- The WebSockets library doesn't pass on the close code. A server restart is therefore handled
  like any other drop: the board reconnects after a random 0–10 s instead of 0–30 s.

## Protocol

The library implements
[the device protocol](https://github.com/wbskt/Wbskt/blob/master/Docs/Device.Protocol.md):
- credentials in NVS, and registering again only after `CLIENT_UNAUTHORIZED`;
- reusing the access token, and renewing it after a 401 on the upgrade;
- sending `capabilities` first on every connection;
- answering `sys.ping`;
- acking commands and refusing expired ones;
- the offline buffer;
- reconnecting with full jitter, and honouring `Retry-After`.

The parts with no hardware dependency, in `src/wbskt/`, have host tests: `make -C test/host` after
cloning ArduinoJson into `test/host/ArduinoJson`.

The whole client also runs against the
[conformance suite](https://github.com/wbskt/Wbskt/tree/master/Tests/Conformance).
`test/conformance` builds `WbsktClient` for Linux, using shims of the Arduino core, HTTPClient,
Preferences and WebSockets in `test/conformance/shim/`, and CI runs the suite against it:

```sh
make -C test/conformance
cd <wbskt/Wbskt>/Tests/Conformance
WBSKT_HARNESS=<this repo>/test/conformance/wbskt-harness python -m pytest -v
```

The WebSockets shim copies the parts of the library's behaviour (2.7.3) that the client depends
on: when `loop()` connects, which failures raise an event, and the reasons it gives. If a newer
library changes any of these, change the shim too.

## License

MIT
