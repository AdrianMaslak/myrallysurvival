# Rally Survival

Unreal Engine 5.8 C++ project foundation for the rally concept in this folder.

## Current state

Source module, game/editor targets, lightweight rendering defaults and build script are prepared.
Damage-state and repair-condition source is prepared; see docs/DAMAGE-FOUNDATION.md.
Unreal Engine 5.8.3 is installed and the editor module builds.
/Game/Maps/L_GravelLab is the default map. The Octavia drives in it from a first-person cockpit view
(single player, self-tested); see docs/OCTAVIA-VEHICLE.md.
There is no seat system or repair interaction yet; multiplayer testing is pending.
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
- Only the driver supplies vehicle input. The passenger must not possess the same vehicle pawn.
- Replicate durable part state; local UI and visual effects derive from that state.
- Do not implement client-side physics prediction until the replicated baseline is tested with two players.
- Solo will use the same authoritative gameplay path with one player.

## Version control

Git attributes are prepared for binary assets. Before the first asset commit, run
git lfs install in the repository and verify git lfs version.
A local Git repository is initialized with Git LFS enabled. No remote or commit has been created.

## Reference

- https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-modular-vehicles-quickstart?application_version=5.8
- https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.8
