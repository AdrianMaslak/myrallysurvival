param([string]$EngineRoot='C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root=Split-Path $PSScriptRoot -Parent
$project=Join-Path $root 'RallySurvival.uproject'
if (Get-ProjectEditors $project) { throw 'Save and close this project in Unreal before importing crew assets.' }
$script=(Join-Path $PSScriptRoot 'import_crew.py').Replace('\','/')
$log=Join-Path $root 'Saved\Logs\CrewImport.log'
$process=Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList @(
    "`"$project`"","-ExecutePythonScript=`"$script`"","-abslog=`"$log`"",'-unattended','-nosplash','-nosound') -WindowStyle Hidden -PassThru
if (!$process.WaitForExit(240000)) { Stop-Process -Id $process.Id; throw 'Crew import timed out.' }
Select-String -LiteralPath $log -Pattern 'LogPython: CREW_|LogPython: Error' | ForEach-Object {$_.Line}
if (!(Select-String -LiteralPath $log -SimpleMatch 'CREW_IMPORT_PASS' -Quiet) -or
    (Select-String -LiteralPath $log -SimpleMatch 'Python script executed with errors' -Quiet)) {throw 'Crew import failed.'}
