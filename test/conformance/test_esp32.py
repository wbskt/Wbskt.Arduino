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
