param([string]$Engine = 'C:\Program Files\Epic Games\UE_5.6',
    [ValidateSet('All','Normal','Full','Flow','Faults','SensorFault','LockFault','Overload','AGVFault')][string]$Mode = 'All',
    [ValidateRange(1,60)][int]$FixedFPS = 10,
    # 0 keeps the repeatable unpaced fixed-step benchmark; 4 is normal test playback.
    [ValidateSet(0,1,2,4,8,16)][int]$PlaybackRate = 4,
    [ValidateRange(1,120)][int]$TimeoutMinutes = 30,
    [switch]$CheckStowage, [switch]$RequirePairedTrip, [switch]$Profile,
    [switch]$CheckDispatch, [string]$DispatchBaselineReport,
    [ValidateSet('Depot','Legacy')][string]$ParkingLayout='Depot',
    [ValidateSet('Reused','Previous')][string]$ApronLayout='Reused',
    [int]$EnvironmentSeed=166290927,
    [ValidateRange(0,1)][double]$Wetness=0,
    [ValidateRange(0,1)][double]$Snow=0,
    [ValidateRange(0,1)][double]$Ice=0,
    [string]$ResultSummaryPath)
$ErrorActionPreference = 'Stop'
if($RequirePairedTrip) { $CheckStowage=$true }
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'PortSim.uproject'
$editor = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$cases = if ($Mode -eq 'All') { @('Normal','SensorFault','LockFault','Overload','AGVFault') }
    elseif ($Mode -eq 'Faults') { @('SensorFault','LockFault','Overload','AGVFault') } else { @($Mode) }
foreach ($case in $cases) {
    $started = Get-Date
    # A terminated Unreal process can leave the previous log handle pending in
    # Windows for a short time. Use a per-run file so that stale handles never
    # prevent the next verification from starting.
    $testLog = Join-Path $root "Saved\Logs\SiteTest_${case}_${ParkingLayout}_${ApronLayout}_$PID.log"
    if (Test-Path -LiteralPath $testLog) { Remove-Item -LiteralPath $testLog -Force }
    $arguments = @("`"$project`"", '/Engine/Maps/Entry','-game','-nullrhi','-nosound','-unattended','-nosplash','-benchmark',"-fps=$FixedFPS",'-PortSimSiteTest',"`"-abslog=$testLog`"")
    if($PlaybackRate -gt 0) { $arguments += "-PortSimPlayback=$PlaybackRate" }
    if($Profile) { $arguments += '-PortSimProfile' }
    $culture=[Globalization.CultureInfo]::InvariantCulture
    $arguments += @("-PortSimWetness=$($Wetness.ToString($culture))","-PortSimSnow=$($Snow.ToString($culture))","-PortSimIce=$($Ice.ToString($culture))")
    $arguments += @("-PortSimEnvironmentSeed=$EnvironmentSeed",'-PortSimShips=3',
        '-PortSimCargo1=200','-PortSimCargo2=200','-PortSimCargo3=200')
    if($ParkingLayout -eq 'Legacy') { $arguments += '-PortSimLegacyParking' }
    if($ApronLayout -eq 'Previous') { $arguments += '-PortSimPreviousApron' }
    if($case -eq 'Full') { $arguments += '-PortSimFullUnloadTest' }
    if($case -eq 'Flow') { $arguments += @('-PortSimContinuousTrafficTest','-PortSimSiteShipmentLimit=240') }
    $expectedShipments=if($case -eq 'Full') {600} elseif($case -eq 'Flow') {240} else {120}
    $expected = $null
    if ($case -eq 'SensorFault') { $arguments += '-PortSimSTSSensorFault'; $expected = 'Required STS sensor observation invalid/stale' }
    if ($case -eq 'LockFault') { $arguments += '-PortSimSTSLockFault=0'; $expected = 'Twist lock alignment failed' }
    if ($case -eq 'AGVFault') { $arguments += '-PortSimSTSAGVFault'; $expected = 'AGV alignment recovery timed out during STS handover' }
    if ($case -eq 'Overload') {
        $settings = Get-Content -LiteralPath (Join-Path $root 'Config\STS_Simulation.json') -Raw | ConvertFrom-Json
        $settings.default_container_mass_kg = 100000
        $dir = Join-Path $root 'Saved\Tests\STS'
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
        $path = Join-Path $dir 'site_overload.json'
        $settings | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath $path -Encoding utf8
        $arguments += "`"-PortSimSTSSettings=$path`""
        $expected = 'Payload exceeds resolved STS reference capacity'
    }
    Write-Output "RUN Site $case (parking=$ParkingLayout seed=$EnvironmentSeed playback=$PlaybackRate; 0=unpaced benchmark)"
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $null = $process.Handle
    $timeoutMs = if ($case -in @('Normal','Full','Flow')) { $TimeoutMinutes*60000 } else { 900000 }
    if (-not $process.WaitForExit($timeoutMs)) { $process.Kill(); throw "Site $case timed out" }
    $process.Refresh()
    $text = Get-Content -LiteralPath $testLog -Raw
    if ($expected) {
        if ($process.ExitCode -notin @(0,1) -or $text -notmatch ('PORTSIM_SITE_FAIL:.*' + [Regex]::Escape($expected)) -or
            $text -notmatch 'FPlatformMisc::RequestExitWithStatus\(0, 1,' -or $text -match 'SITE_HANDOVER:') {
            throw "Site $case did not reject the fault before handover. See $testLog"
        }
    } else {
        if ($process.ExitCode -ne 0 -or $text -notmatch 'PORTSIM_SITE_PASS:') {
            Select-String -LiteralPath $testLog -Pattern 'FAIL|WORKING_CRANE|Fatal' | Select-Object -Last 12
            throw "Site test failed. See $testLog"
        }
        $completed = 0
        $reports = Get-ChildItem -LiteralPath (Join-Path $root 'Saved\Results') -Filter 'Site_*.csv' | Where-Object { $_.LastWriteTime -ge $started }
        foreach ($report in $reports) {
            $rows = @(Import-Csv -LiteralPath $report.FullName)
            if ($rows.Count -lt $expectedShipments -or ($case -ne 'Flow' -and $rows.Count -ne $expectedShipments)) { continue }
            $completed++
            if (@($rows.STSLane | Sort-Object -Unique).Count -ne 9) { throw 'Not all nine STSs completed shipments' }
            if (@($rows.AGVID | Sort-Object -Unique).Count -lt 9) { throw 'Cost-aware dispatch did not sustain concurrent STS service' }
            if (@($rows | Where-Object { [int]$_.STSLane -lt 1 -or [int]$_.STSLane -gt 9 -or [int]$_.RMGID -lt 1 -or [int]$_.RMGID -gt 46 }).Count) { throw 'Invalid crane ID in shipment report' }
            if (@($rows.LengthFt | Sort-Object -Unique).Count -ne 3) { throw '20/40/45 ft manifest coverage missing' }
            if (@($rows.PlannedSlot | Sort-Object -Unique).Count -ne $rows.Count) { throw 'Planned yard destinations are not unique' }
            foreach ($row in $rows) {
                $duration = [double]$row.FinalPlacementAtSeconds - [double]$row.StartedAtSeconds
                if ([Math]::Abs($duration - [double]$row.ShipmentSeconds) -gt .0001 -or
                    [double]$row.STSHandoverAtSeconds -lt [double]$row.StartedAtSeconds -or
                    [double]$row.FinalPlacementAtSeconds -lt [double]$row.STSHandoverAtSeconds -or
                    [double]$row.STSSeconds -gt [double]$row.ShipmentSeconds -or
                    [double]$row.GrossKg -ne ([double]$row.TareKg+[double]$row.CargoKg) -or
                    [int]$row.LengthFt -notin @(20,40,45) -or [int]$row.PlannedYardBlock -lt 1 -or [int]$row.PlannedYardBlock -gt 23) {
                    throw "Site timing/mass mismatch: $($row.ContainerID)"
                }
            }
            if($CheckStowage) {
                & (Join-Path $PSScriptRoot 'AssertStowageReport.ps1') -ReportPath $report.FullName -LogPath $testLog -RequirePairedTrip:$RequirePairedTrip
            }
            if($CheckDispatch -or $DispatchBaselineReport) {
                & (Join-Path $PSScriptRoot 'AssertDispatchReport.ps1') -ReportPath $report.FullName -BaselineReport $DispatchBaselineReport
            }
            $snapshot = Get-Content -LiteralPath ($report.FullName.Replace('.csv','_profile.json')) -Raw | ConvertFrom-Json
            if ($snapshot.sts_profile.reference.geometry.rail_gauge.value -ne 30.48) { throw 'Site reference snapshot missing' }
            if($snapshot.ship_arrival_layout -ne 'applied_bay_row_tier' -or
                @([regex]::Matches($text,'SHIP_ARRIVAL_STOWAGE: vessel=\d applied=200 ')).Count -ne 3) {
                throw 'Physical arrival plans for all three 200-container vessels were not applied'
            }
            $expectedParking=if($ParkingLayout -eq 'Depot') {'depot'} else {'legacy_distributed'}
            if($snapshot.agv_parking_layout -ne $expectedParking) { throw 'Parking baseline switch not applied' }
            $expectedApron=if($ParkingLayout -eq 'Depot' -and $ApronLayout -eq 'Reused') {'reused_transport_lanes'} else {'previous_routes'}
            if($snapshot.agv_apron_layout -ne $expectedApron) { throw 'Apron route switch not applied' }
            $expectedPriority=if($expectedApron -eq 'reused_transport_lanes') {'yard_support_path'} else {'oldest_ready'}
            if($snapshot.agv_dispatch_priority -ne $expectedPriority) { throw 'Yard support dispatch priority not applied' }
            if($expectedApron -eq 'reused_transport_lanes' -and $text -notmatch 'AGV_APRON_ROUTE:') {
                throw 'Loaded AGVs did not use the converted apron roads'
            }
            if($ResultSummaryPath) {
                $lastPlacement=($rows | ForEach-Object {[double]$_.FinalPlacementAtSeconds} | Measure-Object -Maximum).Maximum
                $returnTime=[regex]::Match($text,'RECEIVING_METRICS:.*elapsed_simulation=([0-9.]+)').Groups[1].Value
                [ordered]@{ParkingLayout=$ParkingLayout;ApronLayout=$ApronLayout;EnvironmentSeed=$EnvironmentSeed;PlaybackRate=$PlaybackRate;
                    FixedFPS=$FixedFPS;Shipments=$rows.Count;Report=$report.FullName;Log=$testLog;
                    LastPlacementSeconds=$lastPlacement;ContinuousTraffic=($case -eq 'Flow');
                    ProgressReachedSeconds=[double]$returnTime;
                    ReturnCompletedSeconds=$(if($case -eq 'Flow') {$null} else {[double]$returnTime})} |
                    ConvertTo-Json | Set-Content -LiteralPath $ResultSummaryPath -Encoding utf8
            }
            Write-Output "TIMING Site: 9 STSs, $expectedShipments shipments; final placement $($rows[-1].FinalPlacementAtSeconds) seconds"
        }
        if ($completed -ne 1) { throw "Expected one completed $expectedShipments-shipment report" }
    }
    Write-Output "PASS Site $case : $testLog"
}
