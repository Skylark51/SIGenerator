param(
    [string]$RuntimeDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'dist'),
    [string]$BuildDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build-single-file'),
    [string]$OutputPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'SI generator.exe')
)
$ErrorActionPreference = 'Stop'
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$build = [IO.Path]::GetFullPath($BuildDirectory)
$output = [IO.Path]::GetFullPath($OutputPath)
foreach ($required in @('SIGenerator.exe', 'Qt6Core.dll', 'platforms/qwindows.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $required) -PathType Leaf)) {
        throw "Incomplete runtime: $required is missing"
    }
}

# An ASCII staging path also supports source checkouts with Korean filenames.
$stage = Join-Path $env:TEMP ('SI-Generator-single-' + [guid]::NewGuid().ToString('N'))
$payload = Join-Path $build 'payload'
New-Item -ItemType Directory -Path $stage,$payload -Force | Out-Null
try {
    $files = @(Get-ChildItem -LiteralPath $runtime -File -Recurse | Where-Object {
        $_.Name -ne 'README.md' -and $_.Extension -ne '.pdb'
    } | Sort-Object FullName)
    $manifest = [Collections.Generic.List[string]]::new()
    $ddf = [Collections.Generic.List[string]]::new()
    @('.OPTION EXPLICIT', '.Set Cabinet=on', '.Set Compress=on',
      '.Set CompressionType=LZX', '.Set CompressionMemory=21',
      '.Set MaxDiskSize=0', '.Set MaxCabinetSize=0', '.Set FolderSizeThreshold=1073741824',
      '.Set CabinetNameTemplate=runtime.cab', ".Set DiskDirectoryTemplate=$stage",
      ".Set RptFileName=$stage\makecab.rpt", ".Set InfFileName=$stage\makecab.inf") |
        ForEach-Object { $ddf.Add($_) }
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($runtime.Length).TrimStart('\').Replace('\', '/')
        if ($relative -notmatch '^[\x20-\x7e]+$' -or $relative.Contains('"')) {
            throw "Runtime filenames must be ASCII: $relative"
        }
        # Do not silently include redistributable installers or development tools.
        if ($file.Extension -eq '.exe' -and $relative -ne 'SIGenerator.exe') {
            throw "Unexpected executable in runtime: $relative"
        }
        $copy = Join-Path $stage ('files/' + $relative)
        New-Item -ItemType Directory -Path (Split-Path -Parent $copy) -Force | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $copy
        $digest = (Get-FileHash -LiteralPath $copy -Algorithm SHA256).Hash.ToLowerInvariant()
        $manifest.Add("$digest  $relative")
        $cabName = $relative.Replace('/', '\')
        $ddf.Add(('"{0}" "{1}"' -f $copy, $cabName))
    }
    [IO.File]::WriteAllText((Join-Path $payload 'runtime.manifest'),
        (($manifest -join "`n") + "`n"), [Text.UTF8Encoding]::new($false))
    $directive = Join-Path $stage 'runtime.ddf'
    [IO.File]::WriteAllLines($directive, $ddf, [Text.Encoding]::ASCII)
    $cabLog = Join-Path $stage 'makecab.log'
    & makecab.exe /F $directive /V1 > $cabLog
    if ($LASTEXITCODE -ne 0) { Get-Content -LiteralPath $cabLog; throw 'Runtime compression failed' }
    Copy-Item -LiteralPath (Join-Path $stage 'runtime.cab') -Destination $payload -Force
    cmake -S (Join-Path $PSScriptRoot 'standalone') -B $build -G 'Visual Studio 17 2022' -A x64 "-DSI_PAYLOAD_DIRECTORY=$($payload.Replace('\', '/'))"
    if ($LASTEXITCODE -ne 0) { throw 'Single-file launcher configuration failed' }
    cmake --build $build --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Single-file launcher build failed' }
    New-Item -ItemType Directory -Path (Split-Path -Parent $output) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $build 'Release/SI generator.exe') -Destination $output -Force
    $size = (Get-Item -LiteralPath $output).Length
    $originalSize = ($files | Measure-Object Length -Sum).Sum
    Write-Host ('Ready: {0} ({1:N2} MiB; runtime {2:N2} MiB)' -f $output, ($size / 1MB), ($originalSize / 1MB))
} finally {
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if ($resolvedStage.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedStage) -like 'SI-Generator-single-*') {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
