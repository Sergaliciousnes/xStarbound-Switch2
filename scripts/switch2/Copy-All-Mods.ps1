# Requires Windows PowerShell 5.1 or PowerShell 7. Source files are read only.
[CmdletBinding()]
param(
    [string]$SteamPath,
    [string]$Destination,
    [switch]$NonInteractive
)
$ErrorActionPreference = 'Stop'

function Get-PakHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '') }
        finally { $sha.Dispose() }
    } finally { $stream.Dispose() }
}

function Get-WorkshopMods([string]$Root) {
    if ([string]::IsNullOrWhiteSpace($Root)) { return }
    $Root = $Root.Trim().Trim('"').Trim("'")
    if (Test-Path -LiteralPath $Root -PathType Container) { $Root = (Get-Item -LiteralPath $Root).FullName }
    $candidates = @( (Join-Path $Root 'steamapps\workshop\content\211820') )
    if ((Split-Path $Root -Leaf) -eq '211820') { $candidates = @($Root) + $candidates }
    foreach ($candidate in $candidates) {
        if (!(Test-Path -LiteralPath $candidate -PathType Container)) { continue }
        foreach ($folder in (Get-ChildItem -LiteralPath $candidate -Directory | Sort-Object Name)) {
            if ($folder.Name -notmatch '^[0-9]+$') { continue }
            $pak = Join-Path $folder.FullName 'packed.pak'
            if (Test-Path -LiteralPath $pak -PathType Leaf) {
                [pscustomobject]@{ Id = $folder.Name; Source = (Get-Item -LiteralPath $pak).FullName }
            }
        }
    }
}

try {
    Write-Host 'Copy All Starbound Workshop Mods' -ForegroundColor Cyan
    Write-Host 'Copies packed Workshop mods as <workshop_id>.pak. Steam originals are unchanged.'
    if ([string]::IsNullOrWhiteSpace($SteamPath)) {
        $programFiles = ${env:ProgramFiles(x86)}
        if ([string]::IsNullOrWhiteSpace($programFiles)) { $programFiles = $env:ProgramFiles }
        if ([string]::IsNullOrWhiteSpace($programFiles)) { $programFiles = 'C:\Program Files (x86)' }
        $SteamPath = Join-Path $programFiles 'Steam'
    }
    while ($true) {
        $mods = @(Get-WorkshopMods $SteamPath)
        if ($mods.Count -gt 0) { break }
        $message = "No Starbound Workshop packed.pak files found under '$SteamPath'."
        if ($NonInteractive) { throw "$message Supply -SteamPath with your Steam or Steam library folder." }
        Write-Host $message -ForegroundColor Yellow
        Write-Host 'Wait for Steam Workshop downloads to finish, or choose the library containing the mods.'
        $SteamPath = Read-Host 'Enter your Steam folder / Steam library folder (or Q to cancel)'
        if ([string]::IsNullOrWhiteSpace($SteamPath) -or $SteamPath -eq 'Q') { exit 0 }
    }
    Write-Host ("Found {0} packed mod(s)." -f $mods.Count)
    if ([string]::IsNullOrWhiteSpace($Destination)) {
        $defaultDestination = Join-Path (Split-Path $PSScriptRoot -Parent) 'switch\xstarbound\mods'
        if ($NonInteractive) {
            if (!(Test-Path -LiteralPath (Split-Path $defaultDestination -Parent) -PathType Container)) {
                throw 'Supply -Destination when running this script outside the extracted tester package.'
            }
            $Destination = $defaultDestination
        } else {
            Write-Host "Default output: $defaultDestination"
            $Destination = Read-Host 'Destination mods folder (Enter for default, or Q to cancel)'
            if ($Destination -eq 'Q') { exit 0 }
            if ([string]::IsNullOrWhiteSpace($Destination)) { $Destination = $defaultDestination }
        }
    }
    $Destination = [IO.Path]::GetFullPath($Destination.Trim().Trim('"').Trim("'"))
    # Only create the chosen output directory; never remove/reorganize a source tree.
    [IO.Directory]::CreateDirectory($Destination) | Out-Null
    $copied = 0; $updated = 0; $unchanged = 0; $failed = 0
    foreach ($mod in $mods) {
        $target = Join-Path $Destination ($mod.Id + '.pak')
        $temporary = Join-Path $Destination ('.' + $mod.Id + '.copying-' + [guid]::NewGuid().ToString('N') + '.tmp')
        try {
            if (Test-Path -LiteralPath $target -PathType Container) { throw "Output is a directory: $target" }
            $sourceHash = (Get-PakHash $mod.Source)
            $exists = Test-Path -LiteralPath $target -PathType Leaf
            if ($exists -and (Get-PakHash $target) -eq $sourceHash) {
                $unchanged++; Write-Host ("Unchanged: {0}.pak" -f $mod.Id); continue
            }
            [IO.File]::Copy($mod.Source, $temporary, $false)
            if ((Get-PakHash $temporary) -ne $sourceHash) {
                throw 'Copy verification failed; the Steam download may have changed. Retry after downloads finish.'
            }
            Move-Item -LiteralPath $temporary -Destination $target -Force
            if ($exists) { $updated++ } else { $copied++ }
            Write-Host ("Copied: {0}.pak" -f $mod.Id)
        } catch {
            $failed++; Write-Warning ("Failed {0}: {1}" -f $mod.Id, $_.Exception.Message)
        } finally {
            if (Test-Path -LiteralPath $temporary -PathType Leaf) { Remove-Item -LiteralPath $temporary -Force }
        }
    }
    Write-Host ''
    Write-Host ("Finished: {0} copied, {1} updated, {2} unchanged, {3} failed." -f $copied,$updated,$unchanged,$failed)
    Write-Host "Output: $Destination"
    Write-Host 'Transfer these files to SD:/switch/xstarbound/mods/ if the output is on your PC.'
    Write-Host 'Other destination mods are left in place, including mods you have unsubscribed from.'
    if ($failed -gt 0) { exit 1 }
    exit 0
} catch {
    Write-Host ("Error: {0}" -f $_.Exception.Message) -ForegroundColor Red
    exit 1
}
