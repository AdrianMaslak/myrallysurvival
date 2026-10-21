# The boot

The boot carries the spares kit (spare wheel, jack, a strut, arm and disc for each axle, a driveshaft,
the water can, the oil bottle, a 20 L fuel can and the first-aid kit, 102 kg), and whatever else is put in it (GAME-PLAN.md:
the boot as an inventory with space and weight; QUESTS.md, quest 5).

- **Where things lie**: every kit item has its own place, where it is packed when the car is new
  (`URallyPanelComponent::BuildBootHomes`): the spare wheel flat in its well under the load floor;
  everything else laid flat on the load floor (59 cm up, from the tailgate lip to the seat backs, between
  the wheel arches; measured off the body mesh) with its longest side across the car, biggest first, each
  at the first place from the back that nothing else covers. Anything else (a broken strut taken off, a
  delivered part) goes at the first free place on the floor round them, and on top once the floor is
  full (`BootPlace`). Parts used to be placed by their bolted end, which put most of the kit through the
  floor and a strut out under the bumper; loading a save packs the boot afresh, kit items into their places
  first.
- **Strapped down**: whatever lies on the floor has an orange strap over it, fore and aft
  (`ARallyDebrisActor::UpdateStraps`).
- **Its place shows**: with the boot lid open, each kit place that is empty shows a faint cyan outline of
  what belongs there, drawn through whatever is in front (so the wheel's well shows under the floor); the
  one for the part in your hands is brighter (`UpdateBootGhosts`, every machine).
- **Packing**: carrying a part with the boot lid open (or gone) and the boot within reach, E puts it in
  when it does not fit anywhere on the car in reach: into its own place if it is a kit item's kind and that
  place is free (a front strut's place takes either front strut), else on the floor. G does the same while
  you look into the boot (it used to drop through the car); looking away, G drops it at your feet. The
  prompt says which, and how full the boot is. Up to 150 kg, and only what there is room for.
- **Taking out**: as before, E on a part in the open boot picks it up.
- **Weight**: whatever the boot holds beyond the kit it left with weighs on the car, at the back; the car's
  tuned mass (1 300 kg) includes the kit. A delivered rally strut put in makes it 6 kg heavier with the
  centre of gravity slightly further back; a boot emptied of its kit makes it 102 kg lighter and
  shifts the balance forward. Every machine works this out from the replicated parts
  (`ARallyCarPawn::UpdateBootLoad`).
- **Saved** with the team: stowed parts are saved where they ride (SAVING.md).
- **Tests**: the shop test puts the delivered strut in the boot and checks the load. Test-Parts takes the
  spare wheel and front strut out (their outlines show, `RallyParts_boot_out.png`) and puts them back:
  each lands exactly in its place, the wheel in its well, strapped (`RallyParts_boot_back.png`).
  `scripts\Test-Boot.ps1` does it as a player on foot: the first-aid kit out of the open boot, G looking into
  the boot puts it back in its place (`RallyBoot_carrying.png`, `RallyBoot_back.png`), G looking away drops
  it.

Not yet: parts left loose in the boot or the cabin, sliding about and falling out of an open boot or door
(next); food, water, fuel cans and tools as boot items; the boot's volume (only weight counts); the roof
rack.
