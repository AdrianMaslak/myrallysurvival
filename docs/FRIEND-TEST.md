# Playing with a friend over the internet (first test)

The game uses Steam's test app (App ID 480, "Spacewar") until it has its own App ID, so Steam shows you both as
playing Spacewar. Steam carries the connection through its relay network: no port forwarding, no IP addresses.

## Before

- The host makes the build: `scripts\Package.ps1` (editor closed) writes `Packaged\RallySurvival-<date>.zip`
  (about 490 MB). Send it (Google Drive or similar).
- Both: Steam running and logged in, and each other in the Steam friends list.
- Both: unzip anywhere and start `RallySurvival.exe`. If Windows Firewall asks, allowing is fine (Steam's relay
  does not need it).

## Joining

1. Host: in the menu, **Continue** (the saved team) or **New game** (moves the old save aside). The countryside
   opens; the opening letter waits for Enter.
2. Host: Shift+Tab opens the Steam overlay: Friends, right-click your friend, **Invite to game**.
3. Friend: accept the invite (Steam pops it up, or Shift+Tab, Friends, the host, **Join game**). The friend's game
   loads the countryside and spawns beside the host.
4. If anything fails, both games go back to the menu with the reason at the top. Note it down.

## What to try (about 20 minutes)

- Walk to the car: host drives (left door, E), friend rides as co-driver (right door, E). Drive SS1 at speed.
- Swap: the friend drives, the host rides. Watch how smooth the car looks from the passenger seat.
- Crash into something at about 40 km/h, get out, change a wheel together (boot F, jack E, 17 mm socket R/E).
- One of you lies under the jacked car (X) while the other watches.
- F10 shows the network: ping, packet loss, bandwidth, and on the friend's side how old the car picture is.
  Note the numbers while driving.

## Report back

- The F10 numbers (ping, loss, snapshot age) while driving.
- Anything that jumped, stuttered, lagged or differed between your two screens, and when.
- Both logs: `RallySurvival\Saved\Logs\RallySurvival.log` in each unzipped folder (zip them; the next start
  overwrites them).

## Known limits

- Test app 480: friends must both own Steam (free) and be logged in; invites show as Spacewar.
- Two players tested so far; up to four can join.
- The car is simulated on the driver's PC; others see it through a short delay (smoothing over the internet is
  the next step).
