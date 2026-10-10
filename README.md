# Rally Survival

Unreal Engine 5.8 C++ project foundation for the rally concept in this folder.

## Current state

Source module, game/editor targets, lightweight rendering defaults and build script are prepared.
Damage-state and repair-condition source is prepared; see docs/DAMAGE-FOUNDATION.md.
Unreal Engine 5.8.3 is installed and the editor module builds.
/Game/Maps/L_GravelLab is generated with a static (non-drivable) Octavia preview; 12 meshes imported and checked.
There is no drivable car, seat system or repair interaction yet; multiplayer testing is pending.
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
4. Generate /Game/Maps/L_GravelLab using scripts/create_gravel_lab.py; see docs/GRAVEL-LAB.md.
   Set it as the editor startup and game default map in Project Settings > Maps & Modes.
5. Follow docs/FIRST-PLAYABLE.md to construct the first driving experiment.

No paid assets, account backend or external networking service is required for the local test.

## Performance starting point

This machine has 16 GiB RAM and a GTX 1650 Ti Max-Q with 4 GiB VRAM.
Lumen, virtual shadow maps and motion blur are disabled; the initial RHI is DX11.
Use simple meshes, conventional lighting, medium scalability and 720p client windows initially.
Actual performance must be measured; these settings are a starting point.

## Architecture

- Chaos Modular Vehicles is the first physics candidate, not yet a validated commitment.
- Vehicle tuning and initial assembly in Blueprints; authoritative gameplay systems in C++.
- Built-in Unreal replication; two-player listen server first.
- Server validates seat ownership, interactions, damage and repair completion.
- Only the driver supplies vehicle input. The passenger must not possess the same vehicle pawn.
- Replicate durable part state; local UI and visual effects derive from that state.
- Do not implement custom physics prediction until the engine vehicle baseline is tested.
- Solo will use the same authoritative gameplay path with one player.

## Version control

Git attributes are prepared for binary assets. Before the first asset commit, run
git lfs install in the repository and verify git lfs version.
A local Git repository is initialized with Git LFS enabled. No remote or commit has been created.

## Reference

- https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-modular-vehicles-quickstart?application_version=5.8
- https://dev.epicgames.com/documentation/en-us/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine?application_version=5.8
