import asyncio
import hashlib
import hmac
import importlib.util
import json
import unittest
from urllib.parse import parse_qs

from connectors.binance.client import (
    APIError, Client, ConnectorError, SymbolRules, UnknownExecution,
    REST_HOST, STREAM_URL, signature, units,
)
from connectors.binance.feed import decode_snapshot, engine_line

INFO = {"symbols": [{"symbol": "BTCUSDT", "status": "TRADING", "isSpotTradingAllowed": True,
    "orderTypes": ["LIMIT", "MARKET"], "filters": [
        {"filterType": "PRICE_FILTER", "minPrice": "0.01", "maxPrice": "1000000", "tickSize": "0.01"},
        {"filterType": "LOT_SIZE", "minQty": "0.00001", "maxQty": "100", "stepSize": "0.00001"},
        {"filterType": "MARKET_LOT_SIZE", "minQty": "0", "maxQty": "10", "stepSize": "0"},
        {"filterType": "NOTIONAL", "minNotional": "5", "maxNotional": "1000000"},
    ]}]}
RULES = SymbolRules.from_exchange_info(INFO, "BTCUSDT")
SNAPSHOT = {"lastUpdateId": 42, "bids": [["100.00", "0.50000"], ["99.99", "0.20000"]],
            "asks": [["100.01", "0.30000"]]}


class FakeHTTP:
    def __init__(self, results):
        self.results = list(results)
        self.calls = []

    def __call__(self, method, path, headers, body):
        self.calls.append((method, path, headers, body))
        result = self.results.pop(0)
        if isinstance(result, Exception):
            raise result
        if isinstance(result, tuple):
            return result
        return 200, {}, json.dumps(result).encode()


class RestTests(unittest.TestCase):
    def make_client(self, responses):
        transport = FakeHTTP(responses)
        client = Client("test-key", "test-secret", transport=transport, clock=lambda: 1000, monotonic=lambda: 50)
        return client, transport

    def test_hmac_rfc4231(self):
        self.assertEqual(signature("Jefe", "what do ya want for nothing?"),
                         "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")

    def test_testnet_only_constants(self):
        self.assertEqual(REST_HOST, "testnet.binance.vision")
        self.assertEqual(STREAM_URL, "wss://stream.testnet.binance.vision/ws/")

    def test_exact_decimals(self):
        self.assertEqual(units("123.45000000", "0.01000000"), 12345)
        self.assertEqual(units("0.00000001", "0.00000001"), 1)
        self.assertEqual(units("18446744073709551615", "1"), (1 << 64) - 1)
        for value in ("1e-8", "NaN", "-1", "0", "0.011", "18446744073709551616"):
            with self.subTest(value=value), self.assertRaises(ConnectorError):
                units(value, "0.01")

    def test_rules(self):
        RULES.validate("LIMIT", "0.1", "100")
        for qty, price in (("0.1", "100.001"), ("0.000011", "100"), ("0.01", "100"), ("101", "100")):
            with self.subTest(qty=qty, price=price), self.assertRaises(ConnectorError):
                RULES.validate("LIMIT", qty, price)
        with self.assertRaises(ConnectorError):
            RULES.validate("MARKET", "11")

    def test_signed_order_encoding_and_time(self):
        client, transport = self.make_client([INFO, {"serverTime": 1000100}, {"status": "NEW"}])
        result = client.place("btcusdt", "BUY", "LIMIT", "0.1", price="100.00", order_id="order/a:1")
        self.assertEqual(result["status"], "NEW")
        method, path, headers, body = transport.calls[-1]
        self.assertEqual((method, path), ("POST", "/api/v3/order"))
        self.assertEqual(headers["X-MBX-APIKEY"], "test-key")
        query, actual = body.decode().rsplit("&signature=", 1)
        self.assertEqual(actual, hmac.new(b"test-secret", query.encode(), hashlib.sha256).hexdigest())
        data = parse_qs(query)
        self.assertEqual(data["timestamp"], ["1000100"])
        self.assertEqual(data["newClientOrderId"], ["order/a:1"])
        self.assertEqual(data["quantity"], ["0.1"])
        self.assertNotIn("test-secret", body.decode())
        self.assertNotIn("X-MBX-APIKEY", transport.calls[0][2])

    def test_validation_endpoint_and_market_fields(self):
        client, transport = self.make_client([INFO, {"serverTime": 1000000}, {}])
        client.place("BTCUSDT", "SELL", "MARKET", "0.1", order_id="one", validate_only=True)
        self.assertEqual(transport.calls[-1][1], "/api/v3/order/test")
        body = parse_qs(transport.calls[-1][3].decode())
        self.assertNotIn("price", body)
        self.assertNotIn("timeInForce", body)

    def test_query_and_cancel(self):
        client, transport = self.make_client([{"serverTime": 1000000}, {"status": "NEW"}, {"status": "CANCELED"}])
        client.query("BTCUSDT", "my-order")
        client.cancel("BTCUSDT", "my-order")
        self.assertTrue(transport.calls[1][1].startswith("/api/v3/order?symbol=BTCUSDT&origClientOrderId=my-order&"))
        self.assertEqual(transport.calls[2][0], "DELETE")

    def test_timeout_not_retried(self):
        client, transport = self.make_client([INFO, {"serverTime": 1000000}, TimeoutError()])
        with self.assertRaises(UnknownExecution):
            client.place("BTCUSDT", "BUY", "LIMIT", "0.1", price="100", order_id="one")
        self.assertEqual(len(transport.calls), 3)

    def test_server_error_is_unknown(self):
        client, transport = self.make_client([(504, {}, b'{"code":-1007,"msg":"test-secret"}')])
        client.offset_ms = 0
        with self.assertRaises(APIError) as error:
            client.cancel("BTCUSDT", "one")
        self.assertTrue(error.exception.unknown)
        self.assertNotIn("test-secret", str(error.exception))
        self.assertEqual(len(transport.calls), 1)

    def test_malformed_mutation_response_is_unknown(self):
        client, _ = self.make_client([(200, {}, b"not-json")])
        client.offset_ms = 0
        with self.assertRaises(UnknownExecution):
            client.cancel("BTCUSDT", "one")

    def test_rate_limit_prevents_followup(self):
        client, transport = self.make_client([(429, {"Retry-After": "12"}, b'{"code":-1003}')])
        with self.assertRaises(APIError) as error:
            client.request("GET", "/api/v3/ping")
        self.assertEqual(error.exception.retry_after, 12)
        with self.assertRaises(ConnectorError):
            client.request("GET", "/api/v3/ping")
        self.assertEqual(len(transport.calls), 1)

    def test_missing_credentials(self):
        transport = FakeHTTP([])
        client = Client(transport=transport)
        with self.assertRaises(ConnectorError):
            client.query("BTCUSDT", "one")
        self.assertFalse(transport.calls)

    def test_invalid_order_rejected_before_submission(self):
        client, transport = self.make_client([INFO])
        with self.assertRaises(ConnectorError):
            client.place("BTCUSDT", "BUY", "LIMIT", "0.1", price="100.001", order_id="one")
        self.assertEqual(len(transport.calls), 1)


class DepthTests(unittest.TestCase):
    def test_snapshot_and_cpp_protocol(self):
        event = decode_snapshot(SNAPSHOT, RULES)
        self.assertEqual(event["bids"], [[10000, 50000], [9999, 20000]])
        self.assertEqual(engine_line(event), "SNAPSHOT 42 2 1 10000 50000 9999 20000 10001 30000\n")
        self.assertEqual(engine_line({"type": "stale"}), "STALE\n")

    def test_bad_snapshot(self):
        for patch in ({"lastUpdateId": -1}, {"lastUpdateId": True},
                      {"bids": [["100", "0"]]}, {"bids": [["101", "1"]]},
                      {"bids": [["100", "1"], ["100", "2"]]},
                      {"bids": [["99", "1"], ["100", "2"]]},
                      {"bids": [["100.001", "1"]]}, {"asks": [["101", "1"]] * 21}):
            with self.subTest(patch=patch), self.assertRaises(ConnectorError):
                decode_snapshot(dict(SNAPSHOT, **patch), RULES)


@unittest.skipUnless(importlib.util.find_spec("websockets"), "optional WebSocket dependency not installed")
class StreamTests(unittest.IsolatedAsyncioTestCase):
    async def test_reconnect_marks_stale_and_accepts_reset_sequence(self):
        from connectors.binance.feed import stream
        messages = [[SNAPSHOT], [dict(SNAPSHOT, lastUpdateId=1)]]
        events, delays = [], []
        class Socket:
            async def __aenter__(self):
                self.messages = messages.pop(0)
                return self
            async def __aexit__(self, *args):
                pass
            async def recv(self):
                if not self.messages:
                    raise OSError("disconnected")
                return json.dumps(self.messages.pop(0))
        async def emit(event):
            events.append(event)
        async def sleep(delay):
            delays.append(delay)
        await stream(RULES, emit, count=2, connect_factory=lambda *a, **kw: Socket(), sleep=sleep)
        self.assertEqual([e["sequence"] for e in events if e["type"] == "depth"], [42, 1])
        self.assertEqual(events[-1]["type"], "stale")
        self.assertEqual(len(delays), 1)
        self.assertGreaterEqual(sum(e["type"] == "stale" for e in events), 3)

    async def test_real_local_websocket_ping_and_fragmentation(self):
        from websockets.asyncio.server import serve
        from websockets.asyncio.client import connect
        from connectors.binance.feed import stream
        events = []
        async def handler(socket):
            pong = await socket.ping(b"binance-heartbeat")
            await asyncio.wait_for(pong, 2)
            data = json.dumps(SNAPSHOT)
            await socket.send([data[:20], data[20:]])
            await socket.wait_closed()
        async def emit(event):
            events.append(event)
        async with serve(handler, "127.0.0.1", 0) as server:
            port = server.sockets[0].getsockname()[1]
            def local_connect(url, **kwargs):
                self.assertEqual(url, STREAM_URL + "btcusdt@depth20@100ms")
                return connect(f"ws://127.0.0.1:{port}", proxy=None, **kwargs)
            await stream(RULES, emit, count=1, connect_factory=local_connect)
        self.assertEqual(sum(e["type"] == "depth" for e in events), 1)


if __name__ == "__main__":
    unittest.main()
