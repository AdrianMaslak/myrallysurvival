param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Map = '/Game/Maps/Countryside/L_Countryside',
    [int]$TimeoutSeconds = 240,
    [string[]]$ExtraArgs = @()
)
# The first minute of play as a new player sees it, on foot and touching nothing: screenshots at 1.5, 6,
# 15, 26, 30 and 45 s and of the F1 card in Saved\Screenshots\WindowsEditor\RallyStart_*.png (with the HUD).
# Its own save slot, removed first, so it starts as a new game and leaves the player's save alone.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\RallySurvival.log'
if (Get-ProjectEditors $project) {
    throw 'Close running Unreal Editor instances first.'
}
$slot = Join-Path $root 'Saved\SaveGames\RallyStartTest.sav'
if (Test-Path -LiteralPath $slot) { Remove-Item -LiteralPath $slot }
$arguments = @("`"$project`"", $Map, '-game', '-windowed', '-ResX=1280', '-ResY=720', '-RallyStartShots', '-RallySaveSlot=RallyStartTest', '-nosound', '-unattended') + $ExtraArgs
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw "Start test timed out after $TimeoutSeconds s."
}
$lines = Get-Content -LiteralPath $log | Select-String -Pattern 'RALLY_START|RALLY_QUEST|RALLY_STORY|Error:|Ensure condition'
$lines | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[\s*\d+\]\w+: Display: ', '' }
if (($lines | Select-String -SimpleMatch 'RALLY_START shot').Count -lt 7) {
    throw 'Start test did not take its screenshots; see Saved\Logs\RallySurvival.log.'
}
