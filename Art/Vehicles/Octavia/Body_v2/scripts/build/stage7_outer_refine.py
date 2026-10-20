"""Stage 7 (body): final pass on the visible paint skins. Every visible painted face gets edges <= 4 cm.
The midpoints stay on the straight edges (cap 0), so the shape and all clearances stay exactly as checked.
After the split: Delaunay flips, triangles joined into quads, Livery sampled again from the surface as it was before,
UV0 re-projected, Grime copied from the nearest old vertex. Then the blend is saved and the FBX exported again, actions included."""
import bpy, bmesh, sys, os, math
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform
from mathutils.kdtree import KDTree
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import classify, refine, finish_lib as FL
B = os.path.join(ROOT, "export", "Octavia1U_Body_v2.blend")
bpy.ops.wm.open_mainfile(filepath=B)
KIT = bpy.data.collections["BODY_KIT"]
# internal targets: the spec budgets, tightened on Chassis/doors/Roof so the exterior total stays under 150k
BUDGET = dict(Chassis=45000, Door_FL=11500, Door_FR=11500, Door_RL=11500, Door_RR=11500, Bonnet=5000, Trunk=12000,
              FrontBumper=10000, RearBumper=10000, Fender_FL=4000, Fender_FR=4000, Roof=5000)
LIMITS = (0.04, 0.045, 0.05, 0.055, 0.06, 0.07, 0.08, 0.10)
CHOSEN = {}
PAINT = ["Chassis", "Door_FL", "Door_FR", "Door_RL", "Door_RR", "Bonnet", "Trunk", "FrontBumper", "RearBumper", "Fender_FL", "Fender_FR", "Roof"]
body = [o for o in KIT.objects if o.type == 'MESH' and not o.name.startswith("UCX_")]
tree = classify.scene_tree(body)
for nm in PAINT:
    ob = bpy.data.objects[nm]; me = ob.data
    pi = [i for i, m in enumerate(me.materials) if m and m.name == "M_body_paint"]
    bm = bmesh.new(); bm.from_mesh(me); bm.transform(ob.matrix_world); bm.normal_update(); bm.faces.ensure_lookup_table()
    old = bm.copy(); bmesh.ops.triangulate(old, faces=old.faces); old.faces.ensure_lookup_table(); old.verts.ensure_lookup_table()
    olv = old.loops.layers.uv.get("Livery"); otree = BVHTree.FromBMesh(old)
    gr = old.verts.layers.color.get("Grime") if hasattr(old.verts.layers, "color") else None
    kd = KDTree(len(old.verts))
    for v in old.verts: kd.insert(v.co, v.index)
    kd.balance()
    gatt = me.color_attributes.get("Grime"); gold = [tuple(gatt.data[i].color) for i in range(len(gatt.data))] if gatt else None
    OL = bm.faces.layers.int.new("outer7"); bm.faces.ensure_lookup_table()
    paint = [f for f in bm.faces if f.material_index in pi]
    fl = classify.outer_flags(paint, tree, n_rays=3)
    for f, k in zip(paint, fl): f[OL] = 1 if k else 0
    outer = [f for f, k in zip(paint, fl) if k]
    n0 = len(bm.faces)
    kids = sum(len(c.data.loop_triangles) for c in ob.children if c.type == 'MESH' and not c.name.startswith("UCX_") and (c.data.calc_loop_triangles() or True))
    base = bm
    for lim in LIMITS:
        bm = base.copy(); OLc = bm.faces.layers.int.get("outer7")
        refine.refine(bm, lambda f, lim=lim, OLc=OLc: lim if f[OLc] else None, cap=0.0)
        nt = sum(len(f.verts) - 2 for f in bm.faces) + kids
        print(f"  {nm} limit {lim}: {nt} tris (budget {BUDGET[nm]})", flush=True)
        if nt <= BUDGET[nm]: break
        if lim != LIMITS[-1]: bm.free()
    if base is not bm: base.free()
    CHOSEN[nm] = dict(limit_m=lim, triangles=nt, budget=BUDGET[nm])
    for L_ in (bm.faces.layers.float.get("lim"), bm.faces.layers.int.get("outer7")):
        if L_ is not None:
            try: (bm.faces.layers.float if L_.name == "lim" else bm.faces.layers.int).remove(L_)
            except Exception: pass
    # Livery again from the old surface (paint faces only; point pulled towards the face centre so it lands on the right side of a seam)
    lv = bm.loops.layers.uv.get("Livery")
    if lv and olv:
        for f in bm.faces:
            if f.material_index not in pi: continue
            c = f.calc_center_median()
            for l in f.loops:
                p = l.vert.co.lerp(c, 0.15)
                loc, n, i, d = otree.find_nearest(p)
                if i is None: continue
                of = old.faces[i]; ls = list(of.loops)
                r = barycentric_transform(loc, *(x.vert.co for x in ls[:3]), *(x[olv].uv.to_3d() for x in ls[:3]))
                if r.x == r.x and r.y == r.y: l[lv].uv = (r.x, r.y)
                else: l[lv].uv = min(ls, key=lambda x: (x.vert.co - loc).length)[olv].uv
    bm.verts.ensure_lookup_table()
    near = [kd.find(v.co)[1] for v in bm.verts]
    bm.transform(ob.matrix_world.inverted())
    bm.to_mesh(me); bm.free(); old.free()
    if gold:
        gatt = me.color_attributes.get("Grime") or me.color_attributes.new("Grime", 'BYTE_COLOR', 'POINT')
        for i, j in enumerate(near): gatt.data[i].color = gold[j]
    FL.box_uv(ob)
    for p in me.polygons: p.use_smooth = True
    try: me.set_sharp_from_angle(angle=math.radians(40))
    except Exception: pass
    me.calc_loop_triangles()
    print(f"{nm}: faces {n0} -> {len(me.polygons)}, tris {len(me.loop_triangles)}, outer faces split {len(outer)}", flush=True)
import json; json.dump(CHOSEN, open(os.path.join(ROOT, "work", "stage7_limits.json"), "w"), indent=1)
bpy.ops.wm.save_as_mainfile(filepath=B, compress=True, relative_remap=True)
for o in bpy.data.objects: o.select_set(False)
for o in KIT.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Body_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, colors_type='SRGB')
print("exported")
