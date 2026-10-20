# Drivable Octavia

Code: Source/RallySurvival/Public/Vehicle/RallyCarPawn.h. Physics model and sources: docs/VEHICLE-PHYSICS.md.
Game mode: ARallyGameMode spawns one car per player in /Game/Maps/L_GravelLab.

## Play

Build (editor closed): powershell -ExecutionPolicy Bypass -File .\scripts\Build.ps1 -OpenEditor
Press Play. The game is first person: you start on foot; walk to the front-left door and press E
to drive (docs/CREW-AND-SEATS.md).
Keyboard: W/S throttle/brake, A/D steer, Space handbrake, Left Shift / Left Ctrl gear up / down, Q clutch,
R reset upright, mouse to look around (drifts back to straight ahead after 2 s), C toggles the chase
camera (debug view).
Gamepad: right/left trigger, left stick, A handbrake, RB / LB gear up / down, B clutch, View/Back reset,
right stick points the head, right stick click toggles the chase camera.

Gearbox: manual and sequential (`bManualGearbox`, Rally::Drivetrain::SelectGear). Each press moves one
gear; neutral sits between first and reverse, and reverse goes in only below 5 km/h going forward. A
driver taking the seat finds it in first. A shift takes 0.25 s through neutral with or without the
clutch. A gear the road would spin the engine more than 5% past its limiter in (first at 80 km/h, say)
does not go in: the lever stays and the box crunches (`Rally::Drivetrain::WouldOverRev`). The clutch pedal (Q) goes down in 0.1 s and comes back up over 0.35 s; held, the engine revs
freely, so clutch in, full throttle, let go is a launch. Left alone, the auto clutch still keeps the
car from stalling or creeping. Scripted drives use the automatic box, where holding brake when nearly
stopped selects reverse (capped at 30 km/h).
Crashes dent the body and damage parts (docs/DAMAGE.md); F8 flattens the front-left tyre and F9 repairs
everything (debug keys).
The on-screen readout shows speed, gear, engine rpm and front wheelspin above 20%.
Steering lock is speed-sensitive (halves at 80 km/h); ABS is on and traction control off by default.

## Structure

A 1300 kg box body is the only physics shape; chassis, panels and wheels are visual static meshes.
All setup values (wheels, tyres, springs, engine, gearbox, anti-roll bars, ABS, traction control)
are editable on the pawn under Vehicle > Setup; call ApplySetup after changing them at runtime.
Input actions and key mappings are created in code; move them to assets when rebinding is needed.
Config/DefaultEngine.ini enables async physics at 60 Hz, which the car's physics step needs.

## Engine

The engine bay is the 1.8T 20V set in Art/Vehicles/Octavia/Engine_18T (61 parts from a generated model, with
185 fasteners and their tools; its README). scripts/export_engine_18t.py places it in the car and writes
FBX parts; scripts/import_engine_18t.py brings them into /Game/Vehicles/Octavia/Engine18T with one engine
material (four maps, the model's grime as vertex colour, the leak look) and a material per surface.

- Placement: the car model's bay is narrower than a real Octavia's (78-90 cm free between the inner wings
  at engine height), so the set is scaled to 88%. The crank sits at (150.7, 16.0, 38.1) cm, assembly frame:
  the driveshaft flanges at the height and fore-aft place of the car's driveshaft joints, engine and
  gearbox centred across the bay, the turbo clear of the bulkhead. The crank pulley and gearbox end still
  touch the chassis rails a few cm, low down.
- In the game: the Engine body part is the block, head, cam cover, sump, timing covers, belt, gearbox,
  mounts, coils, fuel rail, front pipe and the rest bolted to them, pivoting on the crank; the crank pulley
  its own mesh; the Bay body part the battery, washer bottle, fuse box and coolant expansion tank. The
  modules (cover, air box, intake, exhaust manifold, turbo, intercooler, alternator, starter, computer) are
  the model's, the computer moved to where the car keeps it by the bulkhead; the exhaust is still the
  prototype's. The oil filler, battery, sump, gearbox and block points (ARallyCarPawn, URallyPanelComponent)
  sit on the new parts.
- Cooling and charge air, at the front: the model's own layout, each body-fixed group moved the least that
  clears the car's body, front subframe and struts by a centimetre (export_engine_18t.py COOLING_MOVES_CM,
  from a fit study against Octavia_With_Wheels.blend): the radiator and its fan shroud 6 cm back and 16 cm
  up over the subframe's crossmember (where the car's own radiator sat), right of the middle with the air
  box beside it; the side-mount intercooler 4 cm forward and 12 cm in, low in the left front corner behind
  the bumper; the coolant tank 10 cm in from the right strut tower. The radiator hoses and the two charge
  pipes (turbo to intercooler under the gearbox, intercooler up to the throttle) are bent to follow: the end
  at the moved part goes with it, the end on the engine stays. The radiator, shroud and hoses are the car's
  radiator panel (slot 6, crushing as before); the two fans' blades are their own meshes and turn when the
  engine is hot (above 98 C, the electrics alive, engine running or not; ARallyCarPawn::UpdateFans). Water
  goes in at the tank's cap, where the level shows; coolant drips from the radiator's lower outlet and
  steams from its face.
- Running (Vehicle/EngineMotionRules.h, ARallyCarPawn::UpdateEngineMotion, every machine, visual only):
  the engine rolls about the crank on its mounts against its torque (3 degrees at full torque, a 3.2 Hz
  spring at 35% of critical damping, so it rocks back past upright once when the throttle lifts), jolts as
  it catches and shudders to a stop, buzzes at idle (0.12 degrees, smoother as it revs) and more the rougher
  it runs (worn below 70% or knocking), with misfire jolts when rough. The crank pulley turns at engine
  speed. The modules bolted to the engine move with it (URallyPanelComponent::SetEngineMotion); the air box,
  intercooler and computer stay on the body, and so do the radiator hoses and charge pipes. Test-Knock checks it leans over a degree under load and
  settles when stopped; the rules have standalone tests.
- Not yet: the model's brake servo, exhaust and tank (the car keeps its own); the radiator coming off; the
  belt turning.

## Surfaces

scripts/apply_lab_surfaces.py assigns RallyPhysicalMaterial surfaces in the lab:
gravel road (friction 0.65, loose, rolling resistance 0.03) and off-road ground (0.5, loose, 0.05).
Surfaces without a RallyPhysicalMaterial count as hard, with the material's friction (default 0.7).

## Tests

- Physics model unit tests: scripts\Test-Rules.cmd (no Unreal needed).
- Driving keys: scripts\Test-Keys.ps1 presses Left Shift and Left Ctrl through the driving controls
  (first, up to third, down through neutral to reverse standing, back to neutral) and holds Q, and checks
  the selected gear after each press and the clutch pedal.
- In-game self-test, editor closed: powershell -ExecutionPolicy Bypass -File .\scripts\Test-Drive.ps1
  Runs -game -RallyAutoDrive (settle, launch, brake, corner, handbrake turn, reverse; then on the manual
  box a clutch-drop launch, a shift without the clutch, a power slide and reverse gear) and prints speed,
  gear, rpm, ride height, roll, lateral g, sideslip and per-wheel load and slip, then quits.
  Screenshots go to Saved/Screenshots/WindowsEditor/RallyAutoDrive_*.png.
- Tuning without rebuilding: add -RallySpringScale=, -RallyDampingScale=, -RallyGripScale=,
  -RallyPressure=, -RallyAntiRollScale= or -RallyPeakSlipScale= to the game command line (Test-Drive.ps1 -ExtraArgs).
- Condensed log: python scripts/drive_log_summary.py Saved/Logs/RallySurvival.log "corner" (per sample:
  speed, gear, rpm, roll, lateral g, sideslip, front and rear slip angles, wheelspin, loads, position).
- The self-test leaves the road before its steady corner so no surface edge falls mid-corner.
- Crash test: scripts/Test-Crash.ps1 (docs/DAMAGE.md).

## Cockpit

The steering wheel is a separate mesh turning about its measured column axis (15.5:1 steering ratio,
about 2.8 turns lock to lock), following the front wheels' visual steering angle.

Driver eye point (-2, -37, 114) cm in the assembly frame, from scripts/inspect_octavia_cabin.py
(writes Art/Vehicles/Octavia/Prototype/Export/cabin.json with seat, wheel, dashboard and glass bounds).
The camera is fixed to the body, so it rolls and pitches with the suspension. Glass is tinted
translucent (M_Prototype_Glass_SolidPreview keeps its old name because the meshes reference it).

## Known limits

- Mirrors show no reflection; gauge needles do not move yet.
- Door glass and windscreen have no dirt or reflections; interior uses one flat material.

- With nobody driving, the server simulates the car out of gear with the handbrake on, the throttle shut
  unless a hand holds it open in the bay.
- Not yet tested with two players or network emulation.
