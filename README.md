# Rally Survival

Unreal Engine 5.8 C++ project foundation for the rally concept in this folder.

## Current state

The plan is docs/GAME-PLAN.md (a decade of privateer rallying in 90s Slovakia); its progress section says
what is built. In short:

- The Octavia drives from a first-person cockpit; players start on foot and take the driver or co-driver
  seat (docs/OCTAVIA-VEHICLE.md, docs/CREW-AND-SEATS.md). Two-player car sync passes automated tests on one
  PC (docs/MULTIPLAYER.md); testing on two PCs is pending.
- Crashes dent the body and damage every part; panels hang, detach and are carried (docs/DAMAGE.md).
- Every part is bolted on and off fixing by fixing with the right tool; the engine is nine bolt-on
  modules; parts come worn, stock, sport or rally, with settings changed by hand (docs/DAMAGE.md).
- The countryside map's three special stages are timed, with pace notes, a scrutineer, rivals, prize
  money, road sections with time controls and a regional cup of twelve events on a calendar from April
  1996, a rally weekend every second weekend (docs/STAGES.md); a first day's quests run on it
  (docs/QUESTS.md).
- The businessman's loan and the story's letters (docs/STORY.md). Money, pocket money, a parts catalogue,
  workshop jobs, fuel and tows (docs/MONEY.md); the boot packed with weight
  (docs/BOOT.md); days and nights, rain, headlamps and sleeping, to the morning or to the rally weekend
  (docs/DAYS.md); injury, cold, getting wet, slivovica and cigarettes (docs/BODY.md); the team is saved
  and loaded (docs/SAVING.md).
- The v2 body and interior (Art/Vehicles/Octavia/Body_v2) in a worn, satin finish, with a working cabin: live
  gauges and warning lamps, moving pedals, lever and stalks, lamps that light (beams, tail, brake, reverse,
  indicators, hazards), wipers that clear the rain, a horn, and controls to look at and click
  (docs/OCTAVIA-VEHICLE.md, Cabin and lamps). In first person you see your own body (docs/CHARACTERS.md).
- F1 in game shows the controls; docs/PLAYTEST.md is a half-hour route through all of it.

/Game/Maps/L_GravelLab is the default map (the tests use it); the editor opens
/Game/Maps/Countryside/L_Countryside, where the stages, quests and the moving sun are.
The project targets Unreal 5.8, which supports the installed Visual Studio 2022 17.14 toolchain; additional toolchain components may be requested by the engine.
Installed engine patch: 5.8.3 (CL 58210709). Keep both test machines on that patch.

## Setup

1. In Epic Games Launcher, install Unreal Engine 5.8 (latest available patch in that release line).
   Keep Windows support and templates; omit other target platforms and optional editor debug symbols initially.
   Check the launcher's disk estimate before installing: this machine currently has about 122 GiB free.
2. In Visual Studio Installer, modify Visual Studio 2022 Community.
   Enable Game development with C++, the engine-required MSVC toolset, and Windows SDK.
   Verified locally: Visual Studio 2022 17.14, MSVC 14.44.35207 and Windows SDK 10.0.26100.0. The first Unreal build may identify additional required components.
3. Run from this folder:

   powershell -ExecutionPolicy Bypass -File .\scripts\Build.ps1 -OpenEditor

   For another engine location, add -EngineRoot 'D:\Epic Games\UE_5.8'.
4. Press Play in the editor; the gravel lab and car game mode are configured in Config/DefaultEngine.ini.
   Controls and the unattended drive test are in docs/OCTAVIA-VEHICLE.md.
5. Continue with docs/FIRST-PLAYABLE.md (multiplayer baseline next).

No paid assets, account backend or external networking service is required for the local test.

## Performance starting point

This machine has 16 GiB RAM and a GTX 1650 Ti Max-Q with 4 GiB VRAM.
Lumen, virtual shadow maps and motion blur are disabled; the initial RHI is DX11.
Use simple meshes, conventional lighting, medium scalability and 720p client windows initially.
Actual performance must be measured; these settings are a starting point.

## Architecture

- Project-owned vehicle physics (raycast suspension, tyre and drivetrain model); see docs/VEHICLE-PHYSICS.md.
  Chaos Modular Vehicles was tried first and dropped: its tyre and suspension model could not represent parts and damage.
- Vehicle tuning and initial assembly in Blueprints; authoritative gameplay systems in C++.
- Built-in Unreal replication; two-player listen server first.
- Server validates seat ownership, interactions, damage and repair completion.
- The driver's machine simulates the car; others replay its snapshots (docs/MULTIPLAYER.md).
- Only the driver supplies vehicle input. The passenger must not possess the same vehicle pawn.
- Replicate durable part state; local UI and visual effects derive from that state.
- Server-side car simulation with client prediction is deferred; revisit for competitive open servers.
- Solo will use the same authoritative gameplay path with one player.

## Version control

Git attributes are prepared for binary assets. Before the first asset commit, run
git lfs install in the repository and verify git lfs version.
Git LFS is enabled. The origin remote is github.com/AdrianMaslak/myrallysurvival.

## Reference

- https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-modular-vehicles-quickstart?application_version=5.8
- https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.8
