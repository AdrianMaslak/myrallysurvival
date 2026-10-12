param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 240,
    [string[]]$ExtraArgs = @(),
    # Play sound and record the game's output to Saved\BouncedWavFiles\RallyUnderside_audio.wav.
    [switch]$WithSound
)
# Underside check: a ridge of rock between the wheels, driven over at about 35 km/h; the floor hits must
# reach the sump, gearbox, exhaust or tank without denting the body. Then the car idles 20 s to show what
# leaks. Screenshot: Saved\Screenshots\WindowsEditor\RallyUnderside_after.png.
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
    '-RallyUndersideTest', '-unattended') + $(if ($WithSound) { @('-RallyRecordAudio', '-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0') } else { @('-nosound') }) + $ExtraArgs
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw "Underside test timed out after $TimeoutSeconds s."
}
$lines = Get-Content -LiteralPath $log | Select-String -Pattern 'RALLY_UNDERSIDE|RALLY_ENGINE|RALLY_DAMAGE|Error:|Ensure condition'
$lines | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[\s*\d+\]\w+: Display: ', '' }
if ($process.ExitCode -ne 0 -or ($lines | Select-String 'Error:|Ensure condition') -or !($lines | Select-String -SimpleMatch 'RALLY_UNDERSIDETEST_DONE result=pass')) {
    throw 'Underside test failed; see Saved\Logs\RallySurvival.log.'
}
