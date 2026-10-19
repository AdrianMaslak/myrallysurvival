"""Škoda Octavia I (1U) 1.8T 20V engine bay set — game-ready separate parts.

Units: metres, Blender Z-up.
Frame: origin on the crankshaft axis at the middle of the block.
  +X  toward the timing-belt end (car's right side)     -X  gearbox end (car's left)
  +Y  toward the front of the car (intake / radiator)    -Y  firewall (exhaust / turbo)
  +Z  up
Every fastener is its own object named <Part>_<Kind>_NN, parented to its part,
plus an Empty <Part>_BoltPoint_NN whose local +Z points the way the fastener backs out.
"""
import bpy, bmesh, math, json, os, sys
from mathutils import Vector, Matrix, Euler

ROOT = os.path.dirname(os.path.abspath(__file__))
TEX = os.path.join(ROOT, "export", "textures")
OUT = os.path.join(ROOT, "export")
os.makedirs(OUT, exist_ok=True)

# ------------------------------------------------------------------ scene reset
bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
scene.unit_settings.system = 'METRIC'
scene.unit_settings.scale_length = 1.0

COLL = {}
for cname in ["Core", "Unbolt", "Wanted", "NiceLater", "Extras"]:
    c = bpy.data.collections.new(cname); scene.collection.children.link(c); COLL[cname] = c

# ------------------------------------------------------------------ materials
MATS = {}
def material(name):
    if name in MATS: return MATS[name]
    m = bpy.data.materials.new(name)
    try: m.use_nodes = True
    except Exception: pass
    nt = m.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    def img(kind, noncolor, loc):
        path = os.path.join(TEX, f"{name}_{kind}.png")
        im = bpy.data.images.load(path, check_existing=True)
        if noncolor: im.colorspace_settings.name = 'Non-Color'
        n = nt.nodes.new('ShaderNodeTexImage'); n.image = im; n.location = loc
        return n
    b = img("basecolor", False, (-600, 300))
    ca = nt.nodes.new('ShaderNodeVertexColor'); ca.layer_name = "Grime"; ca.location = (-600, 520)
    mix = nt.nodes.new('ShaderNodeMix'); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'; mix.location = (-300, 400)
    mix.name = "GrimeMultiply"
    mix.inputs[0].default_value = 1.0
    A = [i for i in mix.inputs if i.name == 'A' and i.type == 'RGBA'][0]; B = [i for i in mix.inputs if i.name == 'B' and i.type == 'RGBA'][0]
    R = [o for o in mix.outputs if o.type == 'RGBA'][0]
    nt.links.new(b.outputs['Color'], A); nt.links.new(ca.outputs['Color'], B); nt.links.new(R, bsdf.inputs['Base Color'])
    r = img("roughness", True, (-600, 0)); nt.links.new(r.outputs['Color'], bsdf.inputs['Roughness'])
    mt = img("metallic", True, (-600, -300)); nt.links.new(mt.outputs['Color'], bsdf.inputs['Metallic'])
    nm = img("normal", True, (-900, -600))
    nmap = nt.nodes.new('ShaderNodeNormalMap'); nmap.location = (-300, -600)
    nt.links.new(nm.outputs['Color'], nmap.inputs['Color']); nt.links.new(nmap.outputs['Normal'], bsdf.inputs['Normal'])
    MATS[name] = m
    return m

# ------------------------------------------------------------------ helpers
def align_z(d):
    d = Vector(d).normalized()
    return Vector((0, 0, 1)).rotation_difference(d).to_matrix().to_4x4()

def align_zx(z, xhint):
    z = Vector(z).normalized(); x = Vector(xhint)
    x = (x - z * x.dot(z))
    if x.length < 1e-6:
        x = Vector((1, 0, 0)) if abs(z.x) < 0.9 else Vector((0, 1, 0)); x = (x - z * x.dot(z))
    x.normalize(); y = z.cross(x)
    M = Matrix((x, y, z)).transposed().to_4x4()
    return M

def catmull(pts, per=10):
    P = [Vector(p) for p in pts]
    if len(P) == 2: return [P[0].lerp(P[1], i / per) for i in range(per + 1)]
    ext = [P[0] * 2 - P[1]] + P + [P[-1] * 2 - P[-2]]
    out = []
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        for k in range(per):
            t = k / per; t2 = t * t; t3 = t2 * t
            out.append(0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(P[-1])
    return out

def frames(path, closed=False):
    T = []
    n = len(path)
    for i in range(n):
        a = path[(i - 1) % n] if (closed or i > 0) else path[i]
        b = path[(i + 1) % n] if (closed or i < n - 1) else path[i]
        T.append((b - a).normalized())
    up = Vector((0, 0, 1)) if abs(T[0].z) < 0.9 else Vector((1, 0, 0))
    N0 = T[0].cross(up).normalized()
    Ns = [N0]
    for i in range(1, n):
        q = T[i - 1].rotation_difference(T[i]); Ns.append((q @ Ns[-1]).normalized())
    return T, Ns

PARTS = []
COLLIDE = {"ExhaustManifold", "IntakeManifold", "IntakeHose", "IntercoolerPipe_Hot", "IntercoolerPipe_Cold",
           "RadiatorHose_Upper", "RadiatorHose_Lower", "ExhaustFrontPipe", "CatalyticConverter", "CentreSilencer",
           "RearSilencer", "AccessoryBracket", "FuelRail", "Turbocharger", "EngineMount_Rear", "FuelTank", "AirFilterBox"}
class Part:
    def __init__(self, name, group, note="", fastener_note="", is_fastener=False):
        self.name, self.group, self.note, self.fnote = name, group, note, fastener_note
        self.bm = bmesh.new(); self.mats = []; self.fast = []; self.xf = Matrix.Identity(4)
        self.is_fastener = is_fastener; self.obj = None; self.extra_points = []; self.props = {}
        self.collide = False; self.hulls = []; self.pivot = None; self.pivot_note = ""; self.tool = ""
        self._skip_hull = False
        self.collide = name in COLLIDE
        if not is_fastener: PARTS.append(self)

    def mi(self, m):
        if m not in self.mats: self.mats.append(m)
        return self.mats.index(m)

    # ---- UV helpers. One texture tile = TILE metres. Cylinders, pipes and tori get whole tiles around
    #      their circumference (the tiles are periodic, so there is no colour jump at the wrap) and run
    #      continuously along their length. Boxes use a local box projection offset by their position.
    @property
    def TILE(self): return 0.05 if self.is_fastener else 0.25

    def _uv(self, t):
        return t.loops.layers.uv.get("UVMap") or t.loops.layers.uv.new("UVMap")

    def _uv_box(self, t, off):
        uv = self._uv(t); T = self.TILE; t.normal_update()
        for f in t.faces:
            n = f.normal; ax = max(range(3), key=lambda i: abs(n[i]))
            for l in f.loops:
                co = l.vert.co + off
                u, v = ((co.y, co.z) if ax == 0 else (co.x, co.z) if ax == 1 else (co.x, co.y))
                l[uv].uv = (u / T, v / T)

    def _ntiles(self, r):
        return max(1, round(2 * math.pi * r / self.TILE))

    def _uv_cyl(self, t, r, off):
        """Local Z is the axis. Sides: u around (whole tiles), v along. Caps: planar."""
        uv = self._uv(t); T = self.TILE; n = self._ntiles(r); t.normal_update()
        for f in t.faces:
            if abs(f.normal.z) > 0.7:
                for l in f.loops:
                    co = l.vert.co; l[uv].uv = ((co.x + off.x) / T, (co.y + off.y) / T)
                continue
            us = [math.atan2(l.vert.co.y, l.vert.co.x) / (2 * math.pi) for l in f.loops]
            if max(us) - min(us) > 0.5: us = [u + 1 if u < 0 else u for u in us]
            for l, u in zip(f.loops, us):
                l[uv].uv = (u * n, (l.vert.co.z + off.z) / T)

    def _merge(self, t, m, M):
        idx = self.mi(m)
        for f in t.faces: f.material_index = idx
        if t.loops.layers.uv.get("UVMap") is None: self._uv_box(t, Vector(M.translation))
        bmesh.ops.transform(t, matrix=self.xf @ M, verts=t.verts)
        if self.collide and not self._skip_hull:
            pts = [v.co.copy() for v in t.verts]
            lo = Vector((min(p.x for p in pts), min(p.y for p in pts), min(p.z for p in pts)))
            hi = Vector((max(p.x for p in pts), max(p.y for p in pts), max(p.z for p in pts)))
            if (hi - lo).length > 0.03: self.hulls.append(pts)
        me = bpy.data.meshes.new("tmp"); t.to_mesh(me); t.free()
        self.bm.from_mesh(me); bpy.data.meshes.remove(me)

    def box(self, size, c, m, rot=(0, 0, 0), bevel=0.0, seg=2):
        t = bmesh.new(); bmesh.ops.create_cube(t, size=1.0)
        for v in t.verts: v.co = Vector((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
        if bevel > 0:
            bmesh.ops.bevel(t, geom=list(t.edges), offset=min(bevel, min(size) * 0.45), offset_type='OFFSET',
                            segments=(3 if bevel >= 0.01 else seg), profile=0.5, affect='EDGES', clamp_overlap=True)
        self._uv_box(t, Vector(c))
        self._merge(t, m, Matrix.Translation(Vector(c)) @ Euler(rot).to_matrix().to_4x4())

    def cyl(self, r, depth, c, axis, m, segs=24, r2=None, bevel=0.0, caps=True, rot_about=0.0):
        t = bmesh.new()
        bmesh.ops.create_cone(t, cap_ends=caps, cap_tris=False, segments=segs, radius1=r,
                              radius2=r if r2 is None else r2, depth=depth)
        if bevel > 0 and caps:
            rim = [e for e in t.edges if abs(e.verts[0].co.z - e.verts[1].co.z) < 1e-7
                   and abs(abs(e.verts[0].co.z) - depth / 2) < 1e-6 and len(e.link_faces) == 2]
            bmesh.ops.bevel(t, geom=rim, offset=min(bevel, depth * 0.45), offset_type='OFFSET', segments=2,
                            profile=0.5, affect='EDGES', clamp_overlap=True)
        self._uv_cyl(t, max(r, r2 or 0), Vector(c))
        M = Matrix.Translation(Vector(c)) @ align_z(axis) @ Matrix.Rotation(rot_about, 4, 'Z')
        self._merge(t, m, M)

    def sphere(self, r, c, m, seg=16, ring=10, scale=(1, 1, 1)):
        t = bmesh.new(); bmesh.ops.create_uvsphere(t, u_segments=seg, v_segments=ring, radius=r)
        for v in t.verts: v.co = Vector((v.co.x * scale[0], v.co.y * scale[1], v.co.z * scale[2]))
        self._uv_cyl(t, r, Vector(c))
        self._merge(t, m, Matrix.Translation(Vector(c)))

    def torus(self, R, r, c, axis, m, seg=32, tseg=10, arc=2 * math.pi):
        t = bmesh.new(); rings = []
        full = abs(arc - 2 * math.pi) < 1e-6
        n = seg if full else seg + 1
        for i in range(n):
            a = arc * i / seg
            ring = []
            for j in range(tseg):
                b = 2 * math.pi * j / tseg
                x = (R + r * math.cos(b)) * math.cos(a); y = (R + r * math.cos(b)) * math.sin(a); z = r * math.sin(b)
                ring.append(t.verts.new((x, y, z)))
            rings.append(ring)
        uv = self._uv(t); nb = self._ntiles(R) * (1 if full else 1); nt_ = self._ntiles(r)
        for i in range(seg):
            A, B = rings[i], rings[(i + 1) % n]
            for j in range(tseg):
                f = t.faces.new((A[j], A[(j + 1) % tseg], B[(j + 1) % tseg], B[j]))
                us = [i / seg * nb, i / seg * nb, (i + 1) / seg * nb, (i + 1) / seg * nb]
                vs = [j / tseg * nt_, (j + 1) / tseg * nt_, (j + 1) / tseg * nt_, j / tseg * nt_]
                for l, u, v in zip(f.loops, us, vs): l[uv].uv = (u, v)
        if not full:
            for cap in (list(reversed(rings[0])), rings[-1]):
                f = t.faces.new(cap)
                for l in f.loops: l[uv].uv = (l.vert.co.x / self.TILE, l.vert.co.z / self.TILE)
        bmesh.ops.recalc_face_normals(t, faces=t.faces)
        self._skip_hull = True
        self._merge(t, m, Matrix.Translation(Vector(c)) @ align_z(axis))
        self._skip_hull = False

    def tube(self, pts, r, m, segs=16, per=10, caps=True, closed=False, profile=None, path=None):
        """Sweep a circle (or a 2D profile list of (u,v)) along a Catmull-Rom path; UVs run along the length."""
        P = path if path is not None else catmull(pts, per)
        T, Ns = frames(P, closed)
        prof = profile or [(r * math.cos(2 * math.pi * j / segs), r * math.sin(2 * math.pi * j / segs)) for j in range(segs)]
        t = bmesh.new(); rings = []
        for p, tg, nv in zip(P, T, Ns):
            bv = tg.cross(nv).normalized()
            rings.append([t.verts.new(p + nv * u + bv * v) for (u, v) in prof])
        k = len(prof); count = len(rings) if closed else len(rings) - 1
        perim = sum((Vector(prof[(j + 1) % k]) - Vector(prof[j])).length for j in range(k))
        around = max(1, round(perim / self.TILE))
        s = [0.0]
        for i in range(1, len(P) + (1 if closed else 0)): s.append(s[-1] + (P[i % len(P)] - P[i - 1]).length)
        uv = self._uv(t)
        for i in range(count):
            A, B = rings[i], rings[(i + 1) % len(rings)]
            for j in range(k):
                f = t.faces.new((A[j], A[(j + 1) % k], B[(j + 1) % k], B[j]))
                u0, u1 = j / k * around, (j + 1) / k * around
                v0, v1 = s[i] / self.TILE, s[i + 1] / self.TILE
                for l, uvv in zip(f.loops, ((u0, v0), (u1, v0), (u1, v1), (u0, v1))): l[uv].uv = uvv
        if caps and not closed:
            for ring in (list(reversed(rings[0])), rings[-1]):
                f = t.faces.new(ring)
                for l, (pu, pv) in zip(f.loops, prof if ring is rings[-1] else list(reversed(prof))):
                    l[uv].uv = (pu / self.TILE, pv / self.TILE)
        bmesh.ops.recalc_face_normals(t, faces=t.faces)
        if self.collide and not closed:
            start, acc = 0, 0.0
            for i in range(1, len(P)):
                acc += (P[i] - P[i - 1]).length
                if acc >= 0.06 or i == len(P) - 1:
                    self.hulls.append([(self.xf @ v.co) for ring in rings[start:i + 1] for v in ring])
                    start, acc = i, 0.0
        self._skip_hull = True
        self._merge(t, m, Matrix.Identity(4))
        self._skip_hull = False

    def text(self, body, c, size, m, depth=0.002, rot=(0, 0, 0)):
        try:
            cu = bpy.data.curves.new("txt", 'FONT'); cu.body = body; cu.size = size; cu.extrude = depth
            cu.align_x = 'CENTER'; cu.align_y = 'CENTER'
            ob = bpy.data.objects.new("txt", cu); scene.collection.objects.link(ob)
            dg = bpy.context.evaluated_depsgraph_get(); ev = ob.evaluated_get(dg); me = ev.to_mesh()
            t = bmesh.new(); t.from_mesh(me); ev.to_mesh_clear()
            bpy.data.objects.remove(ob); bpy.data.curves.remove(cu)
            for lay in list(t.loops.layers.uv): t.loops.layers.uv.remove(lay)
            self._uv_box(t, Vector(c))
            self._merge(t, m, Matrix.Translation(Vector(c)) @ Euler(rot).to_matrix().to_4x4())
        except Exception as e:
            print("text skipped:", e)

    # -------------------------------------------------------------- fasteners
    def fastener(self, kind, pos, axis, size="M8", label="", hose_r=0.02, xdir=(0, 0, 1), tool=None):
        n = len(self.fast) + 1
        nice = {"bolt": "Bolt", "nut": "Nut", "allen": "Bolt", "screw": "Screw", "clip": "Clip", "clamp": "Clamp",
                "pin": "RubberMount", "drain": "DrainPlug", "banjo": "BanjoBolt", "stud": "Nut"}[kind]
        fp = Part(f"{self.name}_{nice}_{n:02d}", self.group, is_fastener=True)
        fp.xf = Matrix.Translation(Vector(pos)) @ align_zx(axis, xdir)
        if kind in ("clip", "pin", "screw", "clamp"): size = {"clip": "spring clip", "pin": "rubber grommet", "screw": "plastic screw", "clamp": f"worm clamp {round(hose_r*2000)+5}-{round(hose_r*2000)+20} mm"}[kind] if size in ("M8", None) else size
        AF = {"M5": (0.008, 0.0035), "M6": (0.010, 0.004), "M8": (0.013, 0.0053), "M10": (0.017, 0.0064),
              "M12": (0.019, 0.0075), "M14": (0.019, 0.008)}
        af, h = AF.get(size, (0.013, 0.0053))
        if tool is None:
            if kind in ("bolt", "nut", "stud", "drain", "banjo"):
                tool = f"{round(af*1000)} mm spanner/socket"
            elif kind == "allen":
                tool = {"M6": "hex key 5 mm", "M8": "hex key 6 mm", "M10": "hex key 8 mm"}.get(size, "hex key 6 mm")
            elif kind == "screw": tool = "Torx T25"
            elif kind == "clamp": tool = "flat screwdriver (or 7 mm socket)"
            else: tool = "hands"
        elif tool.endswith("mm") and kind in ("bolt", "nut", "stud", "banjo"):
            af = int(tool.split()[0]) / 1000.0; tool = tool + " spanner/socket"
        R = af / math.sqrt(3)
        R = af / math.sqrt(3)
        if kind in ("bolt", "drain"):
            fp.cyl(af * 0.62, 0.0012, (0, 0, 0.0006), (0, 0, 1), "zinc", segs=20)
            fp.cyl(R, h, (0, 0, 0.0012 + h / 2), (0, 0, 1), "zinc", segs=6, bevel=0.0006)
        elif kind in ("nut", "stud"):
            fp.cyl(af * 0.62, 0.0012, (0, 0, 0.0006), (0, 0, 1), "zinc", segs=20)
            fp.cyl(R, h * 1.5, (0, 0, 0.0012 + h * 0.75), (0, 0, 1), "zinc", segs=6, bevel=0.0006)
            d = {"M5": .005, "M6": .006, "M8": .008, "M10": .010, "M12": .012, "M14": .014}[size]
            fp.cyl(d / 2, 0.006, (0, 0, 0.0012 + h * 1.5 + 0.003), (0, 0, 1), "zinc", segs=10)
        elif kind == "allen":
            fp.cyl(af * 0.5, h * 1.1, (0, 0, h * 0.55), (0, 0, 1), "zinc", segs=16, bevel=0.0005)
            fp.cyl(af * 0.22, 0.001, (0, 0, h * 1.1 + 0.0002), (0, 0, 1), "painted_black", segs=6)
        elif kind == "screw":
            fp.cyl(0.009, 0.004, (0, 0, 0.002), (0, 0, 1), "plastic_black", segs=20, r2=0.007)
            fp.box((0.012, 0.0025, 0.002), (0, 0, 0.0045), "plastic_black")
        elif kind == "clip":
            fp.box((0.014, 0.008, 0.006), (0, 0, 0.003), "plastic_black", bevel=0.0015)
        elif kind == "pin":
            fp.cyl(0.011, 0.012, (0, 0, 0.006), (0, 0, 1), "rubber", segs=16, bevel=0.002)
        elif kind == "banjo":
            fp.torus(0.009, 0.0035, (0, 0, -0.004), (0, 0, 1), "zinc", seg=16, tseg=8)
            fp.cyl(R, h, (0, 0, h / 2), (0, 0, 1), "zinc", segs=6, bevel=0.0005)
        elif kind == "clamp":
            # worm-drive hose clamp around the hose (local Z = hose axis); housing + screw sit on local +X (= xdir: up/out)
            fp.torus(hose_r + 0.002, 0.0012, (0, 0, 0), (0, 0, 1), "zinc", seg=28, tseg=4)
            fp.box((0.010, 0.012, 0.009), (hose_r + 0.006, 0, 0), "zinc", bevel=0.001)
            fp.cyl(0.0035, 0.004, (hose_r + 0.006, 0.008, 0), (0, 1, 0), "zinc", segs=6)   # screw head
        bp_pos, bp_axis = Vector(pos), Vector(axis).normalized()
        if kind == "clamp":   # bolt point on the clamp screw head; tool approaches along the screw (local +Y)
            bp_pos = fp.xf @ Vector((hose_r + 0.006, 0.010, 0)); bp_axis = (fp.xf.to_3x3() @ Vector((0, 1, 0))).normalized()
        fp.tool = tool
        self.fast.append(dict(name=fp.name, kind=kind, size=size, tool=tool, label=label,
                              position=[round(x, 4) for x in bp_pos], axis=[round(x, 4) for x in bp_axis]))
        fp.parent_part = self
        FASTENERS.append(fp)
        return fp

    def point(self, name, pos, axis):
        self.extra_points.append((name, Vector(pos), Vector(axis)))

FASTENERS = []

def finalize(p, coll, parent=None):
    bm = p.bm
    bm.normal_update()
    if len(bm.verts) == 0: return None
    lo = Vector((min(v.co.x for v in bm.verts), min(v.co.y for v in bm.verts), min(v.co.z for v in bm.verts)))
    hi = Vector((max(v.co.x for v in bm.verts), max(v.co.y for v in bm.verts), max(v.co.z for v in bm.verts)))
    c = (lo + hi) / 2
    bbc = c.copy()
    if p.pivot is not None: c = Vector(p.pivot)
    T = 0.05 if p.is_fastener else 0.25
    if bm.loops.layers.uv.get("UVMap") is None:
        uv = bm.loops.layers.uv.verify()
        for f in bm.faces:
            n = f.normal; ax = max(range(3), key=lambda i: abs(n[i]))
            for l in f.loops:
                co = l.vert.co
                u, v = ((co.y, co.z) if ax == 0 else (co.x, co.z) if ax == 1 else (co.x, co.y))
                l[uv].uv = (u / T, v / T)
    bmesh.ops.translate(bm, vec=-c, verts=bm.verts)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    me = bpy.data.meshes.new(p.name)
    bm.to_mesh(me); bm.free()
    for mname in p.mats: me.materials.append(material(mname))
    for poly in me.polygons: poly.use_smooth = True
    try: me.set_sharp_from_angle(angle=math.radians(38))
    except Exception: pass
    ob = bpy.data.objects.new(p.name, me)
    coll.objects.link(ob)
    if parent is not None:
        ob.parent = parent
        ob.location = c - parent.location
    else:
        ob.location = c
    p.obj = ob; p.center = c; p.bbox = (lo, hi); p.bbc = bbc
    me.calc_loop_triangles(); p.tris = len(me.loop_triangles)
    return ob

def bolt_point(name, part_obj, pos, axis, coll):
    e = bpy.data.objects.new(name, None)
    e.empty_display_type = 'SINGLE_ARROW'; e.empty_display_size = 0.025
    coll.objects.link(e)
    e.parent = part_obj
    M = Matrix.Translation(Vector(pos) - part_obj.location) @ align_z(axis)
    e.matrix_parent_inverse = Matrix.Identity(4)
    e.matrix_basis = M
    return e

# ================================================================== GEOMETRY
CX = [-0.132, -0.044, 0.044, 0.132]      # 88 mm bore spacing
MAINS = [-0.176, -0.088, 0.0, 0.088, 0.176]
X_, Y_, Z_ = (1, 0, 0), (0, 1, 0), (0, 0, 1)
nX, nY, nZ = (-1, 0, 0), (0, -1, 0), (0, 0, -1)

# ---------------------------------------------------------------- CORE
blk = Part("EngineBlock", "Core", "Cast-iron 06A block, bore 81.0 mm, stroke 86.4 mm, 1781 cc. Stays in the car.")
blk.box((0.44, 0.20, 0.20), (0.005, 0, 0.122), "cast_iron", bevel=0.014)
blk.box((0.44, 0.235, 0.10), (0.005, 0, -0.02), "cast_iron", bevel=0.014)
blk.box((0.445, 0.215, 0.012), (0.005, 0, 0.216), "cast_iron", bevel=0.005)
for cx in CX:
    for sy in (1, -1):
        if sy == 1 and cx > 0: continue          # flat pad where the accessory bracket bolts on
        blk.cyl(0.047, 0.165, (cx, sy * 0.083, 0.13), Z_, "cast_iron", segs=20)
for x in MAINS:
    for sy in (1, -1):
        if sy == 1 and x > 0.06: continue       # bracket pad
        blk.box((0.014, 0.012, 0.10), (x, sy * 0.1175, -0.01), "cast_iron")
blk.box((0.014, 0.28, 0.30), (-0.222, 0, 0.04), "cast_iron", bevel=0.012)     # gearbox flange face
blk.cyl(0.045, 0.012, (0.231, 0, 0), X_, "cast_iron", segs=32)                 # front crank seal housing
blk.box((0.05, 0.03, 0.05), (-0.10, 0.112, -0.005), "cast_iron", bevel=0.004)   # oil filter flange boss
blk.box((0.03, 0.06, 0.08), (0.24, -0.13, 0.17), "cast_iron", bevel=0.004)    # right engine-mount boss
blk.tube([(-0.215, 0.118, 0.0), (-0.215, 0.15, 0.15), (-0.215, 0.16, 0.33), (-0.215, 0.16, 0.40)], 0.0055, "zinc", segs=10)  # dipstick tube
blk.point("EngineBlock_DipstickTubeTop", (-0.215, 0.16, 0.40), Z_)

head = Part("CylinderHead", "Core", "Aluminium 20-valve head (3 intake, 2 exhaust per cylinder), DOHC, belt + chain cam drive.")
head.box((0.44, 0.23, 0.113), (0.005, 0, 0.2785), "cast_alu", bevel=0.012)
for cx in CX:
    head.box((0.062, 0.003, 0.045), (cx, -0.1165, 0.283), "cast_alu")
head.box((0.40, 0.003, 0.065), (0, 0.1165, 0.285), "cast_alu")
head.box((0.035, 0.08, 0.065), (-0.232, 0.0, 0.29), "cast_alu", bevel=0.006)   # coolant flange (left end)
head.cyl(0.017, 0.035, (-0.262, 0.03, 0.29), X_, "cast_alu", segs=16)          # upper hose outlet
head.box((0.02, 0.21, 0.085), (0.229, 0, 0.29), "cast_alu", bevel=0.005)

cover = Part("CamCover", "Core", "Cast cylinder-head cover with coil wells and oil filler neck.",
             "9 x M6 nuts (Pelican Parts Golf IV 1.8T valve cover article).")
cover.box((0.42, 0.205, 0.05), (0.005, 0, 0.36), "cast_alu", bevel=0.018)
for x in (-0.15, -0.05, 0.05, 0.15):
    cover.box((0.006, 0.19, 0.008), (x, 0, 0.387), "cast_alu")
for cx in CX:
    cover.cyl(0.024, 0.008, (cx, 0, 0.388), Z_, "cast_alu", segs=20)
cover.cyl(0.024, 0.016, (-0.19, 0.05, 0.391), Z_, "cast_alu", segs=24)        # oil filler neck
cover.cyl(0.009, 0.03, (0.18, -0.09, 0.37), (0, -1, 0.3), "plastic_black", segs=10)  # breather port
for x in (-0.088, 0.0, 0.088): cover.fastener("nut", (x, 0.0, 0.386), Z_, "M6")
for sy in (1, -1):
    for x in (-0.15, 0.0, 0.15): cover.fastener("nut", (x, sy * 0.088, 0.386), Z_, "M6")

sump = Part("OilSump", "Core", "Pressed-steel oil pan with drain plug. Simulated: cracks and leaks.",
            "20 x M6 flange bolts placed evenly (approximate count) + M14 drain plug.")
sump.box((0.43, 0.23, 0.035), (0.005, 0, -0.087), "painted_black", bevel=0.008)
sump.box((0.39, 0.19, 0.13), (0.02, 0.0, -0.17), "painted_black", bevel=0.02)
sump.box((0.04, 0.03, 0.03), (-0.03, -0.095, -0.13), "painted_black", bevel=0.004)   # turbo oil return boss
for x in [-0.19 + i * 0.065 for i in range(7)]:
    for sy in (1, -1): sump.fastener("bolt", (x, sy * 0.107, -0.105), nZ, "M6")
for y in (-0.07, 0.0, 0.07):
    sump.fastener("bolt", (-0.205, y, -0.105), nZ, "M6"); sump.fastener("bolt", (0.215, y, -0.105), nZ, "M6")
sump.fastener("drain", (0.10, 0.0, -0.236), nZ, "M14", "drain plug")

tbu = Part("TimingBeltCover_Upper", "Core", "Plastic upper timing-belt cover over the camshaft sprockets.", "2 spring clips.")
tbu.box((0.014, 0.20, 0.17), (0.252, 0.0, 0.31), "plastic_black", bevel=0.006)
for y in (-0.07, 0.07): tbu.fastener("clip", (0.259, y, 0.385), X_)

tbl = Part("TimingBeltCover_Lower", "Core", "Plastic lower/centre timing-belt cover behind the crank pulley.", "3 x M6 bolts (approximate).")
tbl.box((0.014, 0.17, 0.24), (0.252, -0.01, 0.09), "plastic_black", bevel=0.006)
for p in [(0.259, -0.07, 0.18), (0.259, -0.085, 0.03), (0.259, 0.06, 0.20)]: tbl.fastener("bolt", p, X_, "M6")

pul = Part("CrankPulley", "Core", "Vibration damper / poly-V crank pulley.", "4 x M8 Allen bolts (Torx/Allen) on the hub.")
pul.cyl(0.075, 0.022, (0.273, 0, 0), X_, "painted_black", segs=40, bevel=0.002)
for k in range(5): pul.torus(0.075, 0.0016, (0.265 + k * 0.004, 0, 0), X_, "painted_black", seg=40, tseg=4)
pul.cyl(0.042, 0.012, (0.288, 0, 0), X_, "alu_machined", segs=32, bevel=0.002)
pul.cyl(0.03, 0.025, (0.2495, 0, 0), X_, "painted_black", segs=24)   # hub boss through the cover
for k in range(4):
    a = math.pi / 4 + k * math.pi / 2
    pul.fastener("allen", (0.294, 0.025 * math.cos(a), 0.025 * math.sin(a)), X_, "M8", tool="hex key 6 mm")

# Accessory (poly-V) belt path: crank, A/C, alternator, power steering
PULLEYS = [(0.0, 0.0, 0.075), (0.21, -0.04, 0.055), (0.215, 0.085, 0.028), (0.18, 0.245, 0.06)]
def hull2(points):
    pts = sorted(points)
    cr = lambda o, a, b: (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, up = [], []
    for p in pts:
        while len(lo) >= 2 and cr(lo[-2], lo[-1], p) <= 0: lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(up) >= 2 and cr(up[-2], up[-1], p) <= 0: up.pop()
        up.append(p)
    return lo[:-1] + up[:-1]
def belt_path(circles, x, pad, step=0.004):
    raw = []
    for (y, z, r) in circles:
        for i in range(120):
            a = 2 * math.pi * i / 120; raw.append((y + math.cos(a) * (r + pad), z + math.sin(a) * (r + pad)))
    h = hull2(raw); out = []
    for i in range(len(h)):
        a, b = Vector((x, h[i][0], h[i][1])), Vector((x, h[(i + 1) % len(h)][0], h[(i + 1) % len(h)][1]))
        n = max(1, int((b - a).length / step))
        for k in range(n): out.append(a.lerp(b, k / n))
    return out
belt = Part("AuxiliaryBelt", "Core", "Poly-V accessory belt: crank, A/C compressor, alternator, power-steering pump.")
belt.tube(None, 0, "rubber", closed=True, caps=False, profile=[(-0.0025, -0.011), (0.0025, -0.011), (0.0025, 0.011), (-0.0025, 0.011)],
          path=belt_path(PULLEYS, 0.274, 0.0025))

brk = Part("AccessoryBracket", "Core", "Cast bracket on the front of the block carrying alternator, A/C and PAS pump.",
           "4 x M10 bolts to block (approximate).")
brk.box((0.16, 0.03, 0.30), (0.16, 0.133, 0.03), "cast_alu", bevel=0.010)          # plate y .118-.148, z -0.12..0.18
for z in (0.15, 0.05):
    for x in (0.105, 0.235):
        brk.box((0.02, 0.05, 0.03), (x, 0.173, z), "cast_alu", bevel=0.004)      # alternator lugs
for p in [(0.088, 0.149, 0.17), (0.23, 0.149, 0.17), (0.09, 0.149, -0.045), (0.165, 0.149, -0.10)]:
    brk.fastener("bolt", p, Y_, "M10")

# ---------------------------------------------------------------- UNBOLT PARTS
ecov = Part("EngineCover", "Unbolt", "Plastic '1.8T' engine cover over the cam cover and fuel rail.",
            "4 quarter-turn plastic screws (Pelican Parts Golf IV 1.8T).")
ecov.box((0.36, 0.20, 0.028), (0.02, 0.03, 0.428), "plastic_black", bevel=0.012)
ecov.box((0.36, 0.11, 0.024), (0.02, 0.17, 0.405), "plastic_black", rot=(-0.38, 0, 0), bevel=0.01)
ecov.box((0.30, 0.004, 0.006), (0.02, -0.03, 0.443), "plastic_black")
ecov.text("1.8 T", (0.05, 0.04, 0.4415), 0.05, "plastic_black", depth=0.0012)
ecov.text("20V", (0.05, 0.0, 0.4415), 0.022, "plastic_black", depth=0.0008)
for p in [(-0.14, -0.045, 0.443), (0.18, -0.045, 0.443), (-0.14, 0.11, 0.443), (0.18, 0.11, 0.443)]:
    ecov.fastener("screw", p, Z_, tool="Phillips screwdriver (quarter turn)")

airbox = Part("AirFilterBox", "Unbolt", "Air filter housing with lid, panel filter and MAF sensor on the outlet.",
              "4 lid clips; housing sits on rubber grommets (pull up).")
airbox.box((0.24, 0.26, 0.10), (-0.50, 0.27, 0.30), "plastic_black", bevel=0.016)
airbox.box((0.245, 0.265, 0.05), (-0.50, 0.27, 0.375), "plastic_black", bevel=0.016)
for k in range(6): airbox.box((0.22, 0.006, 0.006), (-0.50, 0.17 + k * 0.04, 0.401), "plastic_black")
airbox.tube([(-0.50, 0.40, 0.29), (-0.49, 0.47, 0.28), (-0.47, 0.52, 0.25)], 0.03, "plastic_black", segs=16, caps=False)
airbox.cyl(0.035, 0.03, (-0.50, 0.125, 0.33), Y_, "plastic_black", segs=20)
airbox.cyl(0.037, 0.07, (-0.50, 0.08, 0.33), Y_, "plastic_black", segs=20, bevel=0.002)  # MAF
airbox.box((0.03, 0.03, 0.025), (-0.50, 0.08, 0.37), "plastic_black", bevel=0.003)
for p, ax in [((-0.62, 0.20, 0.35), nX), ((-0.62, 0.34, 0.35), nX), ((-0.38, 0.20, 0.35), X_), ((-0.38, 0.34, 0.35), X_)]:
    airbox.fastener("clip", p, ax)
for p in [(-0.58, 0.18, 0.25), (-0.42, 0.36, 0.25)]: airbox.point(f"AirFilterBox_Grommet", p, Z_)

ihose_pts = [(-0.50, 0.045, 0.33), (-0.50, 0.015, 0.33), (-0.46, -0.04, 0.32), (-0.34, -0.10, 0.30), (-0.22, -0.20, 0.19), (-0.15, -0.22, 0.097), (-0.115, -0.22, 0.095)]
ihose = Part("IntakeHose", "Unbolt", "Rubber hose from MAF to the turbo compressor inlet.", "2 worm-drive hose clamps.")
ihose.pivot = (-0.115, -0.22, 0.095)
ihose.tube(ihose_pts, 0.032, "rubber", segs=18, per=12, caps=False)
for k in range(6):
    ihose.torus(0.033, 0.002, catmull(ihose_pts, 12)[10 + k * 3], (Vector(catmull(ihose_pts, 12)[11 + k * 3]) - Vector(catmull(ihose_pts, 12)[10 + k * 3])), "rubber", seg=20, tseg=4)
ihose.fastener("clamp", (-0.50, 0.035, 0.33), Y_, hose_r=0.032, xdir=(0, 0, 1))
ihose.fastener("clamp", (-0.124, -0.22, 0.095), X_, hose_r=0.032, xdir=(0, -1, 0.6))

intk = Part("IntakeManifold", "Unbolt", "Cast intake manifold: 4 runners into a plenum, throttle body on the left end.",
            "8 x M8 bolts + 2 x M8 nuts to the head, + 1 Allen bolt to the accessory bracket (Pelican Parts Golf IV 1.8T).")
intk.box((0.40, 0.012, 0.065), (0, 0.124, 0.285), "cast_alu", bevel=0.003)
for cx in CX:
    intk.tube([(cx, 0.124, 0.285), (cx, 0.165, 0.30), (cx, 0.215, 0.285), (cx, 0.245, 0.235), (cx, 0.25, 0.20)], 0.022, "cast_alu", segs=16, caps=False)
intk.cyl(0.045, 0.44, (-0.01, 0.255, 0.19), X_, "cast_alu", segs=28, bevel=0.01)
intk.cyl(0.04, 0.012, (-0.236, 0.255, 0.19), X_, "cast_alu", segs=24)
intk.box((0.02, 0.03, 0.07), (0.09, 0.24, 0.115), "cast_alu", bevel=0.004)
intk.box((0.02, 0.09, 0.02), (0.09, 0.193, 0.08), "cast_alu", bevel=0.004)
for x in (-0.176, -0.088, 0.0, 0.088, 0.176):
    intk.fastener("bolt", (x, 0.130, 0.309), Y_, "M6", tool="10 mm")
for x in (-0.176, -0.088, 0.0, 0.088, 0.176):
    intk.fastener("nut" if abs(x) > 0.15 else "bolt", (x, 0.130, 0.261), Y_, "M6", tool="10 mm")
intk.fastener("allen", (0.09, 0.239, 0.08), Y_, "M8", "support bracket", tool="hex key 6 mm")

exm = Part("ExhaustManifold", "Unbolt", "Cast-iron 4-into-1 manifold, turbo flange underneath.",
           "13 x M8 copper nuts on studs: 8 top, 5 bottom (Pelican Parts A4 1.8T; same head).")
exm.box((0.40, 0.012, 0.062), (0, -0.124, 0.283), "exhaust", bevel=0.003)
for cx in CX:
    exm.tube([(cx, -0.124, 0.283), (cx, -0.15, 0.28), (cx * 0.7, -0.185, 0.25), (cx * 0.25 + 0.01, -0.21, 0.215), (0.01, -0.22, 0.205)], 0.02, "exhaust", segs=14, caps=False)
exm.cyl(0.04, 0.03, (0.01, -0.22, 0.197), Z_, "exhaust", segs=20)
exm.box((0.085, 0.075, 0.012), (0.01, -0.22, 0.183), "exhaust", bevel=0.004)
for cx in CX:
    for dx in (-0.026, 0.026): exm.fastener("nut", (cx + dx, -0.130, 0.305), nY, "M8")
for x in (-0.176, -0.088, 0.0, 0.088, 0.176): exm.fastener("nut", (x, -0.130, 0.262), nY, "M8")

tur = Part("Turbocharger", "Unbolt", "KKK K03-type turbo (K03s on the 180 PS RS). Wastegate actuator rod adjusts boost.",
           "3 x M10 bolts to manifold; support bracket 1 bolt; oil return 2 bolts; oil feed + 2 coolant banjo bolts (Pelican Parts Golf IV 1.8T turbo; banjo count approximate).")
TC = Vector((0.01, -0.22, 0.095))   # turbo shaft axis (along X); turbine inlet flange top at z 0.177 meets manifold flange
tur.torus(0.045, 0.03, (0.035, -0.22, 0.095), X_, "turbine_iron", seg=36, tseg=12)
tur.cyl(0.05, 0.05, (0.035, -0.22, 0.095), X_, "turbine_iron", segs=28, bevel=0.004)
tur.box((0.06, 0.06, 0.012), (0.01, -0.22, 0.171), "turbine_iron", bevel=0.003)
tur.box((0.08, 0.07, 0.01), (0.01, -0.22, 0.172), "turbine_iron", bevel=0.003)
tur.box((0.05, 0.045, 0.03), (0.025, -0.22, 0.152), "turbine_iron", bevel=0.006)
tur.cyl(0.032, 0.03, (0.075, -0.22, 0.095), X_, "turbine_iron", segs=24)
tur.cyl(0.046, 0.01, (0.092, -0.22, 0.095), X_, "turbine_iron", segs=28, bevel=0.002)
tur.cyl(0.03, 0.045, (-0.012, -0.22, 0.095), X_, "cast_iron", segs=20)
tur.torus(0.046, 0.028, (-0.062, -0.22, 0.095), X_, "cast_alu", seg=36, tseg=12)
tur.cyl(0.06, 0.02, (-0.05, -0.22, 0.095), X_, "cast_alu", segs=32, bevel=0.003)
tur.cyl(0.033, 0.04, (-0.094, -0.22, 0.095), X_, "cast_alu", segs=28, bevel=0.003)
tur.tube([(-0.062, -0.256, 0.075), (-0.062, -0.275, 0.045), (-0.065, -0.276, 0.008)], 0.022, "cast_alu", segs=16)
tur.cyl(0.032, 0.03, (-0.075, -0.275, 0.16), X_, "painted_black", segs=24, bevel=0.003)   # wastegate actuator can
tur.cyl(0.003, 0.11, (-0.006, -0.275, 0.16), X_, "zinc", segs=8)                         # actuator rod
tur.box((0.006, 0.012, 0.035), (0.05, -0.27, 0.145), "zinc", bevel=0.001)                # wastegate lever
# oil feed (block -> top of centre housing), coolant in/out (block), oil return (bottom -> sump boss face)
tur.tube([(-0.006, -0.22, 0.125), (-0.006, -0.205, 0.165), (-0.006, -0.15, 0.17), (-0.006, -0.114, 0.17)], 0.004, "zinc", segs=8)
tur.tube([(-0.018, -0.22, 0.125), (-0.018, -0.205, 0.155), (-0.018, -0.16, 0.158), (-0.018, -0.124, 0.158)], 0.005, "zinc", segs=8)
tur.tube([(0.014, -0.22, 0.065), (0.014, -0.205, 0.046), (0.014, -0.14, 0.042), (0.014, -0.102, 0.042)], 0.005, "zinc", segs=8)
tur.tube([(-0.03, -0.22, 0.066), (-0.03, -0.21, 0.0), (-0.03, -0.15, -0.08), (-0.03, -0.125, -0.13), (-0.03, -0.113, -0.13)], 0.009, "zinc", segs=10)
tur.box((0.045, 0.006, 0.025), (-0.03, -0.113, -0.13), "zinc", bevel=0.001)              # return flange on sump boss
tur.box((0.012, 0.02, 0.06), (-0.012, -0.21, 0.04), "painted_black", bevel=0.002)       # support bracket, vertical
tur.box((0.012, 0.09, 0.02), (-0.012, -0.1625, 0.01), "painted_black", bevel=0.002)     # foot to block skirt
tur.box((0.012, 0.006, 0.03), (-0.012, -0.1205, 0.015), "painted_black", bevel=0.001)   # bolt tab
for p in [(0.042, -0.193, 0.167), (-0.022, -0.193, 0.167), (-0.022, -0.247, 0.167)]:
    tur.fastener("bolt", p, nZ, "M10", "turbo to manifold")
tur.fastener("bolt", (-0.012, -0.1235, 0.015), nY, "M10", "support bracket")
for dx in (-0.016, 0.016): tur.fastener("bolt", (-0.03 + dx, -0.116, -0.13), nY, "M6", "oil return")
tur.fastener("banjo", (-0.006, -0.116, 0.17), nY, "M10", "oil feed")
tur.fastener("banjo", (-0.018, -0.126, 0.158), nY, "M10", "coolant")
tur.fastener("banjo", (0.014, -0.104, 0.042), nY, "M10", "coolant")
tur.fastener("nut", (-0.057, -0.275, 0.16), X_, "M6", "boost adjust (wastegate rod)")
tur.props["boost_setting"] = "Wastegate rod length; stock K03 ~0.8 bar, K03s (RS) ~1.0 bar"

ic = Part("Intercooler", "Unbolt", "Side-mount intercooler in the left front corner.", "2 x M8 bracket bolts on top + 2 rubber mounts below.")
ic.box((0.24, 0.06, 0.20), (-0.60, 0.55, -0.13), "rad_core", bevel=0.002)
for z in (-0.012, -0.248):
    ic.box((0.25, 0.07, 0.035), (-0.60, 0.55, z), "plastic_black", bevel=0.008)
ic.cyl(0.026, 0.05, (-0.66, 0.51, -0.248), Y_, "plastic_black", segs=18)
ic.cyl(0.028, 0.05, (-0.54, 0.51, -0.012), Y_, "plastic_black", segs=18)
for x in (-0.69, -0.51):
    ic.box((0.025, 0.02, 0.02), (x, 0.55, 0.012), "painted_black")
    ic.fastener("bolt", (x, 0.55, 0.022), Z_, "M8")
    ic.fastener("pin", (x, 0.55, -0.266), nZ)

hot_pts = [(-0.065, -0.276, 0.008), (-0.065, -0.276, -0.02), (-0.07, -0.285, -0.07), (-0.20, -0.26, -0.18), (-0.45, -0.10, -0.25), (-0.62, 0.25, -0.27), (-0.66, 0.42, -0.25), (-0.66, 0.455, -0.248), (-0.66, 0.485, -0.248)]
icph = Part("IntercoolerPipe_Hot", "Unbolt", "Compressor outlet to intercooler inlet, under the gearbox.", "2 hose clamps.")
icph.tube(hot_pts, 0.025, "rubber", segs=16, per=12, caps=False)
icph.pivot = (-0.065, -0.276, 0.008)
icph.fastener("clamp", (-0.065, -0.277, 0.0), nZ, hose_r=0.025, xdir=(0, -1, 0))
icph.fastener("clamp", (-0.66, 0.478, -0.248), Y_, hose_r=0.026, xdir=(-1, 0, 0))
cold_pts = [(-0.54, 0.485, -0.012), (-0.54, 0.455, -0.012), (-0.51, 0.42, 0.07), (-0.40, 0.33, 0.17), (-0.34, 0.26, 0.19), (-0.307, 0.255, 0.19)]
icpc = Part("IntercoolerPipe_Cold", "Unbolt", "Intercooler outlet up to the throttle body.", "2 hose clamps.")
icpc.tube(cold_pts, 0.028, "rubber", segs=16, per=12, caps=False)
icpc.pivot = (-0.307, 0.255, 0.19)
icpc.fastener("clamp", (-0.54, 0.478, -0.01), Y_, hose_r=0.028, xdir=(-1, 0, 0.3))
icpc.fastener("clamp", (-0.315, 0.256, 0.19), X_, hose_r=0.034, xdir=(0, -0.2, -1))

alt = Part("Alternator", "Unbolt", "90 A alternator on the accessory bracket, driven by the poly-V belt.",
           "2 x M10 through-bolts (medium confidence; not confirmed in a written source).")
alt.cyl(0.06, 0.09, (0.17, 0.215, 0.085), X_, "cast_alu", segs=32, bevel=0.006)
for k in range(4): alt.torus(0.06, 0.003, (0.14 + k * 0.02, 0.215, 0.085), X_, "cast_alu", seg=32, tseg=4)
alt.cyl(0.054, 0.01, (0.12, 0.215, 0.085), X_, "plastic_black", segs=28)
alt.cyl(0.028, 0.02, (0.271, 0.215, 0.085), X_, "alu_machined", segs=24)
alt.cyl(0.008, 0.05, (0.24, 0.215, 0.085), X_, "zinc", segs=10)
for z in (0.15, 0.05): alt.box((0.09, 0.04, 0.025), (0.17, 0.175, z), "cast_alu", bevel=0.004)
alt.box((0.02, 0.015, 0.012), (0.13, 0.215, 0.148), "zinc")                  # B+ terminal
alt.pivot = (0.245, 0.175, 0.15)
for z in (0.15, 0.05): alt.fastener("bolt", (0.245, 0.175, z), X_, "M10")

st = Part("StarterMotor", "Unbolt", "Starter on the front of the 02J bellhousing.",
          "2 x M10 bolts (VW workshop manual), plus up to 2 extra securing bolts 'if fitted'.")
st.cyl(0.045, 0.15, (-0.35, 0.225, -0.06), X_, "painted_black", segs=28, bevel=0.004)
st.cyl(0.022, 0.11, (-0.34, 0.225, 0.0), X_, "zinc", segs=20, bevel=0.003)
st.cyl(0.045, 0.035, (-0.2575, 0.225, -0.06), X_, "cast_alu", segs=28)
st.box((0.012, 0.08, 0.035), (-0.234, 0.225, -0.06), "cast_alu", bevel=0.004)
st.box((0.02, 0.012, 0.012), (-0.39, 0.225, 0.025), "zinc")
st.pivot = (-0.227, 0.195, -0.06)
for y in (0.195, 0.255): st.fastener("bolt", (-0.227, y, -0.06), X_, "M10")

ecu = Part("ECU", "Unbolt", "Bosch Motronic ME7.5 engine computer in the plenum chamber by the firewall.", "3 fixings (2 bolts + 1 nut).")
ecu.box((0.16, 0.04, 0.12), (0.0, -0.58, 0.46), "alu_machined", bevel=0.009)
ecu.box((0.08, 0.03, 0.03), (0.0, -0.55, 0.42), "plastic_black", bevel=0.003)
for k in range(8): ecu.box((0.15, 0.003, 0.002), (0.0, -0.5615, 0.47 + k * 0.006), "alu_machined")
for x in (-0.07, 0.07): ecu.box((0.02, 0.006, 0.02), (x, -0.565, 0.53), "alu_machined")
ecu.box((0.02, 0.006, 0.02), (0.0, -0.565, 0.39), "alu_machined")
ecu.fastener("bolt", (-0.07, -0.561, 0.53), Y_, "M6"); ecu.fastener("bolt", (0.07, -0.561, 0.53), Y_, "M6")
ecu.fastener("nut", (0.0, -0.561, 0.39), Y_, "M6")

# ---------------------------------------------------------------- WANTED
rad = Part("Radiator", "Wanted", "Aluminium core with plastic side tanks. Simulated: damage, leaks, steam.",
           "2 x M6 top bracket bolts + 2 rubber feet.")
rad.box((0.56, 0.03, 0.38), (0, 0.52, 0.02), "rad_core", bevel=0.002)
for sx in (1, -1): rad.box((0.05, 0.045, 0.40), (sx * 0.305, 0.52, 0.02), "plastic_black", bevel=0.01)
rad.cyl(0.017, 0.03, (-0.305, 0.49, 0.20), Y_, "plastic_black", segs=16)
rad.cyl(0.017, 0.03, (0.305, 0.49, -0.16), Y_, "plastic_black", segs=16)
rad.cyl(0.006, 0.03, (0.32, 0.52, 0.215), Z_, "plastic_black", segs=10)   # bleed / steam point
rad.point("Radiator_SteamPoint", (0.0, 0.50, 0.21), Z_)
rad.point("Radiator_LeakPoint", (0.30, 0.52, -0.18), nZ)
for sx in (1, -1):
    rad.box((0.03, 0.02, 0.015), (sx * 0.28, 0.52, 0.215), "painted_black")
    rad.fastener("bolt", (sx * 0.28, 0.52, 0.223), Z_, "M6")
    rad.fastener("pin", (sx * 0.28, 0.52, -0.172), nZ)

fan = Part("RadiatorFan", "Wanted", "Twin electric fans in a plastic shroud behind the radiator.", "4 x M6 bolts to radiator.")
fan.box((0.56, 0.012, 0.36), (0, 0.495, 0.02), "plastic_black", bevel=0.006)
for sx in (1, -1):
    c = (sx * 0.14, 0.485, 0.02)
    fan.torus(0.152, 0.006, (sx * 0.14, 0.488, 0.02), Y_, "plastic_black", seg=40, tseg=6)
    fan.cyl(0.035, 0.03, (sx * 0.14, 0.475, 0.02), Y_, "plastic_black", segs=24, bevel=0.004)
    fan.cyl(0.03, 0.04, (sx * 0.14, 0.455, 0.02), Y_, "painted_black", segs=20)
    for k in range(7):
        a = k * 2 * math.pi / 7
        fan.box((0.11, 0.003, 0.035), (sx * 0.14 + 0.075 * math.cos(a), 0.478, 0.02 + 0.075 * math.sin(a)), "plastic_black", rot=(0.35, -a, 0))
    for k in range(4):
        a = k * math.pi / 2 + math.pi / 4
        fan.box((0.15, 0.006, 0.008), (sx * 0.14 + 0.075 * math.cos(a), 0.462, 0.02 + 0.075 * math.sin(a)), "plastic_black", rot=(0, -a, 0))
for p in [(-0.26, 0.488, 0.185), (0.26, 0.488, 0.185), (-0.26, 0.488, -0.145), (0.26, 0.488, -0.145)]:
    fan.fastener("bolt", p, nY, "M6")

ctank = Part("CoolantExpansionTank", "Wanted", "Translucent coolant reservoir, right rear of the bay. Top up here.", "1 x M6 screw + clip-in base.")
ctank.box((0.12, 0.18, 0.12), (0.42, -0.30, 0.30), "plastic_white", bevel=0.02)
ctank.cyl(0.022, 0.02, (0.42, -0.27, 0.37), Z_, "plastic_white", segs=20)
for z in (0.27, 0.33): ctank.box((0.04, 0.002, 0.002), (0.42, -0.209, z), "plastic_black")   # MIN / MAX marks
ctank.cyl(0.007, 0.04, (0.38, -0.30, 0.235), Z_, "plastic_white", segs=10)
ctank.box((0.02, 0.006, 0.02), (0.42, -0.207, 0.34), "plastic_white")
ctank.fastener("screw", (0.42, -0.205, 0.34), Y_)
ccap = Part("CoolantTankCap", "Wanted", "Screw cap on the coolant reservoir (pressure cap).")
ccap.cyl(0.028, 0.022, (0.42, -0.27, 0.391), Z_, "cap_blue", segs=28, bevel=0.003)
for k in range(12):
    a = k * math.pi / 6; ccap.box((0.004, 0.006, 0.018), (0.42 + 0.028 * math.cos(a), -0.27 + 0.028 * math.sin(a), 0.391), "cap_blue", rot=(0, 0, a))

ofc = Part("OilFillerCap", "Wanted", "Oil filler cap on the cam cover. Top up oil here.")
ofc.cyl(0.03, 0.016, (-0.19, 0.05, 0.407), Z_, "plastic_black", segs=28, bevel=0.003)
ofc.box((0.045, 0.008, 0.012), (-0.19, 0.05, 0.42), "plastic_black", bevel=0.002)
dip = Part("Dipstick", "Wanted", "Oil dipstick with yellow loop handle in the tube on the front of the block.")
dip.torus(0.012, 0.003, (-0.215, 0.16, 0.418), Y_, "cap_yellow", seg=20, tseg=6)
dip.cyl(0.006, 0.015, (-0.215, 0.16, 0.403), Z_, "cap_yellow", segs=10)
dip.tube([(-0.215, 0.16, 0.40), (-0.215, 0.16, 0.33), (-0.215, 0.15, 0.15), (-0.215, 0.118, 0.0), (-0.215, 0.10, -0.15)], 0.0018, "zinc", segs=6, caps=True)

bat = Part("Battery", "Wanted", "60 Ah 12 V battery on its tray above the gearbox.", "Hold-down clamp: 1 x M10 bolt.")
bat.box((0.242, 0.175, 0.19), (-0.56, -0.20, 0.255), "plastic_black", bevel=0.012)
bat.box((0.26, 0.19, 0.012), (-0.56, -0.20, 0.154), "painted_black", bevel=0.002)
bat.box((0.242, 0.03, 0.004), (-0.56, -0.20, 0.352), "plastic_black")
bat.box((0.05, 0.02, 0.015), (-0.56, -0.103, 0.17), "painted_black", bevel=0.002)
bat.fastener("bolt", (-0.56, -0.103, 0.178), Z_, "M8", "hold-down clamp")
bat.point("Battery_PositivePost", (-0.65, -0.15, 0.37), Z_); bat.point("Battery_NegativePost", (-0.47, -0.15, 0.37), Z_)
for nm, x, cmat in (("BatteryTerminal_Positive", -0.65, "red"), ("BatteryTerminal_Negative", -0.47, "painted_black")):
    tp = Part(nm, "Wanted", "Battery terminal clamp with cable stub. Simulated: corrosion / cleaning.", "1 x M8 clamp nut.")
    tp.cyl(0.011, 0.018, (x, -0.15, 0.359), Z_, "zinc", segs=16)
    tp.box((0.035, 0.025, 0.012), (x, -0.13, 0.358), "zinc", bevel=0.002)
    tp.tube([(x, -0.117, 0.358), (x, -0.104, 0.345), (x + (0.02 if x > -0.5 else -0.02), -0.100, 0.27), (x + (0.03 if x > -0.5 else -0.03), -0.100, 0.20)], 0.008, cmat, segs=10)
    tp.fastener("nut", (x + 0.012, -0.13, 0.358), X_, "M6")

gbx = Part("Gearbox_02J", "Wanted", "02J 5-speed manual transaxle. Simulated: loses gears.",
           "8 engine-to-gearbox bolts around the bellhousing; the starter bolts also hold the joint.")
gbx.cyl(0.17, 0.11, (-0.284, -0.01, 0.02), X_, "cast_alu", segs=40, r2=0.20)
gbx.box((0.30, 0.26, 0.26), (-0.47, -0.03, 0.01), "cast_alu", bevel=0.03)
gbx.cyl(0.10, 0.39, (-0.435, -0.175, -0.09), X_, "cast_alu", segs=32, bevel=0.01)
gbx.cyl(0.032, 0.025, (-0.2275, -0.175, -0.09), X_, "cast_alu", segs=20)
gbx.cyl(0.10, 0.04, (-0.64, 0.0, 0.03), X_, "cast_alu", segs=32, bevel=0.006)
gbx.box((0.08, 0.08, 0.06), (-0.44, -0.02, 0.17), "cast_alu", bevel=0.01)
for k in range(6): gbx.box((0.012, 0.012, 0.22), (-0.36 - k * 0.05, 0.103, 0.0), "cast_alu")
gbx.cyl(0.012, 0.012, (-0.45, 0.04, 0.205), Z_, "zinc", segs=12)              # selector shafts
gbx.cyl(0.012, 0.012, (-0.43, -0.06, 0.205), Z_, "zinc", segs=12)
gbx.cyl(0.012, 0.02, (-0.50, -0.25, -0.17), Z_, "zinc", segs=6)              # fill plug
for a in range(20, 340, 40):          # 8 bolts; the starter's 2 bolts take the 9th position
    r = math.radians(a)
    gbx.cyl(0.011, 0.012, (-0.236, -0.01 + 0.207 * math.cos(r), 0.02 + 0.207 * math.sin(r)), X_, "cast_alu", segs=12)  # bolt bosses
    gbx.fastener("bolt", (-0.242, -0.01 + 0.207 * math.cos(r), 0.02 + 0.207 * math.sin(r)), nX, "M12", "engine to gearbox")
for nm, xc, ax in (("DriveshaftFlange_Left", -0.64, nX), ("DriveshaftFlange_Right", -0.205, X_)):
    fl = Part(nm, "Wanted", "Driveshaft output flange (100 mm CV). Bolt points for 6 x M8 12-point CV bolts.", "6 x M8 triple-square CV bolts.")
    fl.cyl(0.05, 0.02, (xc, -0.175, -0.09), ax, "alu_machined", segs=36, bevel=0.002)
    fl.cyl(0.02, 0.012, (xc + 0.016 * ax[0], -0.175, -0.09), ax, "zinc", segs=16)
    for k in range(6):
        a = k * math.pi / 3
        fl.fastener("bolt", (xc + 0.011 * ax[0], -0.175 + 0.039 * math.cos(a), -0.09 + 0.039 * math.sin(a)), ax, "M8", tool="triple-square M8 (XZN)")
    fl.parent_name = "Gearbox_02J"

fp_pts = [(0.105, -0.22, 0.095), (0.14, -0.225, 0.08), (0.155, -0.245, 0.02), (0.15, -0.27, -0.10), (0.12, -0.30, -0.22), (0.08, -0.40, -0.27), (0.06, -0.56, -0.28)]
fpipe = Part("ExhaustFrontPipe", "Wanted", "Downpipe from the turbine outlet with a flex section. Simulated: hangs and drags.",
             "4 x M8 self-locking nuts to the turbo (Pelican Parts Golf IV 1.8T); 2 x M10 bolts to the cat.")
fpipe.cyl(0.046, 0.01, (0.102, -0.22, 0.095), X_, "exhaust", segs=28, bevel=0.002)
fpipe.tube(fp_pts, 0.028, "exhaust", segs=16, per=12, caps=False)
for k in range(9): fpipe.torus(0.031, 0.003, (0.073 - k * 0.0015, -0.44 - k * 0.012, -0.275), (-0.12, -1, 0), "exhaust", seg=20, tseg=4)
fpipe.cyl(0.05, 0.01, (0.06, -0.565, -0.28), Y_, "exhaust", segs=28)
fpipe.cyl(0.008, 0.03, (0.155, -0.258, -0.02), (0, -1, 0.4), "zinc", segs=8)       # lambda probe boss
for k in range(4):
    a = math.pi / 4 + k * math.pi / 2
    fpipe.fastener("nut", (0.108, -0.22 + 0.034 * math.cos(a), 0.095 + 0.034 * math.sin(a)), X_, "M8")
fpipe.pivot = tuple(fpipe.fast[0]["position"])
for dx in (-0.036, 0.036): fpipe.fastener("bolt", (0.06 + dx, -0.571, -0.28), nY, "M10")
fpipe.point("ExhaustFrontPipe_DragPoint", (0.06, -0.50, -0.31), nZ)

cat = Part("CatalyticConverter", "Wanted", "Underfloor catalytic converter with intermediate pipe.", "Flange at front (bolts on front pipe), clamp at rear.")
cat.pivot = (0.06, -0.577, -0.28)
cat.cyl(0.05, 0.01, (0.06, -0.577, -0.28), Y_, "exhaust", segs=28)
cat.cyl(0.03, 0.06, (0.06, -0.61, -0.28), Y_, "exhaust", segs=24, r2=0.068)
cat.cyl(0.068, 0.28, (0.06, -0.78, -0.28), Y_, "exhaust", segs=28, bevel=0.008)
cat.cyl(0.068, 0.06, (0.06, -0.95, -0.28), Y_, "exhaust", segs=24, r2=0.028)
cat.box((0.16, 0.30, 0.004), (0.06, -0.78, -0.205), "exhaust")              # heat shield
cat.tube([(0.06, -0.98, -0.28), (0.07, -1.05, -0.28), (0.15, -1.15, -0.28), (0.25, -1.25, -0.275), (0.30, -1.32, -0.27)], 0.026, "exhaust", segs=14, caps=False)
cat.fastener("clamp", (0.30, -1.32, -0.27), (0.4, -1, 0), hose_r=0.027, xdir=(0, 0, -1))

csil = Part("CentreSilencer", "Wanted", "Middle silencer and pipe up to the rear joint.", "Clamp at rear joint, 2 rubber hangers.")
csil.pivot = (0.30, -1.32, -0.27)
csil.tube([(0.30, -1.32, -0.27), (0.31, -1.40, -0.27), (0.31, -1.45, -0.27)], 0.026, "exhaust", segs=14, caps=False)
csil.cyl(0.075, 0.38, (0.31, -1.64, -0.27), Y_, "exhaust", segs=28, bevel=0.01)
csil.tube([(0.31, -1.83, -0.27), (0.31, -2.0, -0.27), (0.30, -2.30, -0.26), (0.30, -2.45, -0.25)], 0.024, "exhaust", segs=14, caps=False)
for y in (-1.50, -2.20):
    csil.cyl(0.012, 0.03, (0.36 if y > -2 else 0.35, y, -0.24), X_, "rubber", segs=12)
    csil.box((0.004, 0.012, 0.04), (0.34, y, -0.25), "zinc")
csil.fastener("clamp", (0.30, -2.45, -0.25), (0, -1, 0.05), hose_r=0.025, xdir=(0, 0, -1))

rsil = Part("RearSilencer", "Wanted", "Pipe over the rear beam into the transverse rear silencer; tailpipe left.", "2 rubber hangers.")
rsil.pivot = (0.30, -2.45, -0.25)
rsil.tube([(0.30, -2.45, -0.25), (0.30, -2.60, -0.19), (0.30, -2.75, -0.19), (0.28, -2.88, -0.24), (0.22, -2.95, -0.26)], 0.024, "exhaust", segs=14, caps=False)
rsil.box((0.55, 0.20, 0.13), (-0.04, -3.05, -0.26), "exhaust", bevel=0.04)
rsil.tube([(-0.28, -3.12, -0.27), (-0.40, -3.22, -0.28), (-0.42, -3.32, -0.28)], 0.022, "exhaust", segs=14, caps=False)
rsil.cyl(0.032, 0.06, (-0.42, -3.33, -0.28), Y_, "alu_machined", segs=24)
for x in (-0.25, 0.17):
    rsil.cyl(0.012, 0.03, (x, -2.95, -0.18), Z_, "rubber", segs=12)
    rsil.point(f"RearSilencer_Hanger", (x, -2.95, -0.165), Z_)
rsil.point("RearSilencer_DragPoint", (-0.04, -3.05, -0.33), nZ)

tank = Part("FuelTank", "Wanted", "55 l plastic fuel tank under the rear seat. Simulated: holed and leaks.", "2 steel straps, 4 x M10 bolts.")
tank.box((0.68, 0.50, 0.20), (-0.16, -2.20, -0.19), "plastic_black", bevel=0.03)
tank.box((0.30, 0.25, 0.04), (-0.10, -2.20, -0.07), "plastic_black", bevel=0.01)
tank.cyl(0.06, 0.02, (-0.10, -2.20, -0.04), Z_, "plastic_black", segs=24)    # sender flange
tank.tube([(0.10, -2.40, -0.12), (0.30, -2.55, 0.0), (0.50, -2.70, 0.18), (0.62, -2.76, 0.26)], 0.02, "plastic_black", segs=12)
for y in (-2.06, -2.34):
    tank.box((0.72, 0.03, 0.003), (-0.16, y, -0.292), "painted_black")
    for x in (-0.51, 0.19): tank.fastener("bolt", (x, y, -0.296), nZ, "M10")
tank.point("FuelTank_LeakPoint", (-0.16, -2.20, -0.29), nZ)

# ---------------------------------------------------------------- NICE LATER
tb = Part("ThrottleBody", "NiceLater", "Electronic throttle body (drive-by-wire) on the left end of the plenum.", "4 x M6 bolts.")
tb.cyl(0.034, 0.06, (-0.272, 0.255, 0.19), X_, "alu_machined", segs=28, bevel=0.003)
tb.box((0.05, 0.04, 0.06), (-0.27, 0.295, 0.19), "plastic_black", bevel=0.006)
tb.cyl(0.038, 0.008, (-0.302, 0.255, 0.19), X_, "alu_machined", segs=28)
for k in range(4):
    a = math.pi / 4 + k * math.pi / 2
    tb.fastener("bolt", (-0.244, 0.255 + 0.044 * math.cos(a), 0.19 + 0.044 * math.sin(a)), X_, "M6")

for i, cx in enumerate(CX, 1):
    cp = Part(f"CoilPack_{i}", "NiceLater", "Coil-on-plug ignition coil (pulls straight out).")
    cp.cyl(0.012, 0.07, (cx, 0, 0.36), Z_, "plastic_black", segs=14)
    cp.box((0.035, 0.07, 0.022), (cx, 0.008, 0.404), "plastic_black", bevel=0.004)
    cp.box((0.018, 0.02, 0.016), (cx, 0.05, 0.404), "plastic_black", bevel=0.002)
    sp = Part(f"SparkPlug_{i}", "NiceLater", "Spark plug, 16 mm hex.")
    sp.cyl(0.0045, 0.035, (cx, 0, 0.315), Z_, "plastic_white", segs=12)
    sp.cyl(0.0092, 0.008, (cx, 0, 0.293), Z_, "zinc", segs=6)
    sp.cyl(0.007, 0.018, (cx, 0, 0.28), Z_, "zinc", segs=12)

frail = Part("FuelRail", "NiceLater", "Fuel rail with 4 injectors into the intake runners, fuel pressure regulator on the end.", "2 x M6 bolts to the manifold.")
frail.cyl(0.011, 0.36, (0, 0.18, 0.352), X_, "alu_machined", segs=16, bevel=0.002)
frail.cyl(0.013, 0.035, (0.19, 0.18, 0.352), X_, "zinc", segs=16)
for cx in CX:
    frail.tube([(cx, 0.18, 0.345), (cx, 0.172, 0.335), (cx, 0.165, 0.328)], 0.007, "green", segs=10)
for x in (-0.088, 0.088):
    frail.box((0.02, 0.02, 0.008), (x, 0.195, 0.345), "alu_machined")
    frail.box((0.012, 0.012, 0.03), (x, 0.195, 0.326), "alu_machined")
    frail.fastener("bolt", (x, 0.198, 0.349), Z_, "M6")

wp = Part("WaterPump", "NiceLater", "Timing-belt-driven water pump on the block, behind the lower cover.", "3 x M8 bolts.")
wp.cyl(0.042, 0.01, (0.230, 0.04, 0.08), X_, "cast_alu", segs=28)
wp.cyl(0.035, 0.01, (0.240, 0.04, 0.08), X_, "alu_machined", segs=32)
for k in range(3):
    a = math.radians(30 + k * 120)
    wp.fastener("bolt", (0.235, 0.04 + 0.048 * math.cos(a), 0.08 + 0.048 * math.sin(a)), X_, "M8")

th = Part("ThermostatHousing", "NiceLater", "Thermostat housing on the front of the block, lower hose outlet.", "2 x M6 bolts.")
th.box((0.06, 0.03, 0.05), (-0.02, 0.1325, -0.035), "cast_alu", bevel=0.005)
th.cyl(0.017, 0.035, (-0.02, 0.165, -0.035), Y_, "cast_alu", segs=16)
for dx in (-0.024, 0.024): th.fastener("bolt", (-0.02 + dx, 0.1485, -0.035), Y_, "M6")

up_pts = [(-0.2795, 0.03, 0.29), (-0.305, 0.03, 0.29), (-0.325, 0.09, 0.265), (-0.325, 0.25, 0.245), (-0.31, 0.42, 0.21), (-0.305, 0.445, 0.20), (-0.305, 0.475, 0.20)]
rhu = Part("RadiatorHose_Upper", "NiceLater", "Upper radiator hose from the head coolant flange.", "2 hose clamps.")
rhu.tube(up_pts, 0.018, "rubber", segs=14, per=12, caps=False)
rhu.pivot = (-0.2795, 0.03, 0.29)
rhu.fastener("clamp", (-0.288, 0.03, 0.29), nX, hose_r=0.018); rhu.fastener("clamp", (-0.305, 0.47, 0.20), Y_, hose_r=0.018)
lo_pts = [(-0.02, 0.1825, -0.035), (-0.02, 0.21, -0.035), (0.05, 0.27, -0.08), (0.20, 0.40, -0.15), (0.305, 0.445, -0.16), (0.305, 0.475, -0.16)]
rhl = Part("RadiatorHose_Lower", "NiceLater", "Lower radiator hose to the thermostat housing.", "2 hose clamps.")
rhl.tube(lo_pts, 0.018, "rubber", segs=14, per=12, caps=False)
rhl.pivot = (-0.02, 0.1825, -0.035)
rhl.fastener("clamp", (-0.02, 0.19, -0.035), Y_, hose_r=0.018, xdir=(1, 0, 0)); rhl.fastener("clamp", (0.305, 0.47, -0.16), Y_, hose_r=0.018, xdir=(1, 0, 0))

emr = Part("EngineMount_Right", "NiceLater", "Hydraulic engine mount on the timing-belt side.", "3 x M10 bracket-to-engine bolts + 2 x M10 to body.")
emr.box((0.07, 0.05, 0.12), (0.29, -0.13, 0.18), "cast_alu", bevel=0.008)
emr.cyl(0.045, 0.06, (0.33, -0.13, 0.215), Z_, "rubber", segs=24, bevel=0.006)
emr.box((0.12, 0.07, 0.012), (0.33, -0.13, 0.25), "painted_black", bevel=0.002)
for z in (0.14, 0.18, 0.22): emr.fastener("bolt", (0.326, -0.13 + (0.015 if z != 0.18 else -0.015), z), X_, "M10")
for dx in (-0.045, 0.045): emr.fastener("bolt", (0.33 + dx, -0.13, 0.257), Z_, "M10")

eml = Part("EngineMount_Left", "NiceLater", "Gearbox mount on top of the 02J.", "2 x M10 to gearbox + 2 x M10 to body.")
eml.box((0.06, 0.08, 0.04), (-0.58, -0.02, 0.165), "cast_alu", bevel=0.006)
eml.cyl(0.035, 0.04, (-0.58, -0.02, 0.205), Z_, "rubber", segs=20, bevel=0.005)
eml.box((0.10, 0.06, 0.01), (-0.58, -0.02, 0.23), "painted_black")
for dy in (-0.03, 0.03): eml.fastener("bolt", (-0.555, -0.02 + dy, 0.186), Z_, "M10")
for dx in (-0.04, 0.04): eml.fastener("bolt", (-0.58 + dx, -0.02, 0.236), Z_, "M10")

emp = Part("EngineMount_Rear", "NiceLater", "Pendulum (torque) support from the gearbox to the subframe.", "3 x M10 bolts.")
emp.box((0.04, 0.16, 0.03), (-0.30, -0.24, -0.15), "painted_black", rot=(0.3, 0, 0), bevel=0.004)
emp.cyl(0.025, 0.04, (-0.30, -0.31, -0.175), X_, "rubber", segs=16)
for p, ax in [((-0.32, -0.17, -0.12), nX), ((-0.28, -0.17, -0.12), X_), ((-0.322, -0.31, -0.175), nX)]:
    emp.fastener("bolt", p, ax, "M10")

srv = Part("BrakeServo", "NiceLater", "Brake servo with master cylinder and fluid reservoir (left-hand drive).", "2 x M10 master-cylinder nuts (servo studs are inside the cabin).")
srv.cyl(0.115, 0.13, (-0.36, -0.64, 0.30), Y_, "painted_black", segs=40, bevel=0.02)
srv.cyl(0.022, 0.13, (-0.36, -0.51, 0.31), Y_, "cast_alu", segs=20)
srv.box((0.10, 0.012, 0.05), (-0.36, -0.57, 0.31), "cast_alu", bevel=0.003)
srv.box((0.09, 0.05, 0.05), (-0.36, -0.51, 0.36), "plastic_white", bevel=0.008)
srv.cyl(0.018, 0.012, (-0.36, -0.51, 0.39), Z_, "cap_blue", segs=16)
for dx in (-0.04, 0.04): srv.fastener("nut", (-0.36 + dx, -0.564, 0.31), Y_, "M10")

wb = Part("WasherBottle", "NiceLater", "Windscreen washer reservoir, front right.", "2 x M6 bolts.")
wb.box((0.14, 0.16, 0.20), (0.50, 0.38, 0.05), "plastic_white", bevel=0.02)
wb.tube([(0.47, 0.36, 0.15), (0.465, 0.35, 0.25), (0.46, 0.34, 0.30)], 0.014, "plastic_white", segs=12)
wb.cyl(0.02, 0.015, (0.46, 0.34, 0.31), Z_, "cap_blue", segs=16)
for z in (0.10, 0.0): wb.fastener("bolt", (0.43, 0.30, z), nX, "M6")

fb = Part("FuseBox", "NiceLater", "Engine-bay fuse and relay box by the battery.", "1 x M6 screw + clip.")
fb.box((0.12, 0.05, 0.10), (-0.60, -0.42, 0.40), "plastic_black", bevel=0.006)
fb.box((0.124, 0.054, 0.012), (-0.60, -0.42, 0.455), "plastic_black", bevel=0.003)
fb.fastener("screw", (-0.60, -0.393, 0.40), Y_)

# ---------------------------------------------------------------- EXTRAS (needed so the belt and hoses make sense)
ac = Part("ACCompressor", "Extras", "A/C compressor on the accessory bracket.", "4 x M8 bolts (approximate).")
ac.cyl(0.055, 0.15, (0.17, 0.21, -0.04), X_, "cast_alu", segs=32, bevel=0.006)
ac.cyl(0.055, 0.022, (0.274, 0.21, -0.04), X_, "alu_machined", segs=32)
ac.cyl(0.03, 0.012, (0.289, 0.21, -0.04), X_, "painted_black", segs=24)
ac.cyl(0.012, 0.02, (0.255, 0.21, -0.04), X_, "zinc", segs=12)
for x in (0.115, 0.225): ac.box((0.025, 0.012, 0.14), (x, 0.154, -0.04), "cast_alu", bevel=0.003)
for x in (0.115, 0.225):
    for z in (0.02, -0.10): ac.fastener("bolt", (x, 0.16, z), Y_, "M8")

ps = Part("PowerSteeringPump", "Extras", "Belt-driven power-steering pump on the accessory bracket.", "3 x M8 bolts (approximate).")
ps.cyl(0.05, 0.042, (0.214, 0.18, 0.245), X_, "cast_alu", segs=28, bevel=0.005)
ps.cyl(0.035, 0.008, (0.189, 0.18, 0.245), X_, "cast_alu", segs=24)
ps.box((0.045, 0.03, 0.012), (0.2125, 0.143, 0.186), "cast_alu", bevel=0.002)
ps.box((0.03, 0.03, 0.05), (0.2125, 0.155, 0.21), "cast_alu", bevel=0.003)
ps.cyl(0.06, 0.018, (0.274, 0.18, 0.245), X_, "alu_machined", segs=32)
ps.cyl(0.01, 0.045, (0.25, 0.18, 0.245), X_, "zinc", segs=10)
for k in range(3):
    a = math.radians(90 + k * 120)
    ps.fastener("bolt", (0.236, 0.18 + 0.038 * math.cos(a), 0.245 + 0.038 * math.sin(a)), X_, "M8")

of = Part("OilFilter", "Extras", "Spin-on oil filter on the front of the block.")
of.cyl(0.032, 0.006, (-0.10, 0.130, -0.005), Y_, "rubber", segs=24)
of.cyl(0.038, 0.09, (-0.10, 0.178, -0.005), Y_, "cap_blue", segs=28, bevel=0.006)

# ================================================================== FINALIZE
parent_map = {}
for p in PARTS:
    ob = finalize(p, COLL[p.group])
    parent_map[p.name] = ob
bpy.context.view_layer.update()
for p in PARTS:
    pn = getattr(p, "parent_name", None)
    if pn:
        par = parent_map[pn]; w = p.obj.matrix_world.copy()
        p.obj.parent = par; p.obj.matrix_parent_inverse = par.matrix_world.inverted(); p.obj.matrix_world = w

for f in FASTENERS:
    par = f.parent_part
    finalize(f, COLL[par.group], parent=par.obj)
    f.obj["tool"] = f.tool
for p in PARTS:
    for i, fd in enumerate(p.fast, 1):
        bolt_point(f"{p.name}_BoltPoint_{i:02d}", p.obj, fd["position"], fd["axis"], COLL[p.group])
    for k, (nm, pos, ax) in enumerate(p.extra_points, 1):
        bolt_point(nm if nm.count("_") >= 1 and not nm.endswith(("Grommet", "Hanger")) else f"{nm}_{k:02d}", p.obj, pos, ax, COLL[p.group])

# ---------------------------------------------------------------- part-level tools for hand-removed parts
PART_TOOLS = {"CoilPack": "hands (pull up)", "SparkPlug": "16 mm spark-plug socket + extension", "OilFilter": "oil-filter wrench (76 mm cup)",
              "OilFillerCap": "hands", "CoolantTankCap": "hands (only when cold)", "Dipstick": "hands",
              "AuxiliaryBelt": "16 mm spanner on the belt tensioner"}
for p in PARTS:
    for k, v in PART_TOOLS.items():
        if p.name.startswith(k): p.props["tool"] = v

# ---------------------------------------------------------------- convex collision hulls (UCX_<Part>_NN) for bent parts
for p in PARTS:
    p.colliders = 0
    for i, pts in enumerate(p.hulls, 1):
        bmc = bmesh.new(); vs = [bmc.verts.new(v - p.obj.location) for v in pts]
        bmesh.ops.convex_hull(bmc, input=vs)
        loose = [v for v in bmc.verts if not v.link_faces]
        if loose: bmesh.ops.delete(bmc, geom=loose, context='VERTS')
        if len(bmc.faces) < 4: bmc.free(); continue
        me = bpy.data.meshes.new(f"UCX_{p.name}_{i:02d}"); bmc.to_mesh(me); bmc.free()
        ob = bpy.data.objects.new(me.name, me); COLL[p.group].objects.link(ob)
        ob.parent = p.obj; ob.location = (0, 0, 0); ob.display_type = 'WIRE'; ob.hide_render = True
        ob["collider"] = True
        p.colliders += 1

# ---------------------------------------------------------------- grime / oil vertex colour: dark where parts meet, round fasteners, under the engine
from mathutils.bvhtree import BVHTree
bpy.context.view_layer.update()
WB, BB = {}, {}
for p in PARTS:
    M = p.obj.matrix_world; me = p.obj.data
    vw = [M @ v.co for v in me.vertices]
    WB[p.name] = BVHTree.FromPolygons(vw, [tuple(pl.vertices) for pl in me.polygons])
    BB[p.name] = (Vector((min(v.x for v in vw), min(v.y for v in vw), min(v.z for v in vw))), Vector((max(v.x for v in vw), max(v.y for v in vw), max(v.z for v in vw))))
GRIME = (0.24, 0.20, 0.15)
def set_grime(me, gvals):
    att = me.color_attributes.get("Grime") or me.color_attributes.new("Grime", 'BYTE_COLOR', 'POINT')
    for i, g in enumerate(gvals):
        att.data[i].color = (1 - (1 - GRIME[0]) * g, 1 - (1 - GRIME[1]) * g, 1 - (1 - GRIME[2]) * g, 1.0)
    me.color_attributes.active_color = att
    try: me.color_attributes.render_color_index = me.color_attributes.find("Grime")
    except Exception: pass
R_JOINT = 0.04
for p in PARTS:
    lo, hi = BB[p.name]
    others = [q for q in PARTS if q is not p and all(BB[q.name][0][i] - R_JOINT <= hi[i] and BB[q.name][1][i] + R_JOINT >= lo[i] for i in range(3))]
    fpos = [Vector(f["position"]) for f in p.fast]
    engine_zone = abs(p.bbc.x) < 0.45 and abs(p.bbc.y) < 0.35
    M = p.obj.matrix_world; vals = []
    cbm = bmesh.new(); cbm.from_mesh(p.obj.data); cbm.verts.ensure_lookup_table()
    concave = [0.0] * len(cbm.verts)
    for e in cbm.edges:
        if len(e.link_faces) == 2:
            a = e.calc_face_angle_signed(0.0)
            if a < -0.35:                       # inside corner: dirt collects
                for vv in e.verts: concave[vv.index] = max(concave[vv.index], min(1.0, -a / 1.4))
    cbm.free()
    for vi, v in enumerate(p.obj.data.vertices):
        w = M @ v.co; d = 1.0
        for q in others:
            hit = WB[q.name].find_nearest(w, R_JOINT)
            if hit[0] is not None: d = min(d, hit[3])
        for fp in fpos: d = min(d, max(0.0, (w - fp).length - 0.008))
        g = max(0.0, 1 - d / R_JOINT) ** 1.3 * 0.95
        g = max(g, concave[vi] * 0.6)
        if engine_zone: g = max(g, min(1.0, max(0.0, (0.02 - w.z) / 0.25)) * 0.55)   # oil mist / road dirt underneath
        vals.append(min(1.0, g))
    set_grime(p.obj.data, vals)
for f in FASTENERS:
    set_grime(f.obj.data, [0.3] * len(f.obj.data.vertices))

# custom properties (exported to FBX user props and glTF extras)
for p in PARTS:
    o = p.obj
    o["group"] = p.group
    o["description"] = p.note
    o["fastener_count"] = len(p.fast)
    o["fastener_note"] = p.fnote
    o["triangles"] = p.tris
    o["colliders"] = p.colliders
    o["pivot"] = "mounting bolt / joint" if p.pivot is not None else "bounding-box centre"
    for k, v in p.props.items(): o[k] = v

bpy.context.view_layer.update()

# ================================================================== MANIFEST
man = []
for p in PARTS:
    man.append(dict(name=p.name, group=p.group, description=p.note, fastener_note=p.fnote,
                    triangles=p.tris, origin_m=[round(x, 4) for x in p.center],
                    pivot=("mounting bolt / joint" if p.pivot is not None else "bounding-box centre"), colliders=p.colliders,
                    size_m=[round(x, 4) for x in (p.bbox[1] - p.bbox[0])],
                    fasteners=p.fast, extra_points=[dict(name=n, position=[round(x, 4) for x in a], axis=[round(x, 4) for x in b]) for n, a, b in p.extra_points],
                    **({"custom": p.props} if p.props else {})))
with open(os.path.join(OUT, "parts_manifest.json"), "w") as fh:
    json.dump(dict(model="Skoda Octavia I (1U) 1.8T 20V engine bay", units="metres",
                   frame="Blender Z-up; +X timing-belt end (car right), +Y front of car, origin on crank axis",
                   parts=man), fh, indent=1)
import csv
with open(os.path.join(OUT, "fasteners.csv"), "w", newline="") as fh:
    w = csv.writer(fh); w.writerow(["part", "group", "fastener", "kind", "size", "tool", "label", "x", "y", "z", "axis_x", "axis_y", "axis_z"])
    for p in PARTS:
        for fd in p.fast:
            w.writerow([p.name, p.group, fd["name"], fd["kind"], fd["size"], fd["tool"], fd["label"], *fd["position"], *fd["axis"]])
        if "tool" in p.props:
            w.writerow([p.name, p.group, p.name, "part (no fastener)", "", p.props["tool"], "", *[round(x, 4) for x in p.center], 0, 0, 1])

print("PARTS", len(PARTS), "FASTENERS", len(FASTENERS), "COLLIDERS", sum(p.colliders for p in PARTS))
print("MAX_TRIS", max((p.tris, p.name) for p in PARTS))
print("TOTAL_TRIS", sum(p.tris for p in PARTS) + sum(f.tris for f in FASTENERS))

# ================================================================== SAVE / EXPORT
bpy.ops.file.make_paths_relative() if False else None
blend_path = os.path.join(OUT, "Octavia1U_1.8T_20V.blend")
bpy.ops.wm.save_as_mainfile(filepath=blend_path, relative_remap=True, compress=True)

bpy.ops.export_scene.fbx(filepath=os.path.join(OUT, "Octavia1U_1.8T_20V.fbx"), use_selection=False,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE',
                         path_mode='RELATIVE', add_leaf_bones=False, bake_anim=False, colors_type='SRGB')
print("EXPORTED")
