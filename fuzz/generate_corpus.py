#!/usr/bin/env python3
"""Generate seed corpus files for libFuzzer targets."""

import os
import struct
import json

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))


def write_file(path, data):
    full = os.path.join(SCRIPT_DIR, path)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    mode = "wb" if isinstance(data, bytes) else "w"
    with open(full, mode) as f:
        f.write(data)
    print(f"  {path} ({len(data)} bytes)")


def generate_msgpack_corpus():
    try:
        import msgpack
    except ImportError:
        print("SKIP msgpack corpus (pip install msgpack)")
        return

    print("msgpack_decoder corpus:")

    write_file(
        "corpus/msgpack_decoder/valid_quote.bin",
        msgpack.packb(
            [
                {
                    "T": "q",
                    "S": "AAPL",
                    "bp": 150.25,
                    "bs": 200,
                    "ap": 150.30,
                    "as": 100,
                    "t": 1234567890,
                }
            ]
        ),
    )

    write_file(
        "corpus/msgpack_decoder/valid_trade.bin",
        msgpack.packb(
            [{"T": "t", "S": "AAPL", "p": 150.25, "s": 100, "t": 1234567890}]
        ),
    )

    write_file(
        "corpus/msgpack_decoder/auth_success.bin",
        msgpack.packb([{"T": "success"}]),
    )

    write_file("corpus/msgpack_decoder/empty_array.bin", msgpack.packb([]))


def generate_trade_update_corpus():
    print("trade_update_parser corpus:")

    write_file(
        "corpus/trade_update_parser/valid_fill.json",
        json.dumps(
            {
                "stream": "trade_updates",
                "data": {
                    "event": "fill",
                    "qty": "100",
                    "price": "150.25",
                    "order": {
                        "symbol": "AAPL",
                        "side": "buy",
                        "id": "abc-123",
                    },
                },
            }
        ),
    )

    write_file(
        "corpus/trade_update_parser/valid_cancel.json",
        json.dumps(
            {
                "stream": "trade_updates",
                "data": {
                    "event": "canceled",
                    "order": {
                        "symbol": "AAPL",
                        "side": "sell",
                        "qty": "100",
                        "filled_qty": "0",
                        "id": "def-456",
                    },
                },
            }
        ),
    )

    write_file(
        "corpus/trade_update_parser/valid_partial_fill.json",
        json.dumps(
            {
                "stream": "trade_updates",
                "data": {
                    "event": "partial_fill",
                    "qty": "50",
                    "price": "150.00",
                    "order": {
                        "symbol": "GOOGL",
                        "side": "buy",
                        "id": "ghi-789",
                    },
                },
            }
        ),
    )

    write_file(
        "corpus/trade_update_parser/missing_fields.json",
        json.dumps({"stream": "trade_updates", "data": {}}),
    )


def generate_risk_manager_corpus():
    print("risk_manager corpus:")

    # Order struct layout (64 bytes, little-endian):
    #   char symbol[8]
    #   OrderSide side (uint8_t: 0=Buy, 1=Sell) + 7 bytes padding
    #   int64_t quantity
    #   uint64_t price
    #   ... remaining fields to fill 64 bytes

    def make_order(symbol, side, qty, price):
        buf = bytearray(64)
        sym = symbol.encode("ascii")[:8].ljust(8, b"\x00")
        buf[0:8] = sym
        buf[8] = side  # OrderSide enum
        struct.pack_into("<q", buf, 16, qty)
        struct.pack_into("<Q", buf, 24, price)
        return bytes(buf)

    write_file(
        "corpus/risk_manager/valid_buy.bin",
        make_order("AAPL", 0, 10, 150_0000),
    )

    write_file(
        "corpus/risk_manager/valid_sell.bin",
        make_order("AAPL", 1, 5, 175_0000),
    )


if __name__ == "__main__":
    generate_msgpack_corpus()
    generate_trade_update_corpus()
    generate_risk_manager_corpus()
    print("Done.")
