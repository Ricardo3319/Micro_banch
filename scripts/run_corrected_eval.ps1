param(
    [ValidateSet("pilot", "holdout", "full")]
    [string]$Tier = "pilot",
    [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root "$BuildDir/simulator.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Simulator not found: $exe"
}

if ($Tier -eq "full") {
    $warmup = 200000
    $measurement = 1000000
    $seeds = "11,23,37,47,59,71,83,97,109,127"
    $step = "step-21-corrected-full"
} elseif ($Tier -eq "holdout") {
    $warmup = 500
    $measurement = 5000
    $seeds = "71,83,97,109,127"
    $step = "step-20b-corrected-holdout-pilot"
} else {
    $warmup = 500
    $measurement = 5000
    $seeds = "11,23,37,47,59"
    $step = "step-20-corrected-pilot"
}

$out = Join-Path $root "artifacts/$step"
New-Item -ItemType Directory -Force -Path $out | Out-Null

& $exe --mode rescue-main --workload W3 --rho "0.70,0.85,0.90" --seed $seeds `
    --warmup-requests $warmup --measurement-requests $measurement `
    --output (Join-Path $out "w3.csv")
if ($LASTEXITCODE -ne 0) { throw "W3 corrected evaluation failed" }

& $exe --mode rescue-main --workload W2 --rho "0.70,0.85" --seed $seeds `
    --warmup-requests $warmup --measurement-requests $measurement `
    --output (Join-Path $out "w2.csv")
if ($LASTEXITCODE -ne 0) { throw "W2 corrected evaluation failed" }

& $exe --mode rescue-main --workload W1 --rho "0.70,0.85" --seed $seeds `
    --warmup-requests $warmup --measurement-requests $measurement `
    --output (Join-Path $out "w1.csv")
if ($LASTEXITCODE -ne 0) { throw "W1 corrected evaluation failed" }

python (Join-Path $PSScriptRoot "corrected_eval_analysis.py") `
    --tier $Tier --out-dir $out --inputs `
    (Join-Path $out "w3.csv") (Join-Path $out "w2.csv") (Join-Path $out "w1.csv")
if ($LASTEXITCODE -ne 0) { throw "Corrected evaluation analysis failed" }

Write-Output "Corrected $Tier evaluation complete: $out"
