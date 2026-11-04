param([string]$Engine='C:\Program Files\Epic Games\UE_5.6', [switch]$NoBrowser, [switch]$CheckOnly,
    [ValidateRange(1,65535)][int]$Port)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$project=Join-Path $root 'PortSim.uproject'
$editor=Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor.exe'
if(-not (Test-Path -LiteralPath $editor)) { throw "Unreal Editor not found: $editor. Supply -Engine with your UE 5.6 installation directory." }
$version=Get-Content -LiteralPath (Join-Path $Engine 'Engine\Build\Build.version') -Raw | ConvertFrom-Json
if($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 6) { throw 'This project launcher requires UE 5.6.' }
if($CheckOnly) { Write-Output "UE 5.6 launcher ready: $editor"; Write-Output "Project: $project"; return }
$dashboardOptions=@{Background=$true; NoBrowser=$NoBrowser}
if($PSBoundParameters.ContainsKey('Port')) { $dashboardOptions.Port=$Port }
& (Join-Path $PSScriptRoot 'LaunchDashboard.ps1') @dashboardOptions
# Launch the explicitly selected installation, without the version selector.
Start-Process -FilePath $editor -ArgumentList @("`"$project`"") | Out-Null
