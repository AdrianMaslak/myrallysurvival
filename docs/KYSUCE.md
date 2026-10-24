# Kysuce rally (real road)

Map: `/Game/Maps/Kysuce/L_Kysuce`. A real road in northern Slovakia, from Oravska Lesna over the ridge to
Nova Bystrica and on through the Bystrica valley and over a second ridge, cut into three special stages
with service parks between them. Placeholder shapes and flat colours only: no textures yet.

| Stage | Length | Surfaces | Ascent / descent | Splits |
| --- | --- | --- | --- | --- |
| SS1 Oravska Lesna | 12.3 km | Tarmac, snow above 900 m (2.6 km over the top) | 168 / 387 m | 3 |
| SS2 Bystrica Valley | 9.6 km | Gravel | 10 / 117 m | 2 |
| SS3 Kysuce Ridge | 13.9 km | Gravel, 3 km of wet mud on the climb, tarmac down to the finish | 239 / 245 m | 3 |

Steepest 20 m: 12% (the road is held to it). The HQ (where the car starts) is at the start of the GPX, 150
m before SS1. Each stage finishes 150 m before a service park and the next starts 150 m after it; a last
park stands at the end of SS3. Every park has the `RallyService` marker, so the existing repair and service
actions work there (within 35 m), with the usual money, tools and parts. `Art/Maps/Kysuce/Route_Overview.svg`
shows the route, its surfaces and the elevation.

## Road edges

Leaving the road should cost something. Beside the 6.2 m carriageway each side has, in its own mesh and
the off-road ground material: a 0.4 m shoulder, then either a ditch and a bank, or a drop.

- **Bank** (the ground beside the road is higher): a ditch 1.5 m wide and 0.25 m deep in villages, 0.6 m
  in fields and 0.85 m in forest, then a bank up to the real ground 12 m out (up to 4 m high). Outside
  villages there is always a bank, 0.4 m (fields) to 0.8 m (forest), varying along the road. Fast enough,
  a car jumps the ditch into the bank; slower, it drops in and may stick.
- **Drop** (the ground falls away more than 1.5 m): no ditch, a slope down to the real ground, up to 6 m
  below. On tarmac and snow, drops over 2.5 m get a guardrail.

About a third of the road has a bank on both sides; a fifth has a drop on one side. Boulders sit on the
verges outside villages, and every tree within 40 m of the road has a solid trunk.

The surface is uneven: bumps and ripples from 0.8 m to 9 m long, up to about 2.5 cm on tarmac, 3.5 cm
on snow, 6 cm on gravel and 7 cm on mud, and twice as rough (plus 5 cm) on the verges. Kysuce roads
have a point every metre (mud every 0.5 m). The bumps come from the world position, so every machine and
every road section agrees on them. Change them with `BUMP_CM`, `DITCH` and `LOW_BANK` in the layout script.

## Checkpoints

Each stage has split times: one for every whole 4 km, three at most, evenly along it
(`Rally::Stage::SplitCount`). A blue SPLIT board stands at each. As the car passes one, the clock is
recorded and shown for 10 s against the reference time's share up to it (red when slower). Before each
start a yellow TC board marks the time control; 300, 200 and 100 m boards count down to the finish.
Splits work on every map: the Highlands stages get one each, the short countryside stages none.

## Data

- `Art/Maps/Kysuce/Source/route.gpx`: the road (exported from Google Maps with mapstogpx). Google's terms
  don't allow its map data in a game. Before release, replace it with the same road traced from
  OpenStreetMap (ODbL, credit "© OpenStreetMap contributors"): export a GPX of it and save it over this
  file.
- `Art/Maps/Kysuce/Source/dem.bin` + `dem.json`: Copernicus GLO-30 elevation cut to the map (decimetres,
  1 arc second). It is a surface model: forest canopy and roofs are in it, which is why the road profile is
  smoothed over about 100 m. Its licence needs the credit line in `dem.json`. For real ground under the
  trees, Slovakia's 1 m lidar terrain model (ZBGIS DMR 5.0) is free; cut it to the same grid in WGS84 to
  replace `dem.bin`.
- `scripts/kysuce_layout.py` (Python 3 with numpy; tifffile only to cut the elevation again) writes
  `route.json`, `props.json`, `Kysuce.r16`, the overview and `Source/RallySurvival/Private/Game/KysuceStages.inl`.
  At the top of the script: `SERVICES` (where the parks are), `SNOW_ABOVE_M`, `SS3_SURFACES` and `ZONES`.
  `ZONES` is the land use along the road (village, farm or forest) and decides the placeholder scenery.
  It is an estimate: correct it against the real road.

Coordinates are metres, X east and Y south (Unreal's frame), Z up, origin at the HQ at its road height
(796 m above sea level). The Landscape is 2017 x 2017 samples, 9.3 m apart (18.8 km square, 1024
components). Under the road and its ditches it is at least 0.4 m below them; from 11 to 22 m out it rises
to each bank's top, then blends into the real ground by 80 m. Behind a bank's top there is a dip (about
1 m, up to 3 m) where the 9.3 m grid cannot follow the bank.

## Build and test

Close Unreal first. The generated files are committed, so the layout only needs running after a change.

```powershell
python scripts\kysuce_layout.py
.\scripts\Build.ps1
.\scripts\Build-Kysuce.ps1
.\scripts\Test-Stage.ps1 -Map Kysuce -Stage SS1
```

The build replaces actors tagged `KysuceGenerated` and keeps everything else, so hand-placed assets survive
a rebuild. Scenery is instanced: one actor per kind (`Kysuce_Spruce`, `Kysuce_House`, `Kysuce_Rail` ...).
Swap a kind's mesh and material on its actor, or delete it and paint real assets with the Foliage tool.

## Not yet verified

Written and checked outside Unreal (route, grades, heightmap clearance under the road, prop placement);
not yet built or driven in the editor. Check the build log, the Landscape's performance on the 4 GB card
(1024 components; drop to 1513 samples in `SIZE` if needed), and drive each stage once.
