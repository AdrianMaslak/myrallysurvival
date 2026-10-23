# Škoda Octavia I (1U) liftback: body and interior, to detail, texture and rig

This is the RallySurvival car's shell and cabin as the game uses them today, split into the parts the game moves, opens or knocks off. The geometry comes from a low-detail prototype, its materials are placeholders and it has no textures. The job: make it look like a real 1996-2000 Octavia liftback, inside and out, with every moving piece ready to animate, and keep it a drop-in replacement for the game.

Same conventions as the 1.8T engine-bay set (v3) and the suspension set (v2): real scale in metres, Blender Z-up, +X car's right, +Y car's front, origin on the ground under the middle of the car.

## Files

- `Octavia1U_Body.blend` / `.fbx`: collection `BODY_KIT`, 20 objects, 98.8k triangles.
- `Octavia1U_Interior.blend` / `.fbx`: collection `INTERIOR_KIT`, 22 objects, 38.8k triangles.
- `REFERENCE_DoNotExport` in both files: the game's engine bay (the 1.8T set as placed in the car, scaled 88%) and your v2 suspension, as wireframes. Nothing in the body may cut into them. Do not change or export them.
- `body_manifest.json`: per part, its pivot, size, triangles, material slots, the source objects it was made from, hinge/latch/fixing points, opening axis and angles, and mass.
- `preview_*.png`: what it looks like now, one colour per part.

## The parts

Each object's origin is where the game pivots or hangs that part. **Keep every origin where it is.**

Body (`BODY_KIT`):
- **`Chassis`:** the shell, inner panels, underbody and plastics that never come off.
- **`Door_FL` `Door_FR` `Door_RL` `Door_RR`:** each with its glass, door card, trim, and on the fronts the mirror.
- **`Bonnet`**, and **`Trunk`:** the tailgate, with its glass, spoiler, badges and the parcel shelf that lifts with it.
- **`FrontBumper`:** cover, grille and badge.
- **`RearBumper`**, **`BumperBar_F`** and **`BumperBar_R`:** the steel bars behind the bumper covers.
- **`Fender_FL` `Fender_FR`**: the bolt-on front wings.
- **`Headlight_L` `Headlight_R`:** units with indicators and fogs.
- **`Taillight_L` `Taillight_R`**.
- **`Roof`:** with the aerial. **`Windshield`**. **`Wipers`**.

Empties under each part:
- **`<Part>_HingeA`, `<Part>_HingeB`:** the hinge line for doors, bonnet and tailgate.
- **`<Part>_Latch`:** the latch.
- **`<Part>_FixingA`, `<Part>_FixingB`, `<Part>_FixingMid`:** for parts that bolt or clip on (bumpers, wings, lamps). These are the bolt groups the player undoes.

Door, bonnet and tailgate opening in the game (from the manifest, kit frame, right-hand rule about `open_axis`):

| Part | Opening axis | Max open | Held open |
|---|---|---|---|
| Door_FL, Door_RL | (0, 0, -1) | 70° | 65° |
| Door_FR, Door_RR | (0, 0, 1) | 70° | 65° |
| Bonnet | (1, 0, 0) | 115° | 55° (prop) |
| Trunk (tailgate) | (-1, 0, 0) | 75° | 70° (struts) |

Interior (`INTERIOR_KIT`):
- **`Dashboard`:** dash, gauges, radio, heater panel, lower trim.
- **`Needle_Speedo`, `Needle_Tacho`, `Needle_Temp`, `Needle_Fuel`**.
- **`SteeringWheel`:** origin on the column axis at the wheel's centre.
- **`Stalk_Wiper`, `Stalk_Signal`**.
- **`Pedal_Clutch`, `Pedal_Brake`, `Pedal_Gas`**, and the **`Footrest`**.
- **`GearLever`** (knob and lever) and **`GearBoot`**.
- **`Handbrake`**, **`Armrest`**.
- **`Seat_FL`, `Seat_FR`, `Seat_Rear`**.
- **`Floor`:** carpet. **`Headliner`:** with the interior light. **`Mirror_Interior`**.

The interior's moving parts have an `<Part>_Axis` empty, 5 cm along their axis from the pivot. Except the steering wheel, these pivots are estimates from the old meshes; setting them exactly is part of the job (see Rig).

## Keep (the game depends on these)

1. Frame, scale, part names and the part split above. Each part's origin, within 5 mm.
   - The hinge and fixing empties may move up to 1 cm, onto the real hinges and bolts. Report any move.
2. The outer surface and silhouette, within 5 mm. Same proportions, panel lines, glass areas, wheel openings, ride height and door apertures.
3. Clearances against `REFERENCE_DoNotExport`:
   - **Tyres:** at least 15 mm to every body part, with ±8 cm wheel travel and the fronts at ±35° steer.
   - **Engine bay:** at least 10 mm to the engine set, including the radiator, fans, intercooler and pipes, and the coolant tank by the right strut. The strut towers must meet the v2 strut tops.
   - **Underbody:** the rear final drive and rear driveshafts (v2) need their tunnel and floor clearance (10 mm).
4. Cabin points:
   - The steering wheel centre (manifest `cabin_points_m`) and the column axis: from the wheel into the dash, (0.0045, 0.9204, -0.3909).
   - The driver's eye is near (-0.386, -0.05, 1.12). Keep the view out over the dash and bonnet, and the gauges in sight through the wheel.
5. Two UV channels on every exterior paint panel.
   - **UV0** is for your textures.
   - **UV1**, named `Livery`, is a shared layout across all paint panels, so one livery texture (stripes, numbers, sponsor decals) runs across the panel gaps. Keep the existing `Livery` channel, or remake it the same way across every panel.

## What clashes now (measured against the v2 suspension, Unreal cm, ground at 0)

The prototype's floor and arches were made for a front-drive Octavia with a torsion-beam rear, so the 4x4 suspension goes through them. Fixing this is part of the job:
- **Rear floor:** rebuild it as the 4x4 floor.
  - The rear subframe's four body-mount housings and box ends reach 11-13 cm above the floor's underside (which is at Z 33-37 over |Y| < 40). Give them raised pockets in the floor.
  - The trailing-arm front brackets and the upper links pass about 5 cm through the floor sheet beside the wheel tubs, at X -124 to -80, Y ±32 to ±53. Give them pockets too.
  - The rear final drive needs a tunnel: its top is at Z 38.5 against the floor at 36.8.
  - The rear springs sit about 3 cm into the arch tub. Shape the tubs to take them.
- **Front arches:** at full lock (35°) the tyres' inner shoulders hit the undertray at (107, ±50, 28), from 34° on. Steered 25° at +8 cm travel they are about 2 cm into the arch. Open the arch liners and the undertray so the tyres clear by 15 mm.
- **Rear arches:** at +6 cm travel the tyres touch the inner wing at (-133, ±66, 65). Give them the same 15 mm.
- **Struts:** the front struts pass through the strut towers by 15-100 mm along their length. The towers must take the v2 strut tops (10 mm inboard of the old ones) with a hole for the strut.
- **Exhaust:** do not route one. It is a separate part of the car and is being redone for the 4x4 floor.

## Detail

Body:
- **Panels:**
  - Real gaps of 3-4 mm, sheet edges rolled with thickness (about 1 mm), shut lines and inner door frames.
  - Door handles and locks, the fuel flap as its own object `FuelFlap` (hinge empties like the doors).
  - Rubber seals round the glass and doors, rain gutters, number-plate recesses with plates as their own objects.
  - Tow eyes front and rear.
- **Underbody:**
  - Floor pan with ribs, sills, the transmission tunnel, the spare-wheel well, plastic front wheel-arch liners.
  - Four jacking points on the sills as empties `JackPoint_FL` / `_FR` / `_RL` / `_RR`; the game's jack lifts there.
- **Engine bay:**
  - Inner wings, strut towers meeting the v2 strut tops, the bulkhead with the brake-servo opening, scuttle panel.
  - The radiator support holding the radiator where the engine set has it.
- **Lamps:**
  - Reflectors, bulbs and lens patterns. Every light gets its own material slot so the game can light it on its own:
    `lamp_lowbeam`, `lamp_highbeam`, `lamp_indicator`, `lamp_fog`, `lamp_tail`, `lamp_brake`, `lamp_reverse`, `lamp_plate`.
- **Glass:**
  - Windscreen, a separate pane per door, tailgate glass, quarter lights, all with seals.
  - Each door pane is its own object, `DoorGlass_FL` and so on, closed. Add a `<Pane>_Down` empty where its top edge sits fully wound down.
- **Wipers:**
  - Split into `Wiper_L` and `Wiper_R` (arm and blade), each with its origin on its spindle and a rest and a full-sweep angle.

Interior:
- **Dashboard:**
  - The gauge cluster: speedo 0-220 km/h, tacho 0-7000 rpm with the red line at 6500, temperature and fuel. Readable dial faces as texture; warning lights as their own emissive slot.
  - Switches and vents, glovebox lid as its own object `GloveBox` with hinge empties, the radio, the heater controls.
- **Steering:** wheel with horn pad, column shroud, both stalks.
- **Pedals and levers:** the pedal box, gear lever with its gaiter, handbrake with its gaiter.
- **Seats:**
  - Cushion, backrest and headrest as separate objects (`Seat_FL_Back` etc., with the recline pivot), on rails.
  - Seat belts and buckles.
- **Doors (in the `Door_*` parts):** the cards with handles and window winders as their own objects with pivots.
- **Trim:** headliner with sun visors (`SunVisor_L/R`, hinge pivots), interior mirror, carpet.
- **Rear:** the rear bench, and the boot floor with its spare-wheel well.

## Rig (animation-ready)

- **Objects:** every moving piece is its own object. Its origin is on its pivot, and its local axes are set so the motion is a turn about one local axis, or a slide along one.
- **Custom properties on each moving object:**
  - `motion`: `rotate` or `slide`;
  - `local_axis`: `X`, `Y` or `Z`;
  - `range`: two numbers, degrees or metres, from rest to full;
  - `rest`: which end is the rest state.
- **Ranges:**
  - Needles add `units_per_degree`. Example: the speedo `range` 0 to 240° for 0-220 km/h.
  - The gear lever has two: `range_across`, `range_fore_aft`.
- **Preview actions:** add one Action per moving object that runs its full range, to check it. The game drives the parts itself; the actions are only for checking.
- **Parts that move:**
  - **Hinged:** the four doors, bonnet, tailgate, fuel flap, glovebox, door handles, sun visors, interior mirror.
  - **Sliding:** the four door panes.
  - **Turning:** both wipers, steering wheel, both stalks, the three pedals, gear lever (two axes), handbrake, the four needles.
  - **Seats:** slide and recline.

## Textures

- **Maps:** PBR at 2048 px for paint, glass, interior plastics and fabric; 1024 px for small parts. `_basecolor` (sRGB), `_normal` (OpenGL, +Y up), `_roughness`, `_metallic`, as in the engine set. Add `_emissive` for lamps and gauges.
- **Materials:**
  - Body paint (a plain colour; the livery comes on top in the game), black textured plastic, chrome, rubber, glass (clear, and tinted for the rear), lamp lenses (clear, amber, red).
  - Underbody and wheel arches: underseal and road dirt.
  - Interior: dark grey textured plastic, seat fabric, carpet, headliner cloth, gauge faces.
- **`Grime`:** a vertex-colour layer like the engine set's, white for clean and darker for dirt. Use it on the lower body, sills, arches, underbody, door shuts and the cabin floor.

## Topology

- **Dents:** the game dents the exterior panels by moving their vertices in crashes. Outer skins need even quad density, edges no longer than 4 cm on the outer panels, no long thin triangles and no n-gons. Inner skins can be lighter.
- **Collision:** a `UCX_<Part>_NN` convex hull (or several) for every part that opens or comes off: doors, bonnet, tailgate, bumpers, wings, lamps, mirrors, fuel flap. The game aims at those parts and carries them by these hulls.

## Budgets

- **Exterior:** at most 150k triangles.
  - `Chassis` 50k, each door 15k (with its card), bonnet 5k, tailgate 12k.
  - Each bumper 10k, each wing 4k, each headlight 6k, each tail lamp 4k.
- **Interior:** at most 90k.
  - Dashboard with gauges 30k, steering wheel 8k, each front seat 8k, rear bench 8k.

## Deliver

1. `Octavia1U_Body_v2.blend` and `.fbx`, and `Octavia1U_Interior_v2.blend` and `.fbx`. Keep the `REFERENCE_DoNotExport` collection, and leave it out of the FBX.
2. `textures/` with every map.
3. `parts_manifest.json`, using the keys of `body_manifest.json` plus the new parts and their rig properties.
4. `verification_report.txt`:
   - triangle counts per part;
   - origins and any hinge or fixing empty you moved, with distances;
   - tyre-to-body clearance at ±8 cm and ±35°;
   - body-to-engine and body-to-suspension clearances, with worst gaps in mm;
   - overlaps between the kit's own parts deeper than 2 mm;
   - every moving part's range;
   - UV checks, including `Livery` on every paint panel.
5. The build scripts you used, so we can rebuild after changes.
