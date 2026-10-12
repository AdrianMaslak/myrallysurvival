param(
    [Parameter(Mandatory = $true)][string]$Script,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    # Log line that proves success; the editor exit code does not reflect Python errors.
    [string]$PassMarker
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\RallySurvival.log'
if (!(Test-Path -LiteralPath $editor)) { throw "Unreal Editor is missing at $editor." }
if (Get-ProjectEditors $project) {
    throw 'Close running Unreal Editor instances first; a second editor on this project can conflict on saves.'
}
$scriptPath = (Resolve-Path -LiteralPath $Script).Path -replace '\\', '/'
# Full editor, not -run=pythonscript: level editing APIs need the level editor module.
# -ExecutePythonScript runs once the asset registry is ready, then quits the editor.
$arguments = @("`"$project`"", "-ExecutePythonScript=`"$scriptPath`"", '-unattended', '-nosplash', '-nosound')
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -Wait
$lines = Get-Content -LiteralPath $log
$lines | Select-String -Pattern 'LogPython|Python script executed with errors|Error:' | ForEach-Object { $_.Line }
if ($PassMarker -and !($lines | Select-String -SimpleMatch $PassMarker)) {
    throw "Pass marker '$PassMarker' not found in $log (editor exit code $($process.ExitCode))."
}
if ($lines | Select-String -SimpleMatch 'Python script executed with errors') {
    throw "Python script reported errors; see $log."
}
"Editor exit code: $($process.ExitCode)"
