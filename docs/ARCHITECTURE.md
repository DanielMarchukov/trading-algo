# System Architecture - Rich on Paper Trading Engine

This document details the system architecture, design decisions, and technical implementation of the Rich on Paper
low-latency trading engine.

## Table of Contents

- [System Overview](#system-overview)
- [Architecture Diagrams](#architecture-diagrams)
- [Component Design](#component-design)
- [Key Design Decisions](#key-design-decisions)
- [Performance Optimizations](#performance-optimizations)
- [Future Improvements](#future-improvements)

## System Overview

The trading engine is designed as a multiprocess system with clear separation of concerns:

1. **Hot Path Components** (latency-critical):

   - Market Data Ingestion and normalization (Python)
   - Market Data Consumer (C++)
   - Trading Strategy execution
   - Risk Management checks

1. **Warm/Cold Path Components**:

   - Order Gateway (REST API calls)
   - Position Manager updates
   - Fill processing

## Architecture Diagrams

### Current Architecture

```text
                               +----------------------------------+
                               |     Python Data Publisher        |
                               |         (Core 0)                 |
                               |----------------------------------|
                               | • Alpaca WebSocket Client        |
                               | • Normalizes to 64-byte struct   |
                               | • ZMQ PUB Socket                 |
                               +----------------------------------+
                                              |
                                              | 64-byte MarketEvent
                                              | [ZMQ IPC: ipc:///tmp/market_data.sock]
                                              | [Windows: tcp://127.0.0.1:5555]
                                              ↓
╔════════════════════════════════════════════════════════════════════════════════════════╗
║                                C++ TRADING ENGINE PROCESS                              ║
║                                                                                        ║
║  ┌─────────────────────────────────┐          ┌──────────────────────────────────────┐ ║
║  │   TradingEngine (Main Thread)   │          │   OrderGateway (Worker Thread)       │ ║
║  │─────────────────────────────────│          │──────────────────────────────────────│ ║
║  │ • Creates all components        │          │ • Polls LockFreeMPSCQueue<Order>       │ ║
║  │ • Launches threads              │          │ • Makes REST API calls to Alpaca     │ ║
║  │ • Handles signals (SIGINT/TERM) │          │ • Pinned to Core 0                   │ ║
║  │ • Manages shutdown              │          │ • TODO: Fill listener not impl yet   │ ║
║  └─────────────┬───────────────────┘          └──────────────────────────────────────┘ ║
║                │                                            ↑                          ║
║                │ Creates & owns (std::shared_ptr)           │ Reads orders             ║
║                ↓                                            │                          ║
║  ┌──────────────────────────────────────────────┐  ┌────────┴───────────────────────┐  ║
║  │             Shared Components                │  │   LockFreeMPSCQueue<Order>       │  ║
║  │──────────────────────────────────────────────│  │────────────────────────────────│  ║
║  │ • PositionManager (thread-safe R/W)          │  │ • Lock-free Vyukov MPSC queue   │  ║
║  │ • RiskManager (reads PositionManager)        │  │ • Atomic push, single consumer │  ║
║  │ • LockFreeMPSCQueue<Order>                     │  │ • Decouples hot/cold paths     │  ║
║  └──────────────────────────────────────────────┘  └────────────────────────────────┘  ║
║                     ↑                                                                  ║
║                     │ Shared access (read-only for hot path)                           ║
║                     │                                                                  ║
║  ╔══════════════════╪════════════════════════════════════════════════════════════════╗ ║
║  ║                  │        HOT PATH - CONSUMER THREADS (Core 1, 2, 3...)           ║ ║
║  ╟──────────────────┴────────────────────────────────────────────────────────────────╢ ║
║  ║                                                                                   ║ ║
║  ║  Per-Symbol Thread (e.g., "AAPL" on Core 1):                                      ║ ║
║  ║  ┌────────────────────────────────────────────────────────────────────────────┐   ║ ║
║  ║  │ 1. ZMQ SUB socket receives MarketEvent (64 bytes)                          │   ║ ║
║  ║  │    └─> Zero-copy cast to struct                                            │   ║ ║
║  ║  │                                                                            │   ║ ║
║  ║  │ 2. SimpleMarketMakingStrategy::onMarketEvent(event)                        │   ║ ║
║  ║  │    └─> Returns std::vector<Order> (pre-allocated capacity)                 │   ║ ║
║  ║  │                                                                            │   ║ ║
║  ║  │ 3. For each order: RiskManager::onNewOrder(order)                          │   ║ ║
║  ║  │    └─> Lock-free READ from PositionManager (atomic<int>)                   │   ║ ║
║  ║  │    └─> Returns bool (approve/reject)                                       │   ║ ║
║  ║  │                                                                            │   ║ ║
║  ║  │ 4. If approved: order_queue_->push(order)                                  │   ║ ║
║  ║  │    └─> Lock-free atomic push (Vyukov MPSC)                               │   ║ ║
║  ║  └────────────────────────────────────────────────────────────────────────────┘   ║ ║
║  ║                                                                                   ║ ║
║  ║  Target Latency: <50μs from market event to queue                                 ║ ║
║  ╚═══════════════════════════════════════════════════════════════════════════════════╝ ║
║                                                                                        ║
║  Key Design Points:                                                                    ║
║  • Template-based Strategy injection (compile-time polymorphism)                       ║
║  • Lock-free position reads using atomics                                              ║
║  • Lock-free order queue (no contention on push)                                          ║
║  • CPU affinity for predictable latency                                                ║
║  • Static linking for performance                                                      ║
╚════════════════════════════════════════════════════════════════════════════════════════╝

Legend:
  ─────  Component boundary          ↓  Data flow
  ═════  Process/Thread boundary     •  Key features
  ┌───┐  Internal component
```

## Component Design

### Market Data Publisher (Python)

- **Purpose**: Connect to Alpaca WebSocket API and normalize market data
- **Design**: Single-threaded with asyncio for WebSocket handling
- **Output**: 64-byte packed struct via ZeroMQ PUB socket
- **CPU Affinity**: Pinned to Core 0

### Market Event Consumer (C++)

- **Template-based design** for compile-time strategy injection
- **Per-symbol threads** with CPU affinity (Core 1, 2, 3...)
- **Zero-copy processing** of market events
- **Direct function calls** (no virtual dispatch)

```c++
struct MarketEvent {
    uint64_t eventType;   // 1=Quote, 2=Trade
    char     symbol[8];   // Null-padded symbol
    uint64_t timestamp;   // Exchange timestamp (ns)
    uint64_t p1;          // Bid/Trade price (scaled by 10000)
    uint64_t s1;          // Bid/Trade size
    uint64_t p2;          // Ask price (scaled by 10000)
    uint64_t s2;          // Ask size
    uint64_t arrivedAt;   // Local arrival timestamp (ns)
};
static_assert(sizeof(MarketEvent) == 64);
```

### Strategy Engine

- **Interface**: Pure virtual base class with static dispatch
- **Current Implementation**: Simple market making strategy
- **Stateless design** for thread safety
- **Returns vector of orders** (pre-allocated capacity)

### Risk Manager

- **Lock-free reads** from PositionManager using atomics
- **Configurable limits**:
  - Maximum position per symbol: 1000 shares
  - Maximum order value: $10,000
- **Sub-microsecond validation** time

### Order Gateway

- **Dedicated thread** for REST API calls
- **Thread-safe queue** for order submission
- **Async design** to prevent blocking hot path
- **Handles retries and errors** gracefully

### Position Manager

- **Thread-safe** via oneTBB `concurrent_hash_map`
- **64-bit position counters** to track large exposures
- **Fixed-width 8-byte symbols** with custom hash comparator

## Key Design Decisions

### 1. Python for Data Ingestion

**Decision**: Use Python for market data ingestion instead of C++

**Rationale**:

- Alpaca's Python SDK is better maintained than C++ alternatives
- Rapid prototyping and iteration
- WebSocket handling is simpler in Python
- Performance impact is minimal (network I/O bound)

**Trade-offs**:

- Additional serialization overhead
- Cross-language complexity
- Separate process management

### 2. ZeroMQ for IPC

**Decision**: Use ZeroMQ instead of shared memory

**Rationale**:

- Simpler implementation and debugging
- Built-in pub/sub pattern
- Cross-platform compatibility
- Good enough latency (~10-20μs overhead)

**Future**: Plan to migrate to lock-free shared memory ring buffer

### 3. Template Metaprogramming

**Decision**: Heavy use of templates for compile-time polymorphism

**Rationale**:

- Zero-cost abstractions
- Eliminates virtual function overhead
- Better compiler optimizations
- Type safety at compile time

**Example**:

```cpp
template <typename StrategyType>
class MarketEventConsumer {
    // Strategy type resolved at compile time
    std::unique_ptr<StrategyType> strategy_;
};
```

### 4. Per-Symbol Threading Model

**Decision**: Dedicated thread per trading symbol

**Rationale**:

- Natural parallelism
- No lock contention between symbols
- CPU cache efficiency
- Predictable latency

**Trade-offs**:

- Limited scalability (one thread per symbol)
- Resource usage for many symbols

### 5. Static Linking

**Decision**: Use static linking for all dependencies

**Rationale**:

- Deployment simplicity
- Better performance (no DLL lookups)
- Reproducible builds
- No runtime dependency issues

## Performance Optimizations

### Memory Layout

- **Cache-line aligned structures** for hot path data
- **Compact 64-byte market events** remain cache friendly
- **Pre-allocated vectors** to avoid dynamic allocation
- **Custom allocators** (planned) for deterministic performance

### CPU Optimizations

- **CPU affinity** for all threads
- **NUMA awareness** (planned for multi-socket systems)
- **Compiler optimizations**: `-O3`, `-march=native`
- **Link-time optimization** (LTO) enabled

### Lock-Free Design

- **Atomic operations** for position updates
- **Lock-free MPSC queue** for order submission
- **Read-heavy optimization** in PositionManager
- **Minimal mutex usage** only where necessary

### Network Optimizations

- **TCP_NODELAY** for low-latency sockets
- **Kernel bypass** (planned) using DPDK
- **Direct market access** (future) via FIX protocol

## Future Improvements

### Performance Enhancements

1. **Replace ZeroMQ with Shared Memory Ring Buffer**

   - Implement SPSC lock-free queue
   - Use memory-mapped files for persistence
   - Target: \<2μs IPC latency

1. **C++ Market Data Ingestion**

   - Migrate to Databento C++ client
   - Direct TCP connection to exchange
   - Remove Python serialization overhead

1. **Custom Memory Allocators**

   - Pool allocators for fixed-size objects
   - NUMA-aware allocation
   - Reduce allocation jitter

### Feature Additions

1. **Backtesting Engine**

   - Historical data replay through same pipeline
   - Performance metrics and analysis
   - Strategy parameter optimization

1. **Advanced Risk Management**

   - Portfolio-level risk limits
   - Greeks calculation for options
   - Real-time P&L tracking

1. **Strategy Framework**

   - Plugin architecture for strategies
   - Hot-reloading of strategies
   - A/B testing framework

## Performance Targets

### Current Performance

This isn't benchmarked oficially yet; TBD.

### Target Performance

- Market Data to Strategy: \<15μs
- Strategy Processing: \<15μs
- Risk Validation: \<15μs
- Total Hot Path: \<50μs
