# Rally crew

The visual direction comes from ART-DIRECTION.md and GAME-PLAN.md: a low-budget Slovak privateer
team in 1996, simplified believable proportions, modest geometry, muted clothing and small rally
markings. The first suit is faded blue with cream shoulders, red bands, a zipper, an original team
badge and dark boots. No helmet. The supplied head and glasses remain.

Source: JashiPSX's Simple Character PSX, supplied in `Simple_Character_PSX/`. It is kept untouched.
The derivative, matching arms, FBX exports, preview and original license are in `Art/Characters/Crew/`.
Do not redistribute these source assets as an asset pack; the author's license allows their use
and modification within our game. No animation pack purchase or external service is required.

## In game

- X: lie on the belly / request getting up. Standing waits until the collision capsule fits.
- V while prone: roll between belly and back; the back pose initially looks upward for repairs.
- WASD: walk, crouch-walk, or crawl in either lying pose. Shift runs when standing.
- Ctrl/C: crouch. Space jumps when standing, or requests getting up when prone.
- 1-8 / mouse wheel: the existing tool selection now has visible tools, including socket ratchets.
- E/R on a fixing: the working pose and ratchet movement follow the existing repair action.
- Carrying: hands reach around the existing carried object; the object follows a view-relative anchor.
- Driver: seated legs and hands that track two grip points on the rotating steering-wheel mesh; the left
  foot on the clutch pedal while it is down (else flat on the floor just behind it), the right on the brake or the throttle,
  following the pedals as they swing (URallyCabinComponent::GetFootTarget).
- Co-driver: seated pose with hands resting on the lap.

The owner sees a matching arms-only skeletal mesh, and below it their own body (`CrewOwnerBody`): the
whole suit in the same pose with the neck and head and the upper arms folded to a point (the arms mesh
draws the arms), set back behind the eye (14 cm standing, 24 cm lying down, 8 cm seated) so looking down
shows the chest, legs and boots when standing, crawling or driving, not the inside of the shoulders. Seated, the
chest and shoulders fold away too (spine_02): in a car seat they sit right under the eye and the camera saw
inside them. The
owner's shadow is the whole character's (hidden from them, it still casts). Partners see the complete
character, including inside the car. All three meshes use the same pose solver and suit materials. The old
cylinder stays hidden.

## Animation implementation

`URallyCrewMesh` evaluates lightweight authored procedural poses on the supplied skeleton:
idle/breathing, walk, run, jump/fall, crouch idle/walk, belly idle/crawl, back idle/crawl, driver,
passenger, tool-holding/working, and two-handed carrying. Local bone transforms interpolate between
poses. Two-bone limb placement keeps the arm and leg segment lengths consistent. Finger bones close
for a grip, but the source mesh has very simple mitten-like hands.

These are runtime animations, not mocap or a purchased library of animation sequences. The source
Blender file remains in its editable rest pose. Detailed finger contact, foot placement on uneven
terrain, gear-changing and elaborate car entry/exit animations are outside this set.

Prone/back intent travels with saved movement packets. Observers use replicated stance; carried
parts and selected tools already replicate. Tool-work pulses use the server clock. The steering
grip points follow each machine's existing car/steering-wheel presentation.

## Rebuilding and verification

1. Blender background: `scripts/build_crew.py` (suit, body and arms) and `scripts/build_crew_tools.py`.
2. With Unreal closed: `scripts/Import-Crew.ps1`.
3. `scripts/Build.ps1`.
4. `scripts/Test-Crew.ps1`: pose selection, finite hand positions, steering-wheel grip error and
   screenshots for the main poses plus first-person ratchet/driving views.
5. `scripts/Test-Prone.ps1`: physical clearance and underside targeting regression.

Screenshots: `Saved/Screenshots/WindowsEditor/Crew_*.png`. Test log: `Saved/Logs/CrewTest.log`.

Export rules in `build_crew.py` that the pose solver depends on (each one broke it once):
- No mesh may share a bone's name. FBX keeps one namespace, so a mesh called `head` made the bone
  `head1`, the solver found no `head` and every crew member stood in the T-pose.
- The rig is scaled with one `Armature.transform`. Scaling edit-bone heads and tails one at a time
  compounds along connected chains (the knee ended up at 25 cm). The script asserts the bones match.
- The export is turned a quarter about Z so the character faces +X with its left at -Y in Unreal.
- `SK_CrewArms` is the arms from the sleeve's red band outwards; the shoulders filled the view.
- The tool meshes have their working end along Unreal +Y; `UpdateCrewProps` turns them forward and
  holds them 9 cm past the wrist, inside the fist.
Seat/hand alignment should be rechecked when the car's interior or steering wheel changes.
