# Money

Slovak koruna (Game/EconomyRules.h, `ARallyTeamState`). The team starts with 15 000 Sk in the kitty, the cash
box in the workshop (GAME-PLAN.md: team kitty plus pocket money; pocket money and the physical box come with
the home map). The kitty shows under the clock, with any parts on order.

- **Prize money**, into the kitty at the end of each special stage: 6 000, 4 000, 3 000, 2 000, 1 500 and
  1 000 Sk for the first six places, 500 for finishing lower, nothing for retiring.
- **Parts catalogue**: K opens it (on foot or in the co-driver's seat), the mouse wheel goes through its
  lines, Enter orders the one picked. It is paid from the kitty at once (lines the kitty cannot pay are
  greyed and refused) and phoned through: the next morning's post (08:00) leaves the part by the service
  yard, beside where the car first stood. Stock replacements, sport and rally struts, discs and calipers,
  turbos and engine computers, and an intercooler (2 500 to 30 000 Sk). Grades: DAMAGE.md, Grades and
  settings.
- Saved with the team: the kitty and the orders on their way.
- **Test**: `scripts\Test-Shop.ps1` orders a rally strut (kitty 15 000 to 6 000 Sk), is refused the rally
  turbo, moves the clock to the next morning and finds the strut delivered, then adds a stage win's prize.
  The stage test logs its prize (`RALLY_MONEY`).

Not yet: pocket money, the cash box as a thing in the workshop, entry fees, selling parts, the scrapyard,
the village shop, sponsors, the businessman's loan.
