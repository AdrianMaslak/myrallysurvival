"""Body clearance to the rear suspension through its travel (both cars). For each configuration the moving rear parts
are posed at -8, -4, +4, +8 cm (both sides); every Chassis vertex closer than 13 mm to a posed part is pushed away from it
(mostly upward), the push is spread to neighbours within 25 mm, capped at 30 mm, and stopped where it would come within
10 mm of a part that does not move. Output: work/body_push.json {vertex index: [dx, dy, dz]} for stage9b."""
import bpy, bmesh, sys, math, json
sys.path.insert(0, "/home/claude/octavia_body/tools")
from pairs import tree_bm_obj
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree
CLR, SPREAD, CAP, STATIC_MIN = 0.013, 0.025, 0.030, 0.010
def rot_about(p0, axis, ang): return Matrix.Translation(p0) @ Matrix.Rotation(ang, 4, axis) @ Matrix.Translation(-p0)

def posed_sets(cfg, O):
    out = []
    for side in ("L", "R"):
        sx = -1 if side == "L" else 1
        for dz in (-0.08, -0.04, 0.04, 0.08):
            vs_list = []
            def add(o, Mx, keep=None):
                M = o.matrix_world
                vs = [(Mx @ (M @ v.co)) if (keep is None or keep(M @ v.co)) else (M @ v.co) for v in o.data.vertices]
                vs_list.append((vs, [tuple(p.vertices) for p in o.data.polygons], o.name))
            if cfg == "4x4":
                for nm in ("LowerLink", "UpperLink"):
                    o = O[f"{nm}_R{side}"]; p0 = o.matrix_world.translation; r = 0.334 if nm == "LowerLink" else 0.365   # inner to outer joint (hard points)
                    add(o, rot_about(p0, Vector((0, 1, 0)), -sx * math.asin(dz / r)))
                for nm in ("Driveshaft", "CVBoot_Outer"):
                    add(O[f"{nm}_R{side}"], rot_about(Vector((sx * 0.201, -1.275, 0.295)), Vector((0, 1, 0)), -sx * math.asin(dz / 0.44)))
                ta = O[f"TrailingArm_R{side}"]; piv = ta.matrix_world.translation
                sys.path.insert(0, "/home/claude/octavia_drive/work"); from ta_split import ta_posed_vertices
                vs_list.append((ta_posed_vertices(ta, rot_about(piv, Vector((1, 0, 0)), -math.asin(dz / 0.47))), [tuple(p.vertices) for p in ta.data.polygons], ta.name))
                for nm in ("HubCarrier", "CaliperCarrier", "BrakeCaliper", "BrakeDisc", "CVJoint_Outer"):
                    add(O[f"{nm}_R{side}"], Matrix.Translation((0, 0, dz)))
            else:
                piv = Vector((sx * 0.545, -0.842, 0.245)); W = Vector((sx * 0.702, -1.307, 0.272))
                Mx = rot_about(piv, Vector((1, 0, 0)), -math.asin(dz / (W - piv).length))
                add(O["TwistBeam"], Mx, keep=lambda w, sx=sx: w.x * sx > 0.30)          # one side (cross member twists)
                if side == "L": add(O["TwistBeam"], rot_about(Vector((0, -0.842, 0.245)), Vector((1, 0, 0)), -math.asin(dz / (W - piv).length)))  # both
                for nm in ("RearHub", "CaliperCarrier", "BrakeCaliper", "BrakeDisc"):
                    add(O[f"{nm}_R{side}"], Matrix.Translation((0, 0, dz)))
            out.append(((cfg, side, dz), vs_list))
    return out

push = {}
for cfg in ("4x4", "fwd"):
    bpy.ops.wm.open_mainfile(filepath=f"/home/claude/octavia_drive/work/config_{cfg}.blend")
    O = bpy.data.objects; ch = O["Chassis"]; M = ch.matrix_world
    cw = [M @ v.co for v in ch.data.vertices]
    region = [i for i, w in enumerate(cw) if 0.20 < abs(w.x) < 0.66 and -1.55 < w.y < -0.75 and 0.22 < w.z < 0.72]
    movers = {"LowerLink", "UpperLink", "Driveshaft", "CVBoot_Outer", "TrailingArm", "HubCarrier", "CaliperCarrier", "BrakeCaliper",
              "BrakeDisc", "CVJoint_Outer", "TwistBeam", "RearHub", "Tyre", "Rim", "BrakePadInner", "BrakePadOuter", "BrakeHose", "AntiRollBarLink"}
    statics = [o for o in O if o.type == 'MESH' and o.name != "Chassis" and not any(o.name.startswith(m) for m in movers)
               and not any(k in o.name for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_")) and o.name not in ("RearBumper", "FrontBumper", "Fender_FL", "Fender_FR")]
    st_trees = [tree_bm_obj(o)[0] for o in statics]
    for key, vs_list in posed_sets(cfg, O):
        trees = [BVHTree.FromPolygons(vs, polys) for vs, polys, _n in vs_list]
        for i in region:
            w = cw[i] + Vector(push.get(i, (0, 0, 0)))
            best = None
            for t in trees:
                h = t.find_nearest(w, CLR)
                if h[0] is not None and (best is None or h[3] < best[3]): best = h
            if best is None: continue
            d = w - best[0]; dist = d.length
            u = d.normalized() if dist > 1e-6 else Vector((0, 0, 1))
            if u.z < 0.5: u = (u + Vector((0, 0, 1.0))).normalized()          # body moves away, mostly upward
            need = (CLR - dist) / max(0.3, u.dot(d.normalized() if dist > 1e-6 else u))
            cur = Vector(push.get(i, (0, 0, 0))); add = u * min(need, CAP)
            push[i] = tuple(cur + add)
    print(cfg, "pushed vertices so far", len(push))

# spread to neighbours (smooth falloff), cap, static-part guard (using the 4x4 scene, which has the most parts)
bpy.ops.wm.open_mainfile(filepath="/home/claude/octavia_drive/work/config_4x4.blend")
O = bpy.data.objects; ch = O["Chassis"]; M = ch.matrix_world; cw = [M @ v.co for v in ch.data.vertices]
kd = KDTree(len(cw))
for i, w in enumerate(cw): kd.insert(w, i)
kd.balance()
final = {}
for i, p in push.items():
    pv = Vector(p)
    for co, j, dd in kd.find_range(cw[i], SPREAD):
        f = (1 - dd / SPREAD); f = f * f * (3 - 2 * f)
        cur = Vector(final.get(j, (0, 0, 0)))
        cand = pv * f
        if cand.length > cur.length: final[j] = tuple(cand)
for j in list(final):
    v = Vector(final[j])
    if v.length > CAP: final[j] = tuple(v.normalized() * CAP)
statics = [o for o in O if o.type == 'MESH' and o.name.startswith(("Spring_", "Damper_", "RearSubframe", "RearFinalDrive", "RearAntiRollBar",
                                                                     "FuelTank", "FuelFillerNeck", "Exhaust_", "Propshaft"))]
st = [tree_bm_obj(o)[0] for o in statics]
cut = 0
for j in list(final):
    v = Vector(final[j]); w = cw[j] + v
    near = min((t.find_nearest(w, 0.05)[3] for t in st if t.find_nearest(w, 0.05)[0] is not None), default=1.0)
    if near < STATIC_MIN:
        final[j] = tuple(v * max(0.0, 1 - (STATIC_MIN - near) / max(v.length, 1e-6))); cut += 1
mx = max((Vector(v).length for v in final.values()), default=0)
print("final pushed vertices", len(final), "max mm", round(mx * 1000, 1), "limited by static parts", cut)
json.dump({str(k): [round(c, 5) for c in v] for k, v in final.items()}, open("/home/claude/octavia_drive/work/body_push.json", "w"))
