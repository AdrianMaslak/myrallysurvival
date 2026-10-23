"""Stage 3: new body parts, lamp slots, rig, collision hulls.

- hinge / latch / fixing empties back onto their real positions (they were offset by the part pivot)
- DoorGlass_XX panes split out of the doors (slide down, `_Down` empty), rubber seals round all glass
- Wipers split into Wiper_L / Wiper_R, origins on their spindles, rig: turn about the screen normal
- FuelFlap (hinge empties, rig), number plates Plate_F / Plate_R, plate lamps, tow eyes, JackPoint empties
- interior door handles and window winders as their own objects on the door cards (rig)
- lamp material slots: lamp_lowbeam, lamp_highbeam, lamp_indicator, lamp_fog, lamp_tail, lamp_brake, lamp_reverse, lamp_plate
- UCX_ convex hulls for every part that opens or comes off
Writes work/stage3.blend and work/stage3_parts.json (moved empties, rig data).
"""
import bpy, bmesh, sys, os, math, json
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "work", "stage2.blend"))
O = bpy.data.objects
KIT = bpy.data.collections["BODY_KIT"]
log = dict(empties_moved=[], rig={}, new_parts=[])


def mat(name, color=(0.5, 0.5, 0.5, 1)):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color = color
    return m


def slot(ob, name, color=(0.5, 0.5, 0.5, 1)):
    m = mat(name, color)
    for i, s in enumerate(ob.data.materials):
        if s and s.name == name: return i
    ob.data.materials.append(m); return len(ob.data.materials) - 1


def world_bm(ob):
    bm = bmesh.new(); bm.from_mesh(ob.data); bm.transform(ob.matrix_world); bm.normal_update(); return bm


def put_bm(ob, bm):
    bm.transform(ob.matrix_world.inverted()); bm.normal_update(); bm.to_mesh(ob.data); bm.free()
    for p in ob.data.polygons: p.use_smooth = True


def new_obj(name, bm_world, origin, parent=None, rot=None, mats=()):
    """object with its origin at `origin` (world) and local axes from `rot` (3x3, columns = local X, Y, Z)."""
    R = (rot if rot is not None else Matrix.Identity(3)).to_4x4()
    W = Matrix.Translation(origin) @ R
    me = bpy.data.meshes.new(name)
    bm_world.transform(W.inverted()); bm_world.to_mesh(me); bm_world.free()
    for m in mats: me.materials.append(m)
    for p in me.polygons: p.use_smooth = True
    ob = bpy.data.objects.new(name, me); KIT.objects.link(ob)
    ob.matrix_world = W
    if parent is not None:
        ob.parent = parent; ob.matrix_parent_inverse = parent.matrix_world.inverted()
    log["new_parts"].append(name)
    return ob


def empty(name, pos, parent, axis_rot=None, size=0.03, shape='PLAIN_AXES'):
    e = O.get(name) or bpy.data.objects.new(name, None)
    if e.name not in KIT.objects: KIT.objects.link(e)
    e.empty_display_type = shape; e.empty_display_size = size
    W = Matrix.Translation(pos) @ ((axis_rot or Matrix.Identity(3)).to_4x4())
    e.parent = parent; e.matrix_parent_inverse = Matrix.Identity(4) if parent is None else parent.matrix_world.inverted()
    e.matrix_world = W
    return e


def rig(ob, motion, axis, rng, rest="first", **extra):
    ob["motion"] = motion; ob["local_axis"] = axis; ob["range"] = [float(rng[0]), float(rng[1])]; ob["rest"] = rest
    for k, v in extra.items(): ob[k] = v
    log["rig"][ob.name] = dict(motion=motion, local_axis=axis, range=[float(rng[0]), float(rng[1])], rest=rest, **extra)


def basis(z, xhint):
    z = Vector(z).normalized(); x = Vector(xhint); x = (x - z * x.dot(z)).normalized(); y = z.cross(x)
    return Matrix((x, y, z)).transposed()


# ------------------------------------------------------------------ 1. empties at their real positions
for e in [o for o in KIT.objects if o.type == 'EMPTY']:
    real = e.location.copy()                       # the prototype stored the world position as the local offset
    before = e.matrix_world.translation.copy()
    p = e.parent
    e.matrix_parent_inverse = p.matrix_world.inverted() if p else Matrix.Identity(4)
    e.matrix_world = Matrix.Translation(real)
    log["empties_moved"].append(dict(name=e.name, from_m=[round(x, 4) for x in before], to_m=[round(x, 4) for x in real],
                                     moved_m=round((before - real).length, 4)))

# ------------------------------------------------------------------ 2. door glass panes, seals
RUBBER = mat("KIT_rubber_seal", (0.02, 0.02, 0.02, 1))
GLASS = bpy.data.materials.get("KIT_octavia1u_glass")


def seal_along(bm, loop_pts, r=0.0045, segs=6, mi=0):
    n = len(loop_pts)
    if n < 3: return
    rings = []
    for i in range(n):
        p = loop_pts[i]; t = (loop_pts[(i + 1) % n] - loop_pts[i - 1]).normalized()
        a = t.orthogonal().normalized(); b = t.cross(a)
        rings.append([bm.verts.new(p + (a * math.cos(2 * math.pi * k / segs) + b * math.sin(2 * math.pi * k / segs)) * r) for k in range(segs)])
    for i in range(n):
        A, B = rings[i], rings[(i + 1) % n]
        for k in range(segs):
            try: f = bm.faces.new((A[k], A[(k + 1) % segs], B[(k + 1) % segs], B[k])); f.material_index = mi
            except ValueError: pass


def boundary_loops(bm, faces):
    fs = set(faces); edges = []
    for f in faces:
        for e in f.edges:
            if sum(1 for g in e.link_faces if g in fs) == 1: edges.append(e)
    adj = {}
    for e in edges:
        a, b = e.verts; adj.setdefault(a, []).append(b); adj.setdefault(b, []).append(a)
    loops, seen = [], set()
    for s in adj:
        if s in seen: continue
        loop = [s]; seen.add(s); prev, cur = None, s
        while True:
            nx = [x for x in adj[cur] if x is not prev and x not in seen]
            if not nx: break
            prev, cur = cur, nx[0]; loop.append(cur); seen.add(cur)
        if len(loop) > 3: loops.append([v.co.copy() for v in loop])
    return loops


for side in ("FL", "FR", "RL", "RR"):
    door = O[f"Door_{side}"]
    gi = [i for i, m in enumerate(door.data.materials) if m and m.name == "KIT_octavia1u_glass"][0]
    bm = world_bm(door)
    gf = [f for f in bm.faces if f.material_index == gi]
    loops = boundary_loops(bm, gf)
    pane = bmesh.new()
    vm = {}
    for f in gf:
        vs = []
        for v in f.verts:
            if v not in vm: vm[v] = pane.verts.new(v.co)
            vs.append(vm[v])
        try: pane.faces.new(vs)
        except ValueError: pass
    # 4 mm thick pane
    ext = bmesh.ops.extrude_face_region(pane, geom=pane.faces[:])
    nrm = sum((f.normal for f in gf), Vector()).normalized()
    sgn = 1 if nrm.x * (1 if side[1] == "L" else -1) < 0 else -1
    bmesh.ops.translate(pane, vec=nrm * 0.004 * sgn, verts=[v for v in ext["geom"] if isinstance(v, bmesh.types.BMVert)])
    bmesh.ops.recalc_face_normals(pane, faces=pane.faces)
    zs = [v.co.z for v in pane.verts]; zmin, zmax = min(zs), max(zs)
    ys = [v.co.y for v in pane.verts]
    bottom = Vector((sum(v.co.x for v in pane.verts) / len(pane.verts), (min(ys) + max(ys)) / 2, zmin))
    top_c = Vector((bottom.x, bottom.y, zmax))
    travel = (zmax - zmin) - 0.025
    bmesh.ops.delete(bm, geom=gf, context='FACES')
    # seal round the opening, on the door frame
    for lp in loops:
        seal_along(bm, lp[::2] if len(lp) > 60 else lp, mi=slot(door, "KIT_rubber_seal", (0.02, 0.02, 0.02, 1)))
    put_bm(door, bm)
    gp = new_obj(f"DoorGlass_{side}", pane, bottom, parent=door, mats=(GLASS,))
    rig(gp, "slide", "Z", (0.0, -round(travel, 3)), "first")
    empty(f"DoorGlass_{side}_Down", top_c - Vector((0, 0, travel)), gp, size=0.04, shape='SINGLE_ARROW')

for nm in ("Windshield", "Trunk"):
    ob = O[nm]; bm = world_bm(ob)
    gi = [i for i, m in enumerate(ob.data.materials) if m and m.name == "KIT_octavia1u_glass"]
    if gi:
        loops = boundary_loops(bm, [f for f in bm.faces if f.material_index == gi[0]])
        mi = slot(ob, "KIT_rubber_seal", (0.02, 0.02, 0.02, 1))
        for lp in loops:
            if len(lp) > 8: seal_along(bm, lp, r=0.005, mi=mi)
    put_bm(ob, bm)

# ------------------------------------------------------------------ 3. wipers
wp = O["Wipers"]; bm = world_bm(wp)
bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=0.0003)
comps, seen = [], set()
for f in bm.faces:
    if f in seen: continue
    st, cur = [f], []; seen.add(f)
    while st:
        g = st.pop(); cur.append(g)
        for e in g.edges:
            for h in e.link_faces:
                if h not in seen: seen.add(h); st.append(h)
    comps.append(cur)
screen_n = Vector((0.0, 0.4991, 0.8666))
for nm, pick in (("Wiper_L", lambda c: min(v.co.x for f in c for v in f.verts) < -0.05),
                 ("Wiper_R", lambda c: min(v.co.x for f in c for v in f.verts) >= -0.05)):
    sel = [c for c in comps if pick(c)]
    nb = bmesh.new(); vm = {}
    for c in sel:
        for f in c:
            vs = []
            for v in f.verts:
                if v not in vm: vm[v] = nb.verts.new(v.co)
                vs.append(vm[v])
            try: nb.faces.new(vs)
            except ValueError: pass
    pts = [v.co for v in nb.verts]
    spindle = min(pts, key=lambda p: p.x)
    spindle = Vector((spindle.x + 0.012, sum(p.y for p in pts if p.x < spindle.x + 0.03) / max(1, sum(1 for p in pts if p.x < spindle.x + 0.03)),
                      min(p.z for p in pts if p.x < spindle.x + 0.03)))
    R = basis(-screen_n, (1, 0, 0))                # local Z = into the screen: positive turn sweeps up the glass
    ob = new_obj(nm, nb, spindle, rot=R, mats=list(wp.data.materials))
    rig(ob, "rotate", "Z", (0.0, 92.0 if nm == "Wiper_L" else 96.0), "first", rest_deg=0.0, full_sweep_deg=92.0 if nm == "Wiper_L" else 96.0)
bm.free()
bpy.data.objects.remove(wp)

# ------------------------------------------------------------------ 4. fuel flap, plates, plate lamps, tow eyes, jack points
ch = O["Chassis"]
cbm = world_bm(ch); ctree = BVHTree.FromBMesh(cbm)
liv = cbm.loops.layers.uv.get("Livery")
PAINT = bpy.data.materials["KIT_octavia1u"]
def surf(p0, d):
    h = ctree.ray_cast(p0, d, 3.0)
    return h
# fuel flap: right rear quarter, behind the wheel arch, at the shoulder
c0 = surf(Vector((1.5, -1.66, 0.86)), Vector((-1, 0, 0)))
fb = bmesh.new(); W_, Hh = 0.115, 0.115
n = c0[1].normalized(); up = Vector((0, 0, 1)); t1 = (up - n * up.dot(n)).normalized(); t0 = t1.cross(n)
grid = {}
N = 8
for i in range(N + 1):
    for j in range(N + 1):
        u, v = (i / N - 0.5) * W_, (j / N - 0.5) * Hh
        # rounded square: pull the corners in
        rr = 0.018
        cu = max(abs(u) - (W_ / 2 - rr), 0); cv = max(abs(v) - (Hh / 2 - rr), 0)
        if cu > 0 and cv > 0:
            k = rr / math.hypot(cu, cv); u = math.copysign(W_ / 2 - rr + cu * k, u); v = math.copysign(Hh / 2 - rr + cv * k, v)
        p = c0[0] + t0 * u + t1 * v
        h = surf(p + n * 0.05, -n)
        q = (h[0] if h[0] is not None else p) + n * 0.0012
        grid[(i, j)] = fb.verts.new(q)
uvl = fb.loops.layers.uv.new("UVMap"); uvl2 = fb.loops.layers.uv.new("Livery")
for i in range(N):
    for j in range(N):
        f = fb.faces.new((grid[(i, j)], grid[(i + 1, j)], grid[(i + 1, j + 1)], grid[(i, j + 1)]))
ext = bmesh.ops.extrude_face_region(fb, geom=fb.faces[:])
bmesh.ops.translate(fb, vec=-n * 0.0012, verts=[v for v in ext["geom"] if isinstance(v, bmesh.types.BMVert)])
bmesh.ops.recalc_face_normals(fb, faces=fb.faces)
hinge_a = c0[0] + t0 * (W_ / 2) * (1 if t0.y > 0 else -1) - t1 * (Hh / 2 - 0.02)
hinge_b = hinge_a + t1 * (Hh - 0.04)
mid = (hinge_a + hinge_b) / 2
flap = new_obj("FuelFlap", fb, mid, parent=ch, rot=basis((hinge_b - hinge_a), n), mats=(PAINT,))
empty("FuelFlap_HingeA", hinge_a, flap); empty("FuelFlap_HingeB", hinge_b, flap)
empty("FuelFlap_Latch", c0[0] - t0 * (W_ / 2) * (1 if t0.y > 0 else -1), flap)
rig(flap, "rotate", "Z", (0.0, 85.0), "first")
flap["open_axis"] = [round(x, 4) for x in (hinge_b - hinge_a).normalized()]; flap["max_open_deg"] = 85.0; flap["mass_kg"] = 0.2
log["fuel_flap"] = dict(centre_m=[round(x, 4) for x in c0[0]], normal=[round(x, 3) for x in n])

PLATE = mat("KIT_plate", (0.9, 0.9, 0.85, 1))
def plate(name, centre, normal, parent):
    pb = bmesh.new()
    bmesh.ops.create_cube(pb, size=1.0)
    for v in pb.verts: v.co = Vector((v.co.x * 0.520, v.co.y * 0.110, v.co.z * 0.0015))
    R = basis(normal, (1, 0, 0))
    c = centre + normal * 0.004
    pb.transform(Matrix.Translation(c) @ R.to_4x4())
    ob = new_obj(name, pb, c, parent=parent, rot=R, mats=(PLATE,))
    ob["description"] = "Number plate (generic, own object)."
    return ob
plate("Plate_F", Vector((0.0, 2.2395, 0.425)), Vector((0, 1, 0)), O["FrontBumper"])
plate("Plate_R", Vector((0.0, -2.2895, 0.800)), Vector((0, -1, 0.06)).normalized(), O["Trunk"])
# plate lamps (in the tailgate, above the rear plate)
tr = O["Trunk"]; tbm = world_bm(tr); mi = slot(tr, "lamp_plate", (1, 1, 0.9, 1))
for sx in (-1, 1):
    r = bmesh.ops.create_cube(tbm, size=1.0, matrix=Matrix.Translation((sx * 0.13, -2.282, 0.866)) @ Matrix.Diagonal((0.045, 0.014, 0.006, 1)))
    for f in {f for v in r["verts"] for f in v.link_faces}: f.material_index = mi
put_bm(tr, tbm)
# tow eyes (chassis): front behind the bumper's lower right opening, rear under the bumper
for name, c, axis in (("front", Vector((0.34, 2.15, 0.31)), Vector((0, 1, 0))), ("rear", Vector((0.42, -2.30, 0.34)), Vector((0, -1, 0)))):
    R = basis(Vector((1, 0, 0)), axis).to_4x4()
    segs, tub = 16, 8
    rings = []
    for i in range(segs):
        a = 2 * math.pi * i / segs
        cc = Vector((0, math.cos(a) * 0.024, math.sin(a) * 0.024))
        tng = Vector((0, -math.sin(a), math.cos(a)))
        ring = []
        for k in range(tub):
            b = 2 * math.pi * k / tub
            off = Vector((math.cos(b) * 0.006, 0, 0)) + cc.normalized() * math.sin(b) * 0.006
            ring.append(cbm.verts.new(c + (R.to_3x3() @ (cc + off))))
        rings.append(ring)
    mi = [i for i, m in enumerate(ch.data.materials) if m.name == "KIT_octavia1u_undertray"][0]
    for i in range(segs):
        A, B = rings[i], rings[(i + 1) % segs]
        for k in range(tub):
            f = cbm.faces.new((A[k], A[(k + 1) % tub], B[(k + 1) % tub], B[k])); f.material_index = mi
put_bm(ch, cbm)
cbm = world_bm(ch); ctree = BVHTree.FromBMesh(cbm); cbm.free()
for name, x, y in (("JackPoint_FL", -0.70, 0.80), ("JackPoint_FR", 0.70, 0.80), ("JackPoint_RL", -0.69, -0.82), ("JackPoint_RR", 0.69, -0.82)):
    h = ctree.ray_cast(Vector((x, y, -0.5)), Vector((0, 0, 1)), 2.0)
    p = h[0] if h[0] is not None else Vector((x, y, 0.20))
    e = empty(name, p, ch, size=0.05, shape='SINGLE_ARROW')
    log.setdefault("jack_points", {})[name] = [round(v, 4) for v in p]

# ------------------------------------------------------------------ 5. interior door handles and window winders
CARD = bpy.data.materials.get("KIT_octavia1u_plastic")
for side, (yh, yw) in (("FL", (0.56, 0.24)), ("FR", (0.56, 0.24)), ("RL", (-0.46, -0.78)), ("RR", (-0.46, -0.78))):
    door = O[f"Door_{side}"]; dbm = world_bm(door); dt = BVHTree.FromBMesh(dbm); dbm.free()
    sx = -1 if side[1] == "L" else 1
    for kind, y, z in (("DoorHandle", yh, 0.93), ("WindowWinder", yw, 0.70)):
        h = dt.ray_cast(Vector((0.0, y, z)), Vector((sx, 0, 0)), 1.2)
        if h[0] is None: continue
        p, nn = h[0], h[1]
        if nn.x * sx > 0: nn = -nn                 # face into the cabin
        b = bmesh.new()
        if kind == "DoorHandle":
            # lever: pivot at its front end, pull towards the cabin
            bmesh.ops.create_cube(b, size=1.0, matrix=Matrix.Translation((0, -0.055 * (1 if side[0] == "F" else -1), 0.012)) @ Matrix.Diagonal((0.012, 0.11, 0.022, 1)))
            bmesh.ops.create_cube(b, size=1.0, matrix=Matrix.Translation((0, 0, 0.004)) @ Matrix.Diagonal((0.02, 0.03, 0.008, 1)))
            pivot = p + nn * 0.004 + Vector((0, 0.06 * (1 if side[0] == "F" else -1), 0))
            R = basis(Vector((0, 0, 1)), Vector((0, 1, 0)))
            b.transform(R.to_4x4()); b.transform(Matrix.Translation(pivot))
            ob = new_obj(f"DoorHandle_{side}", b, pivot, parent=door, rot=R, mats=(CARD,))
            rig(ob, "rotate", "Z", (0.0, 30.0 * (-sx)), "first")
        else:
            # crank: hub on the card, arm, knob; turns about the card normal
            R = basis(nn, Vector((0, 1, 0)))
            bmesh.ops.create_cone(b, cap_ends=True, segments=16, radius1=0.022, radius2=0.018, depth=0.012, matrix=Matrix.Translation((0, 0, 0.006)))
            bmesh.ops.create_cube(b, size=1.0, matrix=Matrix.Translation((0.045, 0, 0.016)) @ Matrix.Diagonal((0.09, 0.016, 0.008, 1)))
            bmesh.ops.create_cone(b, cap_ends=True, segments=12, radius1=0.010, radius2=0.010, depth=0.030, matrix=Matrix.Translation((0.088, 0, 0.034)))
            b.transform(Matrix.Translation(p) @ R.to_4x4())
            ob = new_obj(f"WindowWinder_{side}", b, p, parent=door, rot=R, mats=(CARD,))
            rig(ob, "rotate", "Z", (0.0, 1800.0), "first", note="five turns wind the pane fully down (DoorGlass slide 0 -> its range)")

# ------------------------------------------------------------------ 6. lamp slots
LAMPS = {k: mat(k, c) for k, c in (("lamp_lowbeam", (1, 1, 0.95, 1)), ("lamp_highbeam", (1, 1, 1, 1)), ("lamp_indicator", (1, 0.55, 0.05, 1)),
                                    ("lamp_fog", (1, 1, 0.9, 1)), ("lamp_tail", (0.8, 0.0, 0.0, 1)), ("lamp_brake", (1, 0.05, 0.05, 1)),
                                    ("lamp_reverse", (1, 1, 1, 1)), ("lamp_plate", (1, 1, 0.9, 1)))}
def reassign(ob, rules):
    me = ob.data
    names = [m.name if m else "" for m in me.materials]
    bm = world_bm(ob)
    for f in bm.faces:
        src = names[f.material_index]
        for test, target in rules:
            t = test(src, f)
            if t: f.material_index = slot(ob, target); break
    put_bm(ob, bm)
for nm in ("Headlight_L", "Headlight_R"):
    ob = O[nm]; xs = [ (ob.matrix_world @ v.co).x for v in ob.data.vertices]
    cx = (min(xs) + max(xs)) / 2; inb = 1 if nm.endswith("L") else -1
    # the twin reflector: inboard half high beam, outboard half low beam
    lb = [(ob.matrix_world @ v.co).x for p in ob.data.polygons if ob.data.materials[p.material_index].name.startswith("KIT_octavia1u_lowbeam") for v in [ob.data.vertices[p.vertices[0]]]]
    rc = (min(lb) + max(lb)) / 2 if lb else cx
    reassign(ob, [(lambda s, f: s.startswith("KIT_octavia1u_lowbeam") and (f.calc_center_median().x - rc) * inb > 0, "lamp_highbeam"),
                  (lambda s, f: s.startswith("KIT_octavia1u_lowbeam"), "lamp_lowbeam"),
                  (lambda s, f: s.startswith("KIT_octavia1u_signal"), "lamp_indicator"),
                  (lambda s, f: s == "KIT_octavia1u_fog", "lamp_fog")])
for nm in ("Fender_FL", "Fender_FR"):
    reassign(O[nm], [(lambda s, f: s.startswith("KIT_octavia1u_signal"), "lamp_indicator")])
for nm in ("Taillight_L", "Taillight_R"):
    ob = O[nm]
    lit = [p for p in ob.data.polygons if ob.data.materials[p.material_index].name in ("KIT_octavia1u_FL_signal_L", "KIT_octavia1u_FL_brake")]
    zs = [(ob.matrix_world @ ob.data.vertices[i].co).z for p in lit for i in p.vertices]
    xs = [abs((ob.matrix_world @ ob.data.vertices[i].co).x) for p in lit for i in p.vertices]
    z0, z1 = min(zs), max(zs); xm = (min(xs) + max(xs)) / 2
    def band(f):
        c = f.calc_center_median(); t = (c.z - z0) / max(1e-6, z1 - z0); return t, abs(c.x)
    LIT = ("KIT_octavia1u_FL_signal_L", "KIT_octavia1u_FL_brake")
    # cut the lit area along the band lines first, so the lamp sections have straight edges
    names_ = [m.name for m in ob.data.materials]
    bm_ = world_bm(ob)
    for zc in (z0 + 0.30 * (z1 - z0), z0 + 0.66 * (z1 - z0)):
        geom = [f for f in bm_.faces if names_[f.material_index] in LIT]
        geom = list({e for f in geom for e in f.edges}) + geom + list({v for f in geom for v in f.verts})
        bmesh.ops.bisect_plane(bm_, geom=geom, plane_co=(0, 0, zc), plane_no=(0, 0, 1))
    put_bm(ob, bm_)
    reassign(ob, [(lambda s, f: s in LIT and band(f)[0] > 0.66, "lamp_indicator"),
                  (lambda s, f: s in LIT and band(f)[0] < 0.30, "lamp_reverse"),
                  (lambda s, f: s in LIT and band(f)[1] > xm, "lamp_tail"),
                  (lambda s, f: s in LIT, "lamp_brake")])
reassign(O["Trunk"], [(lambda s, f: s == "KIT_octavia1u_lampglass", "lamp_brake")])   # high-level brake lamp

# ------------------------------------------------------------------ 7. UCX hulls
HULLS = {"Door_FL": 7, "Door_FR": 7, "Door_RL": 6, "Door_RR": 6, "Bonnet": 4, "Trunk": 6, "FrontBumper": 6, "RearBumper": 5,
         "Fender_FL": 4, "Fender_FR": 4, "Headlight_L": 2, "Headlight_R": 2, "Taillight_L": 1, "Taillight_R": 1, "FuelFlap": 1,
         "DoorGlass_FL": 1, "DoorGlass_FR": 1, "DoorGlass_RL": 1, "DoorGlass_RR": 1, "Plate_F": 1, "Plate_R": 1, "Wiper_L": 2, "Wiper_R": 2}
import random
random.seed(3)
for nm, k in HULLS.items():
    ob = O[nm]
    pts = [v.co.copy() for v in ob.data.vertices]          # local space
    if len(pts) < 8: continue
    # k-means in local space
    cent = random.sample(pts, k)
    for _ in range(12):
        groups = [[] for _ in cent]
        for p in pts: groups[min(range(len(cent)), key=lambda i: (p - cent[i]).length_squared)].append(p)
        cent = [sum(g, Vector()) / len(g) if g else cent[i] for i, g in enumerate(groups)]
    for i, g in enumerate(groups, 1):
        if len(g) < 4: continue
        hb = bmesh.new()
        for p in g: hb.verts.new(p)
        try: bmesh.ops.convex_hull(hb, input=hb.verts)
        except Exception: hb.free(); continue
        bmesh.ops.delete(hb, geom=[v for v in hb.verts if not v.link_faces], context='VERTS')
        if len(hb.faces) < 4: hb.free(); continue
        me = bpy.data.meshes.new(f"UCX_{nm}_{i:02d}"); hb.to_mesh(me); hb.free()
        u = bpy.data.objects.new(me.name, me); KIT.objects.link(u)
        u.parent = ob; u.matrix_parent_inverse = Matrix.Identity(4); u.matrix_basis = Matrix.Identity(4)
        u.display_type = 'WIRE'; u.hide_render = True; u["collider"] = True

# rig for the existing hinged parts (from the manifest)
man = json.load(open(os.path.join(ROOT, "src", "body_manifest.json")))
for p in man["body"]:
    if "open_axis" in p:
        ob = O[p["name"]]
        rig(ob, "rotate", "hinge_line", (0.0, float(p["max_open_deg"])), "first", open_axis=[float(x) for x in p["open_axis"]],
            held_open_deg=float(p["held_open_deg"]), hinge_a=O[p["name"] + "_HingeA"].name, hinge_b=O[p["name"] + "_HingeB"].name,
            note="turns about the HingeA-HingeB line (right-hand rule about open_axis); origin kept where the game pivots it")

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, "work", "stage3.blend"), compress=True)
json.dump(log, open(os.path.join(ROOT, "work", "stage3_parts.json"), "w"), indent=1)
print("saved; new parts:", log["new_parts"])
