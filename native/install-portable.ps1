# Install a verified release package independently of the source checkout.
param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$SourceCommit,
    [string]$PortableRoot = 'C:\SI-Generator',
    # Optional standalone artifact from the SAME successful Windows build.
    [string]$SingleFilePath
)

$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackagePath).Path
$target = [System.IO.Path]::GetFullPath($PortableRoot)
$repoRoot = Split-Path -Parent $PSScriptRoot
$desktop = [Environment]::GetFolderPath('Desktop')
$desktopExe = Join-Path $desktop 'SI generator.exe'
$singleFile = $null
if ($SingleFilePath) {
    $singleFile = (Resolve-Path -LiteralPath $SingleFilePath).Path
    foreach ($process in @(Get-Process -Name 'SI generator' -ErrorAction SilentlyContinue)) {
        if ([string]::IsNullOrWhiteSpace($process.Path) -or $process.Path -ieq $desktopExe) {
            throw 'Close the desktop SI generator before applying the update.'
        }
    }
}
if ($target.TrimEnd('\') -eq $repoRoot.TrimEnd('\')) {
    throw 'Use a separate portable directory, not the source checkout.'
}
New-Item -ItemType Directory -Path $target -Force | Out-Null
# The existing updater validates Qt6Core.dll, backs up runtime files, and rolls
# back copied files if installation fails. It also refuses to update a running app.
& (Join-Path $PSScriptRoot 'update-local.ps1') -PackagePath $package -ProjectRoot $target

$exe = Join-Path $target 'SIGenerator.exe'
[ordered]@{
    sourceCommit = $SourceCommit.ToLowerInvariant()
    packageSha256 = (Get-FileHash -LiteralPath $package -Algorithm SHA256).Hash
    executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    installedAt = (Get-Date).ToString('o')
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $target 'portable-version.json') -Encoding UTF8

$shell = New-Object -ComObject WScript.Shell
$shortcuts = @(Get-ChildItem -LiteralPath $desktop -Filter '*.lnk' -File | Where-Object {
    $link = $shell.CreateShortcut($_.FullName)
    [System.IO.Path]::GetFileName($link.TargetPath) -ieq 'SIGenerator.exe'
})
$shortcutPaths = @($shortcuts | ForEach-Object { $_.FullName })
if ($singleFile) {
    $desktopBackup = Join-Path $target ('backups/desktop-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Path $desktopBackup -Force | Out-Null
    if (Test-Path -LiteralPath $desktopExe) {
        Copy-Item -LiteralPath $desktopExe -Destination $desktopBackup
    }
    $expected = (Get-FileHash -LiteralPath $singleFile -Algorithm SHA256).Hash
    $stageExe = Join-Path $desktop ('SI-generator-update-' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        Copy-Item -LiteralPath $singleFile -Destination $stageExe
        if ((Get-FileHash -LiteralPath $stageExe -Algorithm SHA256).Hash -ne $expected) {
            throw 'Desktop single-file SHA-256 verification failed.'
        }
        Move-Item -LiteralPath $stageExe -Destination $desktopExe -Force
    } finally {
        if (Test-Path -LiteralPath $stageExe) { Remove-Item -LiteralPath $stageExe -Force }
    }
    # Only replace the two historical SI shortcuts. Other shortcuts are untouched.
    foreach ($shortcutPath in $shortcutPaths) {
        if ((Split-Path -Leaf $shortcutPath) -in @('SI Generator.lnk', 'SIGenerator.lnk')) {
            Copy-Item -LiteralPath $shortcutPath -Destination $desktopBackup
            Remove-Item -LiteralPath $shortcutPath -Force
        }
    }
    $versionFile = Join-Path $target 'portable-version.json'
    $version = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
    $version | Add-Member -NotePropertyName desktopExecutable -NotePropertyValue $desktopExe -Force
    $version | Add-Member -NotePropertyName desktopExecutableSha256 -NotePropertyValue $expected -Force
    $version | ConvertTo-Json | Set-Content -LiteralPath $versionFile -Encoding UTF8
    Write-Host "Desktop single EXE: $desktopExe"
    Write-Host "Portable commit: $SourceCommit"
    return
}
if ($shortcutPaths.Count -eq 0) {
    $shortcutPaths = @(Join-Path $desktop 'SI Generator.lnk')
}
foreach ($shortcutPath in $shortcutPaths) {
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = $target
    $shortcut.IconLocation = "$exe,0"
    $shortcut.Arguments = ''
    $shortcut.Description = 'SI Generator (C: portable)'
    $shortcut.Save()
    $check = $shell.CreateShortcut($shortcutPath)
    if ($check.TargetPath -ne $exe -or $check.WorkingDirectory -ne $target) {
        throw "Shortcut verification failed: $shortcutPath"
    }
    Write-Host "Shortcut: $shortcutPath -> $exe"
}
Write-Host "Portable commit: $SourceCommit"
