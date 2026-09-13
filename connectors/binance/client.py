"""Synchronous, single-owner HTTPS client. Private requests are Testnet only."""
from __future__ import annotations

import hashlib
import hmac
import http.client
import json
import os
import re
import ssl
import time
from dataclasses import dataclass
from decimal import Decimal, localcontext
from urllib.parse import urlencode

REST_HOST = "testnet.binance.vision"
STREAM_URL = "wss://stream.testnet.binance.vision/ws/"
UINT64_MAX = (1 << 64) - 1


class ConnectorError(Exception):
    pass


class APIError(ConnectorError):
    def __init__(self, status, code=None, retry_after=0, unknown=False):
        self.status, self.code = status, code
        self.retry_after, self.unknown = retry_after, unknown
        # Do not echo server messages: they can contain submitted parameters.
        super().__init__(f"Binance HTTP {status}, code={code}, "
                         f"execution_unknown={unknown}, retry_after={retry_after}s")


class UnknownExecution(ConnectorError):
    """A mutation may have reached Binance. Query its client ID; never resend blindly."""


def symbol_name(value):
    value = value.upper()
    if not re.fullmatch(r"[A-Z0-9]{2,30}", value):
        raise ConnectorError("Symbol must contain 2–30 ASCII letters/digits")
    return value


def client_id(value):
    if not re.fullmatch(r"[.A-Za-z0-9_:/-]{1,36}", value):
        raise ConnectorError("Invalid client order ID (1–36 supported ASCII characters)")
    return value


def decimal_value(value, *, zero=False):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9]{1,24}(?:\.[0-9]{1,24})?", value):
        raise ConnectorError("Expected a plain unsigned decimal string")
    result = Decimal(value)
    if not zero and result == 0:
        raise ConnectorError("Value must be positive")
    return result


def units(value, increment, *, zero=False):
    with localcontext() as ctx:
        ctx.prec = 100
        count = decimal_value(value, zero=zero) / decimal_value(increment)
        if count != count.to_integral_value() or count > UINT64_MAX:
            raise ConnectorError("Value is off-grid or exceeds uint64")
        return int(count)


def signature(secret, query):
    return hmac.new(secret.encode(), query.encode(), hashlib.sha256).hexdigest()


class HTTPTransport:
    """TLS verification, keep-alive, bounded response; redirects are never followed."""
    def __init__(self):
        self.connection = None

    def close(self):
        if self.connection is not None:
            self.connection.close()
            self.connection = None

    def __call__(self, method, path, headers, body):
        if self.connection is None:
            self.connection = http.client.HTTPSConnection(
                REST_HOST, timeout=15, context=ssl.create_default_context())
        try:
            self.connection.request(method, path, body=body, headers=headers)
            response = self.connection.getresponse()
            payload = response.read(4 * 1024 * 1024 + 1)
            if len(payload) > 4 * 1024 * 1024:
                raise OSError("Response too large")
            return response.status, dict(response.getheaders()), payload
        except (OSError, http.client.HTTPException):
            self.close()
            raise


@dataclass(frozen=True)
class SymbolRules:
    symbol: str
    tick: str
    step: str
    filters: dict
    order_types: tuple

    @classmethod
    def from_exchange_info(cls, payload, symbol):
        for item in payload["symbols"]:
            if item["symbol"] == symbol:
                if item["status"] != "TRADING" or not item.get("isSpotTradingAllowed", False):
                    raise ConnectorError("Symbol is not available for Spot trading")
                filters = {f["filterType"]: f for f in item["filters"]}
                return cls(symbol, filters["PRICE_FILTER"]["tickSize"],
                           filters["LOT_SIZE"]["stepSize"], filters, tuple(item["orderTypes"]))
        raise ConnectorError("Symbol not found")

    @staticmethod
    def check_range(value, filt, low, high, step):
        val = decimal_value(value)
        minimum = decimal_value(filt[low], zero=True)
        maximum = decimal_value(filt[high], zero=True)
        if (minimum and val < minimum) or (maximum and val > maximum):
            raise ConnectorError(f"Value outside {low}/{high}")
        grid = filt[step]
        if decimal_value(grid, zero=True):
            units(value, grid)

    def validate(self, kind, qty, price=None):
        if kind not in self.order_types:
            raise ConnectorError("Unsupported order type for this symbol")
        self.check_range(qty, self.filters["LOT_SIZE"], "minQty", "maxQty", "stepSize")
        if kind == "MARKET" and "MARKET_LOT_SIZE" in self.filters:
            self.check_range(qty, self.filters["MARKET_LOT_SIZE"], "minQty", "maxQty", "stepSize")
        if kind == "LIMIT":
            self.check_range(price, self.filters["PRICE_FILTER"], "minPrice", "maxPrice", "tickSize")
            with localcontext() as ctx:
                ctx.prec = 100
                notional = decimal_value(price) * decimal_value(qty)
            for name in ("MIN_NOTIONAL", "NOTIONAL"):
                f = self.filters.get(name, {})
                if "minNotional" in f and notional < decimal_value(f["minNotional"], zero=True):
                    raise ConnectorError("Order below minimum notional")
                if "maxNotional" in f and notional > decimal_value(f["maxNotional"], zero=True):
                    raise ConnectorError("Order exceeds maximum notional")
        # Binance remains authoritative for dynamic, account and market-notional filters.


class Client:
    def __init__(self, key="", secret="", *, transport=None, clock=time.time, monotonic=time.monotonic):
        self.key, self.secret = key, secret
        self.transport = transport if transport is not None else HTTPTransport()
        self.clock, self.monotonic = clock, monotonic
        self.offset_ms = None
        self.blocked_until = 0

    @classmethod
    def from_environment(cls):
        return cls(os.environ.get("BINANCE_TESTNET_API_KEY", ""),
                   os.environ.get("BINANCE_TESTNET_API_SECRET", ""))

    def close(self):
        close = getattr(self.transport, "close", None)
        if close:
            close()

    def sync_time(self):
        start = self.clock() * 1000
        result = self.request("GET", "/api/v3/time")
        end = self.clock() * 1000
        self.offset_ms = int(result["serverTime"] - (start + end) / 2)

    def request(self, method, path, params=None, *, signed=False):
        if self.monotonic() < self.blocked_until:
            raise ConnectorError("Rate limited: wait until Retry-After expires")
        mutation = method != "GET"
        data = dict(params or {})
        headers = {"Accept": "application/json", "User-Agent": "taha-orderbook-testnet/1"}
        if signed:
            if not self.key or not self.secret:
                raise ConnectorError("Set BINANCE_TESTNET_API_KEY and BINANCE_TESTNET_API_SECRET locally")
            if self.offset_ms is None:
                self.sync_time()
            data.update(recvWindow="5000", timestamp=str(int(self.clock() * 1000) + self.offset_ms))
            headers["X-MBX-APIKEY"] = self.key
        query = urlencode(data)
        if signed:
            query += "&signature=" + signature(self.secret, query)
        body = None
        if method == "GET":
            if query:
                path += "?" + query
        else:
            body = query.encode("ascii")
            headers["Content-Type"] = "application/x-www-form-urlencoded"
        try:
            status, response_headers, raw = self.transport(method, path, headers, body)
        except (OSError, http.client.HTTPException):
            if mutation:
                raise UnknownExecution("Connection lost: execution unknown; query the client order ID before any new submission") from None
            raise ConnectorError("HTTPS connection failed") from None
        normalized_headers = {k.lower(): v for k, v in response_headers.items()}
        retry_after = 0
        if status in (418, 429):
            try:
                retry_after = max(1, int(normalized_headers.get("retry-after", "60")))
            except ValueError:
                retry_after = 60
            self.blocked_until = self.monotonic() + retry_after
        try:
            result = json.loads(raw)
        except (ValueError, UnicodeError):
            if mutation:
                raise UnknownExecution("Unparseable response: execution unknown; query the client order ID") from None
            raise APIError(status) from None
        code = result.get("code") if isinstance(result, dict) else None
        if not 200 <= status < 300 or (isinstance(code, int) and code < 0):
            unknown = mutation and (status >= 500 or code in (-1006, -1007))
            raise APIError(status, code, retry_after, unknown)
        return result

    def rules(self, symbol):
        symbol = symbol_name(symbol)
        return SymbolRules.from_exchange_info(
            self.request("GET", "/api/v3/exchangeInfo", {"symbol": symbol}), symbol)

    def place(self, symbol, direction, kind, qty, *, price=None, tif="GTC", order_id, validate_only=False):
        symbol = symbol_name(symbol)
        client_id(order_id)
        if direction not in ("BUY", "SELL") or kind not in ("LIMIT", "MARKET"):
            raise ConnectorError("Expected BUY/SELL and LIMIT/MARKET")
        if tif not in ("GTC", "IOC", "FOK"):
            raise ConnectorError("Invalid time in force")
        if kind == "MARKET" and (price is not None or tif != "GTC"):
            raise ConnectorError("MARKET takes neither price nor time in force")
        self.rules(symbol).validate(kind, qty, price)
        params = dict(symbol=symbol, side=direction, type=kind, quantity=qty,
                      newClientOrderId=order_id, newOrderRespType="FULL")
        if kind == "LIMIT":
            params.update(price=price, timeInForce=tif)
        return self.request("POST", "/api/v3/order/test" if validate_only else "/api/v3/order", params, signed=True)

    def query(self, symbol, order_id):
        return self.request("GET", "/api/v3/order", {
            "symbol": symbol_name(symbol), "origClientOrderId": client_id(order_id)}, signed=True)

    def cancel(self, symbol, order_id):
        return self.request("DELETE", "/api/v3/order", {
            "symbol": symbol_name(symbol), "origClientOrderId": client_id(order_id)}, signed=True)
