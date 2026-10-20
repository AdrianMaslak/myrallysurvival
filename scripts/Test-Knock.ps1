param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 420,
    [string[]]$ExtraArgs = @(),
    # Play sound and record 24 s of the drive (12 s at stock boost, 12 s pinking at 1.4 bar) to
    # Saved\BouncedWavFiles\RallyKnock_audio.wav, then compare the pinking band in the two halves.
    [switch]$WithSound
)
# Engine feedback check, replaying the 2026-10-18 playtest that "blew" the engine with no warning: the
# wastegate at 1.4 bar on the stock computer (it pinks and wears) and all but two of the intake manifold's
# bolts left out (those two work loose until it hangs and the engine stops). Checks the pinking, the blue
# smoke from 90% engine health, the clinks and the hanging intake. Screenshots in
# Saved\Screenshots\WindowsEditor: RallyKnock_intake_before.png (bolted down), RallyKnock_smoke.png and
# RallyKnock_intake_after.png (hanging); the intake shots with the engine cover out of the way.
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
    '-RallyKnockTest', '-unattended') + $(if ($WithSound) { @('-RallyRecordAudio', '-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0') } else { @('-nosound') }) + $ExtraArgs
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw "Knock test timed out after $TimeoutSeconds s."
}
$lines = Get-Content -LiteralPath $log | Select-String -Pattern 'RALLY_KNOCKTEST|RALLY_FIX a fixing|RALLY_ENGINE|Error:|Ensure condition'
$lines | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[\s*\d+\]\w+: Display: ', '' }
if ($process.ExitCode -ne 0 -or ($lines | Select-String 'Error:|Ensure condition') -or !($lines | Select-String -SimpleMatch 'RALLY_KNOCKTEST_DONE result=pass')) {
    throw 'Knock test failed; see Saved\Logs\RallySurvival.log.'
}
if ($WithSound) {
    python (Join-Path $PSScriptRoot 'knock_audio_check.py') (Join-Path $root 'Saved\BouncedWavFiles\RallyKnock_audio.wav')
    if ($LASTEXITCODE -ne 0) { throw 'The pinking is not clear in the recording.' }
}
