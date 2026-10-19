# Days and nights

A day passes in 45 real minutes (GAME-PLAN.md: a game day of about 45 minutes, sleep to skip). The game
starts at 09:00 on day 1, Saturday 6 April 1996; the date and time are in the top right corner, with
the next rally weekend under them (STAGES.md, calendar).

- **Sun** (`ARallyDayClock`): rises at 06:00, is highest (60 degrees) at noon and sets at 18:00, moving
  the map's movable sun (the countryside has one; the lab's sun stays put). Below the horizon the sky goes
  dark with it (the map's sky atmosphere and real-time sky light follow the sun), and each machine adds a
  faint, low moon of its own so the ground is not black.
- **Headlamps**: L in the driver's seat. Each lamp lights only while its lamp unit is on the car and the
  battery works and has charge (a flat battery from a missing alternator puts them out).
- **Sleeping**: Z in a seat (driver or co-driver; the screen goes dark, Z again wakes). When every player in
  the world is asleep, the clock goes on to 06:30 the next morning (or this morning, before 06:30) and the
  team is saved (docs/SAVING.md). Getting out of the seat wakes you. Shift+Z sleeps till the rally weekend instead: when every player sleeps
  so, the weekdays pass too and they wake at 06:30 on the next rally day (STAGES.md, calendar); if anyone
  chose Z, only the night passes. Loan payments due meanwhile are taken on waking. A bed at home comes with the home map.
- **Rain** (`Game/WeatherRules.h`): the first day is dry; after it two days in five bring a shower, starting
  between noon and 17:00 and lasting 1.5 to 3.5 hours, coming on and easing off over half an hour. It is
  worked out from the day and hour, so every machine agrees. The sky darkens (the sun up to 60% dimmer),
  the map's fog thickens (made movable for it), tyres grip up to 15% less, and players outside get wet
  (docs/BODY.md). It looks like rain (2026-10-19; it used to stay sunny and blue): the sky's light down to
  40% and hazier, the fog grey, the whole picture greyed (an unbound post-process volume: saturation down
  to 40%, a little darker), and up to 700 grey streaks falling at 9 m/s within 7 m of whoever is watching,
  none inside the car they sit in (`ARallyDayClock::UpdateRain`, on each machine for its own player). The clock in the corner says "rain" or "pouring". Development: `RallyRain 1` in the
  console forces a downpour, `RallyRain -1` gives the weather back.
- The clock's day and hour are saved and loaded with the team.
- **Test**: `scripts\Test-Day.ps1` (countryside map): a 14:00 downpour while the driver walks to the car
  (`RallyDay_rain.png` once in), then 21:30 and the headlamps go on (`RallyDay_night.png`), everyone
  sleeps, and it must be day 2, 06:30 (`RallyDay_morning.png`); then everyone sleeps till the rally weekend
  and it must be day 15, Saturday 20 April, 06:30, with the first weekend's cup event closed and no points
  for the team. Then the cold, the wet, the vices and the hospital (docs/BODY.md).

Not yet: a wristwatch instead of the corner clock, dashboard and tail lights, clouds, rain on the windscreen
and its sound, puddles, a bed at home.
