"""Smoother shading for the v2 body's paint (scripts/export_body_v2.py applies it before export).

The refined paint skins keep every prototype vertex and fill in between, and the fill ripples by a millimetre or two
over 5-10 cm (the bonnet, the bumpers): with every face smooth-shaded and no custom normals, each ripple showed in
the highlights. This does not move a vertex. It gives each painted face the area-weighted average normal of the
painted faces within RADIUS of it that face the same way (within SAME_SIDE of it: the panel's inner shell, a
door shut or the other side of a crease never counts), then each corner of a face the average of those smoothed
normals over the faces at its vertex that are on the same side of any crease (within CREASE). Other materials keep
the normals they had.
"""
import math
import bpy
import numpy as np
from mathutils import Vector
from mathutils.kdtree import KDTree

RADIUS = 0.045
SAME_SIDE = math.cos(math.radians(30.0))
CREASE = math.cos(math.radians(35.0))


def smooth_paint_normals(ob, radius=RADIUS):
    me = ob.data
    paint = {i for i, m in enumerate(me.materials) if m and "body_paint" in m.name}
    if not paint or not me.polygons:
        return 0
    nf = len(me.polygons)
    centres = np.zeros(nf * 3)
    normals = np.zeros(nf * 3)
    areas = np.zeros(nf)
    mats = np.zeros(nf, dtype=np.int64)
    me.polygons.foreach_get("center", centres)
    me.polygons.foreach_get("normal", normals)
    me.polygons.foreach_get("area", areas)
    me.polygons.foreach_get("material_index", mats)
    centres = centres.reshape(-1, 3)
    normals = normals.reshape(-1, 3)
    painted = np.array([m in paint for m in mats])
    idx = np.nonzero(painted)[0]
    tree = KDTree(len(idx))
    for k, i in enumerate(idx):
        tree.insert(centres[i], k)
    tree.balance()
    smooth = normals.copy()
    for i in idx:
        found = tree.find_range(centres[i], radius)
        if len(found) < 2:
            continue
        near = idx[[f[1] for f in found]]
        dist = np.array([f[2] for f in found])
        same = normals[near] @ normals[i] > SAME_SIDE
        w = areas[near] * np.exp(-(dist / (0.5 * radius)) ** 2) * same
        n = (normals[near] * w[:, None]).sum(0)
        length = np.linalg.norm(n)
        if length > 1e-12:
            smooth[i] = n / length
    # Per corner: the smoothed normals of the painted faces round its vertex on the same side of any crease.
    nl = len(me.loops)
    loop_vert = np.zeros(nl, dtype=np.int64)
    me.loops.foreach_get("vertex_index", loop_vert)
    loop_face = np.zeros(nl, dtype=np.int64)
    starts = np.zeros(nf, dtype=np.int64)
    totals = np.zeros(nf, dtype=np.int64)
    me.polygons.foreach_get("loop_start", starts)
    me.polygons.foreach_get("loop_total", totals)
    for f in range(nf):
        loop_face[starts[f]:starts[f] + totals[f]] = f
    corner = np.zeros(nl * 3)
    me.corner_normals.foreach_get("vector", corner)
    corner = corner.reshape(-1, 3)
    faces_at = {}
    for l in range(nl):
        if painted[loop_face[l]]:
            faces_at.setdefault(loop_vert[l], []).append(loop_face[l])
    out = corner.copy()
    for l in range(nl):
        f = loop_face[l]
        if not painted[f]:
            continue
        ring = np.array(faces_at[loop_vert[l]])
        keep = ring[normals[ring] @ normals[f] > CREASE]
        n = (smooth[keep] * areas[keep][:, None]).sum(0)
        length = np.linalg.norm(n)
        out[l] = n / length if length > 1e-12 else smooth[f]
    me.normals_split_custom_set([Vector(v) for v in out])
    return len(idx)


def smooth_objects(objects):
    for ob in objects:
        if ob.type == "MESH" and not ob.name.startswith("UCX_"):
            count = smooth_paint_normals(ob)
            if count:
                print("BODY_NORMALS", ob.name, count, "painted faces")
