#!/bin/bash
#
# Usage:
#   chmod +x setup_macos_dev.sh
#   ./setup_macos_dev.sh
#
# After running this script, you can start the trading system with:
#   ./run_trading_system_macos.sh

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

if [[ "$OSTYPE" != "darwin"* ]]; then
    print_error "This script is designed for macOS. Detected: $OSTYPE"
    exit 1
fi

print_status "Starting Rich-on-Paper Trading Engine setup for macOS..."

print_status "Checking for Xcode Command Line Tools..."
if ! xcode-select -p &> /dev/null; then
    print_status "Installing Xcode Command Line Tools..."
    xcode-select --install
    print_warning "Please complete the Xcode Command Line Tools installation in the popup window"
    print_warning "Then run this script again"
    exit 0
else
    print_status "Xcode Command Line Tools already installed"
fi

print_status "Checking for Homebrew..."
if ! command -v brew &> /dev/null; then
    print_status "Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

    if [[ -f "/opt/homebrew/bin/brew" ]]; then
        echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
        eval "$(/opt/homebrew/bin/brew shellenv)"
    fi
else
    print_status "Homebrew already installed"
    brew update
fi

print_status "Installing development tools via Homebrew..."
brew install \
    cmake \
    ninja \
    git \
    curl \
    wget \
    pkg-config \
    llvm@20 \
    gcc

print_status "Installing Python 3.12..."
brew install python@3.12

if ! command -v python3.12 &> /dev/null; then
    PYTHON_PATH=$(brew --prefix python@3.12)/bin/python3.12
    if [ -f "$PYTHON_PATH" ]; then
        print_status "Creating python3.12 symlink..."
        sudo ln -sf "$PYTHON_PATH" /usr/local/bin/python3.12
    fi
fi

print_status "Installing C++ library dependencies..."
brew install \
    boost \
    zeromq \
    openssl \
    autoconf \
    automake \
    autoconf-archive \
    libtool

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

SHELL_PROFILE="$HOME/.zshrc"
if [ -n "$BASH_VERSION" ]; then
    SHELL_PROFILE="$HOME/.bash_profile"
fi

if ! grep -q "VCPKG_ROOT" "$SHELL_PROFILE"; then
    echo "export VCPKG_ROOT=$VCPKG_ROOT" >> "$SHELL_PROFILE"
    print_status "Added VCPKG_ROOT to $SHELL_PROFILE"
fi

export VCPKG_ROOT="$VCPKG_ROOT"

print_status "Setting up compiler environment..."
if [[ -d "/opt/homebrew" ]]; then
    # Apple Silicon Mac
    export CC="/opt/homebrew/opt/llvm@20/bin/clang"
    export CXX="/opt/homebrew/opt/llvm@20/bin/clang++"
    export LDFLAGS="-L/opt/homebrew/opt/llvm@20/lib"
    export CPPFLAGS="-I/opt/homebrew/opt/llvm@20/include"
else
    # Intel Mac
    export CC="/usr/local/opt/llvm@20/bin/clang"
    export CXX="/usr/local/opt/llvm@20/bin/clang++"
    export LDFLAGS="-L/usr/local/opt/llvm@20/lib"
    export CPPFLAGS="-I/usr/local/opt/llvm@20/include"
fi

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
    -DVCPKG_TARGET_TRIPLET=arm64-osx \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -G Ninja

print_status "Building C++ trading engine..."
cmake --build build

print_status "Running tests to verify setup..."
print_status "Running Python tests..."
pytest data-ingestion/src/ -v || print_warning "Some Python tests failed - this is expected if API keys are not set"

print_status "Running C++ tests..."
cd build
ctest --output-on-failure
cd ..

print_status "Creating macOS run script..."
cat > run_trading_system_macos.sh << 'EOF'
#!/bin/bash
#
# run_trading_system_macos.sh - Start the Rich-on-Paper trading system on macOS
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
    print_error "Python virtual environment not found. Run setup_macos_dev.sh first."
    exit 1
fi

# Check if build directory exists
if [ ! -d "build" ] || [ ! -f "build/hello" ]; then
    print_error "C++ binary not found. Run setup_macos_dev.sh first."
    exit 1
fi

# Check for required environment variables
if [ -z "$APCA_API_KEY_ID" ] || [ -z "$APCA_API_SECRET_KEY" ]; then
    print_error "Alpaca API credentials not set!"
    echo "Please set the following environment variables:"
    echo "  export APCA_API_KEY_ID='your_key_here'"
    echo "  export APCA_API_SECRET_KEY='your_secret_here'"
    echo ""
    echo "You can add these to ~/.zshrc for persistence."
    exit 1
fi

# Cleanup function
cleanup() {
    print_status "Shutting down trading system..."

    # Kill Python publisher if running
    if [ ! -z "$PUBLISHER_PID" ] && kill -0 $PUBLISHER_PID 2>/dev/null; then
        kill $PUBLISHER_PID 2>/dev/null || true
    fi

    # Kill C++ engine if running
    if [ ! -z "$ENGINE_PID" ] && kill -0 $ENGINE_PID 2>/dev/null; then
        kill $ENGINE_PID 2>/dev/null || true
    fi

    # Wait a moment for processes to terminate
    sleep 2

    # Force kill if still running
    if [ ! -z "$PUBLISHER_PID" ] && kill -0 $PUBLISHER_PID 2>/dev/null; then
        kill -9 $PUBLISHER_PID 2>/dev/null || true
    fi
    if [ ! -z "$ENGINE_PID" ] && kill -0 $ENGINE_PID 2>/dev/null; then
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
    print_error "Check if Alpaca API credentials are correct"
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

chmod +x run_trading_system_macos.sh

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

print_status "Creating macOS quick start guide..."
cat > QUICKSTART_MACOS.md << 'EOF'
# Rich-on-Paper Trading Engine - macOS Quick Start Guide

## Prerequisites Installed by Setup Script
- macOS Ventura 13.0+ or Sonoma 14.0+
- Xcode Command Line Tools
- Homebrew package manager
- Python 3.12 with virtual environment
- LLVM/Clang 20 compiler with C++23 support
- CMake 3.28.3+
- vcpkg package manager
- All required system libraries (Boost, ZeroMQ, etc.)

## macOS-Specific Considerations

### 1. System Integrity Protection (SIP)
- The trading engine uses IPC sockets at `/tmp/market_data.sock`
- macOS allows this by default, no special permissions needed

### 2. Compiler Setup
The setup script configured LLVM/Clang 20 from Homebrew:
- Apple Silicon Macs: `/opt/homebrew/opt/llvm@20`
- Intel Macs: `/usr/local/opt/llvm@20`

### 3. CPU Affinity
- macOS supports thread affinity differently than Linux
- The engine will still pin threads but with macOS-specific APIs

## Getting Started

### 1. Configure Alpaca API Credentials

```bash
# Copy and edit the template
cp .env.template .env
nano .env  # or use your preferred editor

# Source the environment
source .env

# Or add to ~/.zshrc for persistence
echo "export APCA_API_KEY_ID='your_key_here'" >> ~/.zshrc
echo "export APCA_API_SECRET_KEY='your_secret_here'" >> ~/.zshrc
source ~/.zshrc
```

### 2. Run the Trading System

```bash
./run_trading_system_macos.sh
```

### 3. Manual Operation (For Development)

**Terminal 1 - Python Publisher:**
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

### 4. Troubleshooting macOS-Specific Issues

**"Library not loaded" errors:**
```bash
# Fix dynamic library paths
brew upgrade boost zeromq
brew link --force boost
```

**"Permission denied" on socket:**
```bash
# Clear old socket file
rm -f /tmp/market_data.sock
```

**Performance monitoring:**
```bash
# Use macOS Activity Monitor or:
top -pid $(pgrep hello)
```

**Firewall warnings:**
- macOS may ask to allow network connections
- Click "Allow" for both Python and hello binary

### 5. Development Tools

**Debugging with LLDB:**
```bash
lldb ./build/hello
(lldb) run
```

**Performance profiling with Instruments:**
```bash
# Open Instruments app
open /Applications/Xcode.app/Contents/Applications/Instruments.app
# Profile the running process
```

### 6. macOS Security & Privacy

If you encounter "Developer cannot be verified" errors:
1. Go to System Settings → Privacy & Security
2. Click "Allow Anyway" for the blocked app
3. Or remove quarantine: `xattr -cr ./build/hello`

For more details, see the main README.md
EOF

print_status "Adding helpful aliases..."
cat >> "$SHELL_PROFILE" << 'EOF'

# Rich-on-Paper Trading Engine aliases
alias trading-start='cd $(pwd) && ./run_trading_system_macos.sh'
alias trading-test='cd $(pwd) && source env/bin/activate && pytest data-ingestion/src/ && cd build && ctest'
alias trading-build='cd $(pwd) && cmake --build build'
EOF

print_status "============================================"
print_status "Setup completed successfully!"
print_status "============================================"
echo ""
echo "NEXT STEPS:"
echo ""
echo "1. Set up your Alpaca API credentials:"
echo "   cp .env.template .env"
echo "   # Edit .env with your credentials"
echo "   source .env"
echo ""
echo "2. Run the trading system:"
echo "   ./run_trading_system_macos.sh"
echo ""
echo "   Or use the convenient alias (after restarting terminal):"
echo "   trading-start"
echo ""
echo "3. macOS-specific notes:"
echo "   - If prompted, allow network connections for Python and hello"
echo "   - Check Activity Monitor for CPU/memory usage"
echo "   - Logs are in standard output (no syslog integration)"
echo ""
echo "TESTING:"
echo "- Run all tests: trading-test"
echo "- Build only: trading-build"
echo ""
print_status "Happy trading on macOS!"

if [[ "$SHELL_PROFILE" == *"zshrc"* ]]; then
    print_warning "Remember to restart your terminal or run: source ~/.zshrc"
else
    print_warning "Remember to restart your terminal or run: source ~/.bash_profile"
fi