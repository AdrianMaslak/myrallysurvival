param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [switch]$Art,
    [switch]$Preview
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
if (Get-ProjectEditors $project) {
    throw 'Save and close this project in Unreal before installing roadside additions.'
}
if ($Art) {
    & 'C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe' --background --factory-startup --python (Join-Path $PSScriptRoot 'build_countryside_roadside.py')
    if ($LASTEXITCODE -ne 0) { throw 'Roadside art generation failed.' }
}
$manifest = Join-Path $root 'Art\Maps\Countryside\Roadside\roadside.json'
if (!(Test-Path -LiteralPath $manifest)) { throw 'Build roadside art first with -Art.' }
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$scriptPath = (Join-Path $PSScriptRoot 'apply_countryside_roadside.py') -replace '\\','/'
$log = Join-Path $root 'Saved\Logs\CountrysideRoadside.log'
$marker = 'COUNTRYSIDE_ROADSIDE_PASS:'
$arguments = @("`"$project`"", '/Engine/Maps/Entry', "-ExecutePythonScript=`"$scriptPath`"", '-unattended', '-nosplash', '-nosound', '-log=CountrysideRoadside.log')
if ($Preview) {
    $scriptPath = (Join-Path $PSScriptRoot 'preview_countryside_roadside.py') -replace '\\','/'
    $log = Join-Path $root 'Saved\Logs\CountrysideRoadsidePreview.log'
    $marker = 'COUNTRYSIDE_ROADSIDE_PREVIEW_PASS:'
    $arguments = @("`"$project`"", '/Game/Maps/Countryside/L_Countryside', "-ExecCmds=`"py $scriptPath`"", '-unattended', '-nosplash', '-nosound', '-log=CountrysideRoadsidePreview.log', '-windowed', '-ResX=1280', '-ResY=720')
}
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
"Roadside authoring PID: $($process.Id)"
if (!$process.WaitForExit(600000)) {
    Stop-Process -Id $process.Id
    throw 'Roadside import exceeded ten minutes. See Saved/Logs/CountrysideRoadside.log.'
}
$lines = Get-Content -LiteralPath $log
$lines | Select-String -Pattern 'LogPython: (Error|Warning)|COUNTRYSIDE_ROADSIDE|Fatal error' | ForEach-Object { $_.Line }
if ($process.ExitCode -ne 0 -or !($lines | Select-String -SimpleMatch $marker)) {
    throw "Roadside installation failed; see $log. The original map backup is under Saved/RoadsideBackups."
}
