"""Stage 9e (body): open the trailing-arm pockets from below. The stage-2 clearance skin had wrapped a thin sheet of floor
under the front of each trailing arm (z 0.28-0.345); at full droop the 4x4 arm dips onto it and the front-drive twist
beam sits just below it. Downward-facing Chassis faces there that have the arm right above them are deleted, so the
arm hangs below the body in an open recess as on the real car. Saves + re-exports the body FBX."""
import bpy, bmesh, os
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(ROOT, "export", "Octavia1U_Body_v2.blend")
bpy.ops.wm.open_mainfile(filepath="/home/claude/octavia_susp/export/Octavia1U_Suspension.blend")
arms = []
for n in ("TrailingArm_RL", "TrailingArm_RR"):
    o = bpy.data.objects[n]; bm = bmesh.new(); bm.from_mesh(o.data); bm.transform(o.matrix_world); arms.append(BVHTree.FromBMesh(bm)); bm.free()
bpy.ops.wm.open_mainfile(filepath=B)
KIT = bpy.data.collections["BODY_KIT"]
ch = bpy.data.objects["Chassis"]
bm = bmesh.new(); bm.from_mesh(ch.data); M = ch.matrix_world; R = M.to_3x3()
kill = []
for f in bm.faces:
    ws = [M @ v.co for v in f.verts]
    if not all(0.45 < abs(w.x) < 0.60 and -1.00 < w.y < -0.79 and 0.27 < w.z < 0.350 for w in ws): continue
    n = (R @ f.normal).normalized()
    if n.z > -0.3: continue
    c = sum(ws, Vector()) / len(ws)
    if any(t.ray_cast(c + Vector((0, 0, 0.001)), Vector((0, 0, 1)), 0.12)[0] is not None for t in arms): kill.append(f)
bmesh.ops.delete(bm, geom=kill, context='FACES')
loose = [v for v in bm.verts if not v.link_faces]
if loose: bmesh.ops.delete(bm, geom=loose, context='VERTS')
bm.to_mesh(ch.data); bm.free(); ch.data.update()
print("pocket faces removed", len(kill))
bpy.ops.wm.save_as_mainfile(filepath=B, compress=True, relative_remap=True)
for o in bpy.data.objects: o.select_set(False)
for o in KIT.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Body_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, colors_type='SRGB')
print("exported")
