import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from clearance import *
bpy.ops.wm.open_mainfile(filepath=sys.argv[-1])
O = bpy.data.objects
kit = [o for o in bpy.data.collections["BODY_KIT"].objects if o.type == 'MESH']
T = {o.name: tree_of(o)[0] for o in kit}
print("== TYRES (min over poses; negative = intersecting, depth)")
for side in ("FL", "FR", "RL", "RR"):
    ty = O[f"REF_SUSP_Tyre_{side}"]; faces = [tuple(p.vertices) for p in ty.data.polygons]
    worst = {}
    for pose, M in wheel_poses(side):
        vs = verts_w(ty, M @ ty.matrix_world)
        for o in kit:
            g, at, n = min_gap(vs[::2], faces, T[o.name]) if False else min_gap(vs, faces, T[o.name])
            if o.name not in worst or g < worst[o.name][0]: worst[o.name] = (g, pose, at)
    for k, (g, pose, at) in sorted(worst.items(), key=lambda x: x[1][0])[:4]:
        print(f"  {side} {k:12s} {g*1000:7.1f} mm  pose {pose}  at {tuple(round(x,3) for x in at) if at else None}")
print("== STATIC REFERENCE vs BODY (worst 25)")
res = []
for r in bpy.data.collections["REFERENCE_DoNotExport"].objects:
    if r.type != 'MESH' or "UCX" in r.name or "Tyre" in r.name: continue
    if any(k in r.name for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_")): continue
    vs = verts_w(r); faces = [tuple(p.vertices) for p in r.data.polygons]
    for o in kit:
        g, at, n = min_gap(vs, faces, T[o.name], 0.1)
        if g < 0.02: res.append((g, r.name, o.name, at))
for g, a, b, at in sorted(res)[:40]:
    print(f"  {g*1000:7.1f} mm  {a:34s} {b:12s} at {tuple(round(x,3) for x in at) if at else ''}")
