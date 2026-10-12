# Lipova countryside rally

Map: `/Game/Maps/Countryside/L_Countryside`.

One shared service yard and three separate routes through a fictional Central/Eastern European
countryside. This is a first playable environment with original, deliberately simple geometry.
The existing gravel lab remains the startup map and physics test level.

| Stage | Length | Surface | Road width | Character |
| --- | --- | --- | --- | --- |
| SS1 Lipova Village | 1.29 km | Asphalt | 6.6 m | Village houses, faster curves and a climb over open countryside |
| SS2 Pine Ridge | 1.54 km | Gravel | 5.8 m | Forest, tighter slow bends and rolling terrain |
| SS3 Fields of Hlinik | 1.73 km | Gravel then asphalt | 6.2 m | Fields, barns and silos, flowing sections and tighter changes of direction |

Combined stage distance: 4.56 km, plus service access roads. All starts and finishes return to
the service yard through short separate links. Arrows on `Art/Maps/Countryside/Route_Overview.svg`
show the intended direction. Starts use red gantries; finishes use dark gantries. Both have labels.

## Play

1. Save the current level in Unreal.
2. In Content Browser, open `Content/Maps/Countryside/L_Countryside`.
3. Press Play. Walk to the team car and press E to enter, using the existing game controls.
4. Follow the SS1, SS2, or SS3 signs out of the service yard.

The existing game mode spawns the car at (10, 0, 1.2) metres. The service apron is centred at the
world origin, with ground surface z=0, so this map works without modifying that spawn logic.
Two PlayerStarts sit behind the car. Do not move the service yard without coordinating a spawn change.

Start/finish TargetPoint actors in `00_Gameplay/StageAnchors` have `StageAnchor`, stage ID, and
`START` or `FINISH` tags. They are integration points for future timing, checkpoint and pace-note
systems. Gates and arrows are currently visual markers; there is no stage timer, progress UI,
checkpoint enforcement, race restart, service repair interaction or dynamic traffic in this map.

## Surfaces and geometry

Roads are continuous triangulated strips, sampled approximately every 3 m. Shared edges match
exactly at the mixed-surface transition. Six wider junction aprons close the angled joins to the
service links. Gravel shoulders blend down into the terrain; road meshes
use complex-as-simple static collision, supporting the existing simple visibility wheel traces.
Terrain is a set of static mesh tiles, not an editable Unreal Landscape.

The map owns its own RallyPhysicalMaterial assets under `Maps/Countryside/Materials`:

| Surface | Friction | Looseness | Rolling resistance |
| --- | --- | --- | --- |
| Asphalt | 0.90 | 0 | 0.015 |
| Gravel | 0.65 | 1 | 0.030 |
| Off-road ground | 0.50 | 1 | 0.050 |

These are initial map tuning values. Gravel and ground match the lab's existing surface values;
asphalt is an initial choice, not a measured tyre/surface calibration. They do not modify the car.
The material overrides are assigned to each road section, including the two surface types on SS3.

Buildings and tree trunks have static collision. Leaves, labels, paint lines and route markers do not.
Obstacles use simple collision: each wall box, roof prism, chimney, fence, canopy post, silo and trunk is
exported as its own `UCX_` convex hull, never one hull around a whole village or tree cluster, so the
gaps between them stay open. Window frames and doors are trim and have no hull. Their meshes keep the
default trace flag, so complex traces still hit the triangles. The importer checks each obstacle
mesh's hull count against the manifest. Terrain and roads stay complex-as-simple to keep every bump.
Complex-as-simple objects cannot simulate physics, so future movable or breakable scenery should
follow the obstacle pattern.
Trees are grouped into spatial clusters with distance culling. Lighting is movable daylight and sky
fill, compatible with the project's existing non-Lumen, DX11 setup. No lighting build is required.
The environment is a visual prototype; detailed textures, close-up foliage and boundary treatment
remain future art work. Map performance and complete stage driving require playtesting on target PCs.

## Authoring and rebuild

Only these new locations belong to this map work:

- `scripts/countryside_layout.py`: route design, elevation, spatial queries and overview.
- `scripts/build_countryside_art.py`: Blender mesh generation, original buildings, vegetation and FBX exports.
- `scripts/import_countryside.py`: Unreal materials, mesh import, map assembly and collision validation.
- `scripts/preview_countryside.py`: engine screenshot capture.
- `scripts/Build-Countryside.ps1`: isolated authoring workflow.
- `Art/Maps/Countryside/`: editable Blender source, FBX files, manifest, overview and QA artifacts.
- `Content/Maps/Countryside/`: map and all map-specific Unreal assets.

Offline route check:

```powershell
python scripts\countryside_layout.py
```

Generate art using the installed Blender (or substitute your Blender executable):

```powershell
& 'C:\Program Files (x86)\Steam\steamapps\common\Blender\blender.exe' --background --factory-startup --python scripts\build_countryside_art.py
```

Build in `Saved/CountrysideAuthoring`, using a snapshot of the current game DLL and vehicle assets.
This leaves the open main project, its current level, physics code and default settings alone:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\Build-Countryside.ps1
powershell -ExecutionPolicy Bypass -File scripts\Build-Countryside.ps1 -Preview
powershell -ExecutionPolicy Bypass -File scripts\Build-Countryside.ps1 -Drive
```

`-Drive` is a spawn and driver-entry smoke test, using the existing game's automatic walk-to-car
option. It checks the runtime seat-entry event; it does not drive the three stages. A hidden game
window may not render a cockpit screenshot. The regular environment previews use the editor viewport.

The initial delivery can be copied into the main project with `-Publish` after a successful build.
The runner refuses to overwrite an existing live countryside folder. Subsequent replacements should
be reviewed with that map closed, so editor saves cannot overwrite newly generated content.
Do not use the main project's general editor runner while another editor is working on that project.

`Export/countryside.json` records all routes, stage anchors, physical surface assignments and mesh
bounds. `unreal-validation.json` records imported geometry and downward road collision checks after
saving and reopening the map. `Preview/` contains engine screenshots, not concept images.

Validated in Unreal 5.8.3: 193 imported meshes, 352,486 source triangles, 1,454 trees, and 1,215
downward collision checks across all three lanes sampled on each stage and service link. The checks
start 30 m above the route, so overhead building/trunk intrusions are detected as well as missing road
surfaces. All passed after the service junction aprons were added. Unreal map check: zero errors and
zero warnings. This does not replace driving every stage or measuring multiplayer/performance behavior.

Runtime smoke check also passed: the new map loaded with RallyGameMode and logged
`RALLY_SEAT enter RallyCharacter_0 seat=0`, confirming the spawn position and on-foot approach work.
Four corrected engine views were visually inspected. Preview cameras, stage anchors, labels and sun
use named `Rotator(pitch=..., yaw=..., roll=...)` arguments to avoid Unreal Python's positional order.

No existing gameplay sources, car assets, global configuration or lab assets are owned or changed by
this generator. The game binary copied into the isolated project is a snapshot; later mechanics changes
are picked up the next time the authoring runner starts.

Engine API references used for the authoring tools:
- https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Automation/TakeHighResScreenshot
- https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/BodySetup
