"""Stage 4b (body): preview Actions for every moving object, then re-export the body FBX with them."""
import bpy, sys, os
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import finish_lib as FL
B = os.path.join(ROOT, "export", "Octavia1U_Body_v2.blend")
bpy.ops.wm.open_mainfile(filepath=B)
O = bpy.data.objects; KIT = bpy.data.collections["BODY_KIT"]
for ob in KIT.objects:
    if ob.get("local_axis") == "hinge_line":
        ob["hinge_a_m"] = list(O[ob.name + "_HingeA"].matrix_world.translation); ob["hinge_b_m"] = list(O[ob.name + "_HingeB"].matrix_world.translation)
bpy.context.scene.frame_start, bpy.context.scene.frame_end = 1, 49
acts = [FL.preview_action(o) for o in KIT.objects if o.type == 'MESH' and o.get("motion") in ("rotate", "slide")]
print("actions", len([a for a in acts if a]))
bpy.ops.wm.save_as_mainfile(filepath=B, compress=True, relative_remap=True)
for o in bpy.data.objects: o.select_set(False)
for o in KIT.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Body_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, colors_type='SRGB')
print("exported")
