# The crew's bodies

GAME-PLAN.md keeps cold, wet and injury (no hunger, thirst or tiredness meters), and alcohol and cigarettes.
Injury, the hospital and the cold are in; wet, alcohol and cigarettes come later.

- **Injury** (`Player/BodyRules.h`): a crash hurts everyone in the car from about 30 km/h of change of speed;
  a 50 km/h wall about halfway. It adds up, 0 to 100%. Hurt, the edges of sight darken and fringe (camera
  post-process, on that player's machine) and walking slows (to 55% at worst). The top right says how hurt.
- **First-aid kit**: in the boot kit, a white box with a red cross (1 kg). Carrying it while hurt, E treats
  you: the injury is gone. It has three uses, then it is empty and gone. It can be ordered again later
  (not yet in the catalogue).
- **Hospital**: hurt to 100%, a player is taken out of the car to hospital and wakes in the service yard,
  still 25% hurt; the 3 000 Sk bill comes out of the kitty. The others carry on.
- **Cold**: the air is about 6 C before dawn and 22 C mid-afternoon (`AmbientC`). Outside below 12 C cold
  builds (at 6 C full in about ten minutes); in the car with the engine running the heater takes it away
  within a minute, a cabin with the engine off slowly, a warm day too. Cold, the hands are slow (holding a
  fixing takes up to twice as long) and the world looks blue; the top right says how cold.
- Not yet: treating a team mate, wet (rain), alcohol and cigarettes, food or a drink to warm up.
- Test: the crash test logs `RALLY_BODY` for its driver; the rules tests cover the numbers.
