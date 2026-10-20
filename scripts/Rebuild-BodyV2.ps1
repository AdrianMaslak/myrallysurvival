# Everything after pulling work done without Unreal at hand (docs/OCTAVIA-VEHICLE.md, Rebuilding the v2 body): build
# the code, write the game textures, export the v2 body and interior from Blender, and import the body, the engine and
# the suspension into Unreal. Close the Unreal Editor first.
#   powershell -ExecutionPolicy Bypass -File .\scripts\Rebuild-BodyV2.ps1
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Blender = 'C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe',
    # A Python 3 with numpy and Pillow; found on its own if left out (py, python, then Blender's own with Pillow added).
    [string]$Python = '',
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

if (!$SkipBuild) {
    '== Build'
    & (Join-Path $PSScriptRoot 'Build.ps1') -EngineRoot $EngineRoot
}

'== Game textures (scripts/build_game_textures.py)'
function Test-Python([string]$Exe, [string[]]$Pre = @()) {
    try { & $Exe @Pre -c 'import numpy, PIL' 2>$null; return $LASTEXITCODE -eq 0 } catch { return $false }
}
$pre = @()
if (!$Python) {
    if (Get-Command py -ErrorAction SilentlyContinue) { if (Test-Python 'py' @('-3')) { $Python = 'py'; $pre = @('-3') } }
    if (!$Python -and (Get-Command python -ErrorAction SilentlyContinue) -and (Test-Python 'python')) { $Python = 'python' }
    if (!$Python) {
        # Blender's own Python has numpy; Pillow goes in once.
        $bundled = Get-ChildItem -Path (Split-Path $Blender) -Recurse -Filter python.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match '\\python\\bin\\python\.exe$' } | Select-Object -First 1
        if (!$bundled) { throw 'No Python 3 with numpy and Pillow found; pass -Python, or: py -m pip install numpy pillow' }
        $Python = $bundled.FullName
        if (!(Test-Python $Python)) { & $Python -m pip install --user pillow; if ($LASTEXITCODE -ne 0) { throw 'Could not install Pillow.' } }
    }
}
& $Python @pre (Join-Path $PSScriptRoot 'build_game_textures.py')
if ($LASTEXITCODE -ne 0) { throw 'build_game_textures.py failed.' }

'== Blender export (scripts/export_body_v2.py)'
if (!(Test-Path -LiteralPath $Blender)) { throw "Blender is missing at $Blender; pass -Blender." }
# Blender writes warnings to stderr, which would stop the script under 'Stop'.
$ErrorActionPreference = 'Continue'
$output = & $Blender --background --factory-startup --python (Join-Path $PSScriptRoot 'export_body_v2.py') 2>&1
$ErrorActionPreference = 'Stop'
$output | Select-String -Pattern 'INTERIOR_DETAILS|EXPORT_DONE|Error|Traceback' | ForEach-Object { $_.Line }
if (!($output | Select-String -SimpleMatch 'EXPORT_DONE')) { throw 'The Blender export did not finish.' }

'== Unreal imports'
$invoke = Join-Path $PSScriptRoot 'Invoke-EditorPython.ps1'
& $invoke -Script (Join-Path $PSScriptRoot 'import_body_v2.py') -PassMarker 'BODYV2_IMPORT_PASS' -EngineRoot $EngineRoot
& $invoke -Script (Join-Path $PSScriptRoot 'import_engine_18t.py') -PassMarker 'ENGINE18T_IMPORT_PASS' -EngineRoot $EngineRoot
& $invoke -Script (Join-Path $PSScriptRoot 'import_suspension_v2.py') -PassMarker 'SUSPV2_IMPORT_PASS' -EngineRoot $EngineRoot

'== Done. Commit the generated textures_game folders, the Export FBX files and the Content assets, then run'
'   scripts\Test-Rules.cmd, Test-Drive.ps1, Test-Day.ps1, Test-Crew.ps1 and Test-Panels.ps1.'
