# Days and nights

A day passes in 45 real minutes (GAME-PLAN.md: a game day of about 45 minutes, sleep to skip). The game
starts at 09:00 on day 1; the time is in the top right corner.

- **Sun** (`ARallyDayClock`): rises at 06:00, is highest (60 degrees) at noon and sets at 18:00, moving
  the map's movable sun (the countryside has one; the lab's sun stays put). Below the horizon the sky goes
  dark with it (the map's sky atmosphere and real-time sky light follow the sun), and each machine adds a
  faint, low moon of its own so the ground is not black.
- **Headlamps**: L in the driver's seat. Each lamp lights only while its lamp unit is on the car and the
  battery works and has charge (a flat battery from a missing alternator puts them out).
- **Sleeping**: Z in a seat (driver or co-driver; the screen goes dark, Z again wakes). When every player in
  the world is asleep, the clock goes on to 06:30 the next morning (or this morning, before 06:30) and the
  team is saved (docs/SAVING.md). Getting out of the seat wakes you. A bed at home comes with the home map.
- The clock's day and hour are saved and loaded with the team.
- **Test**: `scripts\Test-Day.ps1` (countryside map): 21:30, the driver walks to the car and gets in, the
  headlamps go on (screenshot `RallyDay_night.png`), everyone sleeps, and it must be day 2, 06:30
  (`RallyDay_morning.png`).

Not yet: the cold at night, tiredness (dropped from the plan), a wristwatch instead of the corner clock,
dashboard and tail lights, rain and fog.
