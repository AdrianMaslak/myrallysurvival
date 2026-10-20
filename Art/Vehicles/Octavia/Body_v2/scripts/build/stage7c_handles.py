"""Stage 7c (body): rebuild the four interior door-pull meshes so the lever lies fore-aft on the door card (stage 3 had its
long axis across the car). Origin, rotation, rig properties and preview actions are unchanged; only the mesh is rebuilt,
in world space, then brought into the object's local frame. Then save + FBX."""
import bpy, bmesh, os
from mathutils import Vector, Matrix
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(ROOT, "export", "Octavia1U_Body_v2.blend")
bpy.ops.wm.open_mainfile(filepath=B)
O = bpy.data.objects; KIT = bpy.data.collections["BODY_KIT"]
bpy.context.scene.frame_set(1)
for s in ("FL", "FR", "RL", "RR"):
    h = O["DoorHandle_" + s]
    sx = -1 if s[1] == "L" else 1; inw = -sx; f = 1 if s[0] == "F" else -1
    o = h.matrix_world.translation.copy()
    print(s, "local Z in world", tuple(round(x, 3) for x in (h.matrix_world.to_3x3() @ Vector((0, 0, 1))).normalized()))
    b = bmesh.new()
    # mount plate on the card (centred on the origin, 4 mm proud of the card)
    bmesh.ops.create_cube(b, size=1.0, matrix=Matrix.Translation(o) @ Matrix.Diagonal((0.008, 0.03, 0.02, 1)))
    # lever: runs from the pivot towards the door's other end, a finger gap off the card
    bmesh.ops.create_cube(b, size=1.0, matrix=Matrix.Translation(o + Vector((inw * 0.010, -0.055 * f, 0.0))) @ Matrix.Diagonal((0.012, 0.11, 0.022, 1)))
    b.transform(h.matrix_world.inverted())
    b.to_mesh(h.data); b.free()
    vs = [h.matrix_world @ v.co for v in h.data.vertices]
    print(s, "x", round(min(v.x for v in vs), 3), round(max(v.x for v in vs), 3), "y", round(min(v.y for v in vs), 3), round(max(v.y for v in vs), 3))
bpy.ops.wm.save_as_mainfile(filepath=B, compress=True, relative_remap=True)
for ob in bpy.data.objects: ob.select_set(False)
for ob in KIT.objects: ob.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Body_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, colors_type='SRGB')
print("exported")
