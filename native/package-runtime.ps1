# Deploy the unchanged Qt app, with app-local MSVC libraries (no installer).
param(
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [string]$RuntimeDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist')
)
$ErrorActionPreference = 'Stop'
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
& (Join-Path $QtRoot 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime --dir $runtime (Join-Path $runtime 'SIGenerator.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }

# windeployqt can otherwise bundle a VC redistributable installer instead of
# the DLLs. A portable app must work before any such installer has been run.
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Visual C++ redist directory not found' }
$crt = Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Redist/MSVC') -Directory |
    Sort-Object Name -Descending | ForEach-Object {
        Join-Path $_.FullName 'x64/Microsoft.VC143.CRT'
    } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $crt) { throw 'App-local Microsoft.VC143.CRT not found' }
Get-ChildItem -LiteralPath $crt -File -Filter '*.dll' | Copy-Item -Destination $runtime -Force
Copy-Item (Join-Path (Split-Path -Parent $PSScriptRoot) 'README.md') (Join-Path $runtime 'README.md') -Force
