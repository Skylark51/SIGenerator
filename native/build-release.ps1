param(
    [string]$QtRoot = $(if ($env:QT_ROOT_DIR) { $env:QT_ROOT_DIR } else { $env:CMAKE_PREFIX_PATH }),
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    throw "QtRoot is required. Example: .\\native\\build-release.ps1 -QtRoot C:\\Qt\\6.8.3\\msvc2022_64"
}
$qtDeploy = Join-Path $QtRoot "bin/windeployqt.exe"
if (-not (Test-Path $qtDeploy)) { throw "windeployqt.exe not found in $QtRoot" }

$dist = Join-Path $repoRoot "dist"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 "-DCMAKE_PREFIX_PATH=$QtRoot" "-DSI_GENERATOR_OUTPUT_DIRECTORY=$dist"
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
cmake --build build --config Release --parallel 4
if ($LASTEXITCODE -ne 0) { throw "Windows build failed" }

if (-not $SkipTests) {
    ctest --test-dir build -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Regression tests failed" }
}
& $qtDeploy --release --no-translations --dir $dist (Join-Path $dist "SIGenerator.exe")
if ($LASTEXITCODE -ne 0) { throw "Qt deployment failed" }
Copy-Item (Join-Path $repoRoot "README.md") (Join-Path $dist "README.md") -Force

$zip = Join-Path $repoRoot "SI-Generator-Windows-x64.zip"
if (Test-Path $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $dist "*") -DestinationPath $zip
Write-Host "Ready: $zip"
