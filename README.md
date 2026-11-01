# Wbskt for ESP32

Connect an ESP32 to [Wbskt](https://wbskt.com). The board registers once with a policy PIN and
keeps its credentials in flash. It holds a TLS WebSocket open, sends readings and takes commands
from the console and from workflows. After a drop it reconnects without help, and readings made
while it was offline go out in order once it is back.

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

- [Install](#install)
- [Sending readings](#sending-readings)
- [State](#state)
- [Commands](#commands)
- [Events](#events)
- [Configuration](#configuration)
- [API](#api)
- [How the connection behaves](#how-the-connection-behaves)
- [Memory and limits](#memory-and-limits)
- [Self-hosting](#self-hosting)
- [Troubleshooting](#troubleshooting)
- [Protocol and tests](#protocol-and-tests)

## Install

- **Arduino IDE:** install the ESP32 boards (Espressif, 2.x or 3.x). Then add this library from a
  ZIP, together with **ArduinoJson** (7.x) and **WebSockets** by Markus Sattler (2.6 or later; 2.7.2
  or later recommended, see [How the connection behaves](#how-the-connection-behaves)) from the
  Library Manager.
- **PlatformIO:**

  ```ini
  [env:esp32dev]
  platform = espressif32
  board = esp32dev
  framework = arduino
  lib_deps =
      https://github.com/wbskt/Wbskt.Arduino.git
      bblanchon/ArduinoJson@^7.0.0
      links2004/WebSockets@^2.7.2
  board_build.partitions = min_spiffs.csv
  ```

WiFi and TLS together come to about 1.3 MB, which is more than the default 1.2 MB app partition.
Pick a bigger app partition that still allows OTA: **Minimal SPIFFS (1.9MB APP with OTA)** in the
Arduino IDE (Tools → Partition Scheme), or `min_spiffs.csv` in PlatformIO.

The library runs on the ESP32, ESP32-S3 and ESP32-C3, which CI builds for. Other ESP32 variants
with WiFi should work. AVR, ESP8266 and boards without WiFi are not supported.

## Sending readings

```cpp
JsonDocument reading;
reading["celsius"] = 21.5;
wbskt.send("temperature", reading);
```

`send(type, payload)` takes any JSON value as the payload. The type is yours: workflows trigger on
it and the console shows it. It may be 1–100 characters. Types starting with `sys.` (in any case),
plus `capabilities` and `state.report`, belong to the platform, and `send` returns `false` for them.

`send` never waits for the network. It returns `true` once the message is sent or queued:

- **Connected:** the message goes out at once, unless older messages are still waiting. A new
  message never overtakes the queue.
- **Offline, or signing in:** it waits in the offline buffer (16 KB by default, see
  [Configuration](#configuration)). When the buffer is full, the oldest messages are dropped first.
  `droppedMessages()` counts them, and `queuedMessages()` says how many are waiting.
- **Over the send rate:** the client sends at most 8 messages a second, with bursts of up to 50.
  Anything faster waits in the same buffer. The server allows 10 a second with bursts of 100 and
  silently drops the excess, so the client keeps under that with room for its own acks and pongs.

Every message carries `sentAt`, the time it was made, so the server can tell a late (buffered)
reading from a live one. The clock comes from NTP (`pool.ntp.org` by default). A message made
before the clock is set records how long ago it was made, and gets its `sentAt` worked out once the
clock is set. If the clock still isn't set when the message goes out, it is sent without `sentAt`
and the server uses its arrival time.

## State

```cpp
JsonDocument state;
state["humidity"] = 41;
state["door"] = "open";
wbskt.reportState(state);
```

`reportState` sends the state variables that changed. The console shows the latest value of each,
and keeps numbers as readings for its charts. Send only what changed. State reports go through the
same buffer and rate as `send`.

## Commands

```cpp
wbskt.addCommand("relay.set", "Switch the relay")
    .param("on", WbsktDataType::Boolean, true, "On");

wbskt.onMessage([](const WbsktMessage& message) {
    if (strcmp(message.type, "relay.set") == 0) {
        digitalWrite(RELAY_PIN, message.payload["on"] | false);
    }
});
```

Declare commands before `begin()`. The client sends them as its `capabilities` on every connection,
so the console can offer each one with a form. `.param(name, type, required, label, description)`
adds a field. The type is `WbsktDataType::String`, `Number`, `Boolean` or `Object`. The label
defaults to the name. To add commands once connected, call `addCommand` and then
`publishCapabilities()`.

The client confirms delivery (`sys.ack`) as soon as a command arrives, before your handler runs.
That is delivery, not success: if the outcome matters, report it as a message or state of your own.
A command whose `expiresAt` has passed is refused and never reaches your handler. That check needs
the clock, so until NTP has set it, no command is refused as expired.

`onMessage` also gets plain messages that ask for no ack; their `commandId` is `nullptr`. The
`payload` and strings in `WbsktMessage` are valid only during the call. Copy what you need to keep.

See `examples/Relay` for a full sketch.

## Events

```cpp
wbskt.onConnected([] { Serial.println("connected"); });
wbskt.onDisconnected([] { Serial.println("disconnected; reconnecting"); });
```

`onConnected` runs once the socket is open and the capabilities have gone out, along with the first
burst of any queued messages. It is a good place to report your current state. `onDisconnected` runs when an open connection
drops, and when `end()` closes it. Failed attempts to connect don't raise it. Every handler runs
inside `wbskt.loop()`, on the sketch's own task.

## Firmware updates (OTA)

```cpp
wbskt.enableOta("1.0.0");  // before begin()
```

With OTA on, the board takes `sys.ota.update` from the server and updates itself. The command
carries a link to the firmware, not the firmware: a binary is 1–2 MB and a message is at most 64 KB.

```json
{ "type": "sys.ota.update", "commandId": "…",
  "payload": { "url": "https://…/garage-relay-1.1.0.bin", "sha256": "<64 hex digits>", "version": "1.1.0", "size": 1212416 } }
```

`url` and `sha256` are required. `version` and `size` are optional. The console doesn't send this
command yet ([wbskt/Wbskt#510](https://github.com/wbskt/Wbskt/issues/510)), but the library is
ready for it.

1. **Build** the new firmware with a higher version (Arduino IDE: Sketch → Export Compiled Binary;
   PlatformIO: `.pio/build/<env>/firmware.bin`). Get its digest with `sha256sum firmware.bin`.
2. **Host** the `.bin` anywhere the board can reach over HTTPS. Redirects are followed. The
   download is checked against `rootCa`, so a host whose certificate isn't from Let's Encrypt
   needs its roots in `otaRootCa`. GitHub release assets are one example.
3. **Send** `sys.ota.update`. The board acks it and answers with `sys.ota.status` messages:
   `accepted`, then `downloading`, then it restarts into the new firmware. Once that firmware has
   connected, the board sends `installed` with the version. If the update can't go ahead, the
   board answers `rejected`; if it goes wrong partway, `failed`. Both carry an `error`.

What the board does on its own:

- It **refuses** a command over plain `http`, one without a valid SHA-256, one bigger than the
  OTA partition, and any command while OTA is off or another update is under way. `onOtaRequest`
  can refuse one too, for example on low battery.
- It **closes the socket** while it downloads, to free the heap a second TLS session needs, and
  `loop()` is blocked until the download finishes (about 20–60 s). `onOta` reports progress.
- It **checks the SHA-256** before the new firmware is made bootable. A wrong digest, a cut-off
  download or a bad image leaves the running firmware in place. The board reconnects and reports
  `failed`.
- It **goes back** to the previous firmware if the new one doesn't connect within
  `otaConfirmTimeoutMs` (5 minutes from boot), or restarts `otaTrialBoots` (3) times without
  connecting. The old firmware then reports `failed`. The timer counts from boot whatever the
  reason, so a WiFi outage right after an update rolls back too; the update can just be sent
  again. A firmware that crashes before `begin()` isn't caught.
- It **keeps the credentials**. NVS isn't touched, so the board signs in as the same client.
- It reports the version as the `firmware` state variable on every connection, and lists `"ota"`
  under `system` in its capabilities.

An update replaces the whole program: your code, this library and the Arduino core all ship
together, so you can also move a board to something else entirely. **Every firmware you ship must
keep an update path**, `enableOta` or one of your own. A board running firmware without one can
only be updated over USB after that.

The partition table and bootloader can't change over the air. Build with a scheme that has two
app partitions (Minimal SPIFFS, see [Install](#install)) before the first board goes out.

On security: the download is HTTPS with verified certificates, and the digest is checked, so a
swapped or damaged file is refused. Anyone who can send commands in the workspace can update the
board. The image isn't signed, so a compromised host plus a matching digest in the command would
get through; for that, see ESP32 Secure Boot (which can't be undone on the chip).

See `examples/Ota` for a full sketch.

## Configuration

Pass a `WbsktConfig` as the third constructor argument. Every field has a default, so set only what
you need.

```cpp
WbsktConfig config;
config.apiHost = "api.example.com";
config.socketHost = "ws.example.com";
config.offlineBufferBytes = 32 * 1024;
WbsktClient wbskt("greenhouse-sensor", "483920", config);
```

| Field | Default | |
|---|---|---|
| `apiHost` | `"api.dev.wbskt.com"` | Host for registration and login, over HTTPS. |
| `socketHost` | `"ws.dev.wbskt.com"` | Host for the WebSocket, over TLS. |
| `port` | `443` | Port for both hosts. |
| `rootCa` | `nullptr` | PEM root certificates the hosts chain to. `nullptr` uses Let's Encrypt's ISRG Root X1 and X2, which the hosted service and a default self-hosted install use. TLS is always verified. |
| `offlineBufferBytes` | `16384` | RAM for messages waiting to go out, allocated once in the constructor. Each message takes its JSON size plus 16 bytes. A 60-byte reading takes 76, so the default holds about 200 of them: half an hour of one reading every 10 s. `0` turns buffering off, and anything that can't be sent at once is dropped. |
| `ntpServer` | `"pool.ntp.org"` | NTP server that sets the clock for `sentAt` and command expiry. `nullptr` if the sketch sets the clock itself (`configTime`, a GPS or an RTC). |
| `heartbeatIntervalMs` | `20000` | How often to ping the server while connected. `0` turns the heartbeat off. |
| `heartbeatTimeoutMs` | `10000` | How long to wait for each pong. |
| `heartbeatMisses` | `2` | Missed pongs in a row before the link counts as dead and the client reconnects. With the defaults a dead link is noticed within about 40 s. |
| `otaRootCa` | `nullptr` | PEM roots for the host firmware is downloaded from. `nullptr` uses `rootCa`. |
| `otaConfirmTimeoutMs` | `300000` | After an update, how long from boot the new firmware has to connect before the board goes back to the previous one. `0` waits for ever. |
| `otaTrialBoots` | `3` | Restarts without connecting after an update before the board goes back. |
| `backgroundSignIn` | `true` | Run registration and login on a short-lived FreeRTOS task, so `loop()` keeps running during the TLS handshake. `false` signs in inside `loop()`, which then blocks for 1–3 s. |

## API

| | |
|---|---|
| `WbsktClient(name, pin, config = {})` | `name` is how the board shows in the console. `pin` is the policy PIN, or `nullptr` for a board whose credentials are already in NVS. |
| `begin()` | Reads the stored credentials and starts connecting. Call it once WiFi is set up. |
| `loop()` | Does all the work. Call it from `loop()` as often as possible. |
| `end()` | Closes the connection and stops. `begin()` starts again. |
| `connected()` | `true` while the socket is open. |
| `send(type, payload)` | Sends or queues a message. `false` for a reserved or empty type, or a message over 64 KB. |
| `reportState(values)` | Sends or queues a state report. |
| `addCommand(name, description)` | Declares a command. Chain `.param(...)` for its fields. |
| `publishCapabilities()` | Sends the commands again, after adding some once connected. |
| `onMessage(handler)` | Commands and messages from the server. |
| `onConnected(handler)`, `onDisconnected(handler)` | Connection events. |
| `enableOta(version)` | Takes firmware updates from the server, and reports `version` as the `firmware` state. |
| `onOtaRequest(handler)` | Return `false` to refuse an update before it starts. |
| `onOta(handler)` | Update progress: `Accepted`, `Downloading` with a percentage, `Installed`, `Failed`, `Rejected`. |
| `queuedMessages()` | Messages waiting in the buffer. |
| `droppedMessages()` | Messages dropped because the buffer was full, since boot. |
| `forgetCredentials()` | Deletes the stored credentials, so the next connection registers again with the PIN. |

## How the connection behaves

**Credentials.** The board registers once with the PIN and keeps the device id and secret in NVS
(namespace `wbskt`). It never registers again while it has them, because each registration is a new
device in the console and counts against the policy's limit. It discards them only when the server
says they are no longer valid (`CLIENT_UNAUTHORIZED`: the device was deleted, or its secret
rotated), and then registers again. With no credentials and no PIN, the client logs an error and
stops.

**Token.** Login returns an access token, which is reused for every reconnect until a minute before
it expires (an hour today). The board logs in again only then, or when the socket refuses the token
with a 401. A refusal for any other reason, such as a 503 while the socket host restarts, keeps the
token. That way a server restart doesn't turn into a login storm. WebSockets before 2.7.2 doesn't
say why an upgrade failed, so with those versions the client drops the token after three failed
upgrades in a row.

**Reconnecting.** Every wait is drawn at random from zero up to a ceiling, so boards that lost the
same host come back spread out, not all at once:

| Situation | Wait |
|---|---|
| First connection after `begin()` | none |
| An open connection dropped | 0–10 s |
| The *n*-th failed attempt in a row | 0 to min(60 s, 2 s × 2ⁿ) |
| Registration or login answered 429 | `Retry-After` + 0–2 s |
| WiFi not connected | checked again every second |

A successful connection resets the failure count. The WebSockets library doesn't pass on close
codes, so a server restart (1001) is handled like any other drop: 0–10 s rather than the protocol's
0–30 s.

**Dead links.** If the access point vanishes or the network drops packets without closing the TCP
connection, the board can't tell by itself. The heartbeat (above) pings the server, and the client
reconnects when the pongs stop. A ping and its pong are a few bytes of WebSocket, but with TLS and TCP overhead
they come to roughly 20 MB a month at the default 20 s. On a metered link, raise
`heartbeatIntervalMs`.

**Sign-in.** Registration and login are HTTPS calls, each with its own TLS handshake: 1–3 s of CPU on
an ESP32, and about 40 KB of heap while it runs. With `backgroundSignIn` they run on a FreeRTOS task
with a 10 KB stack, which exists only during the call. Your `loop()` keeps running, and messages you
send meanwhile are queued. Handlers and NVS writes still happen inside `wbskt.loop()`.

## Memory and limits

- **RAM:** the offline buffer (`offlineBufferBytes`, 16 KB by default) is allocated once in the
  constructor, so queuing messages doesn't fragment the heap. On top of that, each message briefly
  takes its serialized size while it is being encoded or sent.
- **Messages** are at most 64 KB. In practice the WebSockets library receives frames of up to
  15 KB on ESP32 (`WEBSOCKETS_MAX_DATA_SIZE`), so keep commands from the server small.
- **Commands sent while the board is offline** are not queued by the server. The console tells the
  sender that the device is offline.
- **Timekeeping:** `millis()` wraps after 49 days. Every timer in the library handles the wrap.

## Self-hosting

Point the client at your own domain:

```cpp
WbsktConfig config;
config.apiHost = "api.example.com";    // api.<DOMAIN>
config.socketHost = "ws.example.com";  // ws.<DOMAIN>
```

A default install (Traefik with Let's Encrypt) needs nothing else. For a private CA, set
`config.rootCa` to its PEM root certificate.

## Troubleshooting

Turn on the ESP32's log output (Arduino IDE: Tools → Core Debug Level → Warn or Info). The library
logs lines starting with `wbskt:`.

| What you see | Why |
|---|---|
| `no stored credentials and no policy PIN` | First boot without a PIN. Pass the policy's PIN. |
| `registration answered` with a 4xx code | The server refused the PIN. Check it, and that the policy is enabled and not full. |
| `login answered 401 CLIENT_NOT_REGISTERED` | The policy needs approval. Approve the device in the console; the board connects on its next retry. |
| It connects, then drops every minute or so | Pongs aren't coming back in time. Raise `heartbeatTimeoutMs` on a slow link. |
| `droppedMessages()` keeps rising | The board makes messages faster than 8 a second, or was offline longer than the buffer covers. Send less often, or raise `offlineBufferBytes`. |
| The sketch doesn't fit | Pick the Minimal SPIFFS partition ([Install](#install)). |
| An update is `rejected` with `no OTA partition` | The board was flashed with a scheme without OTA. Flash it once over USB with Minimal SPIFFS. |
| An update `failed` with `download failed` | The board couldn't reach the URL over HTTPS or verify its certificate. Check the URL, and set `otaRootCa` for a host that doesn't use Let's Encrypt. |

## Protocol and tests

The library implements
[the device protocol](https://github.com/wbskt/Wbskt/blob/master/Docs/Device.Protocol.md): stored
credentials, token reuse, `capabilities` first on every connection, ping/pong, command acks and
expiry, the offline buffer with `sentAt`, and the jittered reconnect schedule. `sys.ota.update` and
`sys.ota.status` are this library's proposal for firmware updates; the server side is
[wbskt/Wbskt#510](https://github.com/wbskt/Wbskt/issues/510).

The parts with no hardware dependency, in `src/wbskt/`, have host tests. Run `make -C test/host`
after cloning ArduinoJson into `test/host/ArduinoJson`.

The whole client also runs against the
[conformance suite](https://github.com/wbskt/Wbskt/tree/master/Tests/Conformance).
`test/conformance` builds `WbsktClient` for Linux over shims of the Arduino core, HTTPClient,
Preferences, FreeRTOS and WebSockets, in `test/conformance/shim/`. `test/conformance/test_esp32.py`
adds this library's own scenarios (the heartbeat, a large backlog under the send rate, background
sign-in, OTA updates and rollback), using the suite's fixtures. The OTA shim
(`shim/OtaFlash.cpp`) keeps two fake partitions in the storage file and downloads nothing, so
restarting the harness boots whichever one the last run chose. CI runs both:

```sh
make -C test/conformance
cp test/conformance/test_esp32.py <wbskt/Wbskt>/Tests/Conformance/
cd <wbskt/Wbskt>/Tests/Conformance
WBSKT_HARNESS=<this repo>/test/conformance/wbskt-harness python -m pytest -v
```

The WebSockets shim copies the parts of the library's behaviour (2.7.3) that the client depends
on: when `loop()` connects, which failures raise an event, the reasons it gives, and the heartbeat.
If a newer library changes any of these, change the shim too.

## License

MIT
