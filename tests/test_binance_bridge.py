"""Offline process-level tests of framing, staleness and Python -> C++ delivery."""
import asyncio
import json
import os
import pathlib
import subprocess
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from connectors.binance.feed import Sink

ENGINE = sys.argv.pop(1)
META = "META BTCUSDT 0.01 0.00001\n"
FRAME = "SNAPSHOT 42 1 1 10000 10 10001 20\n"


class BridgeTests(unittest.TestCase):
    def run_engine(self, text):
        return subprocess.run([ENGINE], input=text, text=True, capture_output=True, timeout=5)

    def test_valid_and_eof_stale(self):
        result = self.run_engine(META + FRAME)
        self.assertEqual(result.returncode, 0, result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(events[1]["bid"], [10000, 10])
        self.assertEqual(events[-1]["type"], "stale")

    def test_truncated_negative_and_oversized(self):
        for frame in ("SNAPSHOT -1 0 0\n", "SNAPSHOT 1 21 0\n", FRAME.rstrip(),
                      "SNAPSHOT 1 1 0 18446744073709551616 1\n", "x" * 9000,
                      "SNAPSHOT 1 0 0 extra\n"):
            with self.subTest(frame=frame[:50]):
                self.assertNotEqual(self.run_engine(META + frame).returncode, 0)

    def test_sequence_reset_after_disconnect(self):
        result = self.run_engine(META + FRAME + "STALE\n" + FRAME.replace("42", "1"))
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_timeout_invalidates_without_input(self):
        process = subprocess.Popen([ENGINE], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        try:
            process.stdin.write(META + FRAME)
            process.stdin.flush()
            import selectors
            # Read raw fd, avoiding TextIO read-ahead interfering with select.
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                output = b""
                import time
                deadline = time.monotonic() + 5
                while b'"type":"stale"' not in output and time.monotonic() < deadline:
                    if selector.select(0.2):
                        output += os.read(process.stdout.fileno(), 4096)
                self.assertIn(b'"type":"stale"', output)
        finally:
            process.stdin.close()
            process.wait(timeout=3)
            process.stdout.close()
            process.stderr.close()

    def test_async_sink_to_engine(self):
        async def run():
            sink = Sink(ENGINE)
            await sink.start()
            try:
                await sink.emit({"type": "metadata", "symbol": "BTCUSDT", "tick": "0.01", "step": "0.00001"})
                await sink.emit({"type": "depth", "sequence": 42, "bids": [[10000, 10]], "asks": [[10001, 20]]})
                await sink.emit({"type": "stale"})
            finally:
                await sink.close()
        asyncio.run(run())


if __name__ == "__main__":
    unittest.main()
