param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8',[int]$TimeoutSeconds=180)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root=Split-Path $PSScriptRoot -Parent
$project=Join-Path $root 'RallySurvival.uproject'
if (Get-ProjectEditors $project) {throw 'Close Unreal on this project before the crew test.'}
$log=Join-Path $root 'Saved\Logs\CrewTest.log'
$process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList @(
    "`"$project`"",'/Game/Maps/L_GravelLab','-game','-windowed','-ResX=960','-ResY=720',
    '-RallyCrewTest','-nosound','-unattended',"-abslog=`"$log`"") -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds*1000)) {Stop-Process -Id $process.Id;throw 'Crew test timed out.'}
Select-String -LiteralPath $log -Pattern 'RALLY_CREWTEST|Error:|Ensure condition' | ForEach-Object {$_.Line}
if ($process.ExitCode -ne 0 -or !(Select-String -LiteralPath $log -SimpleMatch 'RALLY_CREWTEST_DONE result=pass' -Quiet) -or
    (Select-String -LiteralPath $log -Pattern 'Error:|Ensure condition' -Quiet)) {throw 'Crew test failed.'}
