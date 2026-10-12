param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [switch]$Publish,
    [switch]$Preview,
    [switch]$Drive
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$staging = Join-Path $root 'Saved\CountrysideAuthoring'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$manifest = Join-Path $root 'Art\Maps\Countryside\Export\countryside.json'
if (!(Test-Path -LiteralPath $manifest)) { throw 'Run build_countryside_art.py in Blender first.' }
New-Item -ItemType Directory -Path $staging -Force | Out-Null
if (Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -like '*CountrysideAuthoring*' }) {
    throw 'The isolated countryside authoring editor is already running.'
}
$project = Join-Path $staging 'RallySurvival.uproject'
Copy-Item -LiteralPath (Join-Path $root 'RallySurvival.uproject') -Destination $project -Force
foreach ($folder in @('Config','Binaries')) {
    Copy-Item -LiteralPath (Join-Path $root $folder) -Destination $staging -Recurse -Force
}
# Snapshot game content for spawn/play tests. Do not copy generated countryside content back into staging.
New-Item -ItemType Directory -Path (Join-Path $staging 'Content') -Force | Out-Null
foreach ($folder in @('Vehicles','Physics','Input')) {
    $source = Join-Path $root "Content\$folder"
    if (Test-Path -LiteralPath $source) { Copy-Item -LiteralPath $source -Destination (Join-Path $staging 'Content') -Recurse -Force }
}
$scriptPath = (Join-Path $root 'scripts\import_countryside.py') -replace '\\','/'
$log = Join-Path $staging 'Saved\Logs\CountrysideBuild.log'
$marker = 'COUNTRYSIDE_IMPORT_PASS'
$arguments = @("`"$project`"", '/Engine/Maps/Entry', "-ExecutePythonScript=`"$scriptPath`"", '-unattended', '-nosplash', '-nosound', '-log=CountrysideBuild.log')
if ($Preview) {
    $scriptPath = (Join-Path $root 'scripts\preview_countryside.py') -replace '\\','/'
    $arguments = @("`"$project`"", '/Game/Maps/Countryside/L_Countryside', "-ExecCmds=`"py $scriptPath`"", '-unattended', '-nosplash', '-nosound', '-log=CountrysidePreview.log', '-windowed', '-ResX=1280', '-ResY=720')
    $log = Join-Path $staging 'Saved\Logs\CountrysidePreview.log'
    $marker = 'COUNTRYSIDE_PREVIEW_PASS'
}
if ($Drive) {
    $arguments = @("`"$project`"", '/Game/Maps/Countryside/L_Countryside', '-game', '-windowed', '-ResX=960', '-ResY=540', '-RallyAutoSeat=driver', '-RallyQuitAfter=25', '-unattended', '-nosound', '-log=CountrysideDrive.log')
    $log = Join-Path $staging 'Saved\Logs\CountrysideDrive.log'
    $marker = ''
}
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
"Countryside authoring PID: $($process.Id)"
if (!$process.WaitForExit(900000)) {
    Stop-Process -Id $process.Id
    throw 'Isolated countryside editor exceeded 15 minutes.'
}
$lines = Get-Content -LiteralPath $log
$lines | Select-String -Pattern 'LogPython: (Error|Warning)|COUNTRYSIDE_|RALLY_AUTOSEAT|Fatal error' | ForEach-Object { $_.Line }
if ($process.ExitCode -ne 0) { throw "Authoring editor failed: $($process.ExitCode). See $log" }
if ($marker -and !($lines | Select-String -SimpleMatch $marker)) { throw "Missing $marker. See $log" }
if ($lines | Select-String -SimpleMatch 'Python script executed with errors') { throw "Python error. See $log" }
if ($Drive) {
    if (!($lines | Select-String -Pattern 'RALLY_SEAT enter .* seat=0')) { throw 'No successful driver seat entry in the game log.' }
    if (!($lines | Select-String -SimpleMatch 'Bringing World /Game/Maps/Countryside/L_Countryside')) { throw 'Smoke test loaded the wrong map.' }
    $shot = Join-Path $staging 'Saved\Screenshots\WindowsEditor\RallySeat_driver.png'
    if ((Test-Path -LiteralPath $shot) -and (Get-Item -LiteralPath $shot).LastWriteTime -ge $process.StartTime) {
        $previewDir = Join-Path $root 'Art\Maps\Countryside\Preview'
        New-Item -ItemType Directory -Path $previewDir -Force | Out-Null
        Copy-Item -LiteralPath $shot -Destination (Join-Path $previewDir 'Driver.png') -Force
    }
    'COUNTRYSIDE_SPAWN_PASS: game loaded, player walked to the car, and driver seat entry was confirmed in the runtime log.'
}
if ($Publish) {
    $source = Join-Path $staging 'Content\Maps\Countryside'
    $destination = Join-Path $root 'Content\Maps\Countryside'
    if (Test-Path -LiteralPath $destination) { throw 'Countryside already exists in the live project. Review before replacing it.' }
    if ($Preview -or $Drive) { throw 'Publish only after the build and collision validation pass.' }
    Copy-Item -LiteralPath $source -Destination $destination -Recurse
    'COUNTRYSIDE_PUBLISH_PASS: new map folder copied to the live project; defaults unchanged.'
}
