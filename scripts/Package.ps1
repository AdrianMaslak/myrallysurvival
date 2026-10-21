# Packaged Win64 Development build, zipped to send to a friend (docs/FRIEND-TEST.md). Editor closed.
param([string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
if (Get-ProjectEditors $project) { throw 'Close Unreal on this project before packaging.' }
$out = Join-Path $root 'Packaged'
$uat = Join-Path $EngineRoot 'Engine\Build\BatchFiles\RunUAT.bat'
& $uat BuildCookRun "-project=$project" -noP4 -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive `
    "-archivedirectory=$out" -utf8output -unattended
if ($LASTEXITCODE -ne 0) { throw "Packaging failed with exit code $LASTEXITCODE." }
$game = Join-Path $out 'Windows'
# Steam test app 480 until the game has its own App ID: next to the game's executable, so a fresh unzip finds it.
Set-Content -LiteralPath (Join-Path $game 'RallySurvival\Binaries\Win64\steam_appid.txt') -Value '480' -NoNewline -Encoding ascii
$zip = Join-Path $out ('RallySurvival-{0}.zip' -f (Get-Date -Format 'yyyy-MM-dd-HHmm'))
Compress-Archive -Path (Join-Path $game '*') -DestinationPath $zip -CompressionLevel Optimal
'{0}  {1:N0} MB' -f $zip, ((Get-Item -LiteralPath $zip).Length / 1MB)
