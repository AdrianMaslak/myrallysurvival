"""Stage 9b (body): clearance to the rear suspension through its travel. Applies the vertex pushes computed by
octavia_drive/work/body_push.py (rear links, driveshafts, trailing arms, hub parts and the twist beam posed at -8..+8 cm,
13 mm target, max 30 mm, mostly upward). Only the underbody round the rear axle moves. Saves + re-exports the body FBX."""
import bpy, os, json
from mathutils import Vector
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(ROOT, "export", "Octavia1U_Body_v2.blend")
P = json.load(open("/home/claude/octavia_drive/work/body_push.json"))
bpy.ops.wm.open_mainfile(filepath=B)
KIT = bpy.data.collections["BODY_KIT"]
ch = bpy.data.objects["Chassis"]; M = ch.matrix_world; Mi = M.inverted()
for k, d in P.items():
    v = ch.data.vertices[int(k)]; v.co = Mi @ (M @ v.co + Vector(d))
ch.data.update()
print("moved", len(P))
bpy.ops.wm.save_as_mainfile(filepath=B, compress=True, relative_remap=True)
for o in bpy.data.objects: o.select_set(False)
for o in KIT.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Body_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, colors_type='SRGB')
print("exported")
