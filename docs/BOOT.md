# The boot

The boot carries the spares kit (spare wheel, jack, a strut, arm, disc and tie rod for each axle, a
driveshaft, the water can and the oil bottle, 86 kg), and whatever else is put in it (GAME-PLAN.md: the boot
as an inventory with space and weight; QUESTS.md, quest 5).

- **Packing**: carrying a part with the boot lid open (or gone) and the boot within reach, E puts it in when
  it does not fit anywhere on the car in reach. The prompt says how full the boot is. Up to 150 kg; parts
  lie on the boot floor, three across and four deep, then on top.
- **Taking out**: as before, E on a part in the open boot picks it up.
- **Weight**: whatever the boot holds beyond the kit it left with weighs on the car, at the back; the car's
  tuned mass (1 300 kg) includes the kit. A delivered rally strut put in makes it 6 kg heavier with the
  centre of gravity slightly further back; a boot emptied of its kit makes it 86 kg lighter and
  shifts the balance forward. Every machine works this out from the replicated parts
  (`ARallyCarPawn::UpdateBootLoad`).
- **Saved** with the team: stowed parts are saved where they ride (SAVING.md).
- **Test**: the shop test puts the delivered strut in the boot and checks the load.

Not yet: food, water, fuel cans and tools as boot items; the boot's volume (only weight counts); the roof
rack.
