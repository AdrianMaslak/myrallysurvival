# Škoda Octavia I (1U) 1.8T suspension and brakes: v2 fixes

Start from v1: `Octavia1U_Suspension.blend`, `build_suspension.py`, `partlib.py`. Keep everything that is not listed below exactly as it is:
- part names and suffixes (`_FL _FR _RL _RR`), collections, and the frame: metres, Blender Z-up, +X car's right, +Y car's front, origin on the ground under the middle of the car;
- pivots, `_MovingEnd`, `_BottomPoint` and `_BoltPoint_NN` empties (bolt point local +Z is the back-out direction);
- hard points, textures, materials, the `Grime` vertex colours, `UCX_` hulls;
- `removal_stage`, the manifest and CSV columns, and both reports.

## 1. Rear driveshafts (the car is 4x4 with independent rear suspension)

Add the following for each side, built the same way as the front:
- `Driveshaft_RL` / `_RR`
- `CVJoint_Inner_RL` / `_RR`
- `CVBoot_Inner_RL` / `_RR`
- `CVJoint_Outer_RL` / `_RR`
- `CVBoot_Outer_RL` / `_RR`

Hard points:
- `R_DS_IN_L (-0.201, -1.275, 0.295)` and `R_DS_IN_R (0.201, -1.275, 0.295)`: the rear final drive's output flanges. Origin of the inner joint and the driveshaft.
- `R_DS_OUT_L (-0.640, -1.307, 0.272)`, mirrored for the right: the outer CV centre, on the rear wheel's spin axis, inside the hub carrier's bearing bore. This is `_MovingEnd`.

Fasteners:
- inner CV: 6 bolts, real tool triple-square M8, tool `10 mm socket`;
- outer: 1 axle bolt in the hub, real tool 12-point M16, tool `17 mm socket`.

Removal stage 3, the same as the front driveshafts.

Clash to fix: the rear damper's lower mount `R_DAMP_BOT (-0.571, -1.317, 0.160)` sits below the wheel centre, so the damper crosses the new shaft line at about Z 0.28. Move the lower mount rearward (-Y) until the damper clears the shaft by at least 15 mm at ±8 cm travel. Keep the top mount, and report the shift in the hard-point table.

## 2. Rear final drive

Add `RearFinalDrive` (collection Shared): the Haldex coupling and the rear differential as one cast housing, with the two output flanges at `R_DS_IN_L/R` and the propshaft flange facing forward (+Y).
- It hangs from the rear subframe on 3 bolts (`19 mm socket`).
- It has a drain plug and a filler plug (`17 mm socket`).
- Removal stage 8, under 3k triangles, with its own `UCX_` hull or hulls.

## 3. Rear subframe

`RearSubframe` is a flat plank now. Replace it with a pressed-steel cross-member:
- a box or U section, about 70 mm deep, with the ends bent up towards the body mounts;
- 4 body mounts, each with a rubber bush in a round housing;
- welded brackets and seams, `painted_black` with the existing grime.

Keep it as it is in these respects:
- the link clevises and anti-roll-bar hangers, where they are now;
- its 4 bolts and the hard points;
- under 3k triangles.

## 4. Rims

Keep the size: steel 6Jx15 ET38, 5x100, centre bore 57.1 mm. Model it properly:
- a dished centre with 5 stud holes and their conical seats;
- 6 to 8 oval vent holes in the dish;
- the rolled outer lip, bead seats and the valve stem.

No hubcap: the wheel bolts must stay visible and reachable.

## 5. Tyres

195/65 R15 road tyre:
- the main circumferential grooves in the mesh;
- tread blocks, sipes and shoulder blocks in the normal map;
- a slight sidewall bulge, and size lettering on the sidewall in the normal map.

Rim and tyre together: at most 5k triangles per wheel.

## 6. Tools

There is no 16 mm socket in the game. Allowed tools:
- `10 mm socket`, `13 mm socket`, `17 mm socket`, `19 mm socket`
- `flat screwdriver`, `Phillips screwdriver`
- `hands`

Every fastener that is 16 mm now (anti-roll-bar links, rear caliper carrier, trailing-arm bracket, steering rack clamps) gets `tool` = `17 mm socket`; `real_tool` keeps `16 mm`. Every other mapping stays as in v1.

## 7. Verify and report

Re-run `verify_suspension.py`, adding these checks:
- clearance between each rear driveshaft (shaft, joints, boots) and the damper, spring, lower link, upper link, trailing arm, hub carrier, subframe and anti-roll bar, at ±8 cm travel, reporting the worst gap;
- clearance between the final drive and the subframe and links;
- reachability of the new fasteners (12 cm along the back-out axis, ignoring parts with a lower removal stage).

Allowed overlaps: the inner CV in its final-drive flange, the outer CV in the hub carrier bore, and the boots on their joints.

Update `README.md`, `parts_manifest.json`, `fasteners.csv`, `verification_report.txt` and `overlap_report.txt`. Export `Octavia1U_Suspension.fbx` again with the same settings.
