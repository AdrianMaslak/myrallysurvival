"""Stage 9 (body): 4x4 propshaft clearance. The front wall of the final-drive pocket hung down to z 0.327 right where the
propshaft's rear CV joint (r 45 mm, top z 0.340) passes at y -1.01..-1.06. Lift the Chassis underside locally over the
joint: z in 0.30..0.36 is remapped to 0.3555..0.36 (order kept), with a 25 mm falloff round the core so nothing tears.
Final drive, tyres and every other part only gain clearance. Saves the blend and re-exports the body FBX (actions kept)."""
import bpy, os, math
from mathutils import Vector
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = os.path.join(ROOT, "export", "Octavia1U_Body_v2.blend")
bpy.ops.wm.open_mainfile(filepath=B)
KIT = bpy.data.collections["BODY_KIT"]
ch = bpy.data.objects["Chassis"]; M = ch.matrix_world; Mi = M.inverted()
CORE = dict(x=0.060, y0=-1.070, y1=-0.995); FALL = 0.025
def w_of(p):
    dx = max(0.0, abs(p.x) - CORE["x"]); dy = max(0.0, CORE["y0"] - p.y, p.y - CORE["y1"])
    d = math.hypot(dx, dy)
    if d >= FALL: return 0.0
    t = 1 - d / FALL; return t * t * (3 - 2 * t)
moved = 0; dmax = 0.0
for v in ch.data.vertices:
    p = M @ v.co
    if not (0.300 <= p.z < 0.360): continue
    w = w_of(p)
    if w <= 0: continue
    zt = 0.3555 + (p.z - 0.300) * (0.360 - 0.3555) / 0.060
    nz = p.z + w * (zt - p.z)
    if nz > p.z + 1e-6:
        dmax = max(dmax, nz - p.z); v.co = Mi @ Vector((p.x, p.y, nz)); moved += 1
ch.data.update()
print("chassis vertices lifted", moved, "max mm", round(dmax * 1000, 1))
bpy.ops.wm.save_as_mainfile(filepath=B, compress=True, relative_remap=True)
for o in bpy.data.objects: o.select_set(False)
for o in KIT.objects: o.select_set(True)
bpy.ops.export_scene.fbx(filepath=os.path.join(ROOT, "export", "Octavia1U_Body_v2.fbx"), use_selection=True,
                         object_types={'MESH', 'EMPTY'}, apply_unit_scale=True, apply_scale_options='FBX_SCALE_UNITS',
                         axis_forward='-Z', axis_up='Y', use_custom_props=True, mesh_smooth_type='FACE', path_mode='RELATIVE',
                         add_leaf_bones=False, bake_anim=True, bake_anim_use_all_actions=True, bake_anim_use_nla_strips=False, colors_type='SRGB')
print("exported")
