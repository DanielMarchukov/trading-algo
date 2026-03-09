# Architecture Rules

Sources: [Alpaca WebSocket streaming docs][1], [Alpaca trading API docs][2], [ixwebsocket GitHub][3]

## Process Boundaries

- Single process: `paper_money` binary contains all components
- Internal communication is via ZeroMQ (IPC on Unix, TCP on Windows)
- Wire format is the 64-byte `MarketEvent` struct

## Thread Model

- Core 0 (housekeeping): OrderGateway, FillListener, OS interrupts, main thread — all cold-path I/O-bound work
- Core 1 (data path): MarketDataPipeline thread (AlpacaWebSocketSource → AlpacaMsgpackDecoder → ZmqMarketEventSink) —
  receives market data from Alpaca WebSocket, decodes msgpack via SAX-style visitor, publishes MarketEvent structs via
  ZMQ
- Cores 2+ (hot path): per-symbol MarketEventConsumer threads, one per core, pinned at startup
- No thread may block another thread on the hot path

## Alpaca Integration

### REST API (v2)

- Base URL: `https://paper-api.alpaca.markets` (paper)
- Auth headers: `APCA-API-KEY-ID`, `APCA-API-SECRET-KEY`
- Key endpoints: `POST /v2/orders` (place), `DELETE /v2/orders/{id}` (cancel), `GET /v2/positions` (reconcile)
- Rate limit: 200 requests/minute. Track `X-Ratelimit-Remaining` header. Never exceed — queue orders if approaching
  limit.
- **Connection reuse is critical**: cpr::Post() creates a new TCP+TLS connection per call (~20-50ms overhead). Use
  `cpr::Session` for persistent connections, or migrate to Boost.Beast.
- Always set `TCP_NODELAY` — Nagle's algorithm adds up to 200ms delay
- Set `TCP_QUICKACK` on Linux to disable delayed ACKs (not sticky, must re-set after each recv)

### WebSocket — Trade Updates (fill listener)

- Endpoint: `wss://paper-api.alpaca.markets/stream`
- Auth: `{"action":"auth","key":"...","secret":"..."}`
- Subscribe: `{"action":"listen","data":{"streams":["trade_updates"]}}`
- Fill event fields: `event` ("fill"/"partial_fill"), `timestamp`, `price` (per-share), `qty` (this fill),
  `position_qty` (total after), plus full `order` object
- Other events: `new`, `canceled`, `expired`, `replaced`, `done_for_day`, `rejected`
- **No message replay on reconnect** — must reconcile via REST `GET /v2/orders?status=all&after=<last_timestamp>` after
  reconnect
- Use exponential backoff with jitter: 100ms * 2^n + random(0,100ms), capped at 30s

### WebSocket — Market Data

- Endpoint: `wss://stream.data.alpaca.markets/v2/iex`
- msgpack mode: append `?encoding=msgpack` to URL
- Trades: `{"T":"t","S":"AAPL","p":150.25,"s":100,"t":"..."}`
- Quotes: `{"T":"q","S":"AAPL","bp":150.20,"bs":200,"ap":150.30,...}`
- Handled by `MarketDataPipeline` (AlpacaWebSocketSource + AlpacaMsgpackDecoder + ZmqMarketEventSink)

### Library Choices

- **ixwebsocket** (vcpkg): Used by AlpacaFillListener and AlpacaWebSocketSource for WebSocket connections.
- **msgpack-cxx** (vcpkg, header-only): Decodes Alpaca market data stream. Uses SAX-style `msgpack::parse()` visitor —
  exception-free, zero allocation on the hot path.
- **simdjson**: 4x faster than RapidJSON for parsing REST responses. Use for any JSON parsing that becomes
  latency-sensitive.

## Component Ownership

- `TradingEngine` owns everything via `std::unique_ptr` / `std::shared_ptr`
- `PositionManager` is shared (read by hot path, written by fill listener) — use TBB `concurrent_hash_map` (current
  approach is correct)
- `RiskManager` holds `shared_ptr<PositionManager>` for read access
- Order queue is shared between consumer threads (producers) and OrderGateway (consumer)

## Dependency Direction

- `MarketEventConsumer` depends on Strategy (template), RiskManager, order callback
- `RiskManager` depends on PositionManager
- `OrderGateway` depends on IRestClient, order queue
- Fill listener depends on PositionManager (write) and order tracker
- No circular dependencies
- Strategy knows nothing about execution — it only produces orders

## Configuration (planned)

- Risk limits, strategy params, symbols, addresses must be configurable without recompilation
- Environment variables for secrets (API keys)
- Config file (YAML/JSON) for everything else
- Defaults must be safe (conservative risk limits)

## Order Lifecycle

Current (broken — open loop):

1. Strategy generates Order
1. RiskManager approves, PositionManager tracks as pending
1. OrderGateway sends to Alpaca
1. **(gap)** — no fill tracking, pending accumulates forever

Target (closed loop):

1. Strategy generates Order
1. RiskManager approves, PositionManager tracks as pending
1. OrderGateway sends to Alpaca, records `client_order_id`
1. Fill listener receives `fill`/`partial_fill` via WebSocket
1. PositionManager::onFill() — decrements pending, increments filled
1. On `canceled`/`expired` — PositionManager::onOrderCancelled()
1. Strategy manages order lifecycle (cancel stale, replace prices)

[1]: https://docs.alpaca.markets/docs/websocket-streaming
[2]: https://docs.alpaca.markets/docs/trading-api
[3]: https://github.com/machinezone/IXWebSocket
