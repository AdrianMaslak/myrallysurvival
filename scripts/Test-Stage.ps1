param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [string]$Stage = 'SS1',
    [int]$TimeoutSeconds = 900,
    # Leave with four seconds of the countdown left: the stage must carry the 10 s jump start penalty.
    [switch]$JumpStart,
    # A front wheel bolt left out: the scrutineer must refuse to start the car.
    [switch]$LooseBolt
)
# Drives a countryside special stage start to finish (docs/STAGES.md): the car is put 9 m behind the
# start line, the driver walks to it, the marshal counts down, the stage drive follows the route to the
# flying finish. Prints the stage log and fails unless the stage was finished.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'EditorGuard.ps1')
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$log = Join-Path $root 'Saved\Logs\StageTest.log'
if (Get-ProjectEditors $project) { throw 'Close Unreal Editor and game instances first.' }
$map = Get-Content -Raw (Join-Path $root 'Art\Maps\Countryside\Export\countryside.json') | ConvertFrom-Json
$route = ($map.routes | Where-Object { $_.id -eq $Stage }).samples
if (!$route) { throw "No stage $Stage." }
$dx = $route[3][0] - $route[0][0]; $dy = $route[3][1] - $route[0][1]
$len = [Math]::Sqrt($dx * $dx + $dy * $dy); $dx /= $len; $dy /= $len
$x = ($route[0][0] - 9 * $dx) * 100; $y = ($route[0][1] - 9 * $dy) * 100; $z = ($route[0][2] + 1.2) * 100
$yaw = [Math]::Atan2($dy, $dx) * 180 / [Math]::PI
$start = ('{0:F0},{1:F0},{2:F0},{3:F1}' -f $x, $y, $z, $yaw)
Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
# A fixed 30 Hz step and seed, as the panel test: the same drive however busy the machine is.
$arguments = @(('"' + $project + '"'), '/Game/Maps/Countryside/L_Countryside', '-game', '-windowed', '-ResX=960', '-ResY=540',
    '-nosound', '-unattended', '-Deterministic', '-FPS=30', '-RallyAutoSeat=driver', '-RallyAutoDrive', '-RallyStageDrive',
    "-RallyCarStart=$start", ('-abslog="' + $log + '"'))
if ($JumpStart) { $arguments += '-RallyStageJump' }
if ($LooseBolt) { $arguments += '-RallyStageLooseBolt' }
$process = Start-Process -FilePath $editor -ArgumentList $arguments -WindowStyle Hidden -PassThru
if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
    Stop-Process -Id $process.Id
    throw 'Stage test timed out.'
}
$lines = Get-Content -LiteralPath $log
$lines | Select-String 'RALLY_STAGE|RALLY_QUEST|RALLY_SEAT enter|Error:|Ensure condition' | ForEach-Object { $_.Line }
if ($JumpStart -and !($lines | Select-String 'RALLY_STAGE .* finished .* penalty=10 ')) { throw 'Jump start was not penalised.' }
if ($LooseBolt) {
    if (!($lines | Select-String 'refused at the start: the front left wheel.s bolts') -or !($lines | Select-String 'RALLY_STAGEDRIVE_DONE result=never_started')) {
        throw 'The scrutineer let a car with a loose wheel bolt start.'
    }
    return
}
# On SS1 the first quests run alongside: the shakedown and the first stage.
if ($Stage -eq 'SS1' -and !$JumpStart -and !(($lines | Select-String 'RALLY_QUEST Shakedown.*quest complete') -and ($lines | Select-String 'RALLY_QUEST SS1 Lipova Village.*quest complete'))) {
    throw 'The first quests did not complete.'
}
if (!($lines | Select-String 'RALLY_STAGEDRIVE_DONE result=finished') -or ($lines | Select-String 'Error:|Ensure condition')) {
    throw 'Stage test failed.'
}
