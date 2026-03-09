#!/bin/bash
#
# Usage:
#   chmod +x setup_macos.sh
#   ./setup_macos.sh                    # from root directory
#   ./setup/setup_macos.sh              # from root directory
#   cd setup && ./setup_macos.sh        # from setup directory
#
# After running this script, you can start the trading system with:
#   ./setup/run_trading_system.sh

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
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

print_skip() {
	echo -e "${CYAN}[SKIP]${NC} $1"
}

# Determine project root and setup directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$SCRIPT_DIR" == */setup ]]; then
	# Script is in setup directory
	PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
	SETUP_DIR="$SCRIPT_DIR"
	print_status "Running from setup directory: $SETUP_DIR"
else
	# Script is in project root or we're running it from root
	if [[ -f "$SCRIPT_DIR/CMakeLists.txt" ]]; then
		PROJECT_ROOT="$SCRIPT_DIR"
	else
		# We might be running ./setup/setup_macos.sh from root
		PROJECT_ROOT="$(pwd)"
		if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
			print_error "Cannot find project root. Please run from project root or setup directory."
			exit 1
		fi
	fi
	SETUP_DIR="$PROJECT_ROOT/setup"
	print_status "Running from project root: $PROJECT_ROOT"
fi

print_status "Project root: $PROJECT_ROOT"
print_status "Setup directory: $SETUP_DIR"

# Create setup directory if it doesn't exist
mkdir -p "$SETUP_DIR"

# Change to project root for all operations
cd "$PROJECT_ROOT"

# OS Check
if [[ "$OSTYPE" != "darwin"* ]]; then
	print_error "This script is designed for macOS. Detected: $OSTYPE"
	read -p "Continue anyway? (y/N) " -n 1 -r
	echo
	if [[ ! $REPLY =~ ^[Yy]$ ]]; then
		exit 1
	fi
fi

print_status "Starting Rich-on-Paper Trading Engine setup for macOS..."
print_status "This script is idempotent - safe to run multiple times!"

# Check for Xcode Command Line Tools
print_status "Checking for Xcode Command Line Tools..."
if ! xcode-select -p &>/dev/null; then
	print_status "Installing Xcode Command Line Tools..."
	xcode-select --install
	print_warning "Please complete the Xcode Command Line Tools installation in the popup window"
	print_warning "Then run this script again"
	exit 0
else
	print_skip "Xcode Command Line Tools already installed"
fi

# Check for Homebrew
print_status "Checking for Homebrew..."
if ! command -v brew &>/dev/null; then
	print_status "Installing Homebrew..."
	/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

	if [[ -f "/opt/homebrew/bin/brew" ]]; then
		echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >>~/.zprofile
		eval "$(/opt/homebrew/bin/brew shellenv)"
	fi
else
	print_skip "Homebrew already installed"
	if [ "$1" == "--upgrade" ]; then
		print_status "Upgrading Homebrew packages (--upgrade flag provided)..."
		brew update
		brew upgrade
	else
		print_skip "Skipping Homebrew upgrade (use --upgrade flag to upgrade)"
	fi
fi

# Install development tools via Homebrew
print_status "Checking and installing development tools..."
PACKAGES=(
	"cmake"
	"ninja"
	"git"
	"curl"
	"wget"
	"pkg-config"
	"llvm@20"
	"gcc"
)

for package in "${PACKAGES[@]}"; do
	if brew list "$package" &>/dev/null; then
		print_skip "$package already installed"
	else
		print_status "Installing $package..."
		brew install "$package"
	fi
done

# Install C++ library dependencies
print_status "Checking and installing C++ library dependencies..."
CPP_PACKAGES=(
	"boost"
	"zeromq"
	"openssl"
	"autoconf"
	"automake"
	"autoconf-archive"
	"libtool"
)

for package in "${CPP_PACKAGES[@]}"; do
	if brew list "$package" &>/dev/null; then
		print_skip "$package already installed"
	else
		print_status "Installing $package..."
		brew install "$package"
	fi
done

# Setup vcpkg
VCPKG_ROOT="$HOME/vcpkg"
print_status "Setting up vcpkg in $VCPKG_ROOT..."
if [ ! -d "$VCPKG_ROOT" ]; then
	git clone https://github.com/Microsoft/vcpkg.git "$VCPKG_ROOT"
	"$VCPKG_ROOT/bootstrap-vcpkg.sh"
else
	print_skip "vcpkg already installed, checking for updates..."
	cd "$VCPKG_ROOT"

	git fetch origin
	LOCAL=$(git rev-parse @)
	REMOTE=$(git rev-parse @{u})

	if [ "$LOCAL" != "$REMOTE" ]; then
		print_status "Updating vcpkg..."
		git pull
		"$VCPKG_ROOT/bootstrap-vcpkg.sh"
	else
		print_skip "vcpkg is already up to date"
	fi
	cd "$PROJECT_ROOT"
fi

# Set up shell profile
SHELL_PROFILE="$HOME/.zshrc"
if [ -n "$BASH_VERSION" ]; then
	SHELL_PROFILE="$HOME/.bash_profile"
fi

if ! grep -q "VCPKG_ROOT=$VCPKG_ROOT" "$SHELL_PROFILE"; then
	echo "export VCPKG_ROOT=$VCPKG_ROOT" >>"$SHELL_PROFILE"
	print_status "Added VCPKG_ROOT to $SHELL_PROFILE"
else
	print_skip "VCPKG_ROOT already in $SHELL_PROFILE"
fi

export VCPKG_ROOT="$VCPKG_ROOT"

# Setup compiler environment
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

# Handle existing build artifacts
if [ -d "build" ] || [ -d "vcpkg_installed" ]; then
	print_warning "Build artifacts found. Options:"
	echo "  1) Keep existing build (default)"
	echo "  2) Clean and rebuild"
	echo "  3) Reconfigure without cleaning vcpkg cache"
	read -p "Choose option [1-3]: " -n 1 -r
	echo

	case $REPLY in
	2)
		print_status "Cleaning all build artifacts..."
		rm -rf build/ vcpkg_installed/
		need_build=true
		;;
	3)
		print_status "Cleaning build directory only..."
		rm -rf build/
		need_build=true
		;;
	*)
		print_skip "Keeping existing build"
		need_build=false
		;;
	esac
else
	need_build=true
fi

# Build the project
if [ "$need_build" = true ]; then
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
else
	print_skip "Build directory exists - skipping build"
fi

# Run tests
if [ "$1" != "--skip-tests" ] && [ "$2" != "--skip-tests" ]; then
	print_status "Running tests to verify setup..."

	print_status "Running C++ tests..."
	if cd build && ctest --output-on-failure; then
		print_status "C++ tests passed!"
	else
		print_error "C++ tests failed - please check the errors above"
	fi
	cd "$PROJECT_ROOT"
else
	print_skip "Tests skipped (--skip-tests flag provided)"
fi

# Helper function to create files
create_file_if_needed() {
	local file_path="$1"
	local file_content="$2"
	local file_description="$3"

	if [ ! -f "$file_path" ]; then
		print_status "Creating $file_description..."
		echo "$file_content" >"$file_path"
		chmod +x "$file_path" 2>/dev/null || true
	else
		print_skip "$file_description already exists"
	fi
}

# Create run_trading_system.sh
print_status "Creating helper scripts in setup directory..."

create_run_script() {
	local script_path="$1"

	if [ ! -f "$script_path" ]; then
		print_status "Creating run script in setup directory..."
		cat >"$script_path" <<'EOF'
#!/bin/bash
#
# run_trading_system.sh - Start the Rich-on-Paper trading system
#
# Location: setup/run_trading_system.sh
# Usage from project root: ./setup/run_trading_system.sh

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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$SCRIPT_DIR" == */setup ]]; then
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
else
    PROJECT_ROOT="$SCRIPT_DIR"
fi

cd "$PROJECT_ROOT"

if [ ! -f "build/paper_money" ]; then
    print_error "C++ binary not found. Run setup/setup_macos.sh first."
    exit 1
fi

if [ -z "$APCA_API_KEY_ID" ] || [ -z "$APCA_API_SECRET_KEY" ]; then
    print_error "Alpaca API credentials not set!"
    echo "Please set the following environment variables:"
    echo "  export APCA_API_KEY_ID='your_key_here'"
    echo "  export APCA_API_SECRET_KEY='your_secret_here'"
    echo ""
    echo "You can add these to ~/.zshrc for persistence."
    echo "Or use the template: setup/.env.template"
    exit 1
fi

SOCKET_PATH="/tmp/market_data.sock"
if [ -S "$SOCKET_PATH" ]; then
    if lsof -U 2>/dev/null | grep -Fq "$SOCKET_PATH"; then
        print_error "Socket already in use by another instance: $SOCKET_PATH"
        exit 1
    fi
    print_status "Cleaning up stale socket file: $SOCKET_PATH"
    rm -f "$SOCKET_PATH"
fi

ENGINE_PID=""
CLEANUP_STARTED=false

cleanup() {
    if [ "$CLEANUP_STARTED" = true ]; then
        return 0
    fi
    CLEANUP_STARTED=true

    print_status "Shutting down trading system..."

    if [ ! -z "$ENGINE_PID" ] && kill -0 $ENGINE_PID 2>/dev/null; then
        print_status "Stopping trading engine (PID: $ENGINE_PID)..."
        kill -TERM $ENGINE_PID 2>/dev/null || true

        local count=0
        while [ $count -lt 50 ] && kill -0 $ENGINE_PID 2>/dev/null; do
            sleep 0.2
            count=$((count + 1))
        done

        if kill -0 $ENGINE_PID 2>/dev/null; then
            print_warning "Force killing trading engine after timeout..."
            kill -9 $ENGINE_PID 2>/dev/null || true
        fi
    fi

    if [ -S "$SOCKET_PATH" ]; then
        rm -f "$SOCKET_PATH" || true
    fi

    print_status "Cleanup complete"
}

trap cleanup EXIT

shutdown_requested=false
handle_signal() {
    if [ "$shutdown_requested" = false ]; then
        shutdown_requested=true
        print_status "Shutdown requested..."
        exit 0
    fi
}

trap handle_signal INT TERM

print_status "Rich-on-Paper Trading System Starting..."
print_status "Project root: $PROJECT_ROOT"

SOCKET_PATH="/tmp/market_data.sock"

./build/paper_money &
ENGINE_PID=$!

startup_timeout_s=30
elapsed=0
while [ $elapsed -lt $startup_timeout_s ]; do
    if [ -S "$SOCKET_PATH" ]; then
        break
    fi

    if ! kill -0 "$ENGINE_PID" 2>/dev/null; then
        print_error "Trading engine failed to start!"
        print_error "Check the error messages above and verify:"
        echo "  1. Alpaca API credentials are correct"
        echo "  2. Network connectivity is available"
        echo "  3. All dependencies are properly linked"
        exit 1
    fi

    sleep 1
    elapsed=$((elapsed + 1))
done

if [ ! -S "$SOCKET_PATH" ]; then
    print_error "Trading engine did not become ready within ${startup_timeout_s}s"
    kill "$ENGINE_PID" 2>/dev/null
    exit 1
fi

print_status "============================================"
print_status "Trading system is running successfully!"
print_status "============================================"
print_status "Engine PID: $ENGINE_PID"
print_status ""
print_status "Press Ctrl+C to stop the system gracefully."
print_status ""

while true; do
    sleep 1

    if [ "$shutdown_requested" = true ]; then
        break
    fi

    if ! kill -0 $ENGINE_PID 2>/dev/null; then
        print_error "Trading engine crashed! Check logs for details."
        break
    fi
done
EOF
		chmod +x "$script_path"
	else
		print_skip "run script already exists"
	fi
}

create_run_script "$SETUP_DIR/run_trading_system.sh"

# Create environment template
ENV_TEMPLATE_CONTENT='# Alpaca API Credentials
# Get these from https://alpaca.markets/
export APCA_API_KEY_ID="your_alpaca_key_here"
export APCA_API_SECRET_KEY="your_alpaca_secret_here"

# Optional: Override API base URL for paper/live trading
# Default is paper trading: https://paper-api.alpaca.markets
# export APCA_API_BASE_URL="https://paper-api.alpaca.markets"

# Usage:
# 1. Copy this file: cp setup/.env.template setup/.env
# 2. Edit setup/.env with your actual credentials
# 3. Source it: source setup/.env'

create_file_if_needed "$SETUP_DIR/.env.template" "$ENV_TEMPLATE_CONTENT" "environment variable template in setup directory"

# Create test script
TEST_SCRIPT_CONTENT='#!/bin/bash
#
# test.sh - Run all tests for the Rich-on-Paper trading system
#
# Location: setup/test.sh
# Usage from project root: ./setup/test.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$SCRIPT_DIR" == */setup ]]; then
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
else
    PROJECT_ROOT="$SCRIPT_DIR"
fi

cd "$PROJECT_ROOT"

echo "Running C++ tests..."
cd build
ctest --output-on-failure
cd ..

echo ""
echo "All tests completed!"'

create_file_if_needed "$SETUP_DIR/test.sh" "$TEST_SCRIPT_CONTENT" "test script in setup directory"

# Create rebuild script
REBUILD_SCRIPT_CONTENT='#!/bin/bash
#
# rebuild.sh - Rebuild the C++ trading engine
#
# Location: setup/rebuild.sh
# Usage from project root: ./setup/rebuild.sh

set -e

# Determine project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$SCRIPT_DIR" == */setup ]]; then
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
else
    PROJECT_ROOT="$SCRIPT_DIR"
fi

cd "$PROJECT_ROOT"

echo "Building C++ trading engine..."
cmake --build build

echo "Build completed!"'

create_file_if_needed "$SETUP_DIR/rebuild.sh" "$REBUILD_SCRIPT_CONTENT" "rebuild script in setup directory"

# Add helpful aliases
print_status "Adding helpful aliases..."
ALIAS_BLOCK='
# Rich-on-Paper Trading Engine aliases
alias trading-start="cd $PROJECT_ROOT && ./setup/run_trading_system.sh"
alias trading-test="cd $PROJECT_ROOT && ./setup/test.sh"
alias trading-build="cd $PROJECT_ROOT && ./setup/rebuild.sh"'

if ! grep -q "trading-start" "$SHELL_PROFILE" 2>/dev/null; then
	echo "$ALIAS_BLOCK" >>"$SHELL_PROFILE"
	print_status "Added trading aliases to $SHELL_PROFILE"
else
	print_skip "Trading aliases already in $SHELL_PROFILE"
fi

print_status "============================================"
print_status "Setup completed successfully!"
print_status "============================================"
echo ""
echo "Generated files are now organized in the setup directory:"
echo "- $SETUP_DIR/run_trading_system.sh  : Start the trading system"
echo "- $SETUP_DIR/.env.template          : Environment variable template"
echo "- $SETUP_DIR/test.sh                : Run all tests"
echo "- $SETUP_DIR/rebuild.sh             : Rebuild C++ code"
echo ""
echo "This setup script is idempotent and can be run safely multiple times."
echo ""
echo "NEXT STEPS:"
echo ""
echo "1. Set up your Alpaca API credentials:"
echo "   - Copy template: cp $SETUP_DIR/.env.template $SETUP_DIR/.env"
echo "   - Edit $SETUP_DIR/.env with your Alpaca API credentials"
echo "   - Source it: source $SETUP_DIR/.env"
echo ""
echo "   Or add to $SHELL_PROFILE:"
echo "   export APCA_API_KEY_ID='your_key_here'"
echo "   export APCA_API_SECRET_KEY='your_secret_here'"
echo ""
echo "2. Run the trading system:"
echo "   $SETUP_DIR/run_trading_system.sh"
echo ""
echo "3. Additional commands:"
echo "   - Run tests: $SETUP_DIR/test.sh"
echo "   - Rebuild: $SETUP_DIR/rebuild.sh"
echo ""
echo "OPTIONS for this script:"
echo "  --upgrade      : Also upgrade system packages"
echo "  --skip-tests   : Skip running tests"
echo ""
echo "4. macOS-specific notes:"
echo "   - If prompted, allow network connections for paper_money"
echo "   - Check Activity Monitor for CPU/memory usage"
echo "   - Logs are in standard output (no syslog integration)"
echo ""
echo "TESTING:"
echo "- Run all tests: trading-test (after restarting terminal)"
echo "- Build only: trading-build (after restarting terminal)"
echo ""
print_status "Happy trading on macOS!"

if [[ "$SHELL_PROFILE" == *"zshrc"* ]]; then
	print_warning "Remember to restart your terminal or run: source ~/.zshrc"
else
	print_warning "Remember to restart your terminal or run: source ~/.bash_profile"
fi
