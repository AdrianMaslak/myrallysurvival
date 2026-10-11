# Multiplayer car sync

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
- Resets (R) are done by the simulating machine; ResetCount tells observers to jump, not smooth.
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
- Seats, passengers and driver changes are not implemented yet; the hand-over code path is untested.
