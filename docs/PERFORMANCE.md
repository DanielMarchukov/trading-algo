# Performance

## Latency Measurement

The engine tracks five latency metrics via HdrHistogram, reporting p50, p90, p99, p99.9 on shutdown (SIGINT/SIGTERM).

### Hot path metrics

- **End-to-end:** WebSocket recv (`arrivedAt`) to order dequeued in OrderGateway
- **ZMQ transport:** `arrivedAt` (pre-publish) to post-ZMQ recv in consumer
- **Strategy + Risk:** post-ZMQ recv to pre-MPSC queue push
- **MPSC queue:** queue push (`queuedAt`) to queue pop in OrderGateway

### Cold path metrics

- **Fill round-trip:** REST `placeOrder` returns to fill received via WebSocket

### Implementation

- **Library:** HdrHistogram_c (vcpkg `hdr-histogram`)
- **Recording:** `hdr_record_value_atomic()` with CAS for thread-safe writes from multiple consumer threads
- **Timestamp source:** `clock_gettime(CLOCK_MONOTONIC)` via vDSO (~20ns overhead on Linux), `steady_clock` on other
  platforms
- **Output:** Console summary + per-metric files (`latency_<metric>.txt`) with full percentile distribution

### Overhead

Each measurement point calls `nowNanos()` which costs ~20ns via vDSO on Linux. Five measurement points add ~100ns total
to the hot path. This is acceptable given the REST submission at the end of the pipeline is ~20-50ms.

### Timestamps in Order struct

The `Order` struct carries two timestamps without growing beyond 64 bytes (one cache line):

- `arrivedAt` — copied from the triggering `MarketEvent`
- `queuedAt` — set at MPSC queue push time in the consumer

These use padding bytes that were previously unused.
