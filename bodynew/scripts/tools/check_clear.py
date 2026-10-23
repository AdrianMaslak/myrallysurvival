import bpy, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pairs import *
from clearance import wheel_poses
bpy.ops.wm.open_mainfile(filepath=sys.argv[-1])
O = bpy.data.objects
kit = [o for o in bpy.data.collections["BODY_KIT"].objects if o.type == 'MESH' and not o.name.startswith("UCX_")]
B = {o.name: tree_bm_obj(o) for o in kit}
def bbox(vs):
    return Vector([min(v[i] for v in vs) for i in range(3)]), Vector([max(v[i] for v in vs) for i in range(3)])
BB = {k: bbox(v[1]) for k, v in B.items()}
res = []
for r in bpy.data.collections["REFERENCE_DoNotExport"].objects:
    if r.type != 'MESH' or r.name.startswith("REF_UCX") or "Tyre" in r.name or any(k in r.name for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_")): continue
    pt, pvs = tree_bm_obj(r); plo, phi = bbox(pvs)
    for k, (bt, bvs) in B.items():
        lo, hi = BB[k]
        if any(plo[i] > hi[i] + 0.05 or phi[i] < lo[i] - 0.05 for i in range(3)): continue
        d = pair(bt, bvs, pt, pvs)
        if d["gap"] < 0.012: res.append((d["gap"] - d["depth"], r.name, k, d))
for s, a, b, d in sorted(res, key=lambda x: x[0])[:30]:
    print(f"  {a:34s} {b:12s} gap {d['gap']*1000:6.1f} mm  cut {d['cut']:5d}  depth {d['depth']*1000:6.1f} mm  at {d['at']}")
print("== tyres")
for side in ("FL", "FR", "RL", "RR"):
    ty = O[f"REF_SUSP_Tyre_{side}"]; worst = {}
    for pose, M in wheel_poses(side):
        pt, pvs = tree_bm_obj(ty, M @ ty.matrix_world)
        for k, (bt, bvs) in B.items():
            d = pair(bt, bvs, pt, pvs, reach=0.1)
            key = (d["gap"] - d["depth"])
            if k not in worst or key < worst[k][0]: worst[k] = (key, pose, d)
    for k, (s, pose, d) in sorted(worst.items(), key=lambda x: x[1][0])[:3]:
        print(f"  {side} {k:12s} gap {d['gap']*1000:6.1f} cut {d['cut']} depth {d['depth']*1000:.1f} pose {pose} at {d['at']}")
