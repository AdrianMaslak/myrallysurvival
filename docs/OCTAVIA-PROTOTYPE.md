# Wheeled Octavia prototype

Blender file: Art/Vehicles/Octavia/Prototype/Octavia_With_Wheels.blend
FBX files and placement data: Art/Vehicles/Octavia/Prototype/Export

## What is prepared

Four separate wheels with centred pivots, 31 cm radius and 20.5 cm width.
Mounts are estimates from the source hub bounds, not factory suspension measurements.
The inferred axle spacing is approximately 256.96 cm; tune against actual geometry and driving behavior.
Front/rear/side Blender previews were inspected.
The wheel-equipped prototype has simple solid materials; glass is still opaque preview material.
All alternative parts remain hidden in the Blender copy.

57 FBX exports (scripts/export_octavia_parts.py): chassis, four doors, bonnet, boot lid, radiator,
both bumper covers, steering wheel, both front wings, both headlamp units (with corner indicators),
both tail lamps, roof (with aerial), windscreen, both bumper bars, interior (all of 02_Interior except
door cards and trim, which ride on the doors, and the steering wheel), engine (all of 03_Engine except
radiator, fans, exhaust and fuel tank, plus the hidden four-cylinder block, engine_i4_sohc, stood upright
under the cover as a stand-in: the selected 1.8T set has no block), the exhaust (middle pipe and silencer,
pivot at the front hanger), four wheels, 26 suspension parts, a jack, a water can and an oil bottle. The
chassis keeps
the body shell, underbody, wipers, exhaust heat shield, fuel tank, subframes, steering rack and anti-roll
bars. Every mesh carries two UV channels: the model's own, and the Livery map the paint reads its image
through (docs/PAINTING.md).
Every part that crushes or comes off its own way in game is its own mesh.

Suspension (2026-10-12): per corner a strut (spring and damper), hub, brake disc with caliper and arms
(front lower arm; rear lower, upper and trailing arm), plus front tie rods and driveshafts on all four
corners. The rear is the model's hidden 4x4 multi-link instead of the torsion beam: it is stored turned
a quarter turn about X, so the export turns it back and slides it onto the wheel centres (about 1.1 cm);
its springs and shocks come from the torsion-beam set. Pivots are each part's mount on the body (arms,
struts, driveshafts) or the wheel centre (hubs, discs); assembly.json records pivots and the moving end
of each part for ARallyCarPawn::SuspensionMounts. The jack (SM_Octavia_Jack, base, screw, saddle and
handle), a 5 L water can (body, handle and spout) and a 4 L oil bottle are built by the script; the model
has none of them. Materials outside the M_Prototype_* set are mapped to
M_Prototype_Mechanical on export.
The Blender source still retains the individual components for future repairs.
Wheel pivots are centred; panel pivots are bounding centres, not authored hinges.
assembly.json stores per-part placement coordinates and export assumptions.

## Validation

Source geometry and transforms compared between working and prototype files: unchanged.
Four added wheel meshes only.
All 12 FBXs reimported successfully into Blender.
Wheel sizes, axle orientation and centred pivots passed round-trip checks.
Unreal validation is recorded separately in unreal-import-validation.json when the import succeeds.
No claim of driving, collision or multiplayer validation is made by these asset checks.

## Re-export (current pipeline)

Run scripts/export_octavia_parts.py in Blender (background), then scripts/reimport_octavia_meshes.py via
scripts/Invoke-EditorPython.ps1. The export keeps the model's own face normals; the first export in
prepare_octavia_prototype.py recalculated normals "outside" on joined copies, which flipped the
headliner, door cards, much of the body shell and the dashboard, so they vanished from inside the car.
It also exports SM_Octavia_SteeringWheel with its hub pivot and column axis (assembly.json).
Re-import replaces meshes in place and reuses the existing M_* materials.

## In Unreal

After a successful project build, close the editor and run:

    powershell -ExecutionPolicy Bypass -File .\scripts\Invoke-EditorPython.ps1 -Script .\scripts\import_octavia_unreal.py -PassMarker OCTAVIA_IMPORT_PASS

Or, in an open editor, File > Execute Python Script with scripts/import_octavia_unreal.py.
Do not use the -run=pythonscript commandlet: level editing crashed there (null access in LevelEditor.dll).
The script now stops with an error in a commandlet after importing meshes.
It imports new assets into /Game/Vehicles/Octavia/Prototype, checks bounds and wheel pivots,
and assembles a static preview in /Game/Maps/L_GravelLab.
It preserves existing assets and refuses to duplicate an existing map assembly.
Static preview parts have no collision; Chaos geometry, suspension and input remain to be configured.

Pinned engine patch on this machine: 5.8.3 (CL 58210709).