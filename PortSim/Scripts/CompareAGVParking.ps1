param([string]$Engine='C:\Program Files\Epic Games\UE_5.6',
    [int]$EnvironmentSeed=166290927,
    [ValidateRange(1,60)][int]$FixedFPS=5,
    [ValidateRange(1,120)][int]$TimeoutMinutes=120)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dir=Join-Path $root ('Saved/Tests/ParkingComparison_'+(Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Path $dir -Force | Out-Null
foreach($layout in @('Legacy','Depot')) {
    # Fixed simulation steps make this an A/B measurement independent of wall
    # time/CPU scheduling. Both layouts use the same corrected traffic code.
    & (Join-Path $PSScriptRoot 'TestSite.ps1') -Engine $Engine -Mode Normal -PlaybackRate 0 `
        -FixedFPS $FixedFPS -TimeoutMinutes $TimeoutMinutes -EnvironmentSeed $EnvironmentSeed `
        -ParkingLayout $layout -ApronLayout Previous -CheckStowage -CheckDispatch -ResultSummaryPath (Join-Path $dir "$layout.json")
}
$before=Get-Content -LiteralPath (Join-Path $dir 'Legacy.json') -Raw | ConvertFrom-Json
$after=Get-Content -LiteralPath (Join-Path $dir 'Depot.json') -Raw | ConvertFrom-Json
$old=@(Import-Csv -LiteralPath $before.Report); $new=@(Import-Csv -LiteralPath $after.Report)
$keys=@('ContainerID','LengthFt','GrossKg','PlannedSlot','STSLane','RMGID','PlannedYardCell','YardSubslot',
    'PlannedYardTier','OutboundOrder','HandlingType','HazardGroup')
function ManifestKeys($rows) {
    @($rows | ForEach-Object { $r=$_; ($keys | ForEach-Object {$r.$_}) -join '|' } | Sort-Object)
}
if($old.Count -ne 120 -or $new.Count -ne 120 -or
    (Compare-Object (ManifestKeys $old) (ManifestKeys $new))) { throw 'Parking comparison requires the same 120 containers and yard plans' }
function Average($rows,$field) { ($rows | ForEach-Object {[double]$_.$field} | Measure-Object -Average).Average }
function Maximum($rows,$field) { ($rows | ForEach-Object {[double]$_.$field} | Measure-Object -Maximum).Maximum }
$result=[ordered]@{
    Basis='same corrected engine, 120 identical cargo/yard plans, same fixed step and environment seed'
    EnvironmentSeed=$EnvironmentSeed;FixedFPS=$FixedFPS;Legacy=$before;Depot=$after
    LastPlacementReductionPercent=100*($before.LastPlacementSeconds-$after.LastPlacementSeconds)/$before.LastPlacementSeconds
    ReturnCompletionReductionPercent=100*($before.ReturnCompletedSeconds-$after.ReturnCompletedSeconds)/$before.ReturnCompletedSeconds
    LegacyTrafficWaitMeanSeconds=(Average $old 'TrafficWaitSeconds');DepotTrafficWaitMeanSeconds=(Average $new 'TrafficWaitSeconds')
    LegacySTSHoldMeanSeconds=(Average $old 'STSWaitingForAGVSeconds');DepotSTSHoldMeanSeconds=(Average $new 'STSWaitingForAGVSeconds')
    LegacySTSHoldMaxSeconds=(Maximum $old 'STSWaitingForAGVSeconds');DepotSTSHoldMaxSeconds=(Maximum $new 'STSWaitingForAGVSeconds')
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $dir 'comparison.json') -Encoding utf8
Write-Output ('PARKING COMPARISON: {0:N1}s -> {1:N1}s; {2:N2}% reduction (negative means slower)' -f
    $before.LastPlacementSeconds,$after.LastPlacementSeconds,$result.LastPlacementReductionPercent)
Write-Output "PARKING_COMPARISON_PASS: $dir"
