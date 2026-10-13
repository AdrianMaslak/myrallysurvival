param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$TimeoutSeconds = 180
)
# Money (docs/MONEY.md) on the lab: a rally strut ordered from the catalogue is paid from the kitty, the
# rally turbo after it cannot be afforded, the next morning's post leaves the strut by the service yard,
# and a stage win's prize goes into the kitty.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\ShopTest.log'
if (Get-ProjectEditors $project) { throw 'Close Unreal Editor and game instances first.' }
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
$arguments = @(('"' + $project + '"'), '-game', '-windowed', '-ResX=640', '-ResY=360', '-nosound', '-unattended', '-RallyShopTest',
    ('-abslog="' + $log + '"'))
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) { Stop-Process -Id $process.Id; throw 'Shop test timed out.' }
$lines = Get-Content -LiteralPath $log
$lines | Select-String 'RALLY_MONEY|RALLY_SHOPTEST|Error:|Ensure condition' | ForEach-Object { $_.Line }
if (!($lines | Select-String 'RALLY_SHOPTEST_PASS') -or ($lines | Select-String 'Error:|Ensure condition')) { throw 'Shop test failed.' }
