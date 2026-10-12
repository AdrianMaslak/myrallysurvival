# Destruction design (target)

What we want crashes to do. DAMAGE.md describes what is built today; this file is the goal it grows
towards. Decisions agreed 2026-10-11. Numbers marked "start" are first guesses for tuning, not balance.

## Decisions

1. **Believable rally sim.** Real dents from each crash, panels that hang and fall off, glass that cracks,
   wheels that bend. Not soft-body: the chassis stays one rigid body plus deformed meshes. Mechanical
   consequences come first; visual damage tells players where to look.
2. **Field-fixable, tow if bad.** Almost anything can be bodged with enough parts and time. A few failures
   (bent chassis, seized engine, broken gearbox) need the service van or a tow. The car is never written off
   by one crash; a team retires only when it gives up or runs out of time.
3. **Lost parts stay in the world.** A bumper, bonnet, door or wheel that comes off lies where it landed.
   Players walk back, carry it and bolt or strap it on. Leaving it behind has consequences.
4. **Every failure has a symptom before it has a readout.** Players should hear, see or feel a problem
   (pulling, steam, knocking, rattling) and diagnose it by looking, not by opening a menu.

## Layers

| Layer | Examples | State lives in |
| --- | --- | --- |
| Body shell | dents, scraped paint, crushed roof | replicated dent list (built) |
| Panels | bonnet, boot lid, 4 doors, 2 bumpers, 2 front wings, 4 lamp units | per-panel fixing state (built) |
| Glass and lamps | windscreen, side and rear windows, headlamps, tail lamps | per-pane state |
| Wheels | tyre, rim, wheel attached or off | part health (tyres and wheel off built) |
| Mechanical | suspension, steering, radiator, engine, gearbox and more | part health and fluid levels (running gear, cooling, engine, battery, oil, fuel, exhaust and gearbox built) |
| Chassis | bent frame | one value per car |
| Crew | injuries | per player |

## Panels: hang and detach

Each panel has fixings (hinges and a latch, or clips for bumpers) with their own health. Impacts damage the
fixings near the contact patch, as they already damage nearby parts. A panel's state follows from its fixings:

| State | Cause | Looks | Gameplay | Field fix |
| --- | --- | --- | --- | --- |
| Intact | | closed, panel gaps normal | none | |
| Loose | latch or one clip damaged | gaps, rattles over bumps | worsens with jolts and speed | strap or tape (bodge) |
| Hanging | latch gone, or one hinge gone | flaps open, swings, drags | see per panel below | strap shut, or pull it off |
| Detached | all fixings gone | debris on the road (ARallyDebrisActor) | missing-part effects | carry back and refit |

Per panel:
- **Bonnet.** Latch fails and the bonnet flips up at speed, blocking the windscreen. Stop and strap it, or
  rip it off. Missing: engine bay open to rain and river crossings (electrics), no protection for the radiator
  from above.
- **Doors.** Can **jam** in a side hit: that door will not open, so the crew uses the other door, a broken
  window or the roof (the exit search in CREW-AND-SEATS.md already tries the other door, then the roof;
  a broken window would be a new exit). Hanging: drags,
  noise, cannot close. Missing: cold and rain in the cabin, the occupant on that side takes more from side hits.
- **Bumpers.** Clips break and the bumper drags on one end (sparks, noise, a little drag), then falls off.
  Missing front bumper: the radiator takes more from frontal hits and stones.
- **Boot lid.** Pops open on rear hits or rolls; spares and tools behind it are exposed. (Spilling the boot's
  contents is an idea for later, see open questions.)

Delayed failure is the point: a loose bonnet should fly open two kilometres after the crash, not at the
moment of impact. Loose panels progress towards Hanging from speed (start: above 80 km/h) and jolts (landings,
kerbs); a strap bodge resets that progress but can fail again on a big jolt.

Refit:
- A detached panel keeps its dents (the debris actor already copies the dented mesh).
- Proper fix: carry the panel to the car, 2-4 actions with a spanner at its fixings. Only possible if the panel
  is not too crushed (start: no point pushed in more than 15 cm); otherwise it is service-only.
- Bodge: straps, wire or tape. Fast, holds the panel on loosely, can fail again.
- Doors and bonnet are heavy: one player carries slowly, two carry at walking pace.

## Glass and lamps

| State | Cause | Gameplay | Field fix |
| --- | --- | --- | --- |
| Cracked | roof or pillar impacts, hard frontal hits, stones | windscreen crazes, harder to see through (worse at night and in rain) | none, live with it |
| Shattered | heavy impacts, rolls | hole: clear view but wind, rain and cold get in; window becomes an exit | plastic sheet and tape |

Headlamps and tail lamps break individually. Broken headlamps matter at night; that ties into electrics.

## Wheels

Built: tyre health with a flat at 0, and wheels coming off (DAMAGE.md, "Wheels coming off"). Add:
- **Rim:** bent by direct wheel hits and by driving on a flat. Vibration and a slow air leak. Hammer roughly
  straight (bodge) or swap the wheel.
- **Off the rim:** driving far on a flat strips the tyre. (Wheel off is built: a hard hit on an already
  broken corner, or one huge hit, tears the hub; the corner drags on its disc.)
- Wheel change is built: jack from the boot, wheel off and on with the spanner, the spare wheel from the
  boot (DAMAGE.md, "Running gear").

## Mechanical components

Aim: about 20 serviceable components (game concept). Built ones marked.

### What the Octavia model has (survey 2026-10-11)

Every item below is a separate object in Art/Vehicles/Octavia/Working/Octavia_Liftback_Working.blend
(collections 03_Engine and 04_Suspension), so each can become its own removable part:

- Engine bay: radiator with two fans, intercooler, engine cover (1.8T), airbox, battery on its tray and
  fluid reservoirs (washer, coolant, brake fluid; one object "engbay"), gearbox, turbocharger, turbo
  manifold, charge pipes, intake, vacuum line, radiator hoses, belt-drive pulleys and accessories
  ("commonparts"), downpipe. Under the car: mid exhaust, rear silencer, heat shield, fuel tank.
- Not modelled: the engine block and cylinder head (empty space under the cover in the selected 1.8T
  set). A hidden alternative, engine_i4_sohc (about 48 x 55 x 29 cm), stands in (built 2026-10-12: stood
  upright it sits under the cover beside the gearbox, part of SM_Octavia_Engine).
- Front suspension: subframe, two struts with coil springs, two hub carriers, two lower arms, two
  driveshafts with CV boots, steering rack, tie rods (one object for both sides), anti-roll bar with
  its mounts and two drop links.
- Rear suspension: torsion beam, rear hubs (both sides in one object), two coil springs, two shocks
  (each pair in one object). A hidden alternative is a full 4x4 multi-link rear: subframe, upper,
  lower and trailing arms, anti-roll bar and hubs. Built (2026-10-12): the export uses the 4x4 rear
  (stored rotated in the file; turned and slid onto the wheel centres) with the torsion set's springs
  and shocks, and splits every corner into strut, hub, disc and arms, the front also tie rod and
  driveshaft: 26 suspension meshes that move with the wheels.
- Brakes: no separate brake objects. Each front hub object contains its disc (about 22 cm, two faces)
  and caliper as separate loose pieces, so they can be split out by geometry; the rear hub object holds
  both rear discs. The pedal, handbrake lever and clutch pedal are in the interior.
- Everything mechanical uses the single M_Prototype_Mechanical material.

| Component | How it breaks | Symptom | Field fix | Service only |
| --- | --- | --- | --- | --- |
| Tyres (built) | direct wheel hits | flat: pulls, sluggish, rides on the rim | swap spare | |
| Suspension arms (built) | wheel hits, big landings | toe-out pulls (built) | replace from spares, hammer straight (built) | |
| Struts (built) | wheel hits; add big landings | sags and bounces (built); add bottoming out | replace from spares (built) | |
| Brakes (built) | hard direct wheel hits | weak, pulls under braking (built) | replace disc from spares (built) | |
| Rims | wheel hits, driving on a flat | vibration, slow leak | hammer straight or swap wheel | |
| Steering / tie rods (built) | front wheel hits | bent: pulls; broken: wheel no longer steers (built) | replace tie rod, hammer straight (built); align by eye | |
| Driveshafts (built) | hard front wheel hits | broken = no drive at all, open diff (built); add clicking | swap from spares (built) | |
| Radiator (built) | frontal hits (built), stones | coolant leaks, temperature climbs, steam (built) | sealant, top up water (built); from a stream | |
| Engine (built) | overheating, very hard hits (built); no oil, over-revving | power loss, smoke (built); knocking | let it cool, refill (built) | seized engine (built) |
| Oil sump (built) | underside rock hits (built) | oil light, level drops, smoke (built) | patch, refill oil (built) | |
| Gearbox (built) | underside hits (built); abuse | pops out of gear, lost gears (built); grinding | limp on remaining gears (built) | yes |
| Exhaust (built) | underside hits (built) | hangs and drags (built); loud, sparks | wire it up or pull it off (built) | |
| Fuel tank (built) | underside hits (built); rear hits | fuel drops faster than usage (built) | patch (built); refuel | |
| Electrics (battery built) | impacts (built); water, open engine bay | engine cuts out, then dies (built); lights flicker | reconnect the battery (built); swap fuse | |
| Chassis | very heavy impacts, accumulated heavy dents | car crabs, nothing aligns properly | none | yes, or tow |

Order inside this list: steering, driveshafts and the rest of the running gear came first, then the
radiator to engine temperature chain and the battery (both built 2026-10-12; DAMAGE.md, "Engine,
cooling and battery"), then underside hits on the sump, exhaust, fuel tank and gearbox (built 2026-10-12;
DAMAGE.md, "Underside").
Still open in the running gear: landings damaging struts, the rear upper and lower arms and rear
driveshafts as parts in their own right (visual only while the car is front-wheel drive), all-wheel
drive for the 4x4 rear, and choosing by hand how a refitted part sits.

### How damage reaches parts

- **Impacts** (built): severity and contact patch from the body box, routed to nearby parts.
- **Underside hits** (built): contacts pushing up from under the floor still dent nothing, so landings do not
  dent the body; the wheels and suspension take normal landings, and a rock or crest hitting the floor itself
  damages sump, exhaust, fuel tank and gearbox (a broad flat landing a quarter as much).
- **Landings:** very hard landings damage suspension even without an impact.
- **Abuse:** missed shifts and launches wear the gearbox; over-revving and overheating wear the engine.
- **Time and distance:** leaks drain fluids, loose panels progress, bodges can fail on a big jolt.
- **Knock-on damage:** no front bumper means more radiator damage; a radiator leak overheats the engine;
  no bonnet lets water reach the electrics; driving on a flat bends the rim and strips the tyre.

## Rollovers

- Roof contacts dent the roof like any other face (the body box already handles it). Rolls also crack or
  shatter glass and can pop the boot lid and bonnet.
- A car on its roof or side stalls and slowly loses oil. Two players push it back onto its wheels; one player
  alone cannot. A tow strap from another car also works.
- Roof crush deep enough to reach the cabin lowers headroom and makes the injury check harsher.

## Crew injury

- Each occupant checks injury on every impact, from the change of velocity at their seat (start: minor above
  12 m/s, serious above 20 m/s). Side hits hurt the occupant on that side more; a missing door makes it worse.
- Minor: blurred vision and shaky hands (slower repair actions) for a while.
- Serious: slow walk, blurred vision until treated with the first-aid kit by the other player.
- Rolls add a smaller injury check per roof or side landing.
- Injury replicates per player; the screen effects are local.

## Feedback

Crashes should feel like events. All of this runs locally from replicated state and contact events; the only
new network traffic is one impact event per crash so every machine plays it.
- Impact: crunch scaled by severity and by material (metal, glass, plastic bumper), camera shake, rumble.
- Scraping: sparks and grinding at metal-on-rock contact, a dragging bumper or exhaust.
- Glass: crack sound, shards on the ground for a shattered pane.
- Radiator: steam at the front when hot or leaking (built, simple puffs), a puddle under a parked car.
- Engine: smoke colour by cause (white coolant, blue oil, black running rich), knocking. Built: white
  steam, grey haze from a dry engine, blue smoke from a worn one.
- Tyre: hiss on puncture, flapping rubber when flat.
- Panels: rattle when loose, flapping and banging when hanging.

## Networking rules

Same as the rest of the project (README architecture):
- The server owns all damage state and validates every pick-up, refit and repair.
- Replicate compact state only: dent list (built), part health (built), panel fixing state, glass state,
  fluid levels (quantised), injuries. Never replicate meshes.
- Hanging panels swing in a local cosmetic simulation driven by the car's motion; only Detached spawns a
  replicated debris actor (built).
- Late joiners rebuild everything from state, as dents already do.

## Order of work

1. **Panels hang and detach**, with pick-up and refit. ARallyDebrisActor is the starting point.
2. **Feedback pass:** impact sounds, sparks, glass crack and shatter, steam. Cheap and it makes the first
   playable's goal ("crashing feels like an event") testable.
3. **Mechanical:** running gear, radiator to engine temperature chain, underside hits (all built).
4. **Rollovers and crew injury:** push the car back over, injuries and first aid.

Each step ends with an automated crash test like scripts/Test-Crash.ps1 and a two-player run.

## Open questions

- Can rivals pick up your team's detached parts from the road, or only your team?
- Can a serious injury knock a player out until the other one helps? Can a car injure someone on foot?
- Should the boot spill tools and spares when it pops open or the car rolls?
- Fire from a fuel leak near a hot engine: in or out? (Proposal: out for now.)
- How is a bent chassis decided: one very heavy impact, the sum of heavy dents, or both?

## Implementation checkpoint: 2026-10-11

First slice of step 1 implemented: replicated fixing health, local hinge motion, delayed latch wear,
persistent debris with independent dent snapshots, door jams, single-player carrying, three-action
proper refit, and consumable strap bodges. See DAMAGE.md for current controls, prototype kit limitations
and validation. The full panel step is not complete: cooperative carrying, manual removal, physical
bumper drag, single-hinge door sag, missing-panel exposure effects, and inventory remain.

## Implementation checkpoint: 2026-10-12

Since the first checkpoint: manual removal (R), single-hinge droop, panel collision with the world,
open-panel physics (door checks, props and struts, latches catching on a slam, wind and braking
shutting or swinging panels), swing damage, imperfect refits, front wings and lamp units as clipped
panels, per-part crush profiles, and the running gear (struts, arms, brakes, tie rods, driveshafts) with
a jack, a boot spares kit and part swaps. Then cooling and the engine: coolant leaking from a damaged
radiator, engine temperature, overheating wear to seizing, a battery that cuts out, ignition, steam and
smoke, and field fixes (sealant, battery terminals, a water can). Then the underside: floor hits reaching
the sump, fuel tank, exhaust and gearbox, oil and fuel levels, an oil light and fuel gauge, a hanging
exhaust that drags, a gearbox losing gears, and putty, a strap and an oil bottle to deal with them.
DAMAGE.md has the details and test results.
