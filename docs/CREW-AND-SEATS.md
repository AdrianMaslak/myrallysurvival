# Crew on foot and in seats

Code: Source/RallySurvival/Public/Player/RallyCharacter.h, Vehicle/RallyCarPawn.h (seats, doors),
Game/RallyGameMode.h (spawning).

## Play

Players spawn on foot near one team car (7 m ahead of the player starts, on the gravel road).
- On foot: WASD or left stick, mouse or right stick to look, Shift sprint, Space jump, E or X (pad) interact.
- At the front-left door: "E: drive". At the front-right door: "E: ride as co-driver".
  Getting in needs the car below 5 km/h and a free seat; getting out needs it below 15 km/h.
- Driving: the driving controls in OCTAVIA-VEHICLE.md; E gets out.
- Co-driver: mouse or right stick looks around the cabin (relative to the car); E gets out.

## Rules

- The server decides every seat change (ARallyCarPawn::ServerEnterSeat / ServerExitSeat): door in reach
  (1.6 m from the standing point outside the door), seat free, speed limits.
- Driver: the player's controller possesses the car, so the driver's machine simulates it
  (MULTIPLAYER.md). The character stays attached to the seat, hidden.
- Co-driver: keeps the on-foot pawn, attached to the seat with collision and movement off. Each machine
  attaches to its own copy of the car, so the co-driver rides the smooth replay of the driver's car.
- Getting out: own door first, then the other side, then the roof, wherever the capsule fits.
- Front doors swing open about their front hinges for 1.4 s on every entry and exit (replicated bitmask).
- A driver leaving the game is put out of the car first, so the car is not destroyed with the player.
- With nobody driving, the server simulates the car with the handbrake on.

## Tests

- scripts/Test-Drive.ps1: the player walks round to the driver's door (-RallyAutoSeat=driver),
  gets in, runs the scripted drive, stops on the handbrake and gets out.
- scripts/Test-TwoPlayer.ps1: the host walks to the right door and rides as co-driver; the client
  walks to the left door and drives. 2026-10-11, about 100 ms round trip: both seated through the
  normal reach checks; the co-driver's view of the car had no jumps over 0.2 m and stayed within
  0.17 m (mean) of the driver's path, 255 ms behind.
- RALLY_SEAT log lines record interactions, entries, exits and which key fired an exit request.
  Some runs logged an exit request nobody scripted, always rejected because the car was too fast;
  the key-state line is there to show whether it was a real key press on the test window.

## Not yet

- Seated bodies are hidden (no seated pose); the driver's hands and the co-driver are invisible.
- No seat swap without getting out; rear seats unused.
- Co-driver tools (pace notes, roadbook, stage timing) come with the first stage.
- Placeholder cylinder body; no first-person arms.
