"""Shared part-building framework for the Octavia 1U game sets (suspension edition).

Geometry is built in world coordinates (metres, Blender Z-up) with bmesh primitives that carry their
own UVs (boxes: local box projection; cylinders, pipes, revolves: unwrapped along their length with
whole texture repeats around). Parts are finalised into objects whose origin and orientation follow the
game rules (wheel centre, body-side pivot, top mount ...).
"""
import bpy, bmesh, math, os
from mathutils import Vector, Matrix, Euler

TEX = None          # set by the build script
COLL = {}
MATS = {}
PARTS = []
FASTENERS = []
ALLOWED_TOOLS = ["10 mm socket", "13 mm socket", "17 mm socket", "19 mm socket",
                 "flat screwdriver", "Phillips screwdriver", "hands"]

def V(*a): return Vector(a if len(a) == 3 else a[0])
def mirror_v(v): return Vector((-v[0], v[1], v[2]))

def align_z(d):
    d = Vector(d).normalized()
    return Vector((0, 0, 1)).rotation_difference(d).to_matrix().to_4x4()

def align_zx(z, xhint):
    z = Vector(z).normalized(); x = Vector(xhint); x = x - z * x.dot(z)
    if x.length < 1e-6:
        x = Vector((1, 0, 0)) if abs(z.x) < 0.9 else Vector((0, 1, 0)); x = x - z * x.dot(z)
    x.normalize(); y = z.cross(x)
    return Matrix((x, y, z)).transposed().to_4x4()

def basis_from(xdir, up=(0, 0, 1)):
    """3x3 with local X along xdir and local Z as close to `up` as possible."""
    x = Vector(xdir).normalized(); z = Vector(up); z = z - x * x.dot(z)
    if z.length < 1e-6: z = Vector((0, 1, 0)) - x * x.y
    z.normalize(); y = z.cross(x)
    return Matrix((x, y, z)).transposed()

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
    T = []; n = len(path)
    for i in range(n):
        a = path[(i - 1) % n] if (closed or i > 0) else path[i]
        b = path[(i + 1) % n] if (closed or i < n - 1) else path[i]
        T.append((b - a).normalized())
    up = Vector((0, 0, 1)) if abs(T[0].z) < 0.9 else Vector((1, 0, 0))
    Ns = [T[0].cross(up).normalized()]
    for i in range(1, n):
        q = T[i - 1].rotation_difference(T[i]); Ns.append((q @ Ns[-1]).normalized())
    return T, Ns

# ------------------------------------------------------------------ materials
def material(name):
    if name in MATS: return MATS[name]
    m = bpy.data.materials.new(name)
    try: m.use_nodes = True
    except Exception: pass
    nt = m.node_tree
    bsdf = next(n for n in nt.nodes if n.type == 'BSDF_PRINCIPLED')
    def img(kind, noncolor, loc):
        im = bpy.data.images.load(os.path.join(TEX, f"{name}_{kind}.png"), check_existing=True)
        if noncolor: im.colorspace_settings.name = 'Non-Color'
        n = nt.nodes.new('ShaderNodeTexImage'); n.image = im; n.location = loc; return n
    b = img("basecolor", False, (-600, 300))
    ca = nt.nodes.new('ShaderNodeVertexColor'); ca.layer_name = "Grime"; ca.location = (-600, 520)
    mix = nt.nodes.new('ShaderNodeMix'); mix.data_type = 'RGBA'; mix.blend_type = 'MULTIPLY'; mix.location = (-300, 400)
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

# ------------------------------------------------------------------ tools
def pick_tool(real):
    """Nearest allowed tool for a real tool description."""
    r = real.lower()
    if real in ALLOWED_TOOLS: return real
    if "hand" in r: return "hands"
    if "flat" in r: return "flat screwdriver"
    if "phillips" in r or "torx" in r or "pozi" in r: return "Phillips screwdriver"
    if "triple" in r or "hex key" in r or "allen" in r: return "10 mm socket"
    import re
    m = re.search(r"(\d+)\s*mm", r)
    if m:
        n = int(m.group(1)); return f"{min([10, 13, 17, 19], key=lambda s: (abs(s - n), -s))} mm socket"
    return "17 mm socket"

# ------------------------------------------------------------------ Part
class Part:
    def __init__(self, name, group, note="", fnote="", is_fastener=False, side="", stage=9):
        self.name, self.group, self.note, self.fnote, self.side, self.stage = name, group, note, fnote, side, stage
        self.bm = bmesh.new(); self.mats = []; self.fast = []; self.fparts = []
        self.is_fastener = is_fastener; self.obj = None; self.props = {}
        self.origin = None; self.orient_axis = None; self.moving_end = None; self.bottom_point = None
        self.spin_axis = None; self.hulls = []; self.collide = False; self.hull_chunk = 0.06
        self.single_hull = False; self.points = []; self.tool = ""; self.real_tool = ""
        self._skip_hull = False
        self.analytic = []          # (centre, axis, profile) of boolean-cut revolves: their shading normals are set exactly
        if not is_fastener: PARTS.append(self)

    @property
    def TILE(self): return 0.05 if self.is_fastener else 0.25

    def mi(self, m):
        if m not in self.mats: self.mats.append(m)
        return self.mats.index(m)

    # ---- UVs
    def _uv(self, t): return t.loops.layers.uv.get("UVMap") or t.loops.layers.uv.new("UVMap")
    def _uv_box(self, t, off):
        uv = self._uv(t); T = self.TILE; t.normal_update()
        for f in t.faces:
            n = f.normal; ax = max(range(3), key=lambda i: abs(n[i]))
            for l in f.loops:
                co = l.vert.co + off
                u, v = ((co.y, co.z) if ax == 0 else (co.x, co.z) if ax == 1 else (co.x, co.y))
                l[uv].uv = (u / T, v / T)
    def _ntiles(self, r): return max(1, round(2 * math.pi * r / self.TILE))
    def _uv_cyl(self, t, r, off):
        uv = self._uv(t); T = self.TILE; n = self._ntiles(r); t.normal_update()
        for f in t.faces:
            if abs(f.normal.z) > 0.7:
                for l in f.loops: l[uv].uv = ((l.vert.co.x + off.x) / T, (l.vert.co.y + off.y) / T)
                continue
            us = [math.atan2(l.vert.co.y, l.vert.co.x) / (2 * math.pi) for l in f.loops]
            if max(us) - min(us) > 0.5: us = [u + 1 if u < 0 else u for u in us]
            for l, u in zip(f.loops, us): l[uv].uv = (u * n, (l.vert.co.z + off.z) / T)

    def _merge(self, t, m, M):
        idx = self.mi(m)
        for f in t.faces: f.material_index = idx
        if t.loops.layers.uv.get("UVMap") is None: self._uv_box(t, Vector(M.translation))
        bmesh.ops.transform(t, matrix=M, verts=t.verts)
        if self.collide and not self._skip_hull:
            pts = [v.co.copy() for v in t.verts]
            lo = Vector([min(p[i] for p in pts) for i in range(3)]); hi = Vector([max(p[i] for p in pts) for i in range(3)])
            if (hi - lo).length > 0.03: self.hulls.append(pts)
        me = bpy.data.meshes.new("tmp"); t.to_mesh(me); t.free()
        self.bm.from_mesh(me); bpy.data.meshes.remove(me)

    # ---- primitives
    def box(self, size, c, m, rot=(0, 0, 0), bevel=0.0, seg=2, basis=None):
        t = bmesh.new(); bmesh.ops.create_cube(t, size=1.0)
        for v in t.verts: v.co = Vector((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
        if bevel >= 0.003:
            bmesh.ops.bevel(t, geom=list(t.edges), offset=min(bevel, min(size) * 0.45), offset_type='OFFSET',
                            segments=2, profile=0.5, affect='EDGES', clamp_overlap=True)
        self._uv_box(t, Vector(c))
        R = (basis.to_4x4() if basis is not None else Euler(rot).to_matrix().to_4x4())
        self._merge(t, m, Matrix.Translation(Vector(c)) @ R)

    def beam(self, p0, p1, w, h, m, up=(0, 0, 1), bevel=0.004, ext=0.0):
        """Box from p0 to p1 (local X along the segment), width w, height h."""
        p0, p1 = Vector(p0), Vector(p1); d = p1 - p0; L = d.length + 2 * ext
        self.box((L, w, h), (p0 + p1) / 2, m, basis=basis_from(d, up), bevel=bevel)

    def cyl(self, r, depth, c, axis, m, segs=24, r2=None, bevel=0.0, caps=True):
        t = bmesh.new()
        bmesh.ops.create_cone(t, cap_ends=caps, cap_tris=False, segments=segs, radius1=r,
                              radius2=r if r2 is None else r2, depth=depth)
        if bevel >= 0.003 and caps:
            rim = [e for e in t.edges if abs(e.verts[0].co.z - e.verts[1].co.z) < 1e-7
                   and abs(abs(e.verts[0].co.z) - depth / 2) < 1e-6 and len(e.link_faces) == 2]
            bmesh.ops.bevel(t, geom=rim, offset=min(bevel, depth * 0.45), offset_type='OFFSET', segments=2,
                            profile=0.5, affect='EDGES', clamp_overlap=True)
        self._uv_cyl(t, max(r, r2 or 0), Vector(c))
        self._merge(t, m, Matrix.Translation(Vector(c)) @ align_z(axis))

    def cyl_between(self, p0, p1, r, m, segs=20, r2=None, bevel=0.0, caps=True):
        p0, p1 = Vector(p0), Vector(p1)
        self.cyl(r, (p1 - p0).length, (p0 + p1) / 2, p1 - p0, m, segs=segs, r2=r2, bevel=bevel, caps=caps)

    def sphere(self, r, c, m, seg=16, ring=10):
        t = bmesh.new(); bmesh.ops.create_uvsphere(t, u_segments=seg, v_segments=ring, radius=r)
        self._uv_cyl(t, r, Vector(c)); self._merge(t, m, Matrix.Translation(Vector(c)))

    def _revolve_bm(self, prof, segs, closed, u_reps=None, v_norm=False):
        t = bmesh.new(); uv = self._uv(t)
        rings = []
        for i in range(segs):
            th = 2 * math.pi * i / segs; ct, st = math.cos(th), math.sin(th)
            rings.append([t.verts.new((r * ct, r * st, a)) for (r, a) in prof])
        k = len(prof); n = u_reps or self._ntiles(max(r for r, a in prof))
        s = [0.0]
        for j in range(1, k + (1 if closed else 0)):
            (r0, a0), (r1, a1) = prof[j - 1], prof[j % k]; s.append(s[-1] + math.hypot(r1 - r0, a1 - a0))
        vs = (1.0 / s[-1]) if v_norm else (1.0 / self.TILE)
        for i in range(segs):
            A, B = rings[i], rings[(i + 1) % segs]
            for j in range(k if closed else k - 1):
                jj = (j + 1) % k
                try: f = t.faces.new((A[j], B[j], B[jj], A[jj]))
                except ValueError: continue
                u0, u1 = i / segs * n, (i + 1) / segs * n; v0, v1 = s[j] * vs, s[j + 1] * vs
                for l, uvv in zip(f.loops, ((u0, v0), (u1, v0), (u1, v1), (u0, v1))): l[uv].uv = uvv
        bmesh.ops.remove_doubles(t, verts=t.verts, dist=1e-7)
        bmesh.ops.recalc_face_normals(t, faces=t.faces)
        return t

    def revolve(self, prof, c, axis, m, segs=48, closed=True, xhint=(0, 1, 0), u_reps=None, v_norm=False):
        """Revolve a profile [(r, a)] about `axis` through c. U runs round (whole repeats), V along the profile.
        u_reps / v_norm: fixed number of U repeats round and V normalised 0..1 along the profile (tyre layout)."""
        c = Vector(c); M = align_zx(axis, xhint)
        t = self._revolve_bm(prof, segs, closed, u_reps, v_norm)
        self._skip_hull = True
        self._merge(t, m, Matrix.Translation(c) @ M)
        self._skip_hull = False
        if self.collide:
            Mw = Matrix.Translation(c) @ M
            pts = [Mw @ Vector((r * math.cos(2 * math.pi * i / 12), r * math.sin(2 * math.pi * i / 12), a)) for i in range(12) for (r, a) in prof]
            self.hulls.append(pts)

    def revolve_cut(self, prof, c, axis, m, cutters, segs=48, xhint=(0, 1, 0)):
        """Closed revolve with holes: `cutters` are closed bmeshes in world space, removed with an exact boolean."""
        c = Vector(c); M = align_zx(axis, xhint)
        t = self._revolve_bm(prof, segs, True)
        bmesh.ops.transform(t, matrix=Matrix.Translation(c) @ M, verts=t.verts)
        sc = bpy.context.scene; tmp = []
        me = bpy.data.meshes.new("cut_base"); t.to_mesh(me); t.free()
        base = bpy.data.objects.new("cut_base", me); sc.collection.objects.link(base); tmp.append(base)
        cc = bpy.data.collections.new("cutters"); sc.collection.children.link(cc)
        for i, cb in enumerate(cutters):
            cm = bpy.data.meshes.new(f"cutter{i}"); cb.to_mesh(cm); cb.free()
            co = bpy.data.objects.new(f"cutter{i}", cm); cc.objects.link(co); tmp.append(co)
        mod = base.modifiers.new("cut", 'BOOLEAN'); mod.operation = 'DIFFERENCE'; mod.operand_type = 'COLLECTION'
        mod.collection = cc; mod.solver = 'EXACT'
        bpy.context.view_layer.update()
        dg = bpy.context.evaluated_depsgraph_get(); ev = base.evaluated_get(dg)
        em = ev.to_mesh(); t = bmesh.new(); t.from_mesh(em); ev.to_mesh_clear()
        for o in tmp:
            mm = o.data; bpy.data.objects.remove(o); bpy.data.meshes.remove(mm)
        bpy.data.collections.remove(cc)
        self.analytic.append((c.copy(), Vector(axis).normalized(), list(prof)))
        self._skip_hull = True
        self._merge(t, m, Matrix.Identity(4))
        self._skip_hull = False

    def tube(self, pts, r, m, segs=16, per=10, caps=True, closed=False, profile=None, path=None):
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
                u0, u1 = j / k * around, (j + 1) / k * around; v0, v1 = s[i] / self.TILE, s[i + 1] / self.TILE
                for l, uvv in zip(f.loops, ((u0, v0), (u1, v0), (u1, v1), (u0, v1))): l[uv].uv = uvv
        if caps and not closed:
            for ring, pr in ((list(reversed(rings[0])), list(reversed(prof))), (rings[-1], prof)):
                f = t.faces.new(ring)
                for l, (pu, pv) in zip(f.loops, pr): l[uv].uv = (pu / self.TILE, pv / self.TILE)
        bmesh.ops.recalc_face_normals(t, faces=t.faces)
        if self.collide and not self.single_hull:
            start, acc = 0, 0.0
            for i in range(1, len(P)):
                acc += (P[i] - P[i - 1]).length
                if acc >= self.hull_chunk or i == len(P) - 1:
                    self.hulls.append([v.co.copy() for ring in rings[start:i + 1] for v in ring]); start, acc = i, 0.0
        elif self.collide:
            self.hulls.append([v.co.copy() for ring in rings for v in ring])
        self._skip_hull = True
        self._merge(t, m, Matrix.Identity(4))
        self._skip_hull = False

    def helix(self, base, axis, radius, wire, length, turns, m, flat_turns=0.75, segs=6, per_turn=16):
        """Coil spring from `base` along `axis` (unit), closed/flat end coils, UV along the wire."""
        base, axis = Vector(base), Vector(axis).normalized()
        M = align_z(axis).to_3x3()
        total = turns + 2 * flat_turns; n = int(total * per_turn)
        pts = []
        for i in range(n + 1):
            tt = i / per_turn                           # turns travelled
            if tt < flat_turns: z = wire * tt / flat_turns
            elif tt > total - flat_turns: z = length - wire * (total - tt) / flat_turns
            else: z = wire + (length - 2 * wire) * (tt - flat_turns) / turns
            a = 2 * math.pi * tt
            pts.append(base + M @ Vector((radius * math.cos(a), radius * math.sin(a), z)))
        self.tube(None, wire, m, segs=segs, path=pts)

    def text(self, body, c, size, m, depth=0.002, rot=(0, 0, 0)):
        pass

    # ---- fasteners
    def fastener(self, kind, pos, axis, size="M10", real="", label="", source="approximate", xdir=(0, 0, 1)):
        """kind: bolt, nut, socket (socket-head/Allen), screw (countersunk), twelve (12-point), wheelbolt, clip."""
        n = len(self.fast) + 1
        nice = {"bolt": "Bolt", "nut": "Nut", "socket": "Bolt", "screw": "Screw", "twelve": "Bolt",
                "wheelbolt": "Bolt", "clip": "Clip", "banjo": "Bolt"}[kind]
        fp = Part(f"{self.name}_{nice}_{n:02d}", self.group, is_fastener=True, side=self.side)
        M = Matrix.Translation(Vector(pos)) @ align_zx(axis, xdir)
        AF = {"M6": (0.010, 0.004), "M8": (0.013, 0.0053), "M10": (0.016, 0.0064), "M12": (0.018, 0.0075),
              "M14": (0.021, 0.0088), "M16": (0.024, 0.010)}
        af, h = AF.get(size, (0.016, 0.0064))
        R = af / math.sqrt(3)
        def put(fn, *a, **k):
            fn(*a, **k)
        t0 = len(fp.mats)
        if kind in ("bolt", "banjo"):
            fp._addcyl(af * 0.62, 0.0012, 0.0006, "zinc", 12, M)
            fp._addcyl(R, h, 0.0012 + h / 2, "zinc", 6, M)
        elif kind == "nut":
            fp._addcyl(af * 0.62, 0.0012, 0.0006, "zinc", 12, M)
            fp._addcyl(R, h * 1.4, 0.0012 + h * 0.7, "zinc", 6, M)
            d = {"M6": .006, "M8": .008, "M10": .010, "M12": .012, "M14": .014, "M16": .016}.get(size, .010)
            fp._addcyl(d / 2, 0.007, 0.0012 + h * 1.4 + 0.0035, "zinc", 8, M)
        elif kind == "socket":
            fp._addcyl(af * 0.48, h * 1.1, h * 0.55, "zinc", 12, M)
            fp._addcyl(af * 0.22, 0.001, h * 1.1 + 0.0002, "painted_black", 6, M)
        elif kind == "twelve":
            fp._addcyl(af * 0.62, 0.0015, 0.00075, "zinc", 12, M)
            fp._addcyl(af * 0.52, h, 0.0015 + h / 2, "zinc", 12, M)
        elif kind == "wheelbolt":
            fp._addcyl(0.0075, 0.006, 0.003, "zinc", 12, M, r2=0.0095)          # 60 deg cone, narrow end at the hub
            fp._addcyl(0.0098, 0.010, 0.006 + 0.005, "zinc", 6, M)  # 17 mm hex
        elif kind == "screw":
            fp._addcyl(0.0055, 0.0025, -0.00125, "zinc", 16, M, r2=0.0035)
            fp._addcyl(0.0012, 0.0006, 0.0002, "painted_black", 4, M)
        elif kind == "clip":
            fp._addbox((0.014, 0.010, 0.006), (0, 0, 0.003), "zinc", M)
        tool = pick_tool(real) if real else "hands"
        real_tool = "" if (real == tool or real.strip() + " socket" == tool) else real
        fp.tool, fp.real_tool = tool, real_tool
        self.fast.append(dict(name=fp.name, kind=kind, size=size, tool=tool, real_tool=real_tool, label=label,
                              source=source, position=[round(x, 4) for x in Vector(pos)],
                              axis=[round(x, 4) for x in Vector(axis).normalized()]))
        fp.parent_part = self
        self.fparts.append(fp); FASTENERS.append(fp)
        return fp

    def _addcyl(self, r, depth, z, m, segs, M, r2=None, bevel=0.0):
        t = bmesh.new()
        bmesh.ops.create_cone(t, cap_ends=True, cap_tris=False, segments=segs, radius1=r,
                              radius2=r if r2 is None else r2, depth=depth)
        if bevel > 0:
            rim = [e for e in t.edges if abs(e.verts[0].co.z - e.verts[1].co.z) < 1e-7
                   and abs(abs(e.verts[0].co.z) - depth / 2) < 1e-6 and len(e.link_faces) == 2]
            bmesh.ops.bevel(t, geom=rim, offset=min(bevel, depth * 0.45), offset_type='OFFSET', segments=2,
                            profile=0.5, affect='EDGES', clamp_overlap=True)
        self._uv_cyl(t, r, Vector((0, 0, z)))
        self._merge(t, m, M @ Matrix.Translation((0, 0, z)))

    def _addbox(self, size, c, m, M):
        t = bmesh.new(); bmesh.ops.create_cube(t, size=1.0)
        for v in t.verts: v.co = Vector((v.co.x * size[0], v.co.y * size[1], v.co.z * size[2]))
        self._uv_box(t, Vector(c)); self._merge(t, m, M @ Matrix.Translation(c))

    def point(self, name, pos, axis=(0, 0, 1)):
        self.points.append((name, Vector(pos), Vector(axis)))

    # ---- mirror (left -> right)
    def mirrored(self, new_name):
        q = Part(new_name, self.group, self.note, self.fnote, side=new_name[-2:], stage=self.stage)
        q.bm = self._mirror_bm(self.bm); q.mats = list(self.mats)
        if getattr(self, "flip_u", False):
            uvl = q.bm.loops.layers.uv.get("UVMap")
            for f in q.bm.faces:
                for l in f.loops: l[uvl].uv = (-l[uvl].uv[0], l[uvl].uv[1])
        q.props = dict(self.props); q.collide = self.collide; q.tool = self.tool; q.real_tool = self.real_tool
        q.origin = mirror_v(self.origin) if self.origin is not None else None
        q.orient_axis = mirror_v(self.orient_axis) if self.orient_axis is not None else None
        q.moving_end = mirror_v(self.moving_end) if self.moving_end is not None else None
        q.bottom_point = mirror_v(self.bottom_point) if self.bottom_point is not None else None
        q.spin_axis = self.spin_axis
        q.hulls = [[mirror_v(p) for p in h] for h in self.hulls]
        q.points = [(n.replace(self.name, new_name), mirror_v(p), mirror_v(a)) for n, p, a in self.points]
        for fp, fd in zip(self.fparts, self.fast):
            g = Part(fp.name.replace(self.name, new_name), q.group, is_fastener=True, side=q.side)
            g.bm = self._mirror_bm(fp.bm); g.mats = list(fp.mats); g.tool, g.real_tool = fp.tool, fp.real_tool
            g.parent_part = q; q.fparts.append(g); FASTENERS.append(g)
            d = dict(fd); d["name"] = g.name
            d["position"] = [round(x, 4) for x in mirror_v(fd["position"])]; d["axis"] = [round(x, 4) for x in mirror_v(fd["axis"])]
            q.fast.append(d)
        return q

    @staticmethod
    def _mirror_bm(bm):
        b = bm.copy()
        bmesh.ops.scale(b, vec=(-1, 1, 1), verts=b.verts)
        bmesh.ops.reverse_faces(b, faces=b.faces)
        return b

# ------------------------------------------------------------------ finalize
def world_matrix(p, c):
    R = align_z(p.orient_axis) if p.orient_axis is not None else Matrix.Identity(4)
    return Matrix.Translation(c) @ R

def finalize(p, coll, parent=None):
    bm = p.bm; bm.normal_update()
    if len(bm.verts) == 0: return None
    lo = Vector([min(v.co[i] for v in bm.verts) for i in range(3)]); hi = Vector([max(v.co[i] for v in bm.verts) for i in range(3)])
    c = Vector(p.origin) if p.origin is not None else (lo + hi) / 2
    W = world_matrix(p, c)
    bmesh.ops.transform(bm, matrix=W.inverted(), verts=bm.verts)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    me = bpy.data.meshes.new(p.name); bm.to_mesh(me); bm.free()
    for mname in p.mats: me.materials.append(material(mname))
    for poly in me.polygons: poly.use_smooth = True
    try: me.set_sharp_from_angle(angle=math.radians(38))
    except Exception: pass
    if p.analytic: _analytic_normals(me, W, p.analytic)
    ob = bpy.data.objects.new(p.name, me); coll.objects.link(ob)
    if parent is not None:
        ob.parent = parent; ob.matrix_parent_inverse = Matrix.Identity(4)
        ob.matrix_basis = parent.matrix_basis.inverted() @ W
    else:
        ob.matrix_basis = W
    p.obj = ob; p.center = c; p.bbox = (lo, hi); p.W = W
    me.calc_loop_triangles(); p.tris = len(me.loop_triangles)
    return ob

def _analytic_normals(me, W, revs):
    """Exact surface-of-revolution normals on faces of boolean-cut revolves (removes smoothing streaks round the
    holes); every other face keeps its normal smoothing."""
    cur = [Vector(cn.vector) for cn in me.corner_normals]
    R = W.to_3x3(); Ri = R.inverted()
    def prof_normal(prof, r, a):
        k = len(prof); area = sum(prof[j][0] * prof[(j + 1) % k][1] - prof[(j + 1) % k][0] * prof[j][1] for j in range(k))
        best = (9.0, None)
        for j in range(k):
            (r0, a0), (r1, a1) = prof[j], prof[(j + 1) % k]
            dr, da = r1 - r0, a1 - a0; L2 = dr * dr + da * da
            if L2 < 1e-12: continue
            t = max(0.0, min(1.0, ((r - r0) * dr + (a - a0) * da) / L2))
            d = math.hypot(r - (r0 + t * dr), a - (a0 + t * da))
            if d < best[0]:
                L = math.sqrt(L2); n = (da / L, -dr / L) if area > 0 else (-da / L, dr / L)
                best = (d, n)
        return best
    out = list(cur)
    for poly in me.polygons:
        cw = W @ Vector(poly.center); nw = (R @ poly.normal).normalized()
        for c, ax, prof in revs:
            v = cw - c; a = v.dot(ax); rv = v - ax * a; r = rv.length
            d, n2 = prof_normal(prof, r, a)
            if d > 0.0004 or n2 is None or r < 1e-6: continue
            nf = (rv / r) * n2[0] + ax * n2[1]
            if nf.dot(nw) < 0.8: continue
            for li in poly.loop_indices:
                pw = W @ me.vertices[me.loops[li].vertex_index].co
                v = pw - c; a = v.dot(ax); rv = v - ax * a; r = rv.length
                if r < 1e-6: continue
                out[li] = (Ri @ ((rv / r) * n2[0] + ax * n2[1])).normalized()   # the face's profile segment, exact radial
            break
    me.normals_split_custom_set([tuple(n) for n in out])

def add_empty(name, parent_obj, pos, axis, coll, size=0.025, shape='SINGLE_ARROW'):
    e = bpy.data.objects.new(name, None); e.empty_display_type = shape; e.empty_display_size = size
    coll.objects.link(e); e.parent = parent_obj; e.matrix_parent_inverse = Matrix.Identity(4)
    e.matrix_basis = parent_obj.matrix_basis.inverted() @ (Matrix.Translation(Vector(pos)) @ align_z(axis))
    return e
