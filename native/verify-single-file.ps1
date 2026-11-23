param(
    [Parameter(Mandatory = $true)][string]$ExecutablePath,
    [Parameter(Mandatory = $true)][string]$RuntimeDirectory,
    [Parameter(Mandatory = $true)][string]$ManifestPath
)
$ErrorActionPreference = 'Stop'
$exe = (Resolve-Path -LiteralPath $ExecutablePath).Path
$runtime = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
$manifest = (Resolve-Path -LiteralPath $ManifestPath).Path
$stage = Join-Path $env:TEMP ('SI-Generator-verify-' + [guid]::NewGuid().ToString('N'))
$isolated = Join-Path $stage '단일 실행 파일'
$cacheRoot = Join-Path $stage '한글 캐시'
New-Item -ItemType Directory -Path $isolated -Force | Out-Null
$copy = Join-Path $isolated 'SI generator.exe'
Copy-Item -LiteralPath $exe -Destination $copy
$id = (Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant()
$cache = Join-Path $cacheRoot $id
$savedEnvironment = @{}
foreach ($name in @('PATH', 'SI_GENERATOR_CACHE_ROOT', 'SI_GENERATOR_TEST_SETTINGS',
    'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH')) {
    $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
$owned = [Collections.Generic.List[Diagnostics.Process]]::new()
function Start-Verification {
    $process = Start-Process -FilePath $copy -ArgumentList '--single-file-verify' -PassThru -WindowStyle Hidden
    $owned.Add($process)
    return $process
}
function Wait-Verification([Diagnostics.Process]$Process) {
    if (-not $Process.WaitForExit(60000)) { throw 'Single-file verification timed out' }
    if ($Process.ExitCode -ne 0) { throw "Single-file verification failed: $($Process.ExitCode)" }
}
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:SI_GENERATOR_CACHE_ROOT = $cacheRoot
    $env:SI_GENERATOR_TEST_SETTINGS = '1'
    $env:QT_PLUGIN_PATH = Join-Path $stage 'nonexistent-plugins'
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $env:QT_PLUGIN_PATH

    $timer = [Diagnostics.Stopwatch]::StartNew()
    # Two cold launches must serialize extraction and both succeed.
    $first = Start-Verification
    $second = Start-Verification
    Wait-Verification $first
    Wait-Verification $second
    $coldMs = $timer.ElapsedMilliseconds
    foreach ($line in [IO.File]::ReadAllLines($manifest)) {
        $relative = $line.Substring(66)
        $actual = (Get-FileHash -LiteralPath (Join-Path $cache $relative) -Algorithm SHA256).Hash
        $original = (Get-FileHash -LiteralPath (Join-Path $runtime $relative) -Algorithm SHA256).Hash
        if ($actual -ne $line.Substring(0, 64) -or $actual -ne $original) {
            throw "Embedded runtime differs from original: $relative"
        }
    }
    $before = (Get-Item -LiteralPath (Join-Path $cache 'SIGenerator.exe')).LastWriteTimeUtc
    $timer.Restart()
    Wait-Verification (Start-Verification)
    $warmMs = $timer.ElapsedMilliseconds
    if ((Get-Item -LiteralPath (Join-Path $cache 'SIGenerator.exe')).LastWriteTimeUtc -ne $before) {
        throw 'Warm launch unexpectedly re-extracted the runtime'
    }
    [IO.File]::AppendAllText((Join-Path $cache 'Qt6Core.dll'), 'corruption test')
    Wait-Verification (Start-Verification)
    [IO.File]::WriteAllText((Join-Path $cache 'unexpected.dll'), 'extra DLL test')
    Wait-Verification (Start-Verification)
    if (Test-Path -LiteralPath (Join-Path $cache 'unexpected.dll')) { throw 'Unexpected DLL survived cache repair' }

    $launcher = Start-Process -FilePath $copy -WorkingDirectory $isolated -PassThru -WindowStyle Hidden
    $owned.Add($launcher)
    $child = $null
    $timer.Restart()
    while ($timer.ElapsedMilliseconds -lt 30000) {
        $child = Get-Process -Name SIGenerator -ErrorAction SilentlyContinue | Where-Object {
            $_.Path -eq (Join-Path $cache 'SIGenerator.exe') -and $_.MainWindowHandle -ne 0 -and
            $_.MainWindowTitle -like '*프로젝트 관리*'
        } | Select-Object -First 1
        if ($child) { break }
        if ($launcher.HasExited) { throw "Isolated launch exited early: $($launcher.ExitCode)" }
        Start-Sleep -Milliseconds 100
    }
    if (-not $child) { throw 'Project manager window did not appear from the isolated single EXE' }
    $owned.Add($child)
    $startupMs = $timer.ElapsedMilliseconds
    if (-not $child.CloseMainWindow()) { throw 'Could not close the test project manager normally' }
    if (-not $launcher.WaitForExit(15000) -or $launcher.ExitCode -ne 0) {
        throw 'Single-file launcher did not return the normal application exit code'
    }
    if (@(Get-ChildItem -LiteralPath $isolated -Force).Count -ne 1) {
        throw 'Runtime files leaked next to the single EXE'
    }
    Write-Host "Single-file checks passed: concurrent cold $coldMs ms; warm verification $warmMs ms; UI startup $startupMs ms."
} finally {
    # Only processes started by this isolated test are eligible for cleanup.
    foreach ($process in $owned) {
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
    }
    foreach ($name in $savedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process')
    }
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    if ($resolvedStage.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
        (Split-Path -Leaf $resolvedStage) -like 'SI-Generator-verify-*') {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
