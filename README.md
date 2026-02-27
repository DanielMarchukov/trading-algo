# Rich on Paper - Low-Latency Trading Engine

[![Build and Test](https://github.com/DanielMarchukov/rich-on-paper/actions/workflows/build.yml/badge.svg)](https://github.com/DanielMarchukov/rich-on-paper/actions/workflows/build.yml)
[![codecov](https://codecov.io/gh/DanielMarchukov/rich-on-paper/branch/mainline/graph/badge.svg)](https://codecov.io/gh/DanielMarchukov/rich-on-paper)

A production-grade, ultra-low latency trading engine built with modern C++23 and Python, targeting sub-50 microsecond
internal processing latency. The system demonstrates professional software engineering practices including comprehensive
testing, multi-platform support, and continuous integration.

## Key Features

- **Ultra-Low Latency**: Target sub-50μs internal processing latency using lock-free data structures and CPU affinity
- **Multi-Language Architecture**: High-performance C++23 core with Python data ingestion via ZeroMQ
- **Production-Grade**: 80%+ test coverage, CI/CD pipeline, and cross-platform support (Linux, macOS, Windows)
- **Real Market Data**: Integrates with Alpaca Markets for live and paper trading
- **Modular Design**: Clean separation between market data, strategy, risk management, and execution

## Technology Stack

- **C++23**: Core trading engine with template metaprogramming and compile-time optimizations
- **Python 3.12**: Market data ingestion and normalization
- **ZeroMQ**: High-performance IPC/TCP messaging between components
- **vcpkg**: Cross-platform C++ dependency management
- **CMake**: Build system with modern CMake practices
- **Google Test**: Comprehensive unit and integration testing

## Prerequisites

Before running the trading engine, ensure you have:

1. **Alpaca Markets Account**: Sign up at [alpaca.markets](https://alpaca.markets/) for free paper trading API access
1. **Operating System**: Ubuntu 20.04+, macOS 13+, or Windows 10/11
1. **Development Tools**: See platform-specific quickstart guides

## Quick Start

Choose your platform and follow the step-by-step guide:

- [**Ubuntu/Linux Quick Start**](docs/QUICKSTART_UBUNTU.md)
- [**macOS Quick Start**](docs/QUICKSTART_MACOS.md)
- [**Windows Quick Start**](docs/QUICKSTART_WINDOWS.md)

### TL;DR for Experienced Developers

```bash
# Clone and setup
git clone https://github.com/yourusername/rich-on-paper.git
cd rich-on-paper

# Run platform-specific setup script
./setup/setup_ubuntu.sh    # or setup_macos.sh, or .\setup\setup_windows.ps1

# Configure API credentials
cp .env.template .env
# Edit .env with your Alpaca API credentials

# Run the trading system
./run_trading_system.sh    # or platform-specific script
```

## Project Structure

```text
rich-on-paper/
├── src/                    # C++ source files
├── include/                # C++ headers
├── tests/                  # C++ unit tests
├── data-ingestion/         # Python market data publisher
│   └── src/
│       ├── publisher.py    # WebSocket client for Alpaca
│       └── test_*.py       # Python tests
├── setup/                  # Platform-specific setup scripts
├── docs/                   # Documentation
│   ├── ARCHITECTURE.md     # System design and architecture
│   ├── PERFORMANCE.md      # Performance Benchmarks
│   └── QUICKSTART_*.md     # Platform quickstart guides
└── CMakeLists.txt          # CMake configuration
```

## Architecture Overview

The trading engine uses a multiprocess architecture with dedicated threads for different components:

- **Python Publisher**: Connects to Alpaca WebSocket, normalizes data, publishes via ZeroMQ
- **C++ Consumer Threads**: One per symbol, receives market data and generates orders based on trading strategy
- **Risk Manager**: Validates orders against position limits and risk parameters
- **Order Gateway**: Executes approved orders via REST API

For detailed architecture documentation, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Testing

Run the comprehensive test suite:

```bash
# Python tests
pytest data-ingestion/src/ -v

# C++ tests
cd build && ctest --output-on-failure
```

## Performance

Performance Benchmarking is still TBD.

## Development

### Building from Source

```bash
# Configure with CMake & vcpkg
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build build

# Run tests
cd build && ctest
```

### Code Quality Tools

- **C++**: clang-format, clang-tidy, LLVM coverage
- **Python**: black, isort, pylint, mypy
- **Security**: CodeQL, bandit, safety

## Documentation

- [System Architecture](docs/ARCHITECTURE.md) - Detailed design and technical decisions
- [Performance Tuning](docs/PERFORMANCE.md) - Low-latency optimizations

## Project Goals

This project demonstrates:

1. **Production-grade C++ development** with modern standards and best practices
1. **Low-latency system design** with careful attention to cache efficiency and lock-free programming
1. **Professional software engineering** including CI/CD, testing, and documentation
1. **Financial markets knowledge** applied to algorithmic trading

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
