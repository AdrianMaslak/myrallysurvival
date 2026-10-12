# Crash damage

What is built. The target design (panels detaching, more parts, glass, rollovers, injuries) is in
DESTRUCTION.md.

Code: Source/RallySurvival/Public/Damage/ (RallyBodyDeformComponent.h, RallyDamageComponent.h,
ImpactRules.h, PartConditionRules.h, SuspensionRules.h, CrushProfiles.h, EngineRules.h, UndersideRules.h,
RallyPanelComponent.h, RallyEngineComponent.h) and the impact handling in Vehicle/RallyCarPawn.cpp.
Approach adapted from the jourverse "Destructible Vehicle" tutorial (hit sensors, damage mask in the
material, deformed body), done in C++ with real mesh deformation instead of a control rig.

## What a crash does

1. Detection, on the machine that simulates the car (driver's PC, or the server with nobody driving):
   the body box reports each contact. Contacts within 0.1 s and 1.5 m are summed into one impact.
   Severity is the change of velocity, impulse / mass. Contacts pushing up from under the floor
   (landings, bottoming out) and people on foot are ignored.
2. Contact patch: Chaos gives one contact point per pair, usually a box corner, so the car samples a
   grid on the box face being pushed and keeps the samples touching the other object. A pole gives a
   small patch, a wall the whole face. Each sample uses the other object's simple collision where it
   has some; against triangle-only collision (terrain, complex-as-simple scenery, imported maps) it
   falls back to a short trace out of the face, from 60 cm inside the box to 60 cm beyond it.
3. The simulating client sends the impact to the server (ServerReportImpact, rate-limited and
   checked for plausibility); a simulating server applies it directly.
4. The server adds a dent to a replicated list and damages the parts the patch reaches.
   Every machine, including late joiners, rebuilds the same body shape from the list (capped at 48).

## Visual damage (URallyBodyDeformComponent)

- At BeginPlay the chassis, four doors, bonnet, boot lid, radiator and front/rear bumpers are copied
  into dynamic meshes that replace the static meshes on screen. Source assets need Allow CPU Access.
- A dent pushes vertices in under the patch (varied by 12%) and fades out over Radius beyond it.
  Where the push falls off along its own direction (bonnet and wings behind a frontal hit) the metal is
  squeezed lengthwise and folds out of its plane, mostly outward, in creases about 28 cm apart running
  across the push; fold depth follows the squeeze. Under the patch the part is shoved back nearly whole,
  so a bumper hit flat on stays a bumper. Fold directions are each surface point's normal averaged over
  its seam copies and turned away from the car, so seams and both skins of a panel move together.
- A point already pushed in gives way less: crush per point saturates (40 cm for the shell), so repeated
  hits on one spot add less and less instead of driving metal through the car.
- Each deformable part gives its own way (Damage/CrushProfiles.h, Rally::Crush::ForKind), on the car and
  as a loose part alike: how far it gives, crease spacing and strength, how much springs back, crush limit
  and how readily the finish marks (paint scuffs, glass crazes, a lamp lens cracks). 22 meshes deform:
  the shell (chassis: body, underbody, wipers, exhaust, fuel tank, suspension), the 15 panels and six
  fixed parts (roof, windscreen, two bumper bars, interior, engine).

  | Part | Gives | Creases | Springs back | Crush limit | Character |
  | --- | --- | --- | --- | --- | --- |
  | Body shell | 1.0 | 28 cm, 1.0 | 0 | 40 cm | reference |
  | Doors | 0.75 | 24 cm, 0.9 | 5% | 30 cm | side-impact beams: shallower dents |
  | Bonnet | 1.05 | 34 cm, 1.5 | 0 | 45 cm | thin and wide: a few big creases, tents up |
  | Boot lid | 0.9 | 30 cm, 1.2 | 0 | 40 cm | stiffened by the rear window |
  | Radiator | 1.25 | 10 cm, 0.7 | 0 | 30 cm | soft core, fine crush |
  | Bumpers | 1.0 | 50 cm, 0.35 | 45% | 25 cm | plastic cover: broad warps, scuffs 1.6x |
  | Front wings | 1.1 | 24 cm, 1.3 | 0 | 40 cm | thin bolt-on steel: crumples readily |
  | Lamps | 0.7 | 20 cm, 0.2 | 0 | 15 cm | barely bend, lens cracks 2.5x (lamp materials go dark) |
  | Roof | 0.85 | 30 cm, 1.1 | 0 | 35 cm | held up by the pillars |
  | Windscreen | 0.5 | 60 cm, 0.1 | 0 | 12 cm | sags with its frame, crazes 2.5x |
  | Bumper bars | 0.55 | 45 cm, 0.5 | 0 | 25 cm | steel beam: one broad bend |
  | Interior | 0.35 | 40 cm, 0.4 | 0 | 20 cm | pushed in a little, never creased |
  | Engine | 0.15 | 40 cm, 0.2 | 0 | 10 cm | castings hardly move |

  In the 47.6 km/h barrier test the bumper keeps 15.9 cm of dent (it was 28.9 with shell behaviour)
  and the bonnet 27.9 cm with its creases; the same hit works both front wings loose at their front
  bolt and leaves both headlamps hanging by a thread (clips at 6 and 18 of 255). Dent build time is
  unchanged at about 45 ms for 176,573 vertices. Paint and lamp materials are two-sided, so a loose
  wing or lamp unit shows its open back.
- Shading: each vertex keeps its own undamaged normal turned by how far the surrounding faces turned,
  gathered across UV and normal seams, so hard edges stay hard and seams do not show.
- Vertex colour R marks scraped paint (1 intact, 0 bare): in patches on faces that met the obstacle and
  along creases; faces running along the push keep most paint. M_Prototype_Paint blends to dark bare steel;
  the glass material crazes (paler, nearly opaque). Static meshes without vertex colours read white,
  so the materials look unchanged until something is hit.
- Doors are dented in their closed position; open doors swing their dented copy.

Setup after reimporting the Octavia: scripts/Invoke-EditorPython.ps1 -Script scripts/setup_damage_assets.py
-PassMarker DAMAGE_ASSETS_PASS (CPU access on body meshes, damage blend in paint and glass; safe to rerun).

## Part damage (URallyDamageComponent)

Rules and numbers: Source/RallySurvival/Public/Damage/ImpactRules.h (prototype tuning).

| Severity (change of velocity) | Dent | Notes |
| --- | --- | --- |
| below 2 m/s (7 km/h) | none | kerb knocks, touching other cars at walking pace |
| 5.6 m/s (20 km/h tap) | about 9 cm, 52 cm wide shoulder | tyre survives a direct hit |
| 13.9 m/s (50 km/h into a wall) | about 22 cm | direct wheel hit flattens the tyre; radiator gone |
| over 45 m/s | rejected as implausible | |

- Tyre: only direct hits (patch within 70 cm of the wheel centre). Health 0 = flat: 0.2 bar (45% grip,
  sluggish) and the wheel rides 7 cm lower on the rim.
- Suspension (each corner's arms: front lower arm, rear trailing arm): direct hits, or hard hits
  crushing back into the wheel well (reach fades over 1.5 x dent depth). Lost health bends the wheel
  outward, up to 5 degrees toe-out at 0, so a damaged corner pulls.
- Struts, brakes, front tie rods and front driveshafts: see Running gear below.
- Radiator: hits pushing backwards near the grille. A damaged one cools less and leaks; engine and battery
  damage, temperature and coolant: see Engine, cooling and battery below.
- Handling is rebuilt from the undamaged setup plus these symptoms whenever condition changes, on every
  machine, so whoever simulates the car drives the damaged car. The readout lists damaged parts.

Debug keys while driving (not in shipping builds): F8 flattens the front-left tyre, F9 repairs every
part, removes that car's loose debris (including carried parts), and straightens the body.

## Repairs

URallyDamageComponent::CompleteValidatedRepair restores 1 (proper fix) or 0.6 (bodge, flagged).
Repeated bodges cannot replenish a part; bodges never worsen a healthier part. Server-only, no client
RPCs: the interaction layer must validate tools, inventory, distance and completion first.
Panel recovery and running-gear work (wheel change with the jack, swapping suspension parts from the
boot kit, hammering bent parts) have prototype interactions (below). Not yet built: a general inventory,
buying or finding spares, or persistence across game sessions.

## Tests

- Rules: scripts\Test-Rules.cmd (damage, repair and crash rules, no Unreal needed).
- scripts/Test-Crash.ps1: runs to the car, drives into the lab's impact barrier from 30 m, backs off,
  logs impact and part state and saves RallyCrash_*.png (cockpit, front-left, front-right, front).
  Then the bonnet and front bumper are detached, laid beside the nose and photographed
  (RallyCrash_debris, _debris_bonnet, _debris_bumper) to check that flown-off parts still read as parts.
- scripts/Test-TwoPlayer.ps1 -LagMs 50 -ExtraArgs '-RallyCrashTest': the client drives into the barrier
  with the host riding along.
- scripts/Test-Engine.ps1: the crash test, then circles on the crushed radiator until the engine
  overheats (see Engine, cooling and battery).
- scripts/Test-Underside.ps1: drives over a ridge of rock between the wheels (see Underside).

Results 2026-10-11:
- Single player: barrier hit at 47.6 km/h, 17.1 m/s change of velocity (the car bounced back at
  11 km/h); whole front face patch; front crumpled, bonnet buckled and scraped; radiator 4%, both front
  suspension corners 70% (1.5 degrees toe-out each); tyres intact. Reversing straight afterwards drifted
  0.6 degrees. Building the dent took 45 ms (one hitch at the moment of impact). Since the running gear
  was added (2026-10-12) the same hit also leaves both front struts at 71% and both front driveshafts at
  77%; tie rods and brakes come through.
- Two players, about 100 ms round trip: the client's impact reached the server, which applied it; the
  dent and part state replicated back, both sides logged the same state.
- Scripted drive (Test-Drive.ps1, 196 m including off-road corner and reverse): no damage recorded.

## Limits and next steps

- The 45-70 ms dent build hitches one frame per heavy impact; a long scrape along a wall could stutter.
  Parallelising the vertex loop is the first fix if it shows in play.
- Contact patches use the body box, so dents start at the box faces (bumpers, sides, roof), not at
  exact panel surfaces; rounded corners are approximate.
- The triangle-collision fallback for contact patches has not yet been run in the engine. It traces
  up to 45 times per contact against that one object; scraping along terrain or a complex-as-simple
  wall repeats that on every hit event.
- No sparks or glass particles, no sound; steam and smoke are simple puffs. Panel rattles and scraping
  feedback are still pending.
- Wheels and the steering wheel do not deform; toe is visible on the wheels.
- Interior and windscreen deform with the body when inside a dent's reach.

## Panel destruction and recovery: first implementation

URallyPanelComponent owns the fixing health of four doors, bonnet, boot lid, both bumpers, both front
wings, both headlamp units (with their corner indicators) and both tail lamps. Bumpers, wings and lamps
are held by two clips or bolts and no latch: they never swing open, hang from the survivor when one
goes, come off with R (spanner) and refit like any panel.
The radiator keeps its existing mechanical health. Hinges, latches and bumper clips take damage by
distance from the impact patch. Fixing health replicates as bytes; panel transforms are cosmetic.

- Intact, Loose, Hanging and Detached follow from fixing health. A loose latch wears above 80 km/h
  and from strong jolts; pristine fixings do not wear just from driving.
- Hinge motion is a rigid plate on its hinge line, integrated locally on every machine at 240 Hz
  (Rally::Panel::StepHinge): gravity, the car's own acceleration felt as the reverse force, wind
  pressure on the plate, hinge friction, and stops that bounce it back (restitution 0.3). A panel swings
  freely once its latch is gone. A free bonnet catches wind under its front edge and starts to lift at
  about 70 km/h, then flips onto the windscreen; a free front-hinged door swings out in corners, is blown
  shut going forward and wide open reversing. Latched panels rattle in their opening when loose.
- With one hinge (or bumper clip) gone, the panel also droops about the survivor. While it sits in its
  opening the body's edge limits that to 1.5 degrees (or 2 if the latch still holds); swung out, doors
  droop up to 20 degrees, bonnet and boot lid 12, a bumper hangs up to 35.
- Doors, bonnet and boot lid open and close by hand (**F**) when shut on a working latch and not jammed,
  strapped or hanging. A hand swings it out at hand pace (90 degrees/s) to its open angle (bonnet 55,
  boot lid 70, doors 65) and lets go. From there the panel swings on the same plate physics, held only by
  its door check, prop or struts, within a range of its open angle and up to a torque: doors 20 degrees
  and 30 N m (rear 25), bonnet prop 8 degrees and 120 N m, boot struts 40 degrees and 140 N m. Beyond
  either it swings free: braking swings front-hinged doors further open, acceleration and wind swing
  them shut (an open front door blows shut at about 35-40 km/h), and a hard stop knocks the bonnet off
  its prop. Swinging back into its frame with the free edge at 1 m/s or more, or pressed into it with
  15 N m or more, it catches its latch if the latch works (Rally::Panel::LatchCatchSpeed,
  LatchPressTorque; the server decides); otherwise it rests ajar and can swing open again. A front door
  is held while someone gets in or out. From a seat, F (or Y on a pad) opens or pulls shut that seat's
  own door. Opening is replicated per panel (bOpen); the motion is local. Tested: a hard stop from
  18 km/h slams the propped bonnet shut (edge 9.1 m/s, latch caught); driving off gently with the
  co-driver's door open, the wind swings it shut at about 39 km/h and it latches; in the 200 m drive a
  rear door left open swings shut under acceleration and is pressed into its latch.
- Collision: an attached panel out of its shut position is stopped by the ground, walls, trees and other
  cars (anything blocking the Visibility channel). Each corner of the panel's collision hull is joined to
  the hinge it hangs from by a line trace; a blocked line backs the panel off to the last clear angle
  (halving five times) and bounces it (restitution 0.3, resting below 20 degrees/s). Fast swings are
  checked every 4 degrees so a thin post cannot be skipped. A shut panel, or an open one on a parked car
  that has not moved, is not checked. The panel's own car is not traced: the shut position and the hinge
  stops stand in for its body. A bumper hanging from one clip rests on the ground at about 7 degrees
  instead of swinging to 35; a door opened against a post stops at it (27 degrees in the test).
- Refitted panels never sit as they left the factory. Each refit stores a shift (cm) and skew about the
  hinge line (FitOffset, FitRotation, replicated). How crooked follows Rally::Panel::FitSloppiness:
  0.25 for an unhurried bolt-on of a straight panel by a fitter facing it (about 2.5 mm and 0.3 degrees),
  plus up to 0.45 for rushing (average time between the three bolt actions: 0.75 s counts fully, 2 s or
  more not at all), 0.3 for not facing the mount (30 degrees off counts fully) and 0.4 for a bent panel
  (15 cm peak dent counts fully). Shift is 1 cm and skew 1.2 degrees per unit, the shift kept proud of the
  body rather than sunk into it. At 0.8 or more the latch (a bumper's clips) only half catches
  (health 150): the panel sits loose, rattles and can work free at speed. A strap job adds 1.5: always
  crooked, 1.75-2.9 cm and 2.1-3.5 degrees. F9 resets the fit.
- Swing damage (server only, from its own copy of the panel motion): when a panel out of its shut
  position meets something, the point that ran into it gives the speed: each probe's own path since the
  last frame, and the bit of panel where the hinge-to-corner line meets the obstacle followed back to
  where it was. From 3 m/s (a hand never swings a panel that hard) it dents there (that panel only, at
  Rally::Impact::DentFor of the speed, kept as if shut) and, if the blow turns the panel towards shut,
  slams it shut, catching the latch if that works; hinge loss 0.35 x (speed - 3) / 9. A blow forcing it
  open past its stop costs 2 x (speed - 3) / 9: about 27 km/h tears an open door off. An open panel whose
  hinges are both gone comes away (its latch holds nothing). Cooldown 0.25 s per panel. A free panel
  flung against its own open stop strains its hinges once its free edge arrives faster than 3 m/s.
  Doors, bonnet and boot lid opened or shut by hand move at hand pace (90 degrees/s) and a guiding hand
  never counts as a blow, so opening a door into a post stops it there without damage; let go, it stays. Tested: rolling forward at 6 m/s with the driver's door
  open into a post slammed it shut at 4.2 m/s with a new dent; reversing at 11 m/s with the rear door
  open into a post tore it off at 10.5 m/s.
- Damaged front doors can jam. Entry through a jammed door is refused; exiting tries the other door
  and then the roof. F9 clears jams.
- Detached parts collide with cars once clear of the body box of the car they came off (or were dropped
  beside); until then cars pass through them, because they start inside that box. Wheel raycasts still
  ignore them, so a car's body box pushes a part but its wheels do not ride over one.
- Detached parts have server-authoritative physics and replicated movement, no automatic lifetime,
  and their own compact dent snapshot. Later crashes do not dent a missing panel. Refit retains its
  existing dents. A missing front bumper multiplies damage from subsequent radiator hits by 1.35.
- Mesh copies now attach to the source part, so dented panels follow their hinges. Front-door mount
  positions match the export with the mirrors included.
- The 48-dent cap now applies equally on the server and clients, preventing divergent geometry.

Prototype controls on foot:

- Face a fallen part and press **E** to pick it up. Heavy panels slow movement to 1.5 m/s; bumpers to
  2.3 m/s. Sprint and jumping are unavailable while carrying. **G** drops the part.
- Carry it to the corresponding position on its original, stopped car. Press **E three times**, at least
  0.75 seconds apart, to tighten the fixings. A gap over four seconds or leaving reach resets progress.
  Bolting requires the spanner and at most 15 cm peak vertex displacement. It restores fixings, not dents.
- **B** straps on a carried part, including a crushed panel, or secures a loose/hanging panel in reach.
  Each bodge consumes one strap. Strong jolts and further impacts can break it.
- **F** opens or shuts the door, bonnet or boot lid in reach (hands must be empty). Bolt slowly and face
  the part to fit it straighter.
- A loose wheel is carried like a panel and bolted back on its corner with **E three times** (spanner;
  a wheel cannot be strapped on).
- **R** takes a part off into your hands (hands empty, car stopped): a bolted panel with the spanner in
  three actions (doors, bonnet and boot lid must be open to reach their hinge bolts; bumpers any time),
  a hanging panel pulled off in two tugs without tools, a strap cut in one, a wheel unbolted in three
  (with the jack under that corner it stays up; without, the corner settles onto its hub). Actions keep the bolting pace and restart after four
  idle seconds or on another part. Refitting it later gives the usual imperfect fit. Running gear is
  taken off the same way (see Running gear below).
- F and R act on the part most nearly in view among those in reach; the client sends its choice and
  the server checks reach and state, as for picking up.
- Every player temporarily starts with one spanner and two straps. This is a prototype field kit;
  retrieving tools and spare fixings from an inventory remains to be built.
- Pickup and repairs validate distance, line of sight, stopped-car speed, part identity, carrier,
  fixing state, tool availability, action timing and strap consumption on the server.

Scope still pending within destruction step 1: two-person carrying, bumper drag forces, panels
colliding with their own car beyond the shut position and hinge stops, wheels riding over loose parts,
missing-panel rain/temperature effects, rattle sounds, and a general inventory. Parts persist during the current session only. Carrying uses a simple
fixed pose without arms; the carried mesh can clip nearby scenery. All players in the current single-team
lab may recover parts; rival-team permissions are still undecided.

## Wheels coming off

A direct wheel hit (patch within 70 cm of the wheel centre) tears the hub (ERallyPart::Hub*, health 0)
when that corner's suspension was already at 0 before the hit and the hit is at least 5 m/s, or when the
hit alone is 22 m/s or more (about 60-70 km/h into a wall); Rally::Impact::TearsWheelOff. The 50 km/h
barrier test leaves a healthy corner's wheel on, and breaks the suspension to 70%; it takes two more such
hits on that corner to lose the wheel.

- The wheel spawns as a loose part (slots 15-18 in URallyPanelComponent, ARallyDebrisActor without a
  dent copy), thrown out sideways and still spinning. The car's wheel mesh is hidden.
- The corner rides on the hub and brake disc: radius 15 cm instead of 31, grip 0.45, and a scraping drag
  of 0.5 added to the surface's rolling resistance. The corner drops (8.6 cm measured at the pivots).
- A wheel carries its tyre's condition: bolting the same wheel back restores the hub and keeps a flat
  tyre flat; the spare wheel brings a good tyre. Any wheel fits any corner. Bent suspension stays.
- F9 repairs hubs too and removes loose wheels.

## Running gear: struts, arms, brakes, tie rods, driveshafts

Every corner has its strut, arm (front lower arm, rear trailing arm) and brake disc with caliper; the
front corners also have a tie rod and a driveshaft. They are separate meshes that move with the wheel
(hub and disc ride with it, arms swing about their inner pivot, struts and driveshafts stretch between
body and hub; ARallyCarPawn::SuspensionMounts, positions from assembly.json). The rear is the model's
hidden 4x4 multi-link (upper and lower arms, trailing arm, driveshafts) in place of the torsion beam;
the car is still front-wheel drive, so the rear driveshafts and the rear upper and lower arms are only
visual for now.

Damage (Damage/SuspensionRules.h; ERallyPart Strut*, TieRod*, Driveshaft*, Brake*), from the same
wheel-area impacts as the arms, each by change of velocity and reach:

| Part | Starts at | Gone at | Reach | What the driver feels |
| --- | --- | --- | --- | --- |
| Strut | 4 m/s | 16 m/s | wheel area, crush counts | spring rate 0.5-1.0 and damping 0.2-1.0 of new: the corner sags and bounces (broken: 1.5-2.5 degrees of lean) |
| Tie rod | 3 m/s | 12 m/s | wheel area, half the crush | bent: toe-in up to 3 degrees, pulls; broken: the wheel no longer steers and flops 6 degrees out (pulled 28 degrees in the drive test) |
| Driveshaft | 6 m/s | 20 m/s | wheel area, crush counts | broken: the open differential sends all the drive to the free stub, so the car has no drive (moved 1.1 m in the drive test) |
| Brake | 5 m/s | 20 m/s | direct hits only | braking 0.15-1.0 of new: the car pulls, and spins under hard braking when one corner is gone |

The readout lists each damaged part by corner. A part that has been taken off counts as broken (health 0)
until something is fitted.

Working on it (on foot, car stopped, spanner):

- The boot carries a kit, stowed on the boot floor and reachable only with the boot lid open: a spare
  wheel, the jack, a front and a rear strut, a front and a rear arm, two tie rods, a front driveshaft and
  a front and a rear brake disc. Each piece is picked up with **E** like a loose panel.
- Jack: carried to a corner and placed with **E**; it stands under the sill by that wheel and winds the
  car up there at 0.1 m/s to 38 cm (the corner's pivot rose 10.4 cm in the test, wheel clear of the ground),
  its foot gripping so the car cannot roll off. **E** with empty hands at that corner lowers it, and it is
  left loose on the ground. A wheel taken off with the jack in place leaves the corner up; without it the
  corner drops onto its hub.
- **R** three times unbolts a part into your hands. Everything except a tie rod sits behind the wheel,
  so the wheel comes off first. The part keeps its condition.
- Carrying a part to a corner where that kind is missing, **E** three times bolts it on. A part fits its
  own kind on its own axle, either side; it brings its own condition (a spare is new). A wheel goes on any
  corner whose strut, arm and disc are all there.
- **B** hammers a bent arm or tie rod roughly straight with the spanner: a bodge to 60%, flagged, never
  better and never repeated upwards; broken parts must be replaced.
- F9 also lowers the jack, removes the car's loose and stowed parts and restocks the boot kit.

All of it runs on the server through the usual RPCs (the client sends the part it chose; reach, state,
tools and timing are checked there); slot state (removed, which part is where, the jack's corner)
replicates with the panel conditions.

Kit parts ride attached to the car without welding to it (bAutoWeld off on every loose part). Unreal welds
attached auto-weld bodies into a body that starts simulating, which happens on a client the moment it
takes over as driver; the welded spare disc's 7 kg replaced the car's 1300 kg and launched it about 47 m
up. A car taking over simulation also starts its dampers from the measured compression, which removed a
smaller 0.7 m/s jolt.

## Engine, cooling and battery

Rules in Damage/EngineRules.h (standalone tests); state, replication and the steam in
Damage/RallyEngineComponent.h. Prototype tuning.

- Ignition: the engine runs while someone is in the driver's seat, the ignition is on, the engine is not
  seized and the battery works. Taking the seat turns the key; **I** (driver) switches it off or tries to
  start it. With nobody driving it is off. A stopped engine makes no torque and leaves the clutch open, so
  the car rolls freely.
- Temperature, run by the server from the power the engine makes at the crank (the simulating machine
  sends it with its car state) and replicated in whole degrees, with the coolant in tenths of a litre:
  - heat in: 6 kW while running plus 75% of that power;
  - heat out: the radiator, up to 2.5 kW per degree above the air: the thermostat opens from 82 to
    94 C, air comes from road speed (the fans give a parked car half of full flow above 98 C), a crushed
    core works at 30-100%, and below 70% full the pump starts drawing air (below 30% nothing reaches the
    radiator); plus 0.08 kW per degree off the block;
  - heat capacity: 50 kJ per degree plus 4 per litre of coolant.
  A healthy car settles at 82-98 C whatever it does (full power at 30 km/h is the hottest case).
- Coolant: 6 L. A radiator below 75% leaks, more the worse it is: about 0.1 L/s at 0 (empty in about a
  minute), 0.004 L/s once sealed at 60%. A stopped or cold engine only drips (30%). Above 118 C the coolant
  boils off through the cap.
- Engine (ERallyPart::Engine): wears above 112 C (about a minute from the red to seized at 130 C and more)
  and from very hard hits on the block (10 m/s starts, 35 m/s takes it all; 40 cm reach). It keeps
  40-100% of its torque with health, and knocking cuts up to 60% more from 125 to 150 C; a worn engine
  also drags (friction up to 2.5 times). At 0 it is seized: it stops and will not start. Service only.
- Battery (ERallyPart::Battery), on its tray behind the left headlamp: hard hits nearby (6 m/s starts,
  24 m/s takes it all; 30 cm reach plus crush). Below 50% it loses contact now and then (up to 0.6 times
  a second, 0.4 s each): the engine cuts out and the clutch opens for a moment. At 0 the engine stops and
  will not start.
- Symptoms: steam from the grille while the radiator leaks onto a warm engine, steam from under the
  bonnet when the coolant boils, grey haze from under the bonnet when it has run dry above 112 C, blue
  smoke from the tail pipe below 60% engine health. Each machine makes the puffs itself from replicated
  state (an instanced sphere with M_Prototype_Steam, opacity and colour per instance). The driver's
  readout carries temperature and coolant like a dashboard gauge, and "engine off (I: start)", "engine
  seized" or "no electrics".
- Fixes, on foot in front of the car with the bonnet open (or gone):
  - looking at the radiator shows the coolant level and whether it is leaking;
  - **B** at the radiator seals the leak from the tin of sealant (one per player for now): a bodge to 60%.
    A crushed-flat core (0) is beyond sealant;
  - **B** at the battery cleans and tightens the terminals and ties the battery down: a bodge to 60%,
    even from 0;
  - the boot kit has a 5 L water can: carried to the radiator, **E** pours in what fits. Cold water
    cools a hot engine as it mixes (dry at 127 C plus 5 L came back to 95 C). The can stays in hand with
    whatever is left;
  - switching off and waiting cools it slowly (130 to 127 C in 20 s with no coolant).
- F9 also refills the coolant, resets the temperature and switches the ignition on.
- Not built yet: oil (sump hits, oil light), over-revving, filling the can from a stream, a spare
  radiator or battery, sound (knocking, hiss), and water reaching the electrics.

Test-Engine (2026-10-12): the barrier hit leaves the radiator at 4%, the battery at 60% and the engine at
98%. Circling at half throttle (about 35 km/h, 35-40 kW) the coolant drops from 5.2 L to nothing in 57 s
with steam at the front (108 C then); 131 C at 103 s with the engine worn to 84%, then switched off at
79%; standing 20 s it fell to 127 C, and 5 L of water brought it to 95 C.

## Underside: sump, fuel tank, exhaust, gearbox

Rules in Damage/UndersideRules.h (standalone tests); oil and fuel live in URallyEngineComponent with the
coolant. Prototype tuning.

- Floor hits: contacts pushing up on the body's floor (a rock, a crest, a landing that bottoms out)
  still make no dent, but they are summed like impacts (0.1 s) and the simulating machine reports them
  to the server (ServerReportUnderside, same checks and burst cap as impacts). Each part within 35 cm of
  the contact patch takes damage from the car's upward change of velocity: sump from 0.6 m/s, exhaust
  from 0.5, tank from 0.8, gearbox casing from 1.0. A broad flat contact (the whole floor landing on the
  ground) spreads the load and does a quarter of that. Points, assembly frame: sump (150, 15, 17), gearbox
  (142, -10, 21), tank (-76, 6, 20), exhaust front pipe, middle and silencer.
- Oil sump (ERallyPart::OilSump): 4.5 L of oil. Below 80% it leaks (up to 0.05 L/s at 0, less with the
  engine off); oil dripping onto the hot front pipe smokes. Below 1.5 L the oil light comes on (readout
  "OIL") and the engine wears, the emptier and the higher the revs the faster (dry at 6000 rpm: seized in
  under a minute).
- Fuel tank (ERallyPart::FuelTank): 55 L. The engine burns about 0.8 L an hour idling plus a third of a
  litre per kWh (14 L an hour at 40 kW); below 70% the tank leaks on top (up to 0.03 L/s at 0). Empty, the
  engine stops and will not start. The readout carries a fuel gauge.
- Exhaust (ERallyPart::Exhaust, its own mesh SM_Octavia_Exhaust hung from its front hanger): below 35% it
  has torn from its rear hangers, hangs 4 degrees down with the silencer on the ground and drags (up to
  350 N at the silencer). No sound yet.
- Gearbox (ERallyPart::Gearbox): below 60% it jumps out of gear under load now and then (up to 0.25 times
  a second, 0.6 s in neutral); it loses fifth below 50%, fourth below 30%, third below 15%, and at 0
  nothing engages. The automatic skips lost gears.
- Fixes, on foot, car stopped. Getting under the car needs that end on the jack: the front (either front
  corner) for the sump and gearbox, the rear for the tank and the exhaust's strap.
  - from in front of the car, **B** at a cracked sump patches it with epoxy putty (two patches per player
    for now): a bodge to 60%, which stops the leak;
  - from in front, **B** at a damaged gearbox frees the gear linkage and tops up its oil with the spanner:
    a bodge to 60%, all gears back and no more jumping out; a broken one (0) is a workshop job;
  - from the car's left, **B** at a holed tank patches it the same way;
  - from behind, at a hanging exhaust: **B** wires it up with a strap (bodge to 60%, back on its hangers),
    or **R** twice pulls it off and leaves it on the ground, jack or not (no drag, nothing to refit it with
    yet);
  - with the bonnet open, the dipstick shows the oil level at the engine's filler, and the boot kit's 4 L
    bottle of oil is poured in there with **E**, like the water can.
- F9 also refills oil and fuel and puts the exhaust back.
- Not built yet: refuelling (a fuel can), refitting a pulled-off exhaust, landings tuned against real
  jumps, gearbox wear from abuse, sound.

Test-Underside (2026-10-12): a ridge 1.6 m long and 50 cm wide rising to 33 cm, driven over at 32 km/h,
gives two floor hits (1.1 m/s at the front of the floor, 1.25 m/s in the middle) and no dent: sump 70%
(seeping), fuel tank 79%, exhaust 43%, gearbox 97%. The scripted drive and the barrier crash report no
floor hits. In the two-player version the client's reports reach the server, which applies them, and the
state comes back to the client.

Validation commands:
- scripts\Test-Rules.cmd
- powershell -ExecutionPolicy Bypass -File scripts\Test-Panels.ps1
- powershell -ExecutionPolicy Bypass -File scripts\Test-Drive.ps1 -ExtraArgs '-RallyLoosePanels'
  (bonnet and rear-left door latches gone and the rear-right door left open before the drive;
  RALLY_AUTODRIVE loose lines log their angles)
- powershell -ExecutionPolicy Bypass -File scripts\Test-Crash.ps1
- powershell -ExecutionPolicy Bypass -File scripts\Test-Engine.ps1
- powershell -ExecutionPolicy Bypass -File scripts\Test-Underside.ps1
- scripts/Test-TwoPlayer.ps1 -LagMs 50 -ExtraArgs '-RallyPanelTest'

The panel functional test seeds fixing failures, then uses ordinary on-foot interaction RPCs to pick up
and bolt on a dented bumper, reject bolting a crushed bonnet, strap it on, break the strap with another
impact, and repair everything while a panel is being carried. It then opens and shuts the bonnet by
hand, leaves it hanging from one hinge (must not open by hand, sags onto the body's edge), breaks the
front-left corner with one side hit (wheel stays on) and tears the wheel off with a second, checks the
corner drops, and carries the wheel back and bolts it on. A refitted bumper must sit off square and a
strapped bonnet crooked; the hanging bumper must rest on the ground before its 35-degree stop; finally the
driver's door is opened from the seat against a post (must stop at it), then swings fully open once the
post is removed. Then the car rolls forward with the driver's door open into a post (slammed shut, new
dent), reverses with the rear-left door open into a post (torn off), the boot lid is opened and unbolted
by hand and bolted back on, and the hanging bonnet is pulled off by hand and dropped. It also checks that independent debris
geometry matches its original and that later dents exclude missing panels. Then the running gear: open
the boot, carry the jack to the front-left corner and lift it, unbolt the wheel (corner stays up), unbolt
the strut (counts as broken) and bolt on the spare from the boot (new), hammer a half-bent tie rod to the
bodge level, bolt the wheel back and lower the jack (corner back at ride height within 3 cm). Last the
engine bay: with the radiator at 30%, the battery at 20% and 1 L of coolant left, open the bonnet, seal
the radiator (B, uses the tin), bodge the battery (B), fetch the water can from the boot, pour it in at
the radiator (E: 1.0 to 6.0 L, can empty), put the can down and shut the bonnet. Then under the car, with
the sump and tank at 30%, the exhaust hanging and 1 L of oil left: patch the sump from the front and the
tank from the left (B, putty), pull the exhaust off from behind (R, it stays on the ground), open the
bonnet, take the oil bottle from the boot, pour it in at the filler (to 4.5 L, 0.5 L left in the bottle),
put the bottle down and shut the bonnet. The under-car steps first check that nothing can be patched
without the jack, then put it under the front for the sump and the gearbox (bodged back to all gears) and
under the rear for the tank.

Results 2026-10-12: Test-Rules 90/90 and 38/38; Test-Panels passes all 45 phases (jack lift 10.4 cm,
lowered back to 0.0 cm); Test-Crash and Test-Drive (196.9 m) pass; Test-TwoPlayer -LagMs 50 passes the
network comparison (shape error mean 0.12 m) and, with -RallyPanelTest, SERVER_PASS and CLIENT_PASS. The
two-player panel test runs about four minutes; the script gives the host 330 s for it.

Results after the engine work (2026-10-12): Test-Rules 109/109 and 40/40; Test-Panels passes all 51
phases; Test-Crash, Test-Engine and Test-Drive (202.4 m) pass; Test-TwoPlayer -LagMs 50 passes the network
comparison (shape error mean 0.07 m, the client's engine started by the server when it took the seat)
and, with -RallyPanelTest, SERVER_PASS and CLIENT_PASS with the engine-bay work done from the client.

Results after the jack requirement, gearbox fix and sound (2026-10-12): Test-Rules 121/121 and 43/43;
Test-Panels passes all 59 phases; Test-Underside (also with sound recorded), Test-Crash, Test-Engine and
Test-Drive (204.8 m) pass; Test-TwoPlayer -LagMs 50 passes the network comparison (shape error mean
0.04 m) and, with -RallyPanelTest, SERVER_PASS and CLIENT_PASS.

Results after the underside work (2026-10-12): Test-Rules 121/121 and 43/43; Test-Panels passes all 58
phases; Test-Underside, Test-Crash, Test-Engine and Test-Drive (199.4 m, no floor hits) pass;
Test-TwoPlayer -LagMs 50 passes the network comparison (shape error mean 0.12 m) and, with -RallyPanelTest,
SERVER_PASS and CLIENT_PASS.


Panel validation results, 2026-10-11 (Unreal 5.8.3):
- Editor module builds successfully.
- Standalone rules: 40/40 damage and panel checks, 38/38 vehicle physics checks.
- Test-Panels: passed pickup, three bolt actions, dent preservation, crushed-panel rejection,
  strap refit and failure, and F9 cleanup while carrying.
- Real barrier crash: 47.6 km/h approach, 17.1 m/s impact severity. Bonnet latch failed with both
  hinges intact; bumper clips fell to 25/255 and remained attached. Existing front suspension and
  radiator damage remained 70% and 4%. Reverse heading changed -0.53 degrees. Dent build: 53.5 ms.
- Test-TwoPlayer -LagMs 50 -ExtraArgs '-RallyPanelTest': SERVER_PASS and CLIENT_PASS, zero engine
  errors/ensures. The remote client performed recovery via ordinary RPCs. Server assertions verified
  one strap consumed, repeat bodge rejected without consumption, and repair clearing all debris.
- Pickup RPCs identify the client's chosen part, preventing latency from turning a rejected pickup
  into entering a nearby seat. Pickup ignores the source car's approximate body box; world obstacles
  and other cars still block reach.
- Inspected hanging-bumper, carried-part, refitted-bumper and barrier-crash screenshots. Carrying remains
  a placeholder pose. A heavy dent rebuild reached 104 ms while two game processes ran on this PC;
  that existing performance limitation still needs work.
- Late joining during a detach/carry and separate-PC testing remain untested. The independent dent
  snapshot is designed to reconstruct debris without depending on the source car's replication order.

Crush shape rework, 2026-10-11: the old dent noise (push varied 35% every 12 cm, sideways buckle up to
45% of the push every 16 cm) shredded parts under a flat patch; a detached front bumper no longer read
as a bumper. Same 47.6 km/h barrier hit after the rework: bumper and grille keep their shape, bonnet
folds in a few creases, max displacement bonnet 26.4 cm (was 31.6), bumper 28.9 cm (was 32.9).
Test-Rules, Test-Crash, Test-Panels and Test-TwoPlayer -LagMs 50 -ExtraArgs '-RallyPanelTest' pass.
Dent build time unchanged (about 50 ms).
