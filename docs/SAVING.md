# Saving the team

The team's state is saved at rally milestones and loaded when the game starts (GAME-PLAN.md: saved when
sleeping at home and at rally milestones; the home and the bed come with milestone 3). It lives in the
host's save (`Saved/SaveGames/RallyTeam.sav`); friends who join see the host's world.

## What is saved (Game/RallySaveGame.h)

- The car: where it stands, every part's condition and whether it was bodged.
- Every slot of `URallyPanelComponent`: hinge and latch health, straps, fixings out, grade and settings,
  how crooked a refitted panel sits, open, jammed or removed; the jack's corner.
- The body's dents.
- The engine: temperature, coolant, oil, fuel, battery charge.
- Every part of the car lying about, carried (saved at the carrier's feet) or in its boot: what it is, its
  grade, condition, dents, and where it lies or rides; a part that came off its place keeps its link to it.
- Stage results and the attempt count.
- The day and the hour (docs/DAYS.md).

Not yet: the players' own kit (straps, sealant, putty, the tool in hand), money.

## When

- At the end of a special stage (finished or retired): `ARallyStageDirector` asks the game mode.
- When everyone has slept through the night in the car (docs/DAYS.md).
- When the game starts, if there is a save made on this map.
- By hand in development: console `RallySave`, `RallyLoad`.
- Automated runs (`-unattended`) neither load nor save, so tests never touch the player's save, except the
  save test with its own slot (`-RallySaveSlot=`).

## Test

`scripts\Test-Save.ps1` runs the lab twice. The first run damages the radiator and turbo, dents the front,
takes the front bumper off (left lying beside the car), leaves a wheel bolt out, fits a rally strut's
settings and the turbo's boost, drains coolant and moves the car, then saves. The second loads it at the
start and checks each of those, the bumper's link to its place and the stowed boot kit.
