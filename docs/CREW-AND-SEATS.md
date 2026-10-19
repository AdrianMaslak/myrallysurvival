# Crew on foot and in seats

Code: Source/RallySurvival/Public/Player/RallyCharacter.h, Vehicle/RallyCarPawn.h (seats, doors),
Game/RallyGameMode.h (spawning).

## Play

Players spawn on foot near one team car (7 m ahead of the player starts, on the gravel road).
- On foot: WASD or left stick, mouse or right stick to look, Shift sprint, Space jump, E or X (pad) interact,
  F or Y (pad) open or shut a door, the bonnet or the boot lid (DAMAGE.md); seated, your own door.
  R or right shoulder (pad) takes a part off: unbolt, pull off, cut a strap (DAMAGE.md).
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
- Getting out: own unjammed door first, then the other unjammed side, then the roof, wherever the capsule fits.
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

## Panel recovery controls

On foot, face a fallen panel and press E to carry it; G drops it. B straps on a carried panel or
secures a loose one in reach. Each player has two straps; inventory and two-person carrying are pending.
Carrying slows walking and blocks sprinting, jumping and seat entry.

Fixings and tools (DAMAGE.md, Fixings): every panel, wheel and suspension part is held by a few bolts,
nuts or screws you can see and aim at. The tool in hand shows at the bottom of the screen; 1-8 or the
mouse wheel change it: 1 bare hands, 2-5 the 10, 13, 17 and 19 mm spanners, 6 screwdriver, 7 pliers,
8 hammer. Aim at a fixing and hold R for half a second to undo it, or hold E to do it up, with the tool
that fits it (the prompt names it). Once all of a part's fixings are out, R on the part lifts it off into
your hands. A carried part goes back on with E (offered up, its fixings started by hand), then each
fixing is done up. Doors, bonnet and boot lid have to be open to reach their hinge bolts; wings and
headlamps need the bonnet open, tail lamps the boot; parts behind a wheel need the wheel off.

Running gear: with the boot open, E takes the jack, the spare wheel or a spare suspension part from the
boot kit. E with the jack at a wheel puts it under that corner and lifts it; E with empty hands there
lowers it again. A wheel has four 17 mm bolts; struts, arms, discs and driveshafts two or three each (13,
17 and 19 mm, a screw on the disc). B with the hammer knocks a bent arm roughly straight. Looking at a part
says what is wrong with it ("bent 40%, the wheel points out").

Engine bay, with the bonnet open: look at the radiator for the coolant level, B there seals a leak from
the tin of sealant, B at the battery with the 10 mm spanner bodges its terminals, and the boot kit's water
can is poured in with E at the radiator. In the driver's seat, I switches the engine off or tries to start it; taking the seat
turns the key, and the engine stops when the driver gets out.

Engine modules, with the bonnet open: the engine cover, air filter box, intake and exhaust manifolds,
turbo, intercooler, alternator, starter and engine computer come off the same way, fixing by fixing, in
order (cover and air box before the turbo, the turbo before the exhaust manifold, the front bumper before
the intercooler). DAMAGE.md, Engine modules, lists what each one does.

Grades and settings: parts come worn, stock, sport or rally. T and Shift+T set what the tool in hand
adjusts on the part in view: a rally strut's damper (screwdriver) or height collar (19 mm), the turbo's
boost at the wastegate (10 mm). See DAMAGE.md, Grades and settings.

Under the car, with that end on the jack: B at a cracked sump (from in front) or a holed tank (from the
left) patches it with putty, B at a damaged gearbox (from in front, 10 mm spanner) frees its linkage; behind the car, B
wires up a hanging exhaust with a strap (rear jacked) and R pulls it off (no jack needed).

A dot in the middle of the screen shows where you point while on foot. Actions act on exactly what it is
on: the part whose collision hull (the shape it falls off with) the view ray reaches first, within 2.3 m
of the eyes, or a small spot for places without a mesh of their own (radiator filler, battery, dipstick;
under the car the sump, gearbox and tank). The dot turns yellow and the prompt says what can be done;
on a part with nothing to do, the prompt just names it. Under-car spots only count while crouched, and
then before the bumpers and doors in front of them.

Hold Ctrl (or C) to crouch: eyes about a metre off the ground, walking slowly. Hold Q to lean: forward by
default (up to 40 cm, bending down a little: over an open engine bay, into a wheel arch), or with A, D or S
held as well to the left, right (35 cm, with a slight tilt) or back. While Q is held the movement keys
steer the head and the feet stay put. The head stops short of walls but goes through the car's parts:
leaning past a wing or into a wheel arch is how a bolt behind it is seen and reached, and aiming looks
through a part the eyes are inside (its collision does not count from inside). The server allows for the
lean when it checks reach. `scripts\Test-Lean.ps1` crouches beside the front left wheel and leans in: the eyes
must get past the wing's outer face and still reach bolts (screenshots `RallyLean_straight.png` and
`RallyLean_leaning.png`).

The engine starts with the key: I in the driver's seat (the starter turns it over for a moment first),
and I again stops it. Getting out takes the key with you (docs/SOUND.md). The boot kit's oil bottle is
poured in with E at the engine's filler, bonnet open, where the dipstick shows the level.
See DAMAGE.md for damage, reach, timing and tool checks.
