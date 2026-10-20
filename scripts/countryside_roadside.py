"""Deterministic, additive roadside hazards. Distances are metres in the game frame.

Reads the delivered route manifest so hazards follow the actual authored routes.
No Unreal or Blender dependency; shared by the asset builder and map installer.
"""
import json
import math
import random
from collections import Counter
from pathlib import Path

from countryside_layout import RoadIndex, tangent

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT / 'Art/Maps/Countryside/Roadside'
SEED = 19473
TAG = 'CountrysideRoadsideV1'
CLEARANCE = 0.65

# Local bounds of the reusable meshes, including buried bases. X follows the road.
SHAPES = {
    'Berm': (3.1, 1.35, 0.36),
    'RockA': (1.10, 0.88, 0.66),
    'RockB': (0.78, 0.68, 0.50),
    'RockC': (1.40, 1.02, 0.76),
    'Stump': (0.68, 0.68, 0.78),
    'StoneBoundary': (2.4, 0.62, 0.76),
}


def route_data():
    return json.loads((ROOT / 'Art/Maps/Countryside/Export/countryside.json').read_text(encoding='utf-8'))['routes']


def footprint(spec):
    """Sample the full bounding rectangle, including long edges on tight bends."""
    sx, sy, _ = SHAPES[spec['mesh']]
    sx *= spec['scale'][0] / 2
    sy *= spec['scale'][1] / 2
    yaw = math.radians(spec['yaw'])
    c, s = math.cos(yaw), math.sin(yaw)
    corners = [(-sx, -sy), (sx, -sy), (sx, sy), (-sx, sy)]
    for a, b in zip(corners, corners[1:] + corners[:1]):
        steps = max(1, math.ceil(math.dist(a, b) / 0.25))
        for i in range(steps + 1):
            t = i / steps
            x, y = a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t
            yield spec['position'][0] + c * x - s * y, spec['position'][1] + s * x + c * y


def make_layout():
    roads = route_data()
    index = RoadIndex(roads)
    rng = random.Random(SEED)
    specs, pockets = [], []

    def add(road, sample, side, offset, mesh, scale, pocket, yaw_offset=0):
        p = road['samples'][sample]
        t = tangent(road['samples'], sample)
        lateral = side * (road['width'] / 2 + offset)
        x, y = p[0] - t[1] * lateral, p[1] + t[0] * lateral
        spec = dict(name=f'Country_Roadside_{road["id"]}_{pocket:02}_{len(specs):03}',
                    stage=road['id'], pocket=pocket, mesh=mesh,
                    position=[x, y, p[2]], yaw=math.degrees(math.atan2(t[1], t[0])) + yaw_offset,
                    scale=scale, side=side, sample=sample)
        # Check every road and service link, not just the road this object belongs to.
        for px, py in footprint(spec):
            distance, _, width = index.nearest(px, py)
            if distance - width / 2 < CLEARANCE or (abs(px) < 105 and abs(py) < 100):
                return False
        specs.append(spec)
        return True

    for road in roads:
        if '_Access' in road['id']:
            continue
        pts = road['samples']
        # The start and finish approaches remain open; pockets have long recovery gaps.
        i, pocket = 28, 0
        while i < len(pts) - 28:
            before, after = tangent(pts, i - 4), tangent(pts, i + 4)
            turn = before[0] * after[1] - before[1] * after[0]
            outside = -1 if turn > 0 else 1
            side = outside if abs(turn) > 0.035 else (-1 if pocket % 2 else 1)
            start = len(specs)
            # Six metres at each end taper into the existing shoulder. The earth bank
            # starts beyond a 70 cm recovery strip and rises roughly 15-30 cm.
            for j in range(-3, 4):
                taper = (0.50, 0.72, 0.93, 1.0, 0.93, 0.72, 0.50)[j + 3]
                add(road, i + j, side, 1.48, 'Berm',
                    [1.0, rng.uniform(0.92, 1.06), taper * rng.uniform(0.85, 1.10)], pocket)
            if road['id'] == 'SS1' and pocket % 3 == 0 or road['id'] == 'SS3' and pocket % 4 == 0:
                # Short, low field boundaries, with open ends. Never a continuous wall.
                for j in (-1, 0, 1):
                    add(road, i + j, side, 2.15, 'StoneBoundary', [1.0, 1.0, rng.uniform(0.85, 1.0)], pocket)
            else:
                for j in (-2, 1, 3):
                    mesh = rng.choice(['RockA', 'RockB', 'RockC'])
                    scale = rng.uniform(0.82, 1.12)
                    add(road, i + j, side, rng.uniform(2.05, 2.60), mesh,
                        [scale, scale, scale], pocket, rng.uniform(-35, 35))
                if road['id'] == 'SS2':
                    add(road, i, side, 3.1, 'Stump', [1.0, 1.0, rng.uniform(0.85, 1.15)], pocket)
            # Occasional visible apex rock, further back than the recovery strip.
            if abs(turn) > 0.08 and pocket % 3 == 1:
                add(road, i, -side, 1.8, 'RockB', [0.95, 0.95, 0.9], pocket, 15)
            if len(specs) > start:
                pockets.append(dict(stage=road['id'], index=pocket, sample=i, side=side,
                                    position=pts[i], tangent=tangent(pts, i), count=len(specs) - start))
            pocket += 1
            i += rng.randint(29, 36)
    if not pockets or not specs:
        raise RuntimeError('No roadside pockets generated')
    return dict(version=1, seed=SEED, map='/Game/Maps/Countryside/L_Countryside',
                tag=TAG, minimum_road_clearance_m=CLEARANCE, pockets=pockets, objects=specs,
                counts=dict(Counter(s['mesh'] for s in specs)),
                stages=dict(Counter(p['stage'] for p in pockets)))


if __name__ == '__main__':
    result = make_layout()
    ART.mkdir(parents=True, exist_ok=True)
    (ART / 'layout.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k: result[k] for k in ('seed', 'minimum_road_clearance_m', 'counts', 'stages')}, indent=2))
