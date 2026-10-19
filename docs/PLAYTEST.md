# Trying it out

A route through what is in the game now, about half an hour. Open the project in the editor (it opens the
countryside map, L_Countryside) and press Play. F1 shows the controls at any time.

**New since the 2026-10-18 playtest** (worth trying first):

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
  (the tie rod is part of the arm). Placeholder colours tell them apart (arms black, struts red, discs
  steel, driveshafts dark grey); damage shows (a bent arm kinks, a snapped one hangs, a damaged strut or
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
   tool 1), the turbo (13 mm) come off in that order; without the turbo the engine has two thirds of its
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

Development console (`~`): `RallyPart strut rally` (or rearstrut, disc, reardisc, turbo, ecu, intercooler,
slivovica, cigarettes, firstaid; worn, stock, sport, rally) puts a part in front of you; `RallySave` and
`RallyLoad`; `RallyRain <0..1>` (or -1 for the weather's own). F9 in the driver's seat repairs everything.

Two players: one hosts (Play as listen server), the other joins; the driver and co-driver seats, the road
book and sleeping all work across the network; the team's state is the host's.
