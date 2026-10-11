# Crash damage

Code: Source/RallySurvival/Public/Damage/ (RallyBodyDeformComponent.h, RallyDamageComponent.h,
ImpactRules.h, PartConditionRules.h) and the impact handling in Vehicle/RallyCarPawn.cpp.
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
- A dent pushes vertices in under the patch and fades out over Radius beyond it, with noise that varies
  the push and buckles metal sideways, so the bonnet and wings ripple instead of sliding.
- Shading: each vertex keeps its own undamaged normal turned by how far the surrounding faces turned,
  gathered across UV and normal seams, so hard edges stay hard and seams do not show.
- Vertex colour R marks scraped paint (1 intact, 0 bare). M_Prototype_Paint blends to dark bare steel;
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
- Suspension: direct hits, or hard hits crushing back into the wheel well (reach fades over 1.5 x dent
  depth). Lost health bends the wheel outward, up to 5 degrees toe-out at 0, so a damaged corner pulls.
- Radiator: hits pushing backwards near the grille. Tracked only; overheating is not modelled yet.
- Handling is rebuilt from the undamaged setup plus these symptoms whenever condition changes, on every
  machine, so whoever simulates the car drives the damaged car. The readout lists damaged parts.

Debug keys while driving (not in shipping builds): F8 flattens the front-left tyre, F9 repairs every
part and straightens the body.

## Repairs

URallyDamageComponent::CompleteValidatedRepair restores 1 (proper fix) or 0.6 (bodge, flagged).
Repeated bodges cannot replenish a part; bodges never worsen a healthier part. Server-only, no client
RPCs: the interaction layer must validate tools, inventory, distance and completion first.
Not yet built: wheel change (jack, spare, wheel nuts), panel repair, inventory, persistence.

## Tests

- Rules: scripts\Test-Rules.cmd (damage, repair and crash rules, no Unreal needed).
- scripts/Test-Crash.ps1: runs to the car, drives into the lab's impact barrier from 30 m, backs off,
  logs impact and part state and saves RallyCrash_*.png (cockpit, front-left, front-right, front).
- scripts/Test-TwoPlayer.ps1 -LagMs 50 -ExtraArgs '-RallyCrashTest': the client drives into the barrier
  with the host riding along.

Results 2026-10-11:
- Single player: barrier hit at 47.6 km/h, 17.1 m/s change of velocity (the car bounced back at
  11 km/h); whole front face patch; front crumpled, bonnet buckled and scraped; radiator 4%, both front
  suspension corners 70% (1.5 degrees toe-out each); tyres intact. Reversing straight afterwards drifted
  0.6 degrees. Building the dent took 45 ms (one hitch at the moment of impact).
- Two players, about 100 ms round trip: the client's impact reached the server, which applied it; the
  dent and part state replicated back, both sides logged the same state.
- Scripted drive (Test-Drive.ps1, 196 m including off-road corner and reverse): no damage recorded.

## Limits and next steps

- The 45-70 ms dent build hitches one frame per heavy impact; a long scrape along a wall could stutter.
  Parallelising the vertex loop is the first fix if it shows in play.
- Contact patches use the body box, so dents start at the box faces (bumpers, sides, roof), not at
  exact panel surfaces; rounded corners are approximate.
- No detached parts (bumper, bonnet, doors stay on), no sparks, smoke or glass particles, no sound.
- Wheels and the steering wheel do not deform; toe is visible on the wheels.
- Interior and windscreen deform with the body when inside a dent's reach.
