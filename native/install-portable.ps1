# Install a verified release package independently of the source checkout.
param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[0-9a-fA-F]{40}$')]
    [string]$SourceCommit,
    [string]$PortableRoot = 'C:\SI-Generator'
)

$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackagePath).Path
$target = [System.IO.Path]::GetFullPath($PortableRoot)
$repoRoot = Split-Path -Parent $PSScriptRoot
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

$desktop = [Environment]::GetFolderPath('Desktop')
$shell = New-Object -ComObject WScript.Shell
$shortcuts = @(Get-ChildItem -LiteralPath $desktop -Filter '*.lnk' -File | Where-Object {
    $link = $shell.CreateShortcut($_.FullName)
    [System.IO.Path]::GetFileName($link.TargetPath) -ieq 'SIGenerator.exe'
})
$shortcutPaths = @($shortcuts | ForEach-Object { $_.FullName })
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
