"""Scenarios for what the ESP32 SDK does beyond the shared checklist. CI copies this file next to
Tests/Conformance in wbskt/Wbskt and runs it with that suite's fixtures and fake hosts."""

import asyncio
import time


RECONNECT = 25.0


async def test_reconnects_when_pongs_stop_coming_back(api, socket_server, device, monkeypatch):
    monkeypatch.setenv("WBSKT_HEARTBEAT", "1000,500,2")
    harness = await device()
    await harness.expect("connected")
    first = await socket_server.wait_connections(1)

    # The server stops reading, as when the access point vanishes: no close, no more pongs.
    first.transport.pause_reading()

    await harness.expect("disconnected", timeout=10.0)
    await socket_server.wait_connections(2, timeout=RECONNECT)
    assert len(api.logins) == 1


async def test_sends_a_large_backlog_in_order_under_the_rate_limit(api, socket_server, device):
    socket_server.down = True
    harness = await device()
    for n in range(150):
        await harness.command("send", type="reading", payload=n)
    for _ in range(150):
        await harness.expect("sent", type="reading")
    socket_server.down = False

    await harness.expect("connected", timeout=RECONNECT)
    readings = []
    while len(readings) < 150:
        _, frame, arrived = await socket_server.next_frame(RECONNECT)
        if frame["type"] == "reading":
            readings.append((frame["payload"], arrived))

    assert [payload for payload, _ in readings] == list(range(150))
    times = [arrived for _, arrived in readings]
    # The server allows bursts of 100 and 10 a second; no one-second window may go past that.
    assert max(sum(1 for t in times if start <= t < start + 1.0) for start in times) <= 100
    assert times[-1] - times[0] >= (150 - 50) / 8 - 1.5


async def test_keeps_looping_while_signing_in(api, socket_server, device):
    handle = api.handle

    def slow(path, body):
        if path.endswith("/login"):
            time.sleep(3)
        return handle(path, body)

    api.handle = slow
    harness = await device()
    await asyncio.sleep(0.5)

    started = asyncio.get_running_loop().time()
    await harness.command("send", type="reading", payload=1)
    await harness.expect("sent", type="reading", timeout=5.0)
    assert asyncio.get_running_loop().time() - started < 1.0

    await harness.expect("connected", timeout=RECONNECT)
    frame = await socket_server.next_of("reading", timeout=RECONNECT)
    assert frame["payload"] == 1


DIGEST = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"


def ota_update(command_id: str, url: str = "https://firmware.example/fw.bin", version: str = "1.1.0"):
    return {
        "type": "sys.ota.update",
        "payload": {"url": url, "sha256": DIGEST, "version": version, "size": 1200000},
        "commandId": command_id,
    }


async def next_ota_status(socket_server, timeout: float = RECONNECT):
    return (await socket_server.next_of("sys.ota.status", timeout=timeout))["payload"]


async def test_lists_ota_and_reports_the_firmware_version(api, socket_server, device, monkeypatch):
    monkeypatch.setenv("WBSKT_OTA", "1.0.0")
    harness = await device()
    await harness.expect("connected")

    capabilities = await socket_server.next_of("capabilities")
    state = await socket_server.next_of("state.report")
    assert capabilities["payload"]["system"] == ["ota"]
    assert state["payload"] == {"firmware": "1.0.0"}


async def test_refuses_an_update_when_ota_is_off(api, socket_server, device):
    harness = await device()
    await harness.expect("connected")
    capabilities = await socket_server.next_of("capabilities")
    assert "system" not in capabilities["payload"]

    await socket_server.send(0, ota_update("o1"))
    ack = await socket_server.next_of("sys.ack")
    status = await next_ota_status(socket_server)

    assert ack["payload"] == {"commandId": "o1"}
    assert status["commandId"] == "o1"
    assert status["state"] == "rejected"
    assert "does not take OTA" in status["error"]


async def test_refuses_an_update_over_plain_http(api, socket_server, device, monkeypatch):
    monkeypatch.setenv("WBSKT_OTA", "1.0.0")
    harness = await device()
    await harness.expect("connected")

    await socket_server.send(0, ota_update("o2", url="http://firmware.example/fw.bin"))
    status = await next_ota_status(socket_server)
    await harness.expect("ota", state="rejected")

    assert status == {"commandId": "o2", "state": "rejected", "error": "url must start with https://"}


async def test_reports_a_failed_download_after_reconnecting(api, socket_server, device, monkeypatch):
    monkeypatch.setenv("WBSKT_OTA", "1.0.0")
    monkeypatch.setenv("WBSKT_OTA_INSTALL", "download answered HTTP 404")
    harness = await device()
    await harness.expect("connected")

    await socket_server.send(0, ota_update("o3"))
    accepted = await next_ota_status(socket_server)
    await harness.expect("disconnected")
    await harness.expect("ota", state="failed")
    await harness.expect("connected", timeout=RECONNECT)
    statuses = [await next_ota_status(socket_server) for _ in range(2)]

    assert accepted["state"] == "accepted"
    assert [s["state"] for s in statuses] == ["downloading", "failed"]
    assert statuses[1] == {"commandId": "o3", "state": "failed", "version": "1.1.0", "error": "download answered HTTP 404"}
    assert len(api.logins) == 1


async def test_confirms_the_update_once_the_new_firmware_connects(api, socket_server, device, monkeypatch):
    monkeypatch.setenv("WBSKT_OTA", "1.0.0")
    monkeypatch.setenv("WBSKT_OTA_INSTALL", "ok")
    old = await device()
    await old.expect("connected")

    await socket_server.send(0, ota_update("o4"))
    await old.expect("ota", state="downloading", percent=100)
    await old.expect("restart")

    monkeypatch.setenv("WBSKT_OTA", "1.1.0")
    new = await device()
    await new.expect("ota", state="installed", timeout=RECONNECT)
    await new.expect("connected")
    installed = None
    while installed is None:
        status = await next_ota_status(socket_server)
        if status["state"] == "installed":
            installed = status
    state = await socket_server.next_of("state.report")

    assert installed == {"commandId": "o4", "state": "installed", "progress": 100, "version": "1.1.0"}
    assert state["payload"] == {"firmware": "1.1.0"}


async def test_goes_back_when_the_new_firmware_never_connects(api, socket_server, device, monkeypatch):
    monkeypatch.setenv("WBSKT_OTA", "1.0.0")
    monkeypatch.setenv("WBSKT_OTA_INSTALL", "ok")
    old = await device()
    await old.expect("connected")
    await socket_server.send(0, ota_update("o5"))
    await old.expect("restart")

    socket_server.down = True
    monkeypatch.setenv("WBSKT_OTA", "1.1.0")
    monkeypatch.setenv("WBSKT_OTA_CONFIRM_MS", "1500")
    broken = await device()
    await broken.expect("restart", timeout=10.0)

    socket_server.down = False
    monkeypatch.setenv("WBSKT_OTA", "1.0.0")
    monkeypatch.delenv("WBSKT_OTA_CONFIRM_MS")
    back = await device()
    await back.expect("ota", state="failed")
    await back.expect("connected", timeout=RECONNECT)
    failed = None
    while failed is None:
        status = await next_ota_status(socket_server)
        if status["state"] == "failed":
            failed = status

    assert failed["commandId"] == "o5"
    assert failed["version"] == "1.1.0"
    assert "went back" in failed["error"]
