import argparse
import asyncio
import json
import sys

from .client import Client, ConnectorError, symbol_name


def nonnegative(value):
    result = int(value)
    if result < 0:
        raise argparse.ArgumentTypeError("Must be nonnegative")
    return result


def parser():
    root = argparse.ArgumentParser(description="Binance Spot TESTNET: market data and paper orders only")
    commands = root.add_subparsers(dest="command", required=True)
    commands.add_parser("ping", help="Check public REST connection and server time")
    for command in ("info", "depth", "open-orders"):
        sub = commands.add_parser(command)
        sub.add_argument("symbol", type=symbol_name)
    feed = commands.add_parser("feed", help="Stream complete top-20 snapshots every 100 ms")
    feed.add_argument("symbol", type=symbol_name)
    feed.add_argument("--engine", help="Path to the binance_marketdata C++ executable")
    feed.add_argument("--count", type=nonnegative, default=0, help="Snapshot count; 0 means continuous")
    feed.add_argument("--retries", type=nonnegative, default=10)
    for command in ("place", "validate"):
        sub = commands.add_parser(command, help="Submit a testnet order" if command == "place" else "Validate without placing")
        sub.add_argument("symbol", type=symbol_name)
        sub.add_argument("side", choices=("BUY", "SELL"))
        sub.add_argument("type", choices=("LIMIT", "MARKET"))
        sub.add_argument("quantity", help="Decimal base-asset quantity, never a float")
        sub.add_argument("--price")
        sub.add_argument("--tif", choices=("GTC", "IOC", "FOK"), default="GTC")
        sub.add_argument("--client-id", required=True, help="Unique ID retained for query/cancel; never blindly resend")
    for command in ("query", "cancel"):
        sub = commands.add_parser(command)
        sub.add_argument("symbol", type=symbol_name)
        sub.add_argument("client_id")
    return root


async def feed_command(client, args):
    from .feed import Sink, stream
    rules = client.rules(args.symbol)
    sink = Sink(args.engine)
    await sink.start()
    try:
        await stream(rules, sink.emit, count=args.count, retries=args.retries)
    finally:
        await sink.close()


def main():
    client = None
    try:
        args = parser().parse_args()
        client = Client.from_environment()
        if args.command == "feed":
            asyncio.run(feed_command(client, args))
            return 0
        if args.command == "ping":
            client.request("GET", "/api/v3/ping")
            result = client.request("GET", "/api/v3/time")
        elif args.command == "info":
            result = client.request("GET", "/api/v3/exchangeInfo", {"symbol": args.symbol})
        elif args.command == "depth":
            result = client.request("GET", "/api/v3/depth", {"symbol": args.symbol, "limit": 20})
        elif args.command == "open-orders":
            result = client.request("GET", "/api/v3/openOrders", {"symbol": args.symbol}, signed=True)
        elif args.command in ("place", "validate"):
            # ID is intentionally public and available even if the subsequent request times out.
            print(json.dumps({"environment": "spot-testnet", "client_id": args.client_id}), file=sys.stderr)
            result = client.place(args.symbol, args.side, args.type, args.quantity,
                                  price=args.price, tif=args.tif, order_id=args.client_id,
                                  validate_only=args.command == "validate")
        elif args.command == "query":
            result = client.query(args.symbol, args.client_id)
        else:
            result = client.cancel(args.symbol, args.client_id)
        print(json.dumps(result, separators=(",", ":")))
        return 0
    except KeyboardInterrupt:
        return 130
    except ModuleNotFoundError:
        print("Install connectors/binance/requirements.txt to use WebSocket streaming", file=sys.stderr)
        return 2
    except ConnectorError as error:
        print(str(error), file=sys.stderr)
        return 1
    except (OSError, ValueError, KeyError, TypeError):
        print("Invalid response, configuration, or unavailable local process", file=sys.stderr)
        return 1
    finally:
        if client:
            client.close()


if __name__ == "__main__":
    sys.exit(main())
