"""Outer/inner classification of body faces: a face is outer skin if rays leaving it along its normal escape the car."""
import bmesh, math, random
from mathutils import Vector
from mathutils.bvhtree import BVHTree

def scene_tree(objs):
    bm = bmesh.new()
    for o in objs:
        t = bmesh.new(); t.from_mesh(o.data); t.transform(o.matrix_world)
        me_tmp = None
        bm_verts = len(bm.verts)
        t.to_mesh(o.data.copy()) if False else None
        # merge
        vm = [bm.verts.new(v.co) for v in t.verts]
        for f in t.faces:
            try: bm.faces.new([vm[v.index] for v in f.verts])
            except ValueError: pass
        t.free()
    tree = BVHTree.FromBMesh(bm); bm.free()
    return tree

def outer_flags(faces, tree, n_rays=3, reach=3.0):
    random.seed(7); out = []
    for f in faces:
        c = f.calc_center_median(); n = f.normal
        if n.length < 0.5: out.append(False); continue
        esc = 0
        t1 = n.orthogonal().normalized(); t2 = n.cross(t1)
        for k in range(n_rays):
            d = (n + t1 * random.uniform(-0.3, 0.3) + t2 * random.uniform(-0.3, 0.3)).normalized()
            h = tree.ray_cast(c + n * 0.003, d, reach)
            if h[0] is None: esc += 1
        out.append(esc * 2 > n_rays)
    return out
