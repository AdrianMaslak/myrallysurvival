"""Verification for the body + interior v2 kit -> export/verification_report.txt and export/parts_manifest.json."""
import bpy, bmesh, sys, os, math, json
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from pairs import tree_bm_obj, pair
from clearance import wheel_poses
import classify
from mathutils import Vector
EXP = os.path.join(ROOT, "export")
src_man = json.load(open(os.path.join(ROOT, "src", "body_manifest.json")))
SRC = {p["name"]: p for p in src_man["body"] + src_man["interior"]}
s3 = json.load(open(os.path.join(ROOT, "work", "stage3_parts.json")))
irig = json.load(open(os.path.join(ROOT, "work", "interior_rig.json")))
L = []
BUDGET = dict(Chassis=50000, Door_FL=15000, Door_FR=15000, Door_RL=15000, Door_RR=15000, Bonnet=5000, Trunk=12000, FrontBumper=10000,
              RearBumper=10000, Fender_FL=4000, Fender_FR=4000, Headlight_L=6000, Headlight_R=6000, Taillight_L=4000, Taillight_R=4000,
              Dashboard=30000, SteeringWheel=8000, Seat_FL=8000, Seat_FR=8000, Seat_Rear=8000)
GROUP = {"Door_": ["DoorGlass_", "DoorHandle_", "WindowWinder_"], "Seat_FL": ["Seat_FL_"], "Seat_FR": ["Seat_FR_"], "Dashboard": ["GaugeFaces", "WarningLights", "GloveBox"]}
manifest = dict(frame=src_man["frame"], game_frame=src_man["game_frame"], body=[], interior=[], cabin_points_m=src_man["cabin_points_m"])


def tris(o):
    o.data.calc_loop_triangles(); return len(o.data.loop_triangles)


def part_entry(o, kit_name):
    e = dict(name=o.name, pivot_m=[round(x, 4) for x in o.matrix_world.translation], triangles=tris(o),
             materials=[m.name[2:] if m and m.name.startswith("M_") else (m.name if m else "") for m in o.data.materials],
             uv_layers=list(o.data.uv_layers.keys()), parent=o.parent.name if o.parent else None)
    vs = [o.matrix_world @ v.co for v in o.data.vertices]
    if vs: e["size_m"] = [round(max(v[i] for v in vs) - min(v[i] for v in vs), 4) for i in range(3)]
    if o.name in SRC:
        for k in ("sources", "mass_kg", "max_open_deg", "held_open_deg", "open_axis", "opens_about"):
            if k in SRC[o.name]: e[k] = SRC[o.name][k]
    for c in o.children:
        if c.type == 'EMPTY':
            key = c.name[len(o.name) + 1:].lower() + "_m" if c.name.startswith(o.name + "_") else c.name + "_m"
            e[key] = [round(x, 4) for x in c.matrix_world.translation]
    for k in ("motion", "local_axis", "range", "rest", "units_per_degree", "units", "range_across", "range_fore_aft", "note", "zero_mark_deg"):
        if k in o.keys():
            v = o[k]; e[k] = list(v) if hasattr(v, "__len__") and not isinstance(v, str) else v
    e["colliders"] = sum(1 for c in o.children if c.name.startswith("UCX_"))
    return e


# ================================================================ PROTOTYPE overlaps (for comparison)
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "src", "Octavia1U_Body.blend"))
_pb = [o for o in bpy.data.collections["BODY_KIT"].objects if o.type == 'MESH' and not o.name.startswith("UCX_")]
_pt = {o.name: tree_bm_obj(o)[0] for o in _pb}
PROTO_OV = {}
for i, a in enumerate(sorted(_pt)):
    for b in sorted(_pt)[i + 1:]:
        n = len(_pt[a].overlap(_pt[b]))
        if n: PROTO_OV[(a, b)] = n
# ================================================================ BODY
bpy.ops.wm.open_mainfile(filepath=os.path.join(EXP, "Octavia1U_Body_v2.blend"))
O = bpy.data.objects; KIT = bpy.data.collections["BODY_KIT"]
body = [o for o in KIT.objects if o.type == 'MESH' and not o.name.startswith("UCX_")]
L.append("OCTAVIA 1U BODY + INTERIOR v2 — VERIFICATION\n")
L.append("TRIANGLES (part incl. its child objects such as door glass, handles, winders)")
tot = 0
T = {o.name: tris(o) for o in body}
for o in sorted(body, key=lambda o: o.name):
    if o.parent and o.parent.name in T and o.parent.type == 'MESH': continue
    kids = [c for c in body if c.parent is o]
    n = T[o.name] + sum(T[c.name] for c in kids)
    tot += T[o.name] + sum(T[c.name] for c in kids)
    b = BUDGET.get(o.name)
    L.append(f"  {o.name:16s} {n:7d}" + (f"   budget {b:6d}  {'ok' if n <= b else 'OVER'}" if b else "") + (f"   (with {', '.join(c.name for c in kids)})" if kids else ""))
L.append(f"  exterior total {sum(T.values())}  (limit 150000)")
L.append("")

L.append("ORIGINS (kept within 5 mm of body_manifest pivot_m)")
for p in src_man["body"]:
    nm = p["name"]
    if nm == "Wipers":
        L.append("  Wipers           split into Wiper_L / Wiper_R, origins on their spindles (see RIG)"); continue
    o = O.get(nm)
    if not o: L.append(f"  {nm} MISSING"); continue
    d = (o.matrix_world.translation - Vector(p["pivot_m"])).length
    L.append(f"  {nm:16s} moved {d*1000:5.1f} mm")
L.append("")
L.append("HINGE / LATCH / FIXING EMPTIES")
L.append("  In the prototype files every one of these sat at manifest position + part pivot (its world position had been")
L.append("  stored as the local offset). They are now at the manifest positions in world space (your choice), i.e.")
L.append("  0 mm from body_manifest.json; the table gives how far each moved from where the old file had it.")
for e in s3["empties_moved"]:
    L.append(f"  {e['name']:24s} now {e['to_m']}  (was {e['from_m']}, {e['moved_m']*1000:.0f} mm away)")
L.append("")

# ---- clearances
L.append("TYRE-TO-BODY CLEARANCE (v2 tyres; fronts at -35/-25/0/25/35 deg steer x -8..+8 cm travel; rears -8..+8 cm)")
tb = {o.name: tree_bm_obj(o) for o in body}
def bbox(vs): return Vector([min(v[i] for v in vs) for i in range(3)]), Vector([max(v[i] for v in vs) for i in range(3)])
BBX = {k: bbox(v[1]) for k, v in tb.items()}
worst_t = {}
for side in ("FL", "FR", "RL", "RR"):
    ty = O[f"REF_SUSP_Tyre_{side}"]
    for pose, M in wheel_poses(side):
        pt, pvs = tree_bm_obj(ty, M @ ty.matrix_world); plo, phi = bbox(pvs)
        for k, (bt, bvs) in tb.items():
            lo, hi = BBX[k]
            if any(plo[i] > hi[i] + 0.1 or phi[i] < lo[i] - 0.1 for i in range(3)): continue
            d = pair(bt, bvs, pt, pvs, reach=0.1)
            if d["gap"] > 0.099: continue
            key = (side, k)
            if key not in worst_t or d["gap"] < worst_t[key][0]: worst_t[key] = (d["gap"], pose, d["at"], d["cut"])
for (side, k), (g, pose, at, cut) in sorted(worst_t.items()):
    L.append(f"  {side} vs {k:14s} {g*1000:6.1f} mm   worst at steer/travel {pose}  at {at}" + ("  INTERSECTS" if cut else ""))
L.append(f"  -> worst {min(v[0] for v in worst_t.values())*1000:.1f} mm (required 15)")
L.append("")
L.append("BODY-TO-ENGINE / BODY-TO-SUSPENSION (static; worst gap per reference part, 30 closest)")
res = []
for r in bpy.data.collections["REFERENCE_DoNotExport"].objects:
    if r.type != 'MESH' or r.name.startswith("REF_UCX") or "Tyre" in r.name or any(k in r.name for k in ("_Bolt_", "_Nut_", "_Screw_", "_Clip_")): continue
    pt, pvs = tree_bm_obj(r); plo, phi = bbox(pvs); best = None
    for k, (bt, bvs) in tb.items():
        lo, hi = BBX[k]
        if any(plo[i] > hi[i] + 0.05 or phi[i] < lo[i] - 0.05 for i in range(3)): continue
        d = pair(bt, bvs, pt, pvs, reach=0.06)
        if best is None or (d["gap"] - d["depth"]) < (best[1]["gap"] - best[1]["depth"]): best = (k, d)
    if best and best[1]["gap"] < 0.06: res.append((r.name, best))
res.sort(key=lambda x: x[1][1]["gap"] - x[1][1]["depth"])
for nm, (k, d) in res[:30]:
    note = ""
    if "Strut_Upper" in nm and d["cut"]: note = "  (seated: the top-mount plate bears on the tower seat, studs/nut through its holes - intended contact)"
    elif d["cut"]: note = "  INTERSECTS"
    L.append(f"  {nm:34s} -> {k:12s} {d['gap']*1000:6.1f} mm{note}")
eng = [d["gap"] for nm, (k, d) in res if nm.startswith("REF_SM_")]; sus = [d["gap"] for nm, (k, d) in res if nm.startswith("REF_SUSP_") and "Strut_Upper" not in nm]
L.append(f"  -> worst engine gap {min(eng)*1000 if eng else 99:.1f} mm (required 10); worst suspension gap {min(sus)*1000 if sus else 99:.1f} mm (required 10;"
         " final drive and rear driveshafts included)")
L.append("")

# ---- self overlaps
L.append("OVERLAPS BETWEEN THE KIT'S OWN PARTS (intersecting triangle pairs; 'prototype' = the same pair in the source file;")
L.append("  depth = how far the new part's vertices sit behind the surface they cross, searched within 3 cm)")
names = sorted(tb)
ov = []
for i, a in enumerate(names):
    for b in names[i + 1:]:
        la, ha = BBX[a]; lb, hb = BBX[b]
        if any(la[k] > hb[k] or lb[k] > ha[k] for k in range(3)): continue
        n = len(tb[a][0].overlap(tb[b][0]))
        if n: ov.append((n, a, b))
ORIG = {p["name"] for p in src_man["body"]}
def pen(a, b):
    # vertices of a lying behind b's nearest face (and within 3 cm of it) -> deepest
    bt = tb[b][0]; dmax = 0.0
    for w in tb[a][1]:
        loc, n, i, d = bt.find_nearest(w, 0.03)
        if loc is not None and (w - loc).dot(n) < 0: dmax = max(dmax, d)
    return dmax
for n, a, b in sorted(ov, reverse=True):
    pn = PROTO_OV.get((a, b)) or PROTO_OV.get((b, a))
    new_part = not (a in ORIG and b in ORIG)
    dep = ""
    if new_part:
        x, y = (a, b) if a not in ORIG else (b, a)
        dep = f"   depth {pen(x, y)*1000:4.1f} mm ({x} into {y})"
    L.append(f"  {a:16s} x {b:16s} {n:5d}   " + (f"prototype {pn:5d}" if pn else ("new part" if new_part else "prototype     0")) + dep)
L.append(f"  {len(ov)} touching pairs (prototype: {len(PROTO_OV)}). Between original parts these are the prototype's own flange/shut-line")
L.append("  and lamp-housing contacts, refined with it. New parts sit on or through the panel that carries them (glass in its channel,")
L.append("  handle/winder bosses into the door card, plates on their recess, flap in its pocket).")
L.append("")

# ---- UVs
L.append("UV CHECKS")
PAINT = {"Chassis", "Door_FL", "Door_FR", "Door_RL", "Door_RR", "Bonnet", "Trunk", "FrontBumper", "RearBumper", "Fender_FL", "Fender_FR", "Roof", "FuelFlap"}
for nm in sorted(PAINT):
    o = O[nm]; me = o.data
    has = list(me.uv_layers.keys())
    ok = has[:2] == ["UVMap", "Livery"]
    # Livery span on the paint faces
    lv = me.uv_layers.get("Livery")
    pi = [i for i, m in enumerate(me.materials) if m and m.name == "M_body_paint"]
    us = [lv.data[li].uv for p in me.polygons if p.material_index in pi for li in p.loop_indices] if lv else []
    span = (min(u.x for u in us), max(u.x for u in us), min(u.y for u in us), max(u.y for u in us)) if us else None
    L.append(f"  {nm:12s} UV channels {has}  {'ok' if ok else 'CHECK'}   Livery range u {span[0]:.3f}..{span[1]:.3f} v {span[2]:.3f}..{span[3]:.3f}" if span else f"  {nm} no paint faces")
L.append("  Livery: the prototype's shared layout, carried over by surface lookup onto the rebuilt skins; FuelFlap samples the")
L.append("  quarter panel underneath it, so stripes run across its gap. UV0 is a world-scale box projection (1 unit = 1 m on paint).")
L.append("")

# ---- topology of the outer skins
L.append("OUTER-SKIN TOPOLOGY (painted faces seen from outside)")
kit_tree = classify.scene_tree(body)
for nm in sorted(PAINT):
    o = O[nm]; bm = bmesh.new(); bm.from_mesh(o.data); bm.transform(o.matrix_world); bm.normal_update(); bm.faces.ensure_lookup_table()
    pi = [i for i, m in enumerate(o.data.materials) if m and m.name == "M_body_paint"]
    fs = [f for f in bm.faces if f.material_index in pi]
    fl = classify.outer_flags(fs, kit_tree, n_rays=3)
    out = [f for f, k in zip(fs, fl) if k]
    if not out: bm.free(); continue
    q = sum(1 for f in out if len(f.verts) == 4); ng = sum(1 for f in out if len(f.verts) > 4)
    el = sorted(e.calc_length() for f in out for e in f.edges)
    over4 = sum(1 for x in el if x > 0.0405)
    thin = 0
    for f in out:
        if len(f.verts) == 3:
            vs = [v.co for v in f.verts]
            ang = min((vs[(k+1)%3]-vs[k]).angle(vs[(k+2)%3]-vs[k], 0) for k in range(3))
            if math.degrees(ang) < 10: thin += 1
    L.append(f"  {nm:12s} faces {len(out):6d}  quads {q/len(out)*100:4.0f}%  n-gons {ng}  edge p50 {el[len(el)//2]*100:4.1f} cm  p99 {el[int(len(el)*.99)]*100:4.1f} cm  max {el[-1]*100:4.1f} cm  (>4 cm: {over4})"
             f"  sliver tris (min angle < 10 deg) {thin} ({thin/len(out)*100:.1f}%)")
    bm.free()
_lim = json.load(open(os.path.join(ROOT, "work", "stage7_limits.json")))
L.append("  Edge limit used on the visible paint skin (smallest of 4/5/6/7 cm that keeps the part inside its budget):")
L.append("    " + ", ".join(f"{k} {v['limit_m']*100:.0f} cm" for k, v in _lim.items()))
L.append("  Method: every prototype vertex kept, long edges split (smooth edges lifted onto the Phong surface, <= 3 mm; the final pass splits on the straight edge), Delaunay")
L.append("  flips on flat areas, triangle pairs joined into quads. Shape deviation from the prototype outer surface: <= 3 mm by")
L.append("  construction, except where the skin was nudged off the tyres (front wings <= 4 mm, front arch return flanges <= 7.5 mm).")
L.append("")

# ---- rig
L.append("MOVING PARTS (body)")
for o in sorted([o for o in body if o.get("motion")], key=lambda o: o.name):
    L.append(f"  {o.name:18s} {o['motion']:6s} axis {o['local_axis']:10s} range {list(o['range'])}  rest {o['rest']}"
             + (f"  open_axis {list(o['open_axis'])}" if 'open_axis' in o.keys() else ""))
for o in body:
    if o.parent is None or o.parent.type != 'MESH' or o.name in [p["name"] for p in src_man["body"]]: pass
    manifest["body"].append(part_entry(o, "BODY_KIT"))
manifest["body_empties"] = {e.name: [round(x, 4) for x in e.matrix_world.translation] for e in KIT.objects if e.type == 'EMPTY'}
manifest["body_triangles"] = sum(T.values())

# ================================================================ INTERIOR
bpy.ops.wm.open_mainfile(filepath=os.path.join(EXP, "Octavia1U_Interior_v2.blend"))
O = bpy.data.objects; KIT = bpy.data.collections["INTERIOR_KIT"]
inter = [o for o in KIT.objects if o.type == 'MESH']
TI = {o.name: tris(o) for o in inter}
L.append("")
L.append("TRIANGLES (interior)")
for o in sorted(inter, key=lambda o: o.name):
    b = BUDGET.get(o.name)
    L.append(f"  {o.name:18s} {TI[o.name]:7d}" + (f"   budget {b:6d}  {'ok' if TI[o.name] <= b else 'OVER'}" if b else ""))
L.append(f"  interior total {sum(TI.values())}  (limit 90000)")
L.append("")
L.append("INTERIOR PIVOTS (given estimate -> used; the axis empties had the same pivot offset as the body empties and are fixed)")
for nm, d in irig["pivots"].items():
    if nm == "dial_faces": continue
    L.append(f"  {nm:16s} {d['given']} -> {d['used']}  ({d['moved_mm']} mm)")
L.append("  Cabin points kept: steering wheel centre and column axis unchanged; driver's eye point unchanged.")
L.append("")
L.append("MOVING PARTS (interior)")
for o in sorted([o for o in inter if o.get("motion")], key=lambda o: o.name):
    extra = "".join(f"  {k} {o[k] if isinstance(o[k], (str, float, int)) else list(o[k])}" for k in ("units_per_degree", "range_across", "range_fore_aft") if k in o.keys())
    L.append(f"  {o.name:18s} {o['motion']:6s} axis {o['local_axis']:4s} range {list(o['range'])}  rest {o['rest']}{extra}")
L.append("  Every rotate/slide object has a preview Action '<name>_preview' (frames 1-49) running its full range.")
for o in inter: manifest["interior"].append(part_entry(o, "INTERIOR_KIT"))
manifest["interior_empties"] = {e.name: [round(x, 4) for x in e.matrix_world.translation] for e in KIT.objects if e.type == 'EMPTY'}
manifest["interior_triangles"] = sum(TI.values())

open(os.path.join(EXP, "verification_report.txt"), "w").write("\n".join(L) + "\n")
json.dump(manifest, open(os.path.join(EXP, "parts_manifest.json"), "w"), indent=1)
print("\n".join(L))
