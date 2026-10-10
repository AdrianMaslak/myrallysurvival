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

Twelve FBX exports: chassis, four doors, bonnet, boot lid, radiator and four wheels.
The export chassis consolidates static pieces for a manageable initial import.
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