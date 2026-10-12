# Rally Survival Co-op – Game Concept

Oct 10, 2026 · @Adrian

## Pitch

**You and your friends enter a brutal cross-country rally, and between stages you have to keep both the car and yourselves alive.**

A co-op rally game with My Summer Car-style hands-on mechanics. Every crash is your problem: there is no repair menu, no instant reset. If you break the car in the middle of a forest stage, you climb out, check the damage and fix it with whatever is in the trunk, or walk for help.

Working title: TBD. Platform: PC first. Players: single player, or 2–4 in co-op with friends or on open servers. Difficulty: sim-ish, close to My Summer Car.

## Pillars

Every feature should support at least one of these. If it doesn't, cut it.

1. **Every crash is a story.** Damage is never just a time penalty. It creates a situation the players have to solve together.
2. **Hands-on, but not tedious.** Repairs are physical and satisfying, but there are tens of parts, not hundreds of bolts.
3. **Risk vs. speed.** Pushing hard wins time but risks the car, the spares and the night ahead.
4. **Better with friends.** Driver and co-driver roles, shared repairs and shared bad decisions make the funniest moments.

## Core loop

Drive fast, break something, fix it yourselves, keep going. The highlighted box is the heart of the game.

&#91;embedded content: core loop · drive, crash, repair, continue\]

A quick bodge gets you back on the road sooner but may fail later; a car you can't fix means walking, waiting or a tow with a time penalty.

## Teams and roles

You play a whole rally team, not just a car. The core is 2 players in the rally car (driver and co-driver). Up to 2 more players can join in optional support roles: the service van and the scout. Single player is fully supported. Roles are fully open: anyone can switch jobs at any time.

| Role        | Type     | What they do                                                                                                           | Why it matters                             |
| ----------- | -------- | ---------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ |
| Driver      | Core     | Drives the special stages and road sections                                                                            | Speed vs. risk to the car                  |
| Co-driver   | Core     | Pace notes, roadbook, time control; helps with field repairs                                                           | Without good calls the driver crashes more |
| Service van | Optional | Drives the van with heavy spares and tools on public roads; big repairs at service parks; tries to reach stranded cars | The only way to do proper fixes mid-rally  |
| Scout       | Optional | Fast motorbike or old hatchback; scouts roads and weather ahead, fetches parts from villages and scrapyards            | Early warnings and emergency parts         |

- **CB radio with limited range.** No mobile phones. A car stuck deep in a forest can't reach the van; someone drives or walks to higher ground, or the crew has to guess. This makes support players essential.
- **Service van can't drive stages.** It uses public roads and can only meet the car at service parks or at road crossings near the stage.
- **Single player:** audio pace notes replace the co-driver, and the service crew is an AI van that only meets you at service parks.
- **Voice:** proximity voice outside vehicles, intercom inside, CB between vehicles.

**Open servers: several teams in one rally.** A server hosts 3–5 teams in the same event, competing on time but sharing roads, villages and service parks. Lone players join any team that needs a co-driver or mechanic; private friend lobbies stay the default.

- Rivals can stop to help after a crash: trade parts, tow each other, and favours are remembered.
- Teams compete for limited parts in village scrapyards.
- Basic protection: kick and vote-kick, and only team members can take parts from your car or van.

## Damage and repair system

Aim for about 20 serviceable components, each with its own way to break and its own symptom, so players learn to diagnose.

| Component               | How it breaks            | Symptom                          | Field fix                                             |
| ----------------------- | ------------------------ | -------------------------------- | ----------------------------------------------------- |
| Tyres and wheels        | Puncture, bent rim       | Pulls to one side, vibration     | Swap spare; bent rim can be hammered roughly straight |
| Suspension arm / shock  | Big landing, side impact | Car leans, bottoms out           | Replace from spares, or strap it and drive slowly     |
| Radiator                | Front impact, stones     | Temperature climbs, steam        | Patch with sealant, top up water from a stream        |
| Oil sump                | Hitting rocks            | Oil light, engine noise          | Patch, refill oil; ignore it and the engine seizes    |
| Steering rack / tie rod | Wheel impact             | Steering off-centre or loose     | Replace tie rod, re-align by eye                      |
| Gearbox                 | Abuse, missed shifts     | Gears grind or pop out           | Limp on remaining gears; real fix only at service     |
| Electrics / lights      | Water, impact            | Lights flicker or die            | Swap fuse, reconnect wire; matters at night           |
| Body and glass          | Rolls, trees             | Doors jammed, cracked windscreen | Mostly cosmetic; jammed doors slow you down           |

**Repair quality.** Every repair is either a _proper fix_ (needs the right part and tool, takes longer) or a _bodge_ (tape, wire, straps; fast but may fail later on the stage). This is the main risk-vs-speed choice off the road.

**Physical interaction.** Players pick up tools, loosen a few key bolts and swap the part. Keep it to 2–4 actions per repair, not every bolt.

**Damage carries over.** Wear and damage stay between stages until fixed at a service park, so a careless early stage haunts you later.

## Survival layer

Survival exists to raise the stakes of breaking down, not to be a separate game. Keep it light: a handful of needs that matter most when you're stranded.

- **Fatigue.** Long days at the wheel blur vision and slow reactions. Sleep at camp or in the car.
- **Hunger and thirst.** Simple, slow-draining. Food and water also compete for trunk space with spares.
- **Cold and weather.** Rain, fog and night make driving harder and repairs slower. Being outside the car at night in the cold is dangerous.
- **Injury.** Hard crashes can injure players: blurred vision or a slow walk until treated with the first-aid kit.
- **Resources.** Fuel, water for the radiator, oil, spares, tape. All limited, all weighed against car performance.

**When you can't fix it:** walk to a village for parts, signal the support crew, spend the night and try at daylight, or call a tow and accept a big time penalty. Retiring from the rally is the true fail state.

## Setting and progression

**Setting.** A fictional multi-day endurance rally through Central or Eastern European forests and mountains in the 1990s. Old cars are simple to fix, there's no GPS, villages are sparse, and it suits the gritty, slightly absurd mood of My Summer Car. Desert or winter rallies could come later as extra events.

**Story.** You're a small privateer team in the mid-90s with a borrowed car and not enough money, entering a gruelling multi-day rally across the region. Light story beats come from sponsors, a rival works team, debts and local characters: world flavour like My Summer Car, not a scripted campaign.

**Structure of one rally.**

1. Day starts at a service park: full repairs, buy parts, pack the trunk.
2. 2–4 special stages per day, timed, with road sections between them.
3. Night at a bivouac or wherever you got stuck.
4. Final results after 3–5 days.

**Progression.**

- Prize money and sponsors buy better parts, tools and eventually better cars.
- Players learn the car and the stages; most progression should be player skill, not stats.
- Unlock harder rallies with longer stages, worse weather and fewer service parks.

## Scope: first prototype

The first goal is a vertical slice that proves the core loop is fun: **one car, one stage, two players, three types of repair.** Everything else waits.

**In the prototype**

- [ ] One car with believable gravel handling
- [ ] One 3–5 km stage with a start, finish and timer
- [ ] Damage to tyres, suspension and radiator, with symptoms
- [ ] Climb out, pick up tools, do a fix or a bodge
- [ ] 2-player co-op from day one, driver plus co-driver
- [ ] Simple pace notes the co-driver can read out

**Not in the prototype:** survival needs, night, weather, economy, support van, multiple cars.

**Tech direction.** Engine not chosen yet. Before committing, build a tiny driving test in two candidates (for example Unity and Godot, or Unreal) and compare gravel handling and networked car sync. Use an existing vehicle physics base and networking library rather than writing them from scratch. Open servers mean planning for dedicated or player-hosted servers early. Build multiplayer in from the start: retrofitting it to a physics game is very hard.

**Decided**

- Setting: 1990s Central/Eastern European endurance rally
- Difficulty: sim-ish, like My Summer Car
- Roles: fully open, anyone does anything
- Modes: single player, private co-op, open servers

**Still open**

- Which engine: decide after the driving and networking test
- Max teams per open server, and whether the parts runner role is worth building
- Working title

## Build order

Co-op comes first, built so single player is simply co-op hosted for one person. Networking is in the foundation from day one; retrofitting it later would mean rewriting cars, damage, parts and inventory. Each phase ends with a clear test before the next one starts.

### Phase 0: Engine and tech test

**Goal:** choose the engine with evidence, not guesses.

- Build the same tiny test in two candidate engines: one car on a gravel road, two players connected, both seeing the car move smoothly.
- Compare how the car feels on gravel, how hard networked car sync is, and how comfortable you are working in each engine.
- Pick a vehicle physics base and a networking library rather than writing either from scratch.

**Done when:** one engine is chosen and two players can drive or ride in the same car without obvious lag or jitter.

### Phase 1: Co-op prototype (the vertical slice)

**Goal:** prove the core loop is fun: drive, crash, repair, continue.

- One 90s rally car with sim-ish gravel handling.
- One 3–5 km stage with start, finish and timer.
- Damage to tyres, suspension and radiator, each with a clear symptom (pulling, leaning, rising temperature).
- Players can climb out, pick up tools and do a proper fix or a quick bodge, in 2–4 actions per repair.
- Driver and co-driver, both able to do anything; simple pace notes the co-driver reads aloud.
- Test it solo as well, to make sure one player can finish the stage alone.

**Done when:** friends who play it want another run, and crashing feels like an event, not just a penalty.

### Phase 2: Expand the co-op rally

**Goal:** turn one stage into a real multi-day rally.

- The full damage list (about 20 components) with carry-over damage between stages.
- Trunk inventory: spares, tools, food, water and fuel, all competing for space and weight.
- Service parks between stages for full repairs and buying parts.
- Several stages across 3–5 days, with road sections between them.
- Night, rain and fog, plus the light survival layer: fatigue, hunger, thirst, cold, injuries.
- Ways out when the car can't be fixed: walking to a village, waiting for daylight, or a tow with a time penalty.

**Done when:** a 2-player team can play a full rally start to finish, and the risk-vs-speed choices matter.

### Phase 3: Optional support roles

**Goal:** let players 3 and 4 join without the game depending on them.

- Service van: drives public roads with heavy spares, meets the car at service parks and road crossings near stages, can tow.
- Scout: a fast motorbike or small car that checks roads and weather ahead and fetches parts from villages and scrapyards.
- CB radio with limited range, so reaching the stranded car takes effort.
- Enough to do while the car is on a stage: route planning, buying parts, preparing repairs, scouting the next stage.

**Done when:** support players have fun on their own, not just waiting for the car.

### Phase 4: Single player extras

**Goal:** make solo a full experience, not just an empty co-op game.

- Recorded co-driver pace notes so the solo driver isn't driving blind.
- An AI service van that meets the player at service parks.
- Tuning the difficulty of repairs and survival for one person.

**Done when:** a solo player can finish a full rally and enjoy it.

### Phase 5: Story, career and open servers

**Goal:** give the game long-term life.

- Career: the broke privateer team in the mid-90s, prize money, sponsors, debts, better parts and cars over time.
- Story flavour: a rival works team, local characters, villages with their own quirks.
- Harder rallies with longer stages, worse weather and fewer service parks.
- Open servers with 3–5 teams in one rally, joining teams that need players, rivals helping each other, and basic protection (kick, vote-kick, only team members can take your parts).

**Done when:** players have a reason to come back after their first finished rally.
