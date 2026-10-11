# First playable: gravel lab

## Scope

A short test track, one car, two players. No survival systems, economy or long stage.
First prove vehicle handling and replication. Then implement seat transitions and one wheel repair.

## 1. Vehicle baseline

- Build a flat 500 m test road with a turn, small bump, braking zone and collision barrier.
- Create a gravel physical material; tune actual tyre behavior experimentally.
- Vehicle physics is project-owned (docs/VEHICLE-PHYSICS.md); async physics at 60 Hz is enabled in Config/DefaultEngine.ini.
- Use the supplied example vehicle if available, otherwise assemble a simple chassis and four wheels.
- Validate wheel orientation, mass, suspension travel, brake and steering inputs.
- Save the vehicle Blueprint and map under Content.
- First check acceleration, braking, reverse, turning and recovery after a collision.
- Status 2026-10-11: Octavia drives in the lab on gravel surfaces with the project physics model;
  launch, braking, cornering, handbrake turn and reverse pass the unattended self-test (docs/OCTAVIA-VEHICLE.md).
  Collision recovery and feel tuning by hand remain.

## 2. Multiplayer baseline

- Start two players in Play In Editor, using Play As Listen Server.
- Observe the same car from the second player before implementing passenger seats.
- Repeat with the remote client driving: host-only driving is not an adequate test.
- Use the editor's network emulation settings and record the configured delay/loss.
- Repeat in separate processes, then on two PCs before claiming success.
- Verify handling at zero emulated delay and around 100 ms measured RTT.
- Test at 1 percent packet loss and document instability.
- Reject persistent jitter, divergent vehicle position, or unresponsive steering.
- Record frame rate, network conditions, driver machine, engine patch and observations.

## 3. Seats and interaction

- Add a walking character and driver/passenger interaction points.
- Keep the passenger as a separate player actor attached to a seat; only the driver possesses the car.
- Server arbitrates simultaneous entry attempts and seat swaps.
- Validate proximity, occupancy and safe exit space on the server.
- Test either player driving, switching roles, disconnecting and reconnecting.
- Verify the driver leaving the seat clears throttle/brake input appropriately.
- Status 2026-10-11: on-foot character, driver and co-driver seats, enter/exit with server checks and
  door animation are in (CREW-AND-SEATS.md); automated walk-in, drive, exit and co-driver tests pass on one PC.
  Seat swaps, reconnects and human two-PC tests remain.

## 4. One repair

- Server triggers a front-left puncture.
- Both players see the same damage and driving symptom.
- Stop, exit, retrieve a spare, replace the wheel, continue.
- Server validates distance, stopped car, required tool and spare consumption.
- Two players attempting the repair must consume one spare and complete one repair.
- New clients receive current part state, including repairs already completed.

## Exit criteria

Both players can drive and ride, recover from a crash and replace a wheel.
No persistent car divergence under the recorded network test conditions.
Repeat solo. Log results before extending to suspension/radiator or a 3-5 km stage.

## Test record

Automated runs on one PC (host and client processes); details in MULTIPLAYER.md.
Still needed: two PCs, a human driver on each side, and frame-rate figures from separate machines.

| Engine patch | Scenario | Driver host/client | RTT/loss | FPS | Result |
| --- | --- | --- | --- | --- | --- |
| 5.8.3 | Scripted drive, engine default sync | client | 0 ms / 0% | ~9 | Fail: rubber-banding, 105 jumps > 0.5 m |
| 5.8.3 | Scripted drive, engine default sync | client | ~100 ms / 0% | ~9 | Fail: 157 jumps > 0.5 m |
| 5.8.3 | Scripted drive, driver-simulated sync | client | 0 ms / 0% | low | Pass: 0 driver jumps, host view 215 ms behind |
| 5.8.3 | Scripted drive, driver-simulated sync | client | ~100 ms / 0% | low | Pass: 1 jump each side, 330 ms behind |
| 5.8.3 | Scripted drive, driver-simulated sync | client | ~100 ms / 1% | low | Pass: 0 jumps, 355 ms behind |
