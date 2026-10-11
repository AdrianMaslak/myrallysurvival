"""Compare host and client views of every car from a Test-TwoPlayer.ps1 run.
Usage: python scripts/net_compare.py <host.log> <client.log>

For each car the machine that simulates it (sim=1, the driver's) is the reference. The other machine's
replayed view is checked for:
- delay: the constant time offset that best aligns it with the reference (expected: interpolation
  delay plus one-way latency);
- shape error at that offset (how far the replay strays from the real path);
- snaps: jumps beyond constant-velocity motion between consecutive samples, on both machines.
"""
import math
import re
import sys

LINE = re.compile(r"RALLY_NET st=(\S+) pt=(\S+) player=(-?\d+) role=(\w+) local=(\d) sim=(\d) pos=(\S+),(\S+),(\S+) vel=(\S+),(\S+) yaw=(\S+)")


def read(path):
    cars = {}
    with open(path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = LINE.search(line)
            if not m or int(m.group(3)) < 0:
                continue
            # (server time, position, velocity, simulated here, locally driven, physics time)
            sample = (float(m.group(1)), (float(m.group(7)) / 100, float(m.group(8)) / 100),
                      (float(m.group(10)) / 100, float(m.group(11)) / 100), m.group(6) == "1", m.group(5) == "1",
                      float(m.group(2)))
            cars.setdefault(int(m.group(3)), []).append(sample)
    for track in cars.values():
        track.sort(key=lambda s: s[0])
    return cars


def position_at(track, t):
    if not track or t < track[0][0] or t > track[-1][0]:
        return None
    lo, hi = 0, len(track) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        lo, hi = (mid, hi) if track[mid][0] <= t else (lo, mid)
    (t0, p0, *_), (t1, p1, *_) = track[lo], track[hi]
    a = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
    return (p0[0] + (p1[0] - p0[0]) * a, p0[1] + (p1[1] - p0[1]) * a)


def errors(view, reference, offset):
    out = []
    for t, pos, *_ in view:
        truth = position_at(reference, t - offset)
        if truth:
            out.append(math.dist(pos, truth))
    return out


def snaps(track):
    # Physics time: server-clock estimates are corrected in steps and game time outruns physics on slow machines.
    jumps = []
    for (_, p0, v0, *_, t0), (_, p1, *_, t1) in zip(track, track[1:]):
        dt = t1 - t0
        if 0.0 < dt < 0.5:
            jumps.append(math.dist((p0[0] + v0[0] * dt, p0[1] + v0[1] * dt), p1))
    return sorted(jumps, reverse=True)


logs = {"host": read(sys.argv[1]), "client": read(sys.argv[2])}
for player in sorted(set(logs["host"]) | set(logs["client"])):
    tracks = {name: cars.get(player, []) for name, cars in logs.items()}
    owner = next((name for name, track in tracks.items() if track and sum(s[3] for s in track) > len(track) / 2), None)
    if owner is None:
        print(f"player {player}: no simulating machine found")
        continue
    observer = "client" if owner == "host" else "host"
    reference = [s for s in tracks[owner] if s[3]]
    view = [s for s in tracks[observer] if not s[3]]
    moving = sum(1 for s in reference if math.hypot(*s[2]) > 1.0)
    driven = "driven" if any(s[4] for s in reference) else "parked, server-simulated"
    print(f"player {player} ({driven}), simulated on {owner}: {len(reference)} samples, {moving} moving; "
          f"{observer} view: {len(view)} samples")
    if not view or moving < 5:
        continue
    candidates = []
    for ms in range(0, 401, 5):
        errs = errors(view, reference, ms / 1000)
        if errs:
            candidates.append((sum(errs) / len(errs), ms))
    mean, delay = min(candidates)
    errs = sorted(errors(view, reference, delay / 1000))
    print(f"  {observer} view lags by {delay} ms; shape error mean {mean:.2f} m, "
          f"95th pct {errs[int(0.95 * (len(errs) - 1))]:.2f} m, max {errs[-1]:.2f} m")
    for name, track in ((owner, reference), (observer, view)):
        jumps = snaps(track)
        print(f"  snaps on {name}: {sum(1 for j in jumps if j > 0.5)} over 0.5 m, "
              f"{sum(1 for j in jumps if j > 0.2)} over 0.2 m; largest {jumps[0] if jumps else 0:.2f} m")
