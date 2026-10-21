param([string]$Engine='C:\Program Files\Epic Games\UE_5.6')
$ErrorActionPreference='Stop'
$projectRoot=Split-Path $PSScriptRoot -Parent
$testRoot=Join-Path $projectRoot 'Saved/Tests/VesselOperations'
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
$settingsPath=Join-Path $testRoot 'settings.json'
$testLog=Join-Path $testRoot ('run_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'.log')
$settings=@{
    ships=1; cargo_1=2; file_mode_1=$true; selected_input_1=0
    input_1_1_name='test_stowage.csv'
    input_1_1_csv="container_id,bay,row,tier`nDISCHARGE001,1,1,82`nRETAIN001,3,2,82`n"
    input_1_0_name='test_operations.csv'
    input_1_0_csv="container_id,operation,bay,row,tier`nDISCHARGE001,DISCHARGE,1,1,82`nLOAD002,LOAD,5,1,84`nLOAD001,LOAD,5,1,82`n"
}
$settings | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding utf8
$editor=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$project=Join-Path $projectRoot 'PortSim.uproject'
$arguments=@("`"$project`"",'/Engine/Maps/Entry','-game','-nullrhi','-nosound','-unattended','-nosplash',
    '-benchmark','-fps=10','-PortSimSiteTest','-PortSimOperationsTest',
    '-PortSimNoSea','-PortSimNoWind','-PortSimNoRain',"`"-PortSimSettings=$settingsPath`"","`"-abslog=$testLog`"")
$process=Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
$null=$process.Handle
Write-Output "Operations integration log: $testLog"
$deadline=(Get-Date).AddMinutes(20)
while(-not $process.WaitForExit(1000)) {
    if((Get-Date) -gt $deadline) { $process.Kill(); throw "Operations integration timed out: $testLog" }
}
$process.Refresh()
$content=Get-Content -LiteralPath $testLog -Raw
if($process.ExitCode -ne 0 -or $content -notmatch 'PORTSIM_OPERATIONS_PASS: discharged=1 loaded=2 reset_verified=1') {
    Select-String -LiteralPath $testLog -Pattern 'FAIL|Error:|SITE_LOADED|SITE_HANDOVER|WORKING_CRANE' | Select-Object -Last 20
    throw "Operations integration failed: $testLog"
}
if($content.IndexOf('SITE_LOADED: LOAD001') -gt $content.IndexOf('SITE_LOADED: LOAD002')) {
    throw 'Upper container loaded before its support'
}
Write-Output 'PASS: stowage plus operations, discharge, two export loads, and reset'
