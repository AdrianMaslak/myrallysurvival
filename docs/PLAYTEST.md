# Trying it out

A route through what is in the game now, about half an hour. Open the project in the editor (it opens the
countryside map, L_Countryside) and press Play. F1 shows the controls at any time.

1. **The first quest.** You start on foot in the service yard; the tracker is at the bottom left. Walk to
   the car, E to get in, I to start the engine. Follow the SS1 signs to the first stage's start.
2. **A stage.** Stop behind the start line. The scrutineer looks the car over, the entry fee (1 000 Sk)
   comes out of the kitty (top right), and the marshal counts down. Leaving early costs 10 s. Drive to the
   flying finish: the clock and the next pace notes are at the top (with a friend in the co-driver's seat,
   the notes are theirs to read aloud). At the finish: your place against six rival crews, and prize money.
   Before a stage, N at the line drives it as a recce at 50 km/h to learn the notes.
3. **Break something and fix it.** Hit something (or F8 in the driver's seat flattens the front left tyre).
   Get out, open the boot (F), take the jack (E) to a wheel (E puts it under). Pick a tool with 1-8 or the
   mouse wheel; aim at a wheel bolt and hold R to undo it with the 17 mm spanner. With all four out, R
   lifts the wheel off. The spare from the boot goes on with E, then each bolt is held home with E. A bolt
   left out loosens the wheel, which wobbles and falls off once the others work loose.
4. **The engine bay.** Open the bonnet (F). The engine cover (screwdriver), the air filter box (by hand,
   tool 1), the turbo (13 mm) come off in that order; without the turbo the engine has two thirds of its
   power. T and Shift+T with the 10 mm on the turbo set the boost (too much on a stock chip knocks).
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
