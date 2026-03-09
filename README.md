# Rich on Paper - Low-Latency Trading Engine

[![Build and Test](https://github.com/DanielMarchukov/rich-on-paper/actions/workflows/build.yml/badge.svg)](https://github.com/DanielMarchukov/rich-on-paper/actions/workflows/build.yml)
[![codecov](https://codecov.io/gh/DanielMarchukov/rich-on-paper/branch/mainline/graph/badge.svg)](https://codecov.io/gh/DanielMarchukov/rich-on-paper)
![CodeRabbit Pull Request Reviews](https://img.shields.io/coderabbit/prs/github/DanielMarchukov/rich-on-paper?utm_source=oss&utm_medium=github&utm_campaign=DanielMarchukov%2Frich-on-paper&labelColor=171717&color=FF570A&link=https%3A%2F%2Fcoderabbit.ai&label=CodeRabbit+Reviews)

A production-grade, ultra-low latency trading engine built with modern C++23, targeting sub-50 microsecond internal
processing latency. The system demonstrates professional software engineering practices including comprehensive testing,
multi-platform support, and continuous integration.

## Key Features

- **Ultra-Low Latency**: Target sub-50μs internal processing latency using lock-free data structures and CPU affinity
- **Single-Binary Architecture**: Entire pipeline in C++23 — market data, strategy, risk, execution
- **Production-Grade**: 80%+ test coverage, CI/CD pipeline, and cross-platform support (Linux, macOS, Windows)
- **Real Market Data**: Integrates with Alpaca Markets for live and paper trading
- **Modular Design**: Clean separation between market data, strategy, risk management, and execution

## Technology Stack

- **C++23**: Trading engine with template metaprogramming and compile-time optimizations
- **ZeroMQ**: High-performance IPC/TCP messaging between components
- **msgpack-cxx**: Zero-copy msgpack decoding for Alpaca WebSocket market data
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
├── tests/                  # C++ unit tests (Google Test)
├── setup/                  # Platform-specific setup scripts
├── docs/                   # Documentation
│   ├── ARCHITECTURE.md     # System design and architecture
│   ├── PERFORMANCE.md      # Performance Benchmarks
│   └── QUICKSTART_*.md     # Platform quickstart guides
└── CMakeLists.txt          # CMake configuration
```

## Architecture Overview

The trading engine runs as a single process with dedicated threads for different components:

- **C++ Market Publisher**: Connects to Alpaca WebSocket, decodes msgpack, publishes MarketEvents via ZeroMQ
- **C++ Consumer Threads**: One per symbol, receives market data and generates orders based on trading strategy
- **Risk Manager**: Validates orders against position limits and risk parameters
- **Order Gateway**: Executes approved orders via REST API

For detailed architecture documentation, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Testing

Run the test suite:

```bash
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

### Sanitizer Builds

Sanitizer builds instrument the binary to detect memory errors, undefined behavior, and data races at runtime. They run
the same test suite but with runtime checks enabled.

```bash
# ASAN + UBSAN (memory errors + undefined behavior)
cmake -B build-asan -S . -DCMAKE_BUILD_TYPE=Debug \
  -DSANITIZE_ADDRESS=ON -DSANITIZE_UNDEFINED=ON \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-asan
(cd build-asan && ctest --output-on-failure)

# TSAN (data races) — separate build, incompatible with ASAN
cmake -B build-tsan -S . -DCMAKE_BUILD_TYPE=Debug \
  -DSANITIZE_THREAD=ON \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build-tsan
(cd build-tsan && ctest --output-on-failure)
```

ASAN+UBSAN runs on every push in CI. TSAN runs nightly.

### Code Quality Tools

- **C++**: clang-format, clang-tidy, LLVM coverage, ASAN/TSAN/UBSAN
- **Security**: CodeQL

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
