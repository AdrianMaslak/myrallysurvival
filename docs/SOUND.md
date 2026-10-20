# Sound

Every sound the car makes is synthesised while it plays, from what the car is doing: no recordings yet.
The models are plain C++ in Source/RallySurvival/Public/Audio/CarSoundModels.h, so the same code renders
WAV previews outside the game; URallySynthComponent plays one model as a mono synth (spatialised in the
world, except the ambience).

## Voices

| Voice | Where | Driven by |
| --- | --- | --- |
| Engine and exhaust | between the engine and the exhaust | rpm, load, running, engine health, exhaust state, misfires, cabin or outside |
| Road | middle of the car | speed, wheels on the ground, surface looseness, sliding, metal dragging, cabin or outside |
| One-shots | middle of the car | crashes, floor hits, gear changes, failed starts, the engine stopping |
| Ambience | around the listener (not spatialised) | nothing: wind gusts and birds |

### Engine and exhaust (FEngineModel)

- A four-cylinder four-stroke: a firing every half revolution (30 a second at 900 rpm, 100 at 3000).
  Each firing sends a pressure pulse into the exhaust, stronger with load, with turbulent noise riding on
  it and small differences between cylinders and cycles.
- The pulses ring the header and front pipe (delay lines with an open-end reflection) and the pipework's
  resonances, then the silencer takes the top off. A leaking exhaust lets the raw pipe through, louder and
  raspier; with no exhaust at all there is no silencer.
- A damaged or missing exhaust manifold blends in sharp open-port pulses, bypassing both the long pipe
  and silencer and using the cabin's bulkhead filtering. Missing boost components silence the turbo
  whistle and blow-off rush; an open air filter box increases intake roar.
- Intake roar with load, valve-train ticks, alternator whine, a turbo that spools with load and revs
  (whistle and hiss) and a blow-off valve when the throttle is lifted on boost.
- Misfires leave gaps; the unburnt charge, and late burning on the overrun, pops in the exhaust: rarely on
  a healthy car, often with a leaking or missing exhaust or a worn engine. A worn engine also knocks once a
  revolution (faintly from 90% health, loud by 15%) and runs rough.
- Coil faults use a separate missed-firing probability for each cylinder, in firing order 1-3-4-2.
  A dead coil always skips its cylinder; weak coils skip intermittently, including at high revs.
- Pinking: with boost past what the engine computer allows, charges detonate now and then under load, a
  sharp metallic tick at 3.8-5.6 kHz, low enough to come through the bulkhead (`FEngineParams::Detonation`).
- Loose parts chatter on their mounts, 1.4-4.2 kHz clicks up to 40 a second (`FEngineParams::Rattle`).
- Below 450 rpm with the ignition on it is the starter: a geared whine labouring on each compression and
  the chuff of each one; switched off, it runs down on compression alone.
- From the cabin the exhaust is muffled and what comes through the bulkhead dominates.

### Road (FRoadModel)

- Gravel: individual stones crunching under the tread (faster with speed and sliding), low rumble, stones
  flicked against the floor, a hiss of spray when sliding.
- Tarmac: a smooth roar rising with speed, a squeal at the limit.
- Wind rising with the square of speed; metal dragging (a hanging exhaust, a hub without its wheel).
- The surface comes from the physical material under the car; sliding from each wheel's spin against the
  ground and the body's drift, as a share of the tyre's peak.

### One-shots (FEventModel)

- Crash: a body thump, sheet metal ringing at random panel modes, crumpling crackles, glass in a big one;
  scaled by the change of velocity.
- Floor hit: a deep thump and a scrape. Gear change: a clunk. The engine stopping: a shudder.
- Turning the key when it will not start: the solenoid clicks (flat battery), the starter strains against
  a seized engine, or turns an engine with no fuel that never catches.
- A fixing working loose on the move: a small bright clink; the last one, the clunk of the part dropping.
- A gear that will not go in (a missed downshift): the teeth rasp and the lever clunks.
- The horn: two buzzing electric horns a minor third apart (415 and 498 Hz), retriggered while held. The
  indicators' relay ticks (the solenoid click, quietly).

### Radio (FRadioModel)

- A station playing a 90s dance tune (Am F C G at 130 bpm: kick, clap, off-beat hat, bass, a square-wave
  arpeggio) through the small dashboard speaker (no deep bass, no top, a little driven) with FM hiss. On the
  ignition with the radio switched on (click it in the cabin); its own voice at the dashboard.

### Ambience (FAmbienceModel)

- Wind gusting through grass and trees, birds now and then.
- Rain (`SetWeather`, from the day clock's rain on each player's own machine): out in it a hiss round
  3.5 kHz and drops ticking at 2.5-6 kHz; from a car's seat the hiss muffled and the roof drumming low,
  each drop a dull tap at 300-1100 Hz; the birds keep quiet. In the game's own output on foot in a
  downpour, 2.5-6 kHz went from 4.7% of the sound to 42.7% (Test-Start -WithSound, dry and with
  -RallyStartRain).

## Networking

Nothing about sound is sent except one-shots nobody could work out locally. Every machine runs the
engine and road voices from state it already has (rpm, power, gear, wheel spin, velocity, damage). The
machine that feels a crash or floor hit plays it at once; the server multicasts it to everyone else.
A failed start is multicast by the engine component, a fixing working loose by the panel component. Battery cut-outs are exact on the driving machine and
random at the same rate elsewhere.

## Starting and stopping

The engine no longer starts by itself: in the driver's seat, I turns the key. The starter turns it over for
0.7 s (slower on a weak battery) before it fires; I again switches it off. The key stays in when you get
out, and I at the driver's door turns it from outside (CREW-AND-SEATS.md); a fault that stops it (flat
battery, no fuel, seized) needs the key turned again once fixed.

## Previews and checks

`scripts\Render-Sounds.cmd` builds Tests/SoundPreview.cpp and writes Saved/SoundPreview/*.wav:
engine_healthy (start, idle, blips, a pull to 6700 rpm, overrun, idle, switch off), engine_damaged,
engine_exhaust_off, engine_cabin, engine_pinking (and _cabin), engine_rattle, road_gravel, road_tarmac,
road_scrape, events (crashes light to heavy, floor hit, gear changes, failed starts, a bolt working loose,
a part dropping) and ambience. It checks that every sound is finite and does not clip, that the engine's
note sits on its firing frequency at idle and at 3000 rpm, that it is silent while off and audible while
cranking, much louder at full load than at idle, louder with no exhaust, that pinking adds its band under
load (16 times outside, 22 from the driver's seat) but not at idle, that loose parts rattle at idle, and
that rain hisses out in it and drums low under a roof (ambience_rain, ambience_rain_roof).
Part-fault previews: engine_manifold_off, engine_turbo_off, engine_coil3_dead, engine_weak_coils and
engine_airbox_off. Checks cover louder open ports, loss of the turbo's lift-off rush, and the dead
cylinder's beat once every two crank revolutions.
`scripts\Test-Knock.ps1 -WithSound` checks the pinking in the game's own output (DAMAGE.md, engine
modules).

In the game: `scripts\Test-Underside.ps1 -WithSound` plays the underside drive with sound on and records
the game's output to Saved/BouncedWavFiles/RallyUnderside_audio.wav (2026-10-12: the engine note climbing
through the gears, gear clunks, both floor hits, braking, idle, quieter in the cockpit than outside, birds).
Unreal mutes a game whose window is not focused; the recording run turns that off.

The voices start once, at BeginPlay (the car) or the first local tick (the ambience), and only when the
machine has an audio device: under -nosound there is none, and retrying every frame created an audio
component per frame, whose hitches spoiled the two-player car sync (shape error 1.05 m instead of 0.04).

## Recordings (next)

Synthesis gets the behaviour right (it follows rpm, load and damage continuously) but not the character
of a real engine. For that the plan is recorded loops of a four-cylinder turbo (ideally an Octavia or Golf
1.8T), layered under or replacing the synthesised engine:

- steady-rpm loops, every 500 rpm from 1000 to 7000 rpm, each on load (accelerating) and off load
  (decelerating), recorded at the exhaust and in the cabin; plus idle;
- starter crank, start, stop; overrun crackles and pops; turbo spool and blow-off; gear changes;
- tyres on gravel and tarmac rolling and sliding, stones on the floor, wind;
- crashes (light, medium, heavy), glass, scraping, doors, bonnet, boot, tools.

Mono WAV, 48 kHz, 16 or 24 bit, loops cut at zero crossings. Free sources are thin for steady-rpm sets;
recording a real car, or a commercial engine pack, is the realistic route.
