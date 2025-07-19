# Rich-on-Paper Trading Engine - Windows Quick Start Guide

## Prerequisites Installed by Setup Script
- Visual Studio 2022 Build Tools with C++ workload
- Python 3.12 with virtual environment
- CMake 3.28.3+
- Ninja build system
- vcpkg package manager
- Git for Windows

## Windows-Specific Considerations

### 1. IPC vs TCP
- Windows doesn't support Unix domain sockets (IPC protocol)
- The system automatically uses TCP sockets instead
- Default: `tcp://127.0.0.1:5555`

### 2. Build System
- Uses Visual Studio 2022 compiler (MSVC)
- Static linking via `x64-windows-static` triplet
- Debug and Release configurations available

### 3. Process Management
- No POSIX signals - uses Windows process management
- CPU affinity works via Windows API (SetThreadAffinityMask)

## Getting Started

### 1. Configure Alpaca API Credentials

**Option 1 - PowerShell (Recommended):**
```powershell
# Copy and edit the template
Copy-Item .env.ps1.template env.ps1
notepad env.ps1

# Source the environment
. .\env.ps1
```

**Option 2 - Command Prompt:**
```batch
copy .env.windows.template env.bat
notepad env.bat
env.bat
```

**Option 3 - System Environment Variables (Permanent):**
1. Win + X â†’ System â†’ Advanced system settings
2. Environment Variables â†’ New
3. Add APCA_API_KEY_ID and APCA_API_SECRET_KEY

### 2. Run the Trading System

**Easy method (double-click):**
- Double-click `run_trading_system.bat`

**PowerShell method:**
```powershell
.\run_trading_system_windows.ps1
```

### 3. Manual Operation (For Development)

**Terminal 1 - Python Publisher:**
```powershell
.\env\Scripts\Activate.ps1
$env:APCA_API_KEY_ID = "your_key"
$env:APCA_API_SECRET_KEY = "your_secret"
python data-ingestion\src\publisher.py
```

**Terminal 2 - C++ Trading Engine:**
```powershell
$env:APCA_API_KEY_ID = "your_key"
$env:APCA_API_SECRET_KEY = "your_secret"
.\build\Debug\hello.exe
```

### 4. Troubleshooting Windows-Specific Issues

**"vcvarsall.bat not found":**
- Re-run the setup script
- Or install Visual Studio 2022 manually

**"Permission denied" errors:**
- Run PowerShell as Administrator
- Check Windows Defender/Antivirus exclusions

**"DLL not found" errors:**
- We use static linking to avoid this
- If it occurs, install Visual C++ Redistributables

**Performance monitoring:**
```powershell
# Task Manager or:
Get-Process hello | Select-Object CPU, WorkingSet, Id
```

### 5. Windows Firewall

When first running, Windows Firewall may prompt:
- Allow both Python.exe and hello.exe through firewall
- Required for ZMQ TCP communication

### 6. Development Tools

**Visual Studio debugging:**
1. Open `build\RichOnPaper.sln` in Visual Studio
2. Set hello as startup project
3. Press F5 to debug

**Windows Terminal (recommended):**
- Install from Microsoft Store for better console experience
- Supports multiple tabs for publisher/engine

### 7. Performance Considerations

**Windows-specific optimizations:**
- Disable Windows Defender real-time scanning for build folder
- Use Release build for production: `cmake --build build --config Release`
- Consider Windows Server for production deployment

### 8. Known Limitations

1. No Unix domain sockets - uses TCP (slight latency increase)
2. Process CPU affinity less granular than Linux
3. Requires Visual Studio runtime (bundled via static linking)

For more details, see the main README.md
