param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 180
)
# Leaning past the car's parts: crouched beside the front left wheel, leaning in must take the eyes past
# the wing's outer face (the head used to stop short of the car's parts) and still reach bolts from there;
# the log lists which of the corner's fixings the crosshair lands on standing straight and leaning.
# Screenshots: Saved\Screenshots\WindowsEditor\RallyLean_straight.png and RallyLean_leaning.png.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\RallySurvival.log'
if (Get-ProjectEditors $project) {
    throw 'Close running Unreal Editor instances first.'
}
$arguments = @("`"$project`"", '-game', '-windowed', '-ResX=960', '-ResY=540', '-RallyLeanTest', '-nosound', '-unattended')
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw "Lean test timed out after $TimeoutSeconds s."
}
$lines = Get-Content -LiteralPath $log | Select-String -Pattern 'RALLY_LEANTEST|Error:|Ensure condition'
$lines | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[\s*\d+\]\w+: Display: ', '' }
if ($process.ExitCode -ne 0 -or ($lines | Select-String 'Error:|Ensure condition') -or !($lines | Select-String -SimpleMatch 'RALLY_LEANTEST_DONE result=pass')) {
    throw 'Lean test failed; see Saved\Logs\RallySurvival.log.'
}
