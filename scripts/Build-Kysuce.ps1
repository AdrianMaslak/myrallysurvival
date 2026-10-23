# Builds /Game/Maps/Kysuce/L_Kysuce from Art/Maps/Kysuce (docs/KYSUCE.md). Run scripts\kysuce_layout.py first
# when the route, surfaces or land use change.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
if (Get-Process UnrealEditor,UnrealEditor-Cmd,RallySurvival -ErrorAction SilentlyContinue) { throw 'Close the running game/editor first.' }
$project = Join-Path $root 'RallySurvival.uproject'
$scriptPath = (Join-Path $PSScriptRoot 'import_kysuce.py').Replace('\','/')
$mapFile = Join-Path $root 'Content\Maps\Kysuce\L_Kysuce.umap'
if (Test-Path -LiteralPath $mapFile) {
    $backup = Join-Path $root ('Saved\KysuceBackups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
    New-Item -ItemType Directory -Path $backup -Force | Out-Null
    Copy-Item -LiteralPath $mapFile -Destination $backup
}
$arguments = @("`"$project`"", '/Engine/Maps/Entry', "-ExecutePythonScript=`"$scriptPath`"", '-unattended', '-nosplash', '-nosound', '-log=Kysuce.log')
$process = Start-Process -FilePath $editor -ArgumentList $arguments -PassThru -WindowStyle Hidden
"Kysuce PID: $($process.Id)"
if (!$process.WaitForExit(2400000)) { Stop-Process -Id $process.Id; throw 'Kysuce build timed out.' }
$log = Join-Path $root 'Saved\Logs\Kysuce.log'
$lines = Get-Content -LiteralPath $log
$lines | Select-String -Pattern 'KYSUCE_|LogPython: Error|Fatal error' | ForEach-Object { $_.Line }
if ($process.ExitCode -ne 0 -or !($lines | Select-String -SimpleMatch 'KYSUCE_BUILD_PASS')) { throw "Kysuce build failed. See $log" }
