param([switch]$Art, [switch]$Validate, [switch]$Preview, [switch]$Drive)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
if (Get-Process UnrealEditor,UnrealEditor-Cmd,RallySurvival -ErrorAction SilentlyContinue) { throw 'Close the running game/editor first.' }
if ($Art) {
    & 'C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe' --background --factory-startup --python (Join-Path $PSScriptRoot 'build_highlands_art.py')
    if ($LASTEXITCODE -ne 0) { throw 'Art build failed.' }
}
$script = 'import_highlands.py'
$marker = 'HIGHLANDS_BUILD_PASS'
if ($Validate) { $script = 'validate_highlands.py'; $marker = 'HIGHLANDS_VALIDATE_PASS' }
if ($Preview) { $script = 'preview_highlands.py'; $marker = 'HIGHLANDS_PREVIEW_PASS' }
$project = Join-Path $root 'RallySurvival.uproject'
$scriptPath = (Join-Path $PSScriptRoot $script).Replace('\','/')
$map = '/Game/Maps/Highlands/L_Highlands'
$arguments = @("`"$project`"", '/Engine/Maps/Entry', "-ExecutePythonScript=`"$scriptPath`"", '-unattended', '-nosplash', '-nosound', '-log=Highlands.log')
if ($Preview) { $arguments = @("`"$project`"", $map, "-ExecCmds=`"py $scriptPath`"", '-unattended', '-nosplash', '-nosound', '-log=Highlands.log') }
if ($Drive) { $arguments = @("`"$project`"", $map, '-game', '-windowed', '-ResX=1280', '-ResY=720', '-RallyAutoSeat=driver', '-RallyAutoDrive', '-RallyRoadTest', '-RallyQuitAfter=45', '-unattended', '-nosound', '-log=Highlands.log'); $marker='RALLY_SEAT enter' }
if (!$Validate -and !$Preview -and !$Drive) {
    $mapFile = Join-Path $root 'Content\Maps\Highlands\L_Highlands.umap'
    if (Test-Path -LiteralPath $mapFile) {
        $backup = Join-Path $root ('Saved\HighlandsBackups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        Copy-Item -LiteralPath $mapFile -Destination $backup
    }
}
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
"Highlands PID: $($process.Id)"
if (!$process.WaitForExit(1200000)) { Stop-Process -Id $process.Id; throw 'Highlands authoring timed out.' }
$log = Join-Path $root 'Saved\Logs\Highlands.log'
$lines = Get-Content -LiteralPath $log
$lines | Select-String -Pattern 'HIGHLANDS_(?!TYRE_RUT)|LogPython: Error|Fatal error|RALLY_SEAT enter' | ForEach-Object { $_.Line }
if ($process.ExitCode -ne 0 -or !($lines | Select-String -SimpleMatch $marker)) { throw "Highlands failed. See $log" }
if ($Drive) {
    foreach ($check in @('HIGHLANDS_RUNTIME_MUD_PASS','HIGHLANDS_RUNTIME_DEBRIS_PASS','HIGHLANDS_TYRE_RUT')) {
        if (!($lines | Select-String -SimpleMatch $check)) { throw "Missing runtime check $check" }
    }
    Copy-Item -LiteralPath $log -Destination (Join-Path $root 'Art\Maps\Highlands\runtime.log') -Force
}
