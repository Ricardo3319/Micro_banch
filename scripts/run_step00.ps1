Param(
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot
Set-Location -Path ".."

# Step-00 gate uses a clean build dir to avoid stale generator/toolchain cache.
if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}

# Prefer MSVC toolchain if available; fallback to Ninja + g++.
$clCmd = Get-Command cl -ErrorAction SilentlyContinue
if ($null -ne $clCmd) {
    cmake -S . -B $BuildDir -G "NMake Makefiles"
} else {
    $ninjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
    $gppCmd = Get-Command g++ -ErrorAction SilentlyContinue
    if (($null -eq $ninjaCmd) -or ($null -eq $gppCmd)) {
        throw "No usable C++ toolchain found. Need either cl + nmake or ninja + g++."
    }
    cmake -S . -B $BuildDir -G "Ninja" -DCMAKE_CXX_COMPILER="$($gppCmd.Source)"
}

if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed."
}

cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    throw "CMake build failed."
}

$exePath = Join-Path $BuildDir "simulator.exe"
if (-not (Test-Path $exePath)) {
    $exePath = Join-Path $BuildDir "simulator"
}

if (-not (Test-Path $exePath)) {
    throw "Built binary not found at '$exePath'."
}

& $exePath "config/default.yaml"
