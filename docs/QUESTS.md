# Quest plan: from sandbox to a first rally day

*The rally weekend in detail. The whole game's plan, and where this fits in it (milestone 2), is
`GAME-PLAN.md`; its decisions answer the questions at the end.*

The car, its damage and the field repairs are far along; the game around them is not. This plan moves
the prototype towards the concept's Phase 1 vertical slice (one stage, two players, crash, repair,
continue) and into Phase 2 (a rally day with service), as a chain of six quests on the countryside map.
Each quest is something a player does, and the reason to build what it needs. Order matters: each one
uses what the ones before it built.

The map already has what the quests need as places: the Lipova service yard (where players spawn), three
special stages with start and finish gates and their routes (SS1 Lipova Village, 1.3 km asphalt; SS2 Pine
Ridge, 1.5 km gravel, narrow, through forest; SS3 Fields of Hlinik, 1.7 km mixed), road sections from the
yard to each start, and signs. 4.5 km of stages in all, the concept's "one 3-5 km stage" split in three.

## Built so far (October 13, 2026)

A first day's chain runs on the countryside map (`ARallyQuestDirector`), with a tracker at the bottom left
and its step saved with the team: Shakedown (find the car, start it, drive to the SS1 start), SS1, back to
the service yard, SS2, order a sport or rally part from the catalogue, SS3, and sleeping through to the next
morning. Each objective is checked from what the game already knows; the chain is code, not data, for now.
What the quests need is built: stage timing with pace notes and rivals (STAGES.md), the scrutineer and entry
fees, money and the catalogue (MONEY.md), the service yard's workshop jobs, saving (SAVING.md), the day and
night (DAYS.md), and the tow (MONEY.md). Not yet: the warning triangle, the boot as an inventory with
weight, recce, the day's total.

The opening (2026-10-19, `scripts\Test-Start.ps1` photographs it): the intro letter alone in the middle,
until Enter puts it away (letters wait while the car moves or a stage runs, so they never cover the road
or the pace notes); the quest tracker bottom left; and through the Shakedown, one line of keys above it for
what the player is doing now: walking ("WASD walk, mouse look, E: use, pick up, get in"), in the seat with
the engine off ("I: start the engine"), then driving (throttle, brake, steering, handbrake, gears,
clutch). F1 brings up the full controls card, centred over everything. What is under the crosshair (yellow:
what can be done; white: what it is), the tool in hand and the driver's gauges are drawn by the HUD in
every build; they used to be development-only debug text.

## The quest system (built with quest 1)

- Quests are data: an ordered list of objectives, each with a kind (reach a place, start or finish a
  stage, repair a part, keep something above a level, return to service before a time) and its
  parameters. Text and checks live together so new quests are cheap.
- The server owns quest state (current quest, objective, timers) and replicates it with the game state;
  the whole team shares it. Late joiners get it like everything else.
- A small tracker on the screen: quest name, current objective, a distance or time where it matters. No
  map markers: the signs, the road book and the co-driver point the way (pillar: diagnose by looking).
- Tests: each quest gets a scripted run (like the crash and panel tests) that plays it start to finish.

## Quest 1: Shakedown

*"Find the car, start it, take it to the start of SS1."*

Players spawn at the service yard on foot. The car stands in the yard, engine off.

Build:
- The quest system, tracker and first objectives (reach the car, start the engine with I, reach a place).
- Gates as places the game knows: START and FINISH of each stage from the map's data, with a trigger and
  a board. The road section from the yard to SS1 start as a sequence of points to follow.
- A short intro card with the controls that matter (E, F, R, B, Q, Ctrl, I, G).

Done when: a player can go from spawning to the SS1 start line; the scripted run does it alone and with
two players.

## Quest 2: SS1 Lipova Village

*"Run the stage against the clock."*

Build:
- Stage timing: a start with a countdown from the line (a jump start is a 10 s penalty), a flying finish,
  the time on the tracker and on a board at the finish. Time keeps running whatever happens on the stage.
- A maximum time (the stage closes: "retired from the stage", a fixed penalty time) so a car that cannot
  go on still has an outcome.
- Results: stage time, penalties, the team's total so far. Replicated: both players see the same clock.

Done when: a timed run of SS1 gives the same time on both machines, a jump start costs 10 s, and a car
left on the stage gets the maximum time.

## Quest 3: Read the road (SS2 Pine Ridge)

*"Narrow gravel through the trees. Listen to your co-driver."*

Build:
- Pace notes generated from the route: corners graded 1 (hairpin) to 6 (flat) from their radius, with
  direction, "tightens" and "opens", crests and "don't cut" where trees line the inside, and distances
  between calls ("100 left 4 tightens").
- The co-driver's road book: a page in the co-driver seat showing the next calls as the car moves, for
  the player there to read aloud. Driving alone, the calls come by themselves (on screen at first, spoken
  later).
- A recce option: drive the stage slowly first to see the notes, as real crews do.

Done when: the calls for SS2 match its corners (a test checks every corner over 30 degrees has a call
before it), and a solo driver gets them in time to brake.

## Quest 4: Limp to the finish (SS3 Fields of Hlinik)

*"Something breaks on the stage. Fix it on the roadside and still make the finish."*

The first quest where the damage system is the point. A hard stage with a hidden rock and a tight
corner by a ditch; the quest does not force a breakdown, but expects one and checks how it ends.

Build:
- Objectives about the car: "finish with the engine alive", "finish on four wheels", tracked from the part
  conditions that already exist.
- Roadside work while the stage clock runs: everything built so far (jack, spares, sealant, putty, water,
  oil), now with a reason to hurry. A warning triangle to put out (another car could come).
- A tow out as the last resort: a time penalty and the car back at the service yard as it was.

Done when: the scripted run punctures a tyre or cracks the sump mid-stage, changes or patches it and
finishes inside the maximum time; and a seized engine ends in a tow, not a soft lock.

## Quest 5: Service

*"Twenty minutes in the service yard. Fix what you can, buy what you need, pack the boot."*

Build:
- A service period: a clock that only runs in the yard, and a late-arrival penalty (as in real time
  control).
- Proper repairs, which only the yard allows: replacing a seized engine, a broken gearbox or a crushed
  radiator, straightening the body, each taking service minutes.
- Money and a parts counter: stage results pay, spares cost.
- The boot as an inventory with space and weight: what goes in (spare wheel, struts, water, oil, putty,
  sealant, straps, food later) is chosen, and its weight changes the car (heavier at the back).

Done when: a team can come in damaged, spend its minutes and money on the worst problems, leave with a
packed boot, and pay for being late.

## Quest 6: Day one

*"SS1, service, SS2, SS3. Damage carries over. Finish the day."*

Build:
- The day as one run: the quests in order with road sections between them, a day result (total time,
  penalties, money) and a board in the yard.
- Damage, fluids and the boot carry over between stages and across a save: the car's state is saved and
  restored (part conditions, dents, fluids, boot contents).
- Light and time of day: SS3 runs at dusk, so the headlamps (and the battery) start to matter.

Done when: two players can play the whole day start to finish in one session, quit, and carry on from
the same state.

## Later (not in these six)

- Night stages, rain and fog; fatigue, food and water, cold (concept: survival layer).
- The service van and the scout (Phase 3), the CB radio.
- Rivals on the same stages (open servers), village scrapyards, a second car.
- Recorded engine and road sounds, spoken pace notes.

## Questions to settle before starting

1. Should the quests be a fixed tutorial-like chain (as above), or should stages be free to run in any
   order with quests only for the first time through?
2. Money: start broke with a small budget (the concept's privateer) or with enough to learn first?
3. Is a tow (big time penalty, car fixed) acceptable for this first day, before the service van exists?
