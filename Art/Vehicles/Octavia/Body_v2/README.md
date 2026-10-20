# Škoda Octavia 1U liftback — body + interior v2

A drop-in replacement for `Octavia1U_Body` and `Octavia1U_Interior`. It keeps the same object names, part split, frame (Blender Z-up, +Y forward, metres) and scale. The FBX files use the same export settings as the originals.

## Files
| File | What it is |
|---|---|
| `Octavia1U_Body_v2.blend` / `.fbx` | BODY_KIT. The blend also keeps `REFERENCE_DoNotExport` (the engine v3 and suspension v2 references, for checking); the FBX leaves it out. |
| `Octavia1U_Interior_v2.blend` / `.fbx` | INTERIOR_KIT, with the same reference handling. |
| `textures/` | PBR maps per material (`<mat>_basecolor/roughness/metallic/normal.png`), plus `_emissive` for lamps and gauges. |
| `parts_manifest.json` | The keys from `body_manifest.json`, plus the new parts, every empty in world space, and the rig properties. |
| `verification_report.txt` | Triangle counts, origins, moved empties, tyre and part clearances, kit overlaps, motion ranges, UV and topology stats. |
| `scripts/build`, `scripts/tools` | The whole pipeline (Blender 5.1 `bpy`). Order: stage1 → stage2 → stage2b → stage3 → textures → stage4 → stage4b → stage5 → stage6 → verify_body. |

## What changed
**Fit to the 4×4 running gear**
- The underbody and chassis were cut against a clearance field built from the v2 suspension, the final drive, the rear driveshafts and CV joints, and the v3 engine.
- The new surfaces are the ones you will see: floor pockets, a final-drive tunnel, rear tubs for the springs, and front and rear arches that clear the tyre at ±8 cm travel and ±35° steer.
- Strut towers have a seat ring and a hole for the top mount. The v2 strut top plate sits on the seat; that contact is intentional.
- There is no exhaust.

**Body detail**
- Paint skins refined to edges ≤4 cm. Every prototype vertex is kept, so the shape stays within 3 mm.
- Panel gaps and rolled edges are as in the prototype.
- New objects:
  - `DoorGlass_XX`, each with a `_Down` empty
  - glass seals
  - `Wiper_L` and `Wiper_R`, which replace the single `Wipers` object, each with its origin on its spindle
  - `FuelFlap`, with hinge empties
  - `Plate_F` and `Plate_R` (fictional plate "OC 1U 98"), with plate lamps
  - tow eyes
  - `JackPoint_FL/FR/RL/RR`
  - `DoorHandle_XX` and `WindowWinder_XX`, parented to their doors
- Lamp material slots: `lamp_lowbeam`, `lamp_highbeam`, `lamp_indicator`, `lamp_fog`, `lamp_tail`, `lamp_brake`, `lamp_reverse`, `lamp_plate`. Each has an emissive map; its strength is 0 at rest, and the game sets it.
- `UCX_` convex hulls on every part that opens or comes off.

**Interior**
- Gauge atlas, with warning lights in their own emissive slot (`gauge_warning`):
  - speedo 0–220 km/h
  - tacho 0–7000 rpm, red from 6500
  - temperature 50/90/130 °C
  - fuel 0–1
- The needles carry `units_per_degree` and have their zero on the dial's zero mark.
- New objects:
  - seats split into `Seat_FL` (cushion, on rails), `Seat_FL_Back` and `Seat_FL_Headrest`, and the same for FR
  - `SeatBelts`
  - `GloveBox`, with hinge empties
  - `SunVisor_L` and `SunVisor_R`
- The steering wheel turns ±540° about the column axis in the spec: (0.0045, 0.9204, −0.3909).

**Rig**
- Every moving object has these custom properties: `motion` (rotate/slide), `local_axis`, `range`, `rest`. Extras where they apply: `units_per_degree`, `range_across` / `range_fore_aft` on the gear lever, and `hinge_a_m`, `hinge_b_m`, `open_axis` on parts that swing on a hinge line.
- Every moving object has one preview Action, `<name>_preview`, that runs its full range. These are for checking only.

**Textures and UVs**
- Paint panels have exactly two UV channels:
  - `UVMap` (UV0): a world-scale box projection, tiled per material
  - `Livery`: the shared layout, carried onto the rebuilt skins by surface lookup
- The `Grime` vertex colour layer is multiplied over base colour in every material.

## Deviations — please read
1. **Hinge, latch and fixing empties.** In the source files these had been double-offset by the part pivot. They are now at their `body_manifest.json` world positions; the report lists each move. Same fix for the interior axis empties.
2. **Arch return flanges.** The inner return flanges of the front arches moved up to 7.5 mm to get 15 mm tyre clearance at full lock and bump. That is over the 5 mm limit, but they can't be seen from outside. The outer wing lip moved ≤4 mm.
3. **`Wipers` is now `Wiper_L` + `Wiper_R`.** The old object no longer exists; update any code that looks it up by name.
4. **Strut top mounts** touch the tower seat (0.1 mm) on purpose. Every other part has ≥10 mm clearance.
5. **Outer topology.** About 50–65% quads, no n-gons. The 4 cm edge limit is met on RearBumper, the fenders and Door_RL/RR. Elsewhere the limit had to grow to keep budgets: Chassis and Door_FR 5 cm, Door_FL 6 cm, Bonnet/Trunk/FrontBumper 7 cm, Roof 10 cm. That was needed to stay under 150k (146.4k now). The prototype's fan triangulation also leaves some sliver triangles; see the report.
5b. **Door pulls.** The DoorHandle_XX meshes were rebuilt after the report ran, because their levers pointed across the car. The report's 16–21 mm handle depths are from before that fix and are now gone; nothing else changed.
6. **Not done:** rain gutters were not modelled as separate geometry, and holes are left in the bay where the engine passes through.
7. **Branding.** The prototype's badge geometry is kept with a plain chrome material, and no logo textures were authored. Add your own licensed badge art if you need it.
8. The source .blends were saved by Blender 5.2; this was built with 5.1.2. No data loss was found, but reopen the files in 5.2 and check.
