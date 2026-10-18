param(
    [Parameter(Mandatory)][string]$ReportPath,
    [Parameter(Mandatory)][string]$LogPath,
    [switch]$RequirePairedTrip
)
$ErrorActionPreference='Stop'
$rows=@(Import-Csv -LiteralPath $ReportPath)
if(-not $rows.Count) { throw 'Empty stowage report' }
$text=Get-Content -LiteralPath $LogPath -Raw
$byCargo=@{}
$byPad=@{}
foreach($row in $rows) { $byCargo[$row.ContainerID]=$row }
foreach($row in $rows) {
    if($null -eq $row.SlotLengthFt -or [int]$row.SlotLengthFt -ne [int]$row.LengthFt -or
        [int]$row.PlannedYardCell -lt 0 -or
        ([int]$row.LengthFt -eq 20 -and [int]$row.YardSubslot -notin @(1,2)) -or
        ([int]$row.LengthFt -ne 20 -and [int]$row.YardSubslot -ne 0)) {
        throw "Invalid dedicated size cell/pad: $($row.ContainerID)"
    }
    $key="$($row.PlannedYardCell)/$($row.PlannedYardTier)/$($row.YardSubslot)"
    if($byPad.ContainsKey($key)) { throw "Duplicate physical yard pad: $key" }
    $byPad[$key]=$row
}
foreach($row in $rows) {
    if($row.DataBasis -ne 'synthetic_simulation_booking' -or [int]$row.OutboundOrder -notin 1..4 -or
        $row.HandlingType -notin @('dry','reefer') -or $row.HazardGroup -notin @('none','flammable','corrosive')) {
        throw "Missing/invalid booking data: $($row.ContainerID)"
    }
    $block=[int]$row.PlannedYardBlock
    if(($row.HazardGroup -eq 'flammable' -and $block -ne 22) -or
        ($row.HazardGroup -eq 'corrosive' -and $block -ne 23) -or
        ($row.HandlingType -eq 'reefer' -and $block -notin 19..21) -or
        ($row.HazardGroup -eq 'none' -and $row.HandlingType -eq 'dry' -and $block -gt 18)) {
        throw "Cargo in incompatible yard zone: $($row.ContainerID)"
    }
    if($row.BelowContainerID) {
        $lower=$byCargo[$row.BelowContainerID]
        if(-not $lower -or [double]$lower.GrossKg -lt [double]$row.GrossKg -or
            [int]$lower.OutboundOrder -lt [int]$row.OutboundOrder -or $lower.LengthFt -ne $row.LengthFt -or
            $lower.HandlingType -ne $row.HandlingType -or $lower.HazardGroup -ne $row.HazardGroup -or
            [double]$lower.FinalPlacementAtSeconds -gt [double]$row.FinalPlacementAtSeconds) {
            throw "Unsupported/buried/overweight stack: $($row.ContainerID)"
        }
        if([int]$row.LengthFt -eq 20) {
            $otherPad=3-[int]$row.YardSubslot
            $tier=[int]$row.PlannedYardTier-1
            $other=$byPad["$($row.PlannedYardCell)/$tier/$otherPad"]
            if(-not $other -or [double]$other.FinalPlacementAtSeconds -gt [double]$row.FinalPlacementAtSeconds) {
                throw "20ft upper layer placed before both lower pads: $($row.ContainerID)"
            }
        }
    }
}
$twoPadLayers=0
foreach($cell in ($rows|Group-Object PlannedYardCell)) {
    if(@($cell.Group.LengthFt|Sort-Object -Unique).Count -ne 1) { throw "Mixed sizes in yard cell $($cell.Name)" }
    foreach($tier in ($cell.Group|Group-Object PlannedYardTier)) {
        $pads=@($tier.Group)
        if($pads.Count -eq 2) {
            ++$twoPadLayers
            if([int]$pads[0].LengthFt -ne 20 -or
                [Math]::Abs([Math]::Abs([double]$pads[0].PlannedXcm-[double]$pads[1].PlannedXcm)-613.4) -gt .1 -or
                [Math]::Abs([double]$pads[0].PlannedYcm-[double]$pads[1].PlannedYcm) -gt .01 -or
                [Math]::Abs([double]$pads[0].PlannedZcm-[double]$pads[1].PlannedZcm) -gt .01) {
                throw "Overlapping/misaligned two-pad cell $($cell.Name)"
            }
        } elseif($pads.Count -gt 2) { throw "Too many boxes in one yard cell tier: $($cell.Name)" }
    }
}
if(-not $twoPadLayers) { throw 'No completed two-pad 20ft yard layer exercised' }
Write-Output "YARD SIZE CELLS: $twoPadLayers completed two-pad 20ft layers; sizes segregated"
$pairedGroups=@($rows | Where-Object PairedTrip -eq '1' | Group-Object TripID)
# Real service time can make every candidate uneconomic. RequirePairedTrip
# separately requests coverage of a complete two-cargo journey.
if(-not $pairedGroups.Count) {
    if($RequirePairedTrip) { throw 'No completed two-container AGV trip exercised' }
    $declines=[Regex]::Matches($text,'AGV_PAIR_DECLINED: V\d+ productive_sts=([\d.]+) saving=(-?[\d.]+)')
    foreach($decline in $declines) {
        if([double]$decline.Groups[2].Value -ge 30) { throw 'A profitable pair was incorrectly rejected' }
    }
    Write-Output "STOWAGE ECONOMICS: $($declines.Count) candidates declined below the 30-second threshold; completed paired transport not exercised in this run"
}
foreach($group in $pairedGroups) {
    $pair=@($group.Group)
    if($pair.Count -ne 2 -or @($pair.LengthFt|Sort-Object -Unique) -ne '20' -or
        @($pair.AGVID|Sort-Object -Unique).Count -ne 1 -or @($pair.RMGID|Sort-Object -Unique).Count -ne 1 -or
        @($pair.TripCargoOrdinal|Sort-Object -Unique).Count -ne 2 -or
        @($pair|Where-Object { [int]$_.TripCargoOrdinal -notin @(1,2) }).Count -or
        ([double]$pair[0].GrossKg+[double]$pair[1].GrossKg) -gt 65000 -or
        @($pair|Where-Object {[double]$_.EstimatedPairSavingSeconds -lt 30}).Count) {
        throw "Invalid paired trip $($group.Name)"
    }
}
Write-Output "STOWAGE Site: $($pairedGroups.Count) paired 20ft trips; $(@($rows|Where-Object BelowContainerID).Count) supported upper-tier placements"
