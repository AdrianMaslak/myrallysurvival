# Škoda Octavia I (1U) 1.8T 20V — engine bay parts

Real-world scale (metres). 61 separate parts, 185 modelled fasteners, about 108k triangles in total; the largest part (turbocharger) is 7.4k triangles.

## Changes in v3
- Textures: stains are smaller and fainter, so base materials read clean; dirt now sits mainly in the `Grime` vertex layer, which is stronger around joints and bolts and now also darkens inside corners.
- UVs: pipes, hoses, cylinders and rings are unwrapped along their length with whole texture tiles around the circumference, so there are no colour jumps on the downpipe or other round parts. Boxes keep a box projection with lower-contrast textures so face seams don't show.
- Power-steering pump body shortened so the intake manifold's upper right bolt is reachable from the front.
- Clamp screws: cold intercooler pipe (throttle end) now points down and forward, away from the upper radiator hose; centre silencer and cat clamps point down/outward, away from the fuel tank and floor.

## Changes in v2
- Textures: relief halved and coarser; variation moved into colour and roughness (rust, exhaust heat colours, oil, grime, faded plastic). Joint grime is a vertex colour attribute `Grime` (multiply it over base colour; white = clean).
- Overlaps: turbo, manifold, alternator, bracket, A/C, starter, gearbox, intake hose, pipes and hoses now meet at flanges. `overlap_report.txt` lists what still touches; the only remaining penetrations are coil packs into the head and onto plugs, the dipstick in its tube, a 3 mm exhaust slip joint and one vertex at the downpipe flange.
- Hose clamps: screws face up or outward; clamp bolt points sit on the screw head, axis along the screw.
- Tools: every fastener has a `tool` (CSV column, manifest field, custom property on the fastener object). Parts removed without a fastener (coils, plugs, caps, dipstick, filter, belt) carry a part-level `tool`.
- Collision: 220 convex hulls named `UCX_<Part>_NN` (Unreal convention), parented to 17 bent parts. Hidden in renders; delete or ignore if you generate your own.
- Pivots: front pipe, intercooler pipes, intake hose, radiator hoses, cat and silencers pivot on their mounting joint; alternator and starter on their upper bolt. Other parts keep the bounding-box centre.
- Bevels: larger rounded edges on block, head, covers, battery, ECU, air box, sump.

## Files
- `Octavia1U_1.8T_20V.blend` — Blender 5.1 source, parts sorted into collections: Core, Unbolt, Wanted, NiceLater, Extras.
- `Octavia1U_1.8T_20V.fbx` — Y-up, -Z forward, unit scale applied, custom properties included. Textures load from `textures/`.
- `textures/` — 1024 px PBR maps per material: `_basecolor` (sRGB), `_normal` (OpenGL, +Y up), `_roughness`, `_metallic`.
- `parts_manifest.json` — every part with description, size, origin, fastener list (position + back-out axis) and source notes.
- `fasteners.csv` — fastener table with a tool column.
- `overlap_report.txt` — part pairs that touch, with penetration depth.
- `build_engine.py`, `make_textures.py` — the scripts that generate everything (run with the `bpy` module) if you want to change dimensions.

## Naming
- Part: `IntakeManifold`
- Fastener mesh: `IntakeManifold_Bolt_03`, `ExhaustManifold_Nut_11`, `IntercoolerPipe_Hot_Clamp_01` … parented to the part.
- Bolt point: `IntakeManifold_BoltPoint_03` (Empty). Its local +Z (Blender) points the way the fastener backs out.
- Extra points: `Radiator_SteamPoint`, `Radiator_LeakPoint`, `FuelTank_LeakPoint`, `ExhaustFrontPipe_DragPoint`, `Battery_PositivePost` …

## Frame
Origin on the crankshaft axis, middle of the block. +X timing-belt end (car's right), +Y front of car, +Z up (Blender).

## Fastener sources
- Intake manifold 8 bolts + 2 nuts + 1 Allen to bracket, turbo 3 bolts to manifold, downpipe 4 nuts, turbo support 1, oil return 2, engine cover 4 quarter-turn screws, valve cover 9 nuts: Pelican Parts Golf IV 1.8T articles (same engine family).
- Exhaust manifold 13 nuts (8 top, 5 bottom): Pelican Parts Audi A4 1.8T (same 20V head).
- Starter 2 bolts (+ up to 2 extra 'if fitted'): VW workshop manual.
- Tools: intake manifold 10 mm (Pelican), turbo-to-manifold 17 mm (Pelican), valve cover 10 mm (Pelican), engine cover Phillips quarter-turn (Pelican). Other spanner sizes follow DIN head sizes for the thread (M6 10, M8 13, M10 17, M12 19, M14 drain 19 mm); Torx T25 on plastic screws is a guess.
- Everything marked "approximate" in the manifest (sump bolts, accessory bracket, A/C, PAS pump, timing-cover bolts, mounts) is placed from typical VW practice, not a confirmed count.

Not included: engine internals, suspension.
