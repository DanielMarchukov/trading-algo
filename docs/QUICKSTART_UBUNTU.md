# Rich on Paper - Ubuntu/Linux Quick Start Guide

This guide will get you from zero to running the trading engine on Ubuntu/Linux in under 10 minutes.

## Prerequisites

- Ubuntu 20.04 or later (also works on Debian-based distributions)
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
chmod +x setup/setup_ubuntu.sh
./setup/setup_ubuntu.sh
```

This script will:

- Install system dependencies (CMake, Python 3.12, C++ libraries)
- Set up vcpkg package manager
- Create Python virtual environment
- Build the C++ trading engine
- Run tests to verify the installation

**Expected duration**: 5-10 minutes on first run (vcpkg needs to build dependencies)

## Step 3: Configure Alpaca API Credentials

1. Copy the environment template:

```bash
cp .env.template .env
```

2. Edit the file with your Alpaca credentials:

```bash
nano .env
```

3. Add your credentials (get these from <https://alpaca.markets/>):

```bash
export APCA_API_KEY_ID="your_actual_key_here"
export APCA_API_SECRET_KEY="your_actual_secret_here"
```

4. Load the environment variables:

```bash
source .env
```

## Step 4: Run the Trading System

Execute the run script:

```bash
./run_trading_system.sh
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

### "APCA_API_KEY_ID not set" Error

**Solution**: Ensure you've sourced the .env file:

```bash
source .env
```

### "Connection refused" or ZMQ Errors

**Solution**: The Python publisher may have failed to start. Check:

```bash
# Verify Python environment
source env/bin/activate
python --version  # Should show Python 3.12.x

# Test publisher directly
python data-ingestion/src/publisher.py
```

### Build Errors

**Solution**: Re-run setup with clean build:

```bash
rm -rf build/ vcpkg_installed/
./setup/setup_ubuntu.sh
```

### Permission Denied

**Solution**: Ensure scripts are executable:

```bash
chmod +x setup/*.sh
chmod +x run_trading_system.sh
```

## Performance Tuning (Optional)

For optimal performance on Linux:

1. **Disable CPU frequency scaling**:

```bash
sudo cpupower frequency-set -g performance
```

2. **Set real-time priority** (requires sudo):

```bash
sudo nice -n -20 ./run_trading_system.sh
```

3. **Monitor performance**:

```bash
# In another terminal
htop
# Press F4 to filter by process name: hello
```

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

## Support

For issues specific to this quickstart, check:

1. The setup script output for any warnings
1. System logs: `journalctl -f`
1. The main [README](../README.md) for more details
