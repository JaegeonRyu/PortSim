param(
    [Parameter(Mandatory)][string]$ReportPath,
    [string]$BaselineReport,
    [double]$MaximumHoldSeconds=1200
)
$ErrorActionPreference='Stop'
$rows=@(Import-Csv -LiteralPath $ReportPath)
if(-not $rows.Count) { throw 'Empty dispatch report' }
foreach($row in $rows) {
    if($null -eq $row.STSWaitingForAGVSeconds -or $null -eq $row.DispatchWaitSeconds) { throw 'Missing dispatch wait telemetry' }
    $eligible=[double]$row.DispatchEligibleAtSeconds
    $assigned=[double]$row.AGVAssignedAtSeconds
    $wait=[double]$row.STSWaitingForAGVSeconds
    if($eligible -lt 0 -or $assigned -lt $eligible -or $assigned -gt [double]$row.STSHandoverAtSeconds -or
        [Math]::Abs([double]$row.DispatchWaitSeconds-($assigned-$eligible)) -gt .001 -or
        $wait -lt 0 -or $wait -gt [double]$row.STSSeconds+.001) { throw "Invalid dispatch timing: $($row.ContainerID)" }
    if($wait -ge $MaximumHoldSeconds) { throw "Excessive suspended AGV wait: $($row.ContainerID) $wait seconds" }
}
$maxHold=($rows | ForEach-Object {[double]$_.STSWaitingForAGVSeconds} | Measure-Object -Maximum).Maximum
$meanHold=($rows | ForEach-Object {[double]$_.STSWaitingForAGVSeconds} | Measure-Object -Average).Average
$end=($rows | ForEach-Object {[double]$_.FinalPlacementAtSeconds} | Measure-Object -Maximum).Maximum
Write-Output ('DISPATCH: {0} shipments; suspended AGV wait mean={1:N1}s max={2:N1}s; last placement={3:N1}s' -f $rows.Count,$meanHold,$maxHold,$end)
if($BaselineReport) {
    $baseline=@(Import-Csv -LiteralPath $BaselineReport)
    if($baseline.Count -ne $rows.Count -or (Compare-Object ($baseline.ContainerID | Sort-Object) ($rows.ContainerID | Sort-Object))) {
        throw 'Dispatch comparison requires the exact same cargo IDs'
    }
    $before=($baseline | ForEach-Object {[double]$_.FinalPlacementAtSeconds} | Measure-Object -Maximum).Maximum
    $oldService=($baseline | ForEach-Object {[double]$_.STSSeconds} | Measure-Object -Maximum).Maximum
    $newService=($rows | ForEach-Object {[double]$_.STSSeconds} | Measure-Object -Maximum).Maximum
    Write-Output ('DISPATCH COMPARISON: makespan {0:N1}s -> {1:N1}s ({2:N2}% reduction); max STS service {3:N1}s -> {4:N1}s' -f $before,$end,(100*($before-$end)/$before),$oldService,$newService)
    if($end -ge $before) { throw 'Changed dispatch did not improve this same-cargo baseline' }
}
