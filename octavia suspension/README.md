# Škoda Octavia I (1U) 1.8T 4x4 — suspension and brakes, both axles (v2)

Same conventions as the 1.8T 20V engine-bay set (v3). Real scale in metres, Blender Z-up, +X car's right, +Y car's front. Origin on the ground under the middle of the car.

- 84 parts in collections Front, Rear and Shared, with suffix _FL _FR _RL _RR.
- 177 fastener meshes, each with a bolt point.
- 201 `UCX_` collision hulls.
- 83.6k triangles: 71.1k in parts, 12.5k in fasteners. Every part is under 3k, and rim plus tyre is 4,964 per wheel (limit 5k).

## Changes in v2
- **Rear driveshafts (4x4):** `Driveshaft`, `CVJoint_Inner`, `CVBoot_Inner`, `CVJoint_Outer` and `CVBoot_Outer` for RL and RR, built like the front ones.
  - They run from the final-drive flanges `R_DS_IN_L/R` to the outer CV centre `R_DS_OUT_L/R`. That point is the `_MovingEnd`, inside the hub carrier's bearing bore.
  - Fasteners: 6 triple-square M8 inner CV bolts (`10 mm socket`) and a 12-point M16 axle bolt (`17 mm socket`). Removal stage 3.
  - The inner bolt circle is turned so no bolt backs out into the upper-link clevis above the shaft.
- **Hub carrier bore:** the rear hub carrier's bore is opened to 56 mm, with a short collar round the outer CV joint so the joint sits in it. The CV stub runs out through the bearing to the axle bolt.
- **Rear damper lower mount:** `R_DAMP_BOT` moved **90 mm rearward**, to (-0.571, -1.407, 0.160). The top mount is unchanged. The damper now clears the shaft and outer boot by 18.9 mm at worst, at +8 cm travel.
  - Two knock-on changes follow from the move:
    - The rear anti-roll-bar link moves from y -1.390 to -1.460. It sat where the damper now passes.
    - The rear caliper turns from 155° to 165° round the disc, so its guide bolts back out past the damper.
  - The damper now covers the caliper-carrier bolts, so it comes off one stage earlier: removal stage 2 instead of 3.
- **`RearFinalDrive` (Shared, stage 8):** the Haldex coupling and rear differential as one cast housing.
  - The output flanges sit at `R_DS_IN_L/R`, and the propshaft flange faces forward.
  - Three mounting ears hang it from the subframe on 3 × M12 bolts (`19 mm socket`). It has a drain plug and a filler plug (`17 mm socket`).
  - 2,772 triangles, 15 hulls.
- **`RearSubframe`:** a pressed-steel cross-member, 2,964 triangles and 32 hulls.
  - The section is a 70 mm deep U with top lips. The ends bend up 30 mm into box-section side members.
  - Four round body-mount housings, each with a rubber bush and a steel sleeve.
  - Weld beads round the housings, along the joints and on the clevis tops. A gusset on each front corner and a tongue bracket for the final drive's rear mount.
  - The link clevises, anti-roll-bar hangers, 4 bolts and hard points are where they were.
- **Rims:** steel 6Jx15 ET38, 5x100, 57.1 mm bore.
  - The dished centre disc has a raised ring outboard of the bolt circle and 5 stud holes. Each stud hole has a pressed boss with a 60° conical seat.
  - 6 oval vent holes, cut with an exact boolean. Shading normals on the cut disc are set to the exact surface normal, so the holes leave no smoothing streaks.
  - Rolled outer lip, 5° bead seats with safety humps, a drop-centre well, and a rubber valve stem whose tip shows through the vent at 200°.
  - No hubcap.
  - The wheel bolts' cone now narrows toward the hub, so it sits in the rim's seat.
- **Tyres:** 195/65 R15, 634 mm new, 200 mm section width with a slight sidewall bulge.
  - The mesh has four 8 mm circumferential main grooves.
  - New 2048 px `tyre` maps add tread blocks with angled lateral grooves, sipes, shoulder blocks with lateral slots, a rim-protector rib, and size lettering on both sidewalls ("195/65 R15 91T", "TUBELESS RADIAL M+S", "DOT 2416").
  - The tyre has its own UV layout (see Textures).
- **Tools:** no 16 mm socket. The 20 fasteners that were 16 mm now have `tool` = `17 mm socket`, and `real_tool` keeps `16 mm`. These are the drop links, rear caliper carrier, trailing-arm bolts and rack clamps. Every other mapping is as in v1.

## Files
- `Octavia1U_Suspension.blend` — Blender 5.1 source.
- `Octavia1U_Suspension.fbx`:
  - Y-up, -Z forward, unit scale applied.
  - Includes custom properties, custom normals and the `Grime` vertex colours.
  - Textures load from `textures/`.
- `textures/` — PBR maps: `_basecolor` (sRGB), `_normal` (OpenGL, +Y up), `_roughness`, `_metallic`.
  - 1024 px, except the tyre at 2048 px.
  - Materials: tyre, rim_silver, disc_face, cast_iron, caliper_grey, pad_friction, painted_black, rubber, zinc.
  - Every map except the tyre is unchanged from v1.
  - Mean normal tilt is 2.8–3.5° on every material (tyre 3.5°).
- `parts_manifest.json` — per part:
  - name, side, group, description
  - origin_m, size_m
  - moving_end_m / bottom_point_m, local_z_axis, spin_axis, removal_stage
  - fasteners: position, back-out axis, tool, real_tool, label, source
  - the given vs used hard points
- `fasteners.csv` — one row per fastener, with tool and real_tool. Parts removed by hand (springs, pads, inner tie rods) get their own rows.
- `overlap_report.txt` — every touching pair, with penetration depth and which allowed exception applies.
- `verification_report.txt`:
  - hard points
  - triangle budget
  - tyre clearance
  - rear driveshaft clearance
  - final-drive clearance
  - overlaps
  - reachability
- `build_suspension.py`, `partlib.py`, `wheel_profile.py`, `make_textures_susp.py`, `verify_suspension.py` — run with the `bpy` module. `wheel_profile.py` holds the tyre and rim cross-sections shared by the build and the tyre texture.

## Tyre texture layout
- U runs round the tyre with exactly 2 repeats, so the lettering appears twice, at opposite sides.
- V runs 0 to 1 along the cross-section in `wheel_profile.TYRE_PROFILE`, in this order: outboard bead, outboard sidewall, tread, inboard sidewall, inboard bead, then the hidden closing edge.
- There are 33 tread pitches per repeat, so the pattern tiles.
- On the right-hand tyres U is flipped, so the lettering reads correctly on both sides of the car.

## Pivots and points
- **Wheel-side parts** (tyre, rim, knuckle, hub carrier, disc, carrier, caliper, pads, outer CV): origin at the wheel centre; spin axis along X (`spin_axis`).
- **Moving links** (lower arm, tie rod, tie-rod end, driveshafts, inner CVs and boots, drop links, trailing arm, upper and lower links, brake hoses): origin at the body-side pivot, plus an Empty `<Part>_MovingEnd` at the wheel-side joint.
  - The rear driveshafts and inner CVs have their origin at `R_DS_IN_L/R`.
  - Their `_MovingEnd` is at `R_DS_OUT_L/R`.
- **Struts and dampers** (`Strut_Upper`, `Strut_Lower`, `Damper`):
  - Origin at the top mount, local +Z along the strut axis pointing up.
  - Empty `<Part>_BottomPoint` at the lower mount.
  - The front strut is split so it telescopes: `Strut_Upper` carries the rod, `Strut_Lower` the damper body.
- **Springs**: origin at the top seat, local +Z along the spring axis. Scale local Z to compress toward the top seat. `bottom_point_m` gives the lower seat.
- **Bolt points**: Empty `<Part>_BoltPoint_NN`; local +Z is the way the fastener backs out.
- **Valve stems**: `Rim_XX_ValveStem`.

## Hard points
All are used exactly except these:

| Point | Change | Why |
|---|---|---|
| `F_TOP`, `F_CLAMP` | 10 mm inboard | The 45 mm damper tube clears the tyre sidewall |
| `F_BJ` | 9 mm inboard | The ball-joint housing clears the 288 mm disc |
| `R_DAMP_BOT` | **90 mm rearward**, as asked | The damper clears the rear driveshaft by 15 mm or more over ±8 cm travel |

The new points `R_DS_IN_L/R` and `R_DS_OUT_L/R` are used exactly.

## Clearance (from verification_report.txt)

**Tyres:**
- Front: checked at every combination of ±35° steer and ±8 cm travel.
- Rear: checked at ±8 cm travel.
- The tightest gaps:

| Gap | Clearance |
|---|---|
| Front tyre to lower arm, 35° lock and full bump | 13.6 mm |
| Front tyre to strut (static; it turns with the wheel) | 14.1 mm |
| Rear tyre to damper | 17.8 mm |
| Rear tyre to trailing arm | 36.3 mm |

**Rear driveshaft** (shaft, joints and boots), worst over -8 to +8 cm travel:

| Part | Clearance |
|---|---|
| Subframe (link clevis, at +8 cm) | 11.2 mm |
| Damper (outer boot, at +8 cm) | 18.9 mm |
| Upper link | 20.3 mm |
| Trailing arm | 20.8 mm |
| Hub carrier (outer boot) | 31.4 mm |
| Anti-roll bar | 50.7 mm |
| Lower link | 56 mm |
| Spring | 71 mm |
| Anti-roll-bar link | 117 mm |

- In this check, the shaft and boots swing about the inner joint. The outer joint moves with the hub carrier, and the inner joint stays on its flange.
- The outer joint sits in the carrier bore, which is an allowed overlap, not a clearance.

**Final drive:**

| Gap | Clearance |
|---|---|
| Lower links | 134 mm |
| Upper links | 120 mm |
| Housing to the subframe cross-member | 6.9 mm |
| Mounting pads to their brackets | 1 mm |

## Overlaps
- 92 pairs touch, and 22 penetrate more than 2 mm.
- Every penetration is in the allowed list: ball-joint studs, bushings in brackets, CV joints in hubs, springs on seats.
- The v2 additions to the allowed list:
  - the rear inner CVs in their final-drive flanges
  - the rear outer CVs in the hub carrier bore
  - boots on their joints
- Rims and tyres do not penetrate each other or the brakes.

## Fasteners and tools
`tool` is always one of:
- 10, 13, 17 or 19 mm socket
- flat screwdriver, Phillips screwdriver
- hands

Where the real fastener needs something else, `tool` holds the nearest allowed tool and `real_tool` holds the real one:

| Real tool | Nearest allowed tool |
|---|---|
| 16 mm (drop links, rear carrier, trailing arm, rack clamps) | 17 mm socket |
| Triple-square M8 (inner CVs, front and rear), hex key 7 mm (front caliper guide bolts) | 10 mm socket |
| Torx T30 (disc screws) | Phillips screwdriver |
| 18 mm and 21 mm sizes | 19 mm socket |
| 12-point M10 and M16 (rear hub bearing, axle bolts) | 17 mm socket |

Counts per corner:

| Part | Front | Rear |
|---|---|---|
| Wheel bolts | 5 | 5 |
| Caliper guide bolts | 2 | 2 |
| Carrier bolts | 2 | 2 |
| Disc screw | 1 | 1 |
| Brake hose | union + clip | union + clip |
| Strut / damper | clamp bolt + nut; 3 top-mount nuts + centre nut | 2 top nuts; bottom bolt + nut |
| Arms and links | lower arm: front bushing bolt + nut, rear bushing bolt, 3 ball-joint bolts; ball-joint pinch bolt + nut in the knuckle | trailing arm: 3 bracket bolts, pivot bolt + nut, 2 bolts to the hub carrier; lower link: bolt + nut at the body end, bolt at the carrier end; upper link: one bolt at each end |
| Steering | tie-rod end nut | — |
| Driveshaft | axle bolt; 6 inner CV bolts | axle bolt; 6 inner CV bolts |
| Hub | — | 4 hub-bearing bolts |
| Drop link nuts | 2 | 2 |

Shared parts:

| Part | Fasteners |
|---|---|
| Front subframe | 4 bolts |
| Rear subframe | 4 bolts |
| Rear final drive | 3 mount bolts, drain plug, filler plug |
| Rack | 2 clamp bolts |
| Front anti-roll bar | 2 clamps × 2 bolts |
| Rear anti-roll bar | 2 clamps × 2 bolts |

Captive nuts: the hub-carrier ends of both rear links and the body end of the upper link use captive nuts, so there is nothing separate to remove there. Real nuts in those spots would face straight into the carrier or the lower-link clevis.

Sources, as recorded per fastener in the `source` field:
- **TightTorque Golf IV (Mk4) torque table:** confirms which fasteners exist on the shared PQ34 platform and the 4motion rear:
  - wheel bolts M14x1.5, caliper guide pins, carrier bolts, disc screw
  - strut centre nut, strut-to-knuckle bolt
  - control-arm bolts, ball-joint clamp bolt, tie-rod end nut, anti-roll-bar links
  - rear links to the bearing housing, rear caliper bracket
- **Jetta Mk3 ball-joint guide:** 3 ball-joint bolts (13 mm) and the knuckle pinch bolt and nut.
- **Rear inner CV flange:** 6 bolts, the same 100 mm flange as the front.
- **Approximate:** 53 fasteners where no source gave a count or size, including the final-drive mounts and plugs. These are marked `approximate` in `source`, and more carry `(approximate)` in `real_tool`.

## Reachability
Every fastener, including the 19 new ones, has 12 cm clear along its back-out axis, apart from parts that come off first. Removal order is the `removal_stage` property, lowest first:

| Stage | Parts |
|---|---|
| 0 | wheel |
| 1 | caliper, hose |
| 2 | pads, tie-rod end, rear drop link, **rear damper** |
| 3 | carrier, front and rear driveshafts, front drop link, rear spring |
| 4 | disc, strut, rear links |
| 5 | knuckle, inner tie rod, trailing arm |
| 6 | lower arm, hub carrier |
| 7 | rack, anti-roll bars |
| 8 | subframes, rear final drive |

The brake hose is ignored when checking its own union bolt. The body is not modelled, so bolts that point up into the body (top mounts, subframe and bracket bolts) are only checked against these parts.

Not included: hub caps, ABS sensors and lines, handbrake cables, propshaft, body.
