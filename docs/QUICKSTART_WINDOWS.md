# Rich on Paper - Windows Quick Start Guide

This guide will get you from zero to running the trading engine on Windows in under 15 minutes.

## Prerequisites

- Windows 10/11 (64-bit)
- Internet connection
- Administrator privileges (for installations)
- Alpaca Markets account with API keys

## Step 1: Clone the Repository

Open PowerShell and run:

```powershell
git clone https://github.com/yourusername/rich-on-paper.git
cd rich-on-paper
```

## Step 2: Run the Setup Script

Open PowerShell **as Administrator** and run:

```powershell
Set-ExecutionPolicy Bypass -Scope Process -Force
.\setup\setup_windows.ps1
```

This script will:

- Install Chocolatey package manager
- Install Visual Studio 2022 Build Tools
- Install CMake, Ninja, and Git
- Set up vcpkg package manager
- Build the C++ trading engine
- Run tests to verify installation

**Expected duration**: 15-20 minutes on first run (Visual Studio tools take time)

## Step 3: Configure Alpaca API Credentials

1. Copy the environment template:

```powershell
Copy-Item setup\env.ps1.template setup\env.ps1
```

2. Edit the file with your Alpaca credentials:

```powershell
notepad setup\env.ps1
```

3. Update with your actual credentials (get these from <https://alpaca.markets/>):

```powershell
$env:APCA_API_KEY_ID = "your_actual_key_here"
$env:APCA_API_SECRET_KEY = "your_actual_secret_here"
```

4. Load the environment variables:

```powershell
. .\setup\env.ps1
```

## Step 4: Run the Trading System

**Option 1 - PowerShell** (Recommended):

```powershell
.\setup\run_trading_system.ps1
```

**Option 2 - Double-click**:

- Navigate to the `setup` folder in File Explorer
- Double-click `run_trading_system.bat`

You should see output like:

```bash
Starting trading engine...
FillListener started
MarketPublisher started
ZmqMarketEventSink: bound to tcp://127.0.0.1:5555
AlpacaWebSocketSource: pinned to CPU core 1
Pinned OrderGateway thread to CPU Core 0
```

## Step 5: Verify It's Working

You should see:

1. Market data being received (check the console output)
1. Orders being placed to Alpaca's paper trading API
1. No error messages
1. Windows Firewall may prompt — allow paper_money.exe

To stop the system, press `Ctrl+C`.

## Troubleshooting

### "APCA_API_KEY_ID not set" Error

**Solution**: Ensure you've loaded the env.ps1 file:

```powershell
. .\setup\env.ps1
```

### "vcvarsall.bat not found"

**Solution**: Visual Studio Build Tools didn't install correctly. Re-run:

```powershell
choco install visualstudio2022buildtools visualstudio2022-workload-vctools -y
```

### Build Errors

**Solution**: Clean and rebuild:

```powershell
Remove-Item -Recurse -Force build, vcpkg_installed -ErrorAction SilentlyContinue
.\setup\setup_windows.ps1
```

### "Permission Denied" Errors

**Solution**:

1. Run PowerShell as Administrator
1. Check Windows Defender exclusions for the project folder
1. Temporarily disable real-time scanning for the build folder

### DLL Not Found

**Solution**: We use static linking to avoid this, but if it occurs:

```powershell
choco install vcredist-all -y
```

## Windows-Specific Considerations

### TCP vs IPC

Windows doesn't support Unix domain sockets, so the system uses TCP:

- Default address: `tcp://127.0.0.1:5555`
- Slight latency increase vs Linux IPC (~10μs)

### Windows Firewall

When first running, Windows Firewall will prompt. You must:

- Allow `paper_money.exe` through the firewall

### Performance Monitoring

1. **Task Manager**:

   - Press `Ctrl+Shift+Esc`
   - Go to "Details" tab
   - Find `paper_money.exe`

1. **PowerShell Monitoring**:

```powershell
while ($true) {
    Get-Process paper_money -ErrorAction SilentlyContinue |
    Select-Object Name, CPU, WorkingSet, Id
    Start-Sleep -Seconds 1
    Clear-Host
}
```

3. **Performance Monitor** (Advanced):
   - Run `perfmon`
   - Add counters for the trading engine process

## Visual Studio Debugging

For development and debugging:

1. Open the solution:

```powershell
# The build process creates a .sln file
start build\RichOnPaper.sln
```

2. In Visual Studio:
   - Set `paper_money` as the startup project
   - Press `F5` to debug
   - Set breakpoints as needed

## Next Steps

- Review the logs to understand the trading flow
- Modify `src/SimpleMarketMakingStrategy.cpp` to implement your own strategy
- Check Alpaca dashboard for your paper trading activity
- Read [Architecture Documentation](ARCHITECTURE.md) to understand the system

## Manual Operation (For Developers)

```powershell
. .\setup\env.ps1
.\build\Debug\paper_money.exe
```

## Convenience Scripts

The setup creates these helper scripts in the `setup` folder:

- `run_trading_system.bat` - Double-click to start
- `run_tests.bat` - Run all tests
- `rebuild.bat` - Rebuild C++ code

## Performance Optimization

For best performance on Windows:

1. **Exclude from antivirus scanning**:

   - Add project folder to Windows Defender exclusions
   - Especially the `build` and `vcpkg_installed` folders

1. **Use Release build** (after initial testing):

```powershell
cmake --build build --config Release
# Then use .\build\Release\paper_money.exe
```

3. **Set process priority**:

```powershell
# In admin PowerShell after starting
$paper_moneyProcess = Get-Process paper_money
$paper_moneyProcess.PriorityClass = 'High'
```

## Support

For Windows-specific issues:

1. Check Event Viewer for application errors
1. Ensure all Visual C++ redistributables are installed
1. See the main [README](../README.md) for more details
