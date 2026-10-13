param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 240
)
# The time of day (docs/DAYS.md) on the countryside map: evening, headlamps on, everyone asleep in the car,
# the next morning at 06:30. Screenshots RallyDay_night.png and RallyDay_morning.png.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\DayTest.log'
if (Get-ProjectEditors $project) { throw 'Close Unreal Editor and game instances first.' }
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
$arguments = @(('"' + $project + '"'), '/Game/Maps/Countryside/L_Countryside', '-game', '-windowed', '-ResX=960', '-ResY=540',
    '-nosound', '-unattended', '-RallyAutoSeat=driver', '-RallyDayTest', ('-abslog="' + $log + '"'))
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) { Stop-Process -Id $process.Id; throw 'Day test timed out.' }
$lines = Get-Content -LiteralPath $log
$lines | Select-String 'RALLY_DAY|RALLY_CAR lights|Error:|Ensure condition' | ForEach-Object { $_.Line }
if (!($lines | Select-String 'RALLY_DAYTEST_PASS') -or ($lines | Select-String 'Error:|Ensure condition')) { throw 'Day test failed.' }
