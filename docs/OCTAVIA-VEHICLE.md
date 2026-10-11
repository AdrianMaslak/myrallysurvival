# Drivable Octavia

Code: Source/RallySurvival/Public/Vehicle/RallyCarPawn.h. Physics model and sources: docs/VEHICLE-PHYSICS.md.
Game mode: ARallyGameMode spawns one car per player in /Game/Maps/L_GravelLab.

## Play

Build (editor closed): powershell -ExecutionPolicy Bypass -File .\scripts\Build.ps1 -OpenEditor
Press Play. Keyboard: W/S throttle/brake, A/D steer, Space handbrake, R reset upright.
Gamepad: right/left trigger, left stick, A handbrake, View/Back reset.
Holding brake below 3 km/h selects reverse; reverse is capped at 30 km/h.
The on-screen readout shows speed, gear, engine rpm and front wheelspin above 20%.
Steering lock is speed-sensitive (halves at 80 km/h); traction control and ABS are on by default.

## Structure

A 1300 kg box body is the only physics shape; chassis, panels and wheels are visual static meshes.
All setup values (wheels, tyres, springs, engine, gearbox, anti-roll bars, ABS, traction control)
are editable on the pawn under Vehicle > Setup; call ApplySetup after changing them at runtime.
Input actions and key mappings are created in code; move them to assets when rebinding is needed.
Config/DefaultEngine.ini enables async physics at 60 Hz, which the car's physics step needs.

## Surfaces

scripts/apply_lab_surfaces.py assigns RallyPhysicalMaterial surfaces in the lab:
gravel road (friction 0.65, loose, rolling resistance 0.03) and off-road ground (0.5, loose, 0.05).
Surfaces without a RallyPhysicalMaterial count as hard, with the material's friction (default 0.7).

## Tests

- Physics model unit tests: scripts\Test-Rules.cmd (no Unreal needed).
- In-game self-test, editor closed: powershell -ExecutionPolicy Bypass -File .\scripts\Test-Drive.ps1
  Runs -game -RallyAutoDrive (settle, launch, brake, corner, handbrake turn, reverse) and prints speed,
  gear, rpm, ride height, roll, lateral g, sideslip and per-wheel load and slip, then quits.
  Screenshots go to Saved/Screenshots/WindowsEditor/RallyAutoDrive_*.png.
- Tuning without rebuilding: add -RallySpringScale=, -RallyDampingScale=, -RallyGripScale=,
  -RallyPressure=, -RallyAntiRollScale= or -RallyPeakSlipScale= to the game command line (Test-Drive.ps1 -ExtraArgs).
- Condensed log: python scripts/drive_log_summary.py Saved/Logs/RallySurvival.log "corner" (per sample:
  speed, gear, rpm, roll, lateral g, sideslip, front and rear slip angles, wheelspin, loads, position).
- The self-test leaves the road before its steady corner so no surface edge falls mid-corner.

## Known limits

- Unpossessed cars on the server get zero throttle and the handbrake; seat changes are not implemented yet.
- Not yet tested with two players or network emulation.
