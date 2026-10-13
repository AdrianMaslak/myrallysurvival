param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 180
)
# Saving and loading the team (docs/SAVING.md), in two runs on the lab: the first puts the car in a known
# state (damage, a dent, a panel off and lying about, a loose bolt, a rally strut's settings, boost, coolant,
# moved) and saves it; the second loads it at the start and compares. Uses its own save slot.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (Get-ProjectEditors $project) { throw 'Close Unreal Editor and game instances first.' }
Remove-Item -LiteralPath (Join-Path $root 'Saved\SaveGames\RallyTeamTest.sav') -ErrorAction SilentlyContinue
foreach ($step in 'write', 'check') {
    $log = Join-Path $root "Saved\Logs\SaveTest_$step.log"
    Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
    $arguments = @(('"' + $project + '"'), '-game', '-windowed', '-ResX=640', '-ResY=360', '-nosound', '-unattended',
        "-RallySaveTest=$step", '-RallySaveSlot=RallyTeamTest', ('-abslog="' + $log + '"'))
    $process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$process.WaitForExit($TimeoutSeconds * 1000)) { Stop-Process -Id $process.Id; throw "Save test ($step) timed out." }
    $lines = Get-Content -LiteralPath $log
    $lines | Select-String 'RALLY_SAVE|Error:|Ensure condition' | ForEach-Object { $_.Line }
    if ($lines | Select-String 'Error:|Ensure condition') { throw "Save test ($step) logged errors." }
}
if (!(Get-Content (Join-Path $root 'Saved\Logs\SaveTest_check.log') | Select-String 'RALLY_SAVETEST_CHECK PASS')) { throw 'Save test failed.' }
