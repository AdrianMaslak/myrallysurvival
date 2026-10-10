param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [switch]$OpenEditor
)
$ErrorActionPreference = 'Stop'
$project = Join-Path (Split-Path $PSScriptRoot -Parent) 'RallySurvival.uproject'
$build = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (!(Test-Path -LiteralPath $build)) {
    throw "Unreal Engine 5.8 is missing at $EngineRoot. Install it through Epic Games Launcher, or supply -EngineRoot."
}
& $build RallySurvivalEditor Win64 Development "-Project=$project" -WaitMutex
if ($LASTEXITCODE -ne 0) { throw "Unreal build failed with exit code $LASTEXITCODE." }
if ($OpenEditor) {
    # Visible editor is the interactive tool used to construct and test the vehicle.
    Start-Process -FilePath $editor -ArgumentList ('"' + $project + '"')
}
