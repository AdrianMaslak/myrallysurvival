"""Stage 7b (body): replace any NaN Livery UV (degenerate lookup) by the UV of a valid loop on the same vertex, then export the FBX."""
import bpy, os, math
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(ROOT, "export", "Octavia1U_Body_v2.blend")
bpy.ops.wm.open_mainfile(filepath=B)
KIT = bpy.data.collections["BODY_KIT"]
for ob in KIT.objects:
    if ob.type != 'MESH': continue
    me = ob.data
    for uvl in me.uv_layers:
        bad = [i for i, d in enumerate(uvl.data) if not (math.isfinite(d.uv.x) and math.isfinite(d.uv.y))]
        if not bad: continue
        good = {}
        for i, lp in enumerate(me.loops):
            d = uvl.data[i].uv
            if math.isfinite(d.x) and math.isfinite(d.y): good.setdefault(lp.vertex_index, tuple(d))
        for i in bad: uvl.data[i].uv = good.get(me.loops[i].vertex_index, (0.0, 0.0))
        print(ob.name, uvl.name, "fixed", len(bad))
# wipers: lift any blade/arm vertex that sits in or within 2 mm of the windscreen out along the glass normal
from mathutils.bvhtree import BVHTree
import bmesh
ws = bpy.data.objects["Windshield"]; t = bmesh.new(); t.from_mesh(ws.data); t.transform(ws.matrix_world); wt = BVHTree.FromBMesh(t)
for nm in ("Wiper_L", "Wiper_R"):
    w = bpy.data.objects[nm]; M = w.matrix_world; Mi = M.inverted(); n_moved = 0
    for v in w.data.vertices:
        p = M @ v.co; loc, nrm, i, d = wt.find_nearest(p, 0.03)
        if loc is None: continue
        if nrm.z < 0: nrm = -nrm
        h = (p - loc).dot(nrm)
        if h < 0.002: v.co = Mi @ (p + nrm * (0.002 - h)); n_moved += 1
    print(nm, "vertices lifted off the glass", n_moved)
t.free()
bpy.ops.wm.save_as_mainfile(filepath=B, compress=True, relative_remap=True)
for o in bpy.data.objects: o.select_set(False)
for o in KIT.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Body_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, colors_type='SRGB')
print("exported")
