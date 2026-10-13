# Money

Slovak koruna (Game/EconomyRules.h, `ARallyTeamState`). The team starts with 15 000 Sk in the kitty, the cash
box in the workshop (GAME-PLAN.md: team kitty plus pocket money; pocket money and the physical box come with
the home map). The kitty shows under the clock, with any parts on order.

- **Entry fee**: 1 000 Sk paid at the start line of each special stage; with less in the kitty the car is
  not started (the line says so).
- **Prize money**, into the kitty at the end of each special stage: 6 000, 4 000, 3 000, 2 000, 1 500 and
  1 000 Sk for the first six places, 500 for finishing lower, nothing for retiring.
- **Parts catalogue**: K opens it (on foot or in the co-driver's seat), the mouse wheel goes through its
  lines, Enter orders the one picked. It is paid from the kitty at once (lines the kitty cannot pay are
  greyed and refused) and phoned through: the next morning's post (08:00) leaves the part by the service
  yard, beside where the car first stood. Stock replacements, sport and rally struts, discs and calipers,
  turbos and engine computers, and an intercooler (2 500 to 30 000 Sk). Grades: DAMAGE.md, Grades and
  settings.
- **Workshop jobs** in the service yard (the car within 25 m of where it first stood): P on a damaged part
  pays for what the roadside cannot do, and the clock moves on while it is done. A new radiator (2 500 Sk,
  half an hour; bonnet open), a new battery (1 500, 12 min), the sump welded (1 200, half an hour), the
  fuel tank welded (1 500, 42 min), the exhaust welded and hung (1 800, half an hour), the gearbox rebuilt
  (8 000, 2 h), the engine rebuilt (15 000, 4 h; aim at the oil filler), the body straightened with every
  attached panel's hinges and latch renewed (4 000, 2 h; aim at any panel). Swapping struts, wheels and
  modules stays the team's own work.
- **Fuel**: the service yard's pump fills the tank at 30 Sk a litre (P at the tank, crouched, or on any panel
  when the body needs nothing), in a few minutes. There is no other fuel yet.
- **Tow**: H in the driver's seat, away from the service yard: the car is set down in the yard as it is,
  crew aboard, for 2 000 Sk and 2 hours; on a stage (or its countdown) the car is retired from it with
  the stage's maximum time. The last resort when it cannot be fixed by the roadside.
- Saved with the team: the kitty and the orders on their way.
- **Test**: `scripts\Test-Shop.ps1` orders a rally strut (kitty 15 000 to 6 000 Sk), is refused the rally
  turbo, moves the clock to the next morning and finds the strut delivered, adds a stage win's prize, and
  has the workshop fit a new radiator (2 500 Sk and half an hour), then takes the car 100 m out and tows it
  back (2 000 Sk).
  The stage test logs its prize (`RALLY_MONEY`).

Not yet: the workshop's prices for loose parts and wheels, pocket money, the cash box as a thing in the workshop, entry fees, selling parts, the scrapyard,
the village shop, sponsors, the businessman's loan.
