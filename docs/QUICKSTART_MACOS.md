# Rich on Paper - macOS Quick Start Guide

This guide will get you from zero to running the trading engine on macOS in under 10 minutes.

## Prerequisites

- macOS 13 (Ventura) or later
- Internet connection
- Alpaca Markets account with API keys

## Step 1: Clone the Repository

```bash
git clone https://github.com/yourusername/rich-on-paper.git
cd rich-on-paper
```

## Step 2: Run the Setup Script

The setup script will install all dependencies and build the project:

```bash
chmod +x setup/setup_macos.sh
./setup/setup_macos.sh
```

This script will:

- Install Xcode Command Line Tools (if needed)
- Install Homebrew (if needed)
- Install all required packages via Homebrew
- Set up vcpkg package manager
- Configure LLVM/Clang compiler
- Build the C++ trading engine
- Run tests to verify installation

**Expected duration**: 10-15 minutes on first run (Homebrew and vcpkg installations)

## Step 3: Configure Alpaca API Credentials

1. Copy the environment template:

```bash
cp .env.template .env
```

1. Edit the file with your Alpaca credentials:

```bash
nano .env
```

1. Add your credentials (get these from <https://alpaca.markets/>):

```bash
export APCA_API_KEY_ID="your_actual_key_here"
export APCA_API_SECRET_KEY="your_actual_secret_here"
```

1. Load the environment variables:

```bash
source .env
```

## Step 4: Run the Trading System

Execute the run script:

```bash
./run_trading_system_macos.sh
```

You should see output like:

```bash
[2024-01-15 10:30:45] Starting Python market data publisher...
[2024-01-15 10:30:48] Starting C++ trading engine...
[2024-01-15 10:30:49] Trading system is running!
[2024-01-15 10:30:49] Publisher PID: 12345
[2024-01-15 10:30:49] Engine PID: 12346
[2024-01-15 10:30:49] Press Ctrl+C to stop...
```

## Step 5: Verify It's Working

You should see:

1. Market data being received (check the console output)
1. Orders being placed to Alpaca's paper trading API
1. No error messages

To stop the system, press `Ctrl+C`.

## Troubleshooting

### "Developer Cannot Be Verified" Error

**Solution**: Go to System Settings → Privacy & Security and click "Allow Anyway" for the blocked app. Alternatively:

```bash
xattr -cr ./build/hello
```

### "APCA_API_KEY_ID not set" Error

**Solution**: Ensure you've sourced the .env file:

```bash
source .env
```

### Compiler Not Found

**Solution**: The setup script should have configured LLVM, but if issues persist:

```bash
brew reinstall llvm
# For Apple Silicon
export CC=/opt/homebrew/opt/llvm/bin/clang
export CXX=/opt/homebrew/opt/llvm/bin/clang++
# For Intel
export CC=/usr/local/opt/llvm/bin/clang
export CXX=/usr/local/opt/llvm/bin/clang++
```

### Build Errors

**Solution**: Re-run setup with clean build:

```bash
rm -rf build/ vcpkg_installed/
./setup/setup_macos.sh
```

### Library Loading Errors

**Solution**: Fix dynamic library paths:

```bash
brew upgrade boost zeromq
brew link --force boost
```

## Performance Monitoring

1. **Using Activity Monitor**:
    - Open Activity Monitor
    - Search for "hello" process
    - Monitor CPU and memory usage

2. **Using Command Line**:

```bash
# Monitor the trading engine
top -pid $(pgrep hello)
```

3. **Using Instruments** (Advanced):

```bash
# Open Instruments for profiling
open /Applications/Xcode.app/Contents/Applications/Instruments.app
```

## macOS-Specific Notes

1. **Firewall**: macOS will prompt to allow network connections for both Python and the trading engine. Click "Allow"
for both
1. **CPU Affinity**: macOS handles thread affinity differently than Linux. The engine will still attempt to pin
threads but with macOS-specific APIs.
1. **IPC Sockets**: The engine uses Unix domain sockets at `/tmp/market_data.sock`. This is allowed by default on macOS.

## Next Steps

- Review the logs to understand the trading flow
- Modify `src/SimpleMarketMakingStrategy.cpp` to implement your own strategy
- Check Alpaca dashboard for your paper trading activity
- Read [Architecture Documentation](ARCHITECTURE.md) to understand the system

## Manual Operation (For Developers)

If you prefer to run components separately:

**Terminal 1 - Python Publisher**:

```bash
source env/bin/activate
source .env
python data-ingestion/src/publisher.py
```

**Terminal 2 - C++ Trading Engine**:

```bash
source .env
./build/hello
```

## Debugging with LLDB

For debugging the C++ engine:

```bash
lldb ./build/hello
(lldb) run
(lldb) bt  # backtrace if it crashes
```

## Convenience Aliases

After running the setup script, you'll have these aliases available (restart terminal first):

- `trading-start` - Start the trading system
- `trading-test` - Run all tests
- `trading-build` - Rebuild the C++ engine

## Support

For issues specific to this quickstart:

1. Check the setup script output for warnings
2. Review Console.app for system logs
3. See the main [README](../README.md) for more details
