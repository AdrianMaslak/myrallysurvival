"""Stage 2: make the body clear the v2 4x4 suspension, the tyres' swept volumes and the engine set.

Method (Chassis): a clearance field is built on a 1.5 cm grid under and around the body,
    F(p) = min over parts of (signed distance to the part - its clearance)
with the tyres taken at every steer/travel pose (15 mm), struts, springs, dampers, hubs, brakes, the final drive and
the engine pieces as convex hulls (10 mm), and thin parts (subframes, links, trailing arms, shafts, rack, anti-roll
bars, arms) by their own surfaces (10 mm). Every chassis panel face that dips into F < 0 is removed, and the F = 0
surface (marching cubes) is welded in where those faces were: that surface is the new floor pocket, final-drive
tunnel, spring/strut tower and wheel tub, exactly 10 / 15 mm off the parts. Pieces of it outside the body (round
the tyres below the sills, round the engine in the open bay) are dropped.
The front wings, bumpers and doors are nudged off the field (outer skin moves at most 4 mm). The strut towers get a
flat seat on the v2 top mounts and a hole for the rod and centre nut. Writes work/stage2.blend.
"""
import bpy, bmesh, sys, os, math, json, time
import numpy as np
from skimage import measure
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools")); sys.path.insert(0, "/home/claude/octavia_susp")
import refine, classify
from clearance import wheel_poses, HP
import wheel_profile as WP
from mathutils import Vector, Matrix, kdtree
from mathutils.bvhtree import BVHTree

t_start = time.time()
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "work", "stage1.blend"))
O = bpy.data.objects
REF = [o for o in bpy.data.collections["REFERENCE_DoNotExport"].objects if o.type == 'MESH']
CLR, CLR_T = 0.017, 0.019          # 10 / 15 mm plus 2 mm for the faceting of the field
H = 0.0125
ISO_RATIO = 0.12
LO = np.array([-0.97, -2.05, -0.06]); HI = np.array([0.97, 2.15, 0.98])
shape = tuple(int(math.ceil((HI[i] - LO[i]) / H)) + 1 for i in range(3))
F = np.full(shape, 1.0, dtype=np.float32)
print("grid", shape, flush=True)

THIN = ("RearSubframe", "TrailingArm", "UpperLink", "LowerLink", "Driveshaft", "CVBoot", "FrontSubframe", "SteeringRack",
        "AntiRollBar", "LowerArm", "TieRod", "BrakeHose")
HULL = ("Strut_", "Spring_", "Damper_", "Knuckle_", "HubCarrier_", "BrakeDisc_", "BrakeCaliper_", "CaliperCarrier_",
        "Rim_", "RearFinalDrive", "CVJoint")


def grid_box(lo, hi):
    i0 = np.clip(np.floor((np.array(lo) - LO) / H).astype(int), 0, np.array(shape) - 1)
    i1 = np.clip(np.ceil((np.array(hi) - LO) / H).astype(int), 0, np.array(shape) - 1)
    return i0, i1


def apply_tree(tree, lo, hi, clr, signed):
    i0, i1 = grid_box(np.array(lo) - clr - 0.04, np.array(hi) + clr + 0.04)
    for a in range(i0[0], i1[0] + 1):
        x = LO[0] + H * a
        for b in range(i0[1], i1[1] + 1):
            y = LO[1] + H * b
            for c in range(i0[2], i1[2] + 1):
                p = Vector((x, y, LO[2] + H * c))
                loc, n, fi, d = tree.find_nearest(p, clr + 0.04)
                if loc is None: continue
                sd = -d if (signed and (p - loc).dot(n) < 0) else d
                v = sd - clr
                if v < F[a, b, c]: F[a, b, c] = v


def hull_tree(vs):
    bm = bmesh.new()
    for v in vs: bm.verts.new(v)
    bmesh.ops.convex_hull(bm, input=bm.verts)
    bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
    bm.normal_update(); t = BVHTree.FromBMesh(bm); bm.free(); return t


def obj_tree(r):
    bm = bmesh.new(); bm.from_mesh(r.data); bm.transform(r.matrix_world); t = BVHTree.FromBMesh(bm); bm.free(); return t


for r in REF:
    n = r.name
    if any(k in n for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_", "Tyre_")): continue
    vs = [r.matrix_world @ v.co for v in r.data.vertices]
    if not vs: continue
    lo = [min(v[i] for v in vs) for i in range(3)]; hi = [max(v[i] for v in vs) for i in range(3)]
    if n.startswith("REF_UCX_SM_Engine18T"):
        apply_tree(hull_tree(vs), lo, hi, CLR, True)
    elif n.startswith("REF_SM_Engine18T"):
        if n.endswith(("Engine", "Turbo", "Intercooler", "Intake", "ExhaustManifold", "AirFilter")):
            apply_tree(obj_tree(r), lo, hi, CLR, False)      # their surface (the UCX pieces give the inside)
            continue
        apply_tree(obj_tree(r) if n.endswith("Bay") else hull_tree(vs), lo, hi, CLR, not n.endswith("Bay"))
    elif n.startswith("REF_SUSP_"):
        if any(k in n for k in HULL):
            apply_tree(hull_tree(vs), lo, hi, CLR, True)
        elif any(k in n for k in THIN):
            apply_tree(obj_tree(r), lo, hi, CLR, False)
print(f"parts done {time.time()-t_start:.0f}s", flush=True)

# ---- tyres: analytic signed distance to the revolved profile, at every pose
poly2 = np.array(WP.TYRE_PROFILE)                 # (r, a), a < 0 outboard
def sd_polygon(P, poly):
    d = np.full(len(P), 1e9); inside = np.zeros(len(P), bool)
    for k in range(len(poly)):
        a = poly[k]; b = poly[(k + 1) % len(poly)]
        ab = b - a; ap = P - a
        t = np.clip((ap @ ab) / (ab @ ab), 0, 1)
        q = a + t[:, None] * ab
        d = np.minimum(d, np.linalg.norm(P - q, axis=1))
        cond = ((a[1] > P[:, 1]) != (b[1] > P[:, 1])) & (P[:, 0] < (b[0] - a[0]) * (P[:, 1] - a[1]) / (b[1] - a[1] + 1e-12) + a[0])
        inside ^= cond
    return np.where(inside, -d, d)
for side in ("FL", "FR", "RL", "RR"):
    ty = O[f"REF_SUSP_Tyre_{side}"]
    W = np.array(ty.matrix_world.translation)
    i0, i1 = grid_box(W - 0.55, W + 0.55)
    gx, gy, gz = np.meshgrid(*[LO[k] + H * np.arange(i0[k], i1[k] + 1) for k in range(3)], indexing="ij")
    P = np.stack([gx.ravel(), gy.ravel(), gz.ravel(), np.ones(gx.size)], 1)
    sub = np.full(gx.shape, 1.0, np.float32)
    for pose, M in wheel_poses(side):
        L = np.array((M @ ty.matrix_world).inverted())
        Q = P @ L.T
        a = Q[:, 0]                                  # tyre mesh local X: the right side is mirrored in the mesh itself
        rr = np.sqrt(Q[:, 1] ** 2 + Q[:, 2] ** 2)
        sd = np.minimum(sd_polygon(np.stack([rr, a], 1), poly2), sd_polygon(np.stack([rr, -a], 1), poly2))
        sub = np.minimum(sub, (sd.reshape(gx.shape) - CLR_T).astype(np.float32))
    sl = tuple(slice(i0[k], i1[k] + 1) for k in range(3))
    F[sl] = np.minimum(F[sl], sub)
    print(f"  tyre {side}  {time.time()-t_start:.0f}s", flush=True)
np.save(os.path.join(ROOT, "work", "field.npy"), F)
json.dump(dict(lo=LO.tolist(), h=H, shape=shape), open(os.path.join(ROOT, "work", "field.json"), "w"))


def field_at(p):
    g = (np.array(p) - LO) / H
    i = np.floor(g).astype(int)
    if np.any(i < 0) or np.any(i >= np.array(shape) - 1): return 1.0
    f = g - i
    c = F[i[0]:i[0] + 2, i[1]:i[1] + 2, i[2]:i[2] + 2]
    c = c[0] * (1 - f[0]) + c[1] * f[0]; c = c[0] * (1 - f[1]) + c[1] * f[1]
    return float(c[0] * (1 - f[2]) + c[1] * f[2])


# ---- Chassis: remove panel faces in F < 0, weld in the F = 0 surface where they were
ch = O["Chassis"]
mats = [m.name for m in ch.data.materials]
PAINT = mats.index("KIT_octavia1u"); UNDER = mats.index("KIT_octavia1u_undertray")
bm = bmesh.new(); bm.from_mesh(ch.data); bm.transform(ch.matrix_world); bm.normal_update()
kit = [o for o in bpy.data.collections["BODY_KIT"].objects if o.type == 'MESH']
otree = classify.scene_tree(kit)
bm.faces.ensure_lookup_table()
oflag = classify.outer_flags(bm.faces, otree)
OUT = bm.faces.layers.int.new("outer")
def silhouette(f, fl):
    """painted skin seen from outside the car, facing away from the car's long axis (not underbody, not arch flanges)"""
    if not (fl and f.material_index == PAINT): return 0
    c = f.calc_center_median(); n = f.normal
    radial = Vector((c.x, 0, c.z - 0.6))
    if radial.length < 1e-6: return 0
    if abs(n.x) > 0.6 and abs(c.x) < 0.78: return 0          # faces inside the arches look at the tyre, not out
    return 1 if (n.z > -0.5 and n.dot(radial.normalized()) > 0.3) else 0
for f, fl in zip(bm.faces, oflag): f[OUT] = silhouette(f, fl)
def near_cut(f):
    if f[OUT]: return None
    c = f.calc_center_median()
    return 0.03 if field_at(c) < 0.03 + f.calc_perimeter() / 3 else None
refine.refine(bm, near_cut, cap=0.0, join=False)
bm.faces.ensure_lookup_table(); bm.normal_update()
kill = []
for f in bm.faces:
    if f[OUT]: continue                                  # silhouette skin stays (checked in the report)
    pts = [v.co for v in f.verts] + [f.calc_center_median()] + [(e.verts[0].co + e.verts[1].co) / 2 for e in f.edges]
    if min(field_at(p) for p in pts) < -0.0005: kill.append(f)
removed_centres = [f.calc_center_median() for f in kill]
removed_mat = [f.material_index for f in kill]
bmesh.ops.delete(bm, geom=kill, context='FACES')
bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
print(f"removed {len(kill)} chassis faces inside the clearance field", flush=True)
# the refinement only split flat faces: merge the coplanar pieces of the inner panels back (keeps the triangle count down)
inner = [f for f in bm.faces if not f[OUT] and f.material_index != PAINT]
r = bmesh.ops.dissolve_limit(bm, angle_limit=math.radians(0.5), verts=list({v for f in inner for v in f.verts}),
                             edges=list({e for f in inner for e in f.edges}), delimit={'MATERIAL'})
ng = [f for f in bm.faces if len(f.verts) > 4]
bmesh.ops.triangulate(bm, faces=ng, quad_method='BEAUTY', ngon_method='BEAUTY')
print("inner panels re-merged:", len(bm.faces), "faces", flush=True)

verts, faces, normals, _ = measure.marching_cubes(F, level=0.0, spacing=(H, H, H), allow_degenerate=False)
verts = verts + LO
print("iso surface", len(verts), "verts", len(faces), "tris", flush=True)
kd = kdtree.KDTree(max(1, len(removed_centres)))
for i, c in enumerate(removed_centres): kd.insert(c, i)
kd.balance()
keep = []
for tri in faces:
    c = Vector(verts[tri].mean(0))
    co, idx, d = kd.find(c)
    if idx is None or d > 0.16: continue
    side = 1 if c.x >= 0 else -1
    if otree.ray_cast(c, Vector((side, 0, 0)), 0.9)[0] is None: continue        # outside the body sideways
    if otree.ray_cast(c, Vector((0, 0, 1)), 1.6)[0] is None: continue           # nothing above: outside
    if c.y > 0.85 and c.z > 0.28 and abs(c.x) < 0.60: continue                  # engine bay: never wrap the engine, just clear it
    keep.append((tri, removed_mat[idx]))
# decimate the iso surface (1.25 cm triangles) down to a panel-like density before welding it in
iso = bmesh.new(); vmap = {}
for tri, mi in keep:
    vs = []
    for k in tri:
        if k not in vmap: vmap[k] = iso.verts.new(Vector(verts[k]))
        vs.append(vmap[k])
    try:
        f = iso.faces.new(vs); f.material_index = mi if mi != PAINT else UNDER
    except ValueError: pass
bmesh.ops.remove_doubles(iso, verts=iso.verts, dist=0.0005)
tm = bpy.data.meshes.new("iso"); iso.to_mesh(tm); iso.free()
for m in ch.data.materials: tm.materials.append(m)
to = bpy.data.objects.new("iso", tm); bpy.context.scene.collection.objects.link(to)
md = to.modifiers.new("d", 'DECIMATE'); md.ratio = ISO_RATIO; md.use_collapse_triangulate = True
dg = bpy.context.evaluated_depsgraph_get(); em = to.evaluated_get(dg).to_mesh()
n_before = len(keep); n_after = len(em.polygons)
off = len(bm.verts); vlist = [bm.verts.new(v.co) for v in em.vertices]
for p in em.polygons:
    try:
        f = bm.faces.new([vlist[i] for i in p.vertices]); f.material_index = p.material_index
    except ValueError: pass
to.evaluated_get(dg).to_mesh_clear(); bpy.data.objects.remove(to); bpy.data.meshes.remove(tm)
print(f"iso surface decimated {n_before} -> {n_after} triangles", flush=True)
# zip: open borders left by the cut snap onto the nearest pocket/tub vertex (closes the seam, smooths the ragged edge)
isoset = set(vlist)
kd2 = kdtree.KDTree(len(vlist))
for i, v in enumerate(vlist): kd2.insert(v.co, i)
kd2.balance()
snapped = 0
for v in list(bm.verts):
    if v in isoset or not v.is_boundary: continue
    if any(f[OUT] for f in v.link_faces): continue
    co, i, d = kd2.find(v.co)
    if i is not None and d < 0.03:
        v.co = co.copy(); snapped += 1
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0015)
print(f"zipped {snapped} border vertices onto the pockets", flush=True)
print(f"welded in {len(keep)} iso-surface triangles", flush=True)

# ---- strut towers: flat seat on the v2 top mount, hole for the rod and centre nut
SEAT = bm.faces.layers.int.new("seat")
for sx in (-1, 1):
    top = Vector((HP["F_TOP"].x * sx, HP["F_TOP"].y, HP["F_TOP"].z)); bj = Vector((HP["F_BJ"].x * sx, HP["F_BJ"].y, HP["F_BJ"].z))
    u = (top - bj).normalized()
    a0 = u.orthogonal().normalized(); a1 = u.cross(a0)
    def radial(p):
        d = p - top; return (d - u * d.dot(u)).length
    kill = [f for f in bm.faces if radial(f.calc_center_median()) < 0.100 and -0.04 < (f.calc_center_median() - top).dot(u) < 0.12]
    bmesh.ops.delete(bm, geom=kill, context='FACES')
    seat = top + u * 0.0015
    segs = 32
    ring = lambda r, h: [bm.verts.new(seat + u * h + (a0 * math.cos(2 * math.pi * k / segs) + a1 * math.sin(2 * math.pi * k / segs)) * r) for k in range(segs)]
    r_hole, r_seat, r_wall = 0.034, 0.090, 0.104
    L_hole = ring(r_hole - 0.002, -0.008); L_in = ring(r_hole, 0.0); L_out = ring(r_seat, 0.0)
    L_sh = ring(r_wall, -0.012); L_w = ring(r_wall + 0.004, -0.050)
    for A, B in ((L_hole, L_in), (L_in, L_out), (L_out, L_sh), (L_sh, L_w)):
        for k in range(segs):
            k2 = (k + 1) % segs
            try:
                f = bm.faces.new((A[k], A[k2], B[k2], B[k])); f.material_index = UNDER
                if B is L_in or B is L_out: f[SEAT] = 1             # the seat itself sits on the top-mount plate
            except ValueError: pass
# the new tower faces must clear everything too (the game's ECU sits beside the left tower); the seat may touch the plate
bm.faces.ensure_lookup_table()
others = []
for r in REF:
    if r.name.startswith(("REF_SUSP_Strut", "REF_SUSP_Spring")) or any(k in r.name for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_")): continue
    if not r.name.startswith(("REF_SM_", "REF_SUSP_")): continue
    others.append(hull_tree([r.matrix_world @ v.co for v in r.data.vertices]) if r.data.vertices else None)
others = [t for t in others if t]
def seat_clash(f):
    for p in [v.co for v in f.verts] + [f.calc_center_median()]:
        for t in others:
            h = t.find_nearest(p, 0.0125)
            if h[0] is not None: return True
    return False
clash_seat = [f for f in bm.faces if f[SEAT] and seat_clash(f)]
bmesh.ops.delete(bm, geom=clash_seat, context='FACES')
print(f"seat faces dropped beside other parts: {len(clash_seat)}", flush=True)
clash = [f for f in bm.faces if f.material_index == UNDER and not f[SEAT] and min(field_at(p) for p in [v.co for v in f.verts] + [f.calc_center_median()]) < -0.004]
bmesh.ops.delete(bm, geom=clash, context='FACES')
print(f"tower faces dropped where they would cut a part: {len(clash)}", flush=True)
bm.normal_update()
bm.transform(ch.matrix_world.inverted()); bm.to_mesh(ch.data); bm.free()
for p in ch.data.polygons: p.use_smooth = True
try: ch.data.set_sharp_from_angle(angle=math.radians(40))
except Exception: pass


def nudge(ob, cap=0.004, skip=None):
    """push vertices that sit in F < 0 up the field gradient; each vertex moves at most `cap` in total."""
    bm = bmesh.new(); bm.from_mesh(ob.data); bm.transform(ob.matrix_world)
    total = {}; n = 0
    skipset = skip(bm) if skip else set()
    for it in range(12):
        moved = 0
        for v in bm.verts:
            if v.index in skipset: continue
            f = field_at(v.co)
            if f >= 0: continue
            used = total.get(v.index, 0.0)
            if used >= cap - 1e-6: continue
            g = np.array([field_at(v.co + Vector(d) * 0.004) - field_at(v.co - Vector(d) * 0.004) for d in ((1, 0, 0), (0, 1, 0), (0, 0, 1))])
            if np.linalg.norm(g) < 1e-9: continue
            g = Vector(g / np.linalg.norm(g))
            step = min(-f + 0.0005, cap - used)
            v.co += g * step; total[v.index] = used + step; moved += 1
        n += moved
        if not moved: break
    bm.transform(ob.matrix_world.inverted()); bm.to_mesh(ob.data); bm.free()
    return dict(nudged=len(total), max_mm=round(max(total.values(), default=0) * 1000, 1))

def keep_still(b):
    lo_, ls_ = b.faces.layers.int.get("outer"), b.faces.layers.int.get("seat")
    return {v.index for v in b.verts if any((lo_ and f[lo_]) or (ls_ and f[ls_]) for f in v.link_faces)}
repz = nudge(O["Chassis"], cap=0.03, skip=keep_still); print("Chassis inner nudge", repz, flush=True)
def not_skin(b):
    lo_ = b.faces.layers.int.get("outer")
    return {v.index for v in b.verts if not any(lo_ and f[lo_] for f in v.link_faces)}
rep0 = nudge(O["Chassis"], cap=0.0075, skip=not_skin); print("Chassis skin nudge", rep0, flush=True)
rep = {"chassis_skin_nudge": rep0, "chassis_faces_removed": len(removed_centres), "iso_tris_added": len(keep)}
for nme in ("Fender_FL", "Fender_FR", "FrontBumper", "RearBumper", "Door_FL", "Door_FR", "Door_RL", "Door_RR"):
    rep[nme] = nudge(O[nme]); print(nme, rep[nme], flush=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, "work", "stage2.blend"), compress=True)
json.dump(rep, open(os.path.join(ROOT, "work", "stage2_report.json"), "w"), indent=1)
print("saved", f"{time.time()-t_start:.0f}s")
