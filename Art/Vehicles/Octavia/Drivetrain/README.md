# Škoda Octavia I (1U) — modular drivetrain kit (v1)

This kit makes two cars from the same body, interior, engine and front suspension: a **front-drive Octavia** and an **Octavia 4x4**.

It follows the engine set's (v3) and suspension set's (v2) conventions:
- real scale in metres, Blender Z-up
- +X is the car's right and +Y the front
- origin on the ground under the middle of the car
- separate parts, with fasteners and bolt points (`tool` / `real_tool`)
- `UCX_` collision hulls and the `Grime` vertex colours
- the same PBR maps

It was fitted to the engine set as the game places it: scale 0.88, at (0.16, 1.507, 0.381).

## What goes in which car
`configs.json` lists every part by kit.

| | Front-drive | 4x4 |
|---|---|---|
| Body, interior, engine set, front suspension, rear wheels, brakes, springs and dampers | ✓ | ✓ |
| Gearbox `Gearbox_02J` (engine set) | ✓ | ✓, plus `AngleDrive` |
| `Propshaft_Front`, `PropshaftCentreBearing`, `Propshaft_Rear` | – | ✓ |
| Rear axle | `TwistBeam`, `PivotBracket_RL/RR`, `RearHub_RL/RR` | suspension set's multilink: hub carriers, trailing arms, links, driveshafts, rear anti-roll bar, rear subframe and `RearFinalDrive` (Haldex) |
| Exhaust `Exhaust_Cat`, `Exhaust_CentreSilencer`, `Exhaust_RearSilencer` | ✓ | ✓ (the same exhaust fits both) |
| Fuel tank | `FuelTank_FWD` (≈50 l) | `FuelTank_4x4`: saddle tank, ≈43 l, lobes either side of the propshaft |
| `FuelFillerNeck` | ✓ | ✓ (the same neck fits both tanks) |

The engine set's `CatalyticConverter`, `CentreSilencer`, `RearSilencer` and `FuelTank` are not used in either car. They were laid out for a different floor and cut into the body. The kit's exhaust and tanks replace them.

## Parts
**Drivetrain4x4**
- `AngleDrive`
  - Transfer box bolted to the back of the 02J at its final drive, with 4 bolts. Drain and filler plugs.
  - Its output flange (`AngleDrive_OutputFlange`) points rearward at (−0.010, 1.212, 0.345).
  - Leave it off for the front-drive car. The gearbox is the same part either way.
- `Propshaft_Front`
  - Rubber flex disc (3 bolts) and a 45 mm tube, over the steering rack and front anti-roll bar, into the centre bearing.
  - Origin at the front flange; local Z points along the shaft.
  - `motion=spin`; `_MovingEnd` at the centre bearing.
- `PropshaftCentreBearing`: rubber-mounted bearing in a bracket, with 2 bolts up into the tunnel at y −0.05.
- `Propshaft_Rear`
  - Centre CV joint, 50 mm tube, and a rear CV joint bolted to the final drive's flange with 6 triple-square bolts (10 mm socket).

**Exhaust** (shared). It starts at the engine set's downpipe flange.
- `Exhaust_Cat` runs up into the right half of the tunnel, beside the propshaft.
- `Exhaust_CentreSilencer`: a slim silencer in the tunnel, then the pipe leaves it and runs back along the right of the floor, under the rear axle.
- `Exhaust_RearSilencer`: a transverse box behind the axle, with the tailpipe on the left.
- Flanges are 2-bolt. Rubber hangers carry `_Hanger_NN` points; there are drag and tailpipe points too.

**FuelSystem**
- **Tanks:** the top of each tank follows the floor 12 mm below it. Two straps each.
  - The 4x4 tank's two lobes are joined by a crossover hose under the propshaft.
  - Points: `_LeakPoint` and `_FillerInlet`.
- **`FuelFillerNeck`:**
  - From the tank's right side, over the exhaust and under the floor behind the axle.
  - Through a rubber grommet in the floor behind the right rear wheelhouse, up inside the quarter panel to the filler cup behind the fuel flap.
  - Points: `_Cap` and `_FloorGrommet`.

**RearAxleFWD**
- **`TwistBeam`:** two trailing arms on a U-section cross member. It twists, so it is also the rear anti-roll bar.
  - It has spring pans, so `Spring_RL/RR` from the suspension set sit on it unchanged.
  - It has damper eyes, so `Damper_RL/RR` bolt to it unchanged.
  - Hub plates carry the hubs.
  - Rig: `motion=rotate`, `local_axis=X`, `range` ±9.8° (±8 cm at the wheel), `rest=middle`.
  - For one wheel, turn that arm about its own pivot (`TwistBeam_PivotL/R`).
  - Points: `TwistBeam_WheelL/R`, `TwistBeam_SpringSeatL/R`, `TwistBeam_DamperEyeL/R`.
- **`PivotBracket_RL/RR`:** body brackets with the pivot bolt and nut. Pivot at (±0.545, −0.842, 0.245).
- **`RearHub_RL/RR`:** stub axle and hub bearing.
  - The disc and caliper carrier sit exactly where they do on the 4x4 hub carrier, so the suspension set's rear brakes, wheels and hoses fit both cars.

## Body changes (body v2.2, in this delivery)
The body is the same part for both cars. I changed it in two places:
1. **Final-drive pocket lip.** The front wall of the final-drive pocket hung down where the propshaft's rear joint passes. It is lifted locally (`stage9_prop_lip.py`).
2. **Rear-suspension travel.** The body had only been checked against the tyres in motion, not the links. With the rear links, driveshafts, trailing arms and twist beam moved to ±8 cm, the underbody round the rear axle is pushed clear by at least 13 mm.
   - The damper-tower lip over the upper links rises up to 80 mm; everything else moves 45 mm at most.
   - Nothing moved comes within 10 mm of a part that stays still (`stage9b_link_clearance.py`).

## Files
- `Octavia1U_Drivetrain.blend` / `.fbx`: collections `Drivetrain4x4`, `Exhaust`, `FuelSystem`, `RearAxleFWD`.
- `textures/`: the engine and suspension sets' maps for the materials used here.
- `parts_manifest.json`:
  - every part: origin, size, triangles, hulls, removal stage, fasteners, points, rig properties
  - the key points
  - the engine placement
- `configs.json`: the part list per car.
- `fasteners.csv`: one row per fastener, with tool and real tool.
- `verification_report.txt`: clearance in both cars, static and under ±8 cm rear travel.
- `scripts/`: `build_drive.py`, `partlib.py`, `check_drive.py` and the scene and push helpers.
