param(
    [ValidateRange(1,65535)][int]$Port=4173,
    [switch]$NoBrowser,
    [switch]$Background,
    [string]$NodePath
)
$ErrorActionPreference='Stop'
$repository=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$server=Join-Path $repository 'Dashboard\server.mjs'
function Get-DashboardHealth {
    param([int]$CandidatePort)
    try { Invoke-RestMethod "http://127.0.0.1:$CandidatePort/api/health" -TimeoutSec 1 } catch { $null }
}
function Test-CurrentDashboard {
    param($Health)
    return $Health -and $Health.application -eq 'PortSim Observatory' -and $Health.repository -eq $repository
}
function Test-DashboardFresh {
    param($Health)
    if(-not $Health.source_hashes) { return $false }
    $sources=@{'server.mjs'=$server;'commands.mjs'=(Join-Path $repository 'Dashboard\commands.mjs')}
    foreach($file in Get-ChildItem -LiteralPath (Join-Path $repository 'Dashboard\public') -File) {
        if($file.Extension -in @('.mjs','.css','.html')) { $sources["public/$($file.Name)"]=$file.FullName }
    }
    if($sources.Count -ne @($Health.source_hashes.PSObject.Properties).Count) { return $false }
    foreach($entry in $sources.GetEnumerator()) {
        $remote=$Health.source_hashes.PSObject.Properties[$entry.Key]
        if(-not $remote) { return $false }
        if(([string]$remote.Value) -ne (Get-FileHash -LiteralPath $entry.Value -Algorithm SHA256).Hash.ToLowerInvariant()) { return $false }
    }
    return $true
}
function Test-DashboardPortAvailable {
    param([int]$CandidatePort)
    $listener=[System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback,$CandidatePort)
    try {
        $listener.Server.ExclusiveAddressUse=$true
        $listener.Start()
        return $true
    } catch { return $false } finally { $listener.Stop() }
}
# Explicit -Port is exact. Default launches search for this workspace's server
# before opening a free port, so a second launch never creates another server.
$explicitPort=$PSBoundParameters.ContainsKey('Port')
$candidatePorts=if($explicitPort) { @($Port) } else { @($Port..($Port+19)) }
$freePort=$null
$health=$null
foreach($candidatePort in $candidatePorts) {
    if(Test-DashboardPortAvailable $candidatePort) {
        if($null -eq $freePort) { $freePort=$candidatePort }
        continue
    }
    $candidateHealth=Get-DashboardHealth $candidatePort
    if(Test-CurrentDashboard $candidateHealth) { $Port=$candidatePort; $health=$candidateHealth; break }
}
if($health -and -not (Test-DashboardFresh $health)) {
    $listener=Get-NetTCPConnection -State Listen -LocalPort $Port -ErrorAction SilentlyContinue |
        Where-Object LocalAddress -eq '127.0.0.1' | Select-Object -First 1
    $existing=if($listener) { Get-CimInstance Win32_Process -Filter "ProcessId=$($listener.OwningProcess)" }
    if(-not $existing -or $existing.Name -ne 'node.exe' -or
        [string]::IsNullOrEmpty($existing.CommandLine) -or
        $existing.CommandLine.IndexOf($server,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "Port $Port has an outdated dashboard, but its process could not be safely identified. Close that server and retry."
    }
    Write-Host "Dashboard source changed; restarting the old server on port $Port."
    Stop-Process -Id $existing.ProcessId -Force
    for($attempt=0;$attempt -lt 30 -and -not (Test-DashboardPortAvailable $Port);$attempt++) {
        Start-Sleep -Milliseconds 100
    }
    if(-not (Test-DashboardPortAvailable $Port)) { throw "Port $Port did not become available after stopping the old dashboard." }
    $freePort=$Port
    $health=$null
}
if(-not $health) {
    if($null -eq $freePort) {
        if($explicitPort) { throw "Port $Port is in use by another service. Choose -Port with an unused port, or omit -Port for automatic selection." }
        throw "No available dashboard port in $($candidatePorts[0])-$($candidatePorts[-1]). Choose -Port with an unused port."
    }
    $Port=$freePort
}
$url="http://127.0.0.1:$Port"
if(-not $explicitPort -and $Port -ne 4173) { Write-Host "Port 4173 is in use. Using PortSim dashboard: $url" }
if ($health) {
    Write-Host "PortSim dashboard already running: $url"
    if(-not $NoBrowser) { Start-Process $url }
    return
}
if (-not $NodePath) {
    $command=Get-Command node -ErrorAction SilentlyContinue
    if($command) { $NodePath=$command.Source }
    else {
        $candidates=@(
            (Join-Path $env:ProgramFiles 'nodejs\node.exe'),
            (Join-Path $env:LOCALAPPDATA 'Programs\nodejs\node.exe'),
            (Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe')
        )
        $NodePath=$candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    }
}
if(-not $NodePath -or -not (Test-Path -LiteralPath $NodePath)) {
    throw 'Node.js was not found. Install Node.js or supply -NodePath with the path to node.exe.'
}
$previousPort=$env:PORTSIM_DASHBOARD_PORT
try {
    $env:PORTSIM_DASHBOARD_PORT="$Port"
    if($Background) {
        $logDir=Join-Path $repository 'PortSim\Saved\Logs'
        New-Item -ItemType Directory -Path $logDir -Force | Out-Null
        $process=Start-Process -FilePath $NodePath -ArgumentList @("`"$server`"") -WorkingDirectory $repository -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $logDir "Dashboard-$Port.log") -RedirectStandardError (Join-Path $logDir "Dashboard-$Port-error.log")
        for($attempt=0;$attempt -lt 40;$attempt++) {
            if($process.HasExited) { throw "Dashboard exited. See $logDir\Dashboard-$Port-error.log" }
            $health=Get-DashboardHealth $Port
            if($health) { break }
            Start-Sleep -Milliseconds 250
        }
        if(-not (Test-CurrentDashboard $health) -or -not (Test-DashboardFresh $health)) {
            if(-not $process.HasExited) { $process.Kill() }
            throw "Dashboard did not start correctly. See $logDir\Dashboard-$Port-error.log"
        }
        Write-Host "PortSim dashboard ready: $url (PID $($process.Id))"
        if(-not $NoBrowser) { Start-Process $url }
    } else {
        Write-Host "PortSim dashboard: $url"
        Write-Host 'Keep this window open. Ctrl+C stops the dashboard. Run Unreal simulation to receive actor data.'
        if(-not $NoBrowser) { Start-Process $url }
        & $NodePath $server
        if($LASTEXITCODE -ne 0) { throw "Dashboard exited with code $LASTEXITCODE" }
    }
} finally { $env:PORTSIM_DASHBOARD_PORT=$previousPort }
