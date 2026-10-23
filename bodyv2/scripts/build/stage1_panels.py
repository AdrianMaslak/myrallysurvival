"""Stage 1: rebuild every paint panel of BODY_KIT as an even quad-dominant skin.

- outer skin (faces whose normal looks out of the car): isotropic remesh, every edge <= 4 cm, triangles paired into quads
- inner skins (door shuts, bonnet underside, inner frames): same mesher, ~13 cm, lighter
- Livery UVs are carried over from the prototype by surface lookup; UV0 is a world-scale box projection
- non-paint faces are kept, with their Livery copied and a fresh UV0
Writes work/stage1.blend (REFERENCE kept untouched).
"""
import bpy, bmesh, sys, os, time, math, json
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import isoremesh, classify, refine
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform

SRC = os.path.join(ROOT, "src", "Octavia1U_Body.blend")
OUT = os.path.join(ROOT, "work"); os.makedirs(OUT, exist_ok=True)
PAINT = "KIT_octavia1u"
LO, LI = 0.030, 0.20
LMIN, EPS, GROW = float(os.environ.get('LMIN', 0.012)), float(os.environ.get('EPS', 0.0015)), float(os.environ.get('GROW', 2.0))
DECIMATE = {"Door_FL": 0.42, "Door_FR": 0.42, "Door_RL": 0.42, "Door_RR": 0.42, "FrontBumper": 0.6}   # inner trim kept lighter
UV0_TILE = 0.5            # metres per UV0 unit on body panels

bpy.ops.wm.open_mainfile(filepath=SRC)
kit = [o for o in bpy.data.collections["BODY_KIT"].objects if o.type == 'MESH']
obstacles = classify.scene_tree(kit)
report = {}


def box_uv(co, n, tile=UV0_TILE):
    ax = max(range(3), key=lambda i: abs(n[i]))
    u, v = ((co.y, co.z) if ax == 0 else (co.x, co.z) if ax == 1 else (co.x, co.y))
    return (u / tile, v / tile)


def livery_sampler(bm_src):
    """Nearest-surface Livery lookup on the prototype faces (triangulated, world space)."""
    lay = bm_src.loops.layers.uv.get("Livery")
    tree = BVHTree.FromBMesh(bm_src); bm_src.faces.ensure_lookup_table()
    def sample(p):
        loc, n, i, d = tree.find_nearest(p)
        if i is None or lay is None: return (0.0, 0.0)
        f = bm_src.faces[i]; ls = list(f.loops)
        a, b, c = (l.vert.co for l in ls[:3]); ua, ub, uc = (l[lay].uv.to_3d() for l in ls[:3])
        r = barycentric_transform(loc, a, b, c, ua, ub, uc)
        return (r.x, r.y)
    return sample, tree


for ob in kit:
    t0 = time.time()
    me = ob.data
    src = bmesh.new(); src.from_mesh(me); src.transform(ob.matrix_world)
    bmesh.ops.triangulate(src, faces=src.faces)
    src.normal_update()
    mats = [m.name if m else "" for m in me.materials]
    pidx = mats.index(PAINT) if PAINT in mats else None
    paint = src.copy()
    bmesh.ops.delete(paint, geom=[f for f in paint.faces if f.material_index != pidx], context='FACES')
    paint.faces.ensure_lookup_table(); paint.normal_update()
    new = bmesh.new()
    uv0 = new.loops.layers.uv.new("UVMap"); uvl = new.loops.layers.uv.new("Livery")
    info = dict(src_tris=len(src.faces))
    if pidx is not None and len(paint.faces):
        flags = classify.outer_flags(paint.faces, obstacles)
        outer_bm = paint.copy(); outer_bm.faces.ensure_lookup_table()
        bmesh.ops.delete(outer_bm, geom=[f for f in outer_bm.faces if not flags[f.index]], context='FACES')
        otree = BVHTree.FromBMesh(outer_bm)
        rm = paint.copy(); rm.faces.ensure_lookup_table()
        fl_of = {f: flags[i] for i, f in enumerate(rm.faces)}
        refine.refine(rm, lambda f: 0.04 if fl_of.get(f, False) else 0.13)
        sample, ptree = livery_sampler(paint)
        vm = {v: new.verts.new(v.co) for v in rm.verts}
        outer_tag = []
        for f in rm.faces:
            try: nf = new.faces.new([vm[v] for v in f.verts])
            except ValueError: continue
            nf.material_index = pidx
            c = f.calc_center_median()
            for l in nf.loops:
                p = l.vert.co.lerp(c, 0.2)
                l[uvl].uv = sample(p); l[uv0].uv = box_uv(l.vert.co, f.normal)
        info.update(paint_src_tris=len(paint.faces), outer_src=sum(flags), paint_faces=len(rm.faces),
                    paint_quads=sum(1 for f in rm.faces if len(f.verts) == 4))
        rm.free(); outer_bm.free()
    # other materials: keep (decimated on the heavy door cards), copy Livery, fresh UV0
    if ob.name in DECIMATE:
        other = src.copy(); other.faces.ensure_lookup_table()
        bmesh.ops.delete(other, geom=[f for f in other.faces if f.material_index == pidx], context='FACES')
        tm = bpy.data.meshes.new("dec"); other.to_mesh(tm); other.free()
        to = bpy.data.objects.new("dec", tm); bpy.context.scene.collection.objects.link(to)
        md = to.modifiers.new("d", 'DECIMATE'); md.ratio = DECIMATE[ob.name]; md.use_collapse_triangulate = True
        dg = bpy.context.evaluated_depsgraph_get(); em = to.evaluated_get(dg).to_mesh()
        keep_bm = bmesh.new(); keep_bm.from_mesh(em); to.evaluated_get(dg).to_mesh_clear()
        bpy.data.objects.remove(to); bpy.data.meshes.remove(tm)
        # put the paint faces back so the loop below sees one source
        src2 = keep_bm
    else:
        src2 = None
    base = src2 if src2 is not None else src
    slay = base.loops.layers.uv.get("Livery")
    base.normal_update()
    keep = [f for f in base.faces if f.material_index != pidx or src2 is not None]
    vm2 = {}
    for f in keep:
        vs = []
        for v in f.verts:
            if v not in vm2: vm2[v] = new.verts.new(v.co)
            vs.append(vm2[v])
        try: nf = new.faces.new(vs)
        except ValueError: continue
        nf.material_index = f.material_index
        for l, lo in zip(nf.loops, f.loops):
            l[uvl].uv = lo[slay].uv if slay else (0, 0)
            l[uv0].uv = box_uv(l.vert.co, f.normal, 0.25)
    bmesh.ops.remove_doubles(new, verts=list(vm2.values()), dist=1e-5)
    new.transform(ob.matrix_world.inverted())
    nm = bpy.data.meshes.new(ob.name)
    new.to_mesh(nm); new.free(); src.free(); paint.free()
    for m in me.materials: nm.materials.append(m)
    for p in nm.polygons: p.use_smooth = True
    try: nm.set_sharp_from_angle(angle=math.radians(40))
    except Exception: pass
    old = ob.data; ob.data = nm; bpy.data.meshes.remove(old)
    nm.calc_loop_triangles(); info["tris"] = len(nm.loop_triangles)
    info["sec"] = round(time.time() - t0, 1)
    report[ob.name] = info
    print(ob.name, info, flush=True)

bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "stage1.blend"), compress=True)
json.dump(report, open(os.path.join(OUT, "stage1_report.json"), "w"), indent=1)
print("TOTAL tris", sum(r["tris"] for r in report.values()))
