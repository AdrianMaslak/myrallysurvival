# Special stages

The countryside map's three special stages are timed (milestone 2 of GAME-PLAN.md, quests 2 and 3 of
QUESTS.md). `ARallyStageDirector` runs them on the server; every machine reads its replicated run and works
out the clock, the pace notes and the rivals' times itself.

## Play

1. Drive to a stage's start gate (SS1, SS2 or SS3 signs from the service yard). Near it the screen names the
   stage, its length and surface.
2. Stop behind the line, on the road, with someone in the driver's seat. The scrutineer looks the car over
   (regional cup rules): four wheels on with every bolt done up, the running gear all there and bolted, no
   fuel leaking, doors, bonnet and boot lid shut. What fails is shown at the line, and the car is not
   started until it is put right. Then the marshal counts down from 5. Leaving before zero is a jump start:
   10 s penalty (the clock still starts at zero).
3. The clock runs to the flying finish. The pace notes are the co-driver's: a page of the road book on their
   screen (the next four calls within 400 m and the distance done), to read aloud to the driver. A driver
   alone in the car sees the next two calls instead ("120  left 4 tightens").
4. Or, at the line (before or during the countdown, which is then called off and the fee given back), N in
   the driver's seat starts a **recce**: the stage driven untimed at no more than
   50 km/h (the line goes red above it), the pace notes shown to the driver too, no fee and no result; it
   ends at the finish line, or if the car goes far from the stage.
5. At the finish the stage's results show for 15 s, and from the second stage on the rally so far beside
   them: each crew's times added up over the stages the team has run (its latest go at each, a retirement
   counting the stage's maximum time). At the finish: the team and six rival crews, fastest first. A stage
   still running at its maximum time (2.5 times its reference time plus a minute) closes, and the car is
   retired from it with that time.

6. **Road sections**: after a stage the next one's start (SS1, then SS2, then SS3) is a time control, due
   2 hours of the day later (about four real minutes). The deadline shows while no stage is running. Every
   minute late when counted down there costs 10 s, up to 5 minutes; workshop jobs and a tow eat into it.

7. **The regional cup**: once all three stages of an event have been run, the event's overall standings give
   cup points (10, 8, 6, 5, 4, 3, 2 for the seven crews), a letter tells the result, and the next event starts
   on the same stages (its results count afresh). J shows the cup's standings. Saved with the team.

8. **Calendar** (`Game/CalendarRules.h`): day 1 is Saturday 6 April 1996. A rally weekend comes every
   second weekend (days 1-2, 15-16, 29-30 ...); the weekdays between are for work and the car. Stages can
   be started only on a rally weekend (the marshal at the line says when the next is; a recce may be
   driven any day). The cup has twelve events, the last in early September. Then the off-season: the
   final table stays on J, and the next season (52 weeks after the last began, Saturday 5 April 1997 for
   the second) starts a new cup; sleeping till the rally weekend (Shift+Z) passes the winter in one go. The top right shows the date, and this weekend's event or
   the next rally's date. When a rally weekend's Sunday has passed, its event is closed: stages not run
   count as retirements, and a crew retired from any stage of an event (or not started) takes no points
   from it. The rivals score as usual.

## Rules (Game/StageRules.h)

- Reference time: the stage's length at 21 m/s on asphalt, 17 on gravel, 19 mixed (SS1 61 s, SS2 91 s, SS3
  91 s).
- Pace notes: from the route's curvature (heading change over about 9 m each side). A run of samples
  turning the same way tighter than 200 m radius is a corner; under 15 degrees in all it is not called.
  Grade from its tightest radius: 1 under 20 m (a hairpin if it turns more than 110 degrees), 2 under 32, 3
  under 50, 4 under 75, 5 under 110, else 6. It tightens or opens when one half is 1.4 times as tight as the
  other; "into" when the next corner starts within 30 m.
- Rivals: six crews with a pace against the reference time (0.90 for the works team to 1.20) and a chance of
  going off (3 to 10%), drawn from a seed of stage, attempt and crew, so every machine agrees. Each start
  is a new attempt with new draws.

## Data

`scripts/export_stage_routes.py` writes `Private/Game/CountrysideStages.inl` from the map's
`Art/Maps/Countryside/Export/countryside.json`: each stage's id, name, surface, width and centre line (about
every 3 m). Run it again whenever the map's routes change. The director is spawned by the game mode on maps
whose name contains "Countryside". Where a car is along a route is searched near where it was last (stages
double back past themselves, and a car off the road must not jump to the other leg).

## Test

`scripts\Test-Stage.ps1 [-Stage SS2] [-JumpStart]` loads the countryside map with the car 9 m behind that
stage's start line; the driver walks to it, the countdown runs, and a stage drive (`-RallyStageDrive`) follows
the route: it steers at a point 8 to 24 m ahead, compensating the steering's fall-off with speed, and keeps
to the speed the bends in the next 90 m allow (grip 6 m/s^2 on asphalt, 4.5 loose), braking in time. It
passes when the stage is finished; with `-JumpStart` it leaves with four seconds of the countdown left and
must carry the 10 s penalty; with `-LooseBolt` a front wheel bolt is left out and the scrutineer must
refuse to start the car; with `-Recce` it drives a recce at 50 km/h and must finish it without a result;
with `-WithPrior` an SS2 result is already recorded and the overall standings must cover two stages; with
`-Late` SS1's time control was due before the game began and the stage must carry the lateness penalty; with
`-CupEvent` SS2 and SS3 are already run, so SS1 completes the cup event and its points must be given.
Screenshots: `RallyStage_running.png`, `RallyStage_results.png`. First runs:
SS1 61.2 s, SS2 87.7 s, SS3 87.2 s; the jump start 67.1 s with its 10 s.

## Not yet

- The road book as a paper item in the co-driver's hands, spoken calls, the crew's own marks from a recce.
- Stricter scrutineering at national and international level (cage, belts, extinguisher, legal parts).
- Service, scrutineering, road sections and time controls between stages, the day's total.
- Rivals on the stage itself, other teams (open servers are dropped for now).

## Highlands map

`L_Highlands` uses its own generated 6 km mud, 7 km gravel and 7 km tarmac routes.
`scripts/highlands_layout.py` exports `HighlandsStages.inl`. The stage director selects
these routes when this map starts. See `HIGHLANDS.md` for authoring and tests.
Stage distances include elevation; pace-note distances use the same 3D centre line.
Wet mud uses a 12 m/s reference pace, slower than gravel's 17 m/s.

## Kysuce map

`L_Kysuce` runs three stages on a real road (36 km of stages, 12% at most), with split times and a service
park between each pair of stages. `scripts/kysuce_layout.py` exports `KysuceStages.inl`. See `KYSUCE.md`.
