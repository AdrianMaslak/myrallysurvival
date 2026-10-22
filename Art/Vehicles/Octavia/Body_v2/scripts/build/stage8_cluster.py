"""Stage 8 (interior): instrument cluster rework (v2.1). Only the cluster changes.
- removes the prototype faces that sat in front of the dial planes (they hid the lower half of every dial)
- GaugeFaces rebuilt: full-circle speedo / tacho / temp / fuel faces + centre panel, chrome bezels, hub caps
- WarningLights rebuilt: 18 tell-tales (unlit look) on the centre panel and in the lower dials; each tell-tale also has a
  lit child object WL_<id> (show it = lamp on)
- needles get their own orange-red illuminated material (M_gauge_needle)
Needle pivots, axes, ranges, units_per_degree and preview actions are untouched."""
import bpy, bmesh, sys, os, math, json
from mathutils import Vector, Matrix
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools")); sys.path.insert(0, HERE)
import finish_lib as FL
import cluster_textures as CT
TEX = os.path.join(ROOT, "export", "textures")
B = os.path.join(ROOT, "export", "Octavia1U_Interior_v2.blend")
bpy.ops.wm.open_mainfile(filepath=B)
O = bpy.data.objects; KIT = bpy.data.collections["INTERIOR_KIT"]
irig = json.load(open(os.path.join(ROOT, "work", "interior_rig.json")))
YD = irig["pivots"]["dial_faces"]                      # dial-plane y per needle (from stage 5)
for im in bpy.data.images:
    if im.name.startswith("gauge_"): im.reload()
GF = bpy.data.materials["M_gauge_faces"]; GW = bpy.data.materials["M_gauge_warning"]
GN = FL.make_material("gauge_needle", TEX); CHR = bpy.data.materials["M_chrome"]; BLK = bpy.data.materials["M_plastic_black"]


# needles: the prototype blades lie in the old tilted dial plane, behind the new faces. New flat blades just in front of
# each face, pointing at the zero mark at rest; the object (origin, rotation, rig, actions) is unchanged.
def rebuild_needle(nm, R):
    o = O[nm]; c = o.matrix_world.translation; yd = YD[nm]
    a = math.radians(o.get("zero_mark_deg", -120)); d = Vector((math.sin(a), 0, math.cos(a))); n = Vector((math.cos(a), 0, -math.sin(a)))
    L = 0.88 * R; prof = ((-0.17 * L, 0.0013), (0.0, 0.0016), (0.80 * L, 0.0009), (L, 0.0004))
    b = bmesh.new(); rings = []
    for t, w in prof:
        base = Vector((c.x, 0, c.z)) + d * t
        rings.append([b.verts.new(Vector((base.x + n.x * s_ * w, yd + dy, base.z + n.z * s_ * w))) for dy in (-0.0024, -0.0012) for s_ in (-1, 1)])
    for r0, r1 in zip(rings, rings[1:]):
        for i, j in ((0, 1), (1, 3), (3, 2), (2, 0)): b.faces.new((r0[i], r0[j], r1[j], r1[i]))
    b.faces.new((rings[0][0], rings[0][2], rings[0][3], rings[0][1])); b.faces.new((rings[-1][0], rings[-1][1], rings[-1][3], rings[-1][2]))
    bmesh.ops.recalc_face_normals(b, faces=b.faces)
    b.transform(o.matrix_world.inverted()); b.to_mesh(o.data); b.free()
    if "UVMap" not in o.data.uv_layers: o.data.uv_layers.new(name="UVMap")
for nm, R in (("Needle_Speedo", 0.066), ("Needle_Tacho", 0.066), ("Needle_Temp", 0.0185), ("Needle_Fuel", 0.0185)): rebuild_needle(nm, R)


def needle_front(nm):
    o = O[nm]; return min((o.matrix_world @ v.co).y for v in o.data.vertices)


DIAL = {}
for nm, big in (("Needle_Speedo", True), ("Needle_Tacho", True), ("Needle_Temp", False), ("Needle_Fuel", False)):
    p = O[nm].matrix_world.translation
    DIAL[nm] = dict(c=Vector((p.x, YD[nm], p.z)), R=0.066 if big else 0.0185, big=big, front=needle_front(nm))
S_, T_ = DIAL["Needle_Speedo"], DIAL["Needle_Tacho"]
BEZ = 0.0055                                           # bezel width (big dials); small: 0.003
gap_l = T_["c"].x + T_["R"] + BEZ; gap_r = S_["c"].x - S_["R"] - BEZ
XMID = (gap_l + gap_r) / 2
PANEL = dict(x0=gap_l - 0.004, x1=gap_r + 0.004, z0=0.846, z1=0.909, y=0.7040)
print("centre gap", round(gap_l, 4), round(gap_r, 4), "width mm", round((gap_r - gap_l) * 1000, 1))

# ------------------------------------------------------------------ 1. clear the prototype faces in front of the dial planes
dash = O["Dashboard"]; me = dash.data
bm = bmesh.new(); bm.from_mesh(me); Mw = dash.matrix_world
# the prototype dial plates are tilted and cut through the new (vertical) faces: remove every prototype face whose centre lies
# inside a bezel or the centre panel, in front of or just behind the new faces (the column shroud, y < 0.66, stays)
def blocked(w):
    for d in DIAL.values():
        r = math.hypot(w.x - d["c"].x, w.z - d["c"].z)
        if r < d["R"] + (BEZ if d["big"] else 0.0015) and 0.66 < w.y < d["c"].y + 0.03: return True
    return PANEL["x0"] < w.x < PANEL["x1"] and PANEL["z0"] < w.z < PANEL["z1"] and 0.66 < w.y < 0.725
kill = [f for f in bm.faces if blocked(Mw @ f.calc_center_median())]
bmesh.ops.delete(bm, geom=kill, context='FACES'); bm.to_mesh(me); bm.free()
print("dashboard faces removed in front of the dials", len(kill))

# ------------------------------------------------------------------ 2. GaugeFaces: faces, centre panel, bezels, hub caps
g = O["GaugeFaces"]
gb = bmesh.new(); uv = gb.loops.layers.uv.new("UVMap")
def face(vs, mi, uvs=None):
    f = gb.faces.new([gb.verts.new(v) for v in vs]); f.material_index = mi
    for l, t in zip(f.loops, uvs or [(0.98, 0.02)] * len(vs)): l[uv].uv = t
    return f
def ring_pt(c, r, a, dy=0.0): return Vector((c.x + r * math.sin(a), c.y + dy, c.z + r * math.cos(a)))
for nm, d in DIAL.items():
    (ax, ay), ar = CT.DIALS[nm]; c, R = d["c"], d["R"]; n = 72 if d["big"] else 40
    for k in range(n):                                  # dial disc (fan, UV straight into the atlas)
        a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1) / n
        vs = [c.copy(), ring_pt(c, R, a1), ring_pt(c, R, a0)]
        face(vs, 0, [((ax + (v.x - c.x) / R * ar) / CT.A, 1 - (ay - (v.z - c.z) / R * ar) / CT.A) for v in vs])
    bw = BEZ if d["big"] else 0.0015; depth = 0.006 if d["big"] else 0.004
    for k in range(n):                                  # chrome bezel: front annulus + inner and outer walls
        a0, a1 = 2 * math.pi * k / n, 2 * math.pi * (k + 1) / n
        face([ring_pt(c, R, a0, -depth), ring_pt(c, R, a1, -depth), ring_pt(c, R + bw, a1, -depth), ring_pt(c, R + bw, a0, -depth)], 1)
        face([ring_pt(c, R, a0, 0.0005), ring_pt(c, R, a1, 0.0005), ring_pt(c, R, a1, -depth), ring_pt(c, R, a0, -depth)], 1)
        face([ring_pt(c, R + bw, a0, -depth), ring_pt(c, R + bw, a1, -depth), ring_pt(c, R + bw, a1, 0.004), ring_pt(c, R + bw, a0, 0.004)], 1)
    rc = 0.0075 if d["big"] else 0.0042                 # hub cap in front of the needle hub (static)
    y0, y1 = d["front"] - 0.0006, d["front"] - 0.0040
    cc = Vector((c.x, y1, c.z)); m = 24
    for k in range(m):
        a0, a1 = 2 * math.pi * k / m, 2 * math.pi * (k + 1) / m
        face([cc, ring_pt(cc, rc, a1), ring_pt(cc, rc, a0)], 2)
        face([ring_pt(cc, rc, a0), ring_pt(cc, rc, a1), ring_pt(cc, rc, a1, y0 - y1), ring_pt(cc, rc, a0, y0 - y1)], 2)
# centre panel (plain face colour from the atlas)
px0, py0, px1, py1 = CT.PANEL_PX; u0, u1 = px0 / CT.A, px1 / CT.A; v0, v1 = 1 - py1 / CT.A, 1 - py0 / CT.A
P = PANEL
face([Vector((P["x0"], P["y"], P["z0"])), Vector((P["x1"], P["y"], P["z0"])), Vector((P["x1"], P["y"], P["z1"])), Vector((P["x0"], P["y"], P["z1"]))],
     0, [(u0, v0), (u1, v0), (u1, v1), (u0, v1)])
bmesh.ops.remove_doubles(gb, verts=gb.verts, dist=1e-6)
gb.transform(g.matrix_world.inverted()); gme = g.data; gme.materials.clear()
for m in (GF, CHR, BLK): gme.materials.append(m)
gb.to_mesh(gme); gb.free()
for p_ in gme.polygons: p_.use_smooth = p_.material_index == 1

# ------------------------------------------------------------------ 3. tell-tales
slots = {}
cen = [t for t in CT.TELLTALES if t[2] == "centre"]
ICON_C = 0.0105; rows = (len(cen) + 1) // 2; step = (P["z1"] - P["z0"] - 0.004) / rows
for i, t in enumerate(cen):
    r, cside = i // 2, i % 2
    slots[t[0]] = (Vector((XMID + (-1 if cside == 0 else 1) * 0.0068, P["y"] - 0.0006, P["z1"] - 0.002 - step * (r + 0.5))), ICON_C)
for where, nm in (("tacho", "Needle_Tacho"), ("speedo", "Needle_Speedo")):
    d = DIAL[nm]; ts = [t for t in CT.TELLTALES if t[2] == where]; R = d["R"]
    for k, t in enumerate(ts):
        u = (k - (len(ts) - 1) / 2) * 0.18 * R
        slots[t[0]] = (Vector((d["c"].x + u, d["c"].y - 0.0004, d["c"].z - 0.33 * R)), 0.0098)
# check the centre column icons stay clear of the bezels
for t in cen:
    p, s = slots[t[0]]
    for d in (S_, T_):
        for cx_, cz_ in ((p.x - s / 2, p.z - s / 2), (p.x + s / 2, p.z - s / 2), (p.x - s / 2, p.z + s / 2), (p.x + s / 2, p.z + s / 2)):
            if math.hypot(cx_ - d["c"].x, cz_ - d["c"].z) < d["R"] + BEZ: print("WARN icon under bezel", t[0])

def quad_bm(p, s, i, lit, b=None):
    b = b or bmesh.new(); u = b.loops.layers.uv.get("UVMap") or b.loops.layers.uv.new("UVMap")
    x0, y0, x1, y1 = CT.cell_px(i, lit)
    f = b.faces.new([b.verts.new(p + Vector(o)) for o in ((-s / 2, 0, -s / 2), (s / 2, 0, -s / 2), (s / 2, 0, s / 2), (-s / 2, 0, s / 2))])
    for l, (uu, vv) in zip(f.loops, ((x0, y1), (x1, y1), (x1, y0), (x0, y0))): l[u].uv = (uu / CT.WA_W, 1 - vv / CT.WA_H)
    return b

wl = O["WarningLights"]
for c in [c for c in wl.children]: bpy.data.objects.remove(c, do_unlink=True)
wb = bmesh.new(); wb.loops.layers.uv.new("UVMap")
for i, t in enumerate(CT.TELLTALES):
    p, s = slots[t[0]]; quad_bm(p, s, i, False, wb)
wb.transform(wl.matrix_world.inverted()); wme = wl.data; wme.materials.clear(); wme.materials.append(GW)
wb.to_mesh(wme); wb.free()
COLNAME = {CT.RED: "red", CT.AMB: "amber", CT.GRN: "green", CT.BLU: "blue"}
wl["telltales"] = [t[0] for t in CT.TELLTALES]
wl["note"] = "unlit tell-tale faces; each lamp is a child WL_<id>: show it (or raise its emission) when the lamp is on"
WL = {}
for i, t in enumerate(CT.TELLTALES):
    p, s = slots[t[0]]; p = p - Vector((0, 0.00025, 0))
    q = quad_bm(Vector((0, 0, 0)), s, i, True)
    m = bpy.data.meshes.new("WL_" + t[0]); q.to_mesh(m); q.free(); m.materials.append(GW)
    ob = bpy.data.objects.new("WL_" + t[0], m); KIT.objects.link(ob)
    ob.matrix_world = Matrix.Translation(p); ob.parent = wl; ob.matrix_parent_inverse = wl.matrix_world.inverted()
    ob["motion"] = "lamp"; ob["telltale"] = t[0]; ob["colour"] = COLNAME[t[1]]; ob["rest"] = "off"
    ob["note"] = "lamp face for the lit state: visible = on"
    WL["WL_" + t[0]] = dict(telltale=t[0], colour=COLNAME[t[1]], position_m=[round(x, 4) for x in p], size_m=round(s, 4), area=t[2])

# ------------------------------------------------------------------ 4. needles: their own illuminated material
for nm in DIAL:
    o = O[nm]; o.data.materials.clear(); o.data.materials.append(GN); FL.grime(o, lambda p: 0.0)
    for p_ in o.data.polygons: p_.material_index = 0

# grime (clean), smooth flags
for ob in [g, wl] + [O[k] for k in WL]:
    if "UVMap" not in ob.data.uv_layers: ob.data.uv_layers.new(name="UVMap")
    FL.grime(ob, lambda p: 0.0)
tris = {}
for ob in [o for o in KIT.objects if o.type == 'MESH']: ob.data.calc_loop_triangles(); tris[ob.name] = len(ob.data.loop_triangles)
print("GaugeFaces", tris["GaugeFaces"], "WarningLights", tris["WarningLights"], "Dashboard", tris["Dashboard"], "interior total", sum(tris.values()))
json.dump(dict(warning_lights=WL, tris=tris, removed_dash_faces=len(kill)), open(os.path.join(ROOT, "work", "cluster_v21.json"), "w"), indent=1)

bpy.ops.wm.save_as_mainfile(filepath=B, compress=True, relative_remap=True)
for o in bpy.data.objects: o.select_set(False)
for o in KIT.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Interior_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, colors_type='SRGB')
print("exported interior")
