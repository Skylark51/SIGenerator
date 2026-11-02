# Update the original working directory without modifying Git-tracked source files.
param(
    [Parameter(Mandatory = $false)]
    [string]$PackagePath,
    [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = "Stop"
$projectPath = [System.IO.Path]::GetFullPath($ProjectRoot)
if (Get-Process -Name "SIGenerator" -ErrorAction SilentlyContinue) {
    throw "Close SIGenerator.exe before applying the update."
}
if (-not (Test-Path -LiteralPath $projectPath -PathType Container)) {
    throw "Project directory not found: $projectPath"
}

function Get-LatestReleaseZip {
    param([string]$DestinationDirectory, [string]$RepositoryRoot)

    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
        throw "GitHub CLI (gh) is required for automatic updates. Install it with: winget install --id GitHub.cli -e ; then run: gh auth login"
    }
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
        throw "Git is required for automatic updates. Install Git, then run git pull --ff-only origin main."
    }

    $headOutput = & git -C $RepositoryRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or -not $headOutput) {
        throw "Unable to determine local Git commit. Run git pull --ff-only origin main in the project directory."
    }
    $localHead = ($headOutput -join "").Trim()

    $runsOutput = & gh run list -R Skylark51/SIGenerator -w windows-release.yml -b main -s success --json databaseId,headSha -L 40
    if ($LASTEXITCODE -ne 0 -or -not $runsOutput) {
        throw "Could not query GitHub Actions. Check the network and run gh auth login."
    }
    $runs = @(($runsOutput -join "`n" | ConvertFrom-Json))
    $matchedRun = $runs | Where-Object { $_.headSha -eq $localHead } | Select-Object -First 1
    if (-not $matchedRun) {
        throw "No successful Windows build matches local commit $localHead. Run git pull --ff-only origin main, wait for the main Windows release build to succeed, then retry."
    }

    Write-Host "Downloading Windows build for commit $localHead (run $($matchedRun.databaseId))..."
    & gh run download $matchedRun.databaseId -R Skylark51/SIGenerator -n SI-Generator-Windows-x64 -D $DestinationDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "GitHub Actions artifact download failed. Verify gh auth login and the workflow run."
    }
    $downloaded = Get-ChildItem -LiteralPath $DestinationDirectory -File -Filter "SI-Generator-Windows-x64.zip" -Recurse | Select-Object -First 1
    if (-not $downloaded) {
        throw "The Windows build artifact did not contain SI-Generator-Windows-x64.zip."
    }
    return $downloaded.FullName
}

$stage = Join-Path $env:TEMP ("SI-Generator-update-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    if ([string]::IsNullOrWhiteSpace($PackagePath)) {
        $zip = Get-LatestReleaseZip -DestinationDirectory $stage -RepositoryRoot $projectPath
    } else {
        if (-not (Test-Path -LiteralPath $PackagePath -PathType Leaf)) {
            throw "Package not found: $PackagePath. Omit -PackagePath to download the matching GitHub build automatically."
        }
        $zip = (Resolve-Path -LiteralPath $PackagePath).Path
    }
    $unpackDirectory = Join-Path $stage "unpacked"
    New-Item -ItemType Directory -Path $unpackDirectory | Out-Null
    Expand-Archive -LiteralPath $zip -DestinationPath $unpackDirectory
    $exe = Get-ChildItem -LiteralPath $unpackDirectory -File -Filter SIGenerator.exe -Recurse | Select-Object -First 1
    if (-not $exe) {
        # GitHub Actions artifacts contain a ZIP inside the artifact ZIP.
        $nested = Get-ChildItem -LiteralPath $unpackDirectory -File -Filter "SI-Generator-Windows-x64.zip" -Recurse | Select-Object -First 1
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
