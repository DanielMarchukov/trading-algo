#!/bin/bash
#
# Setup script for the RichOnPaper Trading Engine development environment.
# This script installs all necessary system packages, C++ and Python
# dependencies, and builds the project.
#
# USAGE:
# 1. Place this script in the root directory of the project.
# 2. Make it executable: chmod +x setup_dev.sh
# 3. Run it: ./setup_dev.sh
set -e

echo "--- Installing system prerequisites (requires sudo)... ---"
sudo apt-get update -y
sudo apt-get upgrade -y
sudo apt-get install -y \
	pkg-config \
	libboost-all-dev \
	libquantlib0-dev \
	libquantlib0v5 \
	curl \
	libcurl4-openssl-dev \
	libssl-dev \
	libzmq3-dev \
	lcov \
	llvm \
	autoconf \
	automake \
	autoconf-archive \
	linux-libc-dev

echo "--- System prerequisites installed successfully. ---"
echo ""

VCPKG_ROOT=~/vcpkg
echo "--- Setting up vcpkg in ${VCPKG_ROOT}... ---"
if [ ! -d "$VCPKG_ROOT" ]; then
	echo "Cloning vcpkg repository..."
	git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
else
	echo "vcpkg directory already exists. Skipping clone."
fi
"$VCPKG_ROOT/bootstrap-vcpkg.sh"
echo "--- vcpkg setup complete. ---"
echo ""
echo "NOTE: For convenience, you should add 'export VCPKG_ROOT=${VCPKG_ROOT}' to your ~/.bashrc or ~/.zshrc file."
echo ""
echo "--- Setting up Python virtual environment... ---"
if [ ! -d "env" ]; then
	echo "Creating Python virtual environment..."
	python3 -m venv env
else
	echo "Python virtual environment 'env' already exists."
fi

source env/bin/activate

echo "Installing Python dependencies from requirements.txt..."
pip install -r data-ingestion/requirements.txt
echo "--- Python environment setup complete. ---"
echo ""

echo "--- Configuring and building the C++ engine... ---"
echo "Cleaning old build directory..."
rm -rf build

# Configure the project using CMake, Ninja, and the vcpkg toolchain.
echo "Configuring CMake with Ninja generator and vcpkg toolchain..."
cmake -G "Ninja" -B build -S . -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"

echo "Building project with CMake..."
cmake --build build

echo "--- C++ build complete. ---"
echo ""
echo "--- Running unit tests to verify setup... ---"
echo "Running Python tests..."
pytest

echo "Running C++ tests..."
ctest --test-dir build --output-on-failure

echo ""
echo "--- Development environment setup complete! ---"
echo "You can now run the publisher and consumer in separate terminals."
