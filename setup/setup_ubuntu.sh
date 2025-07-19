#!/bin/bash
#
# Usage:
#   chmod +x setup_ubuntu_dev.sh
#   ./setup_ubuntu_dev.sh
#
# After running this script, you can start the trading system with:
#   ./run_trading_system.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_status() {
    echo -e "${GREEN}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

if ! grep -q "Ubuntu" /etc/os-release; then
    print_error "This script is designed for Ubuntu. Detected: $(lsb_release -d)"
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

print_status "Starting Rich-on-Paper Trading Engine setup..."

print_status "Updating system packages..."
sudo apt-get update -y
sudo apt-get upgrade -y

print_status "Installing basic development tools..."
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    curl \
    wget \
    unzip \
    tar \
    pkg-config

print_status "Checking Python installation..."
if ! command -v python3.12 &> /dev/null; then
    print_status "Installing Python 3.12..."
    sudo apt-get install -y software-properties-common
    sudo add-apt-repository -y ppa:deadsnakes/ppa
    sudo apt-get update
    sudo apt-get install -y python3.12 python3.12-venv python3.12-dev
else
    print_status "Python 3.12 already installed"
fi

print_status "Installing C++ library dependencies..."
sudo apt-get install -y \
    libboost-all-dev \
    libzmq3-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    autoconf \
    automake \
    autoconf-archive \
    libtool \
    linux-libc-dev

VCPKG_ROOT="$HOME/vcpkg"
print_status "Setting up vcpkg in $VCPKG_ROOT..."
if [ ! -d "$VCPKG_ROOT" ]; then
    git clone https://github.com/Microsoft/vcpkg.git "$VCPKG_ROOT"
    "$VCPKG_ROOT/bootstrap-vcpkg.sh"
else
    print_status "vcpkg already installed, updating..."
    cd "$VCPKG_ROOT"
    git pull
    "$VCPKG_ROOT/bootstrap-vcpkg.sh"
    cd -
fi

if ! grep -q "VCPKG_ROOT" ~/.bashrc; then
    echo "export VCPKG_ROOT=$VCPKG_ROOT" >> ~/.bashrc
    print_status "Added VCPKG_ROOT to ~/.bashrc"
fi

export VCPKG_ROOT="$VCPKG_ROOT"

print_status "Setting up project environment..."

print_status "Creating Python virtual environment..."
if [ ! -d "env" ]; then
    python3.12 -m venv env
fi

print_status "Installing Python dependencies..."
source env/bin/activate
pip install --upgrade pip
pip install -r data-ingestion/requirements.txt

print_status "Cleaning previous build artifacts..."
rm -rf build/
rm -rf vcpkg_installed/

print_status "Configuring CMake project with vcpkg..."
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -G Ninja

print_status "Building C++ trading engine..."
cmake --build build

print_status "Running tests to verify setup..."
print_status "Running Python tests..."
pytest data-ingestion/src/ -v

print_status "Running C++ tests..."
cd build
ctest --output-on-failure
cd ..

print_status "Creating run script..."
cat > run_trading_system.sh << 'EOF'
#!/bin/bash
#
# run_trading_system.sh - Start the Rich-on-Paper trading system
#
# This script starts both the Python data publisher and C++ trading engine
# in the correct order with proper error handling.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

print_status() {
    echo -e "${GREEN}[$(date +'%Y-%m-%d %H:%M:%S')]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if virtual environment exists
if [ ! -d "env" ]; then
    print_error "Python virtual environment not found. Run setup_ubuntu_dev.sh first."
    exit 1
fi

# Check if build directory exists
if [ ! -d "build" ] || [ ! -f "build/hello" ]; then
    print_error "C++ binary not found. Run setup_ubuntu_dev.sh first."
    exit 1
fi

# Check for required environment variables
if [ -z "$APCA_API_KEY_ID" ] || [ -z "$APCA_API_SECRET_KEY" ]; then
    print_error "Alpaca API credentials not set!"
    echo "Please set the following environment variables:"
    echo "  export APCA_API_KEY_ID='your_key_here'"
    echo "  export APCA_API_SECRET_KEY='your_secret_here'"
    echo ""
    echo "You can add these to ~/.bashrc for persistence."
    exit 1
fi

# Cleanup function
cleanup() {
    print_status "Shutting down trading system..."

    # Kill Python publisher if running
    if [ ! -z "$PUBLISHER_PID" ]; then
        kill $PUBLISHER_PID 2>/dev/null || true
    fi

    # Kill C++ engine if running
    if [ ! -z "$ENGINE_PID" ]; then
        kill $ENGINE_PID 2>/dev/null || true
    fi

    # Wait a moment for processes to terminate
    sleep 2

    # Force kill if still running
    if [ ! -z "$PUBLISHER_PID" ]; then
        kill -9 $PUBLISHER_PID 2>/dev/null || true
    fi
    if [ ! -z "$ENGINE_PID" ]; then
        kill -9 $ENGINE_PID 2>/dev/null || true
    fi

    print_status "Cleanup complete"
}

# Set trap to cleanup on exit
trap cleanup EXIT INT TERM

# Start Python publisher
print_status "Starting Python market data publisher..."
source env/bin/activate
python data-ingestion/src/publisher.py &
PUBLISHER_PID=$!

# Wait for publisher to initialize and bind to socket
print_status "Waiting for publisher to initialize..."
sleep 3

# Check if publisher is still running
if ! kill -0 $PUBLISHER_PID 2>/dev/null; then
    print_error "Python publisher failed to start!"
    exit 1
fi

# Start C++ trading engine
print_status "Starting C++ trading engine..."
./build/hello &
ENGINE_PID=$!

# Wait a moment to check if engine started successfully
sleep 2

# Check if engine is still running
if ! kill -0 $ENGINE_PID 2>/dev/null; then
    print_error "C++ trading engine failed to start!"
    exit 1
fi

print_status "Trading system is running!"
print_status "Publisher PID: $PUBLISHER_PID"
print_status "Engine PID: $ENGINE_PID"
print_status "Press Ctrl+C to stop..."

# Wait for user interrupt
while true; do
    sleep 1

    # Check if processes are still running
    if ! kill -0 $PUBLISHER_PID 2>/dev/null; then
        print_error "Python publisher crashed!"
        break
    fi

    if ! kill -0 $ENGINE_PID 2>/dev/null; then
        print_error "C++ trading engine crashed!"
        break
    fi
done
EOF

chmod +x run_trading_system.sh

print_status "Creating environment variable template..."
cat > .env.template << 'EOF'
# Alpaca API Credentials
# Get these from https://alpaca.markets/
export APCA_API_KEY_ID="your_alpaca_key_here"
export APCA_API_SECRET_KEY="your_alpaca_secret_here"

# Optional: Override API base URL for paper/live trading
# Default is paper trading: https://paper-api.alpaca.markets
# export APCA_API_BASE_URL="https://paper-api.alpaca.markets"
EOF

print_status "Creating quick start guide..."
cat > QUICKSTART.md << 'EOF'
# Rich-on-Paper Trading Engine - Quick Start Guide

## Prerequisites Installed by Setup Script
- Ubuntu 22.04 or 24.04 (tested on both)
- Python 3.12 with virtual environment
- C++ compiler with C++23 support
- CMake 3.28.3+
- vcpkg package manager
- All required system libraries

## Getting Started

### 1. First Time Setup (Already Done!)
The setup script has already:
- Installed all system dependencies
- Set up vcpkg for C++ package management
- Created Python virtual environment
- Built the C++ trading engine
- Run all tests to verify installation

### 2. Configure Alpaca API Credentials

You need an Alpaca account for market data:
1. Sign up at https://alpaca.markets/
2. Get your API keys from the dashboard
3. Set environment variables:

```bash
# Option 1: Use the provided template
cp .env.template .env
# Edit .env with your credentials
source .env

# Option 2: Add to ~/.bashrc for persistence
echo "export APCA_API_KEY_ID='your_key_here'" >> ~/.bashrc
echo "export APCA_API_SECRET_KEY='your_secret_here'" >> ~/.bashrc
source ~/.bashrc
```

### 3. Run the Trading System

```bash
./run_trading_system.sh
```

This script will:
1. Start the Python market data publisher (binds to IPC socket)
2. Wait for initialization
3. Start the C++ trading engine (connects to publisher)
4. Monitor both processes
5. Cleanly shut down on Ctrl+C

### 4. Manual Operation (For Development)

If you prefer to run components separately:

**Terminal 1 - Python Publisher (start first!):**
```bash
source env/bin/activate
export APCA_API_KEY_ID='your_key'
export APCA_API_SECRET_KEY='your_secret'
python data-ingestion/src/publisher.py
```

**Terminal 2 - C++ Trading Engine:**
```bash
export APCA_API_KEY_ID='your_key'
export APCA_API_SECRET_KEY='your_secret'
./build/hello
```

### 5. Testing

**Run all tests:**
```bash
# Python tests
source env/bin/activate
pytest data-ingestion/src/ -v

# C++ tests
cd build && ctest --output-on-failure
```

### 6. Troubleshooting

**"Authentication failed" error:**
- Check your Alpaca API credentials
- Ensure you're using paper trading API keys

**"Cannot connect to IPC socket" error:**
- Make sure Python publisher is running FIRST
- Check `/tmp/market_data.sock` exists
- On WSL2, use TCP mode instead of IPC

**"Symbol not found" errors during build:**
- Re-run the setup script
- Check vcpkg installation: `$VCPKG_ROOT/vcpkg list`

### 7. Architecture Overview

```
Alpaca WebSocket → Python Publisher → ZMQ IPC → C++ Consumers → Trading Engine
                                                        ↓
                                                  Risk Manager
                                                        ↓
                                                  Order Gateway → Alpaca REST API
```

### 8. Next Steps

- Monitor the logs to see market data flowing
- Check for generated orders in the Alpaca paper trading dashboard
- Modify `SimpleMarketMakingStrategy` to implement your strategy
- Add more symbols in `src/main.cpp`

For more details, see the main README.md
EOF

print_status "============================================"
print_status "Setup completed successfully!"
print_status "============================================"
echo ""
echo "NEXT STEPS:"
echo ""
echo "1. Set up your Alpaca API credentials:"
echo "   - Copy .env.template to .env"
echo "   - Edit .env with your Alpaca API credentials"
echo "   - Source it: source .env"
echo ""
echo "   Or add to ~/.bashrc:"
echo "   export APCA_API_KEY_ID='your_key_here'"
echo "   export APCA_API_SECRET_KEY='your_secret_here'"
echo ""
echo "2. Run the trading system:"
echo "   ./run_trading_system.sh"
echo ""
echo "3. Monitor the output for successful market data reception"
echo ""
echo "TESTING:"
echo "- Run Python tests: pytest data-ingestion/src/"
echo "- Run C++ tests: cd build && ctest"
echo ""
echo "DEVELOPMENT:"
echo "- C++ binary: ./build/hello"
echo "- Python publisher: python data-ingestion/src/publisher.py"
echo "- Always start the Python publisher FIRST!"
echo ""
print_status "Happy trading!"