# Gravel lab generator

## Layout

About 497 m from start to finish:
- 300 m straight for acceleration and braking, with a braking marker at 240 m.
- 90-degree right corner, 30 m centreline radius.
- 150 m exit straight, plus 40 m runoff beyond the finish.
- 8 m road width.
- Separate repair bay with a placeholder workbench.
- Separate suspension lane with a 10 cm step.
- Separate impact-test barrier so normal driving does not require a crash.

Ground top sits 2 cm below the road surface, so leaving the road is a small lip.
Distance, brake and finish markers have no collision; they are visual guides.
After changing the layout, scripts/update_gravel_lab.py moves existing map actors to match
(location, rotation, scale and collision profile only).

Geometry is greybox only. Gravel friction, visual materials, car, walking pawn,
timer, repair interactions and pace notes are not created by this script.
The default game mode has not been replaced.

## Generate after the first successful Unreal build

1. Open the project. Python Script Plugin and Editor Scripting Utilities are enabled in its descriptor.
2. Save your current level and stop Play In Editor.
3. Choose File > Execute Python Script and select scripts/create_gravel_lab.py.
4. The script creates and saves /Game/Maps/L_GravelLab.
5. Inspect road seams, collision, spawn clearance and lighting in the editor.
6. Set the map as the editor startup/game default map in Project Settings > Maps & Modes.
7. Add the first working Chaos vehicle, then tune and assign gravel physics.

The script refuses to overwrite an existing lab or discard a dirty current map.
If generation fails partway through, inspect the existing map before choosing how to repair it.
No automatic deletion or replacement is performed.

## Offline check

python scripts/create_gravel_lab.py --dry-run

Checks finite positive geometry, unique labels and corner connections.
Unreal API execution, rendering and vehicle collision still need verification in the editor.

References:
- https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-the-unreal-editor-using-python
- https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/EditorActorSubsystem?application_version=5.8
