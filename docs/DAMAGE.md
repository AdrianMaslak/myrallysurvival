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
   small patch, a wall the whole face.
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

Dents are chunky: each dent's shape is sampled on a 12 cm grid in the car's frame and blended linearly between
(Rally::Crush::FacetCm, URallyBodyDeformComponent::ApplyDent), so metal folds in flat facets with straight creases
instead of a smooth, wobbly bowl; the fold creases on top are as before.

The windscreen cracks and lets go. Where it crazes (vertex colour R), a spider-web of crack lines
(T_Windscreen_Cracks, scripts/build_game_textures.py) shows through M_Windscreen_v2 as pale, near-opaque lines. Once
the glass has been pushed 7 cm or the roof over it crushed 18 cm (Rally::Crush::WindscreenOut), it falls out of the
car (ARallyCarPawn::UpdateWindscreen): every machine hides it from the replicated dents, so a saved or repaired car
shows it right, and when it happens while playing the pane drops off as a loose, unreplicated physics piece for 45 s.

- Tyre: only direct hits (patch within 70 cm of the wheel centre). Health 0 = flat: 0.2 bar (45% grip,
  sluggish) and the wheel rides 7 cm lower on the rim.
- Suspension (each corner's arm: front lower arm with the steering's track rod, rear trailing arm):
  direct hits, or hard hits crushing back into the wheel well (reach fades over 1.5 x dent depth). Lost
  health bends the wheel outward, up to 5 degrees toe-out at 0, so a damaged corner pulls; a side hit bends a
  corner up to 1.8 times as much as one along the car (Rally::Impact::SideHitScale); at 0 the corner collapses: 13
  degrees more toe, ground drag 0.3, and the wheel leans in 12 degrees at the top and tucks 5 cm under the body
  (a bent one leans up to 4), so the car crabs and drags and is not drivable at speed; a snapped front
  arm no longer steers its wheel.
- Struts, brakes and front driveshafts: see Running gear below.
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
- On another map: scripts/Test-Crash.ps1 -Map /Game/Maps/Countryside/L_Countryside -ExtraArgs
  '-RallyCarStart=-3900,150,120,-90' starts the car 30 m from the countryside service building's wall
  (-RallyCarStart=X,Y,Z,Yaw in cm and degrees) and drives into it: 47.1 km/h, 17.1 m/s, the same parts
  damage as at the lab barrier.

Results 2026-10-11:
- Single player: barrier hit at 47.6 km/h, 17.1 m/s change of velocity (the car bounced back at
  11 km/h); whole front face patch; front crumpled, bonnet buckled and scraped; radiator 4%, both front
  suspension corners 70% (1.5 degrees toe-out each); tyres intact. Reversing straight afterwards drifted
  0.6 degrees. Building the dent took 45 ms (one hitch at the moment of impact). Since the running gear
  was added (2026-10-12) the same hit also leaves both front struts at 71% and both front driveshafts at
  77%; brakes come through.
- Two players, about 100 ms round trip: the client's impact reached the server, which applied it; the
  dent and part state replicated back, both sides logged the same state.
- Scripted drive (Test-Drive.ps1, 196 m including off-road corner and reverse): no damage recorded.

## Limits and next steps

- The 45-70 ms dent build hitches one frame per heavy impact; a long scrape along a wall could stutter.
  Parallelising the vertex loop is the first fix if it shows in play.
- The contact patch is the part of the body box's face within 12 cm of the nearest sample to what was
  hit. Simple shapes (the lab's boxes) are measured with their closest point; scenery whose collision is
  its own triangles (the countryside's buildings, trunks and terrain) by tracing across the face. Before
  that fallback (2026-10-12) such hits had no patch: a 47 km/h wall hit on the countryside landed as one
  point on a corner of the box and broke nothing; now it matches the lab barrier.
- Contact patches use the body box, so dents start at the box faces (bumpers, sides, roof), not at
  exact panel surfaces; rounded corners are approximate.
- No sparks or glass particles, no sound; steam and smoke are simple puffs. Panel rattles and scraping
  feedback are still pending.
- Wheels and the steering wheel do not deform; toe is visible on the wheels.
- Interior and windscreen deform with the body when inside a dent's reach; the windscreen falls out of a badly
  crushed frame. There is no part to fit back yet: a repair straightening the body puts it back.

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

## Running gear: wheels, struts, arms, brakes, driveshafts

Cut down on 2026-10-19 from 24 parts that broke to 18, each of which shows what is wrong with it. Every
corner has its wheel (tyre and hub: flat, or off), strut, arm (front lower arm, rear trailing arm) and
brake disc with caliper; the front corners also have a driveshaft. The front arm carries the steering's
track rod: the tie rods are no longer parts of their own (no fixings, nothing to aim at or break; their
ERallyPart and panel slots stay so saves line up, and a tie rod taken off or stowed in an older save is
back on its arm). They are the v2 suspension set's meshes (Art/Vehicles/Octavia/Suspension_v2,
`scripts/export_suspension_v2.py` and `scripts/import_suspension_v2.py`), each moving with the wheel:
- the hub (knuckle or rear hub carrier, with the drop link) and the brake (caliper, carrier, pads, hose)
  ride with it, the disc spins;
- arms, links and tie rods swing about their inner pivot;
- struts, the rear springs and the driveshafts stretch between body and hub
  (ARallyCarPawn::SuspensionMounts, positions from suspension_v2.json).
The subframes, steering rack, anti-roll bars and the rear final drive are one mesh on the body
(SuspensionStatic); the car's chassis mesh no longer carries the old ones. The rear is the 4x4 multi-link
with its driveshafts now showing; the car still drives its front wheels only, so the rear shafts, links
and final drive are only visual.

Fixings are the model's own fasteners, from the meshes' tagged sockets (as the engine modules'): a wheel's
five 17 mm bolts; a front strut's three top-mount nuts, centre nut and clamp bolt and nut, a rear damper's
four; a front arm's six bolts and nuts plus the knuckle's ball-joint pinch bolt and nut, a rear trailing
arm's seven; a brake's caliper and carrier bolts, hose union and disc screw (on the brake, which does not
spin, so the screw does not turn with the disc); a front driveshaft's six inner joint bolts and its axle
bolt. Every one takes a 10, 13, 17 or 19 mm socket, the screwdriver or hands (the model's 16 mm ones take
the 17).

What it looks like (`ARallyCarPawn::UpdatePartLooks`, every machine, from replicated health): the set's
own textures (M_Susp and its MI_Susp_* instances, times the model's grime), with a copy of each slot's
material per part. A bent arm kinks down in the middle, 3 to 9 cm (10 snapped; the material's Bend, both
ends where they were); a snapped arm or driveshaft hangs 8-12 cm at its outer end; a damaged strut darkens
towards oily grime and a damaged brake (disc and caliper) towards burnt brown (the material's Tint). Looking at a part says what it is and what is wrong:
"front left arm: bent 40%, the wheel points out", "rear right strut: spring broken, the corner sags",
"front right driveshaft: snapped, no drive", "front left wheel: tyre flat" (`URallyPanelComponent::PartState`).

Damage (Damage/SuspensionRules.h; ERallyPart Strut*, Driveshaft*, Brake*), from the same wheel-area
impacts as the arms, each by change of velocity and reach:

| Part | Starts at | Gone at | Reach | What the driver feels |
| --- | --- | --- | --- | --- |
| Strut | 4 m/s | 16 m/s | wheel area, crush counts | spring rate 0.5-1.0 and damping 0.2-1.0 of new: the corner sags and bounces (broken: 1.5-2.5 degrees of lean) |
| Driveshaft | 6 m/s | 20 m/s | wheel area, crush counts | broken: the open differential sends all the drive to the free stub, so the car has no drive (moved 1.1 m in the drive test) |
| Brake | 5 m/s | 20 m/s | direct hits only | braking 0.15-1.0 of new: the car pulls, and spins under hard braking when one corner is gone |

The readout lists each damaged part by corner. A part that has been taken off counts as broken (health 0)
until something is fitted.

Working on it (on foot, car stopped, spanner):

- The boot carries a kit, reachable only with the boot lid open: a spare wheel lying in its well under
  the load floor, and on the floor the jack, a front and a rear strut, a front and a rear arm, a front
  driveshaft and a front and a rear brake disc (BOOT.md). Each piece is picked up with **E** like a loose
  panel.
- Jack: carried to a corner and placed with **E**; it stands under the sill by that wheel and winds the
  car up there at 0.1 m/s to 38 cm (the corner's pivot rose 10.4 cm in the test, wheel clear of the ground),
  its foot gripping so the car cannot roll off. **E** with empty hands at that corner lowers it, and it is
  left loose on the ground. A wheel taken off with the jack in place leaves the corner up; without it the
  corner drops onto its hub.
- **R** three times unbolts a part into your hands. It all sits behind the wheel, so the wheel comes off
  first. The part keeps its condition.
- Carrying a part to a corner where that kind is missing, **E** three times bolts it on. A part fits its
  own kind on its own axle, either side; it brings its own condition (a spare is new). A wheel goes on any
  corner whose strut, arm and disc are all there.
- **B** with the hammer knocks a bent arm roughly straight: a bodge to 60%, flagged, never better and
  never repeated upwards; snapped parts must be replaced.
- F9 also lowers the jack, removes the car's loose and stowed parts and restocks the boot kit.

All of it runs on the server through the usual RPCs (the client sends the part it chose; reach, state,
tools and timing are checked there); slot state (removed, which part is where, the jack's corner)
replicates with the panel conditions.

Kit parts ride attached to the car without welding to it (bAutoWeld off on every loose part). Unreal welds
attached auto-weld bodies into a body that starts simulating, which happens on a client the moment it
takes over as driver; the welded spare disc's 7 kg replaced the car's 1300 kg and launched it about 47 m
up. A car taking over simulation also starts its dampers from the measured compression, which removed a
smaller 0.7 m/s jolt.

Test-Parts (2026-10-19) photographs each corner with its wheel hidden, sound and then damaged (front left
arm bent 40% and tyre flat, front right arm and driveshaft snapped, rear left disc ruined, rear right strut
broken: `RallyParts_<corner>_sound.png` and `_damaged.png`) and the open boot (`RallyParts_boot.png`),
and checks every part's look-at state, the bend and that the snapped front arm no longer steers.

## Fixings and tools

Between the earlier three presses per part and a game of every bolt: each part has 2-4 fixings you can
see, each turned with one tool (Damage/FixingRules.h, `URallyPanelComponent`).

| Part | Fixings |
| --- | --- |
| Doors | 4 hinge bolts, 13 mm, inside the door by its front edge (door open) |
| Bonnet, boot lid | 4 hinge bolts, 10 mm, on the underside (open) |
| Bumpers | 2 bolts, 10 mm, and 2 screws on the face |
| Front wings | 4 bolts, 10 mm, along the top edge (bonnet open) |
| Headlamps | 4 bolts, 10 mm, on top (bonnet open) |
| Tail lamps | 4 nuts, 10 mm, inside the boot (boot open) |
| Wheels | 4 bolts, 17 mm |
| Struts | 2 bolts, 19 mm, at the foot; a 13 mm brake hose bracket (wheel off) |
| Front lower arms | ball joint nut and pivot bolt, 17 mm (wheel off) |
| Rear trailing arms | 2 bolts, 17 mm (wheel off) |
| Brake discs | 2 caliper carrier bolts, 13 mm, and a screw (wheel off) |
| Driveshafts | hub nut 19 mm, 2 joint bolts 13 mm (wheel off) |

- Tools: bare hands, 10/13/17/19 mm spanners, screwdriver, pliers, hammer, picked with 1-8 or the mouse
  wheel and replicated (`ARallyCharacter::HeldTool`). Everyone has the whole set for now; the physical
  toolbox comes with the inventory.
- Turning: aim at the fixing and hold E (home) or R (out) for 0.5 s with the right tool. The client sends
  the turn when the hold completes; the server checks reach, the tool, access (a shut door, a wheel still
  on) and the pace (one fixing per 0.4 s per player) in `URallyPanelComponent::TurnFixing`.
- Off and on: with every fixing out, R lifts the part off into your hands. A carried part is offered up
  with E (in place, all fixings out) and holds once they are home. A body panel's final fit (how crooked
  it sits, whether its latch half catches) is settled when the last fixing goes home, from how squarely it
  was offered, how bent it is and how hurried the bolting was.
- Loose fixings: on a body panel the two fixings of each hinge or end cap that hinge's health (one out:
  loose, it rattles; both out: held by nothing). A wheel or suspension part with fixings out acts damaged
  (at most half health; none home 0.15), so a loose wheel wobbles. Driving works the rest loose (about one
  a minute at 60 km/h with half out, much faster over bumps); a wheel or suspension part with none left
  home drops off the moving car, a panel's hinge wear finishes it off.
- Telling it is loose: each fixing that works loose clinks (EEvent::BoltLoose) and the last one clunks
  (PartDrop), on every machine (`URallyPanelComponent::OnFixingLoose`, multicast). Loose parts rattle
  while the engine runs (half) or the car moves (full from 40 km/h), as loud as the loosest part's share
  of fixings out (`Rally::Fixing::RattleLevel`). An engine module driven with fixings out is shaken out of
  true over about 8 s, 3 to 6 degrees and up to 1 cm down, and chatters on the move; with none home it
  hangs on its hoses, 14 degrees over and 4 cm down (`LooseTiltDeg`, `LooseDropCm`), and its prompt says
  "hanging loose". One unbolted on a parked car stays where it is, to be lifted off; every fixing home (or
  the part off) puts it straight.
- Aim: a fixing counts when the view ray meets it (a 3 cm sphere) in front of, or just into, the part it
  hits; running gear at the same corner can be reached round (15 cm), a front wing's hull does not block
  its own wheel arch, and parts behind a wheel still on cannot be got at.
- Aim assist (2026-10-19, after the playtest found bolts hard to hit): with the crosshair on a part within
  arm's length (130 cm) whose fixings can be got at, its nearest fixing within 7 cm of the crosshair is the
  one aimed at (`Rally::Fixing::AssistRadiusCm`); not on a closed door or bonnet, nor behind a wheel, so it
  does not take E from getting in.
- Finding them: the part in view lights its fixings amber and draws them 1.8 times their size, and the one
  in the crosshair bright yellow at 2.2 (M_Prototype_Glow; `URallyPanelComponent::SetAimHighlight`, each
  player's own view only).
- **Placing them by hand**: a socket named `Fixing0`, `Fixing1` ... on a part's static mesh puts that
  fixing there, its head facing along the socket's X axis; the numbers follow the part's fixing order
  (its prompt and the bolt survey's log list them). Add the sockets in Unreal's static mesh editor
  (Socket Manager), or in Blender as empties named `SOCKET_Fixing0` ... parented to the mesh (the FBX
  import turns them into sockets). Parts without sockets keep the places worked out from their bounds.
  Test-Parts adds a socket to the front right driveshaft at run time and checks the bolt moves onto it
  and back.
  The 1.8T engine's modules carry theirs from the model (scripts/import_engine_18t.py, where the model's own
  bolt points are); `scripts/set_fixing_sockets.py` held the prototype modules' and is no longer used.
- Wheel bolts sit on the rim's face, 1.3 cm in from the tyre wall (measured off the wheel mesh); they were
  3 cm behind it, out of sight. Test-Parts photographs them lit (`RallyParts_wheel_bolts.png`) and lights
  each corner's arm and strut bolts in its corner shots.
- `scripts\Test-Parts.ps1 -BoltSurvey` photographs every part with fixings from outside, its fixings lit
  (doors, bonnet and boot open, wheels out of the way unless it is a wheel): `RallyBolts_<slot>.png`. The
  first survey (2026-10-19) found the front driveshafts' hub nut inside the hub and the front arms' pivot
  bolt facing the ground: the nut now sits in the middle of the hub's face, just proud of the disc, and the
  pivot bolt's head faces forward. Tail lamp nuts are reached from inside the boot, so they do not show
  from outside. The survey centres each shot on the part's fixings and logs where they are and how each is
  drawn (shown, its material, its distance from where it should be); a door is seen from the doorway behind
  its hinge. The doors' hinge bolts first sat inside the door skin (lower) and the A-pillar (upper); they
  now sit on the door's inner face by its front edge, flush with the trim (measured by casting rays at the
  door meshes), and all four of each door show. The boot lid is seen from behind and above (its bolts face
  down, seen from below). An
  engine module is shot with what comes off before it hidden (cover and air filter box; the front bumper
  for the intercooler, hidden with the dent-able copy it is drawn by): the exhaust manifold's, turbo's,
  starter's and intercooler's fixings show on top. With the 1.8T engine (OCTAVIA-VEHICLE.md, Engine)
  every module's fixings are the model's own bolts: the cover's quarter-turn screws, two of the air box's
  clips, four of the intake's eleven bolts, four of the exhaust manifold's top nuts, the turbo's three bolts
  to the manifold, the alternator's and starter's two (each with a third worked out on top), the computer's
  three. (The prototype's engine mesh covered its alternator and wedged its computer; sockets had moved
  those bolts into sight.) Each part is photographed
  half a second after the camera moves to it (the whole survey takes about a minute).
- Bumper, wing and lamp fixings sit on their part's collision surface. Every fixing is drawn with the 1.8T
  model's own head for its kind (a hex bolt with its washer, a hex nut, the cover's quarter-turn screw, an
  air box clip, scripts/export_engine_18t.py), bolts and nuts sized by their socket, in zinc or plastic
  unless lit. Turning one shows: while the key is held it spins in the socket and rises along its own
  axis with the hold (two turns of thread from home to out), then eases the rest of the way in a fifth
  of a second once the turn is done, on every machine (URallyPanelComponent::ShowFixingTurn,
  AnimateFixings); a bolt standing out shows its thread between the part and its head. Test-Parts
  photographs a wheel bolt half turned and one undone, side on (`RallyParts_bolt_turning.png`,
  `RallyParts_bolt_out.png`).
- Bodges with tools: battery terminals and the gearbox linkage need the 10 mm spanner in hand, knocking an
  arm straight the hammer.
- Tests: `scripts\Test-Rules.cmd` covers the rules; the panel test (solo and two-player) takes the boot lid,
  a wheel and a strut off and puts them, the bumper and a wheel back on bolt by bolt with the right tools,
  aiming at each fixing as a player would (each aim logged `aim_on`/`aim_miss`).

## Engine modules

The engine is a block (with the gearbox and what is bolted to them, one mesh) and nine bolt-on modules,
each its own mesh: the 1.8T set's (OCTAVIA-VEHICLE.md, Engine), the intercooler with its two charge pipes. Each
has its own part condition (ERallyPart) except the cover; 0 means missing as well as broken.

Every fastener that holds a 1.8T module on is a fixing, at the real count and on the model's own bolt point
(Fixing sockets on the mesh, tagged with what it is and what turns it); tools are sockets (10/13/17/19 mm;
a 16 mm fastener takes the 17), a screwdriver or hands. A part held by many is worked with the ratchet spinning, each fixing quicker (down to
half the time: 0.25 s instead of 0.5), so a part takes about as long as one held by four.

Tight spots: holding the socket a fixing takes, its extension reaches it up to 25 cm behind a part in the
way, and of the fixings under the crosshair the one that socket fits is taken before a nearer one it does
not. The starter, low at the front of the engine with its bolts facing the block, is done from underneath:
crouched with the front on the jack, a hand reaches it past whatever is in front. Soft targets (the spheres
round the coolant tank and the battery) hide no fixing behind them. The part in view's fixings show
through what is in front of them: a faint amber copy of each head drawn over everything (M_Prototype_Ghost),
so a bolt round the back of the turbo or behind the radiator support can be found before it is reached
(URallyPanelComponent::UpdateThroughHeads). Test-Parts checks every module's
fixings can be reached from somewhere a player can put their eyes round the open bay (in front, beside each
wing, standing, leaning in and crouched; URallyPanelComponent::DebugReachableFixings), with what the game
makes come off first taken off: all 48 are.

| Module | Fixings | Covered by | Missing or broken |
| --- | --- | --- | --- |
| Engine cover | 4 quarter-turn screws, screwdriver | | nothing (keeps the dirt off) |
| Air filter box | 4 clips, by hand | | louder intake roar; runs unfiltered (dust wear later) |
| Intake manifold | 8 bolts and 2 nuts, 10 mm; the support bracket bolt, 13 mm | engine cover | will not run; damage restricts intake and leaks boost |
| Exhaust manifold | 13 nuts, 13 mm | engine cover, turbo | loses boost; sharp, loud open-port exhaust from the engine bay |
| Turbo | 3 bolts to the manifold, the support bracket and 3 banjo bolts (oil feed, coolant), 17 mm; 2 oil return bolts, 10 mm | engine cover, air filter box | about two thirds of the power |
| Intercooler | 2 bolts, 13 mm, 2 rubber mounts by hand, 4 clamps on its charge pipes (screwdriver) | front bumper | open charge path loses boost; about two thirds of stock power |
| Alternator | 2 bolts, 17 mm | | battery runs flat in about ten minutes |
| Starter motor | 2 bolts, 17 mm, from underneath (the front on the jack) | air filter box | the key only clicks |
| Engine computer | 2 bolts and a nut, 10 mm | | will not run |
| Ignition coils (4) | each its connector's clip, by hand; then it pulls out | engine cover | that cylinder does not fire (below) |

- All need the bonnet open (the intercooler can also be got at with the front bumper off, and its fixings
  need the bumper off: then a hand reaches round the core and along its pipes, like the starter's from
  below; Test-Parts checks all eight with the bumper detached). Fixings sit on
  top of each module, on its collision surface; the order is enforced the same way as a door's hinge
  bolts needing the door open.
- Rules: `Rally::Engine::FModules`, `ModulePower`, `CanCrank`, `Breathes`, `ChargeRate` (EngineRules.h).
  The car works out each module's felt health (missing, all fixings out, or loose fixings, Rally::Fixing)
  in `OnDamageChanged` on every machine and hands it to the engine (`URallyEngineComponent::SetModules`),
  so the driving machine's physics uses the same power.
- Boost delivery multiplies the turbo, exhaust manifold, intercooler and intake conditions. With the
  turbo, exhaust manifold or intercooler missing, the stock engine retains 65% power; damaged parts
  progressively lose boost. Turbo upgrades cannot recover power through a broken boost path. ECU/map
  tuning still applies to the atmospheric portion. Spark blowout and detonation use delivered boost.
  This is gameplay tuning: disconnected turbo oil/coolant lines and detailed air metering are not simulated.
- Sound follows those same conditions: manifold leaks blend sharp cylinder pulses into the engine-bay
  sound, bypassing the exhaust pipe and silencer. Missing boost removes turbo whistle and blow-off rush;
  the air filter box off exposes more intake roar. Exhaust/silencer damage keeps its separate pipe sound.
- Battery charge (`FRallyEngineState::ChargePct`): charged while running with a working alternator,
  drained without; flat (5%) it will neither crank nor keep running.
- Start failures: no starter clicks like a dead battery; no intake or computer cranks and does not catch;
  nor do fewer than two coils that fire ("no spark").
- Ignition coils (modules Coil1-Coil4, one on each cylinder's plug under the engine cover; their slots
  come after every other, so older saves load with four good stock coils). A coil that is dead, unplugged
  or missing leaves its cylinder dead: a quarter of the power gone, the idle 130 rpm lower
  (`IdleDropPerDeadRpm`, on the physics' idle governor), the engine shaking as a worn one does and popping
  on missed firings. On fewer than two cylinders it stops. Three good cylinders normally sustain idle,
  but a single dead coil can stall if revs sag below idle. Two dead coils or several badly worn coils
  stall after sustained unstable idle; holding revs above 1600 prevents that idle stall. The server
  accumulates instability, allows two seconds for starting, and clears it when running recovers.
  A stall stays off after repairs until the key is turned again. A weak coil (below 60%) misfires now and
  then, more the weaker it is (`CoilMisfire`); and under load a coil misfires at boost past what its grade
  holds (`Rally::Grade::CoilBoostLimit`: stock 1.1 bar, sport 1.5, rally 1.8), so a stock set at 1.4 bar
  loses about a quarter of its firings flat out. The misses take their share of the torque on the
  driving machine (`IntermittentPowerLoss`), lower idle, jolt the engine on its mounts and pop in the
  exhaust on every machine. Audio skips the affected cylinder in the 1-3-4-2 firing order, including at
  high rpm, instead of approximating dead coils with a fixed number of random pops per second.
  Heat cooks coils: overheating wears them half as fast again as the engine; crashes near them damage them
  like any module. Finding a bad one: with the engine idling and the cover off, unclip one connector at a
  time (R) - the idle drops on a good coil and does not change on the dead one. A coil fits any cylinder
  whose coil is out (FitTarget). Sport coils (900 Sk in the catalogue) are red. Development console:
  `RallyCoil <1-4> <health>` (0 dead, 0.3 misfiring). Test-Idle unplugs a good coil (866 to 727 rpm),
  kills coil 3 and unplugs it (727 stays 727), and fits a sport coil in its place (back to 866).
  It also checks missing turbo/manifold/intercooler power, stalls on two dead coils and on four weak
  coils, verifies that repairing them does not automatically restart, then restarts with the key.
  Verified 2026-10-20: Test-Idle passes (866 rpm healthy, 727 with one coil unplugged, 866 after
  replacement; each missing boost component gives 0.650 power and zero boost; both stall/restart cases
  pass). Test-Rules passes 201 part-condition and 59 vehicle-physics checks; Render-Sounds passes
  52 checks. Unreal Editor Development build succeeds.
- Crashes: each module near the impact takes damage (`ModuleDamage`, 30 cm reach); the intercooler
  right behind the front bumper takes most front hits.
- Assets: `scripts/export_octavia_parts.py` (PANEL_GROUPS and the built starter and ECU), hulls in
  `scripts/setup_damage_assets.py`.
- Test: the panel test opens the bonnet, finds the turbo's nuts out of reach, takes the engine cover, air
  filter box and turbo off (power falls to 0.65), then puts them all back (full power).
- Test-Knock replays the 2026-10-18 playtest that "blew" the engine with no warning: the wastegate at 1.4
  bar on the stock computer and the intake's bolts left out but two (it had four then, eleven since the 1.8T
  engine), circling flat out. 2026-10-19:
  pinking at full strength under load, blue smoke from 90% engine health (photographed parked and revving
  at 87%), the third and fourth bolts worked loose about a minute apart (clink, then clunk), the intake hung
  at 14 degrees (`RallyKnock_intake_before.png` and `_after.png`, engine cover out of the way) and the
  engine stopped at 66%. With -WithSound the recording's 3.8-5.6 kHz share went from 2.9% at stock boost
  to 7.4% at 1.4 bar.

## Grades and settings

Every part has a grade, worn, stock, sport or rally (Damage/PartGradeRules.h, `Rally::Grade`). The grade
travels with the part (`FRallyDebrisSource::Grade`): off one car, into the boot, onto another. Prompts
name it ("pick up spare rally front strut").

| Part | Worn | Sport | Rally |
| --- | --- | --- | --- |
| Strut | spring 0.92, damping 0.7, 0.5 cm higher | spring 1.3, damping 1.25, 2 cm lower | spring 1.5, damping 1.35, 1 cm lower, adjustable |
| Brake disc and caliper | torque 0.8 | 1.2 | 1.35 |
| Turbo | power 0.92 | 1.12 | 1.25 |
| Engine computer (chip) | power 0.95 | 1.06, safe to 1.1 bar | 1.1, safe to 1.3 bar |

Settings, changed by hand with T (one step up) or Shift+T (down); the tool in hand decides what:

- Rally strut, screwdriver: damper clicks 1 (soft) to 9 (hard), 5 as delivered (0.75 to 1.25 of the grade's
  damping). 19 mm spanner: the height collar, up to 3 cm either way.
- Turbo, 10 mm spanner at the wastegate: boost 0.5 to 1.4 bar, 0.8 as delivered; each tenth of a bar is 5%
  power. Past what the computer's fuel map allows (stock 0.9 bar) the engine knocks under load and wears
  (`KnockWear`: a tenth over costs about 0.1% of engine health per second at full load). The prompt at the
  wastegate says when the boost is over the limit, and the engine pinks under load: a metallic ticking,
  already clear a tenth of a bar over (`Rally::Engine::Detonation`).
- Same reach, access (wheel off for a strut, cover and air box off for the turbo) and pace as a fixing;
  `URallyPanelComponent::Adjust`. Settings stay with the slot (`FRallyPanelCondition::SetA`, `SetB`) and
  go back to as delivered when another part is fitted.
- Engine computer, screwdriver: the map, economy (power 0.92, fuel 0.85, 0.1 bar more boost before
  knocking), as delivered, or sport (power 1.05, fuel 1.15, 0.1 bar less).
- Brake bias, from the driver's seat: ] moves braking forward, [ back, 2% a step from 50 to 76% front (62.5%
  as built, from the calipers' sizes); the handbrake stays the rear's. Saved with the team.
- The parts catalogue (MONEY.md) sells sport and rally parts; the development console command `RallyPart <strut|rearstrut|disc|reardisc|turbo|ecu|intercooler>
  <worn|stock|sport|rally>` puts a spare part in front of you.
- Test: the panel test fits a rally strut from the boot, sets it three clicks harder and a centimetre down
  (spring 1.5, damping 1.6, 2 cm lower) and winds the turbo up to 1.0 bar (power 1.1).

## Engine, cooling and battery

Rules in Damage/EngineRules.h (standalone tests); state, replication and the steam in
Damage/RallyEngineComponent.h. Prototype tuning.

- Ignition: the engine runs while the ignition is on, the engine is not seized and the battery works.
  **I** (in the driver's seat, or at the driver's door from outside) switches it off or tries to start it.
  The key stays in when the driver gets out: the car drops out of gear with the handbrake on and the engine
  idles on, and a hand on the throttle in the bay revs it (CREW-AND-SEATS.md). A stopped engine makes no
  torque and leaves the clutch open, so the car rolls freely.
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
  smoke from the tail pipe from 90% engine health, clear from the start and thicker as it wears
  (`Rally::Engine::WornSmoke`); the worn big end's knock starts there too, and looking at the engine
  cover says how worn the engine is and whether it burns oil. Each machine makes the puffs itself from replicated
  state (an instanced sphere with M_Prototype_Steam, opacity and colour per instance). The driver's
  readout carries temperature and coolant like a dashboard gauge, and "engine off (I: start)", "engine
  seized" or "no electrics".
- Fixes, on foot in front of the car with the bonnet open (or gone):
  - looking at the coolant tank (right rear of the bay) shows the coolant level and whether the radiator is
    leaking; water from the can goes in there;
  - **B** at the coolant tank pours in the tin of sealant (one per player for now), which seals the
    radiator's leak: a bodge to 60%. A crushed-flat core (0) is beyond sealant;
  - **B** at the battery cleans and tightens the terminals and ties the battery down: a bodge to 60%,
    even from 0;
  - the boot kit has a 5 L water can: carried to the radiator, **E** pours in what fits. Cold water
    cools a hot engine as it mixes (dry at 127 C plus 5 L came back to 95 C). The can stays in hand with
    whatever is left;
  - switching off and waiting cools it slowly (130 to 127 C in 20 s with no coolant).
- F9 also refills the coolant, resets the temperature and switches the ignition on.
- Not built yet: over-revving, filling the can from a stream, a spare radiator or battery, a hiss from a
  leaking radiator, and water reaching the electrics.

Test-Engine (2026-10-12): the barrier hit leaves the radiator at 4%, the battery at 60% and the engine at
98%. Circling at half throttle (about 35 km/h, 35-40 kW) the coolant drops from 5.2 L to nothing in 57 s
with steam at the front (108 C then); 131 C at 103 s with the engine worn to 84%, then switched off at
79%; standing 20 s it fell to 127 C, and 5 L of water brought it to 95 C.

## Leaks

What a damaged car loses is seen as it goes (URallyLeakComponent on the car, ARallyPuddleField in the
world, rules in Damage/LeakRules.h with standalone tests). Drawn on every machine from the replicated
damage and fluid levels, so nothing of its own is replicated or saved.

- **Drops**: a cracked sump (oil, dark), a holed radiator (coolant, pale green; it is mostly water) and a
  holed tank (fuel) drip at the rates the engine drains by (UndersideRules.h, EngineRules.h), also with the
  engine off, while there is any left. A seep drips every two seconds, a cracked sump ten times a second;
  each drop carries its share of the litres lost. They leave with the car's motion and fall to the ground
  below, the radiator's across its width.
- **Puddles**: where drops land they join the puddle of that liquid they fall in, or start one: a decal on
  the ground (M_Puddle, ragged edged) as big as what is in it, about a 1.5 mm film (a litre is 46 cm
  round, a drained sump most of a metre). On the move the drops gather into a spot about every 2.5 m, so
  a leaking car leaves a trail. Oil puddles stay, coolant dries over most of a game day, fuel in a minute
  or two. Up to 200 puddles; the smallest go first, so a parked car's puddle outlasts a trail's spots.
- **Soaked parts**: oil runs down the engine's lower block from the sump up, darker and shinier and higher
  as more goes past (fully soaked after 1.5 L); a leaking radiator is wet and dries off over a couple of
  minutes after it stops (M_Prototype_Engine's Wet, WetHeight and WetTint, set per car).
- **Trying it**: `RallyLeak <oil|coolant|fuel|all> [health]` in the console cracks the sump, holes the
  radiator or the tank of the car you sit in or stand by (default 0.2; 1 mends it).
- **Test**: `scripts\Test-Leak.ps1` breaks all three on a parked, idling car: after 25 s the puddles hold
  0.69 of the 0.70 L of oil and 0.89 of the 0.90 L of coolant lost, fuel drying; a short drive then
  leaves a trail of spots (1.24 L of oil and 1.58 L of coolant in the puddles, all of it); the block is
  soaked 0.8. Screenshots: `RallyLeak_parked.png` (the puddles under the front), `RallyLeak_block.png`
  (under the car from in front, a drop falling), `RallyLeak_bay.png` (the oily lower engine) and
  `RallyLeak_trail.png`. Test-Underside also checks that what its cracked sump lost lies on the ground.
- Not yet: puddles in the rain, oil on the road making it slippery, drops off a turbo or a loose oil
  filler cap, cleaning an oily engine.

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
the strut (counts as broken) and bolt on the spare from the boot (new), hammer the half-bent arm to the
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
