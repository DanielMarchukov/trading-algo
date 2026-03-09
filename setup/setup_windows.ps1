# Usage:
#   1. Open PowerShell as Administrator
#   2. cd to project root
#   3. Run: .\setup\setup_windows.ps1
#
# After setup completes, run the trading system with:
#   .\setup\run_trading_system.ps1

# Ensure we're running from the project root (parent of setup folder)
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptPath
Push-Location $projectRoot

# Requires Administrator privileges for some installations
if (-NOT ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    Write-Host "This script requires Administrator privileges. Please run PowerShell as Administrator." -ForegroundColor Red
    Write-Host "Right-click on PowerShell and select 'Run as Administrator'" -ForegroundColor Yellow
    Pop-Location
    exit 1
}

# Set up error handling
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

# Color functions for output
function Write-Status {
    param($Message)
    Write-Host "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] " -ForegroundColor Green -NoNewline
    Write-Host $Message
}

function Write-Error-Message {
    param($Message)
    Write-Host "[ERROR] " -ForegroundColor Red -NoNewline
    Write-Host $Message
}

function Write-Warning-Message {
    param($Message)
    Write-Host "[WARNING] " -ForegroundColor Yellow -NoNewline
    Write-Host $Message
}

function Write-Skip {
    param($Message)
    Write-Host "[SKIP] " -ForegroundColor Cyan -NoNewline
    Write-Host $Message
}

# Helper function to check if a command exists
function Test-CommandExists {
    param($Command)
    $null = Get-Command $Command -ErrorAction SilentlyContinue
    return $?
}

Write-Status "Starting Rich-on-Paper Trading Engine setup for Windows..."
Write-Status "Running from: $projectRoot"

# Check Windows version
$os = Get-CimInstance Win32_OperatingSystem
$version = [version]$os.Version
if ($version.Major -lt 10) {
    Write-Error-Message "This script requires Windows 10 or later. Detected: Windows $($version.Major)"
    Pop-Location
    exit 1
}

# Install Chocolatey if not present
if (Test-CommandExists "choco") {
    Write-Skip "Chocolatey already installed"
    choco upgrade chocolatey -y --no-progress | Out-Null
} else {
    Write-Status "Installing Chocolatey..."
    Set-ExecutionPolicy Bypass -Scope Process -Force
    [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
    Invoke-Expression ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

    # Refresh environment
    $env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
}

# Function to install or skip Chocolatey packages
function Install-ChocoPackage {
    param($PackageName, $AdditionalArgs = "")

    $installed = choco list --local-only | Select-String -Pattern "^$PackageName\s+"
    if ($installed) {
        Write-Skip "$PackageName already installed"
    } else {
        Write-Status "Installing $PackageName..."
        if ($AdditionalArgs) {
            choco install -y $PackageName $AdditionalArgs --no-progress
        } else {
            choco install -y $PackageName --no-progress
        }
    }
}

# Install development tools via Chocolatey
Install-ChocoPackage "git"
Install-ChocoPackage "cmake" "--installargs 'ADD_CMAKE_TO_PATH=System'"
Install-ChocoPackage "ninja"

# Check for Visual Studio Build Tools
$vsInstalled = $false
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) {
        Write-Skip "Visual Studio Build Tools already installed at: $vsPath"
        $vsInstalled = $true
    }
}

if (-not $vsInstalled) {
    Install-ChocoPackage "visualstudio2022buildtools"
    Install-ChocoPackage "visualstudio2022-workload-vctools"
}

# Refresh environment variables
Write-Status "Refreshing environment variables..."
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")

# Install vcpkg
$vcpkgRoot = "$env:USERPROFILE\vcpkg"
if (Test-Path $vcpkgRoot) {
    Write-Skip "vcpkg already installed at $vcpkgRoot"
    Write-Status "Updating vcpkg..."
    Push-Location $vcpkgRoot
    git pull
    & ".\bootstrap-vcpkg.bat" -disableMetrics
    Pop-Location
} else {
    Write-Status "Installing vcpkg in $vcpkgRoot..."
    git clone https://github.com/Microsoft/vcpkg.git $vcpkgRoot
    & "$vcpkgRoot\bootstrap-vcpkg.bat" -disableMetrics
}

# Set VCPKG_ROOT environment variable if not set
$currentVcpkgRoot = [System.Environment]::GetEnvironmentVariable("VCPKG_ROOT", [System.EnvironmentVariableTarget]::User)
if ($currentVcpkgRoot -ne $vcpkgRoot) {
    Write-Status "Setting VCPKG_ROOT environment variable..."
    [System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, [System.EnvironmentVariableTarget]::User)
}
$env:VCPKG_ROOT = $vcpkgRoot

# Verify we're in the project directory
if (!(Test-Path "CMakeLists.txt")) {
    Write-Error-Message "CMakeLists.txt not found. Please run this script from the project root directory."
    Pop-Location
    exit 1
}

# Handle build directory
$buildExists = Test-Path "build"
$vcpkgExists = Test-Path "vcpkg_installed"

if ($buildExists -or $vcpkgExists) {
    Write-Warning-Message "Previous build artifacts found"
    $response = Read-Host "Do you want to clean and rebuild? (y/N)"
    if ($response -eq 'y' -or $response -eq 'Y') {
        Write-Status "Cleaning previous build artifacts..."
        if (Test-Path "build") { Remove-Item -Recurse -Force build }
        if (Test-Path "vcpkg_installed") { Remove-Item -Recurse -Force vcpkg_installed }
        $buildExists = $false
    }
}

# Configure and build if needed
if (-not $buildExists) {
    # Find Visual Studio installation
    Write-Status "Locating Visual Studio installation..."
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsPath) {
            $vcvarsPath = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"
            Write-Status "Found Visual Studio at: $vsPath"
        }
    }

    # Configure CMake project
    Write-Status "Configuring CMake project..."
    if ($vcvarsPath -and (Test-Path $vcvarsPath)) {
        # Use Visual Studio developer command prompt environment
        cmd /c "`"$vcvarsPath`" && cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug -DVCPKG_TARGET_TRIPLET=x64-windows-static -DCMAKE_TOOLCHAIN_FILE=`"$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake`" -G `"Visual Studio 17 2022`" -A x64"
    } else {
        # Fallback to Ninja
        Write-Warning-Message "Visual Studio not found, using Ninja generator"
        cmake -B build -S . `
            -DCMAKE_BUILD_TYPE=Debug `
            -DVCPKG_TARGET_TRIPLET=x64-windows-static `
            -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
            -G Ninja
    }

    # Build the project
    Write-Status "Building C++ trading engine..."
    cmake --build build --config Debug
} else {
    Write-Skip "Build directory exists - skipping build. Delete 'build' folder to force rebuild."
}

Write-Status "Running tests to verify setup..."

Write-Status "Running C++ tests..."
Push-Location build
$cppTestsFailed = $false
try {
    ctest -C Debug --output-on-failure
} catch {
    Write-Error-Message "C++ tests failed!"
    $cppTestsFailed = $true
}
Pop-Location

# Create setup directory if it doesn't exist
$setupDir = Join-Path $projectRoot "setup"
if (!(Test-Path $setupDir)) {
    New-Item -ItemType Directory -Path $setupDir | Out-Null
}

# Create run script in setup folder
Write-Status "Creating/updating run script in setup folder..."
$runScriptPath = Join-Path $setupDir "run_trading_system.ps1"
@'
# run_trading_system.ps1 - Start the Rich-on-Paper trading system on Windows
# Location: setup/run_trading_system.ps1
#
# Usage:
#   From project root: .\setup\run_trading_system.ps1
#   Or: cd setup && .\run_trading_system.ps1

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = if ($scriptDir -match "setup$") { Split-Path -Parent $scriptDir } else { $scriptDir }
Push-Location $projectRoot

$ErrorActionPreference = "Stop"

function Write-Status {
    param($Message)
    Write-Host "[$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')] " -ForegroundColor Green -NoNewline
    Write-Host $Message
}

function Write-Error-Message {
    param($Message)
    Write-Host "[ERROR] " -ForegroundColor Red -NoNewline
    Write-Host $Message
}

if (!(Test-Path "build\Debug\paper_money.exe") -and !(Test-Path "build\Release\paper_money.exe")) {
    Write-Error-Message "C++ binary not found. Run setup\setup_windows.ps1 first."
    Pop-Location
    exit 1
}

if (!$env:APCA_API_KEY_ID -or !$env:APCA_API_SECRET_KEY) {
    Write-Error-Message "Alpaca API credentials not set!"
    Write-Host "Please set the following environment variables:"
    Write-Host "  `$env:APCA_API_KEY_ID = 'your_key_here'"
    Write-Host "  `$env:APCA_API_SECRET_KEY = 'your_secret_here'"
    Write-Host ""
    Write-Host "Or use the setup\env.ps1 template"
    Pop-Location
    exit 1
}

$script:EngineProcess = $null
$script:ShutdownComplete = $false

function Stop-TradingSystem {
    if ($script:ShutdownComplete) {
        return
    }
    $script:ShutdownComplete = $true

    Write-Status "Shutting down trading system..."

    if ($script:EngineProcess -and !$script:EngineProcess.HasExited) {
        Write-Status "Stopping trading engine..."
        Stop-Process -Id $script:EngineProcess.Id -Force -ErrorAction SilentlyContinue
    }

    Write-Status "Cleanup complete"
    Pop-Location
}

[Console]::TreatControlCAsInput = $false
$null = Register-EngineEvent -SourceIdentifier PowerShell.Exiting -Action { Stop-TradingSystem }

try {
    Write-Status "Starting trading engine..."
    $enginePath = if (Test-Path "build\Debug\paper_money.exe") {
        "build\Debug\paper_money.exe"
    } else {
        "build\Release\paper_money.exe"
    }

    $engineStartInfo = New-Object System.Diagnostics.ProcessStartInfo
    $engineStartInfo.FileName = (Resolve-Path $enginePath).Path
    $engineStartInfo.UseShellExecute = $false
    $engineStartInfo.WorkingDirectory = $projectRoot

    $script:EngineProcess = [System.Diagnostics.Process]::Start($engineStartInfo)

    Start-Sleep -Seconds 2

    if ($script:EngineProcess.HasExited) {
        Write-Error-Message "Trading engine failed to start!"
        Stop-TradingSystem
        exit 1
    }

    Write-Status "Trading system is running!"
    Write-Status "Engine PID: $($script:EngineProcess.Id)"
    Write-Status "Press Ctrl+C to stop..."

    while ($true) {
        Start-Sleep -Seconds 1

        if ($script:EngineProcess.HasExited) {
            $rawExitCode = $script:EngineProcess.ExitCode
            $exitCode = if ($rawExitCode -ne 0) { $rawExitCode } else { 1 }
            Write-Error-Message "Trading engine exited unexpectedly with code $rawExitCode."
            exit $exitCode
        }
    }
} finally {
    Stop-TradingSystem
}
'@ | Out-File -FilePath $runScriptPath -Encoding UTF8

# Create batch file wrapper in setup folder
$batchPath = Join-Path $setupDir "run_trading_system.bat"
@'
@echo off
cd /d "%~dp0\.."
powershell.exe -ExecutionPolicy Bypass -File setup\run_trading_system.ps1
pause
'@ | Out-File -FilePath $batchPath -Encoding ASCII

# Create environment templates in setup folder
Write-Status "Creating environment variable templates in setup folder..."

$envPsPath = Join-Path $setupDir "env.ps1.template"
@'
# env.ps1 - Environment variables for PowerShell
#
# Usage:
#   1. Copy this file to env.ps1 (in setup folder)
#   2. Edit with your Alpaca credentials
#   3. From project root: . .\setup\env.ps1

$env:APCA_API_KEY_ID = "your_alpaca_key_here"
$env:APCA_API_SECRET_KEY = "your_alpaca_secret_here"

# Optional: Override API base URL for paper/live trading
# $env:APCA_API_BASE_URL = "https://paper-api.alpaca.markets"

Write-Host "Environment variables set successfully!" -ForegroundColor Green
'@ | Out-File -FilePath $envPsPath -Encoding UTF8

$envBatPath = Join-Path $setupDir "env.bat.template"
@'
@echo off
REM env.bat - Environment variables for Command Prompt
REM
REM Usage:
REM   1. Copy this file to env.bat (in setup folder)
REM   2. Edit with your Alpaca credentials
REM   3. Run from project root: setup\env.bat

set APCA_API_KEY_ID=your_alpaca_key_here
set APCA_API_SECRET_KEY=your_alpaca_secret_here

REM Optional: Override API base URL for paper/live trading
REM set APCA_API_BASE_URL=https://paper-api.alpaca.markets

echo Environment variables set successfully!
'@ | Out-File -FilePath $envBatPath -Encoding ASCII

# Create test runner in setup folder
$testBatPath = Join-Path $setupDir "run_tests.bat"
@'
@echo off
echo Running all tests...
cd /d "%~dp0\.."
echo.
echo C++ tests:
cd build
ctest -C Debug --output-on-failure
cd ..
pause
'@ | Out-File -FilePath $testBatPath -Encoding ASCII

# Create rebuild script in setup folder
$rebuildBatPath = Join-Path $setupDir "rebuild.bat"
@'
@echo off
echo Building C++ project...
cd /d "%~dp0\.."
cmake --build build --config Debug
pause
'@ | Out-File -FilePath $rebuildBatPath -Encoding ASCII

# Final status
Write-Host ""
Write-Status "============================================"
Write-Status "Setup completed successfully!"
Write-Status "============================================"
Write-Host ""

if ($cppTestsFailed) {
    Write-Error-Message "C++ tests failed - please check the errors above"
}

Write-Host "Scripts created in setup folder:" -ForegroundColor Cyan
Write-Host "  - setup\run_trading_system.ps1  : Start trading system (PowerShell)"
Write-Host "  - setup\run_trading_system.bat  : Start trading system (double-click)"
Write-Host "  - setup\run_tests.bat           : Run all tests"
Write-Host "  - setup\rebuild.bat             : Rebuild C++ code"
Write-Host "  - setup\env.ps1.template        : Environment variable template"
Write-Host ""
Write-Host "NEXT STEPS:" -ForegroundColor Yellow
Write-Host "1. Copy setup\env.ps1.template to setup\env.ps1"
Write-Host "2. Edit setup\env.ps1 with your Alpaca API credentials"
Write-Host "3. Run: . .\setup\env.ps1"
Write-Host "4. Run: .\setup\run_trading_system.ps1"
Write-Host ""
Write-Status "Setup can be run again safely - it will skip already installed components"

Pop-Location
