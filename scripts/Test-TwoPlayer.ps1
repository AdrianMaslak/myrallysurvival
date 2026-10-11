param(
    # Outgoing delay added by each process; round trip is about twice this.
    [int]$LagMs = 0,
    [int]$LossPct = 0,
    [int]$HostSeconds = 110,
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    # Added to both processes, e.g. -RallyCrashTest to drive the client's car into the barrier.
    [string[]]$ExtraArgs = @()
)
# Unattended two-player check: a listen-server host and a joining client on this PC, both on foot.
# The host walks to the right front door and rides as co-driver; the client walks to the left door,
# drives and runs the scripted drive (-RallyAutoDrive). Both processes log every car (-RallyNetLog)
# and scripts/net_compare.py compares the driver's car with the co-driver's view of it.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$project = Join-Path $root 'RallySurvival.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
if (Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue) {
    throw 'Close running Unreal Editor and game instances first.'
}
$tag = "lag$LagMs-loss$LossPct"
$hostLog = Join-Path $root "Saved\Logs\TwoPlayer_${tag}_host.log"
$clientLog = Join-Path $root "Saved\Logs\TwoPlayer_${tag}_client.log"
Remove-Item -LiteralPath $hostLog, $clientLog -ErrorAction SilentlyContinue

# Two games on one PC run slowly; below about 20 fps Unreal's 3-steps-per-frame physics cap would
# put the cars in slow motion, which real players at normal frame rates never see.
$common = @('-game', '-windowed', '-ResX=640', '-ResY=360', '-nosound', '-unattended', '-RallyNetLog',
    '-ExecCmds="r.ScreenPercentage 50,sg.ShadowQuality 0,p.MaxPhysicsStepsPerGameTick 8,stat unit"')
if ($LagMs -gt 0) { $common += "-PktLag=$LagMs" }
if ($LossPct -gt 0) { $common += "-PktLoss=$LossPct" }
$common += $ExtraArgs

$hostArgs = @("`"$project`"", '/Game/Maps/L_GravelLab?listen') + $common + @("-abslog=`"$hostLog`"", "-RallyQuitAfter=$HostSeconds", '-RallyAutoSeat=codriver', '-WinX=0', '-WinY=40')
$hostProcess = Start-Process -FilePath $editor -ArgumentList $hostArgs -PassThru
$deadline = (Get-Date).AddSeconds(180)
while (-not ((Test-Path $hostLog) -and (Select-String -Path $hostLog -Pattern 'listening on port' -Quiet))) {
    if ((Get-Date) -gt $deadline -or $hostProcess.HasExited) { throw "Host did not start listening; see $hostLog." }
    Start-Sleep -Seconds 2
}

$clientArgs = @("`"$project`"", '127.0.0.1') + $common + @("-abslog=`"$clientLog`"", '-RallyAutoSeat=driver', '-RallyAutoDrive', '-WinX=820', '-WinY=40')
$clientProcess = Start-Process -FilePath $editor -ArgumentList $clientArgs -PassThru
foreach ($process in @($clientProcess, $hostProcess)) {
    if (-not $process.WaitForExit(($HostSeconds + 120) * 1000)) {
        Stop-Process -Id $process.Id
        Write-Warning "Process $($process.Id) timed out and was stopped."
    }
}
python (Join-Path $PSScriptRoot 'net_compare.py') $hostLog $clientLog
foreach ($log in @($hostLog, $clientLog)) {
    Select-String -Path $log -Pattern 'RALLY_DAMAGE|RALLY_CRASHTEST_DONE|Rejected impact' |
        ForEach-Object { "$(Split-Path $log -Leaf): $($_.Line -replace '^\[[^\]]+\]\[\s*\d+\]\w+: (Display|Warning): ', '')" }
}
