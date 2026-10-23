"""Checks for the suspension set: overlaps, fastener reachability, tyre clearance, hard points, budget.

Writes export/overlap_report.txt and export/verification_report.txt.
"""
import bpy, os, json, math, re
from mathutils import Vector, Matrix, Quaternion
from mathutils.bvhtree import BVHTree
ROOT = os.path.dirname(os.path.abspath(__file__)); OUT = os.path.join(ROOT, "export")
bpy.ops.wm.open_mainfile(filepath=os.path.join(OUT, "Octavia1U_Suspension.blend"))
bpy.context.view_layer.update()
man = json.load(open(os.path.join(OUT, "parts_manifest.json")))
PM = {p["name"]: p for p in man["parts"]}
O = bpy.data.objects
parts = [o for o in O if o.type == 'MESH' and "group" in o.keys()]

def verts_w(o, M=None):
    M = M if M is not None else o.matrix_world
    return [M @ v.co for v in o.data.vertices]
def tree(o, M=None):
    return BVHTree.FromPolygons(verts_w(o, M), [tuple(p.vertices) for p in o.data.polygons], epsilon=0.0)
T = {o.name: tree(o) for o in parts}
BB = {}
for o in parts:
    vw = verts_w(o); BB[o.name] = (Vector([min(v[i] for v in vw) for i in range(3)]), Vector([max(v[i] for v in vw) for i in range(3)]))

# ------------------------------------------------------------------ overlaps
ALLOWED = [
    (r"LowerArm_", r"Knuckle_", "ball-joint stud in the knuckle"),
    (r"TieRodEnd_", r"Knuckle_", "tie-rod ball stud in the steering arm"),
    (r"AntiRollBarLink_", r"(LowerArm_|AntiRollBar|HubCarrier_)", "drop-link ball stud"),
    (r"LowerArm_", r"FrontSubframe", "bushing in its bracket"),
    (r"(LowerLink_|UpperLink_)", r"(RearSubframe|HubCarrier_)", "bushing in its bracket"),
    (r"Damper_", r"HubCarrier_", "bushing in its bracket"),
    (r"CVJoint_Outer_", r"Knuckle_", "CV joint in the hub"),
    (r"CVBoot_Outer_", r"CVJoint_Outer_", "boot clamped on the joint"),
    (r"CVBoot_Inner_", r"CVJoint_Inner_", "boot clamped on the joint"),
    (r"Spring_", r"(Strut_Lower_|Strut_Upper_|LowerLink_)", "spring on its seat"),
    (r"TieRod_", r"SteeringRack", "inner tie-rod joint inside the rack boot"),
    (r"CVJoint_Inner_R", r"RearFinalDrive", "inner CV in its final-drive flange"),
    (r"CVJoint_Outer_R", r"HubCarrier_", "outer CV in the hub carrier bore"),
]
def allowed(a, b):
    for pa, pb, why in ALLOWED:
        if (re.search(pa, a) and re.search(pb, b)) or (re.search(pa, b) and re.search(pb, a)): return why
    return None
def depth(x, y):
    worst, cnt = 0.0, 0
    for w in verts_w(x):
        hit = T[y.name].find_nearest(w, 0.08)
        if hit[0] is None: continue
        d = (w - hit[0]).dot(hit[1])
        if d < -0.002 and abs(d) >= hit[3] * 0.98: cnt += 1; worst = max(worst, -d)
    return worst, cnt
ov = []
for i, a in enumerate(parts):
    for b in parts[i + 1:]:
        la, ha = BB[a.name]; lb, hb = BB[b.name]
        if any(la[k] > hb[k] or lb[k] > ha[k] for k in range(3)): continue
        n = len(T[a.name].overlap(T[b.name]))
        if not n: continue
        d1, c1 = depth(a, b); d2, c2 = depth(b, a)
        ov.append((max(d1, d2), c1 + c2, n, a.name, b.name, allowed(a.name, b.name)))
ov.sort(reverse=True)
bad = [r for r in ov if r[1] and not r[5]]
with open(os.path.join(OUT, "overlap_report.txt"), "w") as fh:
    fh.write(f"Touching pairs: {len(ov)}   penetrating more than 2 mm: {sum(1 for r in ov if r[1])}   of which not in the allowed list: {len(bad)}\n")
    fh.write("Allowed: ball-joint studs, bushings in brackets, CV joints in hubs (front knuckle, rear hub carrier bore), inner rear CV joints in their final-drive flanges, springs on seats, boots on their joints.\n\n")
    for d, c, n, a, b, why in ov:
        fh.write(f"depth {d*1000:6.1f} mm  verts_inside {c:4d}  {a}  <->  {b}" + (f"   [allowed: {why}]" if why and c else "") + "\n")

# ------------------------------------------------------------------ reachability (12 cm along the back-out axis)
stage = {o.name: o.get("removal_stage", 9) for o in parts}
reach = []
for p in man["parts"]:
    for fd in p["fasteners"]:
        pos = Vector(fd["position"]); ax = Vector(fd["axis"]).normalized()
        perp = ax.orthogonal().normalized(); perp2 = ax.cross(perp)
        starts = [pos + ax * 0.012] + [pos + ax * 0.012 + q * 0.007 for q in (perp, -perp, perp2, -perp2)]
        hits = []
        for o in parts:
            if o.name == p["name"] or stage[o.name] < p["removal_stage"]: continue
            if fd["label"] == "brake hose union" and o.name.startswith("BrakeHose"): continue   # the hose is what the union holds
            for s0 in starts:
                h = T[o.name].ray_cast(s0, ax, 0.108)
                if h[0] is not None: hits.append((h[3] + 0.012, o.name)); break
        hits.sort()
        reach.append((p["name"], fd["name"], fd["label"], hits[0] if hits else None))
blocked = [r for r in reach if r[3]]

# ------------------------------------------------------------------ tyre clearance
def min_clear(tyre_verts, other_tree):
    best = 9.0
    for w in tyre_verts[::2]:
        h = other_tree.find_nearest(w, 0.25)
        if h[0] is not None: best = min(best, h[3])
    return best
def rot_about(p0, axis, ang):
    return Matrix.Translation(p0) @ Matrix.Rotation(ang, 4, axis) @ Matrix.Translation(-p0)
hp = {k: Vector(v["used"]) for k, v in man["hard_points"].items()}
clear = []
for side, sx in (("FL", 1), ("FR", -1)):
    S = lambda v: Vector((v.x * sx, v.y, v.z))
    top, bj, fp = S(hp["F_TOP"]), S(hp["F_BJ"]), S(hp["F_ARM_PIV"])
    rp = Vector((-0.330 * sx, 0.920, 0.183)); tro = S(hp["F_TRO"]); re_ = Vector((-0.35 * sx, tro.y, tro.z))
    kp = (top - bj).normalized(); arm_axis = (rp - fp).normalized()
    tyre = O[f"Tyre_{side}"]
    for steer in (-35, 0, 35):
        for dz in (-0.08, 0.0, 0.08):
            # lower arm: rotate about its bushing axis so the ball joint rises dz
            r_arm = (bj - fp) - arm_axis * (bj - fp).dot(arm_axis)
            ang = math.asin(max(-1, min(1, dz / max(1e-6, r_arm.length))))
            sgn = 1 if (arm_axis.cross(r_arm)).z > 0 else -1
            Marm = rot_about(fp, arm_axis, sgn * ang)
            bj2 = Marm @ bj
            Mwheel = Matrix.Translation(bj2 - bj) @ rot_about(bj, kp, math.radians(steer))
            tv = verts_w(tyre, Mwheel @ tyre.matrix_world)
            tro2 = Mwheel @ tro
            d0, d1 = (tro - re_), (tro2 - re_)
            q = d0.rotation_difference(d1).to_matrix().to_4x4()
            Mtr = Matrix.Translation(re_) @ q @ Matrix.Translation(-re_)
            for nm, M in ((f"LowerArm_{side}", Marm), (f"TieRod_{side}", Mtr), (f"TieRodEnd_{side}", Mtr),
                          (f"AntiRollBarLink_{side}", Marm), ("FrontAntiRollBar", Matrix.Identity(4)), ("FrontSubframe", Matrix.Identity(4)),
                          ("SteeringRack", Matrix.Identity(4))):
                o = O[nm]; t2 = tree(o, M @ o.matrix_world)
                c = min_clear(tv, t2)
                if len(BVHTree.FromPolygons(tv, [tuple(p.vertices) for p in tyre.data.polygons]).overlap(t2)): c = -0.001
                clear.append((side, steer, dz, nm, c))
    # parts that turn and travel with the wheel: static clearance
    tv0 = verts_w(tyre)
    for nm in (f"Strut_Lower_{side}", f"Strut_Upper_{side}", f"Spring_{side}"):
        o = O[nm]; c = min_clear(tv0, T[nm])
        if len(T[f"Tyre_{side}"].overlap(T[nm])): c = -0.001
        clear.append((side, "any", "any", nm, c))
for side, sx in (("RL", 1), ("RR", -1)):
    tyre = O[f"Tyre_{side}"]
    for dz in (-0.08, 0.0, 0.08):
        Mw = Matrix.Translation((0, 0, dz))
        tv = verts_w(tyre, Mw @ tyre.matrix_world)
        movers = {}
        for nm, key_in in ((f"LowerLink_{side}", "R_LL_IN"), (f"UpperLink_{side}", "R_UL_IN"), (f"TrailingArm_{side}", "R_TA_PIV")):
            o = O[nm]; pin = Vector(PM[nm]["origin_m"]); pout = Vector(PM[nm]["moving_end_m"])
            q = (pout - pin).rotation_difference(pout + Vector((0, 0, dz)) - pin).to_matrix().to_4x4()
            movers[nm] = Matrix.Translation(pin) @ q @ Matrix.Translation(-pin)
        o = O[f"Damper_{side}"]; top = Vector(PM[o.name]["origin_m"]); bot = Vector(PM[o.name]["bottom_point_m"])
        q = (bot - top).rotation_difference(bot + Vector((0, 0, dz)) - top).to_matrix().to_4x4()
        movers[o.name] = Matrix.Translation(top) @ q @ Matrix.Translation(-top)
        sp = O[f"Spring_{side}"]; spt = Vector(PM[sp.name]["origin_m"]); spb = Vector(PM[sp.name]["bottom_point_m"])
        k = ((spt.z - spb.z) - dz * 0.6) / (spt.z - spb.z)
        movers[sp.name] = Matrix.Translation(spt) @ Matrix.Diagonal((1, 1, k, 1)) @ Matrix.Translation(-spt)
        movers[f"AntiRollBarLink_{side}"] = Matrix.Translation((0, 0, dz * 0.8))
        movers["RearSubframe"] = Matrix.Identity(4); movers["RearAntiRollBar"] = Matrix.Identity(4)
        for nm, M in movers.items():
            o = O[nm]; t2 = tree(o, M @ o.matrix_world); c = min_clear(tv, t2)
            if len(BVHTree.FromPolygons(tv, [tuple(p.vertices) for p in tyre.data.polygons]).overlap(t2)): c = -0.001
            clear.append((side, 0, dz, nm, c))

# ------------------------------------------------------------------ rear driveshaft clearance (+-8 cm travel)
def gap(oa, Ma, ob, Mb):
    va = verts_w(oa, Ma @ oa.matrix_world); vb = verts_w(ob, Mb @ ob.matrix_world)
    ta = BVHTree.FromPolygons(va, [tuple(p.vertices) for p in oa.data.polygons])
    tb = BVHTree.FromPolygons(vb, [tuple(p.vertices) for p in ob.data.polygons])
    if ta.overlap(tb): return -0.001
    best = 9.0
    for w in va:
        h = tb.find_nearest(w, 0.3)
        if h[0] is not None: best = min(best, h[3])
    for w in vb:
        h = ta.find_nearest(w, 0.3)
        if h[0] is not None: best = min(best, h[3])
    return best
def rear_movers(side, dz):
    mv = {}
    for nm in (f"LowerLink_{side}", f"UpperLink_{side}", f"TrailingArm_{side}"):
        pin = Vector(PM[nm]["origin_m"]); pout = Vector(PM[nm]["moving_end_m"])
        q = (pout - pin).rotation_difference(pout + Vector((0, 0, dz)) - pin).to_matrix().to_4x4()
        mv[nm] = Matrix.Translation(pin) @ q @ Matrix.Translation(-pin)
    nm = f"Damper_{side}"; top = Vector(PM[nm]["origin_m"]); bot = Vector(PM[nm]["bottom_point_m"])
    q = (bot - top).rotation_difference(bot + Vector((0, 0, dz)) - top).to_matrix().to_4x4()
    mv[nm] = Matrix.Translation(top) @ q @ Matrix.Translation(-top)
    nm = f"Spring_{side}"; spt = Vector(PM[nm]["origin_m"]); spb = Vector(PM[nm]["bottom_point_m"])
    k = ((spt.z - spb.z) - dz * 0.6) / (spt.z - spb.z)
    mv[nm] = Matrix.Translation(spt) @ Matrix.Diagonal((1, 1, k, 1)) @ Matrix.Translation(-spt)
    mv[f"HubCarrier_{side}"] = Matrix.Translation((0, 0, dz))
    mv[f"AntiRollBarLink_{side}"] = Matrix.Translation((0, 0, dz * 0.8))
    mv["RearSubframe"] = Matrix.Identity(4); mv["RearAntiRollBar"] = Matrix.Identity(4); mv["RearFinalDrive"] = Matrix.Identity(4)
    # driveshaft: shaft and boots swing about the inner joint so the outer joint follows the hub;
    # the inner joint housing stays on its flange, the outer joint moves with the hub carrier
    pin = Vector(PM[f"Driveshaft_{side}"]["origin_m"]); pout = Vector(PM[f"Driveshaft_{side}"]["moving_end_m"])
    q = (pout - pin).rotation_difference(pout + Vector((0, 0, dz)) - pin).to_matrix().to_4x4()
    sw = Matrix.Translation(pin) @ q @ Matrix.Translation(-pin)
    mv[f"Driveshaft_{side}"] = sw; mv[f"CVBoot_Inner_{side}"] = sw; mv[f"CVBoot_Outer_{side}"] = sw
    mv[f"CVJoint_Inner_{side}"] = Matrix.Identity(4); mv[f"CVJoint_Outer_{side}"] = Matrix.Translation((0, 0, dz))
    return mv
ds_clear = []
for side in ("RL", "RR"):
    shaft_parts = [f"{n}_{side}" for n in ("Driveshaft", "CVJoint_Inner", "CVBoot_Inner", "CVJoint_Outer", "CVBoot_Outer")]
    others = [f"{n}_{side}" for n in ("Damper", "Spring", "LowerLink", "UpperLink", "TrailingArm", "HubCarrier", "AntiRollBarLink")] + ["RearSubframe", "RearAntiRollBar"]
    for dz in (-0.08, -0.04, 0.0, 0.04, 0.08):
        mv = rear_movers(side, dz)
        for a in shaft_parts:
            for b in others:
                if a.startswith("CVJoint_Outer") and b.startswith("HubCarrier"): continue   # sits in the bore (allowed overlap)
                ds_clear.append((side, a, b, dz, gap(O[a], mv[a], O[b], mv[b])))
fd_clear = []
for dz in (-0.08, 0.0, 0.08):
    for side in ("RL", "RR"):
        mv = rear_movers(side, dz)
        for b in (f"LowerLink_{side}", f"UpperLink_{side}"):
            fd_clear.append(("RearFinalDrive", b, dz, gap(O["RearFinalDrive"], Matrix.Identity(4), O[b], mv[b])))
fd_clear.append(("RearFinalDrive", "RearSubframe", 0.0, gap(O["RearFinalDrive"], Matrix.Identity(4), O["RearSubframe"], Matrix.Identity(4))))

# ------------------------------------------------------------------ report
tris_parts = sum(p["triangles"] for p in man["parts"])
for o in O:
    if o.type == 'MESH': o.data.calc_loop_triangles()
tris_fast = sum(len(o.data.loop_triangles) for o in O if o.type == 'MESH' and "tool" in o.keys() and "group" not in o.keys())
lines = []
lines.append("HARD POINTS (given -> used, deviation)")
for k, v in man["hard_points"].items():
    lines.append(f"  {k:14s} {v['given']} -> {v['used']}   {v['deviation_mm']} mm")
lines.append("")
lines.append(f"TRIANGLES  parts {tris_parts}  fasteners {tris_fast}  total {tris_parts + tris_fast}")
wheel = {s: PM[f'Tyre_{s}']['triangles'] + PM[f'Rim_{s}']['triangles'] for s in ('FL', 'FR', 'RL', 'RR')}
lines.append(f"  rim + tyre per wheel: {wheel}")
over = [(p['triangles'], p['name']) for p in man['parts'] if not p['name'].startswith(('Tyre', 'Rim')) and p['triangles'] > 3000]
lines.append(f"  parts over 3k: {over or 'none'}")
lines.append("")
lines.append("TYRE CLEARANCE (min distance, mm; negative = touching/intersecting)")
worst = {}
for side, st, dz, nm, c in clear:
    key = (side, nm)
    if key not in worst or c < worst[key][0]: worst[key] = (c, (st, dz))
for (side, nm), (c, when) in sorted(worst.items()):
    lines.append(f"  {side} vs {nm:24s} {c*1000:7.1f} mm   worst at steer/travel {when}")
lines.append("")
lines.append("REAR DRIVESHAFT CLEARANCE (shaft, joints, boots vs each part; worst over -8..+8 cm travel)")
wd = {}
for side, a, b, dz, c in ds_clear:
    key = (side, b)
    if key not in wd or c < wd[key][0]: wd[key] = (c, a, dz)
for (side, b), (c, a, dz) in sorted(wd.items()):
    lines.append(f"  {side}  {b:24s} {c*1000:7.1f} mm   closest: {a} at travel {dz*100:+.0f} cm")
lines.append(f"  worst gap overall: {min(c for *_, c in ds_clear)*1000:.1f} mm")
lines.append("  (the outer CV joint sits in the hub carrier's bore: allowed overlap, not a clearance)")
lines.append("")
lines.append("FINAL DRIVE CLEARANCE (static subframe; links over -8..+8 cm travel)")
wf = {}
for a, b, dz, c in fd_clear:
    if b not in wf or c < wf[b][0]: wf[b] = (c, dz)
for b, (c, dz) in sorted(wf.items()):
    lines.append(f"  RearFinalDrive vs {b:16s} {c*1000:7.1f} mm" + (f"   worst at travel {dz*100:+.0f} cm" if b != "RearSubframe" else "   (closest at the mounting pads, 1 mm under their brackets)"))
lines.append("")
lines.append(f"OVERLAPS  pairs touching {len(ov)}, penetrating >2 mm {sum(1 for r in ov if r[1])}, outside the allowed list {len(bad)}")
for d, c, n, a, b, why in bad: lines.append(f"  {d*1000:5.1f} mm  {a} <-> {b}")
lines.append("")
lines.append(f"REACHABILITY  fasteners {len(reach)}, blocked within 12 cm {len(blocked)}")
for part, f, lab, h in blocked: lines.append(f"  {f:40s} ({lab}) blocked by {h[1]} at {h[0]*100:4.1f} cm")
txt = "\n".join(lines)
open(os.path.join(OUT, "verification_report.txt"), "w").write(txt + "\n")
print(txt)
