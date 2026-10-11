param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 300,
    [string[]]$ExtraArgs = @()
)
# Unattended handling check: runs the game with -RallyAutoDrive and prints the car's log once per second.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\RallySurvival.log'
if (Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue) {
    throw 'Close running Unreal Editor instances first.'
}
$arguments = @("`"$project`"", '-game', '-windowed', '-ResX=960', '-ResY=540', '-RallyAutoDrive', '-nosound', '-unattended') + $ExtraArgs
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw "Drive test timed out after $TimeoutSeconds s."
}
$lines = Get-Content -LiteralPath $log | Select-String -Pattern 'RALLY_AUTODRIVE|Error:|Ensure condition'
$lines | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[\s*\d+\]LogRallyCar: Display: ', '' } |
    Where-Object { $_ -notmatch ' t=\d+\.(?!00)' }
if (!($lines | Select-String -SimpleMatch 'RALLY_AUTODRIVE_DONE')) {
    throw 'Drive test did not finish; see Saved\Logs\RallySurvival.log.'
}
