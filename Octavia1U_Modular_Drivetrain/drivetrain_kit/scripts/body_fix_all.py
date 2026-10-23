"""One coherent body push for both cars (replaces the earlier passes).
Movers: 4x4 rear links, driveshafts, trailing arms (bracket plates fixed), hub/brake parts at -8..+8 cm; FWD twist beam
(each side and both) and its hubs/brakes at -8..+8 cm. Chassis faces cut by or within 13 mm of any posed mover get their
corners moved away in 3 mm steps (links and shafts: biased upward; trailing arm: straight away; damper-tower lip: straight
up, up to 80 mm; elsewhere max 45 mm). A step is refused if it brings the vertex within 10 mm of any part that does not
move (in either car) and closer than it was. Then spread to neighbours within 25 mm (same guard). -> work/body_push.json"""
import bpy, sys, math, json
sys.path.insert(0, "/home/claude/octavia_drive/work"); sys.path.insert(0, "/home/claude/octavia_body/tools")
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree
from pairs import tree_bm_obj
src = open("/home/claude/octavia_drive/work/body_push.py").read().split("push = {}")[0]
ns = {}; exec(src, ns); posed_sets = ns["posed_sets"]
CLR, STEP, CAP, CAP_T, SPREAD, GUARD = 0.013, 0.003, 0.045, 0.080, 0.025, 0.010
MOVERS = ("LowerLink", "UpperLink", "Driveshaft", "CVBoot_Outer", "TrailingArm", "HubCarrier", "CaliperCarrier", "BrakeCaliper",
          "BrakeDisc", "CVJoint_Outer", "TwistBeam", "RearHub", "Tyre_R", "Rim_R", "BrakePadInner_R", "BrakePadOuter_R", "BrakeHose_R",
          "AntiRollBarLink_R", "Spring_R", "Damper_R")
mover_trees, mover_names, static_trees = [], [], []
for cfg in ("fwd", "4x4"):
    bpy.ops.wm.open_mainfile(filepath=f"/home/claude/octavia_drive/work/config_{cfg}.blend")
    O = bpy.data.objects
    for key, vl in posed_sets(cfg, O):
        for vs, pl, nm in vl: mover_trees.append(BVHTree.FromPolygons(vs, pl)); mover_names.append(nm)
    for o in O:
        if o.type != 'MESH' or o.name in ("Chassis", "FrontBumper", "RearBumper", "Fender_FL", "Fender_FR"): continue
        if any(k in o.name for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_")): continue
        if o.name.startswith(MOVERS) and not o.name.startswith("TrailingArm"): continue
        if o.name.startswith("TrailingArm"):          # its fixed bracket plates guard; the arm is a mover
            import ta_split
            from mathutils import Matrix
            vs = ta_split.ta_posed_vertices(o, Matrix.Identity(4)); static_trees.append(BVHTree.FromPolygons(vs, [tuple(p.vertices) for p in o.data.polygons]))
            continue
        static_trees.append(tree_bm_obj(o)[0])
    if cfg == "4x4":
        ch = O["Chassis"]; M = ch.matrix_world; me = ch.data
        base = [M @ v.co for v in me.vertices]
        reg_f = [p for p in me.polygons if any(0.18 < abs(base[i].x) < 0.68 and -1.58 < base[i].y < -0.72 and 0.20 < base[i].z < 0.74 for i in p.vertices)]
print("movers", len(mover_trees), "static guards", len(static_trees))
def near_static(w, r=0.03):
    best = r
    for t in static_trees:
        h = t.find_nearest(w, best)
        if h[0] is not None: best = h[3]
    return best
rv = sorted({i for p in reg_f for i in p.vertices}); rix = {i: k for k, i in enumerate(rv)}
polys = [tuple(rix[i] for i in p.vertices) for p in reg_f]
disp = {}
for it in range(int(CAP_T / STEP)):
    cur = [base[i] + Vector(disp.get(i, (0, 0, 0))) for i in rv]
    ct = BVHTree.FromPolygons(cur, polys)
    bad = set()
    for mt in mover_trees:
        for a, b in ct.overlap(mt): bad.update(polys[a])
        for k, w in enumerate(cur):
            if mt.find_nearest(w, CLR)[0] is not None: bad.add(k)
    for f in polys:
        c = sum((cur[k] for k in f), Vector()) / len(f)
        if any(mt.find_nearest(c, CLR)[0] is not None for mt in mover_trees): bad.update(f)
    moved = 0
    for k in bad:
        w = cur[k]; best, bi = None, None
        for mi, mt in enumerate(mover_trees):
            h = mt.find_nearest(w, 0.06)
            if h[0] is not None and (best is None or h[3] < best[3]): best, bi = h, mi
        u = (w - best[0]).normalized() if best is not None and best[3] > 1e-6 else Vector((0, 0, 1))
        i = rv[k]; b0 = base[i]
        tower = (0.45 < abs(b0.x) < 0.64 and -1.31 < b0.y < -1.17 and b0.z > 0.36) or \
                (0.47 < abs(b0.x) < 0.60 and -1.02 < b0.y < -0.90 and b0.z > 0.34)      # damper-tower lip / trailing-arm pocket roof: straight up
        if tower: u = Vector((0, 0, 1))
        elif not (bi is not None and mover_names[bi].startswith("TrailingArm")): u = (u + Vector((0, 0, 0.8))).normalized()
        d = Vector(disp.get(i, (0, 0, 0))) + u * STEP
        if d.length > (CAP_T if tower else CAP): continue
        nw = b0 + d
        if near_static(nw) < GUARD and near_static(nw) < near_static(w): continue
        disp[i] = tuple(d); moved += 1
    print("iteration", it, "to move", len(bad), "moved", moved, flush=True)
    if not bad or not moved: break
if bad:
    import collections; who = collections.Counter()
    for k in bad:
        for mi, mt in enumerate(mover_trees):
            if mt.find_nearest(cur[k], 0.02)[0] is not None: who[(mover_names[mi], tuple(round(c, 2) for c in cur[k]))] += 1
    for w_, n_ in who.most_common(20): print("   LEFT", w_)
# spread with the same guard
cw = base
kd = KDTree(len(cw))
for i, w in enumerate(cw): kd.insert(w, i)
kd.balance()
final = dict(disp)
for i, p in disp.items():
    pv = Vector(p)
    for co, j, dd in kd.find_range(cw[i], SPREAD):
        if j in disp: continue
        f = 1 - dd / SPREAD; f = f * f * (3 - 2 * f); cand = pv * f
        if cand.length <= Vector(final.get(j, (0, 0, 0))).length: continue
        nw = cw[j] + cand
        if near_static(nw) < GUARD and near_static(nw) < near_static(cw[j]): continue
        final[j] = tuple(cand)
print("final pushed vertices", len(final), "max mm", round(max((Vector(v).length for v in final.values()), default=0) * 1000, 1))
json.dump({str(k): [round(c, 5) for c in v] for k, v in final.items()}, open("/home/claude/octavia_drive/work/body_push.json", "w"))
