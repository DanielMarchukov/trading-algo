Write-Host "Generating C++ code coverage..." -ForegroundColor Green

if (-not (Test-Path "src")) {
    Write-Host "ERROR: src directory not found" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path "build\Debug\rich_on_tests.exe")) {
    Write-Host "ERROR: Test executable not found" -ForegroundColor Red
    exit 1
}

$sourceLines = (Get-ChildItem src -Filter "*.cpp" | Get-Content | Measure-Object -Line).Lines
$headerLines = (Get-ChildItem include -Filter "*.hpp" | Get-Content | Measure-Object -Line).Lines
$totalProjectLines = $sourceLines + $headerLines

Write-Host "Your project has $totalProjectLines lines of code" -ForegroundColor Cyan

$srcPath = Resolve-Path "src"
$includePath = Resolve-Path "include"

Write-Host "Source path: $srcPath" -ForegroundColor Yellow
Write-Host "Include path: $includePath" -ForegroundColor Yellow

Write-Host "Running coverage analysis..." -ForegroundColor Green
OpenCppCoverage --modules "*rich_on_tests.exe" --sources "$srcPath" --sources "$includePath" --excluded_sources "*tests*" --excluded_sources "*vcpkg*" --excluded_sources "*gtest*" --export_type=html:coverage_html --export_type=cobertura:coverage_html.xml -- build\Debug\rich_on_tests.exe

if (Test-Path "coverage_html.xml") {
    Write-Host "Coverage XML generated successfully!" -ForegroundColor Green

    [xml]$xml = Get-Content "coverage_html.xml"
    if ($xml.coverage.packages.package.classes.class) {
        $fileCount = ($xml.coverage.packages.package.classes.class | Measure-Object).Count
        Write-Host "Files in report: $fileCount" -ForegroundColor Cyan
    }
}

if (Test-Path "coverage_html\index.html") {
    Write-Host "Opening HTML report..." -ForegroundColor Green
    Start-Process "coverage_html\index.html"
} else {
    Write-Host "HTML report not found" -ForegroundColor Red
}

Write-Host "Done!" -ForegroundColor Green
