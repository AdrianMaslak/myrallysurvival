"""Stage 2b: exact clean-up after the field cut. Body vertices that are still closer than the clearance to the real
reference meshes (tyres at every pose 15 mm, parts 10 mm) are pushed straight away from the nearest point. The strut
top-mount seat is allowed to touch. Outer skin moves at most 7.5 mm in total (arch return flanges); inner panels 30 mm.
"""
import bpy, bmesh, sys, os, math
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from clearance import wheel_poses
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils import kdtree

bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "work", "stage2.blend"))
O = bpy.data.objects
ty_targets = []
for side in ("FL", "FR", "RL", "RR"):
    ty = O[f"REF_SUSP_Tyre_{side}"]
    for pose, M in wheel_poses(side):
        vs = [M @ ty.matrix_world @ v.co for v in ty.data.vertices]
        ty_targets.append((BVHTree.FromPolygons(vs, [tuple(p.vertices) for p in ty.data.polygons]), 0.0155, sum(vs, Vector()) / len(vs), 0.4, vs))
part_targets = []
for r in bpy.data.collections["REFERENCE_DoNotExport"].objects:
    if r.type != 'MESH' or r.name.startswith("REF_UCX") or "Tyre" in r.name: continue
    if any(k in r.name for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_", "Strut_Upper")): continue
    vs = [r.matrix_world @ v.co for v in r.data.vertices]
    if not vs: continue
    lo = Vector([min(v[i] for v in vs) for i in range(3)]); hi = Vector([max(v[i] for v in vs) for i in range(3)])
    part_targets.append((BVHTree.FromPolygons(vs, [tuple(p.vertices) for p in r.data.polygons]), 0.0105, (lo + hi) / 2, (hi - lo).length / 2 + 0.03, vs))
T = ty_targets + part_targets

for name in ("Chassis", "Fender_FL", "Fender_FR"):
    ob = O[name]
    bm = bmesh.new(); bm.from_mesh(ob.data); bm.transform(ob.matrix_world); bm.verts.ensure_lookup_table()
    lo_, ls_ = bm.faces.layers.int.get("outer"), bm.faces.layers.int.get("seat")
    skin = {v.index for v in bm.verts if any(lo_ and f[lo_] for f in v.link_faces)} if lo_ else set()
    if name.startswith("Fender"): skin = {v.index for v in bm.verts}
    seat = {v.index for v in bm.verts if any(ls_ and f[ls_] for f in v.link_faces)} if ls_ else set()
    used = {}
    for it in range(6):
        kd = kdtree.KDTree(len(bm.verts))
        for i, v in enumerate(bm.verts): kd.insert(v.co, i)
        kd.balance()
        n = 0
        for tree, clr, c, rad, tvs in T:
            for (co, i, d) in kd.find_range(c, rad + clr):
                if i in seat: continue
                v = bm.verts[i]
                loc, nrm, fi, dist = tree.find_nearest(v.co, clr)
                if loc is None: continue
                cap = 0.0075 if i in skin else 0.03
                left = cap - used.get(i, 0.0)
                if left <= 0: continue
                dirv = (v.co - loc)
                if dirv.length < 1e-6 or dirv.dot(nrm) < 0: dirv = nrm.copy()
                step = min(clr + 0.0005 - dist if dirv.dot(nrm) >= 0 else clr + dist, left)
                v.co += dirv.normalized() * step; used[i] = used.get(i, 0.0) + step; n += 1
        # the other way round: target vertices close to a body face push that face's vertices away
        btree = BVHTree.FromBMesh(bm); bm.faces.ensure_lookup_table()
        for tree, clr, c, rad, tvs in T:
            for w in tvs:
                loc, nrm, fi, dist = btree.find_nearest(w, clr)
                if loc is None: continue
                dirv = loc - w
                if dirv.length < 1e-7: continue
                dirv.normalize()
                for v in bm.faces[fi].verts:
                    i = v.index
                    if i in seat: continue
                    cap = 0.0075 if i in skin else 0.03
                    left = cap - used.get(i, 0.0)
                    if left <= 0: continue
                    step = min(clr + 0.0005 - dist, left)
                    v.co += dirv * step; used[i] = used.get(i, 0.0) + step; n += 1
        if not n: break
        print(name, "pass", it, "pushed", n, flush=True)
    bm.transform(ob.matrix_world.inverted()); bm.to_mesh(ob.data); bm.free()
    print(name, "moved", len(used), "max", round(max(used.values(), default=0) * 1000, 1), "mm", flush=True)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, "work", "stage2.blend"), compress=True)
