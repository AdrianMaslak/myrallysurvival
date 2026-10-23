# Highlands rally

Map: `/Game/Maps/Highlands/L_Highlands`.
Open it in the Unreal Content Browser and press Play. Walk to the car and press E.

| Stage | Length | Surface | Ascent / descent |
| --- | --- | --- | --- |
| SS1 Blackwater Mud | 6,000 m | Wet mud | 100 / 20 m |
| SS2 Pine Ridge Gravel | 7,000 m | Gravel | 184 / 104 m |
| SS3 High Pass Hairpins | 7,000 m | Tarmac | 210 / 170 m |

The stage centre lines total 20,000 m in 3D. Two 180 m service links and the
100 m start approach are outside this distance. SS3 has five 180-degree bends.
Maximum grade is 14.1%. Stage timing and pace notes use the new route data.
The countryside routes and first-day quests remain on their original map.

The terrain is an editable Unreal Landscape: 1009 x 1009 height samples,
256 components, 8 m sample spacing, 8.064 km square. The source is a 16-bit
heightmap. Narrow roads use separate collision meshes above carved terrain.
Trees, rocks and logs are original low-poly Blender assets.

Two signed service parks sit between the stages. Each has a flat apron,
canopies, benches and spare tyres. The existing service-yard actions recognize
actors within 35 m of each `RallyService` marker. This does not grant free repairs
or bypass the existing money, tools and parts requirements.

## Surfaces and changes during driving

| Surface | Friction | Looseness | Rolling resistance |
| --- | --- | --- | --- |
| Wet mud | 0.38 | 1 | 0.075 |
| Gravel | 0.65 | 1 | 0.030 |
| Tarmac | 0.90 | 0 | 0.015 |

These are initial tuning values. Weather can further reduce grip through the
existing car physics. Dark mud has lower material roughness than gravel.

Mud is a deformable road layer over the Landscape. Attached wheels trace the
ground. Moving wheels compress nearby road vertices; repeated passes deepen the
tracks to a maximum of 12 cm. Both rendering and collision are updated. The
Landscape itself is not modified during play. Shared section ends stay fixed.
Only nearby mud sections update, at 10 Hz, with asynchronous collision cooking.
The server replicates the rut depth field to clients.

Rocks and short logs are Chaos bodies, with convex collision, CCD and replicated
movement. They weigh 28 kg and 45 kg. They can be displaced by impacts and block
the car's body and wheel traces. The existing impact system determines damage
from the collision; hitting debris does not apply an arbitrary damage amount.

Ruts and debris positions last for the current loaded session. They are not yet
included in save-game persistence. Multiplayer behaviour and a complete 20 km
drive still require playtesting.

## Build and verification

Close Unreal before running the authoring or test scripts. Only one game/editor
may run at a time.

```powershell
python scripts\highlands_layout.py
.\scripts\Build.ps1
.\scripts\Build-Highlands.ps1 -Art
.\scripts\Build-Highlands.ps1 -Validate
.\scripts\Build-Highlands.ps1 -Preview
.\scripts\Build-Highlands.ps1 -Drive
```

The layout script exports the timing centre lines to `HighlandsStages.inl`.
The builder backs up an existing generated map under `Saved/HighlandsBackups`.
It replaces actors tagged `HighlandsGenerated`; it preserves untagged additions.
The generator owns only `Content/Maps/Highlands` and `Art/Maps/Highlands`.

`validation.json` records saved-map lane traces and a synthetic rut collision test.
`runtime.log` records the spawn/seat test, a moving debris test, mud collision,
the 20 km timing data check and actual wheel rut updates during an automatic drive.
`Preview/latest.json` lists the most recent engine screenshots.

Verified in Unreal 5.8: 6,138 saved-map lane traces passed; the collision rut
measured 12 cm. The runtime test confirmed driver entry, wheel-created ruts,
the mud physical material, moving debris, two service areas and 20,000 m of
stage timing data. All 451 pure-rule checks passed. This is a playable environment
prototype; a complete stage drive and multiplayer/performance tests remain.
