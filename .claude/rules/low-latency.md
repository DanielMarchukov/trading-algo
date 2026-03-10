# Low-Latency Engineering Rules

Sources: [CppCon 2024 David Gross keynote][1], [C++ Design Patterns for Low-Latency (arxiv 2309.04259)][2],
[Electronic Trading Hub HFT best practices][3], [Semi-static conditions paper][4],
[rigtorp ring buffer optimization][5], [rigtorp huge pages][6]

## Hot Path vs Cold Path

The hot path is **network-to-network**: from WebSocket market data ingestion through to REST order submission. This
spans the full latency-critical pipeline:

1. **Ingestion**: AlpacaWebSocketSource receives market data
1. **Decode**: AlpacaMsgpackDecoder parses msgpack to MarketEvent
1. **Transport**: ZmqMarketEventSink publishes via ZMQ
1. **Consume**: MarketEventConsumer receives from ZMQ
1. **Strategy**: generates order signal
1. **Risk**: RiskManager validates
1. **Queue**: order pushed to MPSC queue
1. **Execute**: OrderGateway dequeues and sends REST order

Steps 1-7 are the **inner hot path** — zero-allocation, zero-syscall, exception-free. Step 8 (OrderGateway REST call) is
the **outer hot path** — network I/O is unavoidable but everything up to and after the syscall must be optimized
(pre-built payloads, connection reuse, TCP_NODELAY).

**Inner hot path rules** (steps 1-7):

- No heap allocation (no `new`, no `std::vector::push_back` that may reallocate, no `std::string` construction).
  Pre-allocate all containers at startup.
- No virtual function dispatch — use templates/CRTP/concepts for compile-time polymorphism (see Static Polymorphism
  section)
- No `std::shared_ptr` — atomic refcount is 10-20ns per access. Use raw non-owning pointers with documented lifetime.
- No mutex locks — use atomics and lock-free structures. Note: oneTBB `concurrent_hash_map::const_accessor` holds a
  shared reader lock (blocks writers until released) — not truly lock-free, do not use on the hot path
- No syscalls (no logging, no I/O, no `std::cout`)
- No exceptions on the normal path — exceptions add 10-20% overhead. Use error codes or `std::expected`
- All functions should be `noexcept` where they do not use exceptions internally
- No `std::string`, `std::map`, `std::unordered_map` — use fixed-width buffers and flat containers
- Accept code duplication over abstraction when latency is critical

**Outer hot path rules** (step 8 — OrderGateway):

- Connection reuse: `cpr::Session` or persistent connections, never per-request TCP+TLS setup
- `TCP_NODELAY` always, `TCP_QUICKACK` on Linux
- Pre-built JSON payloads where possible (snprintf templates)
- Rate limit tracking (`X-Ratelimit-Remaining` header)
- Minimize allocations in order serialization

**Cold path** (fill processing, logging, startup/shutdown, reconciliation): normal C++ OK.

## Static Polymorphism — CRTP vs Concepts vs Deducing This

C++23 introduced "deducing this" (explicit object parameters) which modernizes the CRTP pattern. Current best practice
(2025):

- **Concepts** are preferred over CRTP for constraining template parameters — cleaner syntax, identical runtime
  performance, better error messages
- **Deducing this** eliminates the need for `static_cast` in CRTP and removes the templated base class requirement
- **CRTP** remains valid but is considered legacy when full C++23 support is available. Minimum compiler versions for
  deducing this: GCC 14+, Clang 19+, MSVC 17.2+ (partial).

For this project, use **concepts to constrain** + **templates for injection**:

```cpp
template <typename T>
concept StrategyLike = requires(T t, const MarketEvent& e) {
    { t.onMarketEvent(e) } -> std::same_as<std::vector<Order>>;
};

template <StrategyLike StrategyType>
class MarketEventConsumer { ... };
```

Never add `virtual` to any class used on the hot path.

Sources: [Deducing this for static polymorphism][7], [Concepts vs CRTP][8]

## Lock-Free Data Structures

**SPSC Queue** (single-producer single-consumer):

- Use [rigtorp/SPSCQueue][9] pattern: bounded ring buffer, wait-free
- Cache the opposing index locally — only refresh when buffer appears full/empty. This reduces cache coherency misses
  20x (from ~300M to ~15M for 100M operations)
- Align head/tail indices to separate cache lines: `alignas(64)`
- Use `memory_order_acquire` for reading opposing index, `memory_order_release` for updating own index,
  `memory_order_relaxed` for local index reads

**MPSC Queue** (multi-producer single-consumer):

- Current implementation (Vyukov-style) is correct but allocates per-push (`new Node`). For hot path, prefer bounded
  ring buffer with atomic CAS on head index

**Disruptor pattern** (LMAX):

- For highest throughput: sequence barriers, cache-line padded sequence counters, multiple wait strategies
- Consider when throughput > 10M msgs/sec is needed

## Cache Efficiency

- All hot-path structs: `alignas(64)` + `static_assert(sizeof(T) == expected_size)` + `static_assert(alignof(T) == 64)`
- Keep related data together — avoid pointer chasing
- Use fixed-width symbol keys (`char[8]`) not `std::string`
- Pad between independent atomic variables to avoid false sharing
- Use `std::hardware_destructive_interference_size` (C++17) when available, fall back to 64
- Exploit L1 cache (5ns) vs main memory (100ns) — design for L1

## Branch Prediction

- Use `[[likely]]` / `[[unlikely]]` (C++20) for cold error paths
- **Semi-static conditions**: For branches that change rarely at runtime (e.g., risk limit changes, trading state
  transitions), consider the semi-static branch technique — dynamically rewriting branch instructions. This outperforms
  `[[likely]]` hints for branches where the "hot" path is executed infrequently but must be fast when it is.
  [Paper: arxiv 2308.14185][4]
- Use `if constexpr` for compile-time type-dependent branching
- Avoid data-dependent branches — prefer branchless arithmetic (conditional moves) where possible

## Huge Pages & TLB Optimization

TLB misses can consume 20% of execution cycles (Meta measurement). For large working sets:

- **Transparent Huge Pages (THP)**: Use `madvise(ptr, n, MADV_HUGEPAGE)` after aligned allocation. Set system-wide:
  `echo madvise > /sys/kernel/mm/transparent_hugepage/enabled`
- **Explicit huge pages**: `mmap()` with `MAP_HUGETLB` for guaranteed 2MB pages. Reserve with:
  `echo N > /proc/sys/vm/nr_hugepages`
- **1GB pages**: For very large datasets. Kernel param: `hugepagesz=1G hugepages=4`
- **Custom allocator**: Only for `std::vector`, flat hash maps — containers that make few large allocations. Round to
  page boundary.
- **mimalloc**: Set `MIMALLOC_ALLOW_LARGE_OS_PAGES=1` for automatic huge page usage. Reduced TLB misses from ~19.4M to
  ~6K in benchmarks.

Source: [rigtorp huge pages guide][6], [HRT huge pages blog][10]

## Kernel & OS Tuning (Linux)

For production deployment:

- **CPU isolation**: `isolcpus=2-7 nohz_full=2-7 rcu_nocbs=2-7` in kernel cmdline — removes scheduler ticks and RCU
  callbacks from trading cores
- **IRQ affinity**: Pin NIC interrupts to core 0 (non-trading core)
- **Kernel bypass** (future):
  - **DPDK**: Highest performance, polls NIC directly, bypasses kernel entirely. Best for dedicated trading boxes.
  - **AF_XDP**: Works within kernel via eBPF, easier deployment, good for mixed workloads.
  - **io_uring**: General-purpose async I/O, not true kernel bypass but reduces syscall overhead significantly.
  - **Solarflare OpenOnload**: Drop-in kernel bypass for standard socket apps. Common in HFT.
- **TCP tuning**: `TCP_NODELAY` always, `TCP_QUICKACK`, `SO_BUSY_POLL` for polling-mode socket reads

Source: [QuantVPS kernel bypass guide][11], [Databento kernel bypass guide][12]

## Zero-Copy Architecture (target)

The long-term goal is a zero-copy pipeline where data flows from network to strategy without any memcpy or allocation:

- **Ingestion**: Receive directly into aligned shared-memory ring buffer (io_uring or mmap'd region)
- **Decode**: SAX-style in-place parsing — write fields directly into pre-allocated MarketEvent slots
- **Transport**: Replace ZMQ IPC with shared-memory SPSC ring buffer — consumer reads the same memory the publisher
  wrote
- **Order serialization**: Pre-built JSON templates with `snprintf` into fixed buffers — no `std::string`, no
  nlohmann/json on the hot path
- **REST submission**: `writev()` / scatter-gather I/O to send pre-built buffers without concatenation

When writing new code, prefer designs that move toward zero-copy: use `std::span` / `std::string_view` for non-owning
references, write into caller-provided buffers, avoid returning `std::string` from hot-path functions.

## Serialization

For wire formats:

- **SBE (Simple Binary Encoding)**: 13x faster than FlatBuffers (80 msgs/μs vs 5 msgs/μs). Best for HFT but verbose XML
  schemas.
- **Raw struct casting**: Current approach (64-byte MarketEvent) is correct and fastest for internal same-build IPC —
  zero overhead. Only safe when producer and consumer share the same compiler, ABI, and endianness (true for our
  single-process ZMQ pipeline). Not safe across network or cross-build boundaries — use explicit serialization there.
- **simdjson**: 4x faster than RapidJSON, 25x faster than nlohmann/json. Use for parsing Alpaca REST responses.
  Gigabytes/sec throughput. Zero-copy via on-demand API.
- **nlohmann/json** (current): Fine for cold path order placement. Replace with simdjson only if REST response parsing
  is on hot path.

Source: [simdjson.org][13], [SBE vs FlatBuffers benchmark][14]

## Measurement

- Every optimization must be measured before and after
- Use `std::chrono::steady_clock` or `rdtsc` for latency
- The `arrivedAt` field in MarketEvent exists for end-to-end latency tracking — use it
- Report latency as: p50, p99, p99.9
- Monitor with `perf stat` — focus on: dTLB-load-misses, L1-dcache-load-misses, branch-misses, context-switches
- Cache warming and `constexpr` showed "most significant gains in latency reduction" per [arxiv 2309.04259][2]

[1]: https://cppcon.org/2024-keynote-david-gross/
[2]: https://arxiv.org/abs/2309.04259
[3]: https://electronictradinghub.com/best-practices-on-hft-low-latency-software/
[4]: https://arxiv.org/abs/2308.14185
[5]: https://rigtorp.se/ringbuffer/
[6]: https://rigtorp.se/hugepages/
[7]: https://medium.com/@rogerbooth/using-c-23-deducing-this-to-implement-static-polymorphism-11dd72e124a4
[8]: https://www.devgem.io/posts/exploring-c-23-crtp-and-deducing-this-for-static-polymorphism
[9]: https://github.com/rigtorp/SPSCQueue
[10]: https://www.hudsonrivertrading.com/hrtbeat/low-latency-optimization-part-1/
[11]: https://www.quantvps.com/blog/kernel-bypass-in-hft
[12]: https://databento.com/microstructure/kernel-bypass
[13]: https://simdjson.org/
[14]: https://deeperic.wordpress.com/2024/10/27/performance-comparison-of-data-serialization-formats/
