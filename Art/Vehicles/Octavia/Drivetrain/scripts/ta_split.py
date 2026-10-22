"""trailing-arm posing helper: whole mesh islands; the body-bracket plates stay, everything else swings about the pivot."""
import bmesh
from mathutils import Vector
def ta_posed_vertices(o, Mx):
    M = o.matrix_world; piv = M.translation
    bm = bmesh.new(); bm.from_mesh(o.data); bm.verts.ensure_lookup_table()
    seen = set(); fixed = set()
    brackets = [piv + Vector((s * 0.031, 0, 0.018)) for s in (-1, 1)] + [piv + Vector((0, 0.040, 0.060))]
    for v in bm.verts:
        if v.index in seen: continue
        isl = []; stack = [v]
        while stack:
            q = stack.pop()
            if q.index in seen: continue
            seen.add(q.index); isl.append(q.index)
            for e in q.link_edges: stack.append(e.other_vert(q))
        c = sum((M @ bm.verts[i].co for i in isl), Vector()) / len(isl)
        if min((c - b).length for b in brackets) < 0.012: fixed.update(isl)
    out = [(M @ v.co) if v.index in fixed else (Mx @ (M @ v.co)) for v in bm.verts]
    bm.free(); return out
