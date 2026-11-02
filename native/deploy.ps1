param([string]$QtBin = $env:SI_GENERATOR_QT_BIN)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $projectRoot "SIGenerator.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Release executable was not found: $exe"
}

if ([string]::IsNullOrWhiteSpace($QtBin)) { throw "Set SI_GENERATOR_QT_BIN or pass -QtBin with the Qt bin directory." }
$deploy = Join-Path $QtBin "windeployqt.exe"
if (-not (Test-Path -LiteralPath $deploy)) {
    throw "windeployqt.exe was not found. Set SI_GENERATOR_QT_BIN or pass -QtBin."
}

& $deploy --release --no-translations $exe
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

Write-Host "Deployable native SI Generator: $exe"
