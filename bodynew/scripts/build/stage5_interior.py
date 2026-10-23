"""Stage 5: interior detail and rig.

- _Axis empties back on their pivots (they were offset like the body empties)
- every moving piece on its exact pivot, local axes set so the motion is one local axis; rig custom properties
  (motion, local_axis, range, rest; needles add units_per_degree; gear lever range_across / range_fore_aft)
- seats split into cushion (Seat_XX, on rails, slides) + Seat_XX_Back (recline pivot) + Seat_XX_Headrest; belts and buckles
- GloveBox lid, SunVisor_L / _R, gear lever shaft, readable gauge faces (speedo 0-220, tacho 0-7000 red from 6500,
  temperature, fuel) with a separate emissive warning-light slot
- materials (interior plastic, seat fabric, carpet, headliner cloth, gauges), UV0, Grime, preview actions
Writes export/Octavia1U_Interior_v2.blend / .fbx and work/interior_rig.json.
"""
import bpy, bmesh, sys, os, math, json
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree
import numpy as np
from PIL import Image, ImageDraw, ImageFont

bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "src", "Octavia1U_Interior.blend"))
O = bpy.data.objects
KIT = bpy.data.collections["INTERIOR_KIT"]
TEX = os.path.join(ROOT, "export", "textures")
man = json.load(open(os.path.join(ROOT, "src", "body_manifest.json")))
PIV = {p["name"]: Vector(p["pivot_m"]) for p in man["interior"]}
log = dict(rig={}, pivots={}, new_parts=[], empties_moved=[])


def basis(z, xhint):
    z = Vector(z).normalized(); x = Vector(xhint); x = (x - z * x.dot(z)).normalized(); y = z.cross(x)
    return Matrix((x, y, z)).transposed()


def set_frame(ob, origin, R):
    """move the object's origin/axes without moving its geometry (children follow their world positions)."""
    kids = [(c, c.matrix_world.copy()) for c in ob.children]
    W_old = ob.matrix_world.copy()
    W_new = Matrix.Translation(origin) @ R.to_4x4()
    ob.data.transform(W_new.inverted() @ W_old)
    ob.matrix_world = W_new
    for c, mw in kids:
        c.matrix_parent_inverse = Matrix.Identity(4); c.matrix_world = mw


def rig(ob, motion, axis, rng, rest="first", **extra):
    ob["motion"] = motion; ob["local_axis"] = axis; ob["range"] = [float(rng[0]), float(rng[1])]; ob["rest"] = rest
    for k, v in extra.items(): ob[k] = v
    log["rig"][ob.name] = dict(motion=motion, local_axis=axis, range=[float(rng[0]), float(rng[1])], rest=rest, **extra)


def world_bm(ob):
    bm = bmesh.new(); bm.from_mesh(ob.data); bm.transform(ob.matrix_world); return bm


def new_obj(name, bmw, origin, R=None, parent=None, mats=()):
    R = R if R is not None else Matrix.Identity(3)
    W = Matrix.Translation(origin) @ R.to_4x4()
    me = bpy.data.meshes.new(name); bmw.transform(W.inverted()); bmw.to_mesh(me); bmw.free()
    for m in mats: me.materials.append(m)
    for p in me.polygons: p.use_smooth = True
    ob = bpy.data.objects.new(name, me); KIT.objects.link(ob); ob.matrix_world = W
    if parent: ob.parent = parent; ob.matrix_parent_inverse = parent.matrix_world.inverted()
    log["new_parts"].append(name); return ob


def empty(name, pos, parent, R=None, size=0.03, shape='SINGLE_ARROW'):
    e = O.get(name) or bpy.data.objects.new(name, None)
    if e.name not in KIT.objects: KIT.objects.link(e)
    e.empty_display_type = shape; e.empty_display_size = size
    e.parent = parent; e.matrix_parent_inverse = parent.matrix_world.inverted() if parent else Matrix.Identity(4)
    e.matrix_world = Matrix.Translation(pos) @ (R or Matrix.Identity(3)).to_4x4()
    return e


def mat(name):
    return bpy.data.materials.get("M_" + name) or bpy.data.materials.new("M_" + name)


# ------------------------------------------------------------------ 0. axis empties: were offset by the pivot
for e in [o for o in KIT.objects if o.type == 'EMPTY']:
    before = e.matrix_world.translation.copy(); real = e.location.copy()
    e.matrix_parent_inverse = e.parent.matrix_world.inverted() if e.parent else Matrix.Identity(4)
    e.matrix_world = Matrix.Translation(real)
    log["empties_moved"].append(dict(name=e.name, from_m=[round(x, 4) for x in before], to_m=[round(x, 4) for x in real]))

# ------------------------------------------------------------------ 1. pivots and axes of the moving parts
def bbox(ob):
    vs = [ob.matrix_world @ v.co for v in ob.data.vertices]
    return Vector([min(v[i] for v in vs) for i in range(3)]), Vector([max(v[i] for v in vs) for i in range(3)])

# needles: turn in the dial face; local Z points into the dash, so a positive turn is clockwise as the driver sees it.
NEEDLES = {"Needle_Speedo": dict(rng=(0, 240), upd=220 / 240, units="km/h", r=0.062, zero=-120),
           "Needle_Tacho": dict(rng=(0, 240), upd=7000 / 240, units="rpm", r=0.062, zero=-120),
           "Needle_Temp": dict(rng=(0, 90), upd=80 / 90, units="degC (50-130)", r=0.022, zero=-45),
           "Needle_Fuel": dict(rng=(0, 90), upd=1 / 90, units="tank fraction (0-1)", r=0.022, zero=-45)}
for nm, d in NEEDLES.items():
    ob = O[nm]; p = PIV[nm]
    lo, hi = bbox(ob)
    piv = Vector((p.x, p.y, lo.z + 0.0035))                 # hub centre at the needle's foot
    set_frame(ob, piv, basis(Vector((0, 1, 0)), Vector((1, 0, 0))))
    # needle points straight up in the prototype: turn it to its zero mark (clockwise = +Z here)
    ob.data.transform(Matrix.Rotation(math.radians(d["zero"]), 4, 'Z'))
    rig(ob, "rotate", "Z", d["rng"], "first", units_per_degree=d["upd"], units=d["units"], zero_mark_deg=d["zero"])
    log["pivots"][nm] = dict(given=[round(x, 4) for x in p], used=[round(x, 4) for x in piv], moved_mm=round((piv - p).length * 1000, 1))

sw = O["SteeringWheel"]; col = Vector((0.0045, 0.9204, -0.3909)).normalized()
set_frame(sw, PIV["SteeringWheel"], basis(col, Vector((1, 0, 0))))
rig(sw, "rotate", "Z", (-540, 540), "middle", note="local Z = column axis into the dash (0.0045, 0.9204, -0.3909); 1.5 turns lock to lock each way")
log["pivots"]["SteeringWheel"] = dict(given=[round(x, 4) for x in PIV["SteeringWheel"]], used=[round(x, 4) for x in PIV["SteeringWheel"]], moved_mm=0.0)

for nm, end in (("Stalk_Wiper", "min"), ("Stalk_Signal", "max")):
    ob = O[nm]; lo, hi = bbox(ob)
    x = lo.x + 0.008 if end == "min" else hi.x - 0.008
    vs = [ob.matrix_world @ v.co for v in ob.data.vertices if abs((ob.matrix_world @ v.co).x - x) < 0.015]
    c = sum(vs, Vector()) / len(vs)
    piv = Vector((x, c.y, c.z))
    # flicks up/down about an axis along the column's sideways direction: local Z = car X
    set_frame(ob, piv, basis(Vector((1, 0, 0)), Vector((0, 1, 0))))
    rig(ob, "rotate", "Z", (-12, 12) if nm == "Stalk_Signal" else (-10, 15), "middle",
        note="signal: down = left, up = right" if nm == "Stalk_Signal" else "wiper: up = flick/wash, down = intermittent, slow, fast")
    log["pivots"][nm] = dict(given=[round(x_, 4) for x_ in PIV[nm]], used=[round(x_, 4) for x_ in piv], moved_mm=round((piv - PIV[nm]).length * 1000, 1))

for nm in ("Pedal_Clutch", "Pedal_Brake", "Pedal_Gas"):
    ob = O[nm]; lo, hi = bbox(ob)
    piv = Vector(((lo.x + hi.x) / 2, hi.y - 0.006, hi.z - 0.006))     # top of the arm, under the dash
    set_frame(ob, piv, basis(Vector((1, 0, 0)), Vector((0, 1, 0))))
    rig(ob, "rotate", "Z", (0, 18 if nm != "Pedal_Gas" else 14), "first", note="pressed = pad moves forward (positive about local Z = car +X)")
    log["pivots"][nm] = dict(given=[round(x, 4) for x in PIV[nm]], used=[round(x, 4) for x in piv], moved_mm=round((piv - PIV[nm]).length * 1000, 1))

gl = O["GearLever"]; p = PIV["GearLever"]
set_frame(gl, p, basis(Vector((0, 0, 1)), Vector((1, 0, 0))))
# shaft from the ball joint up into the knob
gb = world_bm(gl)
bmesh.ops.create_cone(gb, cap_ends=True, segments=10, radius1=0.0065, radius2=0.0055, depth=0.085,
                      matrix=Matrix.Translation(p + Vector((0, 0, 0.0425 + 0.002))))
gb.transform(gl.matrix_world.inverted()); gb.to_mesh(gl.data); gb.free()
rig(gl, "rotate", "XY", (-10, 10), "middle", range_across=[-9.0, 9.0], range_fore_aft=[-11.0, 11.0],
    note="ball joint: across = about local Y (gate left/right), fore-aft = about local X; 5-speed H plus reverse left-forward")
log["pivots"]["GearLever"] = dict(given=[round(x, 4) for x in p], used=[round(x, 4) for x in p], moved_mm=0.0)

hb = O["Handbrake"]; lo, hi = bbox(hb)
piv = Vector(((lo.x + hi.x) / 2, lo.y + 0.01, lo.z + 0.012))
set_frame(hb, piv, basis(Vector((1, 0, 0)), Vector((0, 1, 0))))
rig(hb, "rotate", "Z", (0, 22), "first", note="pulled = grip up")
log["pivots"]["Handbrake"] = dict(given=[round(x, 4) for x in PIV["Handbrake"]], used=[round(x, 4) for x in piv], moved_mm=round((piv - PIV["Handbrake"]).length * 1000, 1))

mi = O["Mirror_Interior"]; lo, hi = bbox(mi)
piv = Vector(((lo.x + hi.x) / 2, hi.y - 0.01, hi.z - 0.006))          # ball on the screen mount
set_frame(mi, piv, basis(Vector((0, 0, 1)), Vector((1, 0, 0))))
rig(mi, "rotate", "Z", (-15, 15), "middle", note="ball mount; yaw about local Z")

# ------------------------------------------------------------------ 2. seats: cushion + back + headrest, rails, belts
FABRIC = mat("seat_fabric"); PLAST = mat("interior_plastic"); METAL = mat("metal_black")
for nm in ("Seat_FL", "Seat_FR", "Seat_Rear"):
    ob = O[nm]; bm = world_bm(ob); bm.normal_update()
    lo, hi = bbox(ob)
    if nm == "Seat_Rear":
        rig(ob, "fixed", "-", (0, 0), "first", note="rear bench (static)"); bm.free(); continue
    # recline pivot: rear of the cushion at its top
    ys = sorted(v.co.y for v in bm.verts)
    rec = Vector(((lo.x + hi.x) / 2, lo.y + 0.12, 0.505))
    lean = math.radians(18)                                  # back leans rearwards
    nback = Vector((0, math.cos(lean), math.sin(lean)))      # normal of the back's front plane (forward-up)
    def part_of(f):
        c = f.calc_center_median()
        if c.z > 1.03: return "head"
        d = c - rec
        if c.z > 0.62 or (d.dot(nback) < 0.045 and c.z > 0.50): return "back"
        return "cush"
    groups = {"head": [], "back": [], "cush": []}
    for f in bm.faces: groups[part_of(f)].append(f)
    def extract(fs):
        nb = bmesh.new(); vm = {}
        for f in fs:
            vs = []
            for v in f.verts:
                if v not in vm: vm[v] = nb.verts.new(v.co)
                vs.append(vm[v])
            try: nb.faces.new(vs)
            except ValueError: pass
        return nb
    back_bm = extract(groups["back"]); head_bm = extract(groups["head"]); cush_bm = extract(groups["cush"])
    # rails under the cushion
    for sx in (-0.17, 0.17):
        bmesh.ops.create_cube(cush_bm, size=1.0, matrix=Matrix.Translation(((lo.x + hi.x) / 2 + sx, (lo.y + hi.y) / 2, lo.z + 0.012)) @ Matrix.Diagonal((0.03, 0.52, 0.018, 1)))
    bm.free()
    W0 = ob.matrix_world.copy(); cush_bm.transform(W0.inverted()); cush_bm.to_mesh(ob.data); cush_bm.free()
    rig(ob, "slide", "Y", (-0.10, 0.12), "middle", note="on rails, metres along local Y (car forward)")
    back = new_obj(f"{nm}_Back", back_bm, rec, basis(Vector((1, 0, 0)), Vector((0, 1, 0))), parent=ob, mats=list(ob.data.materials))
    rig(back, "rotate", "Z", (-8, 30), "first", note="recline: positive leans the back rearwards (about local Z = car +X)")
    hp = Vector(((lo.x + hi.x) / 2, sum(v.co.y for v in head_bm.verts) / max(1, len(head_bm.verts)), 1.04))
    head = new_obj(f"{nm}_Headrest", head_bm, hp, parent=back, mats=list(ob.data.materials))
    rig(head, "slide", "Z", (0.0, 0.07), "first", note="headrest height")
# belts and buckles (front)
belt = bmesh.new()
def ribbon(pts, w=0.046):
    vs = []
    for i, p in enumerate(pts):
        t = (pts[min(i + 1, len(pts) - 1)] - pts[max(i - 1, 0)]).normalized()
        side = t.cross(Vector((1, 0, 0))).normalized() if abs(t.x) < 0.9 else Vector((0, 1, 0))
        side = Vector((1, 0, 0)).cross(t).normalized() if abs(t.x) < 0.9 else Vector((0, 1, 0))
        vs.append((belt.verts.new(p - side * w / 2), belt.verts.new(p + side * w / 2)))
    for i in range(len(vs) - 1):
        belt.faces.new((vs[i][0], vs[i + 1][0], vs[i + 1][1], vs[i][1]))
for sx in (-1, 1):
    top = Vector((sx * 0.655, -0.24, 1.12)); low = Vector((sx * 0.62, -0.26, 0.40)); buckle = Vector((sx * 0.13, -0.20, 0.53))
    ribbon([top, top.lerp(low, 0.5) + Vector((0, 0.03, 0)), low])
    bmesh.ops.create_cube(belt, size=1.0, matrix=Matrix.Translation(top) @ Matrix.Diagonal((0.02, 0.05, 0.07, 1)))
    bmesh.ops.create_cube(belt, size=1.0, matrix=Matrix.Translation(buckle) @ Matrix.Diagonal((0.035, 0.05, 0.07, 1)))
    ribbon([buckle + Vector((0, 0, -0.03)), buckle + Vector((0, 0.02, -0.12))], 0.03)
new_obj("SeatBelts", belt, Vector((0, -0.2, 0.7)), mats=(FABRIC,))

# ------------------------------------------------------------------ 3. glovebox lid, sun visors
dash = O["Dashboard"]; dbm = world_bm(dash); dtree = BVHTree.FromBMesh(dbm); dbm.free()
eye = Vector((0.39, -0.05, 1.05))
grid = {}; NX, NZ = 7, 4; W_, H_ = 0.36, 0.16; cen = Vector((0.40, 0.60, 0.64))
lb = bmesh.new()
for i in range(NX + 1):
    for j in range(NZ + 1):
        q = cen + Vector(((i / NX - 0.5) * W_, 0, (j / NZ - 0.5) * H_))
        h = dtree.ray_cast(q + Vector((0, -0.25, 0)), Vector((0, 1, 0)), 0.6)
        p = (h[0] if h[0] is not None else q) + Vector((0, -0.003, 0))
        grid[(i, j)] = lb.verts.new(p)
for i in range(NX):
    for j in range(NZ):
        lb.faces.new((grid[(i, j)], grid[(i + 1, j)], grid[(i + 1, j + 1)], grid[(i, j + 1)]))
ext = bmesh.ops.extrude_face_region(lb, geom=lb.faces[:])
bmesh.ops.translate(lb, vec=Vector((0, -0.004, 0)), verts=[v for v in ext["geom"] if isinstance(v, bmesh.types.BMVert)])
bmesh.ops.recalc_face_normals(lb, faces=lb.faces)
ha, hbp = grid[(0, 0)].co.copy(), grid[(NX, 0)].co.copy(); latch = grid[(NX // 2, NZ)].co.copy()
gbox = new_obj("GloveBox", lb, (ha + hbp) / 2, basis(hbp - ha, Vector((0, -1, 0))), mats=(PLAST,))
empty("GloveBox_HingeA", ha, gbox); empty("GloveBox_HingeB", hbp, gbox)
empty("GloveBox_Latch", latch, gbox)
rig(gbox, "rotate", "Z", (0, 80), "first", note="hinge along the lower edge; opens down towards the passenger")
hl = O["Headliner"]; hbm = world_bm(hl); htree = BVHTree.FromBMesh(hbm); hbm.free()
CLOTH = mat("headliner_cloth")
for nm, sx in (("SunVisor_L", -1), ("SunVisor_R", 1)):
    x0 = sx * 0.33
    h = htree.ray_cast(Vector((x0, 0.30, 1.0)), Vector((0, 0, 1)), 0.6)
    ztop = h[0].z if h[0] is not None else 1.30
    hinge = Vector((x0, 0.33, ztop - 0.012))
    vb = bmesh.new()
    bmesh.ops.create_cube(vb, size=1.0, matrix=Matrix.Translation(hinge + Vector((0, -0.085, -0.004))) @ Matrix.Diagonal((0.36, 0.165, 0.022, 1)))
    bmesh.ops.bevel(vb, geom=list(vb.edges), offset=0.008, segments=2, affect='EDGES')
    bmesh.ops.create_cone(vb, cap_ends=True, segments=8, radius1=0.006, radius2=0.006, depth=0.40,
                          matrix=Matrix.Translation(hinge) @ Matrix.Rotation(math.radians(90), 4, 'Y'))
    v = new_obj(nm, vb, hinge, basis(Vector((1, 0, 0)), Vector((0, 1, 0))), mats=(CLOTH,))
    empty(f"{nm}_HingeA", hinge - Vector((0.18, 0, 0)), v); empty(f"{nm}_HingeB", hinge + Vector((0.18, 0, 0)), v)
    rig(v, "rotate", "Z", (0, 95), "first", note="flips down about the rod along its front edge")

# ------------------------------------------------------------------ 4. gauge faces (texture atlas) and warning lights
A = 2048
img = Image.new("RGB", (A, A), (8, 9, 11)); dr = ImageDraw.Draw(img)
emi = Image.new("RGB", (A, A), (0, 0, 0)); de = ImageDraw.Draw(emi)
F_ = lambda s: ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", s)
def dial(cx, cy, R, v0, v1, major, minor, labels, zero=-120, sweep=240, red_from=None, title=""):
    dr.ellipse((cx - R, cy - R, cx + R, cy + R), fill=(14, 15, 18), outline=(60, 62, 66), width=6)
    def ang(val): return math.radians(zero + sweep * (val - v0) / (v1 - v0))
    if red_from is not None:
        for k in range(200):
            val = red_from + (v1 - red_from) * k / 199; a = ang(val)
            for rr in range(int(R * 0.80), int(R * 0.90)):
                x = cx + rr * math.sin(a); y = cy - rr * math.cos(a); dr.point((x, y), fill=(200, 30, 25))
    val = v0
    while val <= v1 + 1e-6:
        a = ang(val); big = abs((val - v0) / major - round((val - v0) / major)) < 1e-6
        r0 = R * (0.78 if big else 0.85)
        for img_, d_ in ((dr, (235, 235, 230)), (de, (120, 160, 255))):
            img_.line((cx + r0 * math.sin(a), cy - r0 * math.cos(a), cx + R * 0.93 * math.sin(a), cy - R * 0.93 * math.cos(a)),
                      fill=d_, width=10 if big else 4)
        val += minor
    for val, txt in labels:
        a = ang(val); rr = R * 0.62
        f = F_(int(R * 0.15))
        w = dr.textlength(txt, font=f)
        for img_, d_ in ((dr, (235, 235, 230)), (de, (120, 160, 255))):
            img_.text((cx + rr * math.sin(a) - w / 2, cy - rr * math.cos(a) - R * 0.09), txt, fill=d_, font=f)
    if title:
        f = F_(int(R * 0.11)); w = dr.textlength(title, font=f)
        dr.text((cx - w / 2, cy + R * 0.35), title, fill=(200, 200, 195), font=f)
dial(512, 512, 480, 0, 220, 20, 10, [(v, str(v)) for v in range(0, 221, 20)], title="km/h")
dial(1536, 512, 480, 0, 7, 1, 0.5, [(v, str(v)) for v in range(0, 8)], red_from=6.5, title="x1000/min")
dial(256, 1280, 230, 50, 130, 40, 10, [(50, "50"), (90, "90"), (130, "130")], zero=-45, sweep=90, red_from=115, title="°C")
dial(768, 1280, 230, 0, 1, 0.5, 0.25, [(0, "0"), (0.5, "1/2"), (1, "1")], zero=-45, sweep=90, title="FUEL")
# warning-light strip (own emissive slot): oil, battery, brake, ABS, indicators, high beam, glow, belt
WL = [("OIL", (220, 40, 30)), ("BAT", (220, 40, 30)), ("BRAKE", (220, 40, 30)), ("ABS", (230, 160, 20)), ("<", (40, 200, 60)),
      (">", (40, 200, 60)), ("HI", (40, 90, 230)), ("BELT", (220, 40, 30))]
for k, (t, c) in enumerate(WL):
    x0 = 1100 + k * 115; y0 = 1150
    dr.rectangle((x0, y0, x0 + 105, y0 + 105), fill=(16, 16, 18), outline=(40, 40, 44), width=3)
    f = F_(30); w = dr.textlength(t, font=f)
    dr.text((x0 + 52 - w / 2, y0 + 36), t, fill=(70, 70, 72), font=f)
    de.rectangle((x0 + 4, y0 + 4, x0 + 101, y0 + 101), fill=tuple(int(x * 0.25) for x in c))
    de.text((x0 + 52 - w / 2, y0 + 36), t, fill=c, font=f)
img.save(os.path.join(TEX, "gauge_faces_basecolor.png")); emi.save(os.path.join(TEX, "gauge_faces_emissive.png"))
Image.new("L", (64, 64), 150).save(os.path.join(TEX, "gauge_faces_roughness.png"))
Image.new("L", (64, 64), 0).save(os.path.join(TEX, "gauge_faces_metallic.png"))
Image.new("RGB", (64, 64), (128, 128, 255)).save(os.path.join(TEX, "gauge_faces_normal.png"))
for k in ("basecolor", "emissive", "roughness", "metallic", "normal"):
    src = os.path.join(TEX, f"gauge_faces_{k}.png")
    Image.open(src).save(os.path.join(TEX, f"gauge_warning_{k}.png"))
GF = mat("gauge_faces"); GW = mat("gauge_warning")
gb = bmesh.new(); uvl = gb.loops.layers.uv.new("UVMap")
def disc(centre, R, atlas_c, atlas_r, segs=40):
    c = gb.verts.new(centre)
    ring = [gb.verts.new(centre + Vector((R * math.sin(2 * math.pi * k / segs), 0, R * math.cos(2 * math.pi * k / segs)))) for k in range(segs)]
    for k in range(segs):
        f = gb.faces.new((c, ring[(k + 1) % segs], ring[k]))
        for l in f.loops:
            d = l.vert.co - centre
            l[uvl].uv = ((atlas_c[0] + d.x / R * atlas_r) / A, 1 - (atlas_c[1] - d.z / R * atlas_r) / A)
for nm, ac, ar in (("Needle_Speedo", (512, 512), 480), ("Needle_Tacho", (1536, 512), 480), ("Needle_Temp", (256, 1280), 230), ("Needle_Fuel", (768, 1280), 230)):
    p = O[nm].matrix_world.translation
    # sit just in front of the prototype's dial face (found by a ray from the driver's side), behind the needle
    R_ = NEEDLES[nm]["r"]; ys = []
    for dx, dz in ((0.6, 0.0), (-0.6, 0.0), (0.0, 0.6), (0.0, -0.6), (0.4, 0.4)):
        h = dtree.ray_cast(Vector((p.x + dx * R_, p.y - 0.15, p.z + dz * R_)), Vector((0, 1, 0)), 0.4)
        if h[0] is not None: ys.append(h[0].y)
    yd = (sorted(ys)[len(ys) // 2] - 0.0015) if ys else p.y + 0.006
    disc(Vector((p.x, yd, p.z)), R_, ac, ar)
    log["pivots"].setdefault("dial_faces", {})[nm] = round(yd, 4)
gauge = new_obj("GaugeFaces", gb, Vector((-0.38, 0.73, 0.90)), parent=dash, mats=(GF,))
wb = bmesh.new(); uv2 = wb.loops.layers.uv.new("UVMap")
for k in range(8):
    x = -0.4335 + (k - 3.5) * 0.009
    q = [wb.verts.new(Vector((x - 0.004, 0.7215, 0.852))), wb.verts.new(Vector((x + 0.004, 0.7215, 0.852))),
         wb.verts.new(Vector((x + 0.004, 0.7215, 0.860))), wb.verts.new(Vector((x - 0.004, 0.7215, 0.860)))]
    f = wb.faces.new(q)
    x0 = 1100 + k * 115
    for l, (u, v) in zip(f.loops, ((x0, 1255), (x0 + 105, 1255), (x0 + 105, 1150), (x0, 1150))): l[uv2].uv = (u / A, 1 - v / A)
new_obj("WarningLights", wb, Vector((-0.4335, 0.7215, 0.856)), parent=dash, mats=(GW,))

# ------------------------------------------------------------------ 5. budget: steering wheel to 8k
sw_tris = sum(len(p.vertices) - 2 for p in sw.data.polygons)
if sw_tris > 7900:
    md = sw.modifiers.new("d", 'DECIMATE'); md.ratio = 7800 / sw_tris
    bpy.context.view_layer.objects.active = sw
    dg = bpy.context.evaluated_depsgraph_get(); em = sw.evaluated_get(dg).to_mesh(); nm_ = bpy.data.meshes.new("SteeringWheel")
    bmx = bmesh.new(); bmx.from_mesh(em); bmx.to_mesh(nm_); bmx.free(); sw.evaluated_get(dg).to_mesh_clear()
    for m in sw.data.materials: nm_.materials.append(m)
    old = sw.data; sw.modifiers.clear(); sw.data = nm_; bpy.data.meshes.remove(old)

json.dump(log, open(os.path.join(ROOT, "work", "interior_rig.json"), "w"), indent=1)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, "work", "stage5.blend"), compress=True)
print("saved interior stage 5; new", log["new_parts"])
