"""Clearance check of the drivetrain kit in a configuration scene.  usage: python3 check_drive.py -- work/config_4x4.blend 4x4
Static gaps from every kit part to everything else, plus suspension travel (+-8 cm): rear links, driveshafts, trailing
arms and hub/brake/wheel parts (4x4) or the twist beam and hub/brake/wheel parts (FWD) moved to bump and droop."""
import bpy, sys, math, json
sys.path.insert(0, "/home/claude/octavia_body/tools")
from pairs import tree_bm_obj, pair
from mathutils import Vector, Matrix
a = sys.argv[sys.argv.index("--") + 1:]; path, cfg = a[0], a[1]
bpy.ops.wm.open_mainfile(filepath=path)
O = bpy.data.objects
KIT = set()
for cn in ("Drivetrain4x4", "Exhaust", "FuelSystem", "RearAxleFWD"):
    c = bpy.data.collections.get(cn)
    if c: KIT |= {o.name for o in c.objects if o.type == 'MESH' and o.parent is None}
def is_fast(n): return any(k in n for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_"))
ALL = [o for o in O if o.type == 'MESH' and not is_fast(o.name) and not o.name.startswith("UCX_")]
CONTACT = [("AngleDrive", "Gearbox_02J"), ("AngleDrive", "Propshaft_Front"), ("Propshaft_Front", "PropshaftCentreBearing"),
           ("Propshaft_Rear", "PropshaftCentreBearing"), ("Propshaft_Front", "Propshaft_Rear"), ("Propshaft_Rear", "RearFinalDrive"),
           ("PropshaftCentreBearing", "Chassis"), ("Exhaust_Cat", "ExhaustFrontPipe"), ("Exhaust_Cat", "Exhaust_CentreSilencer"),
           ("Exhaust_CentreSilencer", "Exhaust_RearSilencer"), ("Exhaust_Cat", "Chassis"), ("Exhaust_CentreSilencer", "Chassis"),
           ("Exhaust_RearSilencer", "Chassis"), ("FuelTank_FWD", "Chassis"), ("FuelTank_4x4", "Chassis"), ("FuelTank_FWD", "FuelFillerNeck"),
           ("FuelTank_4x4", "FuelFillerNeck"), ("FuelFillerNeck", "Chassis"), ("TwistBeam", "PivotBracket_RL"), ("TwistBeam", "PivotBracket_RR"),
           ("TwistBeam", "RearHub_RL"), ("TwistBeam", "RearHub_RR"), ("TwistBeam", "Spring_RL"), ("TwistBeam", "Spring_RR"),
           ("TwistBeam", "Damper_RL"), ("TwistBeam", "Damper_RR"), ("PivotBracket_RL", "Chassis"), ("PivotBracket_RR", "Chassis"),
           ("RearHub_RL", "BrakeDisc_RL"), ("RearHub_RR", "BrakeDisc_RR"), ("RearHub_RL", "CaliperCarrier_RL"), ("RearHub_RR", "CaliperCarrier_RR"),
           # the same seats as the suspension set's 4x4 hub carrier: carrier lugs by the caliper and pads, rim centred on the hub pilot
           ("RearHub_RL", "BrakeCaliper_RL"), ("RearHub_RR", "BrakeCaliper_RR"), ("RearHub_RL", "BrakePadInner_RL"), ("RearHub_RR", "BrakePadInner_RR"),
           ("RearHub_RL", "Rim_RL"), ("RearHub_RR", "Rim_RR")]
WHY = {"Chassis": "bolted / hung from the body"}
def allowed(a, b): return (a, b) in CONTACT or (b, a) in CONTACT
T = {o.name: tree_bm_obj(o) for o in ALL}
def bb(vs): return Vector([min(v[i] for v in vs) for i in range(3)]), Vector([max(v[i] for v in vs) for i in range(3)])
BB = {k: bb(v[1]) for k, v in T.items()}
L = [f"DRIVETRAIN KIT CLEARANCE — {cfg.upper()} CONFIGURATION", ""]
L.append("STATIC (every gap under 15 mm between a kit part and anything else; intended contacts marked)")
rows = []
for k in sorted(KIT):
    lo, hi = BB[k]
    for o in ALL:
        n = o.name
        if n == k or (n in KIT and n < k): continue
        l2, h2 = BB[n]
        if any(lo[i] > h2[i] + 0.016 or hi[i] < l2[i] - 0.016 for i in range(3)): continue
        d = pair(T[k][0], T[k][1], T[n][0], T[n][1], closed_part=False, reach=0.02)
        if d["gap"] < 0.015: rows.append((d["gap"], k, n, d))
worst = {}
for g, k, n, d in sorted(rows):
    tag = "contact (intended)" if allowed(k, n) else ("INTERSECTS" if d["cut"] else ("OK" if g >= 0.010 else "UNDER 10 mm"))
    L.append(f"  {k:24s} {n:26s} {g*1000:6.1f} mm  {tag}" + (f"  at {d['at']}" if tag in ("INTERSECTS", "UNDER 10 mm") else ""))
    if not allowed(k, n): worst[(k, n)] = g
L.append(f"  -> worst non-contact gap: {min(worst.values())*1000 if worst else 15:.1f} mm")

L.append(""); L.append("BARE PIPES / TANK SHELLS TO THE BODY (hangers, straps, brackets left out)")
from mathutils.bvhtree import BVHTree
def sub_tree(o, mats, keep=None):
    me = o.data; M = o.matrix_world; idx = {i for i, m in enumerate(me.materials) if m and m.name.split(".")[0] in mats}
    polys = [p for p in me.polygons if p.material_index in idx and (keep is None or keep(M @ p.center))]
    used = sorted({v for p in polys for v in p.vertices}); remap = {v: i for i, v in enumerate(used)}
    vs = [M @ me.vertices[v].co for v in used]
    return BVHTree.FromPolygons(vs, [tuple(remap[v] for v in p.vertices) for p in polys]), vs
BARE = {"Exhaust_Cat": {"exhaust"}, "Exhaust_CentreSilencer": {"exhaust"}, "Exhaust_RearSilencer": {"exhaust", "alu_machined"},
        "FuelTank_FWD": {"plastic_black"}, "FuelTank_4x4": {"plastic_black"}, "FuelFillerNeck": {"painted_black", "rubber"}, "PropshaftCentreBearing": {"rubber"}}
KEEP = {"FuelFillerNeck": lambda w: not (w.x > 0.69 and w.y < -1.60 and w.z > 0.36)}   # through the floor grommet, up the quarter to the flap pocket
for k, mats in BARE.items():
    if k not in T: continue
    st, sv = sub_tree(O[k], mats, KEEP.get(k))
    for n in ("Chassis", "RearBumper"):
        d = pair(st, sv, T[n][0], T[n][1], closed_part=False, reach=0.03)
        if d["gap"] > 1: L.append(f"  {k:24s} {n:12s}  > 30 mm"); continue
        L.append(f"  {k:24s} {n:12s} {d['gap']*1000:6.1f} mm" + ("  INTERSECTS" if d["cut"] else ("" if d["gap"] >= 0.010 else "  UNDER 10 mm")) + f"  at {d['at']}")

# ---------------------------------------------------------------- travel
L.append(""); L.append("SUSPENSION TRAVEL (rear wheels at -8 and +8 cm; moving parts checked against kit parts and the body)")
def rot_about(p0, axis, ang): return Matrix.Translation(p0) @ Matrix.Rotation(ang, 4, axis) @ Matrix.Translation(-p0)
def poses(dz, side):
    sx = -1 if side == "L" else 1; M = {}
    if cfg == "4x4":
        W = Vector((sx * 0.702, -1.307, 0.272))
        for nm in ("LowerLink", "UpperLink"):
            o = O.get(f"{nm}_R{side}");
            if not o: continue
            p0 = o.matrix_world.translation; r = 0.334 if nm == "LowerLink" else 0.365   # inner to outer joint (hard points)
            M[o.name] = rot_about(p0, Vector((0, 1, 0)), -sx * math.asin(max(-1, min(1, dz / r))))
        for nm in ("Driveshaft", "CVBoot_Outer"):     # the inner joint stays bolted to its flange; the boot flexes
            o = O.get(f"{nm}_R{side}")
            if o: M[o.name] = rot_about(Vector((sx * 0.201, -1.275, 0.295)), Vector((0, 1, 0)), -sx * math.asin(dz / 0.44))
        ta = O.get(f"TrailingArm_R{side}")
        if ta: M[ta.name] = rot_about(ta.matrix_world.translation, Vector((1, 0, 0)), -math.asin(dz / 0.47))
        wheel = ("HubCarrier", "CVJoint_Outer", "BrakeDisc", "CaliperCarrier", "BrakeCaliper", "BrakePadInner", "BrakePadOuter", "Tyre", "Rim")
    else:
        piv = Vector((sx * 0.545, -0.842, 0.245)); W = Vector((sx * 0.702, -1.307, 0.272))
        ang = math.asin(dz / (W - piv).length)
        M["TwistBeam_" + side] = rot_about(piv, Vector((1, 0, 0)), -ang)
        wheel = ("RearHub", "BrakeDisc", "CaliperCarrier", "BrakeCaliper", "BrakePadInner", "BrakePadOuter", "Tyre", "Rim")
    for nm in wheel:
        o = O.get(f"{nm}_R{side}")
        if o: M[o.name] = Matrix.Translation((0, 0, dz))
    return M
targets = [n for n in KIT] + ["Chassis", "RearBumper"]   # suspension-internal pairs are the suspension set's own check
tw = {}; POSED = {}
for side in ("L", "R"):
    for dz in (-0.08, 0.08):
        PM = poses(dz, side); movers = {("TwistBeam" if m.startswith("TwistBeam_") else m) for m in PM}
        for mover, Mx in PM.items():
            src = "TwistBeam" if mover.startswith("TwistBeam_") else mover
            if src not in T: continue
            # for the twist beam only the moving side's half is moved (x on that side)
            mo = O[src]; Mw = Mx @ mo.matrix_world
            if src.startswith("TrailingArm"):          # the body bracket plates stay; arm, bush and eye swing (whole mesh islands)
                sys.path.insert(0, "/home/claude/octavia_drive/work"); from ta_split import ta_posed_vertices
                vs = ta_posed_vertices(mo, Mx)
                from mathutils.bvhtree import BVHTree
                pt = BVHTree.FromPolygons(vs, [tuple(p.vertices) for p in mo.data.polygons]); pvs = vs
            elif src == "TwistBeam":
                sx = -1 if side == "L" else 1
                vs = [Mx @ (mo.matrix_world @ v.co) if (mo.matrix_world @ v.co).x * sx > 0.30 else mo.matrix_world @ v.co for v in mo.data.vertices]
                import bmesh
                from mathutils.bvhtree import BVHTree
                pt = BVHTree.FromPolygons(vs, [tuple(p.vertices) for p in mo.data.polygons]); pvs = vs
            else:
                pt, pvs = tree_bm_obj(mo, Mw)
            POSED.setdefault((side, dz), []).append(pvs)
            lo, hi = bb(pvs)
            for n in targets:
                if n == src or n not in T or allowed(src, n) or n in movers: continue
                l2, h2 = BB[n]
                if any(lo[i] > h2[i] + 0.03 or hi[i] < l2[i] - 0.03 for i in range(3)): continue
                d = pair(T[n][0], T[n][1], pt, pvs, closed_part=False, reach=0.03)
                key = (src, n)
                if key not in tw or d["gap"] < tw[key][0]: tw[key] = (d["gap"], dz, side, d["cut"], d["at"])
for (src, n), (g, dz, side, cut, at) in sorted(tw.items(), key=lambda x: x[1][0]):
    if g < 0.03:
        L.append(f"  {src:22s} vs {n:24s} {g*1000:6.1f} mm at {dz*100:+.0f} cm ({side})" + ("  INTERSECTS" if cut else ("" if g >= 0.010 else "  UNDER 10 mm")) + (f"  near {at}" if g < 0.010 else ""))
L.append(f"  -> worst under travel: {min(v[0] for v in tw.values())*1000 if tw else 30:.1f} mm")
out = f"/home/claude/octavia_drive/work/check_{cfg}.txt"
open(out, "w").write("\n".join(L) + "\n"); print("\n".join(L))
