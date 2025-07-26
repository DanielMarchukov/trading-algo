#!/bin/bash
#
# Usage:
#   chmod +x setup_ubuntu.sh
#   ./setup_ubuntu.sh                    # from root directory
#   ./setup/setup_ubuntu.sh              # from root directory
#   cd setup && ./setup_ubuntu.sh        # from setup directory
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
		# We might be running ./setup/setup_ubuntu.sh from root
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

is_package_installed() {
	dpkg -l "$1" 2>/dev/null | grep -q "^ii"
}

get_package_version() {
	dpkg -l "$1" 2>/dev/null | grep "^ii" | awk '{print $3}'
}

# OS Check
if ! grep -q "Ubuntu" /etc/os-release; then
	print_error "This script is designed for Ubuntu. Detected: $(lsb_release -d 2>/dev/null || echo 'Unknown')"
	read -p "Continue anyway? (y/N) " -n 1 -r
	echo
	if [[ ! $REPLY =~ ^[Yy]$ ]]; then
		exit 1
	fi
fi

print_status "Starting Rich-on-Paper Trading Engine setup..."
print_status "This script is idempotent - safe to run multiple times!"

# Update package lists
if [ -f /var/lib/apt/lists/lock ]; then
	last_update=$(stat -c %Y /var/lib/apt/lists/lock)
	current_time=$(date +%s)
	time_diff=$((current_time - last_update))

	if [ $time_diff -gt 3600 ]; then
		print_status "Updating system packages (last update was >1 hour ago)..."
		sudo apt-get update -y
	else
		print_skip "Package list recently updated, skipping apt-get update"
	fi
else
	print_status "Updating system packages..."
	sudo apt-get update -y
fi

if [ "$1" == "--upgrade" ]; then
	print_status "Upgrading system packages (--upgrade flag provided)..."
	sudo apt-get upgrade -y
else
	print_skip "Skipping system upgrade (use --upgrade flag to upgrade)"
fi

# Install development tools
print_status "Checking and installing development tools..."
PACKAGES=(
	"build-essential"
	"cmake"
	"ninja-build"
	"git"
	"curl"
	"wget"
	"unzip"
	"tar"
	"pkg-config"
)

for package in "${PACKAGES[@]}"; do
	if is_package_installed "$package"; then
		print_skip "$package already installed ($(get_package_version $package))"
	else
		print_status "Installing $package..."
		sudo apt-get install -y "$package"
	fi
done

# Install Python 3.12
print_status "Checking Python installation..."
if command -v python3.12 &>/dev/null; then
	python_version=$(python3.12 --version 2>&1)
	print_skip "Python 3.12 already installed: $python_version"
else
	print_status "Installing Python 3.12..."
	if ! is_package_installed "software-properties-common"; then
		sudo apt-get install -y software-properties-common
	fi

	if ! grep -q "deadsnakes/ppa" /etc/apt/sources.list.d/*.list 2>/dev/null; then
		sudo add-apt-repository -y ppa:deadsnakes/ppa
		sudo apt-get update
	fi

	sudo apt-get install -y python3.12 python3.12-venv python3.12-dev
fi

# Install C++ library dependencies
print_status "Checking and installing C++ library dependencies..."
CPP_PACKAGES=(
	"libboost-all-dev"
	"libzmq3-dev"
	"libcurl4-openssl-dev"
	"libssl-dev"
	"autoconf"
	"automake"
	"autoconf-archive"
	"libtool"
	"linux-libc-dev"
)

for package in "${CPP_PACKAGES[@]}"; do
	if is_package_installed "$package"; then
		print_skip "$package already installed ($(get_package_version $package))"
	else
		print_status "Installing $package..."
		sudo apt-get install -y "$package"
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

if ! grep -q "VCPKG_ROOT=$VCPKG_ROOT" ~/.bashrc; then
	echo "export VCPKG_ROOT=$VCPKG_ROOT" >>~/.bashrc
	print_status "Added VCPKG_ROOT to ~/.bashrc"
else
	print_skip "VCPKG_ROOT already in ~/.bashrc"
fi

export VCPKG_ROOT="$VCPKG_ROOT"

# Setup Python virtual environment
print_status "Setting up project environment..."

if [ ! -d "env" ]; then
	print_status "Creating Python virtual environment..."
	python3.12 -m venv env
else
	print_skip "Python virtual environment already exists"
fi

print_status "Checking Python dependencies..."
source env/bin/activate

current_pip_version=$(pip --version | awk '{print $2}')
latest_pip_version=$(pip index versions pip 2>/dev/null | grep -oP 'Available versions: \K[0-9.]+' | head -1)

if [ "$current_pip_version" != "$latest_pip_version" ]; then
	print_status "Upgrading pip from $current_pip_version to $latest_pip_version..."
	pip install --upgrade pip
else
	print_skip "pip is already up to date ($current_pip_version)"
fi

if [ -f "data-ingestion/requirements.txt" ]; then
	req_hash=$(sha256sum data-ingestion/requirements.txt | awk '{print $1}')
	hash_file=".requirements.hash"

	if [ -f "$hash_file" ] && [ "$(cat $hash_file)" == "$req_hash" ]; then
		print_skip "Python dependencies are up to date"
	else
		print_status "Installing/updating Python dependencies..."
		pip install -r data-ingestion/requirements.txt
		echo "$req_hash" >"$hash_file"
	fi
else
	print_error "requirements.txt not found!"
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
		-G Ninja

	print_status "Building C++ trading engine..."
	cmake --build build
else
	print_skip "Build directory exists - skipping build"
fi

# Run tests
if [ "$1" != "--skip-tests" ]; then
	print_status "Running tests to verify setup..."

	print_status "Running Python tests..."
	if pytest data-ingestion/src/ -v; then
		print_status "Python tests passed!"
	else
		print_warning "Some Python tests failed - this is expected if API keys are not set"
	fi

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

# Create run_trading_system.sh with proper signal handling
print_status "Creating helper scripts in setup directory..."

# Create run_trading_system.sh with proper signal handling
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
# This script starts both the Python data publisher and C++ trading engine
# in the correct order with proper error handling and signal management.
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

# Determine project root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$SCRIPT_DIR" == */setup ]]; then
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
else
    PROJECT_ROOT="$SCRIPT_DIR"
fi

# Change to project root
cd "$PROJECT_ROOT"

# Check prerequisites
if [ ! -d "env" ]; then
    print_error "Python virtual environment not found. Run setup/setup_ubuntu.sh first."
    exit 1
fi

if [ ! -d "build" ] || [ ! -f "build/paper_money" ]; then
    print_error "C++ binary not found. Run setup/setup_ubuntu.sh first."
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
    echo "Or use the template: setup/.env.template"
    exit 1
fi

# Clean up any existing socket files
TEMP_DIR=$(python3 -c "import tempfile; print(tempfile.gettempdir())")
SOCKET_PATH="$TEMP_DIR/market_data.sock"
if [ -S "$SOCKET_PATH" ]; then
    print_status "Cleaning up existing socket file: $SOCKET_PATH"
    rm -f "$SOCKET_PATH"
fi

# Global variables for process tracking
PUBLISHER_PID=""
ENGINE_PID=""
CLEANUP_STARTED=false

# Cleanup function with proper signal handling
cleanup() {
    # Prevent multiple cleanup calls (race condition protection)
    if [ "$CLEANUP_STARTED" = true ]; then
        return 0
    fi
    CLEANUP_STARTED=true

    print_status "Shutting down trading system..."

    # Stop Python publisher if running
    if [ ! -z "$PUBLISHER_PID" ] && kill -0 $PUBLISHER_PID 2>/dev/null; then
        print_status "Stopping Python publisher (PID: $PUBLISHER_PID)..."
        kill -TERM $PUBLISHER_PID 2>/dev/null || true
        
        # Wait for graceful shutdown (up to 5 seconds)
        local count=0
        while [ $count -lt 25 ] && kill -0 $PUBLISHER_PID 2>/dev/null; do
            sleep 0.2
            count=$((count + 1))
        done
        
        # Force kill if still running
        if kill -0 $PUBLISHER_PID 2>/dev/null; then
            print_warning "Force killing Python publisher after timeout..."
            kill -9 $PUBLISHER_PID 2>/dev/null || true
        fi
    fi

    # Stop C++ engine if running  
    if [ ! -z "$ENGINE_PID" ] && kill -0 $ENGINE_PID 2>/dev/null; then
        print_status "Stopping C++ trading engine (PID: $ENGINE_PID)..."
        kill -TERM $ENGINE_PID 2>/dev/null || true
        
        # Wait for graceful shutdown (up to 10 seconds)
        local count=0
        while [ $count -lt 50 ] && kill -0 $ENGINE_PID 2>/dev/null; do
            sleep 0.2
            count=$((count + 1))
        done
        
        # Force kill if still running
        if kill -0 $ENGINE_PID 2>/dev/null; then
            print_warning "Force killing C++ trading engine after timeout..."
            kill -9 $ENGINE_PID 2>/dev/null || true
        fi
    fi

    # Clean up socket file
    if [ -S "$SOCKET_PATH" ]; then
        print_status "Cleaning up socket file: $SOCKET_PATH"
        rm -f "$SOCKET_PATH" || true
    fi

    print_status "Cleanup complete"
}

# Set trap for cleanup - only on EXIT to avoid double handling
trap cleanup EXIT

# Custom signal handler for SIGINT/SIGTERM that just sets a flag
shutdown_requested=false
handle_signal() {
    if [ "$shutdown_requested" = false ]; then
        shutdown_requested=true
        print_status "Shutdown requested..."
        exit 0  # This will trigger the EXIT trap
    fi
}

# Set traps for signals
trap handle_signal INT TERM

print_status "Rich-on-Paper Trading System Starting..."
print_status "Project root: $PROJECT_ROOT"
print_status "Socket path: $SOCKET_PATH"

# Start Python publisher
print_status "Starting Python market data publisher..."
source env/bin/activate
python data-ingestion/src/publisher.py &
PUBLISHER_PID=$!

# Wait for publisher to initialize and bind to socket
print_status "Waiting for publisher to initialize..."
sleep 4

# Check if publisher is still running
if ! kill -0 $PUBLISHER_PID 2>/dev/null; then
    print_error "Python publisher failed to start!"
    print_error "Check the error messages above and verify:"
    echo "  1. Alpaca API credentials are correct"
    echo "  2. Network connectivity is available"
    echo "  3. No permission issues with socket creation"
    exit 1
fi

# Verify socket was created
if [ ! -S "$SOCKET_PATH" ]; then
    print_warning "Socket file not found at expected location: $SOCKET_PATH"
    print_warning "Publisher may still be starting up..."
    sleep 2
fi

# Start C++ trading engine
print_status "Starting C++ trading engine..."
./build/paper_money &
ENGINE_PID=$!

# Wait a moment to check if engine started successfully
sleep 3

# Check if engine is still running
if ! kill -0 $ENGINE_PID 2>/dev/null; then
    print_error "C++ trading engine failed to start!"
    print_error "Check the error messages above and verify:"
    echo "  1. ZMQ socket is available"
    echo "  2. No port conflicts"
    echo "  3. All dependencies are properly linked"
    exit 1
fi

print_status "============================================"
print_status "Trading system is running successfully!"
print_status "============================================"
print_status "Publisher PID: $PUBLISHER_PID"
print_status "Engine PID: $ENGINE_PID"
print_status "Socket: $SOCKET_PATH"
print_status ""
print_status "Monitor the output for market data and trading activity."
print_status "Press Ctrl+C to stop the system gracefully."
print_status ""

# Main monitoring loop
while true; do
    sleep 1

    # Check if shutdown was requested
    if [ "$shutdown_requested" = true ]; then
        break
    fi

    # Check if processes are still running
    if ! kill -0 $PUBLISHER_PID 2>/dev/null; then
        print_error "Python publisher crashed! Check logs for details."
        break
    fi

    if ! kill -0 $ENGINE_PID 2>/dev/null; then
        print_error "C++ trading engine crashed! Check logs for details."
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

# Determine project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$SCRIPT_DIR" == */setup ]]; then
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
else
    PROJECT_ROOT="$SCRIPT_DIR"
fi

cd "$PROJECT_ROOT"

echo "Running Python tests..."
source env/bin/activate
pytest data-ingestion/src/ -v

echo ""
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
echo "   Or add to ~/.bashrc:"
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
print_status "Happy trading!"
