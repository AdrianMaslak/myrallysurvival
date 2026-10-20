"""Shape-keeping refinement of a body skin.

Every original vertex and edge stays where it is. Edges longer than the limit are bisected (longest first, one per
triangle per pass, the triangle cut from the new midpoint to its opposite corner) until none is left. A new midpoint
on a smooth edge is lifted onto the Phong curve of its two end normals (capped, so large flat facets get a gentle
curve but nothing moves more than `cap`); on folds and borders it stays on the straight edge. Then pairs of
triangles are joined into quads where they are flat and well shaped.
"""
import bmesh, math
from mathutils import Vector


def refine(bm, need_limit, cap=0.003, fold_deg=40.0, join=True, max_passes=40):
    """need_limit(face) -> max edge length for that face (None = leave as is). bm is modified in place (triangles)."""
    bmesh.ops.triangulate(bm, faces=[f for f in bm.faces if len(f.verts) > 3])
    bm.normal_update()
    cosf = math.cos(math.radians(fold_deg))
    LIM = bm.faces.layers.float.get("lim") or bm.faces.layers.float.new("lim")
    for f in bm.faces:
        l = need_limit(f); f[LIM] = l if l else 0.0

    def is_fold(e):
        if len(e.link_faces) != 2: return True
        a, b = e.link_faces
        return a.normal.dot(b.normal) < cosf

    def side_normal(v, ref_n):
        acc = Vector()
        for g in v.link_faces:
            if g.normal.dot(ref_n) > cosf: acc += g.normal * max(g.calc_area(), 1e-9)
        return acc.normalized() if acc.length > 1e-12 else ref_n

    def delaunay(passes=6, flat=math.cos(math.radians(4))):
        for _ in range(passes):
            n = 0
            for e in list(bm.edges):
                if not e.is_valid or len(e.link_faces) != 2: continue
                f1, f2 = e.link_faces
                if len(f1.verts) != 3 or len(f2.verts) != 3 or f1.normal.dot(f2.normal) < flat: continue
                if f1[LIM] <= 0 and f2[LIM] <= 0: continue
                a, b = e.verts
                c = [v for v in f1.verts if v not in (a, b)][0]; d = [v for v in f2.verts if v not in (a, b)][0]
                if any(x.other_vert(c) is d for x in c.link_edges): continue
                ang = (a.co - c.co).angle(b.co - c.co, 0) + (a.co - d.co).angle(b.co - d.co, 0)
                if ang <= math.pi + 1e-3: continue
                # the flipped pair must stay on the same side (convex quad)
                n1 = (a.co - c.co).cross(d.co - c.co); n2 = (b.co - d.co).cross(c.co - d.co)
                fn = f1.normal + f2.normal
                if n1.dot(fn) * n2.dot(fn) <= 0 or abs(n1.normalized().dot(fn.normalized())) < 0.9: continue
                if bmesh.utils.edge_rotate(e, True): n += 1
            bm.normal_update()
            if not n: break

    for _ in range(max_passes):
        cand = []
        for e in bm.edges:
            lim = max((f[LIM] for f in e.link_faces), default=0.0)
            lim = min([f[LIM] for f in e.link_faces if f[LIM] > 0] or [0.0])
            if lim > 0 and e.calc_length() > lim: cand.append(e)
        if not cand: break
        cand.sort(key=lambda e: -e.calc_length())
        busy = set(); did = 0
        for e in cand:
            if not e.is_valid: continue
            lf = list(e.link_faces)
            if any(f in busy or len(f.verts) != 3 for f in lf): continue
            busy.update(lf)
            a, b = e.verts
            mid = (a.co + b.co) / 2
            if not is_fold(e):
                fn = sum((f.normal for f in lf), Vector()).normalized()
                na, nb = side_normal(a, fn), side_normal(b, fn)
                q = mid - 0.5 * (na * (mid - a.co).dot(na) + nb * (mid - b.co).dot(nb))
                d = q - mid
                if d.length > cap: d = d.normalized() * cap
                mid = mid + d
            lims = [f[LIM] for f in lf]
            ne, nv = bmesh.utils.edge_split(e, a, 0.5)
            nv.co = mid
            for face in list(nv.link_faces):
                if len(face.verts) != 4: continue
                vs = list(face.verts); k = vs.index(nv)
                bmesh.utils.face_split(face, nv, vs[(k + 2) % 4])
            did += 1
        bm.normal_update()
        delaunay(2)
        if not did: break
    delaunay(8)
    if join:
        bmesh.ops.join_triangles(bm, faces=[f for f in bm.faces if f[LIM] > 0],
                                 angle_face_threshold=math.radians(12), angle_shape_threshold=math.radians(60))
    bm.normal_update()
    return bm
