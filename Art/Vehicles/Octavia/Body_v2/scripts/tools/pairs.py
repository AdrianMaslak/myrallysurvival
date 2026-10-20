"""Pair clearance between a body part (open sheets) and a reference part, robust for open meshes.

gap  = smallest distance between the two surfaces (vertex-to-surface both ways)
cut  = number of triangle pairs that intersect (0 = clear)
depth= deepest body vertex inside the part (inside = odd number of crossings along +X and along +Z; parts are closed)
"""
from mathutils import Vector
from mathutils.bvhtree import BVHTree

def tree_bm_obj(o, M=None):
    M = M if M is not None else o.matrix_world
    vs = [M @ v.co for v in o.data.vertices]
    return BVHTree.FromPolygons(vs, [tuple(p.vertices) for p in o.data.polygons], epsilon=0.0), vs

def crossings(tree, p, d, maxd=3.0):
    n, q, left = 0, p.copy(), maxd
    while left > 0:
        h = tree.ray_cast(q, d, left)
        if h[0] is None: break
        n += 1; step = h[3] + 1e-5; q = h[0] + d * 1e-5; left -= step
        if n > 60: break
    return n

def inside(tree, p):
    return crossings(tree, p, Vector((1, 0, 0))) % 2 == 1 and crossings(tree, p, Vector((0, 0, 1))) % 2 == 1

def pair(bt, bvs, pt, pvs, closed_part=True, reach=0.3):
    cut = len(bt.overlap(pt))
    gap, at = 9.0, None
    for w in bvs:
        h = pt.find_nearest(w, reach)
        if h[0] is not None and h[3] < gap: gap, at = h[3], w
    for w in pvs[::2]:
        h = bt.find_nearest(w, reach)
        if h[0] is not None and h[3] < gap: gap, at = h[3], h[0]
    depth = 0.0
    if cut and closed_part:
        for w in bvs:
            h = pt.find_nearest(w, 0.25)
            if h[0] is None: continue
            if inside(pt, w): depth = max(depth, h[3])
    return dict(gap=gap if not cut else 0.0, cut=cut, depth=depth, at=tuple(round(x, 3) for x in at) if at else None)
