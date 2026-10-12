param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 240
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\PanelTest.log'
if (Get-ProjectEditors $project) { throw 'Close Unreal Editor and game instances first.' }
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
$arguments = @(('"' + $project + '"'), '-game', '-windowed', '-ResX=960', '-ResY=540', '-nosound', '-unattended',
    '-RallyPanelTest', '-RallyAutoSeat=driver', ('-abslog="' + $log + '"'))
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw 'Panel test timed out.'
}
$lines = Get-Content -LiteralPath $log
$lines | Select-String 'RALLY_PANEL|Error:|Ensure condition' | ForEach-Object { $_.Line }
if ($process.ExitCode -ne 0 -or !($lines | Select-String 'RALLY_PANELTEST_CLIENT_PASS') -or
    ($lines | Select-String 'RALLY_PANELTEST.*FAIL|Error:|Ensure condition')) { throw 'Panel test failed.' }
