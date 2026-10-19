# Vehicle physics

Project-owned model, chosen over Chaos Modular Vehicle (2026-10-11). The plan needs damage,
tyre pressure, wheel size and other parts to change physical parameters directly.

## Why not the Modular Vehicle plugin (UE 5.8.3)

- Grip used a spring-force estimate times SuspensionForceEffect (default 100), not wheel load,
  so surface friction barely mattered.
- Wheelspin subtracted grip force from drive torque (unit mismatch). Realistic grip made every gear spin.
- The configured slip-angle limit was never applied.
- Suspension was an iteration-dependent, overdamped position constraint; its disable switch was unused.
- Input reached the plugin only through the physics-prediction rewind callback, and that hook raced at start-up.

## Structure

Engine-independent, unit-tested headers (Tests/VehiclePhysicsTests.cpp, scripts/Test-Rules.cmd):
- Source/RallySurvival/Public/Vehicle/RallyTyreModel.h: tyre forces.
- Source/RallySurvival/Public/Vehicle/RallyDrivetrain.h: engine, clutch, gearbox, open differential.

ARallyCarPawn::SimulatePhysicsStep runs in AsyncPhysicsTickActor, fixed 60 Hz, on every machine:
1. Per wheel: raycast along body up from full bump to full droop plus radius.
2. Spring k x, separate bump/rebound damping, bump stop in the last 2 cm, anti-roll bars per axle.
3. Wheel load = suspension force. Contact velocity gives slip angle and slip ratio.
4. Eight drivetrain substeps (480 Hz): tyre forces, engine, clutch, gearbox, open front diff, brakes, ABS.
5. Suspension force at the wheel pivot; tyre force at roll-centre height above the contact; aero drag at the COM.

Inputs are replicated (owning client sends to server, server to others) so all machines run the same forces.
The server corrects positions through standard physics replication. Not yet tested with two players.

## Tyre model

Similarity method: slip ratio and tan(slip angle) are divided by their peak values. One curve gives
force from the combined normalised slip S, split back into longitudinal and lateral parts.
Braking or power therefore reduces cornering grip.
- Curve: 1.5 S - 0.5 S^3 up to the peak at S = 1, then decays toward a sliding ratio.
- Peak force = surface friction x tyre grip x load x load-sensitivity factor (-10% per +100% load).
- Surface (RallyPhysicalMaterial): friction, looseness (0 tarmac to 1 loose), rolling resistance.
- Looseness blends tyre values: peak slip angle 7 degrees on tarmac, 12 on loose; peak slip ratio 0.10/0.15;
  sliding ratio 0.80/0.90.
- Pressure (bar): softer gives more loose-surface grip and a later, lazier peak; under 0.5 bar counts as flat
  (grip x0.45, peak angle x2). The magnitudes are gameplay assumptions, not measurements.

## Values and sources

| Item | Value | Basis |
| --- | --- | --- |
| Gravel road friction | 0.65 | hpwizard: gravel peak 0.60, sliding 0.55; Stunt Rally gravel roads 0.8-0.9 times its tyre |
| Gravel tyre grip on loose surfaces | x1.1 (effective mu about 0.72) | tread biting into gravel; raised after driving felt slightly slippery |
| Loose off-road ground | 0.50 | Stunt Rally grass 0.55-0.65 |
| Peak slip angle | 7 (tarmac) to 12 degrees (loose) | ChassisSim: about 6 tarmac, 16 dirt |
| Grip kept when sliding | 0.80 tarmac, 0.90 loose | ChassisSim: road tyre drops 10-20%; rally tyre on dirt about 10% beyond 20 degrees |
| Front cornering stiffness on gravel | about 360 N/deg | Stunt Rally gravel tyre about 309 N/deg at that load; asphalt about 3x stiffer |
| Spring rate, front/rear | 38.5 / 30.7 kN/m | ride frequency 1.6 / 1.7 Hz at 381 / 269 kg sprung corner mass; gravel rally cars 1.8-2 Hz |
| Damping | 0.35 critical bump, 0.55 rebound | raised from 0.25/0.4 after the car felt jumpy; rebound stiffer than bump |
| Travel | 24 cm (front 14 bump + 10 droop) | rally road car; Stunt Rally uses 30-33 cm |
| Anti-roll bars | 2.5 kN/m front, 8 kN/m rear | roll stiffness about 52% front; 54% understeered, 46% made the rear slide; about 3 degrees roll per g |
| Gearbox | manual, sequential; 0.25 s through neutral per shift, clutch optional | the player's car; scripted drives use the automatic box |
| Automatic shift points | up 3465-6300 rpm, down 1800-3000 rpm, scaled by throttle | scripted drives only; early shifts at part throttle stop first-gear torque killing front grip mid-corner |
| Auto clutch at launch | bites over 945-2945 engine rpm | keeps the car from stalling or creeping when the pedal is not used |
| Clutch pedal | down in 0.1 s, up over 0.35 s; opens the clutch whatever the auto clutch does | clutch in, full throttle, let go: a launch from the limiter |
| Traction control | off (`bTractionControl`) | when on it cut throttle from 1.5x to 2.5x peak slip ratio; the engine could not rev in a slide (about 1600 rpm in second, throttle flicking 10-100%) |
| Steering | 32 degrees lock, halves at 80 km/h | speed-sensitive so keyboard steering cannot throw the car sideways at speed |
| Roll centre | 8 cm front, 20 cm rear | MacPherson front, torsion-beam rear |
| Engine | 235 Nm 1950-5000 rpm, 132 kW at 5500, limiter 6800 | Octavia RS 1.8T (180 PS) |
| Gearbox | 3.300 1.944 1.308 1.034 0.838, final drive 3.647 | 02J gearbox code EHA, 132 kW (Octavia Mk1 workshop manual) |
| Mass, COM | 1300 kg, 50 cm high, 58% front | car plus crew |

Sources:
- Stunt Rally 3 (VDrift simulation, GPL): https://github.com/stuntrally/stuntrally3, data/carsim/normal (surfaces.cfg, tires/*.tire, cars/*.car)
- ChassisSim, The Vehicle Dynamics of rallying: https://www.chassissim.com/wp-content/uploads/Rally_Dynamics_blog.pdf
- Tire friction and rolling coefficients: https://www.hpwizard.com/tire-friction-coefficient.html
- Suspension frequency: https://www.hpacademy.com/courses/suspension-tuning-and-optimization/foundation-concepts-suspension-frequency/
- OptimumG springs and dampers: http://downloads.optimumg.com/Technical_Papers/Springs&Dampers_Tech_Tip_1.pdf
- Octavia Mk1 02J gearbox ratios: https://workshop-manuals.com/skoda/octavia-mk1/power_transmission/manual_gearbox_02j/technical_data/identification_of_the_gearbox/identification_characters_aggregate_assignment_ratios_capacities/
- Octavia RS per-gear speeds (about 120 km/h in 2nd, 170 in 3rd; 235 km/h top): https://strikeengine.com/octavia-rs-1-8t-review-overrated-or-future-classic/

## Road speed per gear (0.31 m wheel radius, no wheelspin)

| Gear | km/h per 1000 rpm | at 6500 rpm |
| --- | --- | --- |
| 1 | 9.7 | 63 |
| 2 | 16.5 | 107 |
| 3 | 24.5 | 159 |
| 4 | 31.0 | 201 |
| 5 | 38.2 | 249 (drag limits top speed to about 235 at about 6150 rpm) |

Rpm only follows this table while the driven wheels grip. With wheelspin the engine reads higher;
the readout shows wheelspin above 20%. Without traction control, full throttle on gravel in first
gear spins the fronts to the limiter at 17 km/h while the car keeps accelerating to 55 km/h.

## Self-test results (2026-10-11, gravel lab, single player)

- Drop from 70 cm: lands, one rebound overshoot, settled in about 1 s; oscillation about 1.65 Hz.
- Static loads 3735 N front, 2627 N rear per wheel; body 3 cm above modelled height.
- Launch with traction control: clutch slips near 1700 rpm, then rpm rises with speed (2400 at 18 km/h,
  5300 at 40, 6600 at 52 km/h) with about 25% wheelspin; 75 km/h at 7.5 s; upshift at 61 km/h.
- Braking with ABS on the gravel road: 77 km/h to stop in 35 m (0.66 g).
- Steady corner on loose ground (mu about 0.55), 55 km/h in second, half throttle, 11 degrees steer:
  0.44-0.53 g, front slip 10-12 degrees (at peak), rear 6.5-8.5, sideslip 5-6.5 degrees, mild limit understeer.
  Earlier setups: ARB 3/6 kN/m in first gear understeered (front 15 degrees, rear 7); ARB 1.5/12 kN/m let the rear slide.
- Handbrake at 50 km/h with full lock: rear locks, sideslip reaches 50 degrees, car turns about 90 degrees in 1.5 s.

Manual box, traction control off (2026-10-19, Test-Drive manual phases): clutch in at full throttle
holds 6800 rpm standing; letting go launches with heavy wheelspin to 21 km/h in 1.6 s; a shift to second
without the clutch passes 0.25 s in neutral; a power slide in second (handbrake flick, full lock, full
throttle) revs from 4900 to 6800 rpm at full load, the unloaded inside front wheel spinning (open
differential); reverse gear at 0.6 throttle backs at 16 km/h after 2.5 s.

## Parts and damage hooks

Change ARallyCarPawn setup properties, then call ApplySetup():
- Tyre pressure, puncture: WheelSetups[i].Tyre.Pressure.
- Bigger wheels: Radius, Inertia (raise ride height with Pivot.Z if needed).
- Springs, dampers, travel, anti-roll bars, brake torque, ABS, traction control, gear ratios, torque curve.
- Bent suspension and alignment (camber, toe) are not modelled yet.

## Known limits

- Raycast wheels: a sharp 10 cm step lifts the wheel instantly; sphere or shape sweeps may be needed.
- No tyre relaxation length, camber, limited-slip differential or stalling yet. A missed downshift is
  refused rather than over-revving the engine (no money-shift damage).
- Pressure effects are uncalibrated assumptions.
- Keyboard throttle is on/off, and traction control is off: full throttle on gravel in first spins the fronts
  to the limiter, so short-shifting is the driver's job.
