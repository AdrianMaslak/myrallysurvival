"""List part pairs whose surfaces intersect (fasteners and UCX colliders excluded)."""
import bpy, os, sys
from mathutils.bvhtree import BVHTree
ROOT = os.path.dirname(os.path.abspath(__file__))
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "export", "Octavia1U_1.8T_20V.blend"))
bpy.context.view_layer.update()
parts = [o for o in bpy.data.objects if o.type == 'MESH' and "group" in o.keys()]
T, B = {}, {}
for o in parts:
    M = o.matrix_world; vw = [M @ v.co for v in o.data.vertices]
    T[o.name] = BVHTree.FromPolygons(vw, [tuple(p.vertices) for p in o.data.polygons], epsilon=0.0)
    B[o.name] = ([min(v[i] for v in vw) for i in range(3)], [max(v[i] for v in vw) for i in range(3)])
res = []
for i, a in enumerate(parts):
    for b in parts[i + 1:]:
        la, ha = B[a.name]; lb, hb = B[b.name]
        if any(la[k] > hb[k] or lb[k] > ha[k] for k in range(3)): continue
        n = len(T[a.name].overlap(T[b.name]))
        if not n: continue
        def depth(x, y):
            M = x.matrix_world; worst, cnt = 0.0, 0
            for v in x.data.vertices:
                w = M @ v.co
                hit = T[y.name].find_nearest(w, 0.08)
                if hit[0] is None: continue
                d = (w - hit[0]).dot(hit[1])
                if d < -0.002 and abs(d) >= hit[3] * 0.98:   # behind the nearest face, i.e. inside
                    cnt += 1; worst = max(worst, -d)
            return worst, cnt
        d1, c1 = depth(a, b); d2, c2 = depth(b, a)
        res.append((max(d1, d2), c1 + c2, n, a.name, b.name))
res.sort(reverse=True)
print("OVERLAP_PAIRS", len(res), "PENETRATING(>2mm)", sum(1 for r in res if r[1]))
for d, c, n, a, b in res: print(f"depth {d*1000:6.1f} mm  verts_inside {c:5d}  tris {n:5d}  {a}  <->  {b}")
