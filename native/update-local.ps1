# Update the original working directory without modifying Git-tracked source files.
param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$projectPath = [System.IO.Path]::GetFullPath($ProjectRoot)
$zip = (Resolve-Path -LiteralPath $PackagePath).Path
if (Get-Process -Name "SIGenerator" -ErrorAction SilentlyContinue) {
    throw "Close SIGenerator.exe before applying the update."
}
if (-not (Test-Path -LiteralPath $projectPath -PathType Container)) {
    throw "Project directory not found: $projectPath"
}

$stage = Join-Path $env:TEMP ("SI-Generator-update-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    Expand-Archive -LiteralPath $zip -DestinationPath $stage
    $exe = Get-ChildItem -LiteralPath $stage -File -Filter SIGenerator.exe -Recurse | Select-Object -First 1
    if (-not $exe) {
        # GitHub Actions artifacts contain a ZIP inside the artifact ZIP.
        $nested = Get-ChildItem -LiteralPath $stage -File -Filter "SI-Generator-Windows-x64.zip" -Recurse | Select-Object -First 1
        if (-not $nested) { throw "Neither SIGenerator.exe nor a release package was found." }
        $inner = Join-Path $stage "release"
        Expand-Archive -LiteralPath $nested.FullName -DestinationPath $inner
        $exe = Get-ChildItem -LiteralPath $inner -File -Filter SIGenerator.exe -Recurse | Select-Object -First 1
    }
    if (-not $exe) { throw "Release package does not contain SIGenerator.exe." }
    $packageRoot = $exe.DirectoryName
    if (-not (Test-Path -LiteralPath (Join-Path $packageRoot "Qt6Core.dll"))) {
        throw "Incomplete package: Qt6Core.dll is missing."
    }

    $backup = Join-Path $projectPath ("backups\runtime-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    $installed = [System.Collections.Generic.List[string]]::new()
    $created = [System.Collections.Generic.List[string]]::new()
    try {
        foreach ($file in (Get-ChildItem -LiteralPath $packageRoot -File -Recurse)) {
            $relative = $file.FullName.Substring($packageRoot.Length).TrimStart('\')
            if ($relative -eq "README.md") { continue }
            $destination = Join-Path $projectPath $relative
            $backupFile = Join-Path $backup $relative
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            if (Test-Path -LiteralPath $destination) {
                New-Item -ItemType Directory -Path (Split-Path -Parent $backupFile) -Force | Out-Null
                Copy-Item -LiteralPath $destination -Destination $backupFile -Force
            } else {
                $created.Add($destination)
            }
            $installed.Add($relative)
            Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        }
    } catch {
        foreach ($relative in $installed) {
            $destination = Join-Path $projectPath $relative
            $backupFile = Join-Path $backup $relative
            if (Test-Path -LiteralPath $backupFile) {
                Copy-Item -LiteralPath $backupFile -Destination $destination -Force
            }
        }
        foreach ($destination in $created) {
            if (Test-Path -LiteralPath $destination) { Remove-Item -LiteralPath $destination -Force }
        }
        throw
    }
    Write-Host "Updated: $(Join-Path $projectPath 'SIGenerator.exe')"
    Write-Host "Previous runtime files backed up at: $backup"
} finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
