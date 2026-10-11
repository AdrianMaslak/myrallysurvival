"""Condense the -RallyAutoDrive log into one line per sample: speed, gear, rpm, attitude and tyre slip.
Usage: python scripts/drive_log_summary.py [log] [phase-regex]
"""
import re
import sys
from pathlib import Path

log = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / "Saved/Logs/RallySurvival.log"
phase_filter = re.compile(sys.argv[2]) if len(sys.argv) > 2 else None
sample = re.compile(r"RALLY_AUTODRIVE (t=\S+) phase=(\S+) kmh=(\S+) gear=(\S+) rpm=(\S+) origin_z=(\S+) roll=(\S+) "
                    r"pitch=\S+ yaw=\S+ lat_g=(\S+) slip=(\S+) wheels(.*?)(?: at=(\S+))?$")
wheel = re.compile(r"\[(\d+)N c\S+ s(\S+) a(\S+)\]")

for line in log.read_text(encoding="utf-8", errors="replace").splitlines():
    if "RALLY_AUTODRIVE stop" in line or "RALLY_AUTODRIVE_DONE" in line:
        print(line.split("Display: ")[-1])
        continue
    m = sample.search(line)
    if not m or (phase_filter and not phase_filter.search(m.group(2))):
        continue
    wheels = wheel.findall(m.group(10))
    if len(wheels) != 4:
        continue
    loads = "/".join(w[0] for w in wheels)
    front_angle = (float(wheels[0][2]) + float(wheels[1][2])) / 2
    rear_angle = (float(wheels[2][2]) + float(wheels[3][2])) / 2
    print(f"{m.group(1):8} {m.group(2):14} {float(m.group(3)):6.1f}km/h g{m.group(4):>2} {m.group(5):>5}rpm "
          f"z{float(m.group(6)):5.1f} roll{float(m.group(7)):5.1f} latg{float(m.group(8)):5.2f} beta{float(m.group(9)):6.1f} "
          f"aF{front_angle:6.1f} aR{rear_angle:6.1f} spinFL{float(wheels[0][1]):5.2f} spinFR{float(wheels[1][1]):5.2f} loads {loads} at {m.group(11) or '?'}m")
