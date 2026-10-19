param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 240,
    [string[]]$ExtraArgs = @(),
    # Play sound and record the game's output to Saved\BouncedWavFiles\RallyLeak_audio.wav.
    [switch]$WithSound
)
# Leaks: a cracked sump, a holed radiator and fuel tank on a parked, idling car (-RallyLeakTest). After 25 s
# the puddles under it must hold what was lost (oil and coolant; fuel dries fast) and the engine's block be
# soaked with oil; a short drive then leaves a trail of spots. Screenshots in Saved\Screenshots\WindowsEditor:
# RallyLeak_parked.png (the puddles), RallyLeak_block.png (the oily block, the wet radiator), RallyLeak_trail.png.
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
    '-RallyLeakTest', '-unattended') + $(if ($WithSound) { @('-RallyRecordAudio', '-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0') } else { @('-nosound') }) + $ExtraArgs
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw "Leak test timed out after $TimeoutSeconds s."
}
$lines = Get-Content -LiteralPath $log | Select-String -Pattern 'RALLY_LEAKTEST|Error:|Ensure condition'
$lines | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[\s*\d+\]\w+: Display: ', '' }
if ($process.ExitCode -ne 0 -or ($lines | Select-String 'Error:|Ensure condition') -or !($lines | Select-String -SimpleMatch 'RALLY_LEAKTEST_DONE result=pass')) {
    throw 'Leak test failed; see Saved\Logs\RallySurvival.log.'
}
