# Trying it out

A route through what is in the game now, about half an hour. Open the project in the editor (it opens the
countryside map, L_Countryside) and press Play. F1 shows the controls at any time.

**New since the 2026-10-18 playtest** (worth trying first):

- **4x4:** try a gravel launch, handbrake turn and limping after one front driveshaft breaks; a broken rear shaft should leave front drive.

- **Lie down under the car:** jack up a front corner, press X, then crawl underneath with WASD.
  Look up and use the usual tools and E/R controls. Press X again to get up; if there is no
  clearance, keep crawling out until you can stand. Space also requests getting up.
- **Fuel can:** the boot kit now has a 20 L fuel can (it looks like the water can for now). Carry it to the
  fuel flap on the right rear wing (look at the wing, it says "fuel filler: 23 of 55 L") and press E to pour
  it in; no bonnet or jack needed. In the service yard P at the flap fills the tank, and P with the can in
  hand fills the can (30 Sk a litre). An old save gets a can on loading.
- **New on 20 October: the engine runs on its own, connected, with coils to swap.**
  - Get out with the engine running and it idles on (out of gear, handbrake on). At the driver's door, I
    starts or stops it from outside. With the bonnet up, look at the throttle at the end of the intake
    from beside the right wing and hold E: it revs to about 4000 and rocks on its mounts.
  - The front of the bay is the model's: the radiator with its two fans (they spin when the engine gets
    hot), the hoses, the side-mount intercooler low in the left corner with its pipes from the turbo and up
    to the throttle (all connected now), and the coolant tank by the right strut, where water goes in.
  - Four ignition coils under the engine cover, each held by its connector's clip. `RallyCoil 2 0` in the
    console kills one: the engine shakes, pops and idles lower. Find it: engine idling, cover off, unclip
    the connectors one at a time (R) - the idle drops on a good coil, not on the dead one. Pull it (R),
    fit a new one (catalogue: 450 Sk, or 900 for a red sport coil that holds more boost) and clip it (E).
    Stock coils misfire flat out past 1.1 bar of boost.
  - The accessory belt runs and the alternator, power-steering and air-conditioning pulleys turn with the
    crank (look from beside the right wing with the bonnet up).
  - The suspension is the new model's: real struts, springs, arms, calipers and discs, steel wheels with
    vent holes and treaded tyres, the 4x4 rear with its driveshafts and final drive, and every real bolt (a
    wheel has five now). Take a wheel off and look: the brake's bolts, the strut's clamp, the arm's ball
    joint. The car body has not been reshaped for the 4x4 rear yet, so from underneath the rear subframe
    and arms show through the floor (the new body will fix it).
- **New on the night of 19-20 October: the engine by hand.** The bay holds the 1.8T 20V model, and it
  rocks on its mounts as it starts, revs and stops (a misfire jolts it). Every bolt that holds a part on
  is the real one at the real count (the exhaust manifold's thirteen nuts, the intake's eleven); a part
  held by many goes quicker per bolt, so each part still takes a few seconds. Tools are sockets
  (10/13/17/19 mm), the screwdriver and hands. Hold R on a bolt and watch it spin out along its thread.
  The part you look at shows its bolts faintly through whatever is in front, so you can find the ones
  round the back of the turbo. With the right socket in hand, the extension reaches a bolt behind a pipe.
  The starter's two bolts are done from underneath: jack up the front and crouch at the bumper.
- **New on 19 October (day):** Q leans through the car's parts (into a wheel arch, past a wing) to see
  and reach a bolt. The boot's kit items each have their own place: with the lid open an empty place
  shows a faint cyan outline (brighter for what you carry), E or G looking into the boot straps a part
  back into its place (the spare wheel into its well), G looking away drops it. The engine bay has
  placeholder colours and grime until real models arrive.
- **Leaks show**: a cracked sump, holed radiator or tank drips oil (black), coolant (green) or fuel onto
  the ground; parked, a puddle grows under the car, driving leaves a trail of spots, and the engine's lower
  block goes black with oil. `RallyLeak all` in the console breaks all three to try it.
- **Gears by hand.** Left Shift / Left Ctrl shift up and down (RB / LB), reverse below neutral and only
  nearly stopped; Q is the clutch (B), never required. Clutch in, full throttle, let go: a launch. S only
  brakes. A downshift that would over-rev the engine crunches and does not go in. Traction control is
  off, so the engine revs in a slide and the fronts spin if you floor it in first.
- **An engine in trouble tells you.** Turn the boost past 0.9 bar on the stock chip and it pinks under
  load (a metallic ticking; the wastegate prompt warns you); wear shows as blue smoke from 90%. Leave a
  bolt out and it clinks loose on the move, the part rattles and sits crooked, and the last one clunks.
- **Fewer, clearer suspension parts.** Per corner: wheel, strut, arm, brake, and a driveshaft at the front
  (the tie rod is part of the arm). Damage shows (a bent arm kinks, a snapped one hangs, a damaged strut or
  disc darkens), and looking at a part says what is wrong with it.
- **Bolts you can find.** The part you look at lights its bolts amber and the one you would turn yellow;
  near a part the crosshair snaps to its nearest bolt. Wheel bolts sit on the hub face where you can see
  them, the driveshaft's hub nut in the middle of the hub; a door's hinge bolts inside it by its front
  edge (open the door and look from the doorway); the alternator's and engine computer's where they
  show from above with the bonnet up. Bolts can now be placed by hand on a mesh (sockets Fixing0, 1 ...;
  docs/DAMAGE.md) if one still sits wrong.
- **The boot's kit lies in the boot**, the spare wheel in its well under the floor.
- **The opening.** The letter shows alone until Enter (it waits while you drive), one line of keys for what
  you are doing replaces the wall of controls, and prompts, the tool in hand and the car's gauges are part
  of the HUD.
- Drunk, the view sways in the driver's seat too.
- **Rain looks and sounds like rain**: an overcast grey sky, streaks falling, a hiss outside and drumming on
  the roof from the seat (`RallyRain 1` in the console to try it).

1. **The first quest.** You start on foot in the service yard; the tracker is at the bottom left, and a
   line of keys above it says what to press. Walk to the car, E to get in, I to start the engine (it is in
   first gear; shift up with Left Shift). Follow the SS1 signs to the first stage's start.
2. **A stage.** Stop behind the start line. The scrutineer looks the car over, the entry fee (1 000 Sk)
   comes out of the kitty (top right), and the marshal counts down. Leaving early costs 10 s. Drive to the
   flying finish: the clock and the next pace notes are at the top (with a friend in the co-driver's seat,
   the notes are theirs to read aloud). At the finish: your place against six rival crews, and prize money.
   Before a stage, N at the line drives it as a recce at 50 km/h to learn the notes.
3. **Break something and fix it.** Hit something (or F8 in the driver's seat flattens the front left tyre).
   Get out, open the boot (F), take the jack (E) to a wheel (E puts it under). Pick a tool with 1-8 or the
   mouse wheel; aim at a wheel bolt and hold R to undo it with the 17 mm socket. With all four out, R
   lifts the wheel off. The spare from the boot goes on with E, then each bolt is held home with E. A bolt
   left out loosens the wheel, which wobbles and falls off once the others work loose.
4. **The engine bay.** Open the bonnet (F). The engine cover (screwdriver), the air filter box (by hand,
   tool 1), the turbo (17 mm, and 10 mm for its oil return) come off in that order; without the turbo the engine has two thirds of its
   power. T and Shift+T with the 10 mm on the turbo set the boost (past 0.9 bar on a stock chip it pinks
   under load and wears the engine; the prompt says so). Bolt a part back with one left out and drive: it
   clinks loose and the part hangs on its hoses.
5. **Parts and money.** K opens the catalogue (mouse wheel, Enter): a rally strut (adjustable with the
   screwdriver and the 19 mm, T and Shift+T) comes with the next morning's post to the service yard. Put
   parts in the open boot with E; the boot's weight is felt. In the service yard, P on a damaged part pays
   the workshop (a new radiator, a rebuilt engine...), which costs hours too. H calls a tow back.
6. **Night.** The day lasts 45 minutes. At dusk L switches the headlamps on; outside at night you get cold
   (the car's heater warms you). Z in a seat sleeps; when everyone sleeps the night passes and the game
   saves. A swig of slivovica (B with the bottle) warms you but makes you drunk; a cigarette calms cold hands.
   Some afternoons it rains (`RallyRain 1` in the console forces it): standing out in it soaks you, and wet,
   the cold comes sooner; the heater dries you.
7. **Crashes hurt.** A hard one leaves you hurt (darker, fringed sight, slower walk); the first-aid kit in the
   boot treats you or a team mate. Badly hurt, you wake in the service yard with a hospital bill.
8. **The calendar.** The date is in the top right: day 1 is Saturday 6 April 1996, a rally weekend. Stages
   start only on rally weekends (every second one); between them the marshal sends you away. Shift+Z in a
   seat sleeps through the weekdays to the next rally morning (everyone must choose it). Running all three
   stages of a weekend scores cup points (J shows the cup); a weekend missed or a stage retired from scores
   none.

9. **The cabin (October 20).** First rebuild the body's assets (docs/OCTAVIA-VEHICLE.md, Rebuilding the v2 body).
   In the driver's seat: I turns the key, and every warning lamp lights for two seconds, then oil, battery and belt
   stay until the engine runs; the needles rise with speed, revs, temperature and fuel. Your feet sit on the
   pedals and press them; look down and you see your legs (and lying under the car, your body). L: dipped beams,
   tail and plate lamps and the dials lit; B high beam; comma / full stop the indicators (they tick, flash front,
   side and rear, and cancel after a turn); X hazards; brake for the brake lamps, reverse for the reversing
   lamp; K the wipers, T the horn. Look at a switch and click: the light knob left of the wheel, the hazard
   button, the radio (its amber display), the stalks, the interior lamp, the hub for
   the horn. In the rain (`RallyRain 1`) drops gather on the windscreen until the wipers clear them. Check it at
   night too: the lamps' brightness is a first guess.

Development console (`~`): `RallyPart strut rally` (or rearstrut, disc, reardisc, turbo, ecu, intercooler, coil,
slivovica, cigarettes, firstaid; worn, stock, sport, rally) puts a part in front of you; `RallyCoil 3 0` kills
ignition coil 3 (0.3 makes it misfire now and then); `RallySave` and
`RallyLoad`; `RallyRain <0..1>` (or -1 for the weather's own). Seated or on foot (the car you are in or the nearest): `RallyPaint <r g b>` tries a
paint colour over the livery (0..1 each, `1 1 1` as painted; try `1.6 0.4 0.3` for a faded red), `RallyMud <0..1>`
cakes or cleans the car. F9 in the driver's seat repairs everything.

Two players: one hosts (Play as listen server), the other joins; the driver and co-driver seats, the road
book and sleeping all work across the network; the team's state is the host's.


## Cold engine and warm-up (2026-10-22)

Not yet compiled or run in Unreal. Test-Rules passes 281/281 part-condition, 76/76 vehicle and
61/61 network checks. Render-Sounds passes 60/60. Standalone thermal checks cover warm-up, thermostat, oil lag,
cooling, cold wear, seizure and gauge buffering. Start with Test-Idle, Test-Engine and Test-Day after
compiling. Their old warm-engine timing assumptions may need changing: the car now starts at ambient,
and the overheated engine cools much more slowly. Do not shorten the thermal model to fit old timeouts.

- New game at 09:00 (14 C air): turn I. Photograph the cold cabin gauge and white tailpipe vapour.
  Listen for fast idle and a light tick. The heater should not immediately clear cold or wet.
- Idle from 15 C: about 19m 14s to real coolant 85 C. Drive gently (about 20 kW average): about 6m 01s.
  Idle settles to 900 rpm by 40 C, vapour ends by 50 C. The needle reaches its buffered 90 at real 75 C.
  Oil still lags: wait for the tick to fade before hard revs. Heater rises from 40 to full at 80 C.
- Fresh cold engine: hold near 6500 rpm at high load. Hear the tick become a knock and feel torque
  drop before health falls. Continued abuse should seize in roughly a minute (53.1 s in the fixed
  90 kW rules test). Confirm immediate crank stop, one clunk, and only clicks on subsequent I attempts.
  The workshop engine rebuild must restore starting. A warm engine at normal revs must not take cold wear.
- Back off during the warning: knock fades in 30 s and damage already done remains. Compare warm and
  cold starts from inside and outside. Repeat with a client driving and a second listener.
- Stop a warm engine, save/load, and confirm coolant, oil and abuse persist. Leave it off for an hour:
  excess temperature should halve. Sleep overnight with it off: it should approach morning ambient.
  Test-Day must still pass morning/weekend skipping. A running engine does not cold-soak during sleep.
- Test-Engine: crushed-radiator leaks, boiling, wear and top-up must still work. Test-Idle: check the
  initial 1100 rpm, then normal idle and existing coil faults. Capture gauge/vapour screenshots before warming.
