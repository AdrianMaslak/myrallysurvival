param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 420,
    [string[]]$ExtraArgs = @()
)
# Cooling check: the crash test's barrier hit crushes the radiator; the car then drives circles until the
# coolant has leaked away and the engine has overheated and started to wear, stops, switches off, stands
# and takes five litres of cold water. Screenshots: Saved\Screenshots\WindowsEditor\RallyEngine_*.png.
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
    '-RallyCrashTest', '-RallyEngineTest', '-nosound', '-unattended') + $ExtraArgs
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw "Engine test timed out after $TimeoutSeconds s."
}
$lines = Get-Content -LiteralPath $log | Select-String -Pattern 'RALLY_ENGINE|RALLY_CRASHTEST impact_kmh|Error:|Ensure condition'
$lines | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[\s*\d+\]\w+: Display: ', '' }
if ($process.ExitCode -ne 0 -or ($lines | Select-String 'Error:|Ensure condition') -or !($lines | Select-String -SimpleMatch 'RALLY_ENGINETEST_DONE result=pass')) {
    throw 'Engine test failed; see Saved\Logs\RallySurvival.log.'
}
