"""Stage 6 (interior): materials, UV0, Grime, preview actions, export Octavia1U_Interior_v2.blend / .fbx."""
import bpy, bmesh, sys, os, math, json
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import finish_lib as FL
from mathutils import Vector
bpy.ops.wm.open_mainfile(filepath=os.path.join(ROOT, "work", "stage5.blend"))
KIT = bpy.data.collections["INTERIOR_KIT"]; TEX = os.path.join(ROOT, "export", "textures")
MAP = {"KIT_dooropen": "interior_plastic", "KIT_seatbelt": "interior_plastic", "KIT_octavia1u_buttons": "plastic_black",
       "KIT_octavia1u_chrome": "chrome", "KIT_octavia1u_dashboardcolor": "interior_plastic", "KIT_octavia1u_dashboardtop": "interior_plastic",
       "KIT_octavia1u_dials_3a": "plastic_black", "KIT_octavia1u_disgusingcoulor": "interior_plastic", "KIT_octavia1u_interiorvents": "plastic_black",
       "KIT_octavia1u_plastic": "interior_plastic", "KIT_octavia1u_silver": "chrome", "KIT_octavia1u_skodalogo": "chrome",
       "KIT_octavia1u_steer3a": "interior_plastic", "KIT_midsize_interior": "metal_black", "KIT_octavia1u_shifterM": "interior_plastic",
       "KIT_octavia1u_ebrake": "interior_plastic", "KIT_octavia1u_armrest": "seat_fabric", "KIT_octavia1u_seats": "seat_fabric",
       "KIT_octavia1u_intercarpet": "carpet", "KIT_octavia1u_headliner": "headliner_cloth", "KIT_octavia1u_interiorlight": "lens_clear"}
meshes = [o for o in KIT.objects if o.type == 'MESH']
for ob in meshes:
    me = ob.data
    for i, s in enumerate(me.materials):
        nm = s.name if s else ""
        tgt = MAP.get(nm, nm[2:] if nm.startswith("M_") else "interior_plastic")
        me.materials[i] = FL.make_material(tgt, TEX)
    names = [m.name for m in me.materials]; first = {}
    for i, n in enumerate(names): first.setdefault(n, i)
    for p in me.polygons: p.material_index = first[names[p.material_index]]
    for l in [l for l in me.uv_layers if l.name != "UVMap"]: me.uv_layers.remove(l)
    FL.box_uv(ob)
    for p in me.polygons: p.use_smooth = True
    try: me.set_sharp_from_angle(angle=math.radians(40))
    except Exception: pass
def g_cabin(c):
    g = 0.0
    if c.z < 0.50: g = 0.45 * (0.50 - c.z) / 0.20          # floor, pedals, seat bases
    return g
for ob in meshes:
    FL.grime(ob, (lambda c: 0.55 if ob.name.startswith(("Pedal", "Footrest")) else g_cabin(c)) if ob.name not in ("GaugeFaces", "WarningLights") else (lambda c: 0.0))
bpy.context.scene.frame_start, bpy.context.scene.frame_end = 1, 49
acts = [FL.preview_action(o) for o in KIT.objects if o.type == 'MESH' and o.get("motion") in ("rotate", "slide")]
print("actions", len([a for a in acts if a]))
tris = {}
for ob in meshes: ob.data.calc_loop_triangles(); tris[ob.name] = len(ob.data.loop_triangles)
json.dump(tris, open(os.path.join(ROOT, "work", "interior_tris.json"), "w"), indent=1)
print("interior tris", sum(tris.values()))
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(ROOT, "export", "Octavia1U_Interior_v2.blend"), compress=True, relative_remap=True)
for o in bpy.data.objects: o.select_set(False)
for o in KIT.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Interior_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, colors_type='SRGB')
print("exported interior")
