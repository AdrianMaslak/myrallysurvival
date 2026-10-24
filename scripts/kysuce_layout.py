"""Kysuce rally: three stages on a real road, Oravska Lesna to the Bystrica valley (docs/KYSUCE.md).

Inputs: Art/Maps/Kysuce/Source/route.gpx (the road) and the Copernicus GLO-30 elevation around it, cut
to Source/dem.bin on the first run. Outputs the stage centre lines (KysuceStages.inl), route.json,
props.json, the 16-bit Landscape heightmap and an overview drawing. Units are metres; X east, Y south
(Unreal's frame), Z up. The origin is the rally HQ at the start of the GPX, at its road height.
Needs numpy (and tifffile to cut the elevation the first time).
"""
import json
import math
import random
import re
import urllib.request
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
ART = ROOT/'Art/Maps/Kysuce'
SOURCE = ART/'Source'
MAP = '/Game/Maps/Kysuce/L_Kysuce'
EARTH = 6371000.0
COS_LAT = math.cos(math.radians(49.31))
SIZE = 2017
WIDTH = 6.2
# Distance along the GPX (m) where each piece starts. Stage finishes and the next starts sit 150 m either
# side of a service park; the HQ is at the GPX start.
SERVICES = [12600.0, 22490.0]
# Surfaces inside each stage, from metres along the GPX. SS1's road above SNOW_ABOVE_M is snow.
SNOW_ABOVE_M = 900.0
SS3_SURFACES = [(0.0, 'Gravel'), (26000.0, 'Mud'), (29000.0, 'Gravel'), (32300.0, 'Tarmac')]
# Placeholder land use along the GPX (m): the scenery placed beside the road. Adjust to the real road.
# Road edges by land use: ditch depth (m) before a bank, and the lowest bank either side (m above the road),
# so the road runs in a U unless the ground really drops away. Unevenness of each surface (cm).
DITCH = dict(Village=.25, Farm=.6, Forest=.85)
LOW_BANK = dict(Village=0.0, Farm=.4, Forest=.8)
BUMP_CM = dict(Tarmac=2.5, Snow=3.5, Gravel=6.0, Mud=7.0)
ZONES = [(0.0, 'Village'), (1300.0, 'Farm'), (2200.0, 'Forest'), (11300.0, 'Village'), (13600.0, 'Farm'),
         (15000.0, 'Forest'), (17200.0, 'Village'), (19000.0, 'Farm'), (21300.0, 'Village'), (23300.0, 'Farm'),
         (25200.0, 'Forest'), (32900.0, 'Farm'), (34700.0, 'Village')]


def read_gpx():
    text = (SOURCE/'route.gpx').read_text(encoding='utf-8')
    raw = [(float(a), float(b)) for a, b in re.findall(r'<trkpt lat="([-\d.]+)" lon="([-\d.]+)"', text)]
    lat0, lon0 = raw[0]
    points = []
    for lat, lon in raw:
        p = (to_x(lon, lon0), to_y(lat, lat0))
        if not points or math.dist(p, points[-1]) > .5: points.append(p)
    return np.array(points), lat0, lon0


def to_x(lon, lon0): return (lon-lon0)*math.pi/180*EARTH*COS_LAT
def to_y(lat, lat0): return -(lat-lat0)*math.pi/180*EARTH


def to_lonlat(x, y, lat0, lon0):
    return lon0+x/(EARTH*COS_LAT)*180/math.pi, lat0-y/EARTH*180/math.pi


def catmull_rom(points, step=1.0):
    """Centripetal Catmull-Rom through the GPX points, so the corners are curves, not kinks."""
    p = np.vstack([2*points[0]-points[1], points, 2*points[-1]-points[-2]])
    out = []
    for i in range(1, len(p)-2):
        p0, p1, p2, p3 = p[i-1], p[i], p[i+1], p[i+2]
        t1 = np.linalg.norm(p1-p0)**.5; t2 = t1+np.linalg.norm(p2-p1)**.5; t3 = t2+np.linalg.norm(p3-p2)**.5
        n = max(1, math.ceil(np.linalg.norm(p2-p1)/step))
        for t in np.linspace(t1, t2, n, endpoint=False):
            a1 = (t1-t)/t1*p0+t/t1*p1
            a2 = (t2-t)/(t2-t1)*p1+(t-t1)/(t2-t1)*p2
            a3 = (t3-t)/(t3-t2)*p2+(t-t2)/(t3-t2)*p3
            b1 = (t2-t)/t2*a1+t/t2*a2
            b2 = (t3-t)/(t3-t1)*a2+(t-t1)/(t3-t1)*a3
            out.append((t2-t)/(t2-t1)*b1+(t-t1)/(t2-t1)*b2)
    out.append(points[-1])
    return np.array(out)


def smooth(values, sigma):
    """Gaussian along the line; the ends are mirrored through themselves so they stay put."""
    r = int(3*sigma)
    k = np.exp(-.5*(np.arange(-r, r+1)/sigma)**2); k /= k.sum()
    head = 2*values[0]-values[r:0:-1]; tail = 2*values[-1]-values[-2:-r-2:-1]
    padded = np.concatenate([head, values, tail])
    return np.convolve(padded, k, mode='valid')


def resample(points, spacing):
    d = np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(points, axis=0), axis=1))])
    count = math.ceil(d[-1]/spacing)
    at = np.linspace(0, d[-1], count+1)
    return np.stack([np.interp(at, d, points[:, k]) for k in range(points.shape[1])], axis=1)


def along(points):
    return np.concatenate([[0], np.cumsum(np.linalg.norm(np.diff(points, axis=0), axis=1))])


class Elevation:
    """Copernicus GLO-30 (1 arc second, heights at pixel centres), cut to the map and kept in Source."""
    def __init__(self, lon_range, lat_range):
        info = SOURCE/'dem.json'
        if not info.exists(): self.fetch(lon_range, lat_range)
        meta = json.loads(info.read_text())
        self.west, self.north, self.step = meta['west'], meta['north'], meta['step']
        self.h = np.fromfile(SOURCE/'dem.bin', dtype='<i2').reshape(meta['rows'], meta['cols'])/10.0

    @staticmethod
    def fetch(lon_range, lat_range):
        import tifffile
        step = 1/3600
        west = math.floor(lon_range[0]*3600)/3600; east = math.ceil(lon_range[1]*3600)/3600
        south = math.floor(lat_range[0]*3600)/3600; north = math.ceil(lat_range[1]*3600)/3600
        rows = round((north-south)/step)+1; cols = round((east-west)/step)+1
        lats = north-np.arange(rows)*step; lons = west+np.arange(cols)*step
        tile_lat = np.floor(lats-1e-9).astype(int); tile_row = np.rint((tile_lat+1-lats)/step).astype(int)
        tile_lon = np.floor(lons+1e-9).astype(int); tile_col = np.rint((lons-tile_lon)/step).astype(int)
        out = np.zeros((rows, cols))
        cache_dir = ROOT/'Saved/DEM'; cache_dir.mkdir(parents=True, exist_ok=True)
        for lat in np.unique(tile_lat):
            for lon in np.unique(tile_lon):
                name = f'Copernicus_DSM_COG_10_N{lat:02d}_00_E{lon:03d}_00_DEM'
                cache = cache_dir/(name+'.tif')
                if not cache.exists():
                    print('downloading', name)
                    urllib.request.urlretrieve(f'https://copernicus-dem-30m.s3.amazonaws.com/{name}/{name}.tif', cache)
                tile = tifffile.imread(cache)
                assert tile.shape == (3600, 3600), tile.shape
                rm = tile_lat == lat; cm = tile_lon == lon
                out[np.ix_(rm, cm)] = tile[np.ix_(tile_row[rm], tile_col[cm])]
        np.rint(out*10).astype('<i2').tofile(SOURCE/'dem.bin')
        (SOURCE/'dem.json').write_text(json.dumps(dict(west=west, north=north, step=step, rows=rows, cols=cols,
            source='Copernicus GLO-30 DSM', licence='Produced using Copernicus WorldDEM-30 (c) DLR e.V. 2010-2014 and '
            '(c) Airbus Defence and Space GmbH 2014-2018 provided under COPERNICUS by the European Union and ESA'), indent=2))

    def __call__(self, lon, lat):
        fr = (self.north-np.asarray(lat))/self.step; fc = (np.asarray(lon)-self.west)/self.step
        r = np.clip(np.floor(fr).astype(int), 0, self.h.shape[0]-2); c = np.clip(np.floor(fc).astype(int), 0, self.h.shape[1]-2)
        a = np.clip(fr-r, 0, 1); b = np.clip(fc-c, 0, 1)
        h = self.h
        return (h[r, c]*(1-a)*(1-b)+h[r+1, c]*a*(1-b)+h[r, c+1]*(1-a)*b+h[r+1, c+1]*a*b)


def split_count(length_m):
    # Mirrors Rally::Stage::SplitCount (StageRules.h).
    return min(3, int(length_m//4000))


def zone_at(d):
    return [z for start, z in ZONES if start <= d][-1]


def create():
    gpx, lat0, lon0 = read_gpx()
    plan = resample(catmull_rom(gpx), 1.0)
    plan = np.stack([smooth(plan[:, 0], 4), smooth(plan[:, 1], 4)], axis=1)
    plan = resample(plan, 2.0)
    d = along(plan)
    total = d[-1]

    # The landscape: a square around the road with 700 m to spare, its samples SPACING apart.
    lo = plan.min(axis=0)-700; hi = plan.max(axis=0)+700
    spacing = math.ceil(max(hi-lo)/(SIZE-1)*10)/10
    centre = (lo+hi)/2
    corner = centre-spacing*(SIZE-1)/2
    lons = [to_lonlat(x, 0, lat0, lon0)[0] for x in (corner[0]-200, corner[0]+spacing*(SIZE-1)+200)]
    lats = [to_lonlat(0, y, lat0, lon0)[1] for y in (corner[1]+spacing*(SIZE-1)+200, corner[1]-200)]
    dem = Elevation(lons, lats)

    lon, lat = to_lonlat(plan[:, 0], plan[:, 1], lat0, lon0)
    ground = dem(lon, lat)
    # A road's profile from 30 m surface heights (trees included): smoothed over about 100 m.
    z = smooth(ground, 25)
    # Level ground at the HQ, the service parks and the last park at the finish.
    parks = [0.0]+SERVICES+[total-40]
    for s in parks:
        i = int(np.searchsorted(d, s)); level = z[i]
        t = np.clip((np.abs(d-s)-40)/200, 0, 1); t = t*t*(3-2*t)
        z = level*(1-t)+z*t
    # No steeper than 12%: steeper steps pass the excess to their neighbours until none is left.
    step = np.diff(d)
    for _ in range(5000):
        excess = np.diff(z)-np.clip(np.diff(z), -.12*step, .12*step)
        if np.abs(excess).max() < 1e-3: break
        z[:-1] += excess/2; z[1:] -= excess/2
    datum = z[0]
    z = z-datum
    road = np.column_stack([plan, z])

    def piece(a, b):
        i, j = int(np.searchsorted(d, a)), int(np.searchsorted(d, b))
        return road[i:j+1]

    starts = [150.0, SERVICES[0]+150, SERVICES[1]+150]
    ends = [SERVICES[0]-150, SERVICES[1]-150, total-150]
    names = [('SS1', 'Oravska Lesna', 'Mixed'), ('SS2', 'Bystrica Valley', 'Gravel'), ('SS3', 'Kysuce Ridge', 'Mixed')]
    stages = []
    for (sid, name, surface), a, b in zip(names, starts, ends):
        p = piece(a, b)
        stages.append(dict(id=sid, name=name, surface=surface, width=WIDTH, from_m=a, to_m=b,
                           samples=p.round(4).tolist()))

    # Road sections, each one surface, sharing their end samples.
    def surface_at(i):
        if d[i] < starts[0] or d[i] > ends[2] or ends[0] < d[i] < starts[1] or ends[1] < d[i] < starts[2]: return 'Tarmac'
        if d[i] < ends[0]: return 'Snow' if z[i]+datum > SNOW_ABOVE_M else 'Tarmac'
        if d[i] < ends[1]: return 'Gravel'
        return [s for start, s in SS3_SURFACES if start <= d[i]][-1]
    labels = [surface_at(i) for i in range(len(road))]
    # Runs shorter than 150 m take the surface before them.
    runs = []
    for i, s in enumerate(labels):
        if runs and runs[-1][2] == s: runs[-1][1] = i
        else: runs.append([i, i, s])
    merged = []
    for r in runs:
        if merged and (d[r[1]]-d[r[0]] < 150 or r[2] == merged[-1][2]): merged[-1][1] = r[1]
        else: merged.append(r)
    verges = road_edges(road, d, z+datum, dem, lat0, lon0, parks)
    roads = []
    for n, (i, j, s) in enumerate(merged):
        i = merged[n-1][1] if n else i
        roads.append(dict(id=f'Road{n:02d}_{s}', surface=s, width=WIDTH, bump_cm=BUMP_CM[s], from_m=round(float(d[i]), 1),
                          samples=road[i:j+1].round(4).tolist(), verges=(verges[i:j+1]*100).round(1).tolist()))

    services = []
    for s in parks:
        i = int(np.searchsorted(d, s))
        services.append(road[i].round(3).tolist())

    # Heightmap: the elevation, the parks flat, and the road corridor fitted to the road's edges: well under
    # the road and its ditches, up to each bank's top behind it (11 to 22 m out), then the real ground.
    axis = np.arange(SIZE)*spacing
    gx, gy = np.meshgrid(corner[0]+axis, corner[1]+axis)
    glon, glat = to_lonlat(gx, gy, lat0, lon0)
    h = dem(glon, glat)-datum
    near = np.full(h.shape, np.inf); near_z = np.zeros(h.shape); near_bank = np.zeros(h.shape)
    reach = 80.0; cells = int(reach/spacing)+2
    fwd = np.gradient(road[:, :2], axis=0); fwd /= np.linalg.norm(fwd, axis=1)[:, None]
    for k in range(0, len(road), 2):
        x, y, rz = road[k]
        cx = int(round((x-corner[0])/spacing)); cy = int(round((y-corner[1])/spacing))
        r0, r1 = max(cy-cells, 0), min(cy+cells+1, SIZE); c0, c1 = max(cx-cells, 0), min(cx+cells+1, SIZE)
        ox, oy = gx[r0:r1, c0:c1]-x, gy[r0:r1, c0:c1]-y
        dist = np.hypot(ox, oy)
        closer = dist < near[r0:r1, c0:c1]
        bank = np.where(ox*-fwd[k, 1]+oy*fwd[k, 0] < 0, verges[k, 0], verges[k, 1])
        near[r0:r1, c0:c1][closer] = dist[closer]; near_z[r0:r1, c0:c1][closer] = rz
        near_bank[r0:r1, c0:c1][closer] = bank[closer]
    t = np.clip((near-11)/11, 0, 1); t = t*t*(3-2*t)
    level = (near_z+np.minimum(near_bank, 0)-1.5)*(1-t)+(near_z+near_bank)*t
    t = np.clip((near-22)/(reach-22), 0, 1); t = t*t*(3-2*t)
    h = level*(1-t)+h*t
    for px, py, pz in services:
        box = np.maximum(np.abs(gx-px), np.abs(gy-py))
        t = np.clip((box-38)/30, 0, 1); t = t*t*(3-2*t)
        h = (pz-.65)*(1-t)+h*t
    z_mid = round(float(h.min()+h.max())/2)
    z_scale = 200 if h.max()-h.min() < 1000 else 400
    encoded = np.rint((h-z_mid)*12800/z_scale+32768)
    assert encoded.min() > 0 and encoded.max() < 65535, (h.min(), h.max())
    landscape = dict(size=SIZE, spacing=spacing, origin=[float(corner[0]), float(corner[1]), z_mid], z_scale=z_scale)

    def height(x, y):
        fx = (x-corner[0])/spacing; fy = (y-corner[1])/spacing
        ix = min(max(int(fx), 0), SIZE-2); iy = min(max(int(fy), 0), SIZE-2); tx = fx-ix; ty = fy-iy
        return float((h[iy, ix]*(1-tx)+h[iy, ix+1]*tx)*(1-ty)+(h[iy+1, ix]*(1-tx)+h[iy+1, ix+1]*tx)*ty)

    stats = []
    for st in stages:
        p = np.array(st['samples']); seg = np.diff(p, axis=0)
        flat = np.hypot(seg[:, 0], seg[:, 1])
        # Grade over 20 m, as a car feels it.
        rise = p[10:, 2]-p[:-10, 2]; run = np.convolve(flat, np.ones(10), 'valid')
        stats.append(dict(id=st['id'], length_m=round(float(np.linalg.norm(seg, axis=1).sum()), 1),
                          max_grade=round(float(np.max(np.abs(rise/run))), 3),
                          ascent_m=round(float(np.clip(np.diff(p[:, 2]), 0, None).sum()), 1),
                          descent_m=round(float(np.clip(-np.diff(p[:, 2]), 0, None).sum()), 1),
                          splits=split_count(float(np.linalg.norm(seg, axis=1).sum()))))
    props = scenery(road, d, labels, verges, stages, services, height)
    return dict(map=MAP, origin_lat=lat0, origin_lon=lon0, datum_m=round(float(datum), 2), stages=stages,
                roads=roads, services=services, stats=stats, landscape=landscape), props, encoded


# Across a verge from the road's edge (m out): mirrors VergeOut and VergeZ in RallyRoad.cpp.
VERGE_OUT = [0.0, .4, .9, 1.5, 2.2, 5.2, 10.5, 12.5]


def verge_z(out, bank, ditch):
    if bank >= 0: z = [0.0, -.08, -ditch, -ditch, 0.0, bank, bank, bank-3]
    else: z = [0.0, -.08, -.15, -.2]+[-.2+(bank+.2)*min((o-1.5)/9, 1) for o in VERGE_OUT[4:7]]+[bank-3]
    return float(np.interp(out, VERGE_OUT, z))


def road_edges(road, d, ground_z, dem, lat0, lon0, parks):
    """Each sample's verges (m): left bank, right bank, ditch depth. A bank is the real ground 12 m out
    against the road, from 6 m below to 4 m above it; outside villages no lower than LOW_BANK (varied along
    the road), so there is a bank to hit unless the ground really drops away."""
    fwd = np.gradient(road[:, :2], axis=0); fwd /= np.linalg.norm(fwd, axis=1)[:, None]
    right = np.stack([-fwd[:, 1], fwd[:, 0]], axis=1)
    zones = [zone_at(x) for x in d]
    low = np.array([LOW_BANK[zn] for zn in zones])*(1+.6*np.sin(d/37)*np.sin(d/113+1))
    ditch = smooth(np.array([DITCH[zn] for zn in zones]), 10)
    out = np.zeros((len(road), 3))
    for column, side in ((0, -1), (1, 1)):
        q = road[:, :2]+right*side*12
        lon, lat = to_lonlat(q[:, 0], q[:, 1], lat0, lon0)
        bank = np.clip(dem(lon, lat)-ground_z, -6, 4)
        bank = np.where(bank > -1.5, np.maximum(bank, low), bank)
        bank = np.where([zn == 'Village' for zn in zones], np.clip(bank, 0, 1), bank)
        out[:, column] = smooth(bank, 8)
    out[:, 2] = ditch
    # Flat at the parks, and where the road comes back within 40 m of itself (the junction before SS3).
    keep = np.ones(len(road))
    for s in parks: keep = np.minimum(keep, np.clip((np.abs(d-s)-50)/70, 0, 1))
    for k in range(0, len(road), 5):
        others = np.abs(d-d[k]) > 150
        if np.any(np.hypot(*(road[others, :2]-road[k, :2]).T) < 40): keep[max(k-40, 0):k+41] = 0
    keep = smooth(keep, 10).clip(0, 1)
    return out*keep[:, None]


def scenery(road, d, labels, verges, stages, services, height):
    """Placeholder scenery from the land use table: spruce forest, farm fields, village houses, boulders on
    the verges, guardrails where the road's edge drops away, marker posts and the rally boards."""
    rng = random.Random(1996)
    props = []
    bins = {}
    for x, y, _ in road[::2]: bins.setdefault((int(x//32), int(y//32)), []).append((x, y))
    def clear(x, y, gap):
        bx, by = int(x//32), int(y//32)
        return all((x-a)**2+(y-b)**2 > gap*gap for i in range(bx-2, bx+3) for j in range(by-2, by+3) for a, b in bins.get((i, j), []))
    def away_from_parks(x, y, gap=60):
        return all(math.hypot(x-s[0], y-s[1]) > gap for s in services)
    def add(kind, x, y, z, yaw=0.0, scale=(1, 1, 1), **extra):
        props.append(dict(k=kind, p=[round(x, 2), round(y, 2), round(z, 2)], yaw=round(yaw, 1), s=[round(v, 2) for v in scale], **extra))
    def frame(i):
        a, b = road[max(i-2, 0)], road[min(i+2, len(road)-1)]
        dx, dy = b[0]-a[0], b[1]-a[1]; l = math.hypot(dx, dy)
        return dx/l, dy/l, -dy/l, dx/l   # forward, then right (Y is right of X)

    def ground(i, side, off):
        # The ground at a distance out from the road's centre: the verge, then the Landscape.
        x, y, z = road[i]; fx, fy, rx, ry = frame(i); out = off-WIDTH/2
        if out <= VERGE_OUT[-2]: return z+verge_z(max(out, 0), verges[i, 0 if side < 0 else 1], verges[i, 2])
        return height(x+rx*side*off, y+ry*side*off)

    for i in range(0, len(road)-1):
        x, y, z = road[i]; fx, fy, rx, ry = frame(i); yaw = math.degrees(math.atan2(fy, fx)); zone = zone_at(d[i])
        if not away_from_parks(x, y, 45): continue
        if i % 4 == 0:
            for side in (-1, 1):
                if zone == 'Forest' or (zone == 'Farm' and i % 40 == 0):
                    for _ in range(2 if zone == 'Forest' else 1):
                        off = side*rng.uniform(10, 75 if zone == 'Forest' else 40)
                        tx, ty = x+rx*off+fx*rng.uniform(-4, 4), y+ry*off+fy*rng.uniform(-4, 4)
                        if not clear(tx, ty, 9) or not away_from_parks(tx, ty, 40): continue
                        s = rng.uniform(.75, 1.4); tz = ground(i, side, abs(off))
                        add('Spruce', tx, ty, tz+6*s, rng.uniform(0, 360), (6*s, 6*s, 14*s))
                        if abs(off) < 40: add('Trunk', tx, ty, tz+2*s, 0, (.45*s, .45*s, 4*s))
        if zone == 'Village' and i % 18 == 0:
            for side in (-1, 1):
                if rng.random() < .25: continue
                off = side*rng.uniform(15, 22); hx, hy = x+rx*off, y+ry*off
                if not clear(hx, hy, 12) or not away_from_parks(hx, hy, 45): continue
                w, l, ht = rng.uniform(8, 11), rng.uniform(9, 13), rng.uniform(5, 7)
                hz = height(hx, hy)
                add('House', hx, hy, hz+ht/2-.5, yaw, (l, w, ht))
                add('Roof', hx, hy, hz+ht-.3, yaw, (l+.8, w+.8, 1.2))
                if rng.random() < .5: add('Fence', x+rx*side*(WIDTH/2+3), y+ry*side*(WIDTH/2+3), z+.25, yaw, (16, .08, 1.5))
        if zone == 'Farm' and i % 60 == 30:
            side = rng.choice((-1, 1)); off = side*rng.uniform(12, 30); bx, by = x+rx*off, y+ry*off
            if clear(bx, by, 9) and away_from_parks(bx, by, 40): add('Bale', bx, by, height(bx, by)+.6, rng.uniform(0, 180), (1.5, 1.5, 1.2))
        # Boulders on the verges outside villages: off the road at speed, something to hit.
        if zone != 'Village' and i % 20 == 10 and rng.random() < .35:
            side = rng.choice((-1, 1)); out = rng.uniform(2.2, 6.0); off = WIDTH/2+out
            s = rng.uniform(.7, 1.5)
            add('Boulder', x+rx*side*off+fx*rng.uniform(-3, 3), y+ry*side*off+fy*rng.uniform(-3, 3), ground(i, side, off)+.25*s,
                rng.uniform(0, 360), (s*1.2, s, s*.8))
        # White marker posts every 50 m both sides, 1.2 m above the road, on the shoulder before the ditch.
        if i % 25 == 0:
            for side in (-1, 1):
                add('Post', x+rx*side*(WIDTH/2+.45), y+ry*side*(WIDTH/2+.45), z+.35, yaw, (.12, .12, 1.7))
        # Steel guardrails beside tarmac and snow where the road's edge drops more than 2.5 m.
        if i % 4 == 0 and labels[i] in ('Tarmac', 'Snow') and zone != 'Village':
            for side in (-1, 1):
                if verges[i, 0 if side < 0 else 1] < -2.5:
                    j = min(i+4, len(road)-1); bx, by, bz = road[j]
                    ax, ay = x+rx*side*(WIDTH/2+.7), y+ry*side*(WIDTH/2+.7)
                    cx, cy = bx+rx*side*(WIDTH/2+.7), by+ry*side*(WIDTH/2+.7)
                    seg = math.hypot(cx-ax, cy-ay)
                    pitch = math.degrees(math.atan2(bz-z, seg))
                    add('Rail', (ax+cx)/2, (ay+cy)/2, (z+bz)/2+.6, yaw, (seg+.1, .12, .32), pitch=round(pitch, 2))
                    add('RailPost', ax, ay, z+.2, yaw, (.1, .1, 1.2))

    # Chevrons on the outside of tight bends, as the Highlands map.
    for i in range(20, len(road)-20, 10):
        a, p, b = road[i-10], road[i], road[i+10]
        inc = math.atan2(p[1]-a[1], p[0]-a[0]); out = math.atan2(b[1]-p[1], b[0]-p[0])
        turn = math.atan2(math.sin(out-inc), math.cos(out-inc))
        if abs(turn) < .5 or not away_from_parks(p[0], p[1], 45): continue
        side = -1 if turn > 0 else 1
        add('Chevron', p[0]-math.sin(inc)*side*(WIDTH/2+2.2), p[1]+math.cos(inc)*side*(WIDTH/2+2.2), p[2]+1.1,
            math.degrees(inc), (.12, 2.5, .8), text='<<<' if turn > 0 else '>>>')

    # Boards: stage starts and finishes (built by the importer), time controls, splits and the countdown to
    # the finish.
    for st in stages:
        p = np.array(st['samples']); sd = along(p)
        def board(at, label, colour):
            i = int(np.searchsorted(sd, at)); i = min(i, len(p)-2)
            fx, fy = p[i+1][0]-p[i][0], p[i+1][1]-p[i][1]; l = math.hypot(fx, fy); fx, fy = fx/l, fy/l
            x, y, z = p[i]
            add('Board', x-fy*(WIDTH/2+1.8), y+fx*(WIDTH/2+1.8), z+1.4, math.degrees(math.atan2(fy, fx)), (.1, 1.4, 1.0),
                text=label, colour=colour)
        board(0, 'TC '+st['id'], 'Yellow')
        n = split_count(float(sd[-1]))
        for k in range(n): board(sd[-1]*(k+1)/(n+1), f'SPLIT {k+1}', 'Blue')
        for m in (300, 200, 100): board(sd[-1]-m, str(m), 'Yellow')
    return props


def overview(data):
    pts = [p for r in data['roads'] for p in r['samples']]
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
    xmin, xmax, ymin, ymax = min(xs)-500, max(xs)+500, min(ys)-500, max(ys)+500
    scale = min(900/(xmax-xmin), 640/(ymax-ymin))
    def xy(p): return 50+(p[0]-xmin)*scale, 110+(p[1]-ymin)*scale
    colours = dict(Tarmac='#a8c1cf', Snow='#f4f4f4', Gravel='#d6bd86', Mud='#ac704b')
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="1000" viewBox="0 0 1000 1000">',
           '<rect width="1000" height="1000" fill="#16251d"/>',
           '<g font-family="Arial" fill="#eee9d8"><text x="45" y="48" font-size="30">KYSUCE / REAL ROAD RALLY</text>',
           '<text x="45" y="80" font-size="16">' + ' / '.join(f"{s['id']} {s['length_m']/1000:.1f} km" for s in data['stats'])
           + ' / north is up</text>']
    for r in data['roads']:
        coords = ' '.join(f'{x:.1f},{y:.1f}' for x, y in map(xy, r['samples'][::4]+[r['samples'][-1]]))
        svg.append(f'<polyline points="{coords}" fill="none" stroke="{colours[r["surface"]]}" stroke-width="4"/>')
    for st in data['stages']:
        x, y = xy(st['samples'][len(st['samples'])//2]); svg.append(f'<text x="{x+12:.1f}" y="{y:.1f}" font-size="20">{st["id"]}</text>')
    for i, p in enumerate(data['services']):
        x, y = xy(p); label = 'HQ' if i == 0 else f'Service {i}'
        svg.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="8" fill="#f2b738"/><text x="{x+14:.1f}" y="{y+5:.1f}" font-size="16">{label}</text>')
    svg.append('<text x="45" y="800" font-size="18">ELEVATION (m above the HQ) / 0-' + f'{sum(s["length_m"] for s in data["stats"])/1000:.1f} km of stages</text>')
    total = sum(s['length_m'] for s in data['stats']); dist = 0
    for st in data['stages']:
        coords = []; prev = st['samples'][0]
        for p in st['samples'][::5]:
            dist += math.dist(prev, p); prev = p
            coords.append(f'{45+dist/total*900:.1f},{900-p[2]*.25:.1f}')
        svg.append(f'<polyline points="{" ".join(coords)}" fill="none" stroke="#eee9d8" stroke-width="2"/>')
    for i, (name, colour) in enumerate(colours.items()):
        svg.append(f'<rect x="{45+i*120}" y="960" width="18" height="6" fill="{colour}"/><text x="{70+i*120}" y="967" font-size="14">{name}</text>')
    svg.append('</g></svg>')
    return '\n'.join(svg)


if __name__ == '__main__':
    data, props, encoded = create()
    ART.mkdir(parents=True, exist_ok=True)
    (ART/'route.json').write_text(json.dumps(data, separators=(',', ':')))
    (ART/'props.json').write_text(json.dumps(props, separators=(',', ':')))
    encoded.astype('<u2').tofile(ART/'Kysuce.r16')
    lines = ['// Generated by scripts/kysuce_layout.py. Coordinates in metres.']
    for st in data['stages']:
        name = 'Kysuce_'+st['id']
        lines.append('static const double '+name+'[][3] = {')
        lines.extend('    {'+', '.join(f'{v:.4f}' for v in p)+'},' for p in st['samples'])
        lines.append('};')
    lines.append('static const FRallyStageSource KysuceStages[] = {')
    for st in data['stages']:
        name = 'Kysuce_'+st['id']
        lines.append(f'    {{ TEXT("{st["id"]}"), TEXT("{st["name"]}"), TEXT("{st["surface"]}"), {st["width"]}, {name}, UE_ARRAY_COUNT({name}) }},')
    lines.append('};')
    (ROOT/'Source/RallySurvival/Private/Game/KysuceStages.inl').write_text('\n'.join(lines)+'\n')
    (ART/'Route_Overview.svg').write_text(overview(data))
    kinds = {}
    for p in props: kinds[p['k']] = kinds.get(p['k'], 0)+1
    print(json.dumps(dict(stats=data['stats'], landscape=data['landscape'], roads=[(r['id'], r['from_m']) for r in data['roads']],
                          props=kinds), indent=2))
