# Rich on Paper - Low-Latency Trading Engine

## Context and Scope

This project is about building an ultra-low latency trading engine capable of processing real-time market data and
executing trades with a target internal processing latency of less than 50 microseconds. The architecture leverages
modern C++20 for performance-critical components, while Python is used for data ingestion. This is a living document
that will evolve as the project progresses, capturing design decisions, architecture diagrams, and performance metrics.

Key Technologies: C++20, Python3.12, ZeroMQ, GoogleTest, Boost.

## Goals

I have set out the following goals for this project, with functional and non-functional requirements listed below.

### Personal Goals

- Learn C++ while building a production-grade trading system
- Leverage CI/CD to really treat this as a production system, add tests, and ensure code quality
- Learn and put into practice Operating System concepts, Networking, and Low Level optimizations
- Learn about (algorithmic) trading, market making, the market structure, and solidify my knowledge.

### Functional requirements

- Process real-time market data.
- Create buy/sell signals.
- Evaluate risk the risk of trades.
- Track currently held positions, performance, PnL metrics.
- Communicate with the exchange API, send out orders.
- Track fills and update positions.

### Non-functional requirements

- Sub 50μs internal processing latency. This excludes the latency over the public internet, but this can be improved
later on as a stretch goal.
- Template metaprogramming and compile-time optimizations to reduce runtime overhead, as well as enhance type safety.
- At least 80% test coverage over the codebase.
- Multiplatform support and ease of setup to run locally on any machine.

## 🚀 Design

The approach is to split the application into multiple key components:

- [Hot Path] Market Data Ingestion - Python component for simplicity, normalizes the data and packs it into a struct.
Sends it off to C++ via ZeroMQ.
- [Hot Path] Market Data Consumer - C++ component that receives the data from the ZeroMQ socket and passes it to the
Strategy.
- [Hot Path] Strategy - constructs buy/sell orders based on market events.
- [Hot Path] Risk Manager - filters out orders that violate risk limits. Refers to Position Manager for current
positions and PnL.
- [Hot/Cold Path] Execution Gateway - sends out orders via Exchange APIs (hot). Passes fill events to
Position Manager (cold).
- [Cold Path] Position Manager - tracks current positions, PnL, and provides state to the Risk Manager.

Based on my research, this approach is close to real world trading applications, which I aim to replicate.

Below is a diagram of the proposed architecture.

### 📈 Architecture

#### Initial Architecture Diagram

```text
                                  [ Alpaca Websocket ]
                                          |
                                          | Raw Market Data (msgpack)
                                          V
+-----------------------------------------+ - - - - - - - - - - - - - +
|      PYTHON PROCESS                     |                           |
|                                         |                           |
|  +---------------------------+          |                           |
|  |     Python Publisher      |          |  (data-ingestion/src)     |
|  | (publisher.py)            |          |                           |
|  |---------------------------|          |                           |
|  | - Connects to Websocket   |          |                           |
|  | - Normalizes data into a  |          |                           |
|  |   48-byte struct          |          |                           |
|  +---------------------------+          |                           |
|              |                          |                           |
+--------------|--------------------------+ - - - - - - - - - - - - - +
               |
               | 48-byte MarketEvent
               | [ ZMQ: ipc://market_data.sock ]
               V
+------------------------------------------------------------------------------------+
|      C++ PROCESS (Strategy Engine)                                                 |
|                                                                                    |
|  +--------------------------------+   (Query Position)     +---------------------+ |
|  |  C++ Consumer Thread (AAPL)    |   +------------------->|  PositionManager    | |
|  |--------------------------------|   |                    | (Shared Singleton)  | | (Shared
|  | 1. Receives MarketEvent        |   |   +----------------|---------------------| |  Objects)
|  |                                |   |   | (Read State)   | - Owns all current  | |
|  | 2. Strategy->onMarketEvent()   |   |   |                |   positions & PnL   | |
|  |    (generates Proposed Order)  |   |   |                +---------------------+ |
|  |           |                    |   |   +--------------+                      ^  |
|  |           | Proposed Order     |   |________________. |           Fill Event |  |
|  |           V                    |                    | |                      |  |
|  | 3. RiskManager->isAllowed()    |                    +--------------------+   |  |
|  |    (queries PositionManager)   |------------------->|   RiskManager      |   |  |
|  |           ^                    |       (Read State) | (Shared Singleton) |   |  |
|  |           | Approve/Reject     |<-------------------|--------------------|   |  |
|  |           |                    |                    | - Owns risk limits |   |  |
|  |           |                    |                    |   (e.g. max size)  |   |  |
|  | 4. If Approved, send to Exec   |                    +--------------------+   |  |
|  |           |                    |                                             |  |
|  +-----------|--------------------+                                             |  |
|              | Final Order                                                      |  |
|              |                                                                  |  |
|              V                                                                  |  |
|  +---------------------------+                                                  |  |
|  |   Execution Gateway       |                                                  |  |
|  |   (Dedicated Thread)      | -------> [ Exchange API (e.g., FIX) ]            |  |
|  |---------------------------| <------        (Fill Confirmation)               |  |
|  | - Sends orders to exchange|                                                  |  |
|  | - Receives fills back     |                                                  |  |
|  +---------------------------+                                                  |  |
|              |                                                                  |  |
|              | Fill Event (e.g. "BOUGHT 100 AAPL @ 150.25")                     |  |
|              |                                                                  |  |
|              +------------------------------------------------------------------+  |
|                                                                                    |
+------------------------------------------------------------------------------------+
```

#### Existing Architecture Diagram

```text
                               +----------------------------------+
                               |     Python Data Publisher        |
                               |    (Pinned to e.g. Core 0)       |
                               +----------------------------------+
                                                |
                                                | 48-byte MarketEvent
                                                | [ZMQ IPC: ipc:///tmp/market_data.sock]
                                                V
+--------------------------------------------------------------------------------------------------+
|                                   C++ TRADING ENGINE PROCESS                                     |
|                                                                                                  |
|   +---------------------------------------+      +-------------------------------------------+   |
|   |      TradingEngine (main thread)      |      |      ExecutionGateway (Worker Thread 1)   |   |
|   |---------------------------------------|      |-------------------------------------------|   |
|   | - Owns all shared components          |      | - Listens on a thread-safe Order Queue    |   |
|   | - Creates & launches all threads      |      | - Makes slow, blocking REST API calls     |   |
|   | - Manages clean shutdown              |      | - Receives Fills from broker (simulated)  |   |
|   +---------------------------------------+      | - WRITES to PositionManager               |   |
|                  |     ^                         +-------------------------------------------+   |
| (Creates &       |     | (Shared via std::shared_ptr)                                            |
|  Injects)        |     |                                                                         |
|   +--------------+-----+------------------+      +-----------------------------------------+     |
|   | std::shared_ptr<PositionManager>      |      |      ThreadSafeQueue<Order>             |     |
|   | std::shared_ptr<RiskManager>          |      |-----------------------------------------|     |
|   | std::shared_ptr<ThreadSafeQueue>      |      | - Decouples Hot Path from Cold Path     |     |
|   +---------------------------------------+      | - Single point of contention (Mutex)    |     |
|                                                  +-----------------------------------------+     |
|                                                                                                  |
| /-------------------------------------------------------------------------------------------\    |
| |                     HOT PATH - CONSUMER THREAD (e.g. "AAPL" on Core 2)                    |    |
| |-------------------------------------------------------------------------------------------|... |
| | 1. SUB socket receives MarketEvent                                                        |    |
| |           |                                                                               |    |
| |           V (Direct C++ function call)                                                    |    |
| | 2. strategy.processMarketEvent() -> returns std::vector<Order>                            |    |
| |           |                                                                               |    |
| |           V (Loop through proposed orders)                                                |    |
| | 3. risk_manager.onNewOrder(order) -> returns bool                                         |    |
| |    (Performs a lock-free READ from PositionManager)                                       |    |
| |           |                                                                               |    |
| |           V (If Approved)                                                                 |    |
| | 4. PUSH order onto the ThreadSafeQueue                                                    |    |
| |              --- END OF LOW-LATENCY HOT PATH (sub-microsecond) ---                        |    |
| \-------------------------------------------------------------------------------------------/    |
|                                                                                                  |
+--------------------------------------------------------------------------------------------------+
```

The arrows depict the data flow of a market event/fill through the system.

## 🔧 Key Design Decisions

### Python over C++ for the data ingestion

There were a few reasons for this choice:

- Initial quick prototuping through Python
- I wanted to explore multi language stack - using Python and C++ for the trading system, together.
- Alpaca's C++ library is not well maintained, but their Python library has more recent updates and more community
resources/support.
- I can deepen my Low-Latency Python knowledge.

### ZeroMQ for communication between processes

I evaluated ZeroMQ, Kafka or some similar messaging queue system, Redis Pub/Sub, and shared memory. I know from
experience as well as theoretical knowledge that messaging queue systems are not fast enough for ultra low latency.
Redis is fast as a cache, but still is not there for the ultra low latency requirement that I have set out
(less than 50 microseconds). So the real choice is between using ZeroMQ or shared memory.

ZeroMQ is quite easy to setup and get going with. It does not meet the ultra low latency requirement - potentially
possible if going for inproc protocol instead of IPC (inproc is similar to shared memory), but it has the
infrastructure to support exchange between processes, or even between threads.

Long-term, shared memory wins out, and is a good learning opportunity. The difference is that shared memory, ring
buffers - these can be a project of its own, and the initial goal is to get things running quickly, then iterate on it.
Especially I don't expect ZeroMQ to be a significant tech debt. It still wins over the other options.

### Alpaca API now, Databento later

I looked at the following options:

- Alpaca API - has free tier, as well as paid - Python library.
- Databento - has free tier, and multiple paid plans - C++ library.
- EODHD - free tier, slightly more options than Alpaca, but not much different. Has a lot of historical data though,
- and some other features going for it.

Based on the other decisions I have made, and the design choices, Alpaca fits in nicely with the free tier. It also has
a paper trading API, which is exactly what I will need to submit my orders to the exchange. So the starter bundle is
perfect. On top of that, lots of users on reddit recommend Alpaca for pet projects in the algo trading space.

Databento came up as an option when looking into low latency data ingestion, and striving to _long-term_ make the
market data to exchange latency in the sub-100 microseconds range. That is because they provide raw TCP access to
the data through their C++ client. Going for C++ data ingestion is a very likely future improvement, hence Databento
remains a top contender for data ingestion.

### vcpkg over raw CMakeLists.txt based dependency management

Initially this project started out with trying to be pure CMake project when it comes to dependencies, and soon I
realised not all C++ dependencies/libraries are trivially available. New CMake versions make it possible to fetch
the source and build it inside the project, but that adds a lot of compile time, and sometimes I needed custom CMake
code to actually be able to find/build the libraries.

The other aspect is related to my goal for making this project easy to consume and run locally for anyone - I care
about correctness and ease of setup.

### Static Dispatch and Template Metaprogramming over Dynamic Dispatch

TBD

## Future improvements

In no particular order.

### Replace Python ingestion with C++ and move to Databento dataset

This will bring several benefits:

- Databento has a high quality dataset and well supported C++ client.
- Provides raw TCP access for ingesting events (Alpaca has only websockets).
- Wider range of exchanges supported; Alpaca's data only covers IEX.
- Ultimately, C++ is faster than Python.

### Replace ZeroMQ with Ring Buffer (LMAX Disruptor)

I can utilize shared memory and a ring buffer to achieve lower latency than ZeroMQ, which is a message broker. This
will allow for more direct communication between threads and reduce overhead.

### Backtesting support

The idea is be to run historical data through the same flow as live data.
There will be some considerations to address, especially with handling historical data,
getting it ingested, etc.

### ~Multiplatform support~ -- DONE

Currently, I suspect my setup does not work on Windows, which I want to address at some point and verify I am able to
run the code anywhere.

## Links

Some resources and books that I am consuming while working on this project, and attempting to implement the learnings:

- Operating Systems: Three Easy Pieces - [Book](http://pages.cs.wisc.edu/~remzi/OSTEP/)
- C++ Concurrency in Action - [Book](https://www.manning.com/books/c-concurrency-in-action-second-edition)
- TCP/IP Illustrated - [Book](https://www.amazon.com/TCP-Illustrated-Volume-Addison-Wesley-Professional/dp/0201633469)
- Building Low Latency Applications with C++ - [Book](https://www.amazon.co.uk/Building-Low-Latency-Applications-ecosystem/dp/1837639353)
- C++ Software Design - [Book](https://www.oreilly.com/library/view/c-software-design/9781098113155/)

Other references/documentation:

- [Alpaca](https://alpaca.markets/)
- [Databento](https://databento.com/)

## 🛠️ Getting Started / How To Run

TBD
