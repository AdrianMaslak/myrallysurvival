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
  mounts, spark plugs, fuel rail, front pipe and the rest bolted to them, pivoting on the crank; the crank pulley
  its own mesh; the Bay body part the battery, washer bottle, fuse box and coolant expansion tank. The
  modules (cover, air box, intake, exhaust manifold, turbo, intercooler, alternator, starter, computer and
  the four ignition coils, each held by its connector's clip) are the model's, the computer moved to where the car keeps it by the bulkhead; the exhaust is still the
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
  speed, and the accessory belt runs round it and the alternator, power-steering and air-conditioning
  pulleys, each turning at its ratio of the crank (2.7, 1.3 and 1.4 times); the belt's texture moves along
  it (M_Engine18T_Belt's BeltOffset), shown at most three lengths a second so it never just flickers. The
  alternator's pulley rides on the alternator; with the alternator off, the belt is off too. The modules bolted to the engine move with it (URallyPanelComponent::SetEngineMotion); the air box,
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

## Cabin and lamps

`URallyCabinComponent` (rules in `Vehicle/CabinRules.h`, checked in Test-Rules) moves the v2 interior's own
parts and lights the body's lamps. Each moving part is its own mesh at its pivot, turning about the axis and
over the range of its rig (scripts/export_body_v2.py, GAME_MOVING; game_parts.json).

- **Gauges**: speedometer 0-220 km/h and tachometer 0-7000 rpm over 240 degrees, temperature 50-130 C and fuel
  over 90. The needles follow like damped pointers (the tachometer flicks, the temperature creeps) and drop to
  their stops with the ignition off or a flat battery.
- **Warning lamps** (M_Body_Dash Warn0..7): oil and battery with the engine not running (oil also below 2 l,
  battery also without an alternator or below 20% charge), brake with the handbrake on, the seat belt for 6 s,
  the indicators flashing, high beam. All eight light for a 2 s bulb check when the ignition comes on.
- **Dial lighting** with the headlamps on (Backlight); lamps dim on a weak battery.
- **Pedals, gear lever, handbrake**: as the driver works them, on every machine (the car's snapshots carry the
  pedals and handbrake). The lever follows the 02J's H-gate: 1-2 left, 3-4 middle, 5 and R right.
- **Switches**: B high beam, comma / full stop the left / right indicator (a turn indicator cancels itself once
  the wheel has turned 90 degrees that way and come back), X the hazards, K the wipers (interval every 4 s,
  slow, fast, off; off, they finish the wipe and park), G the glove box, V the sun visor, T the horn (a 90s
  twin electric horn, Rally::Audio EEvent::Horn). The indicator and wiper stalks move with them; the indicators
  tick.
- **Reaching for them**: from the cockpit, look at a control and click (left mouse button): the light switch's
  knob left of the wheel, the hazard button, the radio (its display and the trip meter go dark), the indicator
  stalk (left, right, off), the wiper stalk, the glove box, the sun visor, the interior lamp in the headliner (a
  small warm light and its lens), or hold on the wheel's hub for the horn. A dot and the control's name show under
  the middle of the view (within 8 degrees and 1.25 m of the eye; Rally::Cabin::AimedControl).
- **Lamps**: dipped beams, with tail and number-plate lamps (L); high beam reaches further and narrower (B);
  brake lamps from the pedal with a red light behind the car; the reversing lamp in reverse with a white one;
  the indicators and side repeaters flash; the lenses glow over their lamps. A lamp unit off the car stays
  dark, and nothing lights without the battery. Wipers and gauges need the ignition.

- **Details** (scripts/interior_details.py, built into the interior at export): the stack's radio shows its
  station in amber, its presets and the climate panel's buttons are labelled and the knobs have pointers (lit on
  the ignition, brighter with the dial lamps); a trip meter on the dash in front of the co-driver; a 2 kg fire
  extinguisher strapped in the co-driver's footwell; a pine air freshener hanging from the mirror.

### Rebuilding the v2 body, interior and their textures

The generated files are not in git when they come from a cloud session (no LFS uploads there); make them here:

1. `python scripts\build_game_textures.py` (any Python 3 with numpy and Pillow: `py -m pip install numpy pillow`;
   Blender's own Python works too). Writes textures_game\ for the body, engine and suspension.
2. Blender: `blender -b --factory-startup --python scripts\export_body_v2.py` (the moving parts and the details
   are exported with the rest).
3. With the editor closed: `scripts\Invoke-EditorPython.ps1 -Script scripts\import_body_v2.py -PassMarker
   BODYV2_IMPORT_PASS`, then the engine's and suspension's imports (import_engine_18t.py, import_suspension_v2.py)
   for their retuned maps.
4. `scripts\Build.ps1`, then Test-Rules, Test-Drive, Test-Day, Test-Crew.

## Known limits

- Mirrors show no reflection. The glove box and sun visor only open and shut. Fog lamps are not switched.
- Door glass and windscreen have no dirt or reflections.
- The lamps' brightness (EmissiveStrength 1-14) and the beams' reach are first guesses; check them at night.

- With nobody driving, the server simulates the car out of gear with the handbrake on, the throttle shut
  unless a hand holds it open in the bay.
- Not yet tested with two players or network emulation.
