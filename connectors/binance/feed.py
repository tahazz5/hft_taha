"""Full top-20 snapshots. These are NOT diff-depth updates or individual orders."""
from __future__ import annotations

import asyncio
import json
import random
import sys
import time

from .client import ConnectorError, STREAM_URL, UINT64_MAX, units


def decode_snapshot(payload, rules):
    sequence = payload["lastUpdateId"]
    if type(sequence) is not int or not 0 <= sequence <= UINT64_MAX:
        raise ConnectorError("Invalid market update ID")
    result = {"type": "depth", "symbol": rules.symbol, "sequence": sequence,
              "received_ns": time.time_ns()}
    for key in ("bids", "asks"):
        rows = payload[key]
        if not isinstance(rows, list) or len(rows) > 20:
            raise ConnectorError("Expected at most 20 levels per side")
        converted = []
        for row in rows:
            if not isinstance(row, list) or len(row) != 2:
                raise ConnectorError("Invalid depth level")
            converted.append([units(row[0], rules.tick), units(row[1], rules.step)])
        prices = [row[0] for row in converted]
        if len(set(prices)) != len(prices) or prices != sorted(prices, reverse=(key == "bids")):
            raise ConnectorError("Depth levels are duplicated or unsorted")
        result[key] = converted
    if result["bids"] and result["asks"] and result["bids"][0][0] >= result["asks"][0][0]:
        raise ConnectorError("Crossed market snapshot")
    return result


def engine_line(event):
    if event["type"] == "metadata":
        return f'META {event["symbol"]} {event["tick"]} {event["step"]}\n'
    if event["type"] == "stale":
        return "STALE\n"
    values = ["SNAPSHOT", str(event["sequence"]), str(len(event["bids"])), str(len(event["asks"]))]
    for p, q in event["bids"] + event["asks"]:
        values.extend((str(p), str(q)))
    return " ".join(values) + "\n"


class Sink:
    def __init__(self, engine=None):
        self.engine = engine
        self.process = None

    async def start(self):
        if self.engine:
            # Credentials are never inherited by the market-data subprocess.
            import os
            env = {k: v for k, v in os.environ.items() if not k.startswith("BINANCE_")}
            self.process = await asyncio.create_subprocess_exec(
                self.engine, stdin=asyncio.subprocess.PIPE, env=env)

    async def emit(self, event):
        if self.process:
            if self.process.returncode is not None:
                raise ConnectorError("C++ market-data process exited")
            self.process.stdin.write(engine_line(event).encode("ascii"))
            try:
                await asyncio.wait_for(self.process.stdin.drain(), 2)
            except (TimeoutError, ConnectionError):
                raise ConnectorError("C++ market-data process is blocked or disconnected") from None
        else:
            print(json.dumps(event, separators=(",", ":")), flush=True)

    async def close(self):
        if self.process:
            self.process.stdin.close()
            try:
                code = await asyncio.wait_for(self.process.wait(), 3)
            except TimeoutError:
                self.process.kill()
                await self.process.wait()
                raise ConnectorError("C++ market-data process did not stop") from None
            if code:
                raise ConnectorError(f"C++ market-data process failed ({code})")


async def stream(rules, emit, *, count=0, retries=10, connect_factory=None, sleep=asyncio.sleep):
    # Lazy import keeps REST commands and unit tests dependency-free.
    from websockets.asyncio.client import connect
    from websockets.exceptions import ConnectionClosed, InvalidHandshake
    connect_factory = connect_factory or connect
    url = STREAM_URL + rules.symbol.lower() + "@depth20@100ms"
    delivered = 0
    failures = 0
    await emit({"type": "metadata", "symbol": rules.symbol, "tick": rules.tick, "step": rules.step,
                "environment": "spot-testnet", "levels": 20})
    try:
        while True:
            await emit({"type": "stale", "symbol": rules.symbol})
            try:
                async with connect_factory(url, open_timeout=10, close_timeout=2,
                                           ping_interval=None, max_size=65536,
                                           max_queue=1, compression=None) as websocket:
                    last = None
                    connected_at = time.monotonic()
                    while True:
                        raw = await asyncio.wait_for(websocket.recv(), 5)
                        payload = json.loads(raw)
                        if isinstance(payload, dict) and payload.get("e") == "serverShutdown":
                            raise ConnectionError("Server shutdown")
                        event = decode_snapshot(payload, rules)
                        if last is not None and event["sequence"] < last:
                            raise ValueError("Sequence moved backwards")
                        # Each message replaces the entire top-20. Sequence jumps are valid.
                        last = event["sequence"]
                        await emit(event)
                        delivered += 1
                        if count and delivered >= count:
                            return
                        if time.monotonic() - connected_at > 30:
                            failures = 0
            except (OSError, TimeoutError, ConnectionClosed, InvalidHandshake,
                    ValueError, KeyError, TypeError, ConnectorError) as error:
                await emit({"type": "stale", "symbol": rules.symbol})
                if failures >= retries:
                    raise ConnectorError("Market stream unavailable; reconnect limit reached") from None
                delay = min(30, 2 ** min(failures, 5)) + random.uniform(0, 0.25)
                failures += 1
                # Avoid echoing a URL, headers, payloads, or a library exception body.
                print(f"Market feed interrupted ({type(error).__name__}); retry in {delay:.2f}s",
                      file=sys.stderr)
                await sleep(delay)
    finally:
        await emit({"type": "stale", "symbol": rules.symbol})
