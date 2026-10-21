# Steam friends and internet sessions

Implemented for UE 5.8, App ID 480. Source and config were checked against the installed engine.
No Unreal build, editor, game, packaging or Steam playtest was run for this change.
Project JSON, PowerShell parsing for the three touched scripts and git diff whitespace checks passed.

## Setup

- Enabled OnlineSubsystem, OnlineSubsystemSteam, OnlineSubsystemUtils and SteamSockets plugins.
  SteamShared is a plugin dependency. OnlineSubsystemNull is enabled by the engine by default.
- The game depends publicly on OnlineSubsystem for the session types, and privately on
  OnlineSubsystemUtils and EngineSettings. The Steam plugins load their own runtime modules;
  game code uses the online interfaces rather than Steam SDK headers.
- DefaultEngine.ini selects Steam and SteamDevAppId=480. bRelaunchInSteam=False lets development
  runs fall back when Steam is absent instead of trying to launch the test app through Steam.
- GameNetDriver uses /Script/SteamSockets.SteamSocketsNetDriver, with
  /Script/OnlineSubsystemUtils.IpNetDriver as its fallback. Beacon and replay definitions are retained.
  NetConnectionClassName is /Script/SteamSockets.SteamSocketsNetConnection.
- bUseSteamNetworking=True registers SteamSockets as the default socket subsystem.
  bAllowP2PPacketRelay=True enables its relay network. SteamSockets calls InitRelayNetworkAccess,
  CreateListenSocketP2P and ConnectP2P. Steam chooses the network route; we do not force an IP path
  or add another transport. No router port forwarding is required for the Steam path.
  Actual internet latency and relay routing still need measurement on two PCs.
- When Steam cannot initialize, or -nosteam is passed, SteamSockets is unavailable and the engine
  selects IpNetDriver. This fallback provides local/IP play, not internet NAT traversal.
- DefaultGame.ini adds Countryside and L_GravelLab to MapsToCook. Both must be present in the lead's
  checkout when cooking. Command-line cook map overrides must include both as well.

## Entry points

URallySessionSubsystem is automatically created for the game instance. No custom game instance or UI
is required. Obtain it through GameInstance->GetSubsystem<URallySessionSubsystem>().

- HostSession() creates NAME_GameSession, then opens /Game/Maps/Countryside/L_Countryside with listen.
  Presence, invites, lobby support and joining in progress are enabled. Advertising plus
  bAllowJoinViaPresenceFriendsOnly selects a Steam friends-only lobby, not a public server listing.
- MaxPlayers in RallySessionSubsystem.h is the single player-limit constant, host included.
  RallyGameMode applies it to GameSession admission as well as the lobby's connection count.
- The Steam overlay invite and friends-list Join Game callback both reach
  OnSessionUserInviteAccepted. The subsystem calls JoinSession, resolves the connection string,
  then calls ClientTravel. Start both games before using the overlay for the App ID 480 test.
- JoinFriend(const FOnlineSessionSearchResult&, int32 LocalUserNum = 0) is the C++ menu entry point.
  Pass a Steam result from FindFriendSession. Friend enumeration and a Blueprint result wrapper
  belong to the future menu. No public lobby search or Steam ID entry UI is included.
- Joining another friend destroys the current session first. Leaving during create/join waits for
  that callback and then destroys the session. LeaveSession() destroys it and opens GameDefaultMap
  (currently L_GravelLab) without listen options. A future leave button must call this method.
- Network and travel failures retain GetLastFailure() for a menu and follow the same cleanup path.
  Pending-connection failures can arrive with a null world; these also reach recovery.
  A failure loading the default map is retained without repeatedly trying to reload it.
  Shutdown unregisters callbacks and requests session destruction; process exit cannot wait for it.
- RALLY_SESSION logs Steam on/off and the user name, creation, accepted invites, join/travel requests,
  destruction and failures. A joined line means the lobby was joined and travel requested; verify
  the remote player actually arrived before treating that as a successful connection.

## Launch and App ID 480

Both PCs need Steam running, logged in with separate accounts that are Steam friends. One account
per PC: two processes under one Steam account are not a real Steam multiplayer test. App ID 480 is
Valve's shared test app (Spacewar), not a private RallySurvival application. Use matching game builds
and direct friend invites. Other developers also use this App ID. Do not treat it as a release ID.

Development editor executable, launched as a standalone game:

    UnrealEditor.exe "<checkout>\RallySurvival.uproject" -game -RallyHost -log
    UnrealEditor.exe "<checkout>\RallySurvival.uproject" -game -log

The second line is for the friend's PC. Use Shift+Tab to invite or join once the host has loaded
Countryside. PIE/editor windows are not the SteamSockets test path.

Packaged Win64 Development build:

    RallySurvival.exe -RallyHost -log
    RallySurvival.exe -log

SteamShared writes steam_appid.txt from SteamDevAppId before initializing Steam in non-shipping
builds, and removes it on shutdown. The path is FPlatformProcess::BaseDir(), next to the actual
running executable: Engine/Binaries/Win64 for UnrealEditor.exe, or the packaged game's
Binaries/Win64 executable, not necessarily the root bootstrap executable. That directory must be
writable. If automatic creation fails, put a text file containing 480 there before launching and
check the SteamShared log. Do not commit or distribute that development file as a Steam release.

App ID 480 does not register this executable as the Steam launch target. Accepting an invitation
while this game is closed may launch Spacewar rather than this build. Cold-launch invitations need
verification after the real application and its Steam launch options are configured.

## Local automated tests

Test-TwoPlayer.ps1 passes -nosteam to both processes, including its two-player panel variant:

    powershell -ExecutionPolicy Bypass -File scripts\Test-TwoPlayer.ps1
    powershell -ExecutionPolicy Bypass -File scripts\Test-TwoPlayer.ps1 -ExtraArgs '-RallyPanelTest'

Test-Panels.ps1 in this checkout starts only one process; its comment points to that variant.
Test-Save.ps1 starts two sequential runs and also passes -nosteam. The other test launch sites were
inspected: they start one game, do not create sessions and need no command-line change. This is a
source inspection, not a claim that those tests have passed with Steam enabled.

The existing loopback scripts retain positional map/IP travel. The new equivalent join switch is:

    UnrealEditor.exe "<checkout>\RallySurvival.uproject" -game -nosteam -RallyJoin=127.0.0.1 -log

Start its IP host with /Game/Maps/L_GravelLab?listen -game -nosteam. RallyJoin requires -nosteam so
the request cannot accidentally use SteamSockets. RallyHost requires Steam; it does not create a
Null session when Steam is unavailable. Without either switch, a single-player launch stays local.
Startup switches run once per game instance, so returning after a failure does not host/join again.

## Lead validation

1. Compile Development Editor and package Win64 Development. Check UHT, independent C++ files,
   Steam plugin DLL staging and both cooked maps. No binary assets were changed here.
2. With Steam running, run Test-TwoPlayer.ps1 normally and with -LagMs 50 -LossPct 1, then its panel
   variant. Both processes must log steam=off and use IpNetDriver. Run Test-Save.ps1 and the
   single-player Test-Panels.ps1 and Test-Drive.ps1 as regression checks.
3. Test RallyJoin against the local IP host. Try an unreachable address and confirm return to the
   default map, a retained reason, and no repeated join attempt. With Steam stopped, also check
   plain positional IP hosting/joining uses the fallback, and RallyHost reports a useful failure.
4. On two PCs with separate Steam accounts and internet connections, host Countryside and test
   both overlay Invite to Game and friends-list Join Game. Repeat with the packaged build.
   Logs must show steam=on, created/invite accepted/joined, SteamSocketsNetDriver and relay network
   initialization. Confirm both players can enter the car and the client can drive without forwarding
   ports. Measure latency; relay initialization alone does not prove which route a connection used.
5. Through a debugger or temporary menu harness, call LeaveSession on client and host, host again,
   and accept an invite while already in a session. Try leaving during create/join. Confirm no stale
   lobby, duplicate callbacks or permanent busy state. GetLastFailure must survive return travel.
6. Test a full lobby (MaxPlayers plus one attempted join), a stale invite, host exit, network loss and
   failed travel. Confirm cleanup, default-map recovery, useful reasons and no automatic rehost.

## Moving to our own App ID

Replace SteamDevAppId=480 with the allocated ID. Configure the Steam application, executable launch
options, depots and test-account access. Retest invitations, including a closed game, using matching
builds. Shipping does not write steam_appid.txt; UE 5.8 uses UE_PROJECT_STEAMSHIPPINGID in
OnlineSubsystemSteam.cpp for its shipping relaunch ID. Configure that shipping build value for the
real app and verify it in the release build. Do not use a development text file as the release setup.

## Engine source checked

All paths below are relative to C:/Program Files/Epic Games/UE_5.8/Engine.

- Plugins/Online/OnlineSubsystem/Source/Public/Interfaces/OnlineSessionInterface.h:
  session calls, completion delegates, invite arguments, resolved address and LexToString.
- Plugins/Online/OnlineSubsystem/Source/Public/Interfaces/OnlineIdentityInterface.h,
  OnlineSubsystem.h, OnlineSubsystemNames.h and OnlineSessionSettings.h: identity and settings.
- Plugins/Online/OnlineSubsystemUtils/Source/OnlineSubsystemUtils/Public/OnlineSubsystemUtils.h:
  world-aware Online::GetSubsystem.
- Plugins/Online/OnlineSubsystemSteam/Source/Public/OnlineSubsystemSteam.h;
  Source/Private/OnlineSubsystemModuleSteam.cpp, OnlineSubsystemSteam.cpp,
  OnlineSessionInterfaceSteam.cpp, OnlineSessionAsyncLobbySteam.cpp and OnlineAsyncTaskManagerSteam.cpp:
  -nosteam, App ID, lobby flags, friends-only type, invite dispatch and asynchronous session cleanup.
- Plugins/Runtime/Steam/SteamSockets/Source/SteamSockets/Public/SteamSocketsNetDriver.h,
  SteamSocketsNetConnection.h and SteamSocketsSubsystem.h; Private/SteamSocketsModule.cpp,
  SteamSocketsNetDriver.cpp, SteamSocketsSubsystem.cpp and SteamSocket.cpp:
  driver availability, socket selection, relay config and P2P transport calls.
- Plugins/Runtime/Steam/SteamShared/Source/SteamShared/Public/SteamSharedModule.h and
  Private/SteamSharedModule.cpp: automatic development App ID file.
- Source/Runtime/Engine/Public/Subsystems/GameInstanceSubsystem.h and Subsystems/Subsystem.h;
  Source/Runtime/Engine/Classes/Engine/GameInstance.h, Engine.h, EngineBaseTypes.h, NetDriver.h,
  World.h, GameFramework/PlayerController.h, GameModeBase.h, GameSession.h and
  Kismet/GameplayStatics.h: lifetime, failure events, admission and travel.
- Source/Runtime/Core/Public/Containers/Ticker.h;
  Source/Runtime/CoreUObject/Public/UObject/UObjectGlobals.h;
  Source/Runtime/EngineSettings/Classes/GameMapsSettings.h: startup polling and return map.
- Source/Developer/DeveloperToolSettings/Classes/Settings/ProjectPackagingSettings.h and
  Config/BaseGame.ini: MapsToCook. Config/BaseEngine.ini: driver defaults and packaging class redirect.
- Source/Runtime/Engine/Private/UnrealEngine.cpp: availability-based driver fallback;
  GameModeBase.cpp and GameSession.cpp: InitGame and MaxPlayers admission.
- OnlineSubsystemSteam.uplugin, SteamSockets.uplugin, OnlineSubsystemUtils.uplugin and
  OnlineSubsystemNull.uplugin: plugin names, modules, dependencies and defaults.

## Start menu (RallyMainMenu)

A normal start (the packaged game, or `-game` without `-unattended`, `-RallyHost` or `-RallyJoin`) opens on the
default map behind a menu: Continue (only with a save), New game, a line on joining friends through Steam, Quit.
Continue and New game host a Steam session and open the countryside as a listen server; without Steam they open it
for one player. New game first renames the save with the time (`RallyTeam_<date-time>.sav`, never deleted); the
console command `RallyNewGame` does the same in game. A session that ends or fails returns to the menu with the
reason on top. Friends join by accepting an invite or with Join Game from the Steam friends list (Shift+Tab).

Checked 2026-10-21 on this PC, Steam logged in: the menu shows (`RALLY_MENU shown steam=on`), and `-RallyHost`
creates the lobby and opens L_Countryside with `SteamSocketsNetDriver ... started listening`. A join needs a second
Steam account on another PC (docs/FRIEND-TEST.md).
