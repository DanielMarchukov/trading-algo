# [Rich on Paper] - Low-Latency Trading System

**Career Focus**: Demonstrating C++/Python hybrid architecture skills for front
office, quantitative development roles

## 🚀 Project Overview

- **Objective**: Real-time market data processing and trading with <50μs latency
- **Key Technologies**: Modern C++20, Python 3.12, ZeroMQ, QuantLib

## 📈 Real-Time Architecture

```
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
|  |   40-byte struct          |          |                           |
|  +---------------------------+          |                           |
|              |                          |                           |
+--------------|--------------------------+ - - - - - - - - - - - - - +
               |
               | 40-byte MarketEvent
               | [ ZMQ: inproc://market_data ]
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
|  |    (generates Proposed Order)  |   |   +--------------+ +---------------------+ |
|  |           |                    |   |                  |                      ^  |
|  |           | Proposed Order     |   +----------------+ |           Fill Event |  |
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
|              | [ ZMQ: inproc://execution_orders ]                               |  |
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
|              | [ ZMQ: inproc://fill_events ]                                    |  |
|              +------------------------------------------------------------------+  |
|                                                                                    |
+------------------------------------------------------------------------------------+
```

### Performance Metrics

## 🔧 Key Design Decisions

TBD

| Decision Point | Choice            |
| -------------- | ----------------- |
| IPC Mechanism  | ZeroMQ over Redis |

## 🛠️ Getting Started

TBD
