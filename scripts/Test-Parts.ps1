param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 300,
    [string[]]$ExtraArgs = @(),
    # Instead: every part with fixings photographed from outside with its fixings lit (doors, bonnet and boot
    # open, wheels out of the way), Saved\Screenshots\WindowsEditor\RallyBolts_<slot>.png.
    [switch]$BoltSurvey
)
# Running gear as its damage shows: each corner photographed with its wheel hidden, sound and then damaged
# (front left arm bent 40%, front right arm and driveshaft snapped, rear left disc ruined, rear right strut
# broken, front left tyre flat), and every part's look-at state checked. Screenshots:
# Saved\Screenshots\WindowsEditor\RallyParts_<corner>_sound.png and _damaged.png.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\RallySurvival.log'
if (Get-ProjectEditors $project) {
    throw 'Close running Unreal Editor instances first.'
}
$arguments = @("`"$project`"", '-game', '-windowed', '-ResX=960', '-ResY=540', '-RallyAutoSeat=driver', '-RallyAutoDrive',
    '-RallyPartsTest', '-nosound', '-unattended') + $(if ($BoltSurvey) { @('-RallyBoltSurvey') } else { @() }) + $ExtraArgs
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw "Parts test timed out after $TimeoutSeconds s."
}
$lines = Get-Content -LiteralPath $log | Select-String -Pattern 'RALLY_PARTSTEST|RALLY_BOLTSURVEY|Error:|Ensure condition'
$lines | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[\s*\d+\]\w+: Display: ', '' }
$done = if ($BoltSurvey) { 'RALLY_BOLTSURVEY_DONE' } else { 'RALLY_PARTSTEST_DONE result=pass' }
if ($process.ExitCode -ne 0 -or ($lines | Select-String 'Error:|Ensure condition') -or !($lines | Select-String -SimpleMatch $done)) {
    throw 'Parts test failed; see Saved\Logs\RallySurvival.log.'
}
