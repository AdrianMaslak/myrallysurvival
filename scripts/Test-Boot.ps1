param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 180
)
# On foot by the car with its boot open: the first-aid kit taken out and G pressed looking into the boot
# must strap it back into its own place (it used to fall through the car); taken out again and G pressed
# looking away, it drops beside the car. Screenshots: Saved\Screenshots\WindowsEditor\RallyBoot_carrying.png
# (its place outlined) and RallyBoot_back.png.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\RallySurvival.log'
if (Get-ProjectEditors $project) {
    throw 'Close running Unreal Editor instances first.'
}
$arguments = @("`"$project`"", '-game', '-windowed', '-ResX=960', '-ResY=540', '-RallyBootTest', '-nosound', '-unattended')
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw "Boot test timed out after $TimeoutSeconds s."
}
$lines = Get-Content -LiteralPath $log | Select-String -Pattern 'RALLY_BOOTTEST|Error:|Ensure condition'
$lines | ForEach-Object { $_.Line -replace '^\[[^\]]+\]\[\s*\d+\]\w+: Display: ', '' }
if ($process.ExitCode -ne 0 -or ($lines | Select-String 'Error:|Ensure condition') -or !($lines | Select-String -SimpleMatch 'RALLY_BOOTTEST_DONE result=pass')) {
    throw 'Boot test failed; see Saved\Logs\RallySurvival.log.'
}
