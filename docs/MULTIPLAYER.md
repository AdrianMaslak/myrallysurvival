# Multiplayer car sync

## Steam friends

Steam lobby hosting, overlay invites and internet transport are set up in [STEAM-SETUP.md](STEAM-SETUP.md).
Use `-RallyHost` to host Countryside. Local two-process tests use `-nosteam` and the IP driver.
The lobby and game admission share `URallySessionSubsystem::MaxPlayers` (currently 4, host included).
Steam internet play is not yet built or tested; the local results below concern the existing IP path.

Decision (2026-10-11): the driver's machine simulates the car. Chosen over server simulation with
prediction (cheat-proof but several sessions of work, experimental) and plain server simulation
(driver feels round-trip input delay).

## Why

The first two-player test used the engine default: client and server both simulated the client's car,
the server with inputs arriving later, and replicated movement pulled the client back.
The driver's car rubber-banded by 1.2 to 1.5 m on average, with 105 to 157 jumps over 0.5 m per 40 s drive,
even with no added lag. In first person, a co-driver would see that as violent shaking.

## How it works (ARallyCarPawn)

- The machine whose player is in the driver's seat runs the full vehicle physics. With nobody driving,
  the server runs it. That machine is the only one that applies forces.
- Every other machine (server included, when a client drives) keeps the body kinematic and replays
  FRallyCarNetState snapshots: position, rotation, velocities, steering, gear, rpm, wheel travel and spin.
- 30 snapshots per second. A client driver sends them to the server (unreliable RPC); the server checks
  them (no NaN, under 80 m/s, no jumps over 30 m without a reset) and replicates them to everyone except the driver.
- Replay: a playout clock runs 100 ms behind the newest snapshot and slews gently toward that target;
  position uses cubic Hermite interpolation with the snapshot velocities, rotation uses slerp, and the
  replay extrapolates up to 0.25 s if snapshots stop arriving.
- Snapshot times use the sender's physics clock. The client's estimate of server time is corrected in
  steps, and game time outruns physics on a machine below about 20 fps; both made replays stutter.
- When the driver changes (or leaves), the new simulating machine continues from the replayed velocity.
  Its wheels start afresh with the dampers taking the measured compression, so there is no jolt.
  Anything attached to the car (stowed spares, a jack) must not auto-weld: Unreal welds attached
  auto-weld bodies into a body that switches from following to simulating, and a welded 7 kg spare disc
  once replaced the car's mass and launched it 47 m up on the client (fixed 2026-10-12; ARallyDebrisActor
  turns bAutoWeld off).
- Resets (R) are done by the simulating machine; ResetCount tells observers to jump, not smooth.
- The snapshots also carry the power the engine makes (kW). The server runs the engine's temperature,
  coolant, oil, fuel and wear from it and decides whether the engine runs (URallyEngineComponent); that
  state replicates (whole degrees, tenths of a litre, whole litres of fuel, running) and the simulating
  machine applies it to its physics. Floor hits are reported like impacts (ServerReportUnderside).
- Sound runs on every machine from state it already has; only crash and floor-hit sounds (one unreliable
  multicast each, skipped by the machine that felt it and played it at once) and failed starts are sent
  (docs/SOUND.md). The ignition is a server RPC (I). A battery cut-out is decided by the simulating machine alone; every machine makes its own
  steam and smoke from the replicated state.
- Engine movement replication is off; inputs are not replicated.

Trust model: co-op friends. A modified client could fake its car's path within the plausibility limits.
Competitive open servers would need stronger server checks or server-side simulation.

## Test

Close the editor, then:

    powershell -ExecutionPolicy Bypass -File .\scripts\Test-TwoPlayer.ps1 [-LagMs 50] [-LossPct 1]

Starts a listen-server host and a client on this PC; the client's car runs the scripted drive and both
processes log every car (-RallyNetLog). scripts/net_compare.py then reports, per car, how far behind the
other machine's view runs, how far its path strays, and jumps beyond constant-velocity motion.
LagMs is added per side, so 50 means about 100 ms round trip. The script lowers resolution and allows
8 physics steps per frame because two games on this laptop run near 10 fps.

## Results, client driving (2026-10-11, UE 5.8.3, one PC)

| Network | Host view delay | Path error mean / max | Jumps > 0.5 m, driver / host view |
| --- | --- | --- | --- |
| Before: engine default, no lag | n/a | 1.21 / 7.58 m | 105 (driver) |
| Before: engine default, 100 ms RTT | n/a | 1.49 / 5.30 m | 157 (driver) |
| No lag | 215 ms | 0.37 / 2.11 m | 0 / 1 |
| 100 ms RTT | 330 ms | 0.40 / 1.48 m | 1 / 1 (same moment on both) |
| 100 ms RTT, 1% loss | 355 ms | 0.59 / 2.14 m | 0 / 0 |

Path error is measured after removing the best constant delay; most of it is delay that varies with
packet timing, not a wrong path.

## Known limits

- Not yet tested on two PCs or over the internet; both games ran on one laptop near 10 fps.
- Two cars driven from different machines each see the other as kinematic, so car-to-car contact
  pushes only the local car. Fine for co-op in one car; needs work for rallies with several teams.
- Below about 20 fps Unreal runs at most 3 physics steps per frame and the driver's car slows down.
- Seats and passengers are built (CREW-AND-SEATS.md). Hand-over from a parked server-simulated car to a
  client driver is exercised by every two-player test; a driver leaving mid-drive is less tested.

## Smoothing over the internet (rules only)

`Source/RallySurvival/Public/Vehicle/NetSmoothingRules.h` adds pure C++ rules in `Rally::NetSmooth`.
The pawn is unchanged. These rules have not been tested in Unreal or on the internet.

- Times are seconds. Positions and linear velocities are metres and m/s. Rotations are unit quaternions
  in X/Y/Z/W order. Angular velocities are world-space radians/s.
- `EstimateOffset` takes the minimum receive-minus-send time in the last 120 accepted arrivals
  (about 4 s at 30 Hz). One-way timestamps cannot distinguish clock offset from baseline transit time.
  The estimate includes that transit time. In the clock test, a 7 s offset and 40 ms minimum transit
  converge to 7.040 s. A minimum expires after 120 newer arrivals.
- `ChooseDelay` uses the nearest-rank 99th percentile of receive time minus offset minus the previous
  accepted send time. This includes jitter and missing send intervals. The floor is 33.333 ms.
  It is the smallest delay covering that percentile of the measured arrival boundaries, not a guarantee
  for future traffic or 99% of render frames. A single long outage is intentionally not covered.
- `Slew` changes delay by at most 10 ms/s. `AdvancePlayout` also limits clock corrections to 1% of
  elapsed time, including changes in the offset estimate. Playback stays between 0.99 and 1.01 speed.
  These are chosen limits, not a perceptual result from a player test. Sudden worse traffic can starve
  playback while delay catches up.
- `Sample` uses Hermite position and its derivative for velocity, plus shortest-arc rotation slerp.
  With no newer snapshot it extrapolates linear and angular motion for at most 100 ms, then holds that
  pose and returns zero velocities. Empty buffers return `Empty`; times before the oldest snapshot
  return that snapshot with `Held`. Input snapshots must have finite values and strictly increasing times.
- `BeginCorrection` stores the difference between the displayed pose and a new sample. `ApplyCorrection`
  fades that difference with smoothstep over 250 ms, with zero correction slope at each end. Apply it
  to each new sample, so the car continues moving. Physical sample velocities are unchanged. Errors
  of at least 30 m, or an explicit reset, snap immediately. A rotation-only correction blends normally.

Lead wiring:

1. In `AddSnapshot`, record a monotonic local arrival time with each accepted sender timestamp.
   Reject duplicates and stale packets as today. Keep the latest 121 arrival pairs (120 gaps) and the
   sorted snapshot buffer. Convert Unreal centimetres and cm/s to metres and m/s at this boundary.
2. Initialize delay from `ChooseDelay` and playout to local now minus offset minus delay. Each observer
   tick, slew delay toward `ChooseDelay`, advance playout with `AdvancePlayout`, then call `Sample`.
   Convert the result back to Unreal units. Keep the existing separate wheel, gear and engine replay.
3. When a new arrival changes the sampled path, evaluate the old displayed pose and new sample at the
   same playout time. Begin or rebase a correction from those poses. Apply it each tick using elapsed
   correction age; do not restart it every render frame. Handle `Empty` by leaving the body alone.
4. On `ResetCount`, a huge teleport or a sender/driver change, clear snapshots, arrival history and
   correction state; initialize the new clock and snap to the new pose. Never Hermite across a reset.
   Arrival history assumes sender and receiver clocks advance at the same rate. The existing physics
   clock slowdown below about 20 fps needs separate handling; the 1% slew cannot compensate for it.

`scripts\Test-Rules.cmd` compiles and runs `Tests/NetSmoothingTests.cpp`: 61 checks. The complete command
passes 401 checks (264 condition, 76 vehicle, 61 smoothing). No Unreal build or game run is needed.
The streams send at 30 Hz and render at 120 Hz for 40 s, measuring 4,200 frames after a 5 s warmup.
The path is `(20t, 5 sin(t), 0)` metres with analytic velocities and yaw. Jitter uses an LCG seeded
with `0xC600`; loss removes three seeded sends per block of 100 (36 of 1,200). Baseline transit is
40 ms; jitter adds 10-40 ms. The batch case delivers arrivals at 50 ms boundaries.

| Synthetic stream | Position error mean / max (m) | Buffer delay mean / min / max (ms) | Extrapolated / held frames |
| --- | --- | --- | --- |
| Clean | <0.000001 / <0.000001 | 33.333 / 33.333 / 33.333 | 0 / 0 |
| Jitter 10-40 ms | <0.000001 / <0.000001 | 62.800 / 61.675 / 63.143 | 0 / 0 |
| Jitter, 3% loss | <0.000001 / 0.000493 | 83.612 / 72.011 / 94.232 | 14 / 0 |
| Jitter, 600 ms outage | 0.073029 / 10.162853 | 62.833 / 61.675 / 63.178 | 12 / 60 |
| Jitter, 50 ms arrival batches | <0.000001 / <0.000001 | 83.330 / 81.917 / 83.333 | 0 / 0 |

Error compares the raw sample with the analytic path at playout time, before visual correction.
Delay is additional buffering above the minimum transit estimate, not end-to-end latency.
The outage deliberately demonstrates bounded prediction followed by a stationary hold. Correction
continuity, midpoint, completion, rotation, rebasing, teleport and reset are checked separately.
These smooth synthetic paths do not measure collisions, abrupt steering, real clock drift or player comfort.
