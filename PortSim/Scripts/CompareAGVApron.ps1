param([string]$Engine='C:\Program Files\Epic Games\UE_5.6',
    [string]$BaselineSummary,
    [int]$EnvironmentSeed=166290927,
    [ValidateRange(1,60)][int]$FixedFPS=5,
    [ValidateRange(1,120)][int]$TimeoutMinutes=120)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$dir=Join-Path $root ('Saved/Tests/ApronComparison_'+(Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Path $dir -Force | Out-Null
if(-not $BaselineSummary) {
    $BaselineSummary=Join-Path $dir 'Previous.json'
    & (Join-Path $PSScriptRoot 'TestSite.ps1') -Engine $Engine -Mode Normal -PlaybackRate 0 `
        -FixedFPS $FixedFPS -TimeoutMinutes $TimeoutMinutes -EnvironmentSeed $EnvironmentSeed `
        -ParkingLayout Depot -ApronLayout Previous -CheckStowage -CheckDispatch -ResultSummaryPath $BaselineSummary
}
$before=Get-Content -LiteralPath $BaselineSummary -Raw | ConvertFrom-Json
$baselineProfile=Get-Content -LiteralPath ($before.Report.Replace('.csv','_profile.json')) -Raw | ConvertFrom-Json
if($baselineProfile.ship_arrival_layout -ne 'applied_bay_row_tier') {
    throw 'Arrival placement changed: regenerate the Previous baseline with current code; old grid-based runs are not comparable'
}
if($before.ParkingLayout -ne 'Depot' -or $before.EnvironmentSeed -ne $EnvironmentSeed -or
    $before.FixedFPS -ne $FixedFPS -or $before.PlaybackRate -ne 0 -or $before.Shipments -ne 120 -or
    ($before.ApronLayout -and $before.ApronLayout -ne 'Previous')) { throw 'Incompatible depot baseline conditions' }
& (Join-Path $PSScriptRoot 'TestSite.ps1') -Engine $Engine -Mode Normal -PlaybackRate 0 `
    -FixedFPS $FixedFPS -TimeoutMinutes $TimeoutMinutes -EnvironmentSeed $EnvironmentSeed `
    -ParkingLayout Depot -ApronLayout Reused -CheckStowage -CheckDispatch -ResultSummaryPath (Join-Path $dir 'Reused.json')
$after=Get-Content -LiteralPath (Join-Path $dir 'Reused.json') -Raw | ConvertFrom-Json
$old=@(Import-Csv -LiteralPath $before.Report); $new=@(Import-Csv -LiteralPath $after.Report)
$fields=@('ContainerID','LengthFt','GrossKg','PlannedSlot','STSLane','RMGID','PlannedYardCell',
    'YardSubslot','PlannedYardTier','OutboundOrder','HandlingType','HazardGroup')
function Keys($rows) { @($rows | ForEach-Object { $r=$_; ($fields | ForEach-Object {$r.$_}) -join '|' } | Sort-Object) }
if($old.Count -ne 120 -or $new.Count -ne 120 -or (Compare-Object (Keys $old) (Keys $new))) {
    throw 'Apron comparison requires identical cargo and yard plans'
}
function Mean($rows,$field) { ($rows | ForEach-Object {[double]$_.$field} | Measure-Object -Average).Average }
$result=[ordered]@{
    Basis='same depot, seed, fixed step and 120 identical cargo/yard plans; freed-area routes, safe recovery and support-path dispatch'
    ReusedBaselineSummary=$BaselineSummary;Previous=$before;Reused=$after
    PlacementReductionPercent=100*($before.LastPlacementSeconds-$after.LastPlacementSeconds)/$before.LastPlacementSeconds
    ReturnReductionPercent=100*($before.ReturnCompletedSeconds-$after.ReturnCompletedSeconds)/$before.ReturnCompletedSeconds
    PreviousTrafficWaitMeanSeconds=(Mean $old 'TrafficWaitSeconds');ReusedTrafficWaitMeanSeconds=(Mean $new 'TrafficWaitSeconds')
    PreviousSTSHoldMeanSeconds=(Mean $old 'STSWaitingForAGVSeconds');ReusedSTSHoldMeanSeconds=(Mean $new 'STSWaitingForAGVSeconds')
}
$result | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $dir 'comparison.json') -Encoding utf8
Write-Output ('APRON COMPARISON: {0:N1}s -> {1:N1}s; {2:N2}% reduction (negative means slower)' -f
    $before.LastPlacementSeconds,$after.LastPlacementSeconds,$result.PlacementReductionPercent)
Write-Output "APRON_COMPARISON_PASS: $dir"
