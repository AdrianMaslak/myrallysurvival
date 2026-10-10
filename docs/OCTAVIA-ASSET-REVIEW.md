# Octavia asset inspection

File: skoda_octavia_mk1_1u_1996-2010.glb
Inspection: GLB metadata plus Blender 5.2.2 LTS import and two solid-shaded render views. Unreal import remains pending.

## Verified from the file

- Valid GLB v2 header with matching total file length: 30,234,672 bytes.
- 617 nodes, 357 meshes, 12 materials.
- 419,922 triangles across all meshes/variants, plus 34 line primitives.
- 491,748 vertex entries across all mesh primitives (not welded unique vertices).
- No embedded images or texture entries; materials use simple values.
- No skins or animation clips.
- Named groups include doors, bonnet, tailgate, seats, dashboards, roll cage,
  engine components, radiator and radiator fans.
- Multiple alternatives are included: wagon parts, WRC parts, vRS badges/gauges,
  different dashboards, steering wheels and engine covers.
- The user reports missing road wheels; inspected group names contain steering
  wheels but no clearly named tyre/rim assemblies. Confirm visually.

## Source metadata

Title: Skoda Octavia MK1 (1U) 1996-2010
Author: szymonpasterczyk734
Author URL: https://sketchfab.com/szymonpasterczyk734
Declared license: CC-BY-4.0
License URL: http://creativecommons.org/licenses/by/4.0/
Source: https://sketchfab.com/3d-models/skoda-octavia-mk1-1u-1996-2010-6d100a9f85934a39a80bffa5b5c48430

These are embedded uploader declarations, not an independent rights verification.
The source page could not be fetched (HTTP 403). Names such as midsize_mechanical
and vivace_mechanical occur in the file; authorship of those components is not
established by this inspection. Preserve attribution and verify provenance before
including this asset in a distributed game.

## Preparation plan

1. Preserve the original GLB and import a working copy into Blender.
2. Visually identify one liftback configuration; exclude overlapping alternatives.
3. Verify dimensions, orientation, wheel centres and interior visibility.
   Per-mesh bounds alone are insufficient: the hierarchy includes transforms.
4. Exclude line-only helper geometry from game exports.
5. Keep doors, bonnet, tailgate, radiator and required repair parts separate.
6. Combine static detail selectively and prepare simple materials and collision.
7. Add four placeholder wheels with correct pivots.
8. Export the working configuration, then test it in the Unreal vehicle setup.

Status: promising source geometry; not yet a game-ready vehicle.
## Blender inspection result

Steam Blender path: C:/Program Files (x86)/Steam/steamapps/common/Blender/blender.exe
Editable inspection copy: Art/Vehicles/Octavia/Inspection/Octavia_Source_Inspection.blend
Views: front-quarter.png and rear-quarter.png in the same directory.
Full import contains 323 mesh objects with faces, plus line/helper geometry.
Combined transformed bounds are approximately 2.012 m wide, 4.785 m long and
2.243 m high. These include alternative/floating parts and are NOT stock-car dimensions.
The two rendered views confirm overlapping estate/liftback/rally geometry, a floating
panel above the roof, absent road wheels, and visible brake-disc geometry.
Glass is shown in solid inspection shading; this does not validate final transparency.
Detailed cabin visibility and pivots remain to be inspected after variant selection.
Original GLB unchanged. Next: isolate a single liftback configuration in a working copy.