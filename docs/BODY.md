# The crew's bodies

GAME-PLAN.md keeps cold, wet and injury (no hunger, thirst or tiredness meters), and alcohol and cigarettes.
Injury is in; the rest comes later.

- **Injury** (`Player/BodyRules.h`): a crash hurts everyone in the car from about 30 km/h of change of speed;
  a 50 km/h wall about halfway. It adds up, 0 to 100%. Hurt, the edges of sight darken and fringe (camera
  post-process, on that player's machine) and walking slows (to 55% at worst). The top right says how hurt.
- **First-aid kit**: in the boot kit, a white box with a red cross (1 kg). Carrying it while hurt, E treats
  you: the injury is gone. It has three uses, then it is empty and gone. It can be ordered again later
  (not yet in the catalogue).
- Not yet: treating a team mate, the hospital, death (GAME-PLAN.md: wake up in hospital, the bill from the
  kitty), cold and wet, alcohol and cigarettes.
- Test: the crash test logs `RALLY_BODY` for its driver; the rules tests cover the numbers.
