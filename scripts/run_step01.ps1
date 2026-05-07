Param(
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot
Set-Location -Path ".."

if (-not (Test-Path $BuildDir)) {
    cmake -S . -B $BuildDir
}
cmake --build $BuildDir --config Release

Write-Output "[step01] Tier-1 execution stub. Implement W2 and B1/B2/M0 run pipeline in Step-01."
